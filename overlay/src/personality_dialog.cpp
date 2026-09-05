#include "overlay/personality_dialog.h"

#include <commctrl.h>
#include <cstdio>
#include <windowsx.h>

#include "core/i18n.h"

#pragma comment(lib, "comctl32.lib")

namespace pet::win {
namespace {

constexpr wchar_t kClassName[] = L"PetPersonalityDialog";
constexpr int kIdTrack0 = 200;
constexpr int kIdReroll = 300;
constexpr int kIdOk = 301;
constexpr int kIdCancel = 302;

const pet::Str kTraitLabels[8] = {
    pet::Str::TraitExtroversion, pet::Str::TraitClinginess, pet::Str::TraitCuriosity, pet::Str::TraitLaziness,
    pet::Str::TraitTimidity, pet::Str::TraitLiveliness, pet::Str::TraitMischief, pet::Str::TraitCharm};

float* trait_ptr(pet::Personality& p, int i) {
    float* arr[8] = {&p.extroversion, &p.clinginess, &p.curiosity, &p.laziness,
                     &p.timidity, &p.liveliness, &p.mischief, &p.charm};
    return arr[i];
}
const float* trait_ptr_c(const pet::Personality& p, int i) {
    const float* arr[8] = {&p.extroversion, &p.clinginess, &p.curiosity, &p.laziness,
                           &p.timidity, &p.liveliness, &p.mischief, &p.charm};
    return arr[i];
}

}  // namespace

bool PersonalityDialog::open(const pet::Personality& current, std::uint64_t seed, UINT dpi, DoneFn onDone) {
    onDone_ = std::move(onDone);
    seed_ = seed;
    lastWasReroll_ = false;
    if (hwnd_) close();

    static bool commonInited = false;
    if (!commonInited) {
        INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES};
        InitCommonControlsEx(&icc);
        commonInited = true;
    }

    HINSTANCE hinst = GetModuleHandleW(nullptr);
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = &PersonalityDialog::wnd_proc;
        wc.hInstance     = hinst;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        if (!RegisterClassExW(&wc)) return false;
        registered = true;
    }

    s_ = static_cast<float>(dpi) / 96.0f;
    const float s = s_;
    fonts_.create(s);

    const int w = static_cast<int>(360 * s);
    const int pad = static_cast<int>(16 * s);
    const int titleH = pad + static_cast<int>(34 * s);
    const int rowH = static_cast<int>(38 * s);
    const int btnH = static_cast<int>(30 * s);
    const int hintH = static_cast<int>(40 * s);
    const int h = titleH + rowH * 8 + pad + hintH + pad + btnH + pad;
    const int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);

    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClassName,
                            ui::wide(pet::tr(pet::Str::PersonalityTitle)).c_str(), WS_POPUP,
                            (sx - w) / 2, (sy - h) / 2, w, h, nullptr, nullptr, hinst, this);
    if (!hwnd_) return false;
    ui::apply_round_region(hwnd_, w, h, s);

    int y = titleH;
    for (int i = 0; i < 8; ++i) {
        const int trackTop = y + static_cast<int>(16 * s);
        track_[i] = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_NOTICKS,
                                    pad, trackTop, w - pad * 2, static_cast<int>(20 * s), hwnd_,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdTrack0 + i)), hinst, nullptr);
        SendMessageW(track_[i], TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessageW(track_[i], TBM_SETPAGESIZE, 0, 5);
        y += rowH;
    }
    write_sliders(current);
    y += pad + hintH + pad;

    const int bw = static_cast<int>(88 * s);
    btnReroll_ = CreateWindowExW(0, L"BUTTON", ui::wide(pet::tr(pet::Str::PersonalityReroll)).c_str(),
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, pad, y, bw + static_cast<int>(24 * s),
                                 btnH, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdReroll)), hinst, nullptr);
    btnOk_ = CreateWindowExW(0, L"BUTTON", ui::wide(pet::tr(pet::Str::Ok)).c_str(),
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, w - pad - bw, y, bw, btnH, hwnd_,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdOk)), hinst, nullptr);
    btnCancel_ = CreateWindowExW(0, L"BUTTON", ui::wide(pet::tr(pet::Str::Cancel)).c_str(),
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                 w - pad * 2 - bw * 2, y, bw, btnH, hwnd_,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdCancel)), hinst, nullptr);
    SendMessageW(btnReroll_, WM_SETFONT, reinterpret_cast<WPARAM>(fonts_.normal), TRUE);
    SendMessageW(btnOk_, WM_SETFONT, reinterpret_cast<WPARAM>(fonts_.normal), TRUE);
    SendMessageW(btnCancel_, WM_SETFONT, reinterpret_cast<WPARAM>(fonts_.normal), TRUE);

    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    return true;
}

