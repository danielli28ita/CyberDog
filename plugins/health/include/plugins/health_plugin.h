// 健康提醒插件。设计文档 §2.7 / M10。
//
// 1.1 起喝水和久坐是两个独立的计时器：各自每 30 分钟一次，错开 15 分钟
// （久坐先在第 15 分钟响，喝水在第 30 分钟）。作者定的两套顺序：
//   喝水：轻轻叫 → 字幕 → 尿尿的动作和声音
//   久坐：大跳 → 字幕 → 把球踢出屏幕，提醒起来运动
// 动作本身在 core/action（RemindWater / RemindStand），本插件只管计时和请求。
//
// 规格要点，每条在实现里都有对应：
//   在座判定 系统空闲连续超过 5 分钟视为离开：计时暂停，回来时两个都重置
//   提醒方式 请求宠物做动作 + 气泡。不用系统通知，不抢焦点
//   忽略处理 气泡 15 秒自行消失，不重复弹，下一轮正常计时
//   可配     间隔、总开关、喝水与起身分别开关
//
// 纯 C++。它不知道 Windows，空闲时长由宿主喂进来。

#pragma once

#include "core/plugin_api.h"

namespace pet::plugins {

struct HealthConfig {
    bool  enabled = true;
    float intervalMinutes = 30.0f;    // 两个计时器共用。可配 10–120，测试时可以更小
    float awayAfterSeconds = 300.0f;  // 连续空闲超过这个时长算离开
    bool  remindWater = true;
    bool  remindStand = true;
};

class HealthPlugin final : public IPlugin {
public:
    explicit HealthPlugin(const HealthConfig& cfg = {});

    const char* id() const override { return "plugin.health"; }
    void on_load(IHostServices& host) override;
    void on_tick(const HostClock& clock) override;
    void on_unload() override;

    void set_config(const HealthConfig& cfg);
    const HealthConfig& config() const { return cfg_; }

    // 观测用。
    float water_remaining_seconds() const;
    float stand_remaining_seconds() const;
    unsigned reminders_sent() const { return sent_; }
    bool away() const { return away_; }

private:
    struct Timer {
        float  seated = 0.0f;     // 本轮连续在座时长
        bool   pending = false;   // 到点了但宿主还没接受
        double retryUntil = 0.0;
    };
    void reset_timers();
    void tick_timer(Timer& t, bool isWater, const HostClock& clock, double dt);
    bool fire(bool isWater, const HostClock& clock);

    HealthConfig   cfg_;
    IHostServices* host_ = nullptr;
    double         lastTick_ = -1.0;
    bool           away_ = false;
    Timer          water_, stand_;
    unsigned       sent_ = 0;
};

}  // namespace pet::plugins
