// core/personality.h — 性格参数（设计文档 §2.3 的 L0）
//
// 约束：本文件及整个 core 层不得包含 <windows.h>、D3D、UE 的任何头文件。
// 桌宠覆盖层和将来的 UE 项目都链接这一层，所以它必须保持平台与引擎无关。

#pragma once

#include <cstdint>

namespace pet {

// 一个安装对应一个种子，种子展开成下面这组参数。
// 这些不是标签，是直接参与行为权重和时间常数计算的连续值，取值 [0,1]。
//
// 维度与犬性格量表的对应（只借维度，不照抄问卷；见规格 §2.18）：
//   MCPQ-R Extraversion     → extroversion + liveliness
//   MCPQ-R Motivation/Energy→ curiosity + mischief（比格主导）
//   MCPQ-R Amicability      → charm + clinginess
//   MCPQ-R Neuroticism/Fear → timidity
//   MCPQ-R Training Focus   → 本版不落地，留给 CharacterProfile 将来扩展
//   C-BARQ 的 14 因子过细，桌宠决策表用上面压缩映射。
struct Personality {
    float extroversion = 0.5f;  // 外向：主动靠近光标的倾向
    float clinginess   = 0.5f;  // 黏人：交互后停留时长
    float curiosity    = 0.5f;  // 好奇：对前台窗口变化的反应强度
    float laziness     = 0.5f;  // 懒散：回停靠位的空闲阈值倍率
    float timidity     = 0.5f;  // 胆小：鼠标快速移动时的退避距离
    float liveliness   = 0.5f;  // 活泼：动画播放速度倍率
    float mischief     = 0.5f;  // 捣蛋：捣乱行为的选择权重
    float charm        = 0.5f;  // 卖萌：表演性讨好的权重。歪头、翻肚皮、扒手、装可怜
};

// 与 Personality 成员顺序一致：外向、黏人、好奇、懒散、胆小、活泼、捣蛋、卖萌。
inline constexpr int kTraitCount = 8;

// 角色基线：邪恶比格。数值出处见 设计文档 §2.5 的性格基线表。
// 每次安装在基线上加偏移，所以每个人的狗都不一样，但都还是这条比格。
inline constexpr Personality kEvilBeagleBaseline{
    /*extroversion*/ 0.80f,
    /*clinginess  */ 0.60f,
    /*curiosity   */ 0.85f,  // 高，但不压过捣蛋
    /*laziness    */ 0.60f,
    /*timidity    */ 0.20f,
    /*liveliness  */ 0.75f,
    /*mischief    */ 0.90f,  // 主导特征。不取 1.0，留出「不是只会捣蛋」的余地
    /*charm       */ 0.80f,  // 捣完乱来卖萌，这是它讨得了饶的原因
};

// 捣蛋的上下界与「捣蛋高于好奇」的最小差距。
// 这三个数把两条角色不变量落到每个实例上，不是只落在基线上：
//   1. 捣蛋是最高项，但不取满值——「不是完全捣乱」
//   2. 好奇高，但不压过捣蛋
inline constexpr float kMischiefFloor   = 0.70f;
inline constexpr float kMischiefCeiling = 0.95f;
inline constexpr float kTraitGap        = 0.03f;

static_assert(kEvilBeagleBaseline.mischief > kEvilBeagleBaseline.curiosity,
              "捣蛋基线必须高于好奇基线");
static_assert(kMischiefCeiling < 1.0f,
              "捣蛋不许取满值，否则就是完全捣乱");
static_assert(kMischiefFloor < kMischiefCeiling, "上下界写反了");

// 基线之上的随机偏移幅度，±kPersonalitySpread，结果夹到 [0,1]。
// 1.2：0.15 → 0.22，让不同种子在属性面板上更容易分开。
inline constexpr float kPersonalitySpread = 0.22f;

// 角色档案：一个角色 = 基线 + 偏移幅度 + 不变量。
// 换角色（另一种狗、一只猫）就是换一份档案，展开函数不动。1.1 的模块化改造。
struct CharacterProfile {
    const char* name;
    Personality baseline;
    float spread;           // 每个实例在基线上的随机偏移 ±spread
    float mischiefFloor;    // 主导特征的上下界
    float mischiefCeiling;
    float traitGap;         // 捣蛋至少高出好奇多少
};

inline constexpr CharacterProfile kEvilBeagle{
    "邪恶比格", kEvilBeagleBaseline, kPersonalitySpread,
    kMischiefFloor, kMischiefCeiling, kTraitGap,
};

// 由种子确定地展开出性格参数。同一个种子必须永远得到同一组值。
// 展开方式是「基线 + 偏移」，不是纯随机。
Personality personality_from_profile(const CharacterProfile& profile, std::uint64_t seed);

// 默认角色（邪恶比格）的展开。
inline Personality personality_from_seed(std::uint64_t seed) {
    return personality_from_profile(kEvilBeagle, seed);
}

// 性格向量与权重向量的点积，顺序同 Personality 的成员：
// 外向、黏人、好奇、懒散、胆小、活泼、捣蛋、卖萌。动作目录表打分用。
float personality_dot(const Personality& p, const float w[8]);

// 取出最高的两个性格维下标（0…7），同值时按下标小的优先。属性面板摘要用。
void personality_top2(const Personality& p, int* primaryOut, int* secondaryOut);

// 落实邪恶比格两条不变量（捣蛋上下界、好奇不压过捣蛋）。用户改滑条后也要走这里。
void personality_enforce_invariants(Personality& p, const CharacterProfile& profile = kEvilBeagle);

// 存档键 p_extroversion … p_charm。没有这些键时返回 false（用种子展开）。
bool personality_try_load(const class SaveData& save, Personality& out);
void personality_save(class SaveData& save, const Personality& p);

// 闲置多久回停靠位，单位秒。基准值 90 秒，受 laziness 调制。
// 设计文档 §2.2 规定可配区间 30–600，本函数的输出已经夹在该区间内。
float idle_timeout_seconds(const Personality& p, float base_seconds = 90.0f);

// 停靠状态下两次微动作之间的间隔，单位秒，落在 [20,90] 区间。
// t01 是一个 [0,1] 的随机数，由调用方提供，便于测试时固定。
float micro_action_interval_seconds(const Personality& p, float t01);

}  // namespace pet
