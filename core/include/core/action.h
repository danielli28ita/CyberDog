// 动作系统。设计文档 §2.3 的 L1（效用打分选动作）与 L3（参数化姿态过渡）的代理体版本，
// 按作者要求提前到 P1。纯 C++。
//
// 两个类：
//   ActionSelector  决定「接下来做什么」。打分 = 性格匹配 + 内部需求 + 上下文 − 近期重复惩罚，
//                   softmax 采样而不是取最高分，保留不可预测性。
//   ActionPlayer    把选中的动作播出来：走路周期、坐下、翻身、玩球的球物理、打翻水碗……
//                   输出是部件姿态（PartPose）和对眼神的覆盖要求。
//
// 舞台：狗在一条 x 轴上活动，范围 ±kStageHalfWidth，z 轻微前后。相机不动。
// 动作全部在窗口内完成，不碰用户的任何东西（§2.5 捣乱边界）。

#pragma once

#include <cstdint>

#include "core/gaze.h"
#include "core/personality.h"
#include "core/proxy_mesh.h"
#include "core/rng.h"

namespace pet {

enum class ActionKind : std::uint8_t {
    Idle = 0,     // 站着不动，眼神系统自己玩
    Walk,         // 闲逛到舞台上另一个位置
    Stretch,      // 伸懒腰
    Shake,        // 抖身子
    Sit,          // 坐下待一会
    CharmTilt,    // 卖萌：坐下歪头看你
    CharmPaw,     // 卖萌：坐下抬前爪扒拉
    CharmBelly,   // 卖萌：躺下翻肚皮蹬腿
    PlayBall,     // 玩球：拱一下让球滚开，追上去再拱
    Pounce,       // 捣蛋：冲着光标扑一小段，又跑回来。需要光标在窗口里
    FlipBowl,     // 捣蛋：一爪子把水碗拍翻，然后看你
    RemindWater,  // 健康提醒：叼水碗到中间放下，坐着看你。只由 pet-action 触发
    RemindStand,  // 健康提醒：来回走两趟，抖一下。只由 pet-action 触发
    Entrance,     // 出场：从任务栏下面钻上来，抖一下，看一眼你（§2.6）。只在启动时
    Petted,       // 被摸：眯眼、头往手上蹭、尾巴猛摇。按住期间由 hold() 续
    Cower,        // 挨打：缩身、耳朵贴下、看别处。之后一段时间乖
    Poked,        // 被戳一下：小小地弹一下
    Sleep,        // 趴下睡觉。眼闭、呼吸起伏、偶尔打呼。hold() 续，有交互就醒
    RemindMemo,   // 备忘提醒：跑到中间坐下、叫两声、看你。只由插件触发
    Charge,       // 冲屏：朝相机蹿过来，脸贴到屏幕前叫一声，再退回去。3D 感的主要来源之一
    ReturnHome,   // 回巢：走回栖位（舞台原点），转回面向相机。停靠前由宿主触发
    Count
};
// 提醒动作在 1.1 里换了内容：RemindWater = 轻叫→字幕→尿尿；RemindStand = 大跳→字幕→把球踢出屏幕。
// 枚举名不改，插件和存档都不用动。

const char* action_name(ActionKind k);

// 舞台上的一帧上下文。
struct ActionContext {
    bool  cursorInside = false;
    float cursorStageX = 0.0f;   // 光标投到舞台 x 轴上的位置，[-1,1]
    float cursorYaw = 0.0f;      // 光标相对狗的模型空间偏航（和 GazeInput 同一套）
};

// 默认舞台宽度（窗口还是 640 px 时的值）。1.2 起窗口是整个显示器，宿主用 set_stage 改。
inline constexpr float kStageHalfWidth = 0.72f;

// 狗在舞台上的位置与朝向。yaw=0 朝 +z（面向相机方向），正 yaw 鼻尖转向 +x。
struct DogState {
    float x = 0.0f, z = 0.0f, yaw = 0.0f;
};

// ---------------------------------------------------------------------------

class ActionSelector {
public:
    ActionSelector(const Personality& p, std::uint64_t seed);

