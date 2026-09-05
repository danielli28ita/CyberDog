// 音效播放。XAudio2，随 Windows SDK 提供，不用分发运行时（设计文档 §2.6）。
//
// 常驻策略与图形设备一样：播放前初始化，静默 60 秒后释放。
// 音效数据由 core/sound_synth 在启动时合成，放内存里，没有文件。
//
// §2.6 的默认值都在这里落实：主音量 30%；捣乱音效冷却 ≥5 分钟、每小时 ≤6 次；
// 同一段不连续重复；22:00–08:00 静音（实现方加的默认，可关）。

#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

#include "core/sound_synth.h"

struct IXAudio2;
struct IXAudio2MasteringVoice;
struct IXAudio2SourceVoice;

namespace pet::win {

class Audio {
public:
    ~Audio();

    // 合成全部音效。pitch 由性格决定。
    void prepare(float pitch, std::uint64_t seed);

    // 播一段。mischief=true 的走捣乱音效的冷却与频率上限。返回是否真的出声了。
    bool play(SoundId id, bool mischief = false);

    // 每轮循环调一次：静默超时释放引擎。
    void tick(ULONGLONG nowMs);
    void release();

    void set_enabled(bool v) { enabled_ = v; }
    bool enabled() const { return enabled_; }
    void set_volume(float v) { volume_ = v; }

    // 22:00–08:00 是否静音。作者要求（1.1）：这段时间**不允许**出声，所以没有开关，
    // 这个函数只用来在界面上显示状态。
    static bool quiet_now();

    unsigned played_count() const { return played_; }
    unsigned suppressed_count() const { return suppressed_; }

private:
    bool ensure_engine();

    IXAudio2* engine_ = nullptr;
    IXAudio2MasteringVoice* master_ = nullptr;
    // 每种音效一个源声音，重放时先停再提交。同时最多几种声音，够用。
    IXAudio2SourceVoice* voices_[static_cast<int>(SoundId::Count)]{};
    PcmClip clips_[static_cast<int>(SoundId::Count)];

    bool  enabled_ = true;
    float volume_ = 0.30f;
    ULONGLONG lastPlayMs_ = 0;
    ULONGLONG lastMischiefMs_ = 0;
    ULONGLONG mischiefHourStartMs_ = 0;
    unsigned  mischiefThisHour_ = 0;
    int   lastId_ = -1;
    unsigned played_ = 0, suppressed_ = 0, fromResource_ = 0;   // fromResource_：多少段用了 exe 里的录音资源
    bool  comInit_ = false;
};

}  // namespace pet::win
