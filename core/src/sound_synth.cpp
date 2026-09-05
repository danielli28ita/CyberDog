#include "core/sound_synth.h"
#include "core/math3d.h"
#include "core/rng.h"

#include <cmath>

namespace pet {
namespace {

constexpr float kTwoPi = 6.2831853f;
using K = SoundLayer::Kind;

// 简写构造。
constexpr SoundLayer tone(float start, float len, float f0, float f1, float gain, int harm,
                          float attack, float release, float vibHz = 0, float vibDepth = 0) {
    SoundLayer l;
    l.kind = K::Tone; l.start = start; l.len = len; l.f0 = f0; l.f1 = f1; l.gain = gain;
    l.harmonics = harm; l.attack = attack; l.release = release; l.vibHz = vibHz; l.vibDepth = vibDepth;
    return l;
}
constexpr SoundLayer noise(float start, float len, float cutoff, float gain,
                           float attack, float release, float raw = 0) {
    SoundLayer l;
    l.kind = K::Noise; l.start = start; l.len = len; l.f0 = cutoff; l.gain = gain;
    l.attack = attack; l.release = release; l.raw = raw;
    return l;
}

// ---- 配方表。加一段声音 = 加一行，再在 SoundId 末尾加一个名字 ----
constexpr SoundRecipe kRecipes[] = {
    // CyberDog 1.0 全表重写，方向是「可爱、轻柔」（作者要求）：音高整体偏高、起音软、
    // 尾音长、谐波少、噪声只留一点点气声，整段音量 level 压在 0.25–0.55。
    // 汪：小狗的短促「汪」，两个音节（汪-汪），第二声略低。
    {SoundId::Bark, "叫", 0.62f, {
        tone(0.00f, 0.20f, 820, 560, 0.8f, 3, 0.010f, 0.11f, 28, 0.02f),
        tone(0.30f, 0.22f, 760, 500, 0.7f, 3, 0.010f, 0.13f, 28, 0.02f),
        noise(0.00f, 0.14f, 6500, 0.05f, 0.005f, 0.08f, 0.6f)}, 0.5f},
    // 哼唧：细细的、往上飘再落下来，像小狗撒娇。
    {SoundId::Whimper, "哼唧", 1.1f, {
        tone(0.00f, 0.45f, 700, 1050, 0.5f, 2, 0.10f, 0.15f, 5.0f, 0.04f),
        tone(0.48f, 0.55f, 1000, 640, 0.45f, 2, 0.08f, 0.30f, 5.0f, 0.04f)}, 0.4f},
    // 舒服的哼哼：低而柔，慢颤音，像被摸时的咕噜。
    {SoundId::Happy, "哼哼", 1.1f, {
        tone(0.00f, 1.0f, 260, 300, 0.5f, 2, 0.25f, 0.40f, 3.5f, 0.05f)}, 0.35f},
    // 喘气：三口小小的气，很轻。
    {SoundId::Pant, "喘气", 1.3f, {
        noise(0.00f, 0.26f, 1400, 0.4f, 0.08f, 0.15f), noise(0.42f, 0.26f, 1400, 0.4f, 0.08f, 0.15f),
        noise(0.84f, 0.26f, 1400, 0.35f, 0.08f, 0.15f)}, 0.3f},
    // 网球落地：软一点的「咚」。
    {SoundId::Bounce, "球落地", 0.24f, {
        tone(0.00f, 0.16f, 200, 130, 0.9f, 2, 0.006f, 0.13f)}, 0.45f},
    // 碗翻倒：轻轻的几下「叮」，像小瓷碗。
    {SoundId::Clatter, "碗翻倒", 0.8f, {
        tone(0.00f, 0.22f, 1800, 1650, 0.5f, 3, 0.004f, 0.19f), tone(0.16f, 0.22f, 1650, 1500, 0.4f, 3, 0.004f, 0.19f),
        tone(0.34f, 0.24f, 1500, 1350, 0.3f, 3, 0.004f, 0.21f)}, 0.35f},
    // 抖身：三下柔和的「扑扑」。
    {SoundId::Shake, "抖身", 0.6f, {
        noise(0.00f, 0.10f, 320, 0.5f, 0.025f, 0.07f), noise(0.18f, 0.10f, 320, 0.42f, 0.025f, 0.07f),
        noise(0.36f, 0.10f, 320, 0.35f, 0.025f, 0.08f)}, 0.3f},
    // 脚步：留着配方但走路时不触发（1.0 起不出脚步声）。
    {SoundId::Step, "脚步", 0.1f, {
        noise(0.00f, 0.07f, 500, 0.5f, 0.01f, 0.05f)}, 0.2f},
    // 闻：两下很轻的吸气。
    {SoundId::Sniff, "闻", 0.5f, {
        noise(0.00f, 0.12f, 2600, 0.45f, 0.04f, 0.07f, 0.3f), noise(0.22f, 0.12f, 2800, 0.45f, 0.04f, 0.07f, 0.3f)}, 0.3f},
    // 轻叫：一声小小的「汪」，比 Bark 高一点、轻一半。
    {SoundId::BarkSoft, "轻叫", 0.30f, {
        tone(0.00f, 0.22f, 880, 620, 0.5f, 3, 0.015f, 0.12f, 24, 0.02f)}, 0.35f},
    // 尿尿：细水声，很轻。
    {SoundId::Pee, "尿尿", 2.6f, {
        noise(0.00f, 2.5f, 2400, 0.35f, 0.40f, 0.70f, 0.1f)}, 0.25f},
    // 踢球：软一点的「噗」加一点点破空。
    {SoundId::Kick, "踢球", 0.4f, {
        tone(0.00f, 0.14f, 160, 110, 0.9f, 2, 0.006f, 0.12f), noise(0.00f, 0.30f, 2600, 0.25f, 0.01f, 0.26f)}, 0.4f},
    // 打呼：轻、柔、慢，音量最小。
    {SoundId::Snore, "打呼", 1.5f, {
        tone(0.00f, 1.35f, 92, 104, 0.4f, 2, 0.45f, 0.60f, 12, 0.10f), noise(0.00f, 1.35f, 400, 0.15f, 0.45f, 0.60f)}, 0.25f},
};
static_assert(sizeof(kRecipes) / sizeof(kRecipes[0]) == static_cast<size_t>(SoundId::Count),
              "配方表与 SoundId 数量不一致");

// 简单的合成器骨架：一个浮点缓冲，最后转 16 位并做峰值归一。
struct Synth {
    std::vector<float> buf;
    Rng rng;
    float pitch;

