// 鼠标手势识别。纯 C++：输入是每帧的按键状态和光标位置（客户区像素），
// 输出是「摸」「打」「戳」「拖」四种事件。平台层负责喂数据，不做判断。
//
// 作者定的规则：
//   光标在狗身上变手掌（平台层做）；
//   按住左键在头上慢慢移动是抚摸；
//   按住左键在头上快速来回是打；
//   都能让它乖一点，但摸加亲密度、打减亲密度。
// 补的规则：
//   按下即松开、没怎么动，是戳一下；
//   按在身体上（不是头）拖动，是拖窗口。

#pragma once

namespace pet {

enum class Gesture {
    None,
    PetTick,    // 正在抚摸，每帧发一次，amount 是这帧的秒数
    Hit,        // 一次快速抽打
    Poke,       // 点一下
    DragStart,  // 开始拖动窗口
    DragMove,   // 拖动中，dx dy 是位移
    DragEnd,
    Release,    // 松开（抚摸或打之后）
};

struct GestureEvent {
    Gesture g = Gesture::None;
    float   amount = 0.0f;   // PetTick 用
    int     dx = 0, dy = 0;  // DragMove 用
};

struct PointerSample {
    bool  pressed = false;   // 左键按着
    bool  onHead = false;    // 光标在头的屏幕矩形里
    bool  onBody = false;    // 光标在狗身上（含头）
    int   x = 0, y = 0;      // 客户区像素
    float dt = 0.0f;         // 距上一帧秒数
    float dpiScale = 1.0f;   // 像素阈值按它缩放
};

class GestureTracker {
public:
    // 每帧调一次。返回这一帧产生的事件（最多一个）。
    GestureEvent update(const PointerSample& s);

    bool pressing() const { return down_; }
    bool dragging() const { return mode_ == Mode::Drag; }
    bool petting() const { return mode_ == Mode::Pet; }

private:
    enum class Mode { Idle, Undecided, Pet, Drag };
    Mode  mode_ = Mode::Idle;
    bool  down_ = false;
    bool  downOnHead_ = false;
    bool  hoverOnHead_ = false;  // 没按键时光标在头上（1.7 起悬停就能摸 / 打）
    int   downX_ = 0, downY_ = 0;
    int   lastX_ = 0, lastY_ = 0;
    float held_ = 0.0f;         // 按住多久了
    float travel_ = 0.0f;       // 按住期间累计移动距离（像素）
    float speedAvg_ = 0.0f;     // 平滑后的移动速度（像素/秒）
    float hitCooldown_ = 0.0f;  // 两次「打」之间的最小间隔
    int   lastDirX_ = 0;        // 上一段的横向方向，来回抽打靠它计数
    int   reversals_ = 0;
};

}  // namespace pet
