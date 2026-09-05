#include "overlay/tray_icon.h"

#include <shellapi.h>

#include <cstdio>
#include <cwchar>
#include <iterator>

#include "core/i18n.h"

namespace pet::win {
namespace {

// 托盘的通知消息。WM_APP 之上的编号归应用自己用。
constexpr UINT kTrayMsg = WM_APP + 1;
constexpr UINT kIconId  = 1;

constexpr UINT kMenuToggle = 40001;
constexpr UINT kMenuExit   = 40002;
constexpr UINT kMenuRename = 40003;
constexpr UINT kMenuSound  = 40004;
constexpr UINT kMenuNameReminder = 40005;
constexpr UINT kMenuMemo = 40006;
constexpr UINT kMenuWeather = 40007;
constexpr UINT kMenuAutostart = 40008;
constexpr UINT kMenuStats = 40009;
constexpr UINT kMenuLangZh = 40010;
constexpr UINT kMenuLangEn = 40011;
constexpr UINT kMenuLangIt = 40012;
constexpr UINT kMenuOpenData = 40013;

}  // namespace

TrayIcon::~TrayIcon() {
    destroy();
    if (icon_) { DestroyIcon(icon_); icon_ = nullptr; }
}

// 用 GDI 画一个 32×32 的比格头当托盘图标：棕色头、白吻、黑鼻、两只眼。
// 没有美术资源也有个像样的图标，1.0 单文件分发不用带 .ico。
HICON TrayIcon::make_icon() {
    const int n = 32;
    BITMAPV5HEADER bi{};
    bi.bV5Size        = sizeof(bi);
    bi.bV5Width       = n;
    bi.bV5Height      = -n;
    bi.bV5Planes      = 1;
    bi.bV5BitCount    = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask     = 0x00FF0000;
    bi.bV5GreenMask   = 0x0000FF00;
    bi.bV5BlueMask    = 0x000000FF;
    bi.bV5AlphaMask   = 0xFF000000;

    HDC dc = GetDC(nullptr);
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color) return nullptr;

    auto* px = static_cast<unsigned char*>(bits);
    auto put = [&](int x, int y, int r, int g, int b) {
        if (x < 0 || y < 0 || x >= n || y >= n) return;
        unsigned char* p = px + (y * n + x) * 4;
        p[0] = static_cast<unsigned char>(b); p[1] = static_cast<unsigned char>(g);
        p[2] = static_cast<unsigned char>(r); p[3] = 255;
    };
    auto disc = [&](float cx, float cy, float rad, int r, int g, int b) {
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
                if (dx * dx + dy * dy <= rad * rad) put(x, y, r, g, b);
            }
    };
    // 耳朵（深棕，垂在两侧）→ 头（棕）→ 吻（白）→ 鼻（黑）→ 眼（白底黑瞳加高光）
    disc(6.5f, 19.0f, 5.5f, 120, 70, 30);
    disc(25.5f, 19.0f, 5.5f, 120, 70, 30);
    disc(16.0f, 14.0f, 10.5f, 158, 92, 41);
    disc(16.0f, 21.0f, 6.5f, 236, 230, 220);
    disc(16.0f, 19.5f, 3.0f, 25, 20, 20);
    disc(11.5f, 12.0f, 2.6f, 250, 250, 250);
    disc(20.5f, 12.0f, 2.6f, 250, 250, 250);
    disc(12.0f, 12.3f, 1.5f, 40, 25, 15);
    disc(21.0f, 12.3f, 1.5f, 40, 25, 15);
    put(12, 11, 255, 255, 255);
    put(21, 11, 255, 255, 255);

    HBITMAP mask = CreateBitmap(n, n, 1, 1, nullptr);
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

