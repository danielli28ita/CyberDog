#include "plugins/health_plugin.h"

#include "core/i18n.h"

#include <algorithm>

namespace pet::plugins {

HealthPlugin::HealthPlugin(const HealthConfig& cfg) { set_config(cfg); }

void HealthPlugin::set_config(const HealthConfig& cfg) {
    cfg_ = cfg;
    // 规格写的可配区间是 10–120 分钟。低于 10 只在测试时出现，这里不拦，
    // 但不允许 0 或负数，否则每 tick 都提醒。
    if (cfg_.intervalMinutes <= 0.0f) cfg_.intervalMinutes = 30.0f;
    if (cfg_.awayAfterSeconds < 30.0f) cfg_.awayAfterSeconds = 30.0f;
    reset_timers();
}

void HealthPlugin::reset_timers() {
    water_ = {};
    stand_ = {};
    // 错开半个周期：久坐先响（第 15 分钟），喝水在第 30 分钟。
    stand_.seated = cfg_.intervalMinutes * 60.0f * 0.5f;
}

void HealthPlugin::on_load(IHostServices& host) {
    host_ = &host;
    away_ = false;
    reset_timers();
}

void HealthPlugin::on_unload() { host_ = nullptr; }

float HealthPlugin::water_remaining_seconds() const {
    return std::max(0.0f, cfg_.intervalMinutes * 60.0f - water_.seated);
}
float HealthPlugin::stand_remaining_seconds() const {
    return std::max(0.0f, cfg_.intervalMinutes * 60.0f - stand_.seated);
}

void HealthPlugin::on_tick(const HostClock& clock) {
    if (!host_ || !cfg_.enabled || (!cfg_.remindWater && !cfg_.remindStand)) return;

    const double dt = lastTick_ < 0.0 ? 0.0 : clock.nowSeconds - lastTick_;
    lastTick_ = clock.nowSeconds;

    // 在座判定：只看空闲时长。离开时计时暂停；回来时从头计——
    // 离开过一次就当已经起身活动过、顺便喝过水了。
    const bool awayNow = clock.userIdleSeconds >= cfg_.awayAfterSeconds;
    if (awayNow) { away_ = true; return; }
    if (away_) { away_ = false; reset_timers(); return; }

    if (cfg_.remindWater) tick_timer(water_, true, clock, dt);
    if (cfg_.remindStand) tick_timer(stand_, false, clock, dt);
}

void HealthPlugin::tick_timer(Timer& t, bool isWater, const HostClock& clock, double dt) {
    t.seated += static_cast<float>(dt);
    if (t.pending) {
        // 上次宿主拒绝了（在忙、隐藏），一分钟内重试，过了就放弃这一轮。
        if (clock.nowSeconds <= t.retryUntil) { if (fire(isWater, clock)) t = {}; }
        else t = {};
        return;
    }
    if (t.seated >= cfg_.intervalMinutes * 60.0f) {
        t.pending = true;
        t.retryUntil = clock.nowSeconds + 60.0;
        if (fire(isWater, clock)) t = {};
    }
}

bool HealthPlugin::fire(bool isWater, const HostClock& clock) {
    if (clock.petBusy || !clock.petVisible) return false;   // 留着 pending，下个 tick 再试

    PetActionRequest req;
    if (isWater) {
        req.action = ActionKind::RemindWater;
        req.bubbleText = tr(Str::DrinkWater);
        req.bubbleDelaySeconds = 1.0f;     // 先轻轻叫一声，再出字幕
    } else {
        req.action = ActionKind::RemindStand;
        req.bubbleText = tr(Str::StandUp);
        req.bubbleDelaySeconds = 1.2f;     // 先大跳，落地再出字幕
    }
    req.bubbleSeconds = 15.0f;
    if (!host_->request_pet_action(req)) return false;
    ++sent_;
    return true;
}

}  // namespace pet::plugins
