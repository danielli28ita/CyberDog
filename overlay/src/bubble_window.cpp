#include "overlay/bubble_window.h"

#include <cstdio>
#include <cstring>

namespace pet::win {
namespace {

constexpr wchar_t kClassName[] = L"PetBubbleWindow";

// GDI 画完的像素 alpha 字节是 0，分层窗口会把它们当全透明。
// 办法：先用一个不会在气泡里出现的哨兵色铺底，画完之后逐像素判断——
// 等于哨兵色的置 alpha=0，其余置 alpha=255。气泡里没有半透明，所以预乘不用算。
constexpr COLORREF kSentinel = RGB(255, 0, 255);

}  // namespace

BubbleWindow::~BubbleWindow() { destroy(); }

bool BubbleWindow::ensure_window() {
    if (hwnd_) return true;
    HINSTANCE hinst = GetModuleHandleW(nullptr);
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = DefWindowProcW;
        wc.hInstance     = hinst;
        wc.lpszClassName = kClassName;
        if (!RegisterClassExW(&wc)) {
            std::printf("  [FAIL] 气泡窗口类注册  err=%lu\n", GetLastError());
            return false;
        }
        registered = true;
    }
    hwnd_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        kClassName, L"", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, hinst, nullptr);
    if (!hwnd_) {
        std::printf("  [FAIL] 气泡窗口创建  err=%lu\n", GetLastError());
        return false;
    }
    return true;
}

bool BubbleWindow::show(POINT anchor, const wchar_t* text, UINT dpi, Style style) {
    if (!ensure_window()) return false;
    const bool tag = style == Style::Tag;
    const float s = static_cast<float>(dpi) / 96.0f;
    const int pad = static_cast<int>((tag ? 8 : 14) * s);
    const int tail = tag ? 0 : static_cast<int>(12 * s);
    const int radius = static_cast<int>((tag ? 14 : 20) * s);

    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HFONT font = CreateFontW(-static_cast<int>((tag ? 16 : 15) * s), 0, 0, 0, tag ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    HGDIOBJ oldFont = SelectObject(dc, font);

    RECT tr{0, 0, static_cast<int>(320 * s), 0};
    DrawTextW(dc, text, -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    const int w = tr.right + pad * 2;
    const int h = tr.bottom + pad * 2 + tail;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;   // 自上而下
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) {
        SelectObject(dc, oldFont); DeleteObject(font); DeleteDC(dc); ReleaseDC(nullptr, screen);
        return false;
    }
    HGDIOBJ oldBmp = SelectObject(dc, bmp);

    // 哨兵底色
    HBRUSH sentinel = CreateSolidBrush(kSentinel);
    RECT full{0, 0, w, h};
    FillRect(dc, &full, sentinel);
    DeleteObject(sentinel);

    // 圆角框 + 朝下的小尾巴（尾巴在靠右的位置，狗头在气泡右下方）
    // 先画一层往右下偏 2 px 的暗色，当投影；再画本体。比格配色：奶油底、棕边。
    HBRUSH shadow = CreateSolidBrush(RGB(205, 190, 170));
    HGDIOBJ oldBrush0 = SelectObject(dc, shadow);
    HPEN nopen0 = CreatePen(PS_SOLID, 1, RGB(205, 190, 170));
    HGDIOBJ oldPen0 = SelectObject(dc, nopen0);
    RoundRect(dc, static_cast<int>(2 * s), static_cast<int>(2 * s), w, h - tail + static_cast<int>(2 * s), radius, radius);
    SelectObject(dc, oldPen0);
    SelectObject(dc, oldBrush0);
    DeleteObject(nopen0);
    DeleteObject(shadow);

    HBRUSH fill = CreateSolidBrush(RGB(253, 250, 243));
    HPEN   pen  = CreatePen(PS_SOLID, static_cast<int>(1.5f * s), RGB(158, 92, 41));
    HGDIOBJ oldBrush = SelectObject(dc, fill);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, 0, 0, w - static_cast<int>(2 * s), h - tail, radius, radius);
    tailX_ = tag ? w / 2 : w - static_cast<int>(48 * s);
    if (!tag) {
        const int cx = tailX_;
        POINT tri[3] = {{cx - tail / 2, h - tail - 1}, {cx + tail / 2, h - tail - 1}, {cx, h - 1}};
        Polygon(dc, tri, 3);
        // 盖掉三角形和框之间那条线
        HPEN nopen = CreatePen(PS_SOLID, 1, RGB(253, 250, 243));
        HGDIOBJ p2 = SelectObject(dc, nopen);
        MoveToEx(dc, cx - tail / 2 + 1, h - tail - 1, nullptr);
        LineTo(dc, cx + tail / 2, h - tail - 1);
        SelectObject(dc, p2);
        DeleteObject(nopen);
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, tag ? RGB(214, 36, 58) : RGB(60, 38, 18));   // 标签是红字
    RECT textRect{pad, pad, w - pad, h - tail - pad};
    DrawTextW(dc, text, -1, &textRect, DT_WORDBREAK | DT_NOPREFIX);
    GdiFlush();

    // 置 alpha
    auto* px = static_cast<unsigned char*>(bits);
    for (int i = 0; i < w * h; ++i) {
        unsigned char* p = px + i * 4;   // BGRA
        const bool transparent = p[0] == 255 && p[1] == 0 && p[2] == 255;
        p[3] = transparent ? 0 : 255;
        if (transparent) p[0] = p[1] = p[2] = 0;
    }

    // 位置：尾巴尖（标签是底边中点）对着锚点。
    w_ = w;
    h_ = h;
    POINT dst = place(anchor);
    POINT src{0, 0};
    SIZE  sz{w, h};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    if (!UpdateLayeredWindow(hwnd_, screen, &dst, &sz, dc, &src, 0, &bf, ULW_ALPHA)) {
        std::printf("  [FAIL] UpdateLayeredWindow(气泡)  err=%lu\n", GetLastError());
    }

    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldBmp);
    SelectObject(dc, oldFont);
    DeleteObject(pen);
    DeleteObject(fill);
    DeleteObject(bmp);
    DeleteObject(font);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);

    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    shown_ = true;
    return true;
}

POINT BubbleWindow::place(POINT anchor) const {
    POINT dst{anchor.x - tailX_, anchor.y - h_};
    // 狗贴着屏幕右边时标签别出屏幕：夹回锚点所在显示器。
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (HMONITOR mon = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST); mon && GetMonitorInfoW(mon, &mi)) {
        if (dst.x + w_ > mi.rcMonitor.right) dst.x = mi.rcMonitor.right - w_;
        if (dst.x < mi.rcMonitor.left) dst.x = mi.rcMonitor.left;
        if (dst.y < mi.rcMonitor.top) dst.y = mi.rcMonitor.top;
    }
    return dst;
}

void BubbleWindow::move_anchor(POINT anchor) {
    if (!hwnd_ || !shown_) return;
    const POINT dst = place(anchor);
    SetWindowPos(hwnd_, HWND_TOPMOST, dst.x, dst.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

void BubbleWindow::hide() {
    if (hwnd_ && shown_) ShowWindow(hwnd_, SW_HIDE);
    shown_ = false;
}

void BubbleWindow::destroy() {
    if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }
    shown_ = false;
}

}  // namespace pet::win