bool TrayIcon::create(HWND owner, const wchar_t* tip) {
    owner_ = owner;
    wcsncpy_s(tip_, std::size(tip_), tip, _TRUNCATE);
    // 首选 exe 自带的图标资源（和资源管理器里显示的是同一个），按通知区域的尺寸取；
    // 取不到（比如别的宿主链接了这个库）再退回 GDI 现画的那个。
    if (!icon_) {
        const int cx = GetSystemMetrics(SM_CXSMICON), cy = GetSystemMetrics(SM_CYSMICON);
        icon_ = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101),
                                              IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR));
    }
    if (!icon_) icon_ = make_icon();

    // Explorer 重启后托盘会被清空。这个广播消息是重新注册的唯一可靠信号。
    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");
    if (taskbarCreatedMsg_ == 0) {
        std::printf("  [WARN] RegisterWindowMessage(TaskbarCreated) 失败 err=%lu，"
                    "Explorer 重启后图标不会自动恢复\n", GetLastError());
    }
    return add_icon();
}

bool TrayIcon::add_icon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = owner_;
    nid.uID              = kIconId;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = kTrayMsg;
    nid.hIcon            = icon_ ? icon_ : LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(nid.szTip, std::size(nid.szTip), tip_, _TRUNCATE);

    if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
        std::printf("  [FAIL] Shell_NotifyIcon(NIM_ADD) err=%lu\n", GetLastError());
        added_ = false;
        return false;
    }
    added_ = true;
    return true;
}

