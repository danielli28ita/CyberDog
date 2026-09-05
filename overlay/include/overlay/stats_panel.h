// 属性面板：名字、日期、亲密度、性格、成长记录。右键狗本体打开 / 关闭（1.6，作者要求）。
//
// 用户主动打开的，允许拿焦点；失去焦点（点了别处）就自己关，Esc 也关。
// 自绘的无边框弹窗，按住任意处可拖。数据由宿主每次打开时整理成行，面板不认识业务对象。
// 设计文档 §2.9 / §2.14：这些数本来就在存档里，只是以前没有地方看。

#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace pet::win {

class StatsPanel {
public:
    struct Row {
        std::wstring label;
        std::wstring value;
        float bar = -1.0f;      // 0–1 时在值后面画一条进度条；负数不画
        bool  heading = false;  // 小节标题行
        bool  accent = false;   // 进度条用红色（亲密度），否则棕色
    };

    // near：狗的屏幕矩形，面板摆在它左上方，夹在工作区里。已打开时刷新内容并前置。
    bool show(const RECT& anchor, const std::wstring& title, const std::vector<Row>& rows, UINT dpi);
    void hide();
    bool is_open() const { return hwnd_ != nullptr; }

private:
    static LRESULT CALLBACK wnd_proc(HWND, UINT, WPARAM, LPARAM);
    void paint(HDC dc, const RECT& rc);
    void layout(UINT dpi);

    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr, fontBold_ = nullptr, fontTitle_ = nullptr;
    std::wstring title_;
    std::vector<Row> rows_;
    float s_ = 1.0f;
    int w_ = 0, h_ = 0, rowH_ = 0, pad_ = 0, labelW_ = 0;
};

}  // namespace pet::win
