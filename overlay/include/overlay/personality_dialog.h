// 性格窗口：八维滑条 + 重新随机。托盘「性格…」打开。
// 允许拿焦点。非模态，主循环不停。风格同 rename_dialog。

#pragma once

#include <windows.h>

#include <functional>

#include "core/personality.h"
#include "overlay/ui_style.h"

namespace pet::win {

class PersonalityDialog {
public:
    using DoneFn = std::function<void(const pet::Personality& p, bool rerolledSeed, std::uint64_t newSeed)>;

    // current 为当前性格；seed 用于「重新随机」。确定后回调；取消不回调。
    bool open(const pet::Personality& current, std::uint64_t seed, UINT dpi, DoneFn onDone);
    void close();
    bool is_open() const { return hwnd_ != nullptr; }

private:
    static LRESULT CALLBACK wnd_proc(HWND, UINT, WPARAM, LPARAM);
    void submit(bool fromReroll);
    void paint(HDC dc, const RECT& rc);
    void read_sliders(pet::Personality& out) const;
    void write_sliders(const pet::Personality& p);
    void reroll();

    HWND hwnd_ = nullptr;
    HWND track_[8]{};
    HWND btnReroll_ = nullptr;
    HWND btnOk_ = nullptr;
    HWND btnCancel_ = nullptr;
    ui::Fonts fonts_;
    float s_ = 1.0f;
    std::uint64_t seed_ = 0;
    bool lastWasReroll_ = false;
    DoneFn onDone_;
};

}  // namespace pet::win
