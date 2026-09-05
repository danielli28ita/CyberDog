// 透明覆盖层窗口。
//
// 窗口样式对应 设计文档 §3.3：WS_EX_NOREDIRECTIONBITMAP 让内容完全由
// DirectComposition 提供，不留重定向位图；WS_EX_TOPMOST 置顶；
// WS_EX_TOOLWINDOW 不出现在 Alt+Tab 和任务栏。

#pragma once

#include <windows.h>

#include <functional>

namespace pet::win {

// 把控制台输出代码页设成 UTF-8。
//
// 不调这个的话中文全是乱码：源码是 UTF-8、MSVC 用 /utf-8 编译，printf 写出去的是
// UTF-8 字节；而控制台默认按系统 ANSI 代码页解释（中文系统是 936 GBK），对不上。
// 在 main 的第一行调用，比让用户每次先敲 chcp 65001 可靠。
void enable_utf8_console();

class OverlayWindow {
public:
    // 命中测试回调。返回 true 表示这个点属于宠物本体，可以拖动或点击；
    // 返回 false 表示落在透明区，点击穿透到下层窗口。
    // P1 任务 5 会把它接到 alpha 掩码回读上；现在先用矩形近似。
    using HitTestFn = std::function<bool(int x, int y)>;

    // 消息钩子。在默认处理之前调用，返回 true 表示这条消息已经处理完，
    // 处理结果放进 outResult。托盘用它接自己的通知消息。
    using MessageHook = std::function<bool(HWND, UINT, WPARAM, LPARAM, LRESULT& outResult)>;

    // 鼠标事件，客户区坐标。按下后自动捕获，松开释放，所以拖出窗口也收得到 Up。
    struct MouseEvent {
        enum class Type { Down, Up, Move, RightDown } type;
        int x, y;
    };

    // 会话与显示变化（设计文档 §2.2 边界 3、5）：锁屏 / 解锁、分辨率或 DPI 变化。
    // 锁屏期间什么都不该做；显示变了窗口尺寸和投影都要重算。
    struct SessionEvent { enum class Type { Locked, Unlocked, DisplayChanged } type; };
    using SessionFn = std::function<void(const SessionEvent&)>;
    void set_session_handler(SessionFn fn) { session_ = std::move(fn); }

    // 窗口的有效区域。区域外的像素在系统眼里不属于本窗口：点击直接落到下面的程序。
    // 这是跨进程穿透唯一可靠的办法——WM_NCHITTEST 的 HTTRANSPARENT 只在同一线程的窗口之间传递。
    // 宿主用 alpha 掩码的格子生成区域。系统接管 rgn 的所有权。
    void set_region(HRGN rgn);
    using MouseFn = std::function<void(const MouseEvent&)>;
    void set_mouse_handler(MouseFn fn) { mouse_ = std::move(fn); }

    // 光标在宠物本体上时显示手掌。作者要求：鼠标移到比格上面就变成手掌。
    void set_hand_cursor(bool v) { handCursor_ = v; }

    bool create(const wchar_t* title, int width, int height);
    void destroy();

    void show_no_activate();
    void hide();
    void move_to(POINT pt);
    // 挪到另一块显示器并改客户区尺寸（多屏拖狗用）。不激活。
    bool set_bounds(RECT r);

    // 处理待办消息。返回 false 表示收到了退出请求。
    bool pump();

    HWND hwnd() const { return hwnd_; }
    int  width() const { return width_; }
    int  height() const { return height_; }

    void set_hit_test(HitTestFn fn) { hitTest_ = std::move(fn); }
    void set_draggable(bool v) { draggable_ = v; }
    void set_message_hook(MessageHook fn) { hook_ = std::move(fn); }

    // 让外部（托盘菜单的「退出」）请求结束主循环。
    void request_quit() { quit_ = true; }
    bool quit_requested() const { return quit_; }

    // 隐藏时用它把线程阻塞在消息上，CPU 归零。
    static void wait_for_message() { WaitMessage(); }

private:
    static LRESULT CALLBACK wnd_proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(HWND, UINT, WPARAM, LPARAM);

    HWND hwnd_ = nullptr;
    int  width_ = 0, height_ = 0;
    bool quit_ = false;
    bool draggable_ = false;
    bool handCursor_ = false;
    HitTestFn hitTest_;
    MessageHook hook_;
    MouseFn mouse_;
    SessionFn session_;
    bool sessionRegistered_ = false;
};

}  // namespace pet::win
