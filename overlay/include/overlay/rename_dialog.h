// 改名窗口：一个输入框加确定。托盘菜单「改名…」打开；「天气城市…」也用它。
//
// 这是用户主动发起的对话，允许拿焦点。宠物自己的行为仍然不许。
// 非模态：在主消息泵里跑，主循环不停。
// 1.6 起按 ui_style 的风格自绘：无边框圆角、奶油底、棕边，按住空白处可拖。

#pragma once

#include <windows.h>

#include <functional>
#include <string>

#include "overlay/ui_style.h"

namespace pet::win {

class RenameDialog {
public:
    using DoneFn = std::function<void(const std::wstring& newName)>;

    // 打开（已打开则前置）。current 是当前值，回车或点确定后回调。
    // title / hint / limit 可改，所以「天气城市」这类单行输入也用它。
    // title / hint 为空指针时用改名的默认文案（按当前语言）。
    bool open(const std::wstring& current, UINT dpi, DoneFn onDone,
              const wchar_t* title = nullptr, const wchar_t* hint = nullptr, int limit = 16);
    void close();
    bool is_open() const { return hwnd_ != nullptr; }

private:
    static LRESULT CALLBACK wnd_proc(HWND, UINT, WPARAM, LPARAM);
    void submit();
    void paint(HDC dc, const RECT& rc);

    HWND hwnd_ = nullptr;
    HWND edit_ = nullptr;
    ui::Fonts fonts_;
    float s_ = 1.0f;
    std::wstring title_, hint_;
    RECT editRc_{}, hintRc_{};
    DoneFn onDone_;
};

}  // namespace pet::win
