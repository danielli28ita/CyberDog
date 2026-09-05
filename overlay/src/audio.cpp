#include "overlay/audio.h"

#include <xaudio2.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace pet::win {
namespace {

constexpr ULONGLONG kIdleReleaseMs = 60000;      // 静默 60 秒释放引擎
constexpr ULONGLONG kMischiefCooldownMs = 300000; // 捣乱音效冷却 5 分钟
constexpr unsigned  kMischiefPerHour = 6;

}  // namespace

Audio::~Audio() { release(); }

namespace {

// 从 exe 资源里读一段 WAV（RCDATA，资源号 kSoundResourceBase + SoundId），转成 kSampleRate 的 16 位单声道。
// 只认 PCM 8/16 位、1 或 2 声道；别的格式当没有。没有这个资源就返回 false，调用方退回程序合成。
// 2.0 起录音素材走这条路（作者要求换真实音效），配方合成仍是兜底。
constexpr int kSoundResourceBase = 200;

bool load_wav_resource(int id, PcmClip& out) {
    HRSRC h = FindResourceW(nullptr, MAKEINTRESOURCEW(kSoundResourceBase + id), RT_RCDATA);
    if (!h) return false;
    HGLOBAL g = LoadResource(nullptr, h);
    const auto* p = static_cast<const std::uint8_t*>(LockResource(g));
    const DWORD n = SizeofResource(nullptr, h);
    if (!p || n < 44 || std::memcmp(p, "RIFF", 4) != 0 || std::memcmp(p + 8, "WAVE", 4) != 0) return false;
    auto rd32 = [&](size_t o) { return static_cast<std::uint32_t>(p[o]) | (p[o + 1] << 8) | (p[o + 2] << 16) | (static_cast<std::uint32_t>(p[o + 3]) << 24); };
    auto rd16 = [&](size_t o) { return static_cast<std::uint16_t>(p[o] | (p[o + 1] << 8)); };
    size_t pos = 12;
    int channels = 0, rate = 0, bits = 0, format = 0;
    const std::uint8_t* data = nullptr;
    std::uint32_t dataLen = 0;
    while (pos + 8 <= n) {
        const std::uint32_t len = rd32(pos + 4);
        if (std::memcmp(p + pos, "fmt ", 4) == 0 && len >= 16) {
            format = rd16(pos + 8); channels = rd16(pos + 10); rate = static_cast<int>(rd32(pos + 12)); bits = rd16(pos + 22);
        } else if (std::memcmp(p + pos, "data", 4) == 0) {
            data = p + pos + 8;
            dataLen = (std::min)(len, static_cast<std::uint32_t>(n - pos - 8));
        }
        pos += 8 + len + (len & 1);
    }
    if (!data || format != 1 || channels < 1 || channels > 2 || rate <= 0 || (bits != 8 && bits != 16)) return false;
    const size_t frameBytes = static_cast<size_t>(channels) * (bits / 8);
    const size_t frames = dataLen / frameBytes;
    auto sample = [&](size_t f) -> float {
        float s = 0.0f;
        for (int c = 0; c < channels; ++c) {
            const std::uint8_t* q = data + f * frameBytes + static_cast<size_t>(c) * (bits / 8);
            s += bits == 16 ? static_cast<float>(static_cast<std::int16_t>(q[0] | (q[1] << 8))) / 32768.0f
                            : (static_cast<float>(q[0]) - 128.0f) / 128.0f;
        }
        return s / static_cast<float>(channels);
    };
    const size_t outFrames = static_cast<size_t>(static_cast<double>(frames) * kSampleRate / rate);
    out.samples.resize(outFrames);
    for (size_t i = 0; i < outFrames; ++i) {
        const double src = static_cast<double>(i) * rate / kSampleRate;
        const size_t a = static_cast<size_t>(src);
        const float t = static_cast<float>(src - static_cast<double>(a));
        const float v = a + 1 < frames ? sample(a) * (1.0f - t) + sample(a + 1) * t : sample((std::min)(a, frames - 1));
        out.samples[i] = static_cast<std::int16_t>(v * 30000.0f);
    }
    return !out.samples.empty();
}

}  // namespace

