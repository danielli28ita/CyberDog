#include "overlay/stats_panel.h"

#include <cstdio>

#include "core/i18n.h"
#include "overlay/ui_style.h"

namespace pet::win {
namespace {

constexpr wchar_t kClassName[] = L"PetStatsPanel";

// 比格配色，和气泡一致：奶油底、棕边、深棕字。
constexpr COLORREF kBg     = RGB(253, 250, 243);
constexpr COLORREF kBorder = RGB(158, 92, 41);
constexpr COLORREF kText   = RGB(60, 38, 18);
constexpr COLORREF kMuted  = RGB(140, 118, 96);
constexpr COLORREF kBarBg  = RGB(232, 222, 206);
constexpr COLORREF kBar    = RGB(214, 36, 58);
constexpr COLORREF kBarSoft = RGB(196, 132, 74);

}  // namespace

void StatsPanel::layout(UINT dpi) {
    s_ = static_cast<float>(dpi) / 96.0f;
    pad_ = static_cast<int>(16 * s_);
    rowH_ = static_cast<int>(24 * s_);
    // 英文 / 意大利文的标签比中文长（Estroversione、Carezze / colpi），列宽和面板都放宽。
    const bool wideLabels = pet::language() != pet::Lang::Zh;
    labelW_ = static_cast<int>((wideLabels ? 118 : 72) * s_);
    w_ = static_cast<int>((wideLabels ? 360 : 300) * s_);
    // 标题一行加大，小节标题行前留半行。
    int h = pad_ + static_cast<int>(34 * s_);
    for (const Row& r : rows_) h += r.heading ? rowH_ + rowH_ / 2 : rowH_;
    h += static_cast<int>(22 * s_) + pad_;   // 底部提示
    h_ = h;
}

bool StatsPanel::show(const RECT& anchor, const std::wstring& title, const std::vector<Row>& rows, UINT dpi) {
    title_ = title;
    rows_ = rows;
    layout(dpi);

    HINSTANCE hinst = GetModuleHandleW(nullptr);
    if (!hwnd_) {
        static bool registered = false;
        if (!registered) {
            WNDCLASSEXW wc{};
            wc.cbSize        = sizeof(wc);
            wc.lpfnWndProc   = &StatsPanel::wnd_proc;
            wc.hInstance     = hinst;
            wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
            wc.lpszClassName = kClassName;
            if (!RegisterClassExW(&wc)) return false;
            registered = true;
        }
        hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClassName, ui::wide(pet::tr(Str::StatsWindow)).c_str(),
                                WS_POPUP, 0, 0, w_, h_, nullptr, nullptr, hinst, this);
        if (!hwnd_) return false;
        font_ = CreateFontW(-static_cast<int>(14 * s_), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        fontBold_ = CreateFontW(-static_cast<int>(14 * s_), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        fontTitle_ = CreateFontW(-static_cast<int>(19 * s_), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    }

    // 位置：狗的左上方；出了工作区就夹回来。
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    POINT c{(anchor.left + anchor.right) / 2, (anchor.top + anchor.bottom) / 2};
    RECT work{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    if (HMONITOR mon = MonitorFromPoint(c, MONITOR_DEFAULTTONEAREST); mon && GetMonitorInfoW(mon, &mi)) work = mi.rcWork;
    int x = anchor.left - w_ - static_cast<int>(12 * s_);
    int y = anchor.top - h_ / 3;
    if (x < work.left) x = anchor.right + static_cast<int>(12 * s_);
    if (x + w_ > work.right) x = work.right - w_;
    if (y + h_ > work.bottom) y = work.bottom - h_;
    if (y < work.top) y = work.top;

    // 圆角靠窗口区域裁出来，本身不是分层窗口。
    const int r = static_cast<int>(18 * s_);
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w_, h_, SWP_NOACTIVATE);
    SetWindowRgn(hwnd_, CreateRoundRectRgn(0, 0, w_ + 1, h_ + 1, r, r), TRUE);
    InvalidateRect(hwnd_, nullptr, TRUE);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    return true;
}

void StatsPanel::hide() {
    if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }
    for (HFONT* f : {&font_, &fontBold_, &fontTitle_}) {
        if (*f) { DeleteObject(*f); *f = nullptr; }
    }
}

void StatsPanel::paint(HDC dc, const RECT& rc) {
    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    // 棕边
    HPEN pen = CreatePen(PS_SOLID, static_cast<int>(2 * s_), kBorder);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    const int r = static_cast<int>(18 * s_);
    RoundRect(dc, 0, 0, rc.right, rc.bottom, r, r);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    SetBkMode(dc, TRANSPARENT);
    int y = pad_;
    {
        HGDIOBJ old = SelectObject(dc, fontTitle_);
        SetTextColor(dc, kBorder);
        RECT tr{pad_, y, rc.right - pad_, y + static_cast<int>(28 * s_)};
        DrawTextW(dc, title_.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, old);
        y += static_cast<int>(34 * s_);
    }
    for (const Row& row : rows_) {
        if (row.heading) {
            y += rowH_ / 2;
            HGDIOBJ old = SelectObject(dc, fontBold_);
            SetTextColor(dc, kBorder);
            RECT tr{pad_, y, rc.right - pad_, y + rowH_};
            DrawTextW(dc, row.label.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            // 标题下一条细线
            HPEN thin = CreatePen(PS_SOLID, 1, kBarBg);
            HGDIOBJ op = SelectObject(dc, thin);
            MoveToEx(dc, pad_, y + rowH_ - 2, nullptr);
            LineTo(dc, rc.right - pad_, y + rowH_ - 2);
            SelectObject(dc, op);
            DeleteObject(thin);
            SelectObject(dc, old);
            y += rowH_;
            continue;
        }
        HGDIOBJ old = SelectObject(dc, font_);
        SetTextColor(dc, kMuted);
        RECT lr{pad_, y, pad_ + labelW_, y + rowH_};
        DrawTextW(dc, row.label.c_str(), -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SetTextColor(dc, kText);
        if (row.bar >= 0.0f) {
            // 值靠右一小块，中间是条。
            const int valueW = static_cast<int>(52 * s_);
            RECT vr{rc.right - pad_ - valueW, y, rc.right - pad_, y + rowH_};
            DrawTextW(dc, row.value.c_str(), -1, &vr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            const int bx0 = pad_ + labelW_, bx1 = rc.right - pad_ - valueW - static_cast<int>(8 * s_);
            const int by0 = y + rowH_ / 2 - static_cast<int>(4 * s_), by1 = y + rowH_ / 2 + static_cast<int>(4 * s_);
            RECT track{bx0, by0, bx1, by1};
            HBRUSH tb = CreateSolidBrush(kBarBg);
            FillRect(dc, &track, tb);
            DeleteObject(tb);
            const float v = row.bar > 1.0f ? 1.0f : row.bar;
            RECT fill{bx0, by0, bx0 + static_cast<int>((bx1 - bx0) * v), by1};
            HBRUSH fb = CreateSolidBrush(row.accent ? kBar : kBarSoft);
            FillRect(dc, &fill, fb);
            DeleteObject(fb);
        } else {
            RECT vr{pad_ + labelW_, y, rc.right - pad_, y + rowH_};
            DrawTextW(dc, row.value.c_str(), -1, &vr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
        SelectObject(dc, old);
        y += rowH_;
    }
    {
        HGDIOBJ old = SelectObject(dc, font_);
        SetTextColor(dc, kMuted);
        RECT tr{pad_, rc.bottom - pad_ - static_cast<int>(20 * s_), rc.right - pad_, rc.bottom - pad_};
        DrawTextW(dc, ui::wide(pet::tr(Str::StatsCloseHint)).c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, old);
    }
}

LRESULT CALLBACK StatsPanel::wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    StatsPanel* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<StatsPanel*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    self = reinterpret_cast<StatsPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            // 画到内存位图再贴，不闪。
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HGDIOBJ old = SelectObject(mem, bmp);
            self->paint(mem, rc);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_NCHITTEST: {
            const LRESULT r = DefWindowProcW(hwnd, msg, wp, lp);
            return r == HTCLIENT ? HTCAPTION : r;   // 按住任意处可拖
        }
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) { self->hide(); return 0; }
            break;
        case WM_ACTIVATE:
            // 点了别处就关。拖动结束也会来一次 WA_ACTIVE，不会误关。
            if (LOWORD(wp) == WA_INACTIVE) { PostMessageW(hwnd, WM_CLOSE, 0, 0); return 0; }
            break;
        case WM_CLOSE:
            self->hide();
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace pet::win
