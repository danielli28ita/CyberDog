// 程序合成的音效。不依赖任何音频文件，启动时算出来放内存里。
//
// 为什么合成而不是下载：找不到许可证清楚的比格叫声素材；合成的每一段都可以
// 按性格改音高和时长，且单文件分发不用带资源。将来有录音资产时按 SoundId 替换即可。
//
// 1.1 起是表驱动：每段声音是一份「配方」（几层音调 / 噪声的参数），加一段声音 = 加一行。
// 输出 16 位单声道 PCM，采样率 kSampleRate。纯 C++。

#pragma once

#include <cstdint>
#include <vector>

namespace pet {

inline constexpr int kSampleRate = 22050;

// 顺序不能动：core/action 里按下标引用。新的加在末尾。
enum class SoundId : std::uint8_t {
    Bark,        // 短促的一声叫。出场、扑光标
    Whimper,     // 哼唧。挨打
    Happy,       // 舒服的哼哼。被摸
    Pant,        // 喘气。玩球之后
    Bounce,      // 网球落地
    Clatter,     // 水碗翻倒
    Shake,       // 抖身子，耳朵拍打
    Step,        // 一步脚步声
    Sniff,       // 闻。闲逛时偶尔
    BarkSoft,    // 轻轻叫一声。喝水提醒的开头
    Pee,         // 尿尿的水声
    Kick,        // 踢球
    Snore,       // 打呼
    Count
};

const char* sound_name(SoundId id);

struct PcmClip {
    std::vector<std::int16_t> samples;
    float seconds() const { return static_cast<float>(samples.size()) / kSampleRate; }
};

// 配方的一层。Tone 是带谐波的音调（f0 滑到 f1），Noise 是低通白噪声。
struct SoundLayer {
    enum class Kind : std::uint8_t { None, Tone, Noise } kind = Kind::None;
    float start = 0, len = 0, attack = 0.01f, release = 0.05f, gain = 1.0f;
    float f0 = 0, f1 = 0;        // Tone：起止频率。Noise：f0 = 低通截止频率
    int   harmonics = 1;         // Tone
    float vibHz = 0, vibDepth = 0;
    float raw = 0;               // Noise：混入多少未滤波的白噪声（0–1）
};

struct SoundRecipe {
    SoundId     id;
    const char* name;
    float       seconds;
    SoundLayer  layers[6];
    float       level = 1.0f;   // 整段音量（归一化之后再乘）。打呼这种要小声的给 0.3
};

const SoundRecipe& sound_recipe(SoundId id);

// pitch 是音高倍率（1.0 原样），活泼的狗声音高一点。seed 让同一段每次略有差别。
PcmClip synthesize(SoundId id, float pitch, std::uint64_t seed);
PcmClip synthesize(const SoundRecipe& recipe, float pitch, std::uint64_t seed);

}  // namespace pet