void Audio::prepare(float pitch, std::uint64_t seed) {
    for (int i = 0; i < static_cast<int>(SoundId::Count); ++i) {
        if (load_wav_resource(i, clips_[i])) { ++fromResource_; continue; }
        clips_[i] = synthesize(static_cast<SoundId>(i), pitch, seed + static_cast<std::uint64_t>(i) * 977u);
    }
    std::printf("  音效：%u 段用 exe 里的录音，%d 段程序合成\n", fromResource_,
                static_cast<int>(SoundId::Count) - static_cast<int>(fromResource_));
}

bool Audio::ensure_engine() {
    if (engine_) return true;
    if (!comInit_) {
        // XAudio2 需要 COM 初始化。主线程若已初始化会返回 S_FALSE，同样算成功。
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        comInit_ = SUCCEEDED(hr);
    }
    HRESULT hr = XAudio2Create(&engine_, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) {
        std::printf("  [WARN] XAudio2Create 失败 hr=0x%08lX，本次静音\n", static_cast<unsigned long>(hr));
        engine_ = nullptr;
        return false;
    }
    hr = engine_->CreateMasteringVoice(&master_);
    if (FAILED(hr)) {
        std::printf("  [WARN] CreateMasteringVoice 失败 hr=0x%08lX（没有音频设备？）\n",
                    static_cast<unsigned long>(hr));
        engine_->Release();
        engine_ = nullptr;
        return false;
    }
    master_->SetVolume(volume_);

    WAVEFORMATEX wf{};
    wf.wFormatTag      = WAVE_FORMAT_PCM;
    wf.nChannels       = 1;
    wf.nSamplesPerSec  = kSampleRate;
    wf.wBitsPerSample  = 16;
    wf.nBlockAlign     = 2;
    wf.nAvgBytesPerSec = kSampleRate * 2;
    for (int i = 0; i < static_cast<int>(SoundId::Count); ++i) {
        if (FAILED(engine_->CreateSourceVoice(&voices_[i], &wf))) voices_[i] = nullptr;
    }
    return true;
}

void Audio::release() {
    for (auto& v : voices_) {
        if (v) { v->Stop(); v->DestroyVoice(); v = nullptr; }
    }
    if (master_) { master_->DestroyVoice(); master_ = nullptr; }
    if (engine_) { engine_->Release(); engine_ = nullptr; }
}

bool Audio::quiet_now() {
    // 系统本地时间 22:00 到 08:00 之间不发声。作者要求，无开关。
    const std::time_t t = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &t);
    return local.tm_hour >= 22 || local.tm_hour < 8;
}

bool Audio::play(SoundId id, bool mischief) {
    const int i = static_cast<int>(id);
    if (!enabled_ || quiet_now() || clips_[i].samples.empty()) { ++suppressed_; return false; }

    const ULONGLONG now = GetTickCount64();
    if (mischief) {
        if (mischiefHourStartMs_ == 0 || now - mischiefHourStartMs_ >= 3600000) {
            mischiefHourStartMs_ = now;
            mischiefThisHour_ = 0;
        }
        if (now - lastMischiefMs_ < kMischiefCooldownMs || mischiefThisHour_ >= kMischiefPerHour) {
            ++suppressed_;
            return false;
        }
    }
    // 同一段不连续重复（出声频率高的脚步声除外）。
    if (i == lastId_ && id != SoundId::Step && id != SoundId::Bounce && now - lastPlayMs_ < 2000) {
        ++suppressed_;
        return false;
    }

    if (!ensure_engine() || !voices_[i]) { ++suppressed_; return false; }

    IXAudio2SourceVoice* v = voices_[i];
    v->Stop();
    v->FlushSourceBuffers();
    XAUDIO2_BUFFER buf{};
    buf.pAudioData = reinterpret_cast<const BYTE*>(clips_[i].samples.data());
    buf.AudioBytes = static_cast<UINT32>(clips_[i].samples.size() * sizeof(std::int16_t));
    buf.Flags      = XAUDIO2_END_OF_STREAM;
    if (FAILED(v->SubmitSourceBuffer(&buf)) || FAILED(v->Start())) { ++suppressed_; return false; }

    lastPlayMs_ = now;
    lastId_ = i;
    if (mischief) { lastMischiefMs_ = now; ++mischiefThisHour_; }
    ++played_;
    return true;
}

void Audio::tick(ULONGLONG nowMs) {
    if (engine_ && nowMs - lastPlayMs_ > kIdleReleaseMs) release();
}

}  // namespace pet::win
