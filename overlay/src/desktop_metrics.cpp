#include "overlay/desktop_metrics.h"

#include <shellapi.h>       // SHAppBarMessage, SHQueryUserNotificationState
#include <shellscalingapi.h>  // GetDpiForMonitor

#include <algorithm>
#include <cwchar>

namespace pet::win {
namespace {

// 距屏幕右边缘留多少像素。留一点是为了不贴死在边上，看起来别扭。
constexpr LONG kEdgeMargin = 8;

TaskbarEdge edge_from_abe(UINT abe) {
    switch (abe) {
        case ABE_LEFT:   return TaskbarEdge::Left;
        case ABE_TOP:    return TaskbarEdge::Top;
        case ABE_RIGHT:  return TaskbarEdge::Right;
        case ABE_BOTTOM: return TaskbarEdge::Bottom;
        default:         return TaskbarEdge::Unknown;
    }
}

}  // namespace

DesktopMetrics query(HWND hwnd) {
    DesktopMetrics m;

    // 工作区与 DPI 都跟显示器走，不用全局值，多显示器下才正确。
    HMONITOR mon = hwnd ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)
                        : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof(MONITORINFO)};
    if (GetMonitorInfoW(mon, &mi)) {
        m.workArea = mi.rcWork;
        m.monitor  = mi.rcMonitor;
    }

    UINT dx = 96, dy = 96;
    if (SUCCEEDED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy))) {
        m.dpi = dx;
    }

    // 任务栏状态。ABM_GETTASKBARPOS 给的是主任务栏，够 P1 用；
    // 副屏的 Shell_SecondaryTrayWnd 留到 设计文档 §2.2 边界 6 再处理。
    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    if (SHAppBarMessage(ABM_GETTASKBARPOS, &abd)) {
        m.taskbar      = abd.rc;
        m.edge         = edge_from_abe(abd.uEdge);
        m.taskbarValid = true;
    }

    APPBARDATA st{};
    st.cbSize = sizeof(st);
    const UINT_PTR state = SHAppBarMessage(ABM_GETSTATE, &st);
    m.taskbarAutoHide = (state & ABS_AUTOHIDE) != 0;

    // 通知区域：Shell_TrayWnd 下的 TrayNotifyWnd。用来把栖位让开时钟和托盘图标。
    if (HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr)) {
        if (HWND notify = FindWindowExW(tray, nullptr, L"TrayNotifyWnd", nullptr)) {
            GetWindowRect(notify, &m.notifyArea);
        }
    }

    return m;
}

POINT dock_position(const DesktopMetrics& m, int winW, int winH, int embedPx) {
    // 退化路径：任务栏取不到，或任务栏自动隐藏（设计文档 §2.2 边界 1）。
    // 两种情况都不压入，直接贴工作区右下角。
    const bool canEmbed = m.taskbarValid && !m.taskbarAutoHide && m.edge == TaskbarEdge::Bottom;
    if (!canEmbed) {
        return POINT{m.workArea.right - winW - kEdgeMargin, m.workArea.bottom - winH - kEdgeMargin};
    }

    // 任务栏在下边缘：底边压到任务栏顶边以下 embedPx。
    const LONG y = m.taskbar.top - winH + embedPx;

    // 紧贴屏幕右边缘。
    //
    // 早先这里让开了通知区域的宽度，那是多余的：宠物站在任务栏上方，
    // 只有底部 embedPx 那一条压进任务栏，横向根本不会盖住时钟和托盘图标。
    // 让开 450 px 反而把它推到了屏幕中间偏右。
    LONG x = m.workArea.right - winW - kEdgeMargin;

    // 夹回工作区，避免多显示器或窗口过宽时跑到屏幕外。
    x = std::clamp(x, m.workArea.left, m.workArea.right - winW);
    return POINT{x, y};
}

bool fullscreen_or_presenting() {
    // 投影模式与独占全屏的 D3D 还是问系统。
    // 不再看 QUNS_BUSY：系统判「忙」的依据是「有一个盖满显示器的置顶窗口」，
    // 而 1.2 起覆盖层自己就是这样一个窗口。1.4 用它的结果是每 2 秒把自己藏起来、
    // 系统随即说「不忙」、再显示、再被判忙——作者看到的就是狗一秒一闪。
    QUERY_USER_NOTIFICATION_STATE s{};
    if (SUCCEEDED(SHQueryUserNotificationState(&s)) &&
        (s == QUNS_RUNNING_D3D_FULL_SCREEN || s == QUNS_PRESENTATION_MODE)) {
        return true;
    }

    // 全屏应用：前台窗口属于别的进程，且它的矩形盖满了自己所在的显示器。
    // 桌面（Progman / WorkerW）和任务栏不算。
    HWND fg = GetForegroundWindow();
    if (!fg || !IsWindowVisible(fg) || IsIconic(fg)) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == GetCurrentProcessId()) return false;
    wchar_t cls[64]{};
    GetClassNameW(fg, cls, 64);
    if (wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0 ||
        wcscmp(cls, L"Shell_TrayWnd") == 0 || wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0) {
        return false;
    }
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    HMONITOR mon = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
    if (!mon || !GetMonitorInfoW(mon, &mi)) return false;
    RECT wr{};
    if (!GetWindowRect(fg, &wr)) return false;
    return wr.left <= mi.rcMonitor.left && wr.top <= mi.rcMonitor.top &&
           wr.right >= mi.rcMonitor.right && wr.bottom >= mi.rcMonitor.bottom;
}

}  // namespace pet::win
