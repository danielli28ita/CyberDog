#include "overlay/memo_dialog.h"

#include "core/i18n.h"

namespace pet::win {
namespace {

constexpr wchar_t kClassName[] = L"PetMemoDialog";
constexpr int kIdList = 201, kIdText = 202, kIdWhen = 203, kIdAdd = 204, kIdRemove = 205, kIdClose = 206;
// 提示文案见 core/i18n（MemoHintDefault）。

WNDPROC g_editOldProc = nullptr;

// 输入框里回车 = 添加，Esc = 关闭。和改名窗口同一个办法。
LRESULT CALLBACK edit_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE)) {
        SendMessageW(GetParent(hwnd), WM_KEYDOWN, wp, lp);
        return 0;
    }
    if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0;
    return CallWindowProcW(g_editOldProc, hwnd, msg, wp, lp);
}

}  // namespace

bool MemoDialog::open(UINT dpi, Callbacks cb) {
    cb_ = std::move(cb);
    if (hwnd_) { SetForegroundWindow(hwnd_); refresh(); return true; }

    HINSTANCE hinst = GetModuleHandleW(nullptr);
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = &MemoDialog::wnd_proc;
        wc.hInstance     = hinst;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        if (!RegisterClassExW(&wc)) return false;
        registered = true;
    }
    s_ = static_cast<float>(dpi) / 96.0f;
    const float s = s_;
    fonts_.create(s);
    hint_ = ui::wide(tr(Str::MemoHintDefault));

    const int w = static_cast<int>(480 * s);
    const int pad = static_cast<int>(16 * s);
    const int eh = static_cast<int>(30 * s);
    const int bw = static_cast<int>(72 * s);
    const int labelW = static_cast<int>(40 * s);
    const int listH = static_cast<int>(160 * s);
    const int titleH = pad + static_cast<int>(34 * s);
    const int hintH = static_cast<int>(44 * s);
    const int h = titleH + listH + pad + eh + pad + eh + pad + hintH + pad;
    const int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClassName, ui::wide(tr(Str::MemoTitle)).c_str(), WS_POPUP,
                            (sx - w) / 2, (sy - h) / 2, w, h, nullptr, nullptr, hinst, this);
    if (!hwnd_) return false;
    ui::apply_round_region(hwnd_, w, h, s);

    auto make = [&](const wchar_t* cls, const wchar_t* txt, DWORD style, const RECT& r, int id) {
        HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style, r.left, r.top, r.right - r.left, r.bottom - r.top,
                                 hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hinst, nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(fonts_.normal), TRUE);
        return c;
    };
    const int in = static_cast<int>(4 * s);   // 控件比它的框缩进一点，框由父窗口画
    int y = titleH;
    listRc_ = RECT{pad + in, y + in, w - pad - in, y + listH - in};
    list_ = make(L"LISTBOX", L"", WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, listRc_, kIdList);
    y += listH + pad;
    textLabelRc_ = RECT{pad, y, pad + labelW, y + eh};
    textRc_ = RECT{pad + labelW + in, y + in, w - pad - in, y + eh - in};
    text_ = make(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, textRc_, kIdText);
    y += eh + pad;
    whenLabelRc_ = RECT{pad, y, pad + labelW, y + eh};
    whenRc_ = RECT{pad + labelW + in, y + in, pad + labelW + static_cast<int>(130 * s), y + eh - in};
    when_ = make(L"EDIT", L"+30", WS_TABSTOP | ES_AUTOHSCROLL, whenRc_, kIdWhen);
    make(L"BUTTON", ui::wide(tr(Str::MemoAdd)).c_str(), WS_TABSTOP | BS_OWNERDRAW, RECT{w - pad - bw * 3 - pad * 2, y, w - pad - bw * 2 - pad * 2 + bw, y + eh}, kIdAdd);
    make(L"BUTTON", ui::wide(tr(Str::MemoRemove)).c_str(), WS_TABSTOP | BS_OWNERDRAW, RECT{w - pad - bw * 2 - pad, y, w - pad - bw - pad, y + eh}, kIdRemove);
    make(L"BUTTON", ui::wide(tr(Str::MemoClose)).c_str(), WS_TABSTOP | BS_OWNERDRAW, RECT{w - pad - bw, y, w - pad, y + eh}, kIdClose);
    y += eh + pad;
    hintRc_ = RECT{pad, y, w - pad, y + hintH};

    SendMessageW(text_, EM_SETLIMITTEXT, 120, 0);
    g_editOldProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(text_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&edit_proc)));
    SetWindowLongPtrW(when_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&edit_proc));

    refresh();
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    SetFocus(text_);
    return true;
}

