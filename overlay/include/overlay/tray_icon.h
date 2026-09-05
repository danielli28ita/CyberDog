// 托盘图标与右键菜单。对应 设计文档 的 M1 与验收 V1。
//
// 两条容易漏的：
//   1. 退出时必须 NIM_DELETE，否则图标会赖在通知区域，要把鼠标扫过去才消失。
//      V1 明确把这个列为不通过。
//   2. Explorer 重启后托盘会被清空，要接 TaskbarCreated 广播重新注册。
//      对应 设计文档 §2.2 的边界 4。

#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "overlay/ui_style.h"

namespace pet::win {

enum class TrayCommand {
    ToggleVisible = 1,
    Exit = 2,
    Rename = 3,
    ToggleSound = 4,
    ToggleNameReminder = 5,
    Memo = 6,
    WeatherCity = 7,
    ToggleAutostart = 8,
    Stats = 9,
    LangZh = 10,
    LangEn = 11,
    LangIt = 12,
    OpenData = 13,
};

class TrayIcon {
public:
    using CommandFn = std::function<void(TrayCommand)>;

    ~TrayIcon();

    bool create(HWND owner, const wchar_t* tip);
    void destroy();

    // 挂到 OverlayWindow::set_message_hook。返回 true 表示消息已处理。
    bool handle_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& out);

    void set_on_command(CommandFn fn) { onCommand_ = std::move(fn); }
    // 像点了菜单一样触发一条命令（PET_OPEN 截图调试用）。
    void invoke(TrayCommand c) { if (onCommand_) onCommand_(c); }

    // 菜单里那一项显示「隐藏宠物」还是「显示宠物」，跟着这个状态走。
    void set_pet_visible(bool v) { petVisible_ = v; }
    void set_sound_enabled(bool v) { soundOn_ = v; }
    void set_name_reminder(bool v) { nameReminder_ = v; }
    void set_autostart(bool v) { autostart_ = v; }
    void set_weather_city(const std::wstring& c) { weatherCity_ = c; }

    // 改提示文字（名字变了）。
    void set_tip(const wchar_t* tip);
    // 菜单自绘按这个缩放（DPI / 96）。
    void set_dpi_scale(float s) { dpiScale_ = s; }

private:
    bool add_icon();
    HICON make_icon();

    HWND      owner_ = nullptr;
    bool      added_ = false;
    bool      petVisible_ = true;
    bool      soundOn_ = true;
    bool      nameReminder_ = true;
    bool      autostart_ = false;
    std::wstring weatherCity_;
    HICON     icon_ = nullptr;
    UINT      taskbarCreatedMsg_ = 0;
    wchar_t   tip_[128] = L"";
    CommandFn onCommand_;
    // 自绘菜单（1.6）。项数据要活到菜单关闭，所以放成员里。
    HMENU     menu_ = nullptr;
    std::vector<ui::MenuItem> menuItems_;
    ui::Fonts fonts_;
    float     dpiScale_ = 1.0f;
};

}  // namespace pet::win
