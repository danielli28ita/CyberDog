#include "overlay/rename_dialog.h"

#include <cstdio>

#include "core/i18n.h"

namespace pet::win {
namespace {

constexpr wchar_t kClassName[] = L"PetRenameDialog";
constexpr int kIdEdit = 101;
constexpr int kIdOk   = 102;
constexpr int kIdCancel = 103;

// 输入框有焦点时回车和 Esc 到不了父窗口，得给输入框换一个窗口过程截下来。
// 只有一个改名窗口，旧过程存全局够用。
WNDPROC g_editOldProc = nullptr;

LRESULT CALLBACK edit_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE)) {
        SendMessageW(GetParent(hwnd), WM_KEYDOWN, wp, lp);
        return 0;
    }
    if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0;   // 不让它响一声
    return CallWindowProcW(g_editOldProc, hwnd, msg, wp, lp);
}

}  // namespace

bool RenameDialog::open(const std::wstring& current, UINT dpi, DoneFn onDone,
                        const wchar_t* title, const wchar_t* hintText, int limit) {
    onDone_ = std::move(onDone);
    if (hwnd_) close();   // 换了用途就重建，标题和提示才对
    HINSTANCE hinst = GetModuleHandleW(nullptr);
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = &RenameDialog::wnd_proc;
        wc.hInstance     = hinst;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        if (!RegisterClassExW(&wc)) return false;
        registered = true;
    }
    s_ = static_cast<float>(dpi) / 96.0f;
    const float s = s_;
    title_ = title ? std::wstring(title) : ui::wide(tr(Str::RenameTitle));
    hint_ = hintText ? std::wstring(hintText) : ui::wide(tr(Str::RenameHint));
    hintText = hint_.c_str();
    title = title_.c_str();
    fonts_.create(s);

    const int w = static_cast<int>(400 * s);
    const int pad = static_cast<int>(16 * s);
    const int eh = static_cast<int>(30 * s);
    const int bw = static_cast<int>(76 * s);
    const int titleH = pad + static_cast<int>(34 * s);

    // 提示可能好几行（天气城市那段），高度按实际文字算。1.4 固定一行，作者看不全。
    int hintH = eh;
    {
        HDC dc = GetDC(nullptr);
        HGDIOBJ old = SelectObject(dc, fonts_.normal);
        RECT tr{0, 0, w - pad * 2, 0};
        DrawTextW(dc, hintText, -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        hintH = tr.bottom + static_cast<int>(4 * s);
        SelectObject(dc, old);
        ReleaseDC(nullptr, dc);
    }
    const int h = titleH + eh + pad + hintH + pad + eh + pad;
    const int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);

    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClassName, title, WS_POPUP,
                            (sx - w) / 2, (sy - h) / 2, w, h, nullptr, nullptr, hinst, this);
    if (!hwnd_) return false;
    ui::apply_round_region(hwnd_, w, h, s);

    int y = titleH;
    editRc_ = RECT{pad + static_cast<int>(4 * s), y + static_cast<int>(4 * s), w - pad - static_cast<int>(4 * s), y + eh - static_cast<int>(2 * s)};
    edit_ = CreateWindowExW(0, L"EDIT", current.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                            editRc_.left, editRc_.top, editRc_.right - editRc_.left, editRc_.bottom - editRc_.top, hwnd_,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdEdit)), hinst, nullptr);
    y += eh + pad;
    hintRc_ = RECT{pad, y, w - pad, y + hintH};
    y += hintH + pad;
    HWND ok = CreateWindowExW(0, L"BUTTON", ui::wide(tr(Str::Ok)).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                              w - pad - bw, y, bw, eh, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdOk)), hinst, nullptr);
    HWND cancel = CreateWindowExW(0, L"BUTTON", ui::wide(tr(Str::Cancel)).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                  w - pad * 2 - bw * 2, y, bw, eh, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdCancel)), hinst, nullptr);
    SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(fonts_.normal), TRUE);
    SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(fonts_.normal), TRUE);
    SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(fonts_.normal), TRUE);
    SendMessageW(edit_, EM_SETLIMITTEXT, static_cast<WPARAM>(limit), 0);
    SendMessageW(edit_, EM_SETSEL, 0, -1);
    g_editOldProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(edit_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&edit_proc)));

    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    SetFocus(edit_);
    return true;
}

void RenameDialog::close() {
    if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; edit_ = nullptr; }
    fonts_.destroy();
}

void RenameDialog::submit() {
    wchar_t buf[64]{};
    GetWindowTextW(edit_, buf, 64);
    std::wstring name = buf;
    // 去掉首尾空格；空名字当作没改。
    while (!name.empty() && name.back() == L' ') name.pop_back();
    size_t i = 0;
    while (i < name.size() && name[i] == L' ') ++i;
    name = name.substr(i);
    DoneFn cb = onDone_;
    close();
    if (!name.empty() && cb) cb(name);
}

void RenameDialog::paint(HDC dc, const RECT& rc) {
    ui::paint_frame(dc, rc, s_, title_.c_str(), fonts_);
    ui::paint_field_border(dc, editRc_, s_);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ui::kMuted);
    HGDIOBJ old = SelectObject(dc, fonts_.normal);
    RECT hr = hintRc_;
    DrawTextW(dc, hint_.c_str(), -1, &hr, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
}

LRESULT CALLBACK RenameDialog::wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    RenameDialog* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<RenameDialog*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    self = reinterpret_cast<RenameDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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
            return reinterpret_cast<LRESULT>(ui::ctl_color(reinterpret_cast<HDC>(wp)));
        case WM_DRAWITEM: {
            const auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
            ui::draw_button(dis, self->fonts_, dis->CtlID == kIdOk);
            return TRUE;
        }
        case WM_NCHITTEST: {
            // 按住空白处也能拖动。作者反馈窗口挡住字又拖不动。
            const LRESULT r = DefWindowProcW(hwnd, msg, wp, lp);
            return r == HTCLIENT ? HTCAPTION : r;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == kIdOk) { self->submit(); return 0; }
            if (LOWORD(wp) == kIdCancel) { self->close(); return 0; }
            break;
        case WM_KEYDOWN:
            if (wp == VK_RETURN) { self->submit(); return 0; }
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
