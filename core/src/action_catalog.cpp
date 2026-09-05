#include "core/action_catalog.h"

namespace pet {
namespace {

// 列顺序见 action_catalog.h 的 ActionSpec。
//                 id                 名字            选  时长  性格权重{外向 黏人 好奇 懒散 胆小 活泼 捣蛋 卖萌}   需求权重{精力 社交 好奇 无聊}  常数   光标在  光标不在 需光标 乖时禁  亲密{基 增}   代价{精力 社交 好奇 无聊}
constexpr ActionSpec kCatalog[] = {
    {ActionKind::Idle,        "发呆",         true,  0.0f, {0,    0,   0,    0.45f, 0, 0,     0,    0    }, { 0,     0,    0,    0    }, 0.45f, 0,     0,      false, false, {1, 0},     { 0,      0,     0,     0    }},
    {ActionKind::Walk,        "闲逛",         true,  8.0f, {0.25f,0,   0.45f,0,     0, 0,     0,    0    }, { 0,     0,    0.3f, 0.6f }, 0,     0,     0,      false, false, {1, 0},     {-0.08f,  0,    -0.2f, -0.15f}},
    {ActionKind::Stretch,     "伸懒腰",       true,  2.2f, {0,    0,   0,    0.35f, 0, 0,     0,    0    }, {-0.4f,  0,    0,    0    }, 0.4f,  0,     0,      false, false, {1, 0},     { 0.15f,  0,     0,     0    }},
    {ActionKind::Shake,       "抖身子",       true,  0.9f, {0,    0,   0,    0,     0, 0.3f,  0,    0    }, { 0,     0,    0,    0    }, 0.15f, 0,     0,      false, false, {1, 0},     { 0,      0,     0,     0    }},
    {ActionKind::Sit,         "坐下",         true,  0.0f, {0,    0,   0,    0.5f,  0, 0,     0,    0    }, {-0.5f,  0,    0,    0    }, 0.5f,  0,     0,      false, false, {1, 0},     { 0.15f,  0,     0,     0    }},
    {ActionKind::CharmTilt,   "卖萌·歪头",    true,  3.4f, {0,    0,   0,    0,     0, 0,     0,    0.85f}, { 0,     0.6f, 0,    0    }, 0,     0.35f, 0,      false, false, {0.7f, 0.6f},{-0.05f, -0.45f, 0,     0    }},
    {ActionKind::CharmPaw,    "卖萌·扒手",    true,  3.0f, {0,    0,   0,    0,     0, 0,     0,    0.8f }, { 0,     0.6f, 0,    0    }, 0,     0.4f, -0.1f,   false, false, {0.7f, 0.6f},{-0.05f, -0.45f, 0,     0    }},
    // 翻肚皮是信任的表现，亲密度低于 40 基本不做。
    {ActionKind::CharmBelly,  "卖萌·翻肚皮",  true,  4.6f, {0,    0,   0,    0,     0, 0,     0,    0.75f}, { 0,     0.5f, 0,    0    }, 0,     0.2f, -0.2f,   false, false, {0.2f, 1.2f},{-0.05f, -0.45f, 0,     0    }},
    {ActionKind::PlayBall,    "玩球",         true,  9.0f, {0,    0,   0.35f,0,     0, 0.55f, 0,    0    }, { 0.2f,  0,    0,    0.6f }, 0,     0,     0,      false, false, {1, 0},     {-0.3f,   0,     0,    -0.5f }},
    {ActionKind::Pounce,      "捣蛋·扑光标",  true,  2.4f, {0,    0,   0,    0,     0, 0.35f, 1.0f, 0    }, { 0,     0,    0,    0.3f }, 0,     0,     0,      true,  true,  {1, 0},     {-0.2f,  -0.1f,  0,    -0.35f}},
    {ActionKind::FlipBowl,    "捣蛋·打翻水碗", true,  4.0f, {0,    0,   0,    0,     0, 0,     0.9f, 0    }, { 0,     0,    0,    0.4f }, 0,     0,     0,      false, true,  {1, 0},     { 0,     -0.15f, 0,    -0.4f }},
    {ActionKind::RemindWater, "提醒·尿尿",    false, 6.5f, {0,0,0,0,0,0,0,0}, {0,0,0,0}, 0, 0, 0, false, false, {1, 0}, {0, 0, 0, 0}},
    {ActionKind::RemindStand, "提醒·跳起踢球", false, 7.5f, {0,0,0,0,0,0,0,0}, {0,0,0,0}, 0, 0, 0, false, false, {1, 0}, {-0.1f, 0, 0, -0.2f}},
    {ActionKind::Entrance,    "出场",         false, 3.2f, {0,0,0,0,0,0,0,0}, {0,0,0,0}, 0, 0, 0, false, false, {1, 0}, {0, 0, 0, 0}},
    {ActionKind::Petted,      "被摸",         false, 0.8f, {0,0,0,0,0,0,0,0}, {0,0,0,0}, 0, 0, 0, false, false, {1, 0}, {0, -0.2f, 0, 0}},
    {ActionKind::Cower,       "挨打",         false, 2.2f, {0,0,0,0,0,0,0,0}, {0,0,0,0}, 0, 0, 0, false, false, {1, 0}, {0, 0, 0, 0}},
    {ActionKind::Poked,       "被戳",         false, 0.5f, {0,0,0,0,0,0,0,0}, {0,0,0,0}, 0, 0, 0, false, false, {1, 0}, {0, 0, 0, 0}},
    // 睡觉：精力低、没人理的时候。光标在就不睡。宿主在停靠态也会直接让它睡。
    {ActionKind::Sleep,       "睡觉",         true,  0.0f, {0,    0,   0,    0.5f,  0, 0,     0,    0    }, {-0.9f,  0,    0,    0    }, 0.5f, -0.6f,  0.2f,   false, false, {1, 0},     { 0.5f,   0,     0,     0.1f }},
    {ActionKind::RemindMemo,  "提醒·备忘",    false, 6.0f, {0,0,0,0,0,0,0,0}, {0,0,0,0}, 0, 0, 0, false, false, {1, 0}, {0, 0, 0, 0}},
    // 冲屏：活泼和捣蛋的狗爱干，无聊时更爱干。有人在看（光标在）时加分。
    {ActionKind::Charge,      "冲屏",         true,  3.0f, {0,    0,   0,    0,     0, 0.45f, 0.5f, 0    }, { 0.2f,  0,    0,    0.45f}, 0,     0.3f,  -0.15f, false, false, {1, 0},     {-0.25f,  0,     0,    -0.3f }},
    {ActionKind::ReturnHome,  "回巢",         false, 0.0f, {0,0,0,0,0,0,0,0}, {0,0,0,0}, 0, 0, 0, false, false, {1, 0}, {0, 0, 0, 0}},
};
static_assert(sizeof(kCatalog) / sizeof(kCatalog[0]) == static_cast<size_t>(ActionKind::Count),
              "动作目录表与 ActionKind 数量不一致");

}  // namespace

bool action_roams(ActionKind k) {
    switch (k) {
        case ActionKind::Walk:
        case ActionKind::PlayBall:
        case ActionKind::Pounce:
        case ActionKind::FlipBowl:
        case ActionKind::Charge:
            return true;
        default:
            return false;
    }
}

const ActionSpec& action_spec(ActionKind k) {
    return kCatalog[static_cast<int>(k)];
}

}  // namespace pet
