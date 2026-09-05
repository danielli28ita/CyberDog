// 插件契约的最小 C++ 形式。设计文档 §4.4 一期契约里的 on_tick 与 pet-action 两项，
// 为健康提醒（M10）提前落地。bus / save / panel 三项 P5 再补，接口位置留在这里。
//
// 纯 C++，插件和宿主都只依赖本文件。插件拿不到窗口、渲染、Win32 的任何东西，
// 想让宠物做事只能通过 request_pet_action，宿主可以拒绝。

#pragma once

#include <string>

#include "core/action.h"

namespace pet {

// 宿主每次 tick 给插件的时钟。
struct HostClock {
    double    nowSeconds = 0.0;        // 单调时钟，进程启动为 0
    long long wallClock = 0;           // 墙钟，Unix 秒（time_t）。备忘录这类按时刻触发的用它
    float     userIdleSeconds = 0.0f;  // 系统空闲时长（GetLastInputInfo）。只有时长，没有内容
    bool      petVisible = true;
    bool      petBusy = false;         // 宠物正在做别的动作或被拖拽，此时请求会被拒
    double    sinceLastPetSeconds = 1e9;           // 距上一次被摸多久（本次运行内；没摸过就是很大）
    double    sinceLastInteractionSeconds = 1e9;   // 距上一次任何互动（摸、打、戳、拖）多久
};

struct PetActionRequest {
    ActionKind  action = ActionKind::Idle;
    const char* bubbleText = nullptr;   // UTF-8。nullptr 表示不显示气泡
    float       bubbleSeconds = 15.0f;
    float       bubbleDelaySeconds = 0.0f;   // 动作开始多久之后再显示气泡（先叫一声再出字幕）
};

class IHostServices {
public:
    virtual ~IHostServices() = default;
    // 请求宠物做一个动作并显示气泡。返回 false 表示宿主拒绝了（隐藏、忙、全屏）。
    virtual bool request_pet_action(const PetActionRequest& req) = 0;

    // save 服务（设计文档 §4.4）：每个插件只能读写自己的命名空间，读不到别的插件的。
    // 内容是插件自己定义的文本。宿主负责放到用户目录并原子写盘。
    virtual bool read_plugin_data(const char* pluginId, std::string& out) = 0;
    virtual bool write_plugin_data(const char* pluginId, const std::string& text) = 0;

    // net 服务：异步 HTTP GET。宿主在后台线程取，取到后在主循环里回调（ok=false 表示失败）。
    // 本项目默认不联网（M8）；只有用户明确开启的插件（天气）才用它，宿主可以拒绝（返回 false）。
    using FetchDone = void (*)(void* user, bool ok, const std::string& body);
    virtual bool fetch_text(const std::string& url, FetchDone done, void* user) = 0;
};

class IPlugin {
public:
    virtual ~IPlugin() = default;
    virtual const char* id() const = 0;
    virtual void on_load(IHostServices& host) = 0;
    virtual void on_tick(const HostClock& clock) = 0;   // 宿主限频，最快 1 Hz
    virtual void on_unload() = 0;
};

}  // namespace pet