void TrayIcon::set_tip(const wchar_t* tip) {
    wcsncpy_s(tip_, std::size(tip_), tip, _TRUNCATE);
    if (!added_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = owner_;
    nid.uID    = kIconId;
    nid.uFlags = NIF_TIP;
    wcsncpy_s(nid.szTip, std::size(nid.szTip), tip_, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayIcon::destroy() {
    if (!added_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = owner_;
    nid.uID    = kIconId;
    // 这一步漏了图标就会残留，V1 直接不通过。
    Shell_NotifyIconW(NIM_DELETE, &nid);
    added_ = false;
}

bool TrayIcon::handle_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& out) {
    if (taskbarCreatedMsg_ != 0 && msg == taskbarCreatedMsg_) {
        std::printf("  [info] 收到 TaskbarCreated，重新注册托盘图标\n");
        added_ = false;
        add_icon();
        out = 0;
        return true;
    }

    if (msg == WM_MEASUREITEM) {
        auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
        if (mis->CtlType != ODT_MENU) return false;
        ui::measure_menu_item(mis, dpiScale_, fonts_);
        out = TRUE;
        return true;
    }
    if (msg == WM_DRAWITEM) {
        const auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
        if (dis->CtlType != ODT_MENU) return false;
        ui::draw_menu_item(dis, dpiScale_, fonts_);
        out = TRUE;
        return true;
    }

    if (msg == WM_COMMAND) {
        const UINT id = LOWORD(wp);
        TrayCommand c;
        switch (id) {
            case kMenuToggle: c = TrayCommand::ToggleVisible; break;
            case kMenuExit:   c = TrayCommand::Exit; break;
            case kMenuRename: c = TrayCommand::Rename; break;
            case kMenuSound:  c = TrayCommand::ToggleSound; break;
            case kMenuNameReminder: c = TrayCommand::ToggleNameReminder; break;
            case kMenuMemo:   c = TrayCommand::Memo; break;
            case kMenuWeather: c = TrayCommand::WeatherCity; break;
            case kMenuAutostart: c = TrayCommand::ToggleAutostart; break;
            case kMenuStats: c = TrayCommand::Stats; break;
            case kMenuLangZh: c = TrayCommand::LangZh; break;
            case kMenuLangEn: c = TrayCommand::LangEn; break;
            case kMenuLangIt: c = TrayCommand::LangIt; break;
            case kMenuOpenData: c = TrayCommand::OpenData; break;
            default: return false;
        }
        if (onCommand_) onCommand_(c);
        out = 0;
        return true;
    }

    if (msg != kTrayMsg) return false;

    const UINT ev = LOWORD(lp);
    if (ev == WM_RBUTTONUP || ev == WM_CONTEXTMENU) {
        POINT pt{};
        GetCursorPos(&pt);

        // 1.6 起菜单项自绘（ui_style），和属性面板一个风格。项的数据放在 menuItems_ 里，
        // WM_MEASUREITEM / WM_DRAWITEM 到 owner 窗口，handle_message 上面截下来画。
        menuItems_.clear();
        menuItems_.reserve(16);   // 不许再增长：项里存的是指针，vector 重新分配会让菜单指向旧内存
        if (menu_) DestroyMenu(menu_);
        menu_ = CreatePopupMenu();
        HMENU menu = menu_;
        HMENU langMenu = CreatePopupMenu();   // 子菜单随父菜单一起销毁
        auto add = [&](HMENU m, UINT id, const std::wstring& text, bool checked = false) {
            menuItems_.push_back(ui::MenuItem{text, checked, false});
            AppendMenuW(m, MF_OWNERDRAW | (checked ? MF_CHECKED : 0), id, reinterpret_cast<LPCWSTR>(&menuItems_.back()));
        };
        auto W = [](Str s) { return ui::wide(tr(s)); };
        add(menu, kMenuToggle, W(petVisible_ ? Str::MenuHide : Str::MenuShow));
        add(menu, kMenuStats, W(Str::MenuStats));
        add(menu, kMenuMemo, W(Str::MenuMemo));
        add(menu, kMenuRename, W(Str::MenuRename));
        add(menu, kMenuSound, W(Str::MenuSound), soundOn_);
        add(menu, kMenuNameReminder, W(Str::MenuNameReminder), nameReminder_);
        {
            std::wstring w = W(Str::MenuWeatherUnset);
            if (!weatherCity_.empty()) {
                wchar_t buf[160];
                std::swprintf(buf, 160, W(Str::MenuWeatherFmt).c_str(), weatherCity_.c_str());
                w = buf;
            }
            add(menu, kMenuWeather, w);
        }
        add(menu, kMenuAutostart, W(Str::MenuAutostart), autostart_);
        // 语言子菜单（1.7）：三种，勾当前的。
        const Lang cur = language();
        add(langMenu, kMenuLangZh, ui::wide(lang_native_name(Lang::Zh)), cur == Lang::Zh);
        add(langMenu, kMenuLangEn, ui::wide(lang_native_name(Lang::En)), cur == Lang::En);
        add(langMenu, kMenuLangIt, ui::wide(lang_native_name(Lang::It)), cur == Lang::It);
        menuItems_.push_back(ui::MenuItem{W(Str::MenuLanguage), false, false});
        AppendMenuW(menu, MF_OWNERDRAW | MF_POPUP, reinterpret_cast<UINT_PTR>(langMenu), reinterpret_cast<LPCWSTR>(&menuItems_.back()));
        add(menu, kMenuOpenData, W(Str::MenuOpenData));
        menuItems_.push_back(ui::MenuItem{L"", false, true});
        AppendMenuW(menu, MF_OWNERDRAW | MF_SEPARATOR, 0, reinterpret_cast<LPCWSTR>(&menuItems_.back()));
        add(menu, kMenuExit, W(Str::MenuExit));
        MENUINFO mi{};
        mi.cbSize = sizeof(mi);
        mi.fMask = MIM_BACKGROUND | MIM_STYLE | MIM_APPLYTOSUBMENUS;
        mi.hbrBack = ui::menu_background();
        mi.dwStyle = MNS_NOCHECK;   // 勾自己画
        SetMenuInfo(menu, &mi);
        if (!fonts_.normal) fonts_.create(dpiScale_);

        // 这两步是托盘菜单的固定写法：先把自己设为前台，菜单关闭后再发一条空消息，
        // 否则在菜单外点击时菜单不会消失。
        //
        // 这是「不抢焦点」这条规矩唯一允许的例外：用户自己右键点了托盘图标，
        // 是他主动要交互。宠物自己的任何行为都不许调 SetForegroundWindow。
        SetForegroundWindow(hwnd);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RIGHTALIGN,
                       pt.x, pt.y, 0, hwnd, nullptr);
        PostMessageW(hwnd, WM_NULL, 0, 0);
        DestroyMenu(menu);
        menu_ = nullptr;

        out = 0;
        return true;
    }

    if (ev == WM_LBUTTONDBLCLK) {
        if (onCommand_) onCommand_(TrayCommand::ToggleVisible);
        out = 0;
        return true;
    }

    return false;
}

}  // namespace pet::win
