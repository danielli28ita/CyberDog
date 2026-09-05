#include "core/personality.h"
#include "core/rng.h"
#include "core/save.h"

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

void personality_top2(const Personality& p, int* primaryOut, int* secondaryOut) {
    const float v[kTraitCount] = {p.extroversion, p.clinginess, p.curiosity, p.laziness,
                                  p.timidity, p.liveliness, p.mischief, p.charm};
    int a = 0, b = 1;
    if (v[b] > v[a]) { a = 1; b = 0; }
    for (int i = 2; i < kTraitCount; ++i) {
        if (v[i] > v[a]) {
            b = a;
            a = i;
        } else if (v[i] > v[b]) {
            b = i;
        }
    }
    if (primaryOut) *primaryOut = a;
    if (secondaryOut) *secondaryOut = b;
}

void personality_enforce_invariants(Personality& p, const CharacterProfile& profile) {
    auto clamp01 = [](float x) { return std::clamp(x, 0.0f, 1.0f); };
    p.extroversion = clamp01(p.extroversion);
    p.clinginess   = clamp01(p.clinginess);
    p.laziness     = clamp01(p.laziness);
    p.timidity     = clamp01(p.timidity);
    p.liveliness   = clamp01(p.liveliness);
    p.charm        = clamp01(p.charm);
    p.mischief     = std::clamp(p.mischief, profile.mischiefFloor, profile.mischiefCeiling);
    p.curiosity    = clamp01(p.curiosity);
    if (p.curiosity > p.mischief - profile.traitGap) p.curiosity = p.mischief - profile.traitGap;
    if (p.curiosity < 0.0f) p.curiosity = 0.0f;
}

bool personality_try_load(const SaveData& save, Personality& out) {
    if (!save.has("p_mischief")) return false;
    out.extroversion = save.get_float("p_extroversion", 0.5f);
    out.clinginess   = save.get_float("p_clinginess", 0.5f);
    out.curiosity    = save.get_float("p_curiosity", 0.5f);
    out.laziness     = save.get_float("p_laziness", 0.5f);
    out.timidity     = save.get_float("p_timidity", 0.5f);
    out.liveliness   = save.get_float("p_liveliness", 0.5f);
    out.mischief     = save.get_float("p_mischief", 0.5f);
    out.charm        = save.get_float("p_charm", 0.5f);
    personality_enforce_invariants(out);
    return true;
}

void personality_save(SaveData& save, const Personality& p) {
    save.set_float("p_extroversion", p.extroversion);
    save.set_float("p_clinginess", p.clinginess);
    save.set_float("p_curiosity", p.curiosity);
    save.set_float("p_laziness", p.laziness);
    save.set_float("p_timidity", p.timidity);
    save.set_float("p_liveliness", p.liveliness);
    save.set_float("p_mischief", p.mischief);
    save.set_float("p_charm", p.charm);
}

float idle_timeout_seconds(const Personality& p, float base_seconds) {
    const float factor = 0.5f + p.laziness * 1.5f;
    return std::clamp(base_seconds * factor, 30.0f, 600.0f);
}

float micro_action_interval_seconds(const Personality& p, float t01) {
    const float lo = 20.0f;
    const float hi = 90.0f;
    const float t  = std::clamp(t01, 0.0f, 1.0f);
    const float span = hi - lo;
    const float upper = lo + span * (1.0f - 0.6f * std::clamp(p.liveliness, 0.0f, 1.0f));
    return lo + (upper - lo) * t;
}

}  // namespace pet
