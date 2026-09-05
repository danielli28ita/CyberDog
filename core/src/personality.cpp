#include "core/personality.h"
#include "core/rng.h"

#include <algorithm>

namespace pet {

Personality personality_from_profile(const CharacterProfile& prof, std::uint64_t seed) {
    Rng rng(seed);
    const Personality& b = prof.baseline;

    // 基线 + 偏移。偏移落在 [-spread, +spread]，结果夹到 [0,1]。
    auto vary = [&rng, &prof](float baseline) {
        const float off = (rng.unit() * 2.0f - 1.0f) * prof.spread;
        return std::clamp(baseline + off, 0.0f, 1.0f);
    };

    Personality p;
    p.extroversion = vary(b.extroversion);
    p.clinginess   = vary(b.clinginess);
    p.laziness     = vary(b.laziness);
    p.timidity     = vary(b.timidity);
    p.liveliness   = vary(b.liveliness);
    p.charm        = vary(b.charm);

    // 捣蛋和好奇不能只靠基线保证顺序，个体偏移会把顺序打乱。
    // 这里按顺序算，让两条不变量对每个实例都成立，而不是事后修补：
    //   1. 捣蛋是最高的那一项，但不取满值（「不是完全捣乱」）
    //   2. 好奇高，但不压过捣蛋
    p.mischief  = std::clamp(vary(b.mischief), prof.mischiefFloor, prof.mischiefCeiling);
    p.curiosity = std::min(vary(b.curiosity), p.mischief - prof.traitGap);
    return p;
}

float personality_dot(const Personality& p, const float w[8]) {
    return p.extroversion * w[0] + p.clinginess * w[1] + p.curiosity * w[2] + p.laziness * w[3] +
           p.timidity * w[4] + p.liveliness * w[5] + p.mischief * w[6] + p.charm * w[7];
}

float idle_timeout_seconds(const Personality& p, float base_seconds) {
    // laziness 越高越晚回停靠位。0 对应 0.5 倍，1 对应 2.0 倍。
    const float factor = 0.5f + p.laziness * 1.5f;
    return std::clamp(base_seconds * factor, 30.0f, 600.0f);
}

float micro_action_interval_seconds(const Personality& p, float t01) {
    // 活泼的个体间隔短。区间固定为 设计文档 §2.2 写的 20–90 秒。
    const float lo = 20.0f;
    const float hi = 90.0f;
    const float t  = std::clamp(t01, 0.0f, 1.0f);
    const float span = hi - lo;
    // liveliness=1 时区间压到前 40%，liveliness=0 时用满整个区间。
    const float upper = lo + span * (1.0f - 0.6f * std::clamp(p.liveliness, 0.0f, 1.0f));
    return lo + (upper - lo) * t;
}

}  // namespace pet
