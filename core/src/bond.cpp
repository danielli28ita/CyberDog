#include "core/bond.h"

#include <algorithm>
#include "core/math3d.h"

namespace pet {

void Bond::clamp_points() { affinity_ = clampf(affinity_, 0.0f, kMax); }

void Bond::set_affinity(float v) {
    affinity_ = v;
    clamp_points();
}

int Bond::level() const {
    int lv = 1;
    for (int i = 1; i < kMaxLevel; ++i) {
        if (affinity_ >= kLevelThreshold[i]) lv = i + 1;
        else break;
    }
    return lv;
}

float Bond::xp_into_level() const {
    const int lv = level();
    return affinity_ - kLevelThreshold[lv - 1];
}

float Bond::xp_needed_for_level() const {
    const int lv = level();
    if (lv >= kMaxLevel) {
        const float room = kMax - kLevelThreshold[kMaxLevel - 1];
        return room > 0.0f ? room : 1.0f;
    }
    return kLevelThreshold[lv] - kLevelThreshold[lv - 1];
}

float Bond::level_progress01() const {
    const float need = xp_needed_for_level();
    if (need <= 0.0f) return 1.0f;
    return clampf(xp_into_level() / need, 0.0f, 1.0f);
}

int Bond::tier_index() const { return level() - 1; }

void Bond::apply(BondEvent e, float amount) {
    sinceInteraction_ = 0.0f;
    switch (e) {
        case BondEvent::Pet: {
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
            if (hitWindow_ <= 0.0f) { hitWindow_ = 60.0f; hitsRecent_ = 0; }
            if (hitsRecent_ < kHitCapPerMinute) {
                affinity_ -= kHitPenalty;
                ++hitsRecent_;
            }
            obedient_ = 120.0f;
            break;
        case BondEvent::PlayedBall:
            affinity_ += 2.0f;
            break;
        case BondEvent::Reminded:
            affinity_ += 1.0f;
            break;
    }
    if (affinity_ < kFloor && e == BondEvent::Hit) affinity_ = kFloor;
    clamp_points();
}

void Bond::decay(float dt) {
    sinceInteraction_ += dt;
    if (obedient_ > 0.0f) obedient_ -= dt;
    if (hitWindow_ > 0.0f) { hitWindow_ -= dt; if (hitWindow_ <= 0.0f) hitsRecent_ = 0; }
    if (petWindow_ > 0.0f) { petWindow_ -= dt; if (petWindow_ <= 0.0f) petGained_ = 0.0f; }
    if (sinceInteraction_ > kIdleGraceSeconds && affinity_ > kFloor) {
        affinity_ -= dt * (kDecayPerHour / 3600.0f);
        if (affinity_ < kFloor) affinity_ = kFloor;
    }
}

}  // namespace pet
