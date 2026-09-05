// 亲密度。养成（P3）的第一个属性；CyberDog 1.2 升级为等级 + 经验。
//
// 规则（涨易掉难，无消亡）：
//   摸 / 戳 / 玩球 / 提醒理会 → 涨点；打与长时间不互动 → 慢掉。
//   存档键仍是 affinity（连续点数），兼容旧档。
// 设计文档 M4：软下限，低谷可恢复。
//
// 纯 C++，没有时钟，dt 由调用方喂。

#pragma once

namespace pet {

enum class BondEvent {
    Pet,        // 抚摸，按持续时间计，每秒一次
    Poke,       // 点一下
    Hit,        // 打
    PlayedBall, // 一起玩过球（动作做完）
    Reminded,   // 健康提醒被理会
};

class Bond {
public:
    // 初值 30：大约 Lv.3～4，新领回来不熟也不怕。
    explicit Bond(float affinity = 30.0f) : affinity_(affinity) { clamp_points(); }

    void apply(BondEvent e, float amount = 1.0f);

    // 衰减：无互动超过宽限期后缓慢掉点，不低于软下限。dt 秒。
    void decay(float dt);

    float affinity() const { return affinity_; }          // 累计点数 0–kMax
    // 动作打分仍按「约 100 满亲密」标定，避免 1.2 抬高点数上限后整表行为偏移。
    float affinity01() const { return affinity_ > 100.0f ? 1.0f : (affinity_ / 100.0f); }
    void  set_affinity(float v);

    // ---- 1.2 等级经验 ----
    static constexpr int   kMaxLevel = 10;
    // 各等级起点（累计点数）。Lv1=0 … Lv10=160；满级后还可涨到 kMax。
    static constexpr float kLevelThreshold[kMaxLevel] = {
        0.0f, 8.0f, 18.0f, 30.0f, 45.0f, 62.0f, 82.0f, 105.0f, 130.0f, 160.0f};

    int   level() const;                 // 1…kMaxLevel
    float xp_into_level() const;         // 本级已走多少点
    float xp_needed_for_level() const;   // 本级升到下一级要多少（满级 = 到 kMax 的剩余）
    float level_progress01() const;      // 本级进度 [0,1]，属性条用
    int   tier_index() const;            // 0…9，对应 Str::Tier0…

    // 「乖」的剩余秒数。摸过或打过之后一段时间不捣蛋。
    float obedient_seconds() const { return obedient_; }
    bool  obedient() const { return obedient_ > 0.0f; }

    unsigned hits_last_minute() const { return hitsRecent_; }

    static constexpr float kFloor = 8.0f;          // 软下限 ≈ Lv2 起点，打不穿
    static constexpr float kPetPerMinute = 5.0f;   // 抚摸一分钟内最多涨这么多
    static constexpr float kMax = 200.0f;
    static constexpr float kIdleGraceSeconds = 1200.0f; // 20 分钟无互动才开始掉
    static constexpr float kDecayPerHour = 0.5f;        // 掉速（点/小时）
    static constexpr unsigned kHitCapPerMinute = 2;
    static constexpr float kHitPenalty = 0.5f;

private:
    void clamp_points();

    float    affinity_;
    float    obedient_ = 0.0f;
    float    sinceInteraction_ = 0.0f;
    float    hitWindow_ = 0.0f;
    float    petWindow_ = 0.0f;
    float    petGained_ = 0.0f;
    unsigned hitsRecent_ = 0;
};

}  // namespace pet
