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

// 计算栖位左上角坐标（物理像素）。
// embedPx：窗口底边压到任务栏顶边以下多少像素。设计文档 §2.2 默认 18。
// 任务栏取不到、或任务栏自动隐藏时，退回工作区右下角且不压入。
POINT dock_position(const DesktopMetrics& m, int winW, int winH, int embedPx);

// 当前是否有全屏应用或正在投影。对应 设计文档 §2.2 边界 2。
// 投影与独占全屏问系统；普通全屏应用自己比较前台窗口矩形——
// 系统的 QUNS_BUSY 会把本程序的全屏覆盖层也当成全屏应用（1.5 修的闪烁）。
bool fullscreen_or_presenting();

}  // namespace pet::win