void PersonalityDialog::close() {
    if (!hwnd_) return;
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    for (auto& t : track_) t = nullptr;
    btnReroll_ = btnOk_ = btnCancel_ = nullptr;
    fonts_.destroy();
}

void PersonalityDialog::read_sliders(pet::Personality& out) const {
    for (int i = 0; i < 8; ++i) {
        const int v = static_cast<int>(SendMessageW(track_[i], TBM_GETPOS, 0, 0));
        *trait_ptr(out, i) = static_cast<float>(v) / 100.0f;
    }
    pet::personality_enforce_invariants(out);
}

void PersonalityDialog::write_sliders(const pet::Personality& p) {
    pet::Personality tmp = p;
    pet::personality_enforce_invariants(tmp);
    for (int i = 0; i < 8; ++i) {
        const int v = static_cast<int>(*trait_ptr_c(tmp, i) * 100.0f + 0.5f);
        SendMessageW(track_[i], TBM_SETPOS, TRUE, v);
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

void PersonalityDialog::reroll() {
    seed_ ^= (seed_ << 13) + 0x9E3779B97F4A7C15ull + GetTickCount64();
    if (seed_ == 0) seed_ = 1;
    write_sliders(pet::personality_from_seed(seed_));
    lastWasReroll_ = true;
}

void PersonalityDialog::submit(bool /*fromReroll*/) {
    if (!onDone_) {
        close();
        return;
    }
    pet::Personality p{};
    read_sliders(p);
    const bool rerolled = lastWasReroll_;
    auto cb = std::move(onDone_);
    onDone_ = nullptr;
    const std::uint64_t s = seed_;
    close();
    cb(p, rerolled, s);
}

void PersonalityDialog::paint(HDC dc, const RECT& rc) {
    const std::wstring title = ui::wide(pet::tr(pet::Str::PersonalityTitle));
    ui::paint_frame(dc, rc, s_, title.c_str(), fonts_);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ui::kText);
    SelectObject(dc, fonts_.normal);
    const int pad = static_cast<int>(16 * s_);
    const int titleH = pad + static_cast<int>(34 * s_);
    const int rowH = static_cast<int>(38 * s_);
    int y = titleH;
    for (int i = 0; i < 8; ++i) {
        const int pos = track_[i] ? static_cast<int>(SendMessageW(track_[i], TBM_GETPOS, 0, 0)) : 0;
        wchar_t line[80];
        std::swprintf(line, 80, L"%s  %d%%", ui::wide(pet::tr(kTraitLabels[i])).c_str(), pos);
        RECT lr{pad, y, rc.right - pad, y + static_cast<LONG>(14 * s_)};
        DrawTextW(dc, line, -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        y += rowH;
    }
    SetTextColor(dc, ui::kMuted);
    RECT hint{pad, y, rc.right - pad, y + static_cast<LONG>(40 * s_)};
    DrawTextW(dc, ui::wide(pet::tr(pet::Str::PersonalityHint)).c_str(), -1, &hint,
              DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
}

LRESULT CALLBACK PersonalityDialog::wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PersonalityDialog* self = reinterpret_cast<PersonalityDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<PersonalityDialog*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            self->paint(dc, rc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            const bool primary = dis->CtlID == kIdOk || dis->CtlID == kIdReroll;
            ui::draw_button(dis, self->fonts_, primary);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == kIdOk) {
                self->submit(false);
                return 0;
            }
            if (LOWORD(wp) == kIdCancel) {
                self->close();
                return 0;
            }
            if (LOWORD(wp) == kIdReroll) {
                self->reroll();
                return 0;
            }
            break;
        case WM_HSCROLL:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                self->close();
                return 0;
            }
            if (wp == VK_RETURN) {
                self->submit(false);
                return 0;
            }
            break;
        case WM_NCHITTEST: {
            LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
            if (hit == HTCLIENT) {
                POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                ScreenToClient(hwnd, &pt);
                // 标题带可拖
                if (pt.y < static_cast<LONG>(40 * self->s_)) return HTCAPTION;
            }
            return hit;
        }
        case WM_CLOSE:
            self->close();
            return 0;
        case WM_NCDESTROY:
            self->hwnd_ = nullptr;
            break;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace pet::win
