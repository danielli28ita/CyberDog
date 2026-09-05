// 亲密度。养成（P3）的第一个属性，按作者要求提前到 1.0。
//
// 作者定的规则：
//   增加很容易，减少很难。摸头加、戳一下加、玩球加；打减，但减得少且有上限。
//   没有互动时随时间慢慢衰减。
// 设计文档 M4 的约束：有软下限，低谷可恢复，不存在不可恢复的惩罚。
//
// 纯 C++，没有时钟，dt 由调用方喂。

#pragma once

namespace pet {

enum class BondEvent {
    Pet,        // 抚摸，按持续时间计，每秒一次
    Poke,       // 点一下
    Hit,        // 打
    PlayedBall, // 一起玩过球（动作做完）
    Reminded,   // 健康提醒被理会（P5 面板做了之后再接）
};

class Bond {
public:
    // 初值 30：新领回来的狗，不熟也不怕。
    explicit Bond(float affinity = 30.0f) : affinity_(affinity) {}

    void apply(BondEvent e, float amount = 1.0f);

    // 衰减：一小时不互动掉 1 点，最低不低于软下限。dt 秒。
    void decay(float dt);

    float affinity() const { return affinity_; }          // 0–100
    float affinity01() const { return affinity_ / 100.0f; }
    void  set_affinity(float v);

    // 「乖」的剩余秒数。摸过或打过之后一段时间不捣蛋。
    float obedient_seconds() const { return obedient_; }
    bool  obedient() const { return obedient_ > 0.0f; }

    // 最近一分钟内挨打的次数，用来限制「打」的扣分。
    unsigned hits_last_minute() const { return hitsRecent_; }

    static constexpr float kFloor = 10.0f;   // 软下限。再怎么打也不会变成仇人
    static constexpr float kPetPerMinute = 3.0f;   // 抚摸一分钟内最多涨这么多（2.0）
    static constexpr float kMax   = 100.0f;

private:
    float    affinity_;
    float    obedient_ = 0.0f;
    float    sinceInteraction_ = 0.0f;
    float    hitWindow_ = 0.0f;
    float    petWindow_ = 0.0f;   // 抚摸计数窗口剩余秒数
    float    petGained_ = 0.0f;   // 这个窗口里已经涨了多少
    unsigned hitsRecent_ = 0;
};

}  // namespace pet
