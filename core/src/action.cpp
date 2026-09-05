#include "core/action.h"
#include "core/action_catalog.h"

#include <algorithm>
#include <cmath>

namespace pet {
namespace {

constexpr float kPi = 3.14159265f;
constexpr int kActionCount = static_cast<int>(ActionKind::Count);

// 0→1 的平滑过渡。
float smooth(float x) {
    x = clampf(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

// 在 [a,b] 区间内从 0 升到 1，之后保持 1；区间外为 0。
float ramp(float t, float a, float b) {
    if (b <= a) return t >= a ? 1.0f : 0.0f;
    return smooth((t - a) / (b - a));
}

// 起 in 秒、停 out 秒的包络：0→1→1→0。
float envelope(float t, float total, float in, float out) {
    return ramp(t, 0.0f, in) * (1.0f - ramp(t, total - out, total));
}

float wrap_angle(float a) {
    while (a > kPi) a -= 2.0f * kPi;
    while (a < -kPi) a += 2.0f * kPi;
    return a;
}

// 狗体心到鼻尖 1.08，玩球时站位要留出这段距离加球半径。
constexpr float kBallStandoff = 1.08f + kBallRadius;

// 坐姿：后腿前折、躯干后倾下沉、前腿反向补偿保持竖直。
// 第一版 pitch 0.45 截图里像仰着坐在尾巴上，压小。
constexpr float kSitBodyPitch = 0.30f;
constexpr float kSitBodyLift  = -0.13f;
constexpr float kSitHindFold  = 1.35f;

// 狗体心到鼻尖 1.08。走向一个道具时，落点要沿接近方向退这么多，鼻尖才刚碰到它。
// 第一版直接用了体心距离，截图里碗在狗肚子底下。
constexpr float kNoseReach = 1.08f;

// 从狗当前位置出发、沿直线接近 target，停在距它 standoff 的点。
void approach_point(const DogState& dog, Vec3 target, float standoff, float& outX, float& outZ) {
    const float dx = target.x - dog.x, dz = target.z - dog.z;
    const float d = std::sqrt(dx * dx + dz * dz);
    if (d < 1e-3f) { outX = target.x - standoff; outZ = target.z; return; }
    outX = target.x - dx / d * standoff;
    outZ = target.z - dz / d * standoff;
}

}  // namespace

const char* action_name(ActionKind k) {
    return k < ActionKind::Count ? action_spec(k).name : "?";
}

// 与 sound_synth.h 的 SoundId 顺序一致。core/action 不引用那个头文件，只传下标。
namespace snd {
constexpr int Bark = 0, Whimper = 1, Happy = 2, Pant = 3, Bounce = 4, Clatter = 5, Shake = 6, Step = 7, Sniff = 8,
              BarkSoft = 9, Pee = 10, Kick = 11, Snore = 12;
}

// ===========================================================================
// ActionSelector
// ===========================================================================

ActionSelector::ActionSelector(const Personality& p, std::uint64_t seed)
    : p_(p), rng_(seed) {
    for (double& t : lastPlayed_) t = -1e9;
}

void ActionSelector::drift(float dt) {
    clock_ += dt;
    // 需求随时间漂移：越久没玩越无聊，越久没人理越想社交，休息回精力。
    needs_.boredom   = clampf(needs_.boredom + 0.012f * dt, 0.0f, 1.0f);
    needs_.social    = clampf(needs_.social + 0.008f * dt, 0.0f, 1.0f);
    needs_.energy    = clampf(needs_.energy + 0.006f * dt, 0.0f, 1.0f);
    needs_.curiosity = clampf(needs_.curiosity + 0.005f * dt, 0.0f, 1.0f);
}

float ActionSelector::score(ActionKind k, const ActionContext& ctx, double now) const {
    const ActionSpec& a = action_spec(k);
    if (!a.selectable) return -100.0f;                       // 只由宿主触发，不参与自选
    if (a.requiresCursor && !ctx.cursorInside) return -100.0f;
    if (a.blockedWhenObedient && obedient_) return -100.0f;  // 刚被摸过 / 打过：乖一点
    if (!roamAllowed_ && action_roams(k)) return -100.0f;    // 没人理的时候待在原地

    const Needs& n = needs_;
    const float needVec[4] = {n.energy, n.social, n.curiosity, n.boredom};
    float s = personality_dot(p_, a.wPersonality) + a.constant;
    for (int i = 0; i < 4; ++i) s += needVec[i] * a.wNeeds[i];
    s += ctx.cursorInside ? a.withCursor : a.withoutCursor;
    s *= a.affinity[0] + a.affinity[1] * affinity_;

    // 近期重复惩罚：刚做过的打折，90 秒恢复。只加这一项就能消除重复感（设计文档 §2.3 L1）。
    const double since = now - lastPlayed_[static_cast<int>(k)];
    s -= 1.2f * static_cast<float>(std::exp(-since / 90.0));
    return s;
}

ActionKind ActionSelector::choose(const ActionContext& ctx) {
    float sc[kActionCount];
    float mx = -1e9f;
    for (int i = 0; i < kActionCount; ++i) {
        sc[i] = score(static_cast<ActionKind>(i), ctx, clock_);
        if (sc[i] > mx) mx = sc[i];
    }
    // softmax 采样，温度 0.35。温度低一点，让性格差异看得出来，但不是确定性的。
    const float temp = 0.35f;
    float w[kActionCount];
    float sum = 0.0f;
    for (int i = 0; i < kActionCount; ++i) {
        w[i] = sc[i] < -50.0f ? 0.0f : std::exp((sc[i] - mx) / temp);
        sum += w[i];
    }
    float r = rng_.unit() * sum;
    for (int i = 0; i < kActionCount; ++i) {
        r -= w[i];
        if (r <= 0.0f && w[i] > 0.0f) return static_cast<ActionKind>(i);
    }
    return ActionKind::Idle;
}

void ActionSelector::on_finished(ActionKind k, double nowSeconds) {
    lastPlayed_[static_cast<int>(k)] = nowSeconds;
    clock_ = nowSeconds;
    Needs& n = needs_;
    const ActionSpec& a = action_spec(k);
    n.energy += a.cost[0];
    n.social += a.cost[1];
    n.curiosity += a.cost[2];
    n.boredom += a.cost[3];
    n.energy = clampf(n.energy, 0.0f, 1.0f);
    n.social = clampf(n.social, 0.0f, 1.0f);
    n.boredom = clampf(n.boredom, 0.0f, 1.0f);
    n.curiosity = clampf(n.curiosity, 0.0f, 1.0f);
}

float ActionSelector::rest_seconds() {
    // 1.5–3 秒起步，懒散最多再加 5 秒。
    return 1.5f + rng_.unit() * 1.5f + 5.0f * p_.laziness * rng_.unit();
}

// ===========================================================================
// ActionPlayer
// ===========================================================================

ActionPlayer::ActionPlayer(std::uint64_t seed) : rng_(seed) {}

bool ActionPlayer::start(ActionKind k, const ActionContext& ctx) {
    if (k == ActionKind::Pounce && !ctx.cursorInside) return false;
    cancel();
    kind_ = k;
    t_ = 0.0f;
    phase_ = 0;
    side_ = rng_.chance(0.5f) ? 1.0f : -1.0f;
    switch (k) {
        case ActionKind::Idle:        duration_ = 0.0f; break;
        case ActionKind::Walk: {
            // 至少走 0.6 的距离，否则看不出在闲逛。舞台大了也不一口气走到头：最多 3 个单位。
            targetX_ = rand_stage_x(0.6f);
            targetZ_ = rng_.range(-0.15f, 0.15f);
            duration_ = 8.0f;
            break;
        }
        case ActionKind::Stretch:     duration_ = 2.2f; break;
        case ActionKind::Shake:       duration_ = 0.9f; break;
        case ActionKind::Sit:         duration_ = rng_.range(3.0f, 6.0f); break;
        case ActionKind::CharmTilt:   duration_ = 3.4f; break;
        case ActionKind::CharmPaw:    duration_ = 3.0f; break;
        case ActionKind::CharmBelly:  duration_ = 4.6f; break;
        case ActionKind::PlayBall:
            duration_ = 9.0f;
            ball_ = {};
            ball_.visible = true;
            ball_.pos = {clampf(dog_.x + side_ * 0.85f, stageMin_, stageMax_), 0.55f, 0.25f};
            ball_.vel = {0.0f, 0.0f, 0.0f};
            break;
        case ActionKind::Pounce:
            duration_ = 2.4f;
            homeX_ = dog_.x;
            side_ = ctx.cursorStageX > dog_.x ? 1.0f : -1.0f;
            break;
        case ActionKind::FlipBowl:
            duration_ = 4.0f;
            bowl_ = {};
            bowl_.visible = true;
            bowl_.pos = {clampf(dog_.x + side_ * 0.7f, stageMin_ + 0.2f, stageMax_ - 0.2f), 0.0f, 0.25f};
            break;
        case ActionKind::Charge:
            duration_ = 3.0f;
            homeX_ = dog_.x;
            homeZ_ = dog_.z;
            break;
        case ActionKind::ReturnHome: {
            // 时长按路程算：0.8 单位/秒，加 1.5 秒转身余量。
            const float ex = dog_.x - homeTargetX_, ez = dog_.z - homeTargetZ_;
            duration_ = std::sqrt(ex * ex + ez * ez) / 0.8f + 1.5f;
            break;
        }
        case ActionKind::RemindWater:
            // 轻叫 → 字幕（宿主 1 秒后显示）→ 走两步 → 抬后腿尿尿。
            duration_ = 6.5f;
            pee_ = {};
            pendingSound_ = snd::BarkSoft;
            targetX_ = clampf(dog_.x + (dog_.x > 0 ? -0.45f : 0.45f), stageMin_ + 0.3f, stageMax_ - 0.3f);
            break;
        case ActionKind::RemindStand:
            // 大跳 → 字幕（宿主 1.2 秒后显示）→ 球出现 → 冲过去把球踢出屏幕。
            duration_ = 7.5f;
            ball_ = {};
            break;
        case ActionKind::Entrance:
            duration_ = 3.2f;
            dog_ = {};
            bodyLift_ = -1.3f;   // 藏在任务栏下面
            break;
        case ActionKind::Petted:      duration_ = 0.8f; break;
        case ActionKind::Cower:       duration_ = 2.2f; pendingSound_ = snd::Whimper; break;
        case ActionKind::Poked:       duration_ = 0.5f; break;
        case ActionKind::Sleep:
            duration_ = rng_.range(20.0f, 60.0f);
            snoreTimer_ = rng_.range(2.0f, 5.0f);
            break;
        case ActionKind::RemindMemo:  duration_ = 6.0f; break;
        case ActionKind::Count:       kind_ = ActionKind::Idle; return false;
    }
    return true;
}

void ActionPlayer::hold(ActionKind k, float seconds) {
    if (kind_ == k) {
        if (duration_ < t_ + seconds) duration_ = t_ + seconds;
        return;
    }
    ActionContext ctx;
    start(k, ctx);
    duration_ = seconds;
}

int ActionPlayer::take_sound() {
    const int s = pendingSound_;
    pendingSound_ = -1;
    return s;
}

void ActionPlayer::cancel() {
    kind_ = ActionKind::Idle;
    t_ = 0.0f;
    phase_ = 0;
    bodyLift_ = 0.0f;
    bodyRot_ = {0, 0, 0};
    for (float& r : legRot_) r = 0.0f;
    for (float& sc : legScaleY_) sc = 1.0f;
    headPitch_ = headRoll_ = 0.0f;
    earFlap_ = 0.0f;
    walkAmp_ = 0.0f;
    wantViewer_ = false;
    rollGoal_ = lidGoal_ = 0.0f;
    ball_ = {};
    bowl_ = {};
    pee_ = {};
    poses_leg_lift_ = 0.0f;
    bodyScaleY_ = 1.0f;
}

void ActionPlayer::finish() {
    const DogState keep = dog_;
    cancel();
    dog_ = keep;
    dog_.yaw = wrap_angle(dog_.yaw);
}

void ActionPlayer::turn_toward(float yawGoal, float dt, float rate) {
    const float d = wrap_angle(yawGoal - dog_.yaw);
    const float step = rate * dt;
    dog_.yaw += clampf(d, -step, step);
}

bool ActionPlayer::walk_toward(float tx, float tz, float speed, float dt) {
    const float dx = tx - dog_.x, dz = tz - dog_.z;
    const float dist = std::sqrt(dx * dx + dz * dz);
    if (dist < 0.03f) {
        walkAmp_ = approach(walkAmp_, 0.0f, dt, 0.12f);
        return true;
    }
    // 朝向目标。yaw=0 朝 +z，正 yaw 鼻尖转向 +x，所以 yaw = atan2(dx, dz)。
    turn_toward(std::atan2(dx, dz), dt);
    const float facing = std::cos(wrap_angle(std::atan2(dx, dz) - dog_.yaw));
    if (facing > 0.5f) {
        const float step = speed * dt;
        const float k = step >= dist ? 1.0f : step / dist;
        dog_.x += dx * k;
        dog_.z += dz * k;
        walkAmp_ = approach(walkAmp_, 1.0f, dt, 0.15f);
        walkPhase_ += speed * dt * 9.0f;
    }
    return false;
}

void ActionPlayer::update(float dt, const ActionContext& ctx) {
    if (kind_ == ActionKind::Idle) {
        // 站着也把腿和身子归位，动作被中断时不会僵在半空。
        walkAmp_ = approach(walkAmp_, 0.0f, dt, 0.12f);
        return;
    }
    dt = clampf(dt, 0.0f, 0.1f);
    t_ += dt;
    wantViewer_ = false;

    switch (kind_) {
        case ActionKind::Walk:        tick_walk(dt); break;
        case ActionKind::Stretch:     tick_stretch(); break;
        case ActionKind::Shake:       tick_shake(); break;
        case ActionKind::Sit:
        case ActionKind::CharmTilt:
        case ActionKind::CharmPaw:    tick_sit_like(); break;
        case ActionKind::CharmBelly:  tick_belly(); break;
        case ActionKind::PlayBall:    tick_ball(dt); break;
        case ActionKind::Pounce:      tick_pounce(dt, ctx); break;
        case ActionKind::FlipBowl:    tick_flip_bowl(dt); break;
        case ActionKind::RemindWater: tick_remind_water(dt); break;
        case ActionKind::RemindStand: tick_remind_stand(dt); break;
        case ActionKind::Entrance:    tick_entrance(dt); break;
        case ActionKind::Petted:      tick_petted(); break;
        case ActionKind::Cower:       tick_cower(); break;
        case ActionKind::Poked:       tick_poked(); break;
        case ActionKind::Sleep:       tick_sleep(); break;
        case ActionKind::RemindMemo:  tick_remind_memo(dt); break;
        case ActionKind::Charge:      tick_charge(dt); break;
        case ActionKind::ReturnHome:
            // 走回巢，到了转回面向相机，转完就结束。和闲逛的收尾一样。
            if (phase_ == 0) { if (walk_toward(homeTargetX_, homeTargetZ_, 0.8f, dt)) phase_ = 1; }
            else {
                turn_toward(0.0f, dt, 3.0f);
                walkAmp_ = approach(walkAmp_, 0.0f, dt, 0.12f);
                if (std::fabs(wrap_angle(dog_.yaw)) < 0.03f) { dog_.yaw = 0.0f; t_ = duration_; }
            }
            break;
        default: break;
    }
    // 走路脚步声：每半个步态周期一声。
    if (walkAmp_ > 0.5f) {
        stepAccum_ += dt * 9.0f * 1.0f;   // 与 walk_toward 里的相位速度同量级
        if (stepAccum_ >= kPi) { stepAccum_ -= kPi; }   // 走路不出声（2.0，作者反馈脚步声嘈杂）；节拍留着给以后的动画用
    }
    if (kind_ != ActionKind::Idle && t_ >= duration_) finish();
}

void ActionPlayer::tick_entrance(float dt) {
    // 0–1.2 从任务栏下面钻上来（带一点前倾），1.2–2.1 抖身子，之后看向你。§2.6。
    if (t_ < 1.2f) {
        const float u = smooth(t_ / 1.2f);
        bodyLift_ = -1.3f * (1.0f - u);
        bodyRot_.x = -0.25f * std::sin(u * kPi);
        legRot_[0] = legRot_[1] = -0.5f * std::sin(u * kPi);
    } else if (t_ < 2.1f) {
        if (phase_ == 0) { phase_ = 1; pendingSound_ = snd::Shake; }
        const float u = t_ - 1.2f;
        const float e = envelope(u, 0.9f, 0.15f, 0.25f);
        bodyLift_ = 0.0f;
        bodyRot_.x = 0.0f;
        bodyRot_.z = 0.22f * std::sin(u * 42.0f) * e;
        headRoll_ = 0.35f * std::sin(u * 42.0f + 0.8f) * e;
        earFlap_ = 0.6f * e;
    } else {
        if (phase_ == 1) { phase_ = 2; pendingSound_ = snd::Bark; }
        bodyRot_.z = 0.0f;
        headRoll_ = 0.0f;
        earFlap_ = 0.0f;
        wantViewer_ = true;
        lidGoal_ = -0.1f;
        rollGoal_ = 0.2f;
    }
    (void)dt;
}

void ActionPlayer::tick_petted() {
    // 眯眼，头往上顶（蹭手），尾巴由眼神层按 Viewer 情绪猛摇。
    const float e = ramp(t_, 0.0f, 0.25f);
    headPitch_ = 0.18f * e + 0.04f * std::sin(t_ * 5.0f);
    bodyLift_ = 0.02f * e;
    wantViewer_ = true;
    lidGoal_ = 0.55f;    // 舒服地眯着
    rollGoal_ = 0.12f * std::sin(t_ * 2.2f);
    if (phase_ == 0 && t_ > 0.5f) { phase_ = 1; pendingSound_ = snd::Happy; }
}

void ActionPlayer::tick_cower() {
    // 缩身、后腿弯、头低下偏开、耳朵贴下。
    const float e = envelope(t_, duration_, 0.15f, 0.6f);
    bodyLift_ = -0.14f * e;
    bodyRot_.x = -0.10f * e;
    legRot_[2] = legRot_[3] = 0.55f * e;
    legRot_[0] = legRot_[1] = -0.15f * e;
    headPitch_ = -0.35f * e;
    headRoll_ = 0.0f;
    earFlap_ = -0.5f * e;   // 负值：耳朵向后贴
    wantViewer_ = true;
    lidGoal_ = 0.30f;
    rollGoal_ = -side_ * 0.25f;   // 头偏开
}

void ActionPlayer::tick_poked() {
    // 小小地弹一下，眼睛瞪大。
    const float u = clampf(t_ / duration_, 0.0f, 1.0f);
    bodyLift_ = 0.06f * std::sin(u * kPi);
    legRot_[0] = legRot_[1] = -0.25f * std::sin(u * kPi);
    wantViewer_ = true;
    lidGoal_ = -0.2f;
}

// ---- 各动作 ----

void ActionPlayer::tick_walk(float dt) {
    if (phase_ == 0) {
        // 闲逛到一半闻一下地面。
        if (t_ > 1.0f && t_ < 1.05f && pendingSound_ < 0 && rng_.chance(0.02f)) pendingSound_ = snd::Sniff;
        if (walk_toward(targetX_, targetZ_, 0.7f, dt)) phase_ = 1;
    } else {
        // 到了之后转回来面向相机，转完就结束。
        turn_toward(0.0f, dt, 3.0f);
        walkAmp_ = approach(walkAmp_, 0.0f, dt, 0.12f);
        if (std::fabs(wrap_angle(dog_.yaw)) < 0.03f) { dog_.yaw = 0.0f; t_ = duration_; }
    }
}

void ActionPlayer::tick_stretch() {
    // 前腿前伸、前身下压、抬头。
    const float e = envelope(t_, duration_, 0.6f, 0.6f);
    legRot_[0] = legRot_[1] = -0.95f * e;
    bodyRot_.x = -0.30f * e;   // 鼻子向下（负 pitch）
    bodyLift_ = -0.10f * e;
    headPitch_ = 0.55f * e;
}

void ActionPlayer::tick_shake() {
    if (phase_ == 0) { phase_ = 1; pendingSound_ = snd::Shake; }
    const float e = envelope(t_, duration_, 0.15f, 0.25f);
    const float w = std::sin(t_ * 42.0f);
    bodyRot_.z = 0.22f * w * e;
    headRoll_ = 0.35f * std::sin(t_ * 42.0f + 0.8f) * e;
    earFlap_ = 0.6f * e;
}

void ActionPlayer::tick_sit_like() {
    // 坐下 0.6 秒，最后 0.5 秒站起来。
    const float e = envelope(t_, duration_, 0.6f, 0.5f);
    legRot_[2] = legRot_[3] = kSitHindFold * e;
    legScaleY_[2] = legScaleY_[3] = 1.0f - 0.45f * e;   // 后腿缩短塞到身子底下，不横着伸出来（2.0）
    legRot_[0] = legRot_[1] = -kSitBodyPitch * e;   // 前腿反向补偿，保持竖直
    bodyRot_.x = kSitBodyPitch * e;
    bodyLift_ = kSitBodyLift * e;

    if (kind_ == ActionKind::CharmTilt) {
        // 坐稳之后看向观察者，歪头，每 1.2 秒换一边，眼睛睁大。
        if (t_ > 0.6f && t_ < duration_ - 0.5f) {
            wantViewer_ = true;
            const int side = static_cast<int>((t_ - 0.6f) / 1.2f) % 2;
            rollGoal_ = (side == 0 ? side_ : -side_) * 0.32f;
            lidGoal_ = -0.15f;
        }
    } else if (kind_ == ActionKind::CharmPaw) {
        // 抬右前爪，前后扒拉。
        if (t_ > 0.6f && t_ < duration_ - 0.5f) {
            wantViewer_ = true;
            lidGoal_ = -0.10f;
            const float pe = envelope(t_ - 0.6f, duration_ - 1.1f, 0.25f, 0.3f);
            legRot_[1] = (-1.35f + 0.3f * std::sin((t_ - 0.6f) * 3.2f * 6.2831853f)) * pe
                       + legRot_[1] * (1.0f - pe);
        }
    }
}

void ActionPlayer::tick_belly() {
    // 0–0.6 趴下；0.6–1.2 翻过去；中间蹬腿；最后 1.0 秒翻回来站起。
    const float lie = ramp(t_, 0.0f, 0.6f) * (1.0f - ramp(t_, duration_ - 0.5f, duration_));
    const float roll = ramp(t_, 0.6f, 1.2f) * (1.0f - ramp(t_, duration_ - 1.0f, duration_ - 0.5f));
    bodyLift_ = -0.30f * lie;
    bodyRot_.z = kPi * roll + 0.12f * std::sin(t_ * 9.0f) * roll;
    // 四脚朝天时蹬腿。
    const float kick = roll;
    for (int i = 0; i < 4; ++i) {
        const float ph = t_ * 7.0f + static_cast<float>(i) * 1.6f;
        legRot_[i] = (0.35f * std::sin(ph)) * kick + (i < 2 ? -0.6f : 0.6f) * lie * (1.0f - kick);
    }
    if (roll > 0.9f) { wantViewer_ = true; lidGoal_ = -0.10f; rollGoal_ = 0.0f; }
}

void ActionPlayer::tick_ball(float dt) {
    Ball& b = ball_;
    // 球的物理：重力、地面反弹、滚动摩擦、舞台边缘反弹。
    if (b.visible) {
        b.scale = approach(b.scale, phase_ == 4 ? 0.0f : 1.0f, dt, 0.12f);
        b.vel.y -= 6.0f * dt;
        b.pos = b.pos + b.vel * dt;
        if (b.pos.y < 0.0f) {
            b.pos.y = 0.0f;
            if (b.vel.y < -0.6f) pendingSound_ = snd::Bounce;
            if (b.vel.y < 0.0f) b.vel.y = -b.vel.y * 0.45f;
            if (std::fabs(b.vel.y) < 0.2f) b.vel.y = 0.0f;
        }
        const float ground = b.pos.y <= 0.001f ? 1.0f : 0.2f;
        const float f = std::exp(-dt * 1.4f * ground);
        b.vel.x *= f;
        b.vel.z *= f;
        if (b.pos.x > stageMax_) { b.pos.x = stageMax_; b.vel.x = -std::fabs(b.vel.x) * 0.6f; }
        if (b.pos.x < stageMin_) { b.pos.x = stageMin_; b.vel.x = std::fabs(b.vel.x) * 0.6f; }
        if (b.pos.z > 0.55f) { b.pos.z = 0.55f; b.vel.z = -std::fabs(b.vel.z) * 0.6f; }
        if (b.pos.z < -0.35f) { b.pos.z = -0.35f; b.vel.z = std::fabs(b.vel.z) * 0.6f; }
        b.spinX += b.vel.z * dt / kBallRadius;
        b.spinZ -= b.vel.x * dt / kBallRadius;
    }

    const float speed2 = b.vel.x * b.vel.x + b.vel.z * b.vel.z;
    switch (phase_) {
        case 0:   // 球落地前先看着它
            if (t_ > 0.5f) phase_ = 1;
            break;
        case 1: { // 跑到球后面
            const float dx = b.pos.x - dog_.x, dz = b.pos.z - dog_.z;
            const float d = std::sqrt(dx * dx + dz * dz);
            const float sx = d > 1e-3f ? dx / d : 1.0f, sz = d > 1e-3f ? dz / d : 0.0f;
            // 站位：体心到鼻尖约 1.08，再加球半径，鼻尖才刚好碰到球。
            // 第一版写 0.62，截图里球在狗肚子底下。
            if (walk_toward(b.pos.x - sx * kBallStandoff, b.pos.z - sz * kBallStandoff, 1.1f, dt)) {
                phase_ = 2;
                phaseT_ = t_;
            }
            break;
        }
        case 2: { // 低头拱球
            const float u = (t_ - phaseT_) / 0.45f;
            headPitch_ = -0.6f * std::sin(clampf(u, 0.0f, 1.0f) * kPi);
            bodyLift_ = -0.05f * std::sin(clampf(u, 0.0f, 1.0f) * kPi);
            if (u >= 0.5f && speed2 < 0.01f) {
                const float fx = std::sin(dog_.yaw), fz = std::cos(dog_.yaw);
                const float push = rng_.range(1.6f, 2.4f);
                b.vel = {fx * push + rng_.range(-0.4f, 0.4f), 1.2f, fz * push * 0.5f};
            }
            if (u >= 1.0f) phase_ = 3;
            break;
        }
        case 3:   // 追球。球停了就再拱一次，时间快到就收
            if (t_ > duration_ - 1.2f) { phase_ = 4; }
            else if (speed2 < 0.02f && b.pos.y <= 0.001f) { phase_ = 1; }
            else {
                const float dx = b.pos.x - dog_.x, dz = b.pos.z - dog_.z;
                const float d = std::sqrt(dx * dx + dz * dz);
                if (d > kBallStandoff + 0.1f)
                    walk_toward(b.pos.x - dx / d * kBallStandoff, b.pos.z - dz / d * kBallStandoff, 1.1f, dt);
                else walkAmp_ = approach(walkAmp_, 0.0f, dt, 0.12f);
            }
            break;
        case 4:   // 球淡出，狗转回来
            turn_toward(0.0f, dt, 3.0f);
            walkAmp_ = approach(walkAmp_, 0.0f, dt, 0.12f);
            break;
    }
}

void ActionPlayer::tick_pounce(float dt, const ActionContext& ctx) {
    // 0–0.6 蹲低对准；0.6–1.0 扑出去（带跳跃弧线）；1.0–1.5 落地瞪着；之后跑回原位。
    const float lungeX = clampf(homeX_ + side_ * 0.38f, stageMin_, stageMax_);
    if (t_ < 0.6f) {
        const float e = ramp(t_, 0.0f, 0.35f);
        bodyLift_ = -0.16f * e;
        bodyRot_.x = -0.12f * e;
        legRot_[2] = legRot_[3] = 0.5f * e;
        turn_toward(ctx.cursorInside ? ctx.cursorYaw * 0.6f : dog_.yaw, dt, 6.0f);
    } else if (t_ < 1.0f) {
        const float u = (t_ - 0.6f) / 0.4f;
        if (phase_ == 0) { phase_ = 1; pendingSound_ = snd::Bark; }
        dog_.x = homeX_ + (lungeX - homeX_) * smooth(u);
        bodyLift_ = 0.22f * std::sin(u * kPi);
        bodyRot_.x = 0.25f * std::sin(u * kPi);
        legRot_[0] = legRot_[1] = -0.7f * std::sin(u * kPi);
        legRot_[2] = legRot_[3] = 0.6f * std::sin(u * kPi);
    } else if (t_ < 1.55f) {
        bodyLift_ = approach(bodyLift_, 0.0f, dt, 0.08f);
        bodyRot_.x = approach(bodyRot_.x, 0.0f, dt, 0.08f);
        for (float& r : legRot_) r = approach(r, 0.0f, dt, 0.08f);
    } else {
        if (walk_toward(homeX_, 0.0f, 1.5f, dt)) {
            turn_toward(0.0f, dt, 5.0f);
            if (std::fabs(wrap_angle(dog_.yaw)) < 0.05f) { dog_.yaw = 0.0f; t_ = duration_; }
        }
    }
}

void ActionPlayer::tick_flip_bowl(float dt) {
    Bowl& w = bowl_;
    w.scale = approach(w.scale, phase_ >= 3 && t_ > duration_ - 0.5f ? 0.0f : 1.0f, dt, 0.12f);
    switch (phase_) {
        case 0: { // 走到碗前，鼻尖略压过碗沿，前爪够得着
            float tx, tz;
            approach_point(dog_, w.pos, kNoseReach - 0.25f, tx, tz);
            if (t_ > 0.3f && walk_toward(tx, tz, 0.9f, dt)) { phase_ = 1; phaseT_ = t_; }
            break;
        }
        case 1: { // 抬爪、拍。碗在正前方，用右前爪
            const float u = (t_ - phaseT_) / 0.5f;
            const int paw = 1;
            legRot_[paw] = -1.1f * std::sin(clampf(u, 0.0f, 1.0f) * kPi);
            legRot_[paw == 1 ? 0 : 1] = -0.2f * std::sin(clampf(u, 0.0f, 1.0f) * kPi);
            bodyRot_.x = 0.12f * std::sin(clampf(u, 0.0f, 1.0f) * kPi);
            if (u >= 0.55f) { phase_ = 2; phaseT_ = t_; }
            break;
        }
        case 2: { // 碗翻过去，顺着狗的朝向蹦出去一点
            const float u = clampf((t_ - phaseT_) / 0.35f, 0.0f, 1.0f);
            if (u <= 0.0f + 1e-3f && pendingSound_ < 0) pendingSound_ = snd::Clatter;
            w.tilt = 2.6f * smooth(u);
            w.pos.y = 0.12f * std::sin(u * kPi);
            w.pos.x += std::sin(dog_.yaw) * 0.6f * dt * (1.0f - u);
            w.pos.z += std::cos(dog_.yaw) * 0.6f * dt * (1.0f - u);
            if (u >= 1.0f) phase_ = 3;
            break;
        }
        case 3:   // 转过来看你。眼睑半合，一副「怎么了」的样子
            turn_toward(0.0f, dt, 3.0f);
            wantViewer_ = true;
            lidGoal_ = 0.35f;
            rollGoal_ = -side_ * 0.15f;
            break;
    }
    // 碗不许出画。
    w.pos.x = clampf(w.pos.x, stageMin_, stageMax_);
    w.pos.z = clampf(w.pos.z, -0.3f, 0.35f);
}

void ActionPlayer::tick_remind_water(float dt) {
    // 喝水提醒（作者定的顺序）：先轻轻叫（start 时已发声）→ 字幕（宿主 1 秒后）→ 尿尿的动作和声音。
    // 0–1.0 抬头看你；然后走到一旁、侧过身；抬左后腿；尿 2.6 秒；放腿、回头看你。
    Pee& p = pee_;
    if (t_ < 1.0f) {
        wantViewer_ = true;
        lidGoal_ = -0.05f;
    } else if (phase_ == 0) {
        if (walk_toward(targetX_, 0.15f, 0.8f, dt)) { phase_ = 1; phaseT_ = t_; }
    } else if (phase_ == 1) {
        // 侧过身，屁股对着一边，观众看得见抬腿。
        turn_toward(kPi * 0.5f * (targetX_ >= 0.0f ? 1.0f : -1.0f), dt, 4.0f);
        const float u = clampf((t_ - phaseT_) / 0.7f, 0.0f, 1.0f);
        if (u > 0.3f) {
            // 左后腿向外抬起：绕 z 负角把脚甩向 -x。身子往另一边压一点保持平衡。
            const float lift = smooth((u - 0.3f) / 0.7f);
            poses_leg_lift_ = -1.25f * lift;
            bodyRot_.z = 0.10f * lift;
        }
        if (u >= 1.0f) {
            phase_ = 2;
            phaseT_ = t_;
            pendingSound_ = snd::Pee;
            // 一滩落在抬腿那一侧的地上。
            const float sx = std::cos(dog_.yaw), sz = -std::sin(dog_.yaw);   // 狗的左侧方向
            p.puddlePos = {dog_.x - sx * 0.42f, 0.0f, dog_.z - sz * 0.42f - 0.34f * std::cos(dog_.yaw)};
        }
    } else if (phase_ == 2) {
        poses_leg_lift_ = -1.25f;
        bodyRot_.z = 0.10f;
        const float u = clampf((t_ - phaseT_) / 2.6f, 0.0f, 1.0f);
        const float flow = std::sin(u * kPi);
        p.stream = flow > 0.15f ? 1.0f : flow / 0.15f;
        p.puddle = smooth(u);
        headPitch_ = 0.1f;
        lidGoal_ = 0.4f;   // 舒服地眯着
        if (u >= 1.0f) { phase_ = 3; phaseT_ = t_; }
    } else {
        const float u = clampf((t_ - phaseT_) / 0.5f, 0.0f, 1.0f);
        poses_leg_lift_ = -1.25f * (1.0f - smooth(u));
        bodyRot_.z = 0.10f * (1.0f - u);
        p.stream = 0.0f;
        p.puddle = 1.0f;
        if (u >= 1.0f) {
            turn_toward(0.0f, dt, 4.0f);
            wantViewer_ = true;
            lidGoal_ = -0.1f;
            rollGoal_ = 0.2f;
        }
    }
}

void ActionPlayer::tick_remind_stand(float dt) {
    // 久坐提醒（作者定的顺序）：大跳 → 字幕（宿主 1.2 秒后）→ 把球踢出屏幕，提醒起来运动。
    // 0–1.1 蹲下、大跳、落地；1.4 球出现在前方；跑过去，前腿一扫把球踢向相机外；之后看你。
    Ball& b = ball_;
    if (t_ < 1.1f) {
        if (t_ < 0.25f) {
            const float e = t_ / 0.25f;
            bodyLift_ = -0.14f * e;
            legRot_[2] = legRot_[3] = 0.5f * e;
        } else if (t_ < 0.85f) {
            const float u = (t_ - 0.25f) / 0.6f;
            bodyLift_ = 0.72f * std::sin(u * kPi);
            bodyRot_.x = 0.30f * std::sin(u * kPi);
            legRot_[0] = legRot_[1] = -0.9f * std::sin(u * kPi);
            legRot_[2] = legRot_[3] = 0.8f * std::sin(u * kPi);
            earFlap_ = 0.5f * std::sin(u * kPi);
        } else {
            const float u = (t_ - 0.85f) / 0.25f;
            bodyLift_ = -0.10f * std::sin(u * kPi);
            if (phase_ == 0) { phase_ = 1; pendingSound_ = snd::Bounce; }
        }
        return;
    }
    if (phase_ == 1) {
        if (t_ >= 1.4f) {
            phase_ = 2;
            b.visible = true;
            b.scale = 0.0f;
            b.pos = {clampf(dog_.x + (dog_.x > 0 ? -0.6f : 0.6f), stageMin_ + 0.3f, stageMax_ - 0.3f), 0.5f, 0.05f};
            b.vel = {0, 0, 0};
        }
        return;
    }
    // 球的物理（与玩球相同，但踢飞后不撞墙、不减速）。
    if (b.visible) {
        b.scale = approach(b.scale, phase_ >= 5 ? 0.0f : 1.0f, dt, phase_ >= 5 ? 0.25f : 0.10f);
        b.vel.y -= 6.0f * dt;
        b.pos = b.pos + b.vel * dt;
        if (phase_ < 4 && b.pos.y < 0.0f) {
            b.pos.y = 0.0f;
            if (b.vel.y < -0.6f) pendingSound_ = snd::Bounce;
            if (b.vel.y < 0.0f) b.vel.y = -b.vel.y * 0.45f;
            if (std::fabs(b.vel.y) < 0.2f) b.vel.y = 0.0f;
        }
        b.spinX += b.vel.z * dt / kBallRadius;
        b.spinZ -= b.vel.x * dt / kBallRadius;
    }
    switch (phase_) {
        case 2: { // 跑到球的一侧，让踢的方向朝屏幕较远的那条边
            side_ = (stageMax_ - b.pos.x) > (b.pos.x - stageMin_) ? 1.0f : -1.0f;   // 往哪边踢
            float tx, tz;
            const Vec3 behind{b.pos.x - side_ * 0.95f, 0.0f, b.pos.z};
            approach_point(dog_, behind, 0.0f, tx, tz);
            if (walk_toward(tx, tz, 1.3f, dt)) { phase_ = 3; phaseT_ = t_; }
            break;
        }
        case 3: { // 转向球，前腿一扫
            turn_toward(side_ * kPi * 0.5f, dt, 8.0f);
            const float u = clampf((t_ - phaseT_) / 0.35f, 0.0f, 1.0f);
            legRot_[1] = -1.4f * std::sin(u * kPi);
            bodyRot_.x = 0.15f * std::sin(u * kPi);
            if (u >= 0.45f) {
                phase_ = 4;
                phaseT_ = t_;
                pendingSound_ = snd::Kick;
                // 沿舞台横向飞出去，带上抛和一点朝相机的分量。窗口是整个屏幕，
                // 球会一路滚到屏幕边缘之外——这就是「踢出屏幕」。
                b.vel = {side_ * 9.0f, 2.4f, 0.8f};
            }
            break;
        }
        case 4:   // 球飞出去，狗扭头目送
            legRot_[1] = approach(legRot_[1], 0.0f, dt, 0.1f);
            bodyRot_.x = approach(bodyRot_.x, 0.0f, dt, 0.1f);
            headPitch_ = approach(headPitch_, 0.2f, dt, 0.15f);
            if (b.pos.x > stageMax_ + 1.5f || b.pos.x < stageMin_ - 1.5f || t_ - phaseT_ > 1.6f) { phase_ = 5; phaseT_ = t_; }
            break;
        case 5:   // 看你
            turn_toward(0.0f, dt, 4.0f);
            headPitch_ = approach(headPitch_, 0.0f, dt, 0.2f);
            wantViewer_ = true;
            lidGoal_ = -0.1f;
            rollGoal_ = 0.18f;
            break;
        default: break;
    }
}

float ActionPlayer::rand_stage_x(float minDist) {
    // 在舞台范围内、离当前位置至少 minDist、至多 3 个单位的地方挑一点。
    const float lo = (std::max)(stageMin_, dog_.x - 3.0f);
    const float hi = (std::min)(stageMax_, dog_.x + 3.0f);
    if (hi - lo < minDist * 2.0f) return clampf(dog_.x + (dog_.x > (lo + hi) * 0.5f ? -minDist : minDist), stageMin_, stageMax_);
    float tx;
    int guard = 0;
    do { tx = rng_.range(lo, hi); } while (std::fabs(tx - dog_.x) < minDist && ++guard < 20);
    return tx;
}

void ActionPlayer::tick_charge(float dt) {
    // 0–0.5 转向相机、蹲；0.5–1.3 朝相机蹿（z 到 +4.5，弧线跳到 1.6 高），顶点叫一声；
    // 1.3–2.2 落回原地；之后抖一下、看你。相机远，所以要蹿得远才显得大。
    const float faceYaw = std::atan2(viewDirX_, viewDirZ_);   // 正对相机
    if (t_ < 0.5f) {
        turn_toward(faceYaw, dt, 8.0f);
        const float e = ramp(t_, 0.0f, 0.4f);
        bodyLift_ = -0.15f * e;
        bodyRot_.x = -0.12f * e;
        legRot_[2] = legRot_[3] = 0.55f * e;
        wantViewer_ = true;
        lidGoal_ = -0.15f;
    } else if (t_ < 2.2f) {
        const float u = (t_ - 0.5f) / 1.7f;          // 0→1 整个来回
        const float out = std::sin(u * kPi);           // 0→1→0
        // 沿朝观众的方向蹿出 5 个单位：相机远（约 11），这样能放大到 1.8 倍左右。
        dog_.x = homeX_ + viewDirX_ * 5.0f * out;
        dog_.z = homeZ_ + viewDirZ_ * 5.0f * out;
        bodyLift_ = 0.9f * out - 0.05f * (1.0f - out);
        bodyRot_.x = 0.35f * std::sin(u * 2.0f * kPi);  // 去时仰、回时俯
        legRot_[0] = legRot_[1] = -0.8f * out;
        legRot_[2] = legRot_[3] = 0.7f * out;
        earFlap_ = 0.5f * out;
        wantViewer_ = true;
        lidGoal_ = -0.2f;
        if (phase_ == 0 && u > 0.45f) { phase_ = 1; pendingSound_ = snd::Bark; }
    } else {
        dog_.x = homeX_;
        dog_.z = homeZ_;
        turn_toward(0.0f, dt, 6.0f);
        const float u = t_ - 2.2f;
        const float e = envelope(u, 0.8f, 0.1f, 0.2f);
        bodyLift_ = 0.0f;
        bodyRot_.x = 0.0f;
        for (float& r : legRot_) r = 0.0f;
    for (float& sc : legScaleY_) sc = 1.0f;
        bodyRot_.z = 0.2f * std::sin(u * 42.0f) * e;
        earFlap_ = 0.5f * e;
        wantViewer_ = true;
        lidGoal_ = -0.05f;
        rollGoal_ = 0.2f;
    }
}

void ActionPlayer::tick_remind_memo(float dt) {
    // 跑到中间 → 坐下 → 叫两声（1.0 秒、1.5 秒）→ 看你直到结束。字幕由宿主在 1.6 秒显示。
    if (phase_ == 0) {
        if (walk_toward(0.0f, 0.1f, 1.2f, dt) || t_ > 1.0f) { phase_ = 1; phaseT_ = t_; }
    } else {
        turn_toward(0.0f, dt, 5.0f);
        const float e = ramp(t_ - phaseT_, 0.0f, 0.5f) * (1.0f - ramp(t_, duration_ - 0.5f, duration_));
        legRot_[2] = legRot_[3] = kSitHindFold * e;
        legScaleY_[2] = legScaleY_[3] = 1.0f - 0.45f * e;
        legRot_[0] = legRot_[1] = -kSitBodyPitch * e;
        bodyRot_.x = kSitBodyPitch * e;
        bodyLift_ = kSitBodyLift * e;
        wantViewer_ = true;
        lidGoal_ = -0.1f;
        const float u = t_ - phaseT_;
        if (phase_ == 1 && u > 0.4f) { phase_ = 2; pendingSound_ = snd::Bark; }
        if (phase_ == 2 && u > 0.9f) { phase_ = 3; pendingSound_ = snd::Bark; }
        // 叫的时候头往上扬一下。
        headPitch_ = 0.25f * (std::fabs(std::sin(clampf((u - 0.4f) / 0.5f, 0.0f, 1.0f) * kPi)) +
                              std::fabs(std::sin(clampf((u - 0.9f) / 0.5f, 0.0f, 1.0f) * kPi)));
        rollGoal_ = 0.15f * std::sin(u * 1.2f);
    }
}

void ActionPlayer::tick_sleep() {
    // 趴下：身子降到地面、四腿折起、头放低、闭眼。呼吸起伏。偶尔打呼。
    const float lie = ramp(t_, 0.0f, 0.9f) * (1.0f - ramp(t_, duration_ - 0.8f, duration_));
    // 1.7 改法：身子降到贴地，前腿向前平伸放在地上，后腿缩短塞到身子底下。
    // 以前四条腿都转 75°，腿从身子底下横着伸出来，看起来像腿和身子分开了（作者反馈）。
    bodyLift_ = -0.36f * lie;
    legRot_[0] = legRot_[1] = -1.5f * lie;
    legRot_[2] = legRot_[3] = 0.0f;
    legScaleY_[0] = legScaleY_[1] = 1.0f;
    legScaleY_[2] = legScaleY_[3] = 1.0f - 0.78f * lie;
    headPitch_ = -0.42f * lie;
    headRoll_ = 0.12f * lie;
    bodyScaleY_ = 1.0f + 0.025f * lie * std::sin(t_ * 2.4f);
    wantViewer_ = lie > 0.5f;
    lidGoal_ = lie;
    rollGoal_ = 0.0f;
    if (lie > 0.9f) {
        snoreTimer_ -= 0.016f;
        if (snoreTimer_ <= 0.0f) { pendingSound_ = snd::Snore; snoreTimer_ = rng_.range(3.0f, 6.0f); }
    }
}

// ---- 输出 ----

void ActionPlayer::apply(PartPose* poses) const {
    const auto P = [&](Part id) -> PartPose& { return poses[static_cast<int>(id)]; };

    // 走路：对角腿同相，身体上下起伏。
    const float sw = walkAmp_ * 0.55f;
    const float s = std::sin(walkPhase_);
    const float bob = walkAmp_ * 0.025f * std::fabs(std::sin(walkPhase_ * 2.0f));

    P(Part::Body).offset = {dog_.x, bodyLift_ + bob, dog_.z};
    P(Part::Body).rotation = {bodyRot_.x, dog_.yaw + bodyRot_.y, bodyRot_.z};
    P(Part::Body).scale = {1.0f, bodyScaleY_, 1.0f};

    P(Part::LegFL).rotation.x = legRot_[0] + sw * s;
    P(Part::LegBR).rotation.x = legRot_[3] + sw * s;
    P(Part::LegFR).rotation.x = legRot_[1] - sw * s;
    P(Part::LegBL).rotation.x = legRot_[2] - sw * s;
    P(Part::LegBL).rotation.z = poses_leg_lift_;   // 尿尿抬腿
    P(Part::LegFL).scale.y = legScaleY_[0];
    P(Part::LegFR).scale.y = legScaleY_[1];
    P(Part::LegBL).scale.y = legScaleY_[2];
    P(Part::LegBR).scale.y = legScaleY_[3];

    // 尿尿的水流跟着抬起的腿走（同一枢轴），一滩留在地上。
    P(Part::Stream).rotation.z = poses_leg_lift_ * 0.6f;
    P(Part::Stream).scale = {1.0f, pee_.stream, 1.0f};
    P(Part::Puddle).offset = pee_.puddlePos;
    P(Part::Puddle).scale = {pee_.puddle, 1.0f, pee_.puddle};

    P(Part::Head).rotation = {headPitch_, 0.0f, headRoll_};
    // 抖身子时耳朵向两侧张开；挨打时（负值）向后贴。
    if (earFlap_ >= 0.0f) {
        P(Part::EarL).rotation.z = -earFlap_ * 0.6f;
        P(Part::EarR).rotation.z = +earFlap_ * 0.6f;
    } else {
        P(Part::EarL).rotation.x = P(Part::EarR).rotation.x = earFlap_ * 0.9f;
    }
    // 走路时耳朵随步伐前后甩一点。
    P(Part::EarL).rotation.x = P(Part::EarL).rotation.x - walkAmp_ * 0.18f * s;
    P(Part::EarR).rotation.x = P(Part::EarR).rotation.x - walkAmp_ * 0.18f * s;

    P(Part::Ball).offset = ball_.pos;
    P(Part::Ball).scale = {ball_.scale, ball_.scale, ball_.scale};
    P(Part::Ball).rotation = {ball_.spinX, 0.0f, ball_.spinZ};

    P(Part::Bowl).offset = bowl_.pos;
    P(Part::Bowl).scale = {bowl_.scale, bowl_.scale, bowl_.scale};
    P(Part::Bowl).rotation = {0.0f, 0.0f, bowl_.tilt};

    // 接触阴影：跟着体心在地面上的投影走；离地越高越小（透视上更远）。
    // 淡不掉的原因是 alpha 在部件里定死，用缩小代替变淡，效果够用。
    {
        const float h = (std::max)(0.0f, bodyLift_ + bob);
        const float k = 1.0f / (1.0f + h * 1.2f);
        P(Part::ShadowDog).offset = {dog_.x, 0.0f, dog_.z};
        P(Part::ShadowDog).rotation.y = dog_.yaw;
        P(Part::ShadowDog).scale = {k, 1.0f, k};
        const float bh = (std::max)(0.0f, ball_.pos.y);
        const float bk = ball_.scale / (1.0f + bh * 1.5f);
        P(Part::ShadowBall).offset = {ball_.pos.x, 0.0f, ball_.pos.z};
        P(Part::ShadowBall).scale = {bk, 1.0f, bk};
    }
}

void ActionPlayer::gaze_override(GazeInput& in) const {
    if (wantViewer_) {
        in.wantViewer = true;
        in.rollGoal = rollGoal_;
        in.lidGoal = lidGoal_;
    }
    // 玩球、踢球和拍碗时盯着道具：把它当目标，转换成模型空间的偏航。
    if ((kind_ == ActionKind::PlayBall && ball_.visible) ||
        (kind_ == ActionKind::RemindStand && ball_.visible && phase_ < 5) ||
        (kind_ == ActionKind::FlipBowl && phase_ < 3)) {
        const Vec3 tgt = kind_ == ActionKind::PlayBall ? ball_.pos : bowl_.pos;
        const float dx = tgt.x - dog_.x, dz = tgt.z - dog_.z;
        in.hasTarget = true;
        in.targetAppeared = false;
        in.targetYaw = clampf(wrap_angle(std::atan2(dx, dz) - dog_.yaw), -1.0f, 1.0f);
        in.targetPitch = -0.35f;
    }
}

}  // namespace pet
