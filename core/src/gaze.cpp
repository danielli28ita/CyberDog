#include "core/gaze.h"
#include "core/math3d.h"

#include <cmath>

namespace pet {
namespace {

// 眼睛能转到的最大角度。超过这个就得转头。
constexpr float kEyeMaxYaw   = 0.42f;
constexpr float kEyeMaxPitch = 0.30f;
// 头能转到的最大角度。
constexpr float kHeadMaxYaw   = 0.75f;
constexpr float kHeadMaxPitch = 0.35f;

// 时间常数。眼睛快，头慢——这是「眼先动、头后动」的全部实现。
constexpr float kEyeTau  = 0.045f;
constexpr float kHeadTau = 0.22f;
constexpr float kLidTau  = 0.08f;
constexpr float kBrowTau = 0.12f;

// 眨眼三段：闭合、停住、睁开。
constexpr float kBlinkClose = 0.07f;
constexpr float kBlinkHold  = 0.04f;
constexpr float kBlinkOpen  = 0.11f;
constexpr float kBlinkTotal = kBlinkClose + kBlinkHold + kBlinkOpen;

// 头只追视线的一部分，剩下的留给眼睛，看起来才是「用眼睛看」不是「用脖子看」。
constexpr float kHeadShare = 0.70f;

}  // namespace

const char* gaze_mood_name(GazeMood m) {
    switch (m) {
        case GazeMood::Track:   return "盯着";
        case GazeMood::Wander:  return "扫视";
        case GazeMood::Viewer:  return "看你";
        case GazeMood::SideEye: return "斜眼";
        case GazeMood::Startle: return "受惊";
        case GazeMood::Sleepy:  return "犯困";
    }
    return "?";
}

GazeController::GazeController(const Personality& p, std::uint64_t seed)
    : p_(p), rng_(seed) {
    blinkTimer_ = rng_.range(1.0f, 3.0f);
    pick_wander_goal();
}

void GazeController::start_blink(bool doubleBlink) {
    if (blinkPhase_ >= 0) return;  // 已经在眨
    blinkPhase_ = 0.0f;
    blinkAgain_ = doubleBlink;
    ++blinks_;
}

float GazeController::blink_curve() const {
    if (blinkPhase_ < 0) return 0.0f;
    const float t = blinkPhase_;
    if (t < kBlinkClose) return t / kBlinkClose;
    if (t < kBlinkClose + kBlinkHold) return 1.0f;
    const float o = (t - kBlinkClose - kBlinkHold) / kBlinkOpen;
    return o >= 1.0f ? 0.0f : 1.0f - o;
}

void GazeController::pick_wander_goal() {
    // 活泼的狗换得快。区间 0.6–2.4 秒，liveliness=1 时压到前 45%。
    const float span = 1.8f * (1.0f - 0.55f * p_.liveliness);
    wanderTimer_ = 0.6f + rng_.unit() * span;

    // 三种去处，按性格抽签：
    //   斜眼——捣蛋的表情，概率跟 mischief 走，但封顶，否则整天斜眼
    //   看向观察者——卖萌，概率跟 charm 走
    //   其余随机扫视
    const float r = rng_.unit();
    const float pSide   = 0.10f + 0.20f * p_.mischief;   // 0.10–0.30
    const float pViewer = 0.10f + 0.20f * p_.charm;      // 0.10–0.30

    if (r < pSide) {
        mood_ = GazeMood::SideEye;
        moodTimer_ = rng_.range(0.9f, 2.0f);
        const float side = rng_.chance(0.5f) ? 1.0f : -1.0f;
        goalYaw_   = side * kEyeMaxYaw;          // 眼珠到边
        goalPitch_ = rng_.range(-0.05f, 0.10f);
        headGoalYaw_ = -side * 0.08f;            // 头微微往反方向偏，眼珠显得更斜
        headGoalPitch_ = -0.04f;
        headGoalRoll_ = 0.0f;
        lidGoal_  = 0.45f;                       // 眼睑半合
        browGoal_ = 1.0f;                        // 挑眉
        return;
    }
    if (r < pSide + pViewer) {
        mood_ = GazeMood::Viewer;
        moodTimer_ = rng_.range(1.2f, 2.6f);
        goalYaw_ = 0.0f;
        goalPitch_ = 0.06f;
        headGoalYaw_ = 0.0f;
        headGoalPitch_ = 0.04f;
        // 歪头。charm 越高越常歪，幅度 0.18–0.30 弧度。
        headGoalRoll_ = rng_.chance(0.3f + 0.5f * p_.charm)
                          ? (rng_.chance(0.5f) ? 1.0f : -1.0f) * rng_.range(0.18f, 0.30f)
                          : 0.0f;
        lidGoal_ = 0.0f;
        browGoal_ = 0.0f;
        return;
    }

    mood_ = GazeMood::Wander;
    moodTimer_ = 0.0f;
    goalYaw_   = rng_.range(-0.55f, 0.55f);
    // 多往斜上方看：狗在屏幕右下角，人在屏幕上方，抬眼才像在看人（作者要求，1.7）。
    // 六成机会抬眼（俯仰 0.15–0.40），其余随机；往下看只到 -0.15。
    goalPitch_ = rng_.chance(0.6f) ? rng_.range(0.15f, 0.40f) : rng_.range(-0.15f, 0.30f);
    headGoalYaw_   = goalYaw_ * kHeadShare;
    headGoalPitch_ = goalPitch_ * kHeadShare;
    headGoalRoll_  = 0.0f;
    lidGoal_ = 0.0f;
    browGoal_ = 0.0f;
    // 大幅度扫视常伴随一次眨眼，真实眼动也是这样。
    if (std::fabs(goalYaw_ - eyeYaw_) > 0.5f && rng_.chance(0.5f)) start_blink(false);
}

void GazeController::update(float dt, const GazeInput& in) {
    dt = clampf(dt, 0.0f, 0.1f);

    // ---- 目标选择 ----
    if (in.wantViewer) {
        // 动作系统要求看向观察者。覆盖随机扫视，但眨眼照常。
        noTargetFor_ = 0.0f;
        mood_ = GazeMood::Viewer;
        moodTimer_ = 0.0f;
        wanderTimer_ = 0.3f;   // 覆盖结束后很快换一个目标，不会僵住
        goalYaw_ = 0.0f;
        goalPitch_ = 0.06f;
        headGoalYaw_ = 0.0f;
        headGoalPitch_ = 0.04f;
        headGoalRoll_ = in.rollGoal;
        lidGoal_ = in.lidGoal;
        browGoal_ = 0.0f;
    } else if (in.hasTarget) {
        noTargetFor_ = 0.0f;
        if (in.targetAppeared) {
            // 受惊：瞪大，随后眨一下。胆小的瞪得久。
            mood_ = GazeMood::Startle;
            moodTimer_ = 0.25f + 0.35f * p_.timidity;
            lidGoal_ = -0.20f;   // 负值让眼睑缩到最小，比平时睁得更开
            browGoal_ = 0.0f;
            blinkTimer_ = moodTimer_ + 0.05f;
        }
        if (mood_ == GazeMood::Startle) {
            moodTimer_ -= dt;
            if (moodTimer_ <= 0.0f) { mood_ = GazeMood::Track; lidGoal_ = 0.0f; }
        } else {
            // 目标出现前如果正在斜眼，让它把这个表情做完再转为盯着。
            if (mood_ == GazeMood::SideEye) moodTimer_ -= dt;
            if (mood_ != GazeMood::SideEye || moodTimer_ <= 0.0f) {
                mood_ = GazeMood::Track;
                lidGoal_ = 0.0f;
                browGoal_ = 0.0f;
                headGoalRoll_ = 0.0f;
            }
        }
        goalYaw_   = clampf(in.targetYaw,   -(kHeadMaxYaw + kEyeMaxYaw),   kHeadMaxYaw + kEyeMaxYaw);
        goalPitch_ = clampf(in.targetPitch, -(kHeadMaxPitch + kEyeMaxPitch), kHeadMaxPitch + kEyeMaxPitch);
        headGoalYaw_   = clampf(goalYaw_ * kHeadShare,   -kHeadMaxYaw,   kHeadMaxYaw);
        headGoalPitch_ = clampf(goalPitch_ * kHeadShare, -kHeadMaxPitch, kHeadMaxPitch);
    } else {
        noTargetFor_ += dt;
        if (mood_ == GazeMood::Track || mood_ == GazeMood::Startle) {
            // 目标刚走：先在原地停一下，再开始扫视。
            mood_ = GazeMood::Wander;
            wanderTimer_ = rng_.range(0.4f, 1.0f);
            lidGoal_ = 0.0f;
            browGoal_ = 0.0f;
        }
        if (moodTimer_ > 0.0f) moodTimer_ -= dt;
        wanderTimer_ -= dt;
        if (wanderTimer_ <= 0.0f && moodTimer_ <= 0.0f) pick_wander_goal();

        // 长时间没人理：眼睑慢慢垂下来。懒的狗更快。
        const float sleepyAfter = 40.0f * (1.6f - p_.laziness);
        if (noTargetFor_ > sleepyAfter && mood_ == GazeMood::Wander) {
            mood_ = GazeMood::Sleepy;
        }
        if (mood_ == GazeMood::Sleepy) {
            const float deep = clampf((noTargetFor_ - sleepyAfter) / 30.0f, 0.0f, 1.0f);
            lidGoal_ = 0.30f + 0.30f * deep;
            headGoalPitch_ = -0.10f * deep;
        }
    }

    // ---- 眨眼节奏 ----
    // 间隔 2–6 秒，活泼的狗眨得勤。犯困时眨得慢而重。
    if (blinkPhase_ < 0) {
        blinkTimer_ -= dt;
        if (blinkTimer_ <= 0.0f) {
            start_blink(rng_.chance(0.18f));
            const float base = mood_ == GazeMood::Sleepy ? 5.0f : 2.0f;
            blinkTimer_ = base + rng_.unit() * 4.0f * (1.0f - 0.5f * p_.liveliness);
        }
    } else {
        blinkPhase_ += dt;
        if (blinkPhase_ >= kBlinkTotal) {
            blinkPhase_ = -1.0f;
            if (blinkAgain_) { blinkAgain_ = false; blinkPhase_ = 0.0f; ++blinks_; }
        }
    }
    const float blink = blink_curve();

    // ---- 平滑 ----
    // 眼睛先到，头后到；头到多少，眼睛就回中多少。
    eyeYaw_   = approach(eyeYaw_,   goalYaw_,   dt, kEyeTau);
    eyePitch_ = approach(eyePitch_, goalPitch_, dt, kEyeTau);
    s_.headYaw   = approach(s_.headYaw,   headGoalYaw_,   dt, kHeadTau);
    s_.headPitch = approach(s_.headPitch, headGoalPitch_, dt, kHeadTau);
    s_.headRoll  = approach(s_.headRoll,  headGoalRoll_,  dt, kHeadTau * 1.5f);

    // 瞳孔偏移 = 视线减去头已经转过的部分。再加一点微漂移，静止的眼珠是死的。
    driftPhase_ += dt;
    const float driftX = 0.04f * std::sin(driftPhase_ * 1.7f) * std::sin(driftPhase_ * 0.31f);
    const float driftY = 0.03f * std::sin(driftPhase_ * 1.3f + 1.0f);
    const float px = clampf((eyeYaw_   - s_.headYaw)   / kEyeMaxYaw   + driftX, -1.0f, 1.0f);
    const float py = clampf((eyePitch_ - s_.headPitch) / kEyeMaxPitch + driftY, -1.0f, 1.0f);
    s_.pupilX[0] = s_.pupilX[1] = px;
    s_.pupilY[0] = s_.pupilY[1] = py;

    // 眼睑 = 表情层与眨眼取大。表情层平滑，眨眼不平滑（它本来就是快动作）。
    lidExpr_ = clampf(approach(lidExpr_, lidGoal_, dt, kLidTau), -0.2f, 1.0f);
    s_.lidClose[0] = s_.lidClose[1] = blink > lidExpr_ ? blink : lidExpr_;
    s_.browTilt = approach(s_.browTilt, browGoal_, dt, kBrowTau);
}

}  // namespace pet