    Synth(float seconds, float p, std::uint64_t seed)
        : buf(static_cast<size_t>(seconds * kSampleRate), 0.0f), rng(seed), pitch(p) {}

    float t(size_t i) const { return static_cast<float>(i) / kSampleRate; }

    // 包络的起落用半余弦，不用直线：直线的拐角听起来像「咔」，这是 1.6 及以前音效发急的原因之一。
    static float smooth(float u) { return 0.5f - 0.5f * std::cos(u * 3.14159265f); }
    static float env(float x, float total, float a, float r) {
        if (x < 0 || x > total) return 0.0f;
        if (x < a) return smooth(x / a);
        if (x > total - r) return smooth((total - x) / r);
        return 1.0f;
    }

    void add_noise(const SoundLayer& L) {
        float y = 0.0f;
        const float a = clampf(kTwoPi * L.f0 / kSampleRate, 0.0f, 1.0f);
        for (size_t i = 0; i < buf.size(); ++i) {
            const float x = t(i) - L.start;
            if (x < 0 || x > L.len) continue;
            const float n = rng.unit() * 2.0f - 1.0f;
            y += a * (n - y);
            buf[i] += (y * (1.0f - L.raw) + n * L.raw) * L.gain * env(x, L.len, L.attack, L.release);
        }
    }

    void add_tone(const SoundLayer& L) {
        float phase = 0.0f;
        for (size_t i = 0; i < buf.size(); ++i) {
            const float x = t(i) - L.start;
            if (x < 0 || x > L.len) continue;
            const float u = x / L.len;
            const float f = (L.f0 + (L.f1 - L.f0) * u) * pitch *
                            (1.0f + L.vibDepth * std::sin(kTwoPi * L.vibHz * x));
            phase += kTwoPi * f / kSampleRate;
            float s = 0.0f;
            for (int h = 1; h <= L.harmonics; ++h) s += std::sin(phase * h) / static_cast<float>(h);
            buf[i] += s * L.gain * env(x, L.len, L.attack, L.release);
        }
    }

    PcmClip finish(float level) {
        float peak = 1e-4f;
        for (float v : buf) peak = std::fmax(peak, std::fabs(v));
        PcmClip c;
        c.samples.resize(buf.size());
        const float k = 30000.0f * clampf(level, 0.0f, 1.0f);
        for (size_t i = 0; i < buf.size(); ++i)
            c.samples[i] = static_cast<std::int16_t>(clampf(buf[i] / peak, -1.0f, 1.0f) * k);
        return c;
    }
};

}  // namespace

const SoundRecipe& sound_recipe(SoundId id) { return kRecipes[static_cast<int>(id)]; }
const char* sound_name(SoundId id) { return id < SoundId::Count ? kRecipes[static_cast<int>(id)].name : "?"; }

PcmClip synthesize(const SoundRecipe& r, float pitch, std::uint64_t seed) {
    Synth s(r.seconds, pitch, seed);
    for (const SoundLayer& L : r.layers) {
        if (L.kind == K::Tone) s.add_tone(L);
        else if (L.kind == K::Noise) s.add_noise(L);
    }
    return s.finish(r.level);
}

PcmClip synthesize(SoundId id, float pitch, std::uint64_t seed) {
    if (id >= SoundId::Count) return PcmClip{};
    return synthesize(kRecipes[static_cast<int>(id)], pitch, seed);
}

}  // namespace pet
