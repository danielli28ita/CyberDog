#include "core/hearts.h"

#include <cmath>

namespace pet {
namespace {
constexpr float kLife = 1.4f;
}

void HeartFx::spawn(float headX, float headZ) {
    Heart& h = hearts_[next_];
    next_ = (next_ + 1) % kMaxHearts;
    h.alive = true;
    h.t = 0.0f;
    h.x = headX + 0.18f * std::sin(static_cast<float>(spawned_) * 2.4f);   // 左右错开
    h.z = headZ + 0.5f;
    h.phase = static_cast<float>(spawned_) * 1.7f;
    ++spawned_;
}

void HeartFx::update(float dt) {
    for (Heart& h : hearts_) {
        if (!h.alive) continue;
        h.t += dt;
        if (h.t >= kLife) h.alive = false;
    }
}

bool HeartFx::any_alive() const {
    for (const Heart& h : hearts_) if (h.alive) return true;
    return false;
}

void HeartFx::apply(PartPose* poses) const {
    for (int i = 0; i < kMaxHearts; ++i) {
        PartPose& p = poses[static_cast<int>(Part::Heart0) + i];
        const Heart& h = hearts_[i];
        if (!h.alive) { p.scale = {0, 0, 0}; continue; }
        const float u = h.t / kLife;
        // 0–0.15 弹出到 1.15 倍，之后慢慢缩到 0；一路上飘 0.7，左右摆 0.06。
        const float pop = u < 0.15f ? u / 0.15f * 1.15f : 1.15f - (u - 0.15f) / 0.85f * 1.15f;
        const float s = pop < 0.0f ? 0.0f : pop;
        p.scale = {s, s, s};
        p.offset = {h.x + 0.06f * std::sin(h.phase + u * 9.0f), 1.35f + 0.7f * u, h.z};
        p.rotation.z = 0.15f * std::sin(h.phase + u * 6.0f);
    }
}

}  // namespace pet