    void set_personality(const Personality& p) { p_ = p; }

    // 内部需求随时间漂移。dt 秒。
    void drift(float dt);

    // 选一个动作。ctx 决定哪些动作可选（扑光标要光标在）。
    ActionKind choose(const ActionContext& ctx);

    // 动作播完后调，更新需求与近期记录。
    void on_finished(ActionKind k, double nowSeconds);

    // 亲密度与「乖」状态影响打分：亲密高多卖萌少斜眼；乖着的时候不捣蛋。
    void set_bond(float affinity01, bool obedient) { affinity_ = affinity01; obedient_ = obedient; }
    // 允不允许离开原地的动作（见 action_roams）。宿主在最近有人互动过时才放开。
    void set_roam_allowed(bool v) { roamAllowed_ = v; }
    bool roam_allowed() const { return roamAllowed_; }

    // 两个动作之间歇多久。懒的狗歇得久。
    float rest_seconds();

    struct Needs { float energy = 0.8f, social = 0.3f, curiosity = 0.5f, boredom = 0.2f; };
    const Needs& needs() const { return needs_; }

    // 打分公式公开，测试和可解释面板（P7）用。
    float score(ActionKind k, const ActionContext& ctx, double now) const;

private:

    Personality p_;
    Rng rng_;
    Needs needs_;
    double lastPlayed_[static_cast<int>(ActionKind::Count)]{};
    double clock_ = 0.0;
    float  affinity_ = 0.3f;
    bool   obedient_ = false;
    bool   roamAllowed_ = true;
};

// CyberDog 1.2：性格→行为扩展钩。默认恒为 1，不改变现有打分。
// 以后版本可在此按 ActionKind / 性格覆写倍率；本版禁止改 action_catalog 权重。
inline float personality_behavior_scale(ActionKind /*k*/) { return 1.0f; }

// ---------------------------------------------------------------------------

class ActionPlayer {
public:
    explicit ActionPlayer(std::uint64_t seed);

    // 开始一个动作。返回 false 表示这个动作现在做不了（例如扑光标但光标不在）。
    bool start(ActionKind k, const ActionContext& ctx);
    // 中断当前动作，狗回到站姿。道具收起。
    void cancel();

    // 持续型反应（被摸）：已经在做就把结束时间往后推，否则开始。
    void hold(ActionKind k, float seconds);

    // 舞台 x 范围（模型单位，+x 是屏幕左）。狗走动、球滚动都在这个范围内；
    // 踢出屏幕的球例外。1.2 起由宿主按显示器宽度算。
    void set_stage(float xMin, float xMax) { stageMin_ = xMin; stageMax_ = xMax; }
    // 用户拖着狗走（窗口是整个屏幕，拖的是狗不是窗口）。
    void drag_to(float x) { dog_.x = clampf(x, stageMin_, stageMax_); }

    // 「朝观众」在舞台平面上的方向（从舞台原点指向相机，xz 归一化）。冲屏沿这个方向蹿，
    // 才会在画面里变大而不是往下滑出去。宿主从相机位置算。
    void set_toward_viewer(float dx, float dz) { viewDirX_ = dx; viewDirZ_ = dz; }
    float stage_min() const { return stageMin_; }
    float stage_max() const { return stageMax_; }
    // 巢：闲置回巢走到这里。1.6 起宿主把它设在屏幕右下角（-x 是屏幕右），
    // 缩小不再靠平移推到角落——缩放绕狗自己的脚做，鼠标靠近时它就不会从光标底下滑走。
    void set_home(float x, float z) { homeTargetX_ = x; homeTargetZ_ = z; }
    float home_x() const { return homeTargetX_; }
    float home_z() const { return homeTargetZ_; }

    void update(float dt, const ActionContext& ctx);

    // 这一帧动作里发生了需要出声的事（球落地、碗翻倒……）。取一次清一次。
    // 返回值对应 sound_synth.h 的 SoundId 下标，-1 表示没有。core 不依赖音频后端。
    int take_sound();

