// 眼神与头部朝向。设计文档 §2.3 L3 的一部分，按作者要求提前到 P1。
//
// 纯 C++，不知道相机、窗口、光标是什么。输入是模型空间里的目标方向，
// 输出是头的三个转角、两只眼的瞳孔位置和眼睑闭合度。
// 换成蒙皮模型之后，这份输出直接驱动头骨、眼球骨和眼睑骨，本文件不用改。
//
// 让眼睛显得「活」的三个手法，都在这里：
//   1. 眼先动、头后动。眼睛几十毫秒到位，头用零点几秒跟上，跟上以后瞳孔回中。
//   2. 没有目标时眼睛不会停：随机扫视、偶尔看向观察者、偶尔斜眼。
//   3. 眨眼有节奏、有双眨；受惊时先瞪大再眨。
// 性格参数决定这些行为的频率和倾向，所以两条不同种子的狗眼神不一样。

#pragma once

#include <cstdint>

#include "core/personality.h"
#include "core/rng.h"

namespace pet {

// 每帧给控制器的输入。角度是模型空间弧度，符号约定同 math3d.h 的 rotate_*：
// yaw 正 = 鼻尖转向 +x，pitch 正 = 抬头。
struct GazeInput {
    bool  hasTarget = false;   // 有没有值得看的东西（光标在窗口里）
    float targetYaw = 0.0f;
    float targetPitch = 0.0f;
    bool  targetAppeared = false;   // 这一帧目标刚出现（光标刚进来），会触发受惊反应

    // 动作系统的覆盖：动作进行中要求特定眼神时设置。
    // 例如歪头卖萌要看向观察者并瞪大，扑光标要死盯目标。
    bool  wantViewer = false;   // 看向观察者，忽略随机扫视
    float rollGoal = 0.0f;      // wantViewer 时的头部侧倾目标
    float lidGoal  = 0.0f;      // wantViewer 时的眼睑目标（负值瞪大）
};

// 控制器输出。渲染侧把它翻译成部件姿态。
struct GazeState {
    float headYaw = 0.0f, headPitch = 0.0f, headRoll = 0.0f;   // 弧度
    float pupilX[2] = {0, 0};   // [0]=左眼 [1]=右眼，归一化 [-1,1]，正 = 向 +x
    float pupilY[2] = {0, 0};   // 正 = 向上
    float lidClose[2] = {0, 0}; // 0 全睁 1 全闭
    float browTilt = 0.0f;      // 0–1，挑眉：内眼角压低，配合斜眼用
};

// 当前在做什么，只用于日志和调试。
enum class GazeMood : std::uint8_t {
    Track,     // 盯着目标
    Wander,    // 没目标，随机扫视
    Viewer,    // 看向观察者（正前方），可能歪头
    SideEye,   // 斜眼：头不动，眼珠到边上，眼睑半合，挑眉。捣蛋的表情
    Startle,   // 受惊：瞪大
    Sleepy,    // 长时间没人理：眼睑半垂
};
const char* gaze_mood_name(GazeMood m);

class GazeController {
public:
    GazeController(const Personality& p, std::uint64_t seed);

    // dt 单位秒。调用方要把它夹在合理范围（渲染停了一分钟再回来，dt 不该是 60）。
    void update(float dt, const GazeInput& in);

    const GazeState& state() const { return s_; }
    GazeMood mood() const { return mood_; }

    // 累计眨眼次数，测试和日志用。
    unsigned blink_count() const { return blinks_; }

private:
    void pick_wander_goal();
    void start_blink(bool doubleBlink);
    float blink_curve() const;

    Personality p_;
    Rng rng_;
    GazeState s_;
    GazeMood mood_ = GazeMood::Wander;

    // 目标视线（眼睛要看的方向）与头的目标。
    float goalYaw_ = 0, goalPitch_ = 0;
    float headGoalYaw_ = 0, headGoalPitch_ = 0, headGoalRoll_ = 0;
    // 眼睛实际视线，头在后面追它。
    float eyeYaw_ = 0, eyePitch_ = 0;

    // 表情层的眼睑目标与当前值、挑眉目标（都不含眨眼）。
    float lidGoal_ = 0, lidExpr_ = 0, browGoal_ = 0;

    // 计时器，单位秒。
    float wanderTimer_ = 0;    // 下一次换扫视目标
    float moodTimer_ = 0;      // 当前特殊表情还剩多久
    float blinkTimer_ = 0;     // 下一次眨眼
    float blinkPhase_ = -1;    // <0 不在眨眼；否则是眨眼进行到第几秒
    bool  blinkAgain_ = false; // 这次眨完再眨一次
    float noTargetFor_ = 0;    // 连续没目标多久了
    float driftPhase_ = 0;     // 瞳孔微漂移的相位

    unsigned blinks_ = 0;
};

}  // namespace pet
