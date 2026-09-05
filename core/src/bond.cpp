#include "core/bond.h"

#include <algorithm>
#include "core/math3d.h"

namespace pet {

void Bond::set_affinity(float v) { affinity_ = clampf(v, 0.0f, kMax); }

void Bond::apply(BondEvent e, float amount) {
    sinceInteraction_ = 0.0f;
    switch (e) {
        case BondEvent::Pet: {
            // 每秒 +1.5，但一分钟内最多涨 3 点（2.0，作者要求）。窗口从第一次摸开始计。
            if (petWindow_ <= 0.0f) { petWindow_ = 60.0f; petGained_ = 0.0f; }
            const float room = kPetPerMinute - petGained_;
            const float gain = room > 0.0f ? (std::min)(1.5f * amount, room) : 0.0f;
            affinity_ += gain;
            petGained_ += gain;
            obedient_ = 90.0f;
            break;
        }
        case BondEvent::Poke:
            affinity_ += 0.5f;
            break;
        case BondEvent::Hit:
            // 一分钟内最多扣 3 次，每次 1 点。「减少很难」。
            if (hitWindow_ <= 0.0f) { hitWindow_ = 60.0f; hitsRecent_ = 0; }
            if (hitsRecent_ < 3) { affinity_ -= 1.0f; ++hitsRecent_; }
            obedient_ = 120.0f;
            break;
        case BondEvent::PlayedBall:
            affinity_ += 2.0f;
            break;
        case BondEvent::Reminded:
            affinity_ += 1.0f;
            break;
    }
    // 打不会打到软下限以下；加没有上限之外的限制。
    if (affinity_ < kFloor && e == BondEvent::Hit) affinity_ = kFloor;
    affinity_ = clampf(affinity_, 0.0f, kMax);
}

void Bond::decay(float dt) {
    sinceInteraction_ += dt;
    if (obedient_ > 0.0f) obedient_ -= dt;
    if (hitWindow_ > 0.0f) { hitWindow_ -= dt; if (hitWindow_ <= 0.0f) hitsRecent_ = 0; }
    if (petWindow_ > 0.0f) { petWindow_ -= dt; if (petWindow_ <= 0.0f) petGained_ = 0.0f; }
    // 十分钟没互动才开始掉，之后每小时 1 点。掉到软下限为止。
    if (sinceInteraction_ > 600.0f && affinity_ > kFloor) {
        affinity_ -= dt / 3600.0f;
        if (affinity_ < kFloor) affinity_ = kFloor;
    }
}

}  // namespace pet