    bool active() const { return kind_ != ActionKind::Idle; }
    ActionKind current() const { return kind_; }
    float elapsed() const { return t_; }
    const DogState& dog() const { return dog_; }

    // 把体态写进 poses（先清零再调）。眼神在这之后叠加。
    void apply(PartPose* poses) const;

    // 当前动作对眼神的要求。
    void gaze_override(GazeInput& in) const;

private:
    // 走向目标点，返回是否到达。会转身、迈步。
    bool walk_toward(float tx, float tz, float speed, float dt);
    void turn_toward(float yawGoal, float dt, float rate = 5.0f);
    void finish();

    // 各动作的每帧逻辑。
    void tick_walk(float dt);
    void tick_stretch();
    void tick_shake();
    void tick_sit_like();
    void tick_belly();
    void tick_ball(float dt);
    void tick_pounce(float dt, const ActionContext& ctx);
    void tick_flip_bowl(float dt);
    void tick_remind_water(float dt);
    void tick_remind_stand(float dt);
    void tick_entrance(float dt);
    void tick_petted();
    void tick_cower();
    void tick_poked();
    void tick_sleep();
    void tick_remind_memo(float dt);
    void tick_charge(float dt);
    float rand_stage_x(float minDistFromDog);

    Rng rng_;
    ActionKind kind_ = ActionKind::Idle;
    float t_ = 0.0f;          // 动作进行到第几秒
    float duration_ = 0.0f;   // 预计总时长，个别动作按事件提前结束
    int   phase_ = 0;         // 动作内部阶段
    DogState dog_;

    // 体态参数，apply() 用。
    float bodyLift_ = 0.0f;                     // 躯干抬高（负值下沉）
    Vec3  bodyRot_{0, 0, 0};                    // 躯干附加旋转（不含朝向 yaw）
    float legRot_[4]{};                         // FL FR BL BR 绕 x 的角度
    float legScaleY_[4]{1.0f, 1.0f, 1.0f, 1.0f};   // 腿长倍率。趴下时后腿缩短塞到身子底下
    float headPitch_ = 0.0f, headRoll_ = 0.0f;  // 头部附加
    float earFlap_ = 0.0f;                      // 耳朵张开（抖身用）
    float walkPhase_ = 0.0f;
    float walkAmp_ = 0.0f;                      // 迈步幅度，停下来时衰减到 0

    // 眼神要求
    bool  wantViewer_ = false;
    float rollGoal_ = 0.0f, lidGoal_ = 0.0f;

    // 道具
    struct Ball { bool visible = false; Vec3 pos; Vec3 vel; float spinX = 0, spinZ = 0; float scale = 0; } ball_;
    struct Bowl { bool visible = false; Vec3 pos; float tilt = 0; float scale = 0; bool carried = false; } bowl_;
    struct Pee  { float stream = 0; float puddle = 0; Vec3 puddlePos; } pee_;
    float poses_leg_lift_ = 0.0f;             // 左后腿绕 z 抬起（尿尿）
    float bodyScaleY_ = 1.0f;                 // 呼吸起伏
    float snoreTimer_ = 0.0f;
    float stageMin_ = -kStageHalfWidth, stageMax_ = kStageHalfWidth;
    float viewDirX_ = 0.0f, viewDirZ_ = 1.0f;
    float homeZ_ = 0.0f;

    float targetX_ = 0.0f, targetZ_ = 0.0f;   // 走路目标
    float homeX_ = 0.0f;                      // 扑光标前的位置
    float homeTargetX_ = 0.0f, homeTargetZ_ = 0.0f;   // 巢（回巢目标）
    float phaseT_ = 0.0f;                     // 当前阶段开始时的 t_
    int   pendingSound_ = -1;
    float stepAccum_ = 0.0f;                  // 走路脚步声计数
    float side_ = 1.0f;                       // 一些动作用的左右
};

}  // namespace pet
