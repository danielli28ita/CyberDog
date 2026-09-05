#include "overlay/idle_controller.h"

namespace pet::win {

const char* power_state_name(PowerState s) {
    switch (s) {
        case PowerState::Rendering:         return "渲染中";
        case PowerState::PresentStopped:    return "已停止呈现";
        case PowerState::SwapchainReleased: return "已释放交换链";
        case PowerState::DeviceReleased:    return "已释放图形设备";
    }
    return "未知";
}

void IdleController::set_visible(bool v, ULONGLONG nowMs) {
    if (v == visible_) return;
    visible_ = v;
    if (v) {
        // 重新显示算一次交互，从头开始计时。
        lastActivityMs_ = nowMs;
        hiddenSinceMs_ = 0;
    } else {
        hiddenSinceMs_ = nowMs;
    }
}

PowerState IdleController::update(ULONGLONG nowMs) {
    if (visible_) {
        // 可见时最多降到「停止呈现」。再往下会让画面消失。
        const ULONGLONG idle = nowMs - lastActivityMs_;
        const bool burst = nowMs < burstUntilMs_;
        state_ = (idle >= cfg_.idleStopPresentMs && !burst) ? PowerState::PresentStopped
                                                            : PowerState::Rendering;
        return state_;
    }

    const ULONGLONG hidden = nowMs - hiddenSinceMs_;
    if (hidden >= cfg_.hiddenReleaseDeviceMs) {
        state_ = PowerState::DeviceReleased;
    } else if (hidden >= cfg_.hiddenReleaseSwapchainMs) {
        state_ = PowerState::SwapchainReleased;
    } else {
        state_ = PowerState::PresentStopped;
    }
    return state_;
}

DWORD IdleController::wait_timeout_ms(ULONGLONG nowMs) const {
    // 还在渲染就不等，正常跟 vsync 走。
    if (state_ == PowerState::Rendering) return 0;

    // 已经降到最低一级，没有下一个时间点要等，可以一直睡到有消息。
    if (!visible_ && state_ == PowerState::DeviceReleased) return INFINITE;

    // 否则算到下一个降级时间点还有多久，睡到那时候醒一次。
    ULONGLONG next = 0;
    if (visible_) {
        // 可见且已停止呈现：没有更低的级别可去，等消息即可。
        return INFINITE;
    }
    const ULONGLONG hidden = nowMs - hiddenSinceMs_;
    if (hidden < cfg_.hiddenReleaseSwapchainMs) {
        next = cfg_.hiddenReleaseSwapchainMs - hidden;
    } else if (hidden < cfg_.hiddenReleaseDeviceMs) {
        next = cfg_.hiddenReleaseDeviceMs - hidden;
    } else {
        return INFINITE;
    }
    // 上限一秒，避免时钟跳变时睡过头。
    return static_cast<DWORD>(next > 1000 ? 1000 : next);
}

void wait_message_timeout(DWORD ms) {
    // 只等消息，不等任何句柄。超时也返回，这样降级计时能继续推进。
    MsgWaitForMultipleObjectsEx(0, nullptr, ms, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
}

}  // namespace pet::win
