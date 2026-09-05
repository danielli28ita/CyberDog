// 动作目录表。1.1 的模块化改造：每个动作的「是什么、什么时候选、选了花什么」都在一行里。
//
// 加一个动作要改四处，都在 core 里：
//   1. action.h 的 ActionKind 加一个名字（加在 Count 之前）
//   2. action_catalog.cpp 的表加一行（打分权重、代价、时长）
//   3. action.cpp 写一个 tick_xxx()，在 update() 的 switch 里挂上
//   4. 需要新道具的话在 proxy_mesh 加部件
// 打分公式（设计文档 §2.3 L1）：
//   score = 性格·wPersonality + 需求·wNeeds + constant + 光标项 − 近期重复惩罚，再乘亲密度系数
// 「怎么动」仍是代码（tick 函数），因为代理体阶段的动作是参数曲线；P2 换成动画片段后，
// 表里再加一列片段名即可。

#pragma once

#include "core/action.h"

namespace pet {

struct ActionSpec {
    ActionKind  id;
    const char* name;
    bool        selectable;        // 参与 L1 自选。否则只由宿主 / 插件触发
    float       baseDuration;      // 秒。0 表示由 start() 按情况定
    // 打分。性格权重顺序：外向、黏人、好奇、懒散、胆小、活泼、捣蛋、卖萌。
    float wPersonality[8];
    // 需求权重顺序：精力、社交、好奇、无聊。想表达「精力低时想做」就给精力负权重加常数。
    float wNeeds[4];
    float constant;
    float withCursor;              // 光标在窗口里时加这个
    float withoutCursor;           // 不在时加这个
    bool  requiresCursor;          // 光标不在就不可选
    bool  blockedWhenObedient;     // 刚被摸过 / 打过时不可选（「乖一点」）
    float affinity[2];             // 分数乘 (affinity[0] + affinity[1] × 亲密度01)。{1,0} 表示不受影响
    // 做完之后需求怎么变：精力、社交、好奇、无聊。
    float cost[4];
};

const ActionSpec& action_spec(ActionKind k);
// 这个动作会不会把狗带离原地（闲逛、玩球、扑光标、打翻水碗、冲屏）。
// 1.6 起没人互动时不许自选这些：狗待在右下角，不到屏幕中间占地方（作者要求）。
bool action_roams(ActionKind k);

}  // namespace pet
