// 比格犬代理体。设计文档 §2.5 构建路径的第 0 步：
// 「P1 与 P2 用代理体，胶囊加长方体拼一条狗的比例，带完整骨架。工程不等美术。」
//
// 纯几何生成，不依赖平台和引擎，所以放在 core/。
// 有了 Blender 出的 glTF 之后这个文件不删——它继续当资产加载失败时的兜底，
// 也当渲染管线的回归用例。
//
// 「骨架」在代理体阶段是部件树：每个部件是一段索引区间，带父部件和枢轴点，
// 每帧给每个部件一个局部变换，按树算出世界矩阵。
// 真模型换成蒙皮之后，部件名对应骨骼名，眼神与动作的代码不用改。

#pragma once

#include <cstdint>
#include <vector>

#include "core/math3d.h"

namespace pet {

struct MeshVertex {
    Vec3 position;
    Vec3 normal;
    Vec3 color;
};

// 代理体的部件。顺序固定，Pose 数组按这个下标索引。
// 父部件必须排在子部件前面，compute_part_world 按顺序算。
enum class Part : int {
    Body = 0,   // 躯干、胸口、背鞍、脖子。整条狗的根：它的 offset/rotation 就是狗在舞台上的位置和朝向
    Head,       // 头、方吻、鼻头。枢轴在颈根
    EarL,       // 左垂耳，枢轴在耳根
    EarR,
    EyeL,       // 眼白。枢轴在眼球中心
    EyeR,
    PupilL,     // 虹膜，父部件是眼白。平移即「看向哪里」
    PupilR,
    GlintL,     // 高光点，父部件是虹膜，跟着瞳孔走。眼睛「活」主要靠它
    GlintR,
    LidL,       // 上眼睑。枢轴在眼睑上缘，y 方向缩放即闭合程度，绕 z 旋转即挑眉
    LidR,
    Tail,       // 枢轴在尾根
    LegFL,      // 四条腿，枢轴在腿根。绕 x 转就是迈步
    LegFR,
    LegBL,
    LegBR,
    Ball,       // 道具：球。没有父部件，在舞台空间。不用时缩放到 0
    Bowl,       // 道具：水碗。同上
    Stream,     // 道具：尿尿的水流，父部件是躯干（跟着狗走）。不用时缩放到 0
    Puddle,     // 道具：地上的一滩。舞台空间
    ShadowDog,  // 地面接触阴影：狗脚下的半透明椭圆。跳起来时缩小变淡。最强的深度提示
    ShadowBall, // 球的阴影
    Heart0,     // 好感度上涨时冒的心，四颗，舞台空间，半透明。不用时缩放 0
    Heart1,
    Heart2,
    Heart3,
    Mat,        // 垫子：睡觉或被拖到半空时出现在脚下（2.0）
    Count
};

struct MeshPart {
    Part          id;
    Part          parent;       // Part::Count 表示没有父部件
    std::uint32_t firstIndex;
    std::uint32_t indexCount;
    Vec3          pivot;        // 局部变换绕这一点做，模型空间坐标
    float         alpha = 1.0f; // <1 走半透明管线（不受光、不写深度）
};

struct Mesh {
    std::vector<MeshVertex>    vertices;
    std::vector<std::uint32_t> indices;
    std::vector<MeshPart>      parts;   // 按 Part 枚举顺序，共 Part::Count 个
};

// 部件的局部姿态：绕枢轴先缩放、再旋转（x→y→z 顺序），最后平移。
struct PartPose {
    Vec3 rotation{0, 0, 0};   // 弧度。符号约定见 math3d.h 的 rotate_* 注释
    Vec3 scale{1, 1, 1};
    Vec3 offset{0, 0, 0};     // 平移，在父部件的局部坐标里
};

// 部件局部矩阵：T(-pivot) · S · Rx · Ry · Rz · T(pivot + offset)
Mat4 part_local_matrix(const MeshPart& part, const PartPose& pose);

// 按部件树算出每个部件的模型空间矩阵。poses 与 out 的长度都是 Part::Count。
void compute_part_world(const Mesh& mesh, const PartPose* poses, Mat4* out);

// 生成一条比格犬的代理体，外加球和水碗两个道具。
//
// 比例按真实比格：肩高与体长约 1:1.4，腿短，垂耳长到下颌以下，尾巴上翘。
// 配色按三色比格：黑色背鞍、棕色头部、白色四肢与胸口、白色尾尖。
// 眼睛：眼白、深棕虹膜、白色高光点、与头同色的上眼睑。
//
// 朝向：+Z 是狗头方向，+Y 向上。原点在四脚着地的地面中心。
Mesh build_proxy_beagle();

// 瞳孔在眼白内可平移的最大距离（模型单位），x 与 y 方向。
inline constexpr float kPupilTravelX = 0.020f;
inline constexpr float kPupilTravelY = 0.014f;

// 眼睑完全睁开时保留的缩放（留一条眉线），完全闭合时为 1。
inline constexpr float kLidOpenScale = 0.10f;   // 睁眼时留的眉线。2.0 从 0.14 调小（作者要求眉毛小一点）

// 道具尺寸，动作系统算碰撞和落点用。
inline constexpr float kBallRadius = 0.10f;
inline constexpr float kBowlRadius = 0.16f;
inline constexpr float kBowlHeight = 0.09f;

// 腿根高度（腿的枢轴 y），坐下和翻身时算体态用。
inline constexpr float kLegTop = 0.42f;

// 阴影不透明度与半径（狗、球）。
inline constexpr float kShadowAlpha = 0.32f;
inline constexpr float kShadowDogRx = 0.34f, kShadowDogRz = 0.62f;
inline constexpr float kShadowLift = 0.10f;

}  // namespace pet
