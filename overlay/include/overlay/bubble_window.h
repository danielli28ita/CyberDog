// 气泡：宠物头顶的一句话。健康提醒用它，将来台词也走这里。
//
// 独立的分层窗口，用 GDI 画一次、UpdateLayeredWindow 提交一次，之后不再重绘。
// 技能包 hot path 禁令第 4 条禁的是宠物本体每帧 UpdateLayeredWindow；
// 气泡是一次性的静态位图，显示 15 秒就撤，不在那条禁令范围内。
//
// 不抢焦点（WS_EX_NOACTIVATE）、点击穿透（WS_EX_TRANSPARENT）、不进 Alt+Tab（WS_EX_TOOLWINDOW）。
// 设计文档 §2.7：提醒不用系统通知，不抢焦点。

#pragma once

#include <windows.h>

namespace pet::win {

class BubbleWindow {
public:
    ~BubbleWindow();

    // Speech：带尾巴的对话框，尾巴尖对着 anchor（狗头顶）。
    // Tag：没有尾巴的小标签，红色粗体，底边中点对着 anchor。好感度上涨的「好感度 +1」用它。
    enum class Style { Speech, Tag };

    // 在屏幕点 anchor 上方显示一句话。text 是宽字符。dpi 决定字号和边距。已经显示时会替换内容。
    bool show(POINT anchor, const wchar_t* text, UINT dpi, Style style = Style::Speech);
    // 狗走动时让气泡跟着。没显示时不做事。
    void move_anchor(POINT anchor);
    void hide();
    void destroy();
    bool visible() const { return hwnd_ != nullptr && shown_; }

private:
    bool ensure_window();
    POINT place(POINT anchor) const;
    HWND hwnd_ = nullptr;
    bool shown_ = false;
    int  w_ = 0, h_ = 0, tailX_ = 0;
};

}  // namespace pet::win
