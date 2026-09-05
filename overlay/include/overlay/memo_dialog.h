// 备忘录窗口：列表 + 内容 + 时间 + 添加 / 删除。托盘菜单「备忘录…」打开。
//
// 用户主动发起，允许拿焦点。非模态，在主消息泵里跑。
// 界面归宿主，数据归插件（MemoPlugin）。P5 的 panel 服务到位后，这个窗口改由插件注册。
// 1.6 起按 ui_style 的风格自绘：无边框圆角、奶油底、棕边，按住空白处可拖。

#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "overlay/ui_style.h"

namespace pet::win {

class MemoDialog {
public:
    struct Row { unsigned id; std::wstring label; };
    // 宿主提供：列出当前条目；添加（返回错误文字，空表示成功）；删除。
    struct Callbacks {
        std::function<std::vector<Row>()> list;
        std::function<std::wstring(const std::wstring& text, const std::wstring& when)> add;
        std::function<void(unsigned id)> remove;
    };

    bool open(UINT dpi, Callbacks cb);
    void close();
    bool is_open() const { return hwnd_ != nullptr; }
    void refresh();

private:
    static LRESULT CALLBACK wnd_proc(HWND, UINT, WPARAM, LPARAM);
    void on_add();
    void on_remove();
    void paint(HDC dc, const RECT& rc);
    void set_hint(const std::wstring& h);

    HWND hwnd_ = nullptr;
    HWND list_ = nullptr, text_ = nullptr, when_ = nullptr;
    ui::Fonts fonts_;
    float s_ = 1.0f;
    RECT listRc_{}, textRc_{}, whenRc_{}, hintRc_{}, textLabelRc_{}, whenLabelRc_{};
    std::wstring hint_;
    Callbacks cb_;
    std::vector<unsigned> ids_;   // 列表行 → 备忘 id
};

}  // namespace pet::win
