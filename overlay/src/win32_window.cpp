#include "overlay/win32_window.h"

#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM
#include <wtsapi32.h>  // WTSRegisterSessionNotification：锁屏 / 解锁通知

#include <cstdio>

namespace pet::win {
namespace {
constexpr wchar_t kClassName[] = L"PetOverlayWindow";
}

void enable_utf8_console() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

bool OverlayWindow::create(const wchar_t* title, int width, int height) {
    width_ = width;
    height_ = height;

    HINSTANCE hinst = GetModuleHandleW(nullptr);

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = &OverlayWindow::wnd_proc;
        wc.hInstance     = hinst;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        if (!RegisterClassExW(&wc)) {
            std::printf("  [FAIL] RegisterClassExW  err=%lu\n", GetLastError());
            return false;
        }
        registered = true;
    }

    hwnd_ = CreateWindowExW(
        // WS_EX_NOACTIVATE：点狗不许把覆盖层变成前台窗口。窗口是整个屏幕，
        // 一旦成为前台的置顶全屏窗口，系统会按「全屏应用」处理：任务栏让位、键盘焦点被拿走，
        // 用户就操作不了别的东西了。1.2 第一版就是这么坏的。
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kClassName, title,
        WS_POPUP,
        0, 0, width_, height_,
        nullptr, nullptr, hinst, this);
    if (!hwnd_) {
        std::printf("  [FAIL] CreateWindowExW  err=%lu\n", GetLastError());
        return false;
    }
    // 锁屏 / 解锁通知。失败不算致命：只是锁屏时不会停 tick。
    sessionRegistered_ = WTSRegisterSessionNotification(hwnd_, NOTIFY_FOR_THIS_SESSION) != 0;
    if (!sessionRegistered_) std::printf("  [WARN] WTSRegisterSessionNotification 失败 err=%lu\n", GetLastError());
    return true;
}

void OverlayWindow::destroy() {
    if (hwnd_) {
        if (sessionRegistered_) WTSUnRegisterSessionNotification(hwnd_);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void OverlayWindow::show_no_activate() {
    if (hwnd_) ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
}

void OverlayWindow::hide() {
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

void OverlayWindow::move_to(POINT pt) {
    if (!hwnd_) return;
    // SWP_NOACTIVATE：移动不抢焦点。覆盖层任何时候都不该拿走用户的输入焦点。
    SetWindowPos(hwnd_, HWND_TOPMOST, pt.x, pt.y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE);
}

bool OverlayWindow::set_bounds(RECT r) {
    if (!hwnd_) return false;
    width_ = r.right - r.left;
    height_ = r.bottom - r.top;
    if (width_ <= 0 || height_ <= 0) return false;
    return SetWindowPos(hwnd_, HWND_TOPMOST, r.left, r.top, width_, height_,
                        SWP_NOACTIVATE) != 0;
}

void OverlayWindow::set_region(HRGN rgn) {
    if (hwnd_) SetWindowRgn(hwnd_, rgn, FALSE);
}

bool OverlayWindow::pump() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return !quit_;
}

LRESULT CALLBACK OverlayWindow::wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    OverlayWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<OverlayWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->handle(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT OverlayWindow::handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // 钩子先看。托盘的通知消息和 TaskbarCreated 都从这里进。
    if (hook_) {
        LRESULT r = 0;
        if (hook_(hwnd, msg, wp, lp, r)) return r;
    }

    switch (msg) {
        case WM_NCHITTEST: {
            // 设计文档 §3.3：透明区返回 HTTRANSPARENT，点击落到下层窗口；
            // 本体区返回 HTCLIENT，或在可拖动模式下返回 HTCAPTION。
            // WM_NCHITTEST 的坐标是有符号的屏幕坐标，多显示器下可能为负，
            // 必须用 GET_X_LPARAM 而不是 LOWORD。
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            // 没有命中回调时一律穿透：窗口是整个屏幕，宁可点不到狗，也不能挡住整个桌面。
            const bool onBody = hitTest_ ? hitTest_(pt.x, pt.y) : false;
            if (!onBody) return HTTRANSPARENT;
            return draggable_ ? HTCAPTION : HTCLIENT;
        }
        case WM_MOUSEACTIVATE:
            // 鼠标点击不激活、不拿焦点，消息照常收。
            return MA_NOACTIVATE;
        case WM_SETCURSOR: {
            if (LOWORD(lp) == HTCLIENT && handCursor_) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        // 不用 SetCapture。本窗口带 WS_EX_NOACTIVATE，是后台窗口；Windows 规定只有前台窗口
        // 能完整捕获鼠标，后台窗口捕获后只收得到光标落在自己可见区域内的事件——
        // 松开左键时光标不在狗上，WM_LBUTTONUP 就丢了，ReleaseCapture 永远不执行，
        // 整个桌面从此点不动。松开由宿主每帧用 GetAsyncKeyState 轮询。
        case WM_LBUTTONDOWN:
            if (mouse_) mouse_({MouseEvent::Type::Down, GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            return 0;
        case WM_LBUTTONUP:
            if (mouse_) mouse_({MouseEvent::Type::Up, GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            return 0;
        case WM_MOUSEMOVE:
            if (mouse_) mouse_({MouseEvent::Type::Move, GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            return 0;
        case WM_RBUTTONDOWN:
            // 右键 = 取消：放开一切正在进行的互动。作者要求的保险。
            if (mouse_) mouse_({MouseEvent::Type::RightDown, GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            return 0;
        case WM_WTSSESSION_CHANGE:
            if (session_) {
                if (wp == WTS_SESSION_LOCK) session_({SessionEvent::Type::Locked});
                else if (wp == WTS_SESSION_UNLOCK) session_({SessionEvent::Type::Unlocked});
            }
            return 0;
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            if (session_) session_({SessionEvent::Type::DisplayChanged});
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) quit_ = true;
            return 0;
        case WM_CLOSE:
            quit_ = true;
            return 0;
        case WM_DESTROY:
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

}  // namespace pet::win
