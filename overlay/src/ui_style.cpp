#include "overlay/ui_style.h"

namespace pet::win::ui {
namespace {

HFONT make_font(float s, int px, int weight) {
    return CreateFontW(-static_cast<int>(px * s), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
}

HBRUSH g_bgBrush = nullptr;

HBRUSH bg_brush() {
    if (!g_bgBrush) g_bgBrush = CreateSolidBrush(kBg);
    return g_bgBrush;
}

}  // namespace

std::wstring wide(const char* utf8) {
    if (!utf8 || !*utf8) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w.data(), n);
    return w;
}

void Fonts::create(float s) {
    destroy();
    normal = make_font(s, 14, FW_NORMAL);
    bold   = make_font(s, 14, FW_BOLD);
    title  = make_font(s, 19, FW_BOLD);
}

void Fonts::destroy() {
    for (HFONT* f : {&normal, &bold, &title}) {
        if (*f) { DeleteObject(*f); *f = nullptr; }
    }
}

void apply_round_region(HWND hwnd, int w, int h, float s) {
    const int r = static_cast<int>(18 * s);
    SetWindowRgn(hwnd, CreateRoundRectRgn(0, 0, w + 1, h + 1, r, r), TRUE);
}

int paint_frame(HDC dc, const RECT& rc, float s, const wchar_t* title, const Fonts& fonts) {
    FillRect(dc, &rc, bg_brush());
    HPEN pen = CreatePen(PS_SOLID, static_cast<int>(2 * s), kBorder);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    const int r = static_cast<int>(18 * s);
    RoundRect(dc, 0, 0, rc.right, rc.bottom, r, r);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    if (!title || !*title) return 0;
    const int pad = static_cast<int>(16 * s);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kBorder);
    HGDIOBJ old = SelectObject(dc, fonts.title);
    RECT tr{pad, pad, rc.right - pad, pad + static_cast<int>(28 * s)};
    DrawTextW(dc, title, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, old);
    return pad + static_cast<int>(34 * s);
}

void paint_field_border(HDC dc, const RECT& rc, float s) {
    HPEN pen = CreatePen(PS_SOLID, 1, kSoft);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    const int m = static_cast<int>(3 * s);
    const int r = static_cast<int>(8 * s);
    RoundRect(dc, rc.left - m, rc.top - m, rc.right + m, rc.bottom + m, r, r);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

HBRUSH ctl_color(HDC dc, bool muted) {
    SetTextColor(dc, muted ? kMuted : kText);
    SetBkColor(dc, kBg);
    SetBkMode(dc, OPAQUE);
    return bg_brush();
}

void draw_button(const DRAWITEMSTRUCT* dis, const Fonts& fonts, bool primary) {
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    const COLORREF fill = primary ? (pressed ? RGB(120, 68, 28) : kBorder) : (pressed ? kTrack : kBg);
    const COLORREF text = primary ? kBg : (disabled ? kMuted : kBorder);
    // 先铺父窗口底色，圆角外面才不会露出按钮的默认灰。
    FillRect(dc, &rc, bg_brush());
    HBRUSH b = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, kBorder);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, pen);
    const int r = static_cast<int>(10 * (rc.bottom - rc.top) / 28.0f);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, r, r);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(pen);
    wchar_t label[64]{};
    GetWindowTextW(dis->hwndItem, label, 64);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, text);
    HGDIOBJ of = SelectObject(dc, primary ? fonts.bold : fonts.normal);
    DrawTextW(dc, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, of);
    if (dis->itemState & ODS_FOCUS) {
        RECT fr = rc;
        InflateRect(&fr, -3, -3);
        SetTextColor(dc, kSoft);
        DrawFocusRect(dc, &fr);
    }
}

void measure_menu_item(MEASUREITEMSTRUCT* mis, float s, const Fonts& fonts) {
    const auto* item = reinterpret_cast<const MenuItem*>(mis->itemData);
    if (!item) return;
    if (item->separator) {
        mis->itemWidth = 10;
        mis->itemHeight = static_cast<UINT>(10 * s);
        return;
    }
    HDC dc = GetDC(nullptr);
    HGDIOBJ old = SelectObject(dc, fonts.normal);
    SIZE sz{};
    GetTextExtentPoint32W(dc, item->text.c_str(), static_cast<int>(item->text.size()), &sz);
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
    mis->itemWidth = static_cast<UINT>(sz.cx + 44 * s);   // 左边留勾的位置，右边留边距
    mis->itemHeight = static_cast<UINT>(30 * s);
}

void draw_menu_item(const DRAWITEMSTRUCT* dis, float s, const Fonts& fonts) {
    const auto* item = reinterpret_cast<const MenuItem*>(dis->itemData);
    if (!item) return;
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;
    if (item->separator) {
        FillRect(dc, &rc, bg_brush());
        HPEN pen = CreatePen(PS_SOLID, 1, kTrack);
        HGDIOBJ op = SelectObject(dc, pen);
        const int y = (rc.top + rc.bottom) / 2;
        MoveToEx(dc, rc.left + static_cast<int>(10 * s), y, nullptr);
        LineTo(dc, rc.right - static_cast<int>(10 * s), y);
        SelectObject(dc, op);
        DeleteObject(pen);
        return;
    }
    const bool hot = (dis->itemState & ODS_SELECTED) != 0;
    HBRUSH b = CreateSolidBrush(hot ? kHover : kBg);
    FillRect(dc, &rc, b);
    DeleteObject(b);
    if (hot) {
        // 选中项：左边一条红色竖线，比整块反色柔和。
        RECT bar{rc.left + static_cast<int>(4 * s), rc.top + static_cast<int>(6 * s), rc.left + static_cast<int>(7 * s), rc.bottom - static_cast<int>(6 * s)};
        HBRUSH ab = CreateSolidBrush(kAccent);
        FillRect(dc, &bar, ab);
        DeleteObject(ab);
    }
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ of = SelectObject(dc, fonts.normal);
    if (item->checked) {
        SetTextColor(dc, kAccent);
        RECT cr{rc.left + static_cast<int>(12 * s), rc.top, rc.left + static_cast<int>(30 * s), rc.bottom};
        DrawTextW(dc, L"✓", -1, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    SetTextColor(dc, hot ? kBorder : kText);
    RECT tr{rc.left + static_cast<int>(32 * s), rc.top, rc.right - static_cast<int>(12 * s), rc.bottom};
    DrawTextW(dc, item->text.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, of);
}

HBRUSH menu_background() { return bg_brush(); }

}  // namespace pet::win::ui
