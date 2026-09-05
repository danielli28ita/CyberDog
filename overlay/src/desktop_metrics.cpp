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

DesktopMetrics query_monitor(HMONITOR mon) {
    DesktopMetrics m;
    if (!mon) mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFO mi{sizeof(MONITORINFO)};
    if (GetMonitorInfoW(mon, &mi)) {
        m.workArea = mi.rcWork;
        m.monitor  = mi.rcMonitor;
    }

    UINT dx = 96, dy = 96;
    if (SUCCEEDED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy))) {
        m.dpi = dx;
    }

    // 任务栏：优先取与本显示器同屏的 Shell_TrayWnd / Shell_SecondaryTrayWnd；
    // 找不到再退回 ABM_GETTASKBARPOS（通常是主屏）。
    struct TrayFind {
        HMONITOR mon = nullptr;
        RECT     rc{};
        bool     found = false;
    } trayFind;
    trayFind.mon = mon;
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            auto* d = reinterpret_cast<TrayFind*>(lp);
            wchar_t cls[64]{};
            GetClassNameW(h, cls, 64);
            if (wcscmp(cls, L"Shell_TrayWnd") != 0 && wcscmp(cls, L"Shell_SecondaryTrayWnd") != 0)
                return TRUE;
            if (MonitorFromWindow(h, MONITOR_DEFAULTTONULL) != d->mon) return TRUE;
            if (!IsWindowVisible(h)) return TRUE;
            GetWindowRect(h, &d->rc);
            d->found = true;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&trayFind));

    if (trayFind.found) {
        m.taskbar = trayFind.rc;
        m.taskbarValid = true;
        const LONG tw = trayFind.rc.right - trayFind.rc.left;
        const LONG th = trayFind.rc.bottom - trayFind.rc.top;
        if (th <= tw && trayFind.rc.bottom >= m.monitor.bottom - 2) m.edge = TaskbarEdge::Bottom;
        else if (th <= tw && trayFind.rc.top <= m.monitor.top + 2) m.edge = TaskbarEdge::Top;
        else if (tw < th && trayFind.rc.left <= m.monitor.left + 2) m.edge = TaskbarEdge::Left;
        else if (tw < th && trayFind.rc.right >= m.monitor.right - 2) m.edge = TaskbarEdge::Right;
        else m.edge = TaskbarEdge::Bottom;
    } else {
        APPBARDATA abd{};
        abd.cbSize = sizeof(abd);
        if (SHAppBarMessage(ABM_GETTASKBARPOS, &abd)) {
            RECT inter{};
            if (IntersectRect(&inter, &abd.rc, &m.monitor)) {
                m.taskbar = abd.rc;
                m.edge = edge_from_abe(abd.uEdge);
                m.taskbarValid = true;
            }
        }
    }

    APPBARDATA st{};
    st.cbSize = sizeof(st);
    const UINT_PTR state = SHAppBarMessage(ABM_GETSTATE, &st);
    m.taskbarAutoHide = (state & ABS_AUTOHIDE) != 0;

    if (HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr)) {
        if (MonitorFromWindow(tray, MONITOR_DEFAULTTONULL) == mon) {
            if (HWND notify = FindWindowExW(tray, nullptr, L"TrayNotifyWnd", nullptr)) {
                GetWindowRect(notify, &m.notifyArea);
            }
        }
    }

    return m;
}

DesktopMetrics query(HWND hwnd) {
    HMONITOR mon = hwnd ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)
                        : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    return query_monitor(mon);
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
    // 只信系统明确给出的两种：独占全屏游戏、投影/幻灯片。
    // 不再自己比较前台窗是否盖满显示器——最大化 Chrome/资源管理器/VS 等会误藏。
    // 也不看 QUNS_BUSY：覆盖层自己就是盖满显示器的置顶窗，会被判忙（1.5）。
    QUERY_USER_NOTIFICATION_STATE s{};
    if (FAILED(SHQueryUserNotificationState(&s))) return false;
    return s == QUNS_RUNNING_D3D_FULL_SCREEN || s == QUNS_PRESENTATION_MODE;
}

}  // namespace pet::win
