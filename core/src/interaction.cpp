#include "core/interaction.h"
#include "core/math3d.h"

#include <cmath>
#include <cstdlib>

namespace pet {
namespace {

// 阈值都按 96 DPI 写，用 dpiScale 放大。
constexpr float kPokeMaxTravel = 6.0f;      // 按下松开之间移动不超过这个算戳
constexpr float kPokeMaxHold   = 0.35f;     // 秒
constexpr float kDecideTravel  = 8.0f;      // 移动超过这个才决定是摸还是拖
constexpr float kHitSpeed      = 900.0f;    // 像素/秒。超过算快
constexpr float kPetSpeedMax   = 500.0f;    // 低于这个算慢慢摸
constexpr float kHitCooldown   = 0.25f;

}  // namespace

GestureEvent GestureTracker::update(const PointerSample& s) {
    GestureEvent ev;
    const float scale = s.dpiScale > 0.0f ? s.dpiScale : 1.0f;
    if (hitCooldown_ > 0.0f) hitCooldown_ -= s.dt;

    // ---- 按下 ----
    if (s.pressed && !down_) {
        down_ = true;
        downX_ = lastX_ = s.x;
        downY_ = lastY_ = s.y;
        held_ = 0.0f;
        travel_ = 0.0f;
        speedAvg_ = 0.0f;
        reversals_ = 0;
        lastDirX_ = 0;
        // 按在头上是摸/打候选；按在身上是拖候选；按在空白处不该收到（命中测试挡了）。
        mode_ = s.onBody ? Mode::Undecided : Mode::Idle;
        downOnHead_ = s.onHead;
        hoverOnHead_ = false;
        return ev;
    }

    // ---- 松开 ----
    if (!s.pressed && down_) {
        down_ = false;
        const Mode m = mode_;
        mode_ = Mode::Idle;
        if (m == Mode::Drag) { ev.g = Gesture::DragEnd; return ev; }
        if (m == Mode::Undecided && travel_ <= kPokeMaxTravel * scale && held_ <= kPokeMaxHold) {
            ev.g = Gesture::Poke;
            return ev;
        }
        ev.g = Gesture::Release;
        return ev;
    }

    if (!down_) {
        // ---- 没按键：光标在头上移动就算摸 / 打（1.7，作者要求不用按住左键）----
        // 拖动仍然要按住身体。离开头部就清零，再进来从头算，第一帧不产生位移。
        if (!s.onHead) { hoverOnHead_ = false; return ev; }
        if (!hoverOnHead_) {
            hoverOnHead_ = true;
            lastX_ = s.x;
            lastY_ = s.y;
            speedAvg_ = 0.0f;
            reversals_ = 0;
            lastDirX_ = 0;
            return ev;
        }
        const int dx = s.x - lastX_, dy = s.y - lastY_;
        const float step = std::sqrt(static_cast<float>(dx * dx + dy * dy));
        const float inst = s.dt > 1e-4f ? step / s.dt : 0.0f;
        speedAvg_ = approach(speedAvg_, inst, s.dt, 0.06f);
        const int dirX = dx > 2 ? 1 : (dx < -2 ? -1 : 0);
        if (dirX != 0 && lastDirX_ != 0 && dirX != lastDirX_) ++reversals_;
        if (dirX != 0) lastDirX_ = dirX;
        lastX_ = s.x;
        lastY_ = s.y;
        if (speedAvg_ >= kHitSpeed * scale && hitCooldown_ <= 0.0f && reversals_ >= 1) {
            hitCooldown_ = kHitCooldown;
            reversals_ = 0;
            ev.g = Gesture::Hit;
            return ev;
        }
        if (speedAvg_ <= kPetSpeedMax * scale && step > 0.0f) {
            ev.g = Gesture::PetTick;
            ev.amount = s.dt;
        }
        return ev;
    }

    // ---- 按住中 ----
    held_ += s.dt;
    const int dx = s.x - lastX_, dy = s.y - lastY_;
    const float step = std::sqrt(static_cast<float>(dx * dx + dy * dy));
    travel_ += step;
    const float inst = s.dt > 1e-4f ? step / s.dt : 0.0f;
    speedAvg_ = approach(speedAvg_, inst, s.dt, 0.06f);

    // 来回抽打计数：横向方向反转一次记一次。
    const int dirX = dx > 2 ? 1 : (dx < -2 ? -1 : 0);
    if (dirX != 0 && lastDirX_ != 0 && dirX != lastDirX_) ++reversals_;
    if (dirX != 0) lastDirX_ = dirX;

    lastX_ = s.x;
    lastY_ = s.y;

    if (mode_ == Mode::Undecided) {
        if (travel_ >= kDecideTravel * scale) {
            // 按下时在头上：摸或打；按下时在身上：拖。
            mode_ = downOnHead_ ? Mode::Pet : Mode::Drag;
            if (mode_ == Mode::Drag) { ev.g = Gesture::DragStart; return ev; }
        } else {
            return ev;
        }
    }

    if (mode_ == Mode::Drag) {
        ev.g = Gesture::DragMove;
        ev.dx = dx;
        ev.dy = dy;
        return ev;
    }

    // Pet 模式：快就是打，慢就是摸。离开头部区域时什么都不算。
    if (!s.onHead && !s.onBody) return ev;
    if (speedAvg_ >= kHitSpeed * scale && hitCooldown_ <= 0.0f && reversals_ >= 1) {
        hitCooldown_ = kHitCooldown;
        reversals_ = 0;
        ev.g = Gesture::Hit;
        return ev;
    }
    if (speedAvg_ <= kPetSpeedMax * scale && step > 0.0f) {
        ev.g = Gesture::PetTick;
        ev.amount = s.dt;
    }
    return ev;
}

}  // namespace pet