void MemoDialog::refresh() {
    if (!list_ || !cb_.list) return;
    SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    ids_.clear();
    for (const Row& r : cb_.list()) {
        SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(r.label.c_str()));
        ids_.push_back(r.id);
    }
}

void MemoDialog::set_hint(const std::wstring& h) {
    hint_ = h;
    if (hwnd_) InvalidateRect(hwnd_, &hintRc_, TRUE);
}

void MemoDialog::on_add() {
    wchar_t text[256]{}, when[64]{};
    GetWindowTextW(text_, text, 256);
    GetWindowTextW(when_, when, 64);
    if (!cb_.add) return;
    const std::wstring err = cb_.add(text, when);
    if (!err.empty()) {
        set_hint(err);
        return;
    }
    SetWindowTextW(text_, L"");
    set_hint(ui::wide(tr(Str::MemoHintAdded)));
    refresh();
    SetFocus(text_);
}

void MemoDialog::on_remove() {
    const LRESULT sel = SendMessageW(list_, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR || static_cast<size_t>(sel) >= ids_.size()) return;
    if (cb_.remove) cb_.remove(ids_[static_cast<size_t>(sel)]);
    refresh();
}

void MemoDialog::close() {
    if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; list_ = text_ = when_ = nullptr; }
    fonts_.destroy();
}

void MemoDialog::paint(HDC dc, const RECT& rc) {
    ui::paint_frame(dc, rc, s_, ui::wide(tr(Str::MemoTitle)).c_str(), fonts_);
    ui::paint_field_border(dc, listRc_, s_);
    ui::paint_field_border(dc, textRc_, s_);
    ui::paint_field_border(dc, whenRc_, s_);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ old = SelectObject(dc, fonts_.normal);
    SetTextColor(dc, ui::kText);
    RECT a = textLabelRc_, b = whenLabelRc_;
    DrawTextW(dc, ui::wide(tr(Str::MemoContent)).c_str(), -1, &a, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextW(dc, ui::wide(tr(Str::MemoTime)).c_str(), -1, &b, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SetTextColor(dc, ui::kMuted);
    RECT hr = hintRc_;
    DrawTextW(dc, hint_.c_str(), -1, &hr, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
}

LRESULT CALLBACK MemoDialog::wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MemoDialog* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<MemoDialog*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    self = reinterpret_cast<MemoDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            self->paint(dc, rc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            return reinterpret_cast<LRESULT>(ui::ctl_color(reinterpret_cast<HDC>(wp)));
        case WM_DRAWITEM: {
            const auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
            if (dis->CtlType == ODT_BUTTON) { ui::draw_button(dis, self->fonts_, dis->CtlID == kIdAdd); return TRUE; }
            break;
        }
        case WM_NCHITTEST: {
            const LRESULT r = DefWindowProcW(hwnd, msg, wp, lp);
            return r == HTCLIENT ? HTCAPTION : r;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case kIdAdd:    self->on_add(); return 0;
                case kIdRemove: self->on_remove(); return 0;
                case kIdClose:  self->close(); return 0;
                default: break;
            }
            break;
        case WM_KEYDOWN:
            if (wp == VK_RETURN) { self->on_add(); return 0; }
            if (wp == VK_ESCAPE) { self->close(); return 0; }
            break;
        case WM_CLOSE:
            self->close();
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace pet::win
