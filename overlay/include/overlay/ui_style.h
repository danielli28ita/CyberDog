// 统一的界面风格：奶油底、棕边、深棕字、红色强调。1.6 起属性面板、改名、备忘录、托盘菜单都用它。
// 作者看了属性面板说喜欢，要求别的窗口和菜单都照这个改。
//
// 用法：
//   窗口：无边框 WS_POPUP，show 之后 apply_round_region；WM_PAINT 里先 paint_frame 再画自己的内容；
//         WM_NCHITTEST 把 HTCLIENT 换成 HTCAPTION 就能按住空白处拖。
//   子控件：EDIT / STATIC / LISTBOX 在 WM_CTLCOLOR* 里调 ctl_color；BUTTON 用 BS_OWNERDRAW，WM_DRAWITEM 里调 draw_button。
//   菜单：MF_OWNERDRAW，把 MenuItem 指针放 dwItemData，WM_MEASUREITEM / WM_DRAWITEM 交给 measure_menu_item / draw_menu_item。

#pragma once

#include <windows.h>

#include <string>

namespace pet::win::ui {

// UTF-8 → UTF-16。core/i18n 的文案是 UTF-8，Win32 要宽字符。
std::wstring wide(const char* utf8);

constexpr COLORREF kBg     = RGB(253, 250, 243);
constexpr COLORREF kBorder = RGB(158, 92, 41);
constexpr COLORREF kText   = RGB(60, 38, 18);
constexpr COLORREF kMuted  = RGB(140, 118, 96);
constexpr COLORREF kTrack  = RGB(232, 222, 206);
constexpr COLORREF kAccent = RGB(214, 36, 58);
constexpr COLORREF kSoft   = RGB(196, 132, 74);
constexpr COLORREF kHover  = RGB(244, 236, 222);

struct Fonts {
    HFONT normal = nullptr, bold = nullptr, title = nullptr;
    void create(float s);
    void destroy();
};

// 圆角：窗口区域裁出来。w / h 是窗口尺寸。
void apply_round_region(HWND hwnd, int w, int h, float s);
// 底色 + 棕边 + 标题（可空）。返回标题占掉的高度，内容从这之下开始画。
int paint_frame(HDC dc, const RECT& rc, float s, const wchar_t* title, const Fonts& fonts);
// 在 EDIT / LISTBOX 周围画一圈棕色细框（控件本身不带边）。rc 是控件在父窗口里的矩形。
void paint_field_border(HDC dc, const RECT& rc, float s);
// WM_CTLCOLOREDIT / STATIC / LISTBOX：设字色、底色，返回底色画刷（静态对象，不要删）。
HBRUSH ctl_color(HDC dc, bool muted = false);
// BS_OWNERDRAW 按钮。primary 是棕底奶油字，否则奶油底棕字棕边。
void draw_button(const DRAWITEMSTRUCT* dis, const Fonts& fonts, bool primary);

// 托盘菜单项。
struct MenuItem {
    std::wstring text;
    bool checked = false;
    bool separator = false;
};
void measure_menu_item(MEASUREITEMSTRUCT* mis, float s, const Fonts& fonts);
void draw_menu_item(const DRAWITEMSTRUCT* dis, float s, const Fonts& fonts);
// 菜单本体的底色（SetMenuInfo），返回静态画刷。
HBRUSH menu_background();

}  // namespace pet::win::ui
