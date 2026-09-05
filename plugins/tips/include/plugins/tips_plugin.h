// 操作提示插件：隔一阵子随机冒一条操作建议（怎么摸、右键看属性、拖动换窝、天气、备忘、语言）。
// 长时间没人摸头时优先提示「摸头能涨好感度」。CyberDog 1.0，作者要求。
//
// 节奏：启动 3 分钟后第一条，之后每 20–35 分钟一条；宿主在忙就 30 秒后再试。
// 文案在 core/i18n（Tip*），三语。纯 C++，只依赖插件契约。

#pragma once

#include "core/plugin_api.h"
#include "core/rng.h"

namespace pet::plugins {

class TipsPlugin final : public IPlugin {
public:
    explicit TipsPlugin(std::uint64_t seed) : rng_(seed) {}
    const char* id() const override { return "plugin.tips"; }
    void on_load(IHostServices& host) override { host_ = &host; }
    void on_tick(const HostClock& clock) override;
    void on_unload() override { host_ = nullptr; }

    unsigned shown() const { return shown_; }
    // 多久没摸头就优先提示摸头。
    static constexpr double kNoPetSeconds = 20.0 * 60.0;
    static constexpr double kFirstTipSeconds = 180.0;

private:
    IHostServices* host_ = nullptr;
    Rng rng_;
    double nextAt_ = kFirstTipSeconds;
    int rotation_ = 0;
    unsigned shown_ = 0;
};

}  // namespace pet::plugins
