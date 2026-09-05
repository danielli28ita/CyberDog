// 空闲三级停止。对应 设计文档 §3.3 最后一行与 §7 的预算分档，是 M3 的核心。
//
// 三级不是同一条链，可见和隐藏走的路不一样：
//
//   可见但空闲   →  只停止呈现。**不能释放交换链**——一释放，
//                   DirectComposition 的 visual 就没有内容了，画面会直接消失。
//   隐藏         →  立刻停止呈现；隔一段时间释放交换链；再隔一段释放整个图形设备。
//
// 「停止」是真的不投递、不呈现，不是降到低帧率。技能包 engineering/SKILL.md
// 的 hot path 禁令第 7 条写了这一点。

#pragma once

#include <windows.h>

namespace pet::win {

enum class PowerState {
    Rendering,           // 正常渲染
    PresentStopped,      // 停止呈现，设备与交换链都还在。画面停在最后一帧
    SwapchainReleased,   // 释放交换链。只在隐藏时才会到这一级
    DeviceReleased,      // 释放整个图形设备
};

const char* power_state_name(PowerState s);

class IdleController {
public:
    struct Config {
        // 可见状态下，多久没有交互就停止呈现。
        unsigned idleStopPresentMs = 5000;
        // 隐藏之后，多久释放交换链。
        unsigned hiddenReleaseSwapchainMs = 60000;
        // 隐藏之后，多久释放整个图形设备。设计文档 §7 写的是 5 分钟。
        unsigned hiddenReleaseDeviceMs = 300000;
    };

    void set_config(const Config& c) { cfg_ = c; }
    const Config& config() const { return cfg_; }

    // 有交互就调：光标进入窗口、拖动、托盘操作。
    void notify_activity(ULONGLONG nowMs) { lastActivityMs_ = nowMs; }

    // 短暂唤醒：停靠态播微动作或健康提醒时用。durMs 内保持渲染，
    // 到时直接回到停止呈现，**不重置**交互计时——微动作不算用户交互。
    // 设计文档 §2.2 的「微动作调度器：唤醒、播完立刻停止呈现」就是这个。
    void request_burst(ULONGLONG nowMs, unsigned durMs) {
        const ULONGLONG until = nowMs + durMs;
        if (until > burstUntilMs_) burstUntilMs_ = until;
    }
    bool in_burst(ULONGLONG nowMs) const { return nowMs < burstUntilMs_; }

    void set_visible(bool v, ULONGLONG nowMs);
    bool visible() const { return visible_; }

    // 每轮循环调一次，返回当前应该处于的状态。
    PowerState update(ULONGLONG nowMs);
    PowerState state() const { return state_; }

    // 当前状态下，循环该等多久再醒。返回 INFINITE 表示可以一直等到有消息。
    DWORD wait_timeout_ms(ULONGLONG nowMs) const;

private:
    Config     cfg_{};
    PowerState state_ = PowerState::Rendering;
    bool       visible_ = true;
    ULONGLONG  lastActivityMs_ = 0;
    ULONGLONG  hiddenSinceMs_ = 0;
    ULONGLONG  burstUntilMs_ = 0;
};

// 带超时地等消息。超时到了也返回，这样计时能继续推进。
void wait_message_timeout(DWORD ms);

}  // namespace pet::win
