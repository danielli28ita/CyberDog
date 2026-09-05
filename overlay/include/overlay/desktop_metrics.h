// 桌面几何信息：任务栏矩形、边缘方向、工作区、DPI、全屏检测。
//
// 架构原则：平台 API 集中在一层（技能包 desktop-pet/SKILL.md 架构取舍原则第 2 条）。
// 任务栏、DPI、通知区域这类查询只准出现在本模块，上层不许直接调 Win32。
// 换系统版本时只改这里。
//
// 对应 设计文档 §2.2 的栖位计算。

#pragma once

#include <windows.h>

namespace pet::win {

enum class TaskbarEdge { Left, Top, Right, Bottom, Unknown };

struct DesktopMetrics {
    RECT        monitor{};           // 整个显示器（含任务栏），物理像素。覆盖层窗口就是这么大
    RECT        workArea{};          // 工作区，已排除任务栏
    RECT        taskbar{};           // 任务栏矩形；取不到时四个值都是 0
    RECT        notifyArea{};        // 通知区域（时钟与托盘图标）；取不到时为 0
    TaskbarEdge edge = TaskbarEdge::Unknown;
    bool        taskbarValid = false;
    bool        taskbarAutoHide = false;
    UINT        dpi = 96;            // 有效 DPI，96 为 100% 缩放
};

// 查询窗口所在显示器的桌面指标。hwnd 传 nullptr 时用主显示器。
DesktopMetrics query(HWND hwnd);
// 按显示器句柄查。多屏拖狗换屏时用。
DesktopMetrics query_monitor(HMONITOR mon);

// 计算栖位左上角坐标（物理像素）。
// embedPx：窗口底边压到任务栏顶边以下多少像素。设计文档 §2.2 默认 18。
// 任务栏取不到、或任务栏自动隐藏时，退回工作区右下角且不压入。
POINT dock_position(const DesktopMetrics& m, int winW, int winH, int embedPx);

// 当前是否该整窗隐藏。对应 设计文档 §2.2 边界 2。
// 1.1：只信独占全屏 D3D 与投影模式。不再用「前台窗盖满显示器」——
// 最大化的普通窗口、无边框工具窗容易误伤，作者看到的就是没开游戏狗也消失。
// QUNS_BUSY 仍然不用（会把自己的全屏覆盖层判成忙，1.5 已踩过）。
bool fullscreen_or_presenting();

}  // namespace pet::win
