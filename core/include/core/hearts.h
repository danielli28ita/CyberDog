// 好感度上涨时头顶冒的小心形。纯 C++，输出部件姿态。
//
// 每颗心：从头顶冒出，往上飘、左右轻摆、先放大再缩小消失，约 1.4 秒。
// 最多 kMaxHearts 颗同时在。宿主在亲密度每涨 1 点时 spawn 一颗，摸着不放就是一串。

#pragma once

#include "core/proxy_mesh.h"

namespace pet {

inline constexpr int kMaxHearts = 4;

class HeartFx {
public:
    // headX/headZ 是狗的舞台位置，心从头顶（y≈1.35）冒出。
    void spawn(float headX, float headZ);
    void update(float dt);
    // 写进 Part::Heart0..3 的姿态。没在飞的心缩放为 0。
    void apply(PartPose* poses) const;
    bool any_alive() const;
    unsigned spawned() const { return spawned_; }

private:
    struct Heart { bool alive = false; float t = 0, x = 0, z = 0, phase = 0; };
    Heart hearts_[kMaxHearts];
    int   next_ = 0;
    unsigned spawned_ = 0;
};

}  // namespace pet
