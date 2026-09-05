#include "core/proxy_mesh.h"

#include <algorithm>
#include <cmath>
#include <utility>  // std::move

namespace pet {
namespace {

// 三色比格的配色。
constexpr Vec3 kBlack  {0.13f, 0.11f, 0.10f};  // 背鞍
constexpr Vec3 kTan    {0.74f, 0.50f, 0.27f};  // 头、耳、腿根。1.7 调浅（作者要求），比格的棕偏黄
constexpr Vec3 kTanDark{0.60f, 0.38f, 0.19f};  // 眼睑，比头略深，闭眼时看得出轮廓
constexpr Vec3 kWhite  {0.92f, 0.90f, 0.86f};  // 四肢、胸口、尾尖
constexpr Vec3 kNose   {0.09f, 0.08f, 0.08f};  // 鼻头
constexpr Vec3 kSclera {0.96f, 0.95f, 0.93f};  // 眼白
constexpr Vec3 kIris   {0.22f, 0.12f, 0.06f};  // 虹膜，比格是深棕眼
constexpr Vec3 kGlint  {1.00f, 1.00f, 1.00f};  // 高光点
constexpr Vec3 kTennis    {0.80f, 0.90f, 0.22f};  // 网球的荧光黄绿
constexpr Vec3 kTennisSeam{0.97f, 0.97f, 0.92f};  // 白色接缝
constexpr Vec3 kBowlBlue{0.30f, 0.45f, 0.70f}; // 水碗
constexpr Vec3 kPee    {0.93f, 0.85f, 0.35f};  // 尿
constexpr Vec3 kWater  {0.55f, 0.75f, 0.95f};

// 往网格里加一个轴对齐长方体。中心 c，半尺寸 h。
// 每个面单独给顶点，法线才是硬边的，不会被相邻面平均掉。
void add_box(Mesh& mesh, Vec3 c, Vec3 h, Vec3 color) {
    const float x0 = c.x - h.x, x1 = c.x + h.x;
    const float y0 = c.y - h.y, y1 = c.y + h.y;
    const float z0 = c.z - h.z, z1 = c.z + h.z;

    struct Face { Vec3 n; Vec3 v[4]; };
    const Face faces[6] = {
        {{ 0,  0, -1}, {{x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0}}},  // 后
        {{ 0,  0,  1}, {{x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, {x0, y0, z1}}},  // 前
        {{-1,  0,  0}, {{x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, {x0, y0, z0}}},  // 左
        {{ 1,  0,  0}, {{x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}}},  // 右
        {{ 0, -1,  0}, {{x0, y0, z1}, {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}}},  // 下
        {{ 0,  1,  0}, {{x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}}},  // 上
    };

    for (const Face& f : faces) {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (int i = 0; i < 4; ++i) mesh.vertices.push_back({f.v[i], f.n, color});
        mesh.indices.insert(mesh.indices.end(),
                            {base, base + 1, base + 2, base, base + 2, base + 3});
    }
}

// 圆角长方体（2.0，作者要求「边缘圆润一些，直角太多」）。
// 做法：内缩 r 的小长方体和半径 r 的球做闵可夫斯基和，表面按经纬网参数化：
// 方向 n 上的点 = c + sign(n)·(h - r) + r·n。sign 在 0 处跳变，跨过去的四边形正好铺成平面，
// 平面着色下每块面法线都对。r 不能超过最小半尺寸。
void add_rounded_box(Mesh& mesh, Vec3 c, Vec3 h, Vec3 color, float r, int segs, int rings) {
    const float pi = 3.14159265f;
    const float rr = (std::min)({r, h.x, h.y, h.z});
    const Vec3 inner{h.x - rr, h.y - rr, h.z - rr};
    auto sgn = [](float v) { return v < 0.0f ? -1.0f : 1.0f; };
    auto pt = [&](int i, int j) {
        const float th = pi * static_cast<float>(j) / static_cast<float>(rings);
        const float ph = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segs);
        const Vec3 n{std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)};
        return Vec3{c.x + sgn(n.x) * inner.x + rr * n.x,
                    c.y + sgn(n.y) * inner.y + rr * n.y,
                    c.z + sgn(n.z) * inner.z + rr * n.z};
    };
    for (int j = 0; j < rings; ++j) {
        for (int i = 0; i < segs; ++i) {
            const Vec3 v[4] = {pt(i, j), pt(i + 1, j), pt(i + 1, j + 1), pt(i, j + 1)};
            const Vec3 cr = cross(v[1] - v[0], v[3] - v[0]);
            const Vec3 cr2 = cross(v[2] - v[1], v[0] - v[1]);
            const Vec3 nsum{cr.x + cr2.x, cr.y + cr2.y, cr.z + cr2.z};
            const float len = std::sqrt(nsum.x * nsum.x + nsum.y * nsum.y + nsum.z * nsum.z);
            if (len < 1e-9f) continue;   // 极点处退化的四边形
            const Vec3 n{nsum.x / len, nsum.y / len, nsum.z / len};
            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            for (int k = 0; k < 4; ++k) mesh.vertices.push_back({v[k], n, color});
            mesh.indices.insert(mesh.indices.end(),
                                {base, base + 1, base + 2, base, base + 2, base + 3});
        }
    }
}

// 经纬球，平面着色。segs 是经线数，rings 是纬线数。
// 每个四边形独立给顶点，法线取面法线，和长方体一样是硬边风格。
// 隔一圈换一次颜色，球滚起来才看得出在转。
void add_sphere(Mesh& mesh, Vec3 c, float r, Vec3 colorA, Vec3 colorB, int segs, int rings) {
    const float pi = 3.14159265f;
    auto pt = [&](int i, int j) {
        const float th = pi * static_cast<float>(j) / static_cast<float>(rings);
        const float ph = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segs);
        return Vec3{c.x + r * std::sin(th) * std::cos(ph),
                    c.y + r * std::cos(th),
                    c.z + r * std::sin(th) * std::sin(ph)};
    };
    for (int j = 0; j < rings; ++j) {
        for (int i = 0; i < segs; ++i) {
            // 顶点顺序要让 cross(v1-v0, v3-v0) 指向球外，和 add_box 的绕向一致
            // （从法线一侧看是顺时针）。手算过一个点：这个顺序是对的，反过来会被背面剔除。
            const Vec3 v[4] = {pt(i, j), pt(i + 1, j), pt(i + 1, j + 1), pt(i, j + 1)};
            const Vec3 n = normalize(cross(v[1] - v[0], v[3] - v[0]));
            // 网球接缝：一条绕球的波浪线。面片中心到曲线 sin(2φ)·sinθ ≈ 0.55·cosθ 的距离小就是缝。
            const float thc = pi * (static_cast<float>(j) + 0.5f) / static_cast<float>(rings);
            const float phc = 2.0f * pi * (static_cast<float>(i) + 0.5f) / static_cast<float>(segs);
            const float seam = std::sin(2.0f * phc) * std::sin(thc) - 0.55f * std::cos(thc);
            const Vec3 col = std::fabs(seam) < 0.22f ? colorB : colorA;
            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            for (int k = 0; k < 4; ++k) mesh.vertices.push_back({v[k], n, col});
            mesh.indices.insert(mesh.indices.end(),
                                {base, base + 1, base + 2, base, base + 2, base + 3});
        }
    }
}

// 地面上的椭圆片（两面），阴影用。法线朝上；颜色是深灰，靠 alpha 变淡。
void add_disc(Mesh& mesh, Vec3 c, float rx, float rz, Vec3 color, int segs) {
    const float pi = 3.14159265f;
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({c, {0, 1, 0}, color});
    for (int i = 0; i <= segs; ++i) {
        const float a = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segs);
        mesh.vertices.push_back({{c.x + rx * std::cos(a), c.y, c.z + rz * std::sin(a)}, {0, 1, 0}, color});
    }
    for (int i = 0; i < segs; ++i) {
        // 从上往下看顺时针，和 add_box 的上面一致。
        mesh.indices.insert(mesh.indices.end(), {base, base + 2 + static_cast<std::uint32_t>(i), base + 1 + static_cast<std::uint32_t>(i)});
    }
}

// 心形：xy 平面上两个圆加一个朝下的三角，法线 +z（面向相机那一侧）。半透明管线不剔除背面。
void add_heart(Mesh& mesh, float r, Vec3 color) {
    const float pi = 3.14159265f;
    auto disc = [&](float cx, float cy) {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({{cx, cy, 0}, {0, 0, 1}, color});
        const int segs = 14;
        for (int i = 0; i <= segs; ++i) {
            const float a = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segs);
            mesh.vertices.push_back({{cx + r * std::cos(a), cy + r * std::sin(a), 0}, {0, 0, 1}, color});
        }
        for (int i = 0; i < segs; ++i)
            mesh.indices.insert(mesh.indices.end(), {base, base + 1 + static_cast<std::uint32_t>(i), base + 2 + static_cast<std::uint32_t>(i)});
    };
    disc(-r * 0.62f, r * 0.55f);
    disc(r * 0.62f, r * 0.55f);
    const auto b = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({{-r * 1.55f, r * 0.45f, 0}, {0, 0, 1}, color});
    mesh.vertices.push_back({{ r * 1.55f, r * 0.45f, 0}, {0, 0, 1}, color});
    mesh.vertices.push_back({{0, -r * 1.35f, 0}, {0, 0, 1}, color});
    mesh.indices.insert(mesh.indices.end(), {b, b + 1, b + 2});
}

// 记录「从现在开始加的三角形都属于这个部件」，直到下一次 begin。
struct PartBuilder {
    Mesh& mesh;
    void begin(Part id, Part parent, Vec3 pivot, float alpha = 1.0f) {
        end();
        mesh.parts.push_back({id, parent, static_cast<std::uint32_t>(mesh.indices.size()), 0, pivot, alpha});
    }
    void end() {
        if (mesh.parts.empty()) return;
        MeshPart& p = mesh.parts.back();
        p.indexCount = static_cast<std::uint32_t>(mesh.indices.size()) - p.firstIndex;
    }
};

// 眼睛的几何。sx 是 -1（左眼）或 +1（右眼）。
// 头部方块：x ±0.19，y 0.77–1.13，z 0.52–0.92。方吻：x ±0.12，y 0.76–0.96，z 0.84–1.08。
// 眼睛放在头的前脸、方吻上方两侧，眼白略凸出前脸 0.03。
constexpr float kEyeX  = 0.115f;
constexpr float kEyeY  = 1.030f;
constexpr Vec3  kEyeHalf   {0.050f, 0.045f, 0.025f};
constexpr Vec3  kIrisHalf  {0.028f, 0.028f, 0.012f};
constexpr Vec3  kGlintHalf {0.009f, 0.009f, 0.005f};
constexpr Vec3  kLidHalf   {0.056f, 0.050f, 0.020f};

void add_eye(PartBuilder& pb, Mesh& m, float sx) {
    const Vec3 eyeC  {sx * kEyeX, kEyeY, 0.925f};   // z 0.900–0.950
    const Vec3 irisC {sx * kEyeX, kEyeY, 0.952f};   // z 0.940–0.964，嵌在眼白前脸里
    // 高光偏向相机一侧（+x）和上方，位置随瞳孔走。
    const Vec3 glintC{sx * kEyeX + 0.010f, kEyeY + 0.010f, 0.966f};
    // 眼睑枢轴在上缘：缩放 y 就是从上往下盖。z 在高光之前，闭眼时把整只眼盖住。
    const Vec3 lidC  {sx * kEyeX, kEyeY + 0.005f, 0.955f};
    const Vec3 lidPivot{sx * kEyeX, lidC.y + kLidHalf.y, 0.955f};

    const bool left = sx < 0;
    pb.begin(left ? Part::EyeL : Part::EyeR, Part::Head, eyeC);
    add_box(m, eyeC, kEyeHalf, kSclera);

    pb.begin(left ? Part::PupilL : Part::PupilR, left ? Part::EyeL : Part::EyeR, irisC);
    add_box(m, irisC, kIrisHalf, kIris);

    pb.begin(left ? Part::GlintL : Part::GlintR, left ? Part::PupilL : Part::PupilR, glintC);
    add_box(m, glintC, kGlintHalf, kGlint);

    pb.begin(left ? Part::LidL : Part::LidR, Part::Head, lidPivot);
    add_box(m, lidC, kLidHalf, kTanDark);
}

}  // namespace

Mat4 part_local_matrix(const MeshPart& part, const PartPose& pose) {
    const Vec3& p = part.pivot;
    return translate(-p.x, -p.y, -p.z)
         * scale(pose.scale.x, pose.scale.y, pose.scale.z)
         * rotate_x(pose.rotation.x) * rotate_y(pose.rotation.y) * rotate_z(pose.rotation.z)
         * translate(p.x + pose.offset.x, p.y + pose.offset.y, p.z + pose.offset.z);
}

void compute_part_world(const Mesh& mesh, const PartPose* poses, Mat4* out) {
    for (const MeshPart& part : mesh.parts) {
        const int i = static_cast<int>(part.id);
        const Mat4 local = part_local_matrix(part, poses[i]);
        out[i] = (part.parent == Part::Count) ? local
                                              : local * out[static_cast<int>(part.parent)];
    }
}

Mesh build_proxy_beagle() {
    Mesh m;
    m.vertices.reserve(16000);
    m.indices.reserve(24000);
    m.parts.reserve(static_cast<size_t>(Part::Count));
    PartBuilder pb{m};

    // ---- 躯干。枢轴在体心，翻身绕这里 ----
    pb.begin(Part::Body, Part::Count, {0.0f, 0.62f, 0.0f});
    // 身体。比格腿短身长，所以躯干压扁拉长。
    add_rounded_box(m, {0.00f, 0.62f,  0.00f}, {0.26f, 0.22f, 0.52f}, kWhite, 0.09f, 20, 12);
    // 背鞍：黑色的那一块，只盖住背上，两侧和肚子还是白的。
    add_rounded_box(m, {0.00f, 0.83f, -0.02f}, {0.24f, 0.05f, 0.42f}, kBlack, 0.035f, 16, 8);
    // 胸口
    add_rounded_box(m, {0.00f, 0.52f,  0.44f}, {0.22f, 0.16f, 0.12f}, kWhite, 0.06f, 16, 10);
    // 脖子
    add_rounded_box(m, {0.00f, 0.80f,  0.56f}, {0.16f, 0.15f, 0.14f}, kTan, 0.06f, 16, 10);

    // ---- 头。枢轴在颈根，转头绕这里 ----
    pb.begin(Part::Head, Part::Body, {0.0f, 0.86f, 0.60f});
    add_rounded_box(m, {0.00f, 0.95f,  0.72f}, {0.19f, 0.18f, 0.20f}, kTan, 0.065f, 20, 12);
    // 方吻
    // 吻整个是白的、前端更圆（1.0 作者要求「鼻子四周也是白色的，前端圆润」）。
    add_rounded_box(m, {0.00f, 0.86f,  0.96f}, {0.12f, 0.10f, 0.12f}, kWhite, 0.075f, 18, 12);
    // 鼻头：圆的，不是方块（作者要求，1.7）。嵌在白吻前脸上，略微凸出。
    add_sphere(m, {0.00f, 0.885f, 1.085f}, 0.052f, kNose, kNose, 12, 7);
    // 白色的鼻梁条（blaze）：比格脸中间那道白，从额头一直到鼻头（作者要求，1.7）。
    // 贴在头前脸和吻背上，比脸面高出 0.01 免得和棕色面重合闪烁；宽度留在两眼之间。
    add_box(m, {0.00f, 1.00f,  0.925f}, {0.040f, 0.13f, 0.012f}, kWhite);   // 额头到眼间
    add_box(m, {0.00f, 0.962f, 0.96f},  {0.040f, 0.010f, 0.125f}, kWhite);  // 吻背

    // ---- 长垂耳。比格的招牌，垂到下颌以下。枢轴在耳根 ----
    // 这两块将来要挂弹簧阻尼器做次级运动（设计文档 §2.3 的 L3）。
    pb.begin(Part::EarL, Part::Head, {-0.21f, 1.02f, 0.74f});
    add_rounded_box(m, {-0.21f, 0.80f, 0.74f}, {0.04f, 0.22f, 0.13f}, kTan, 0.035f, 14, 10);
    pb.begin(Part::EarR, Part::Head, { 0.21f, 1.02f, 0.74f});
    add_rounded_box(m, { 0.21f, 0.80f, 0.74f}, {0.04f, 0.22f, 0.13f}, kTan, 0.035f, 14, 10);

    // ---- 眼睛 ----
    add_eye(pb, m, -1.0f);
    add_eye(pb, m, +1.0f);

    // ---- 上翘的尾巴，尖端是白的。枢轴在尾根 ----
    pb.begin(Part::Tail, Part::Body, {0.0f, 0.78f, -0.54f});
    add_rounded_box(m, {0.00f, 0.92f, -0.56f}, {0.05f, 0.16f, 0.05f}, kTan, 0.03f, 12, 8);
    add_rounded_box(m, {0.00f, 1.12f, -0.58f}, {0.045f, 0.10f, 0.045f}, kWhite, 0.03f, 12, 8);

    // ---- 四条腿，白色。枢轴在腿根，绕 x 转就是迈步。前腿略靠前，后腿略粗 ----
    const float legH = kLegTop;
    pb.begin(Part::LegFL, Part::Body, {-0.18f, legH, 0.34f});
    add_rounded_box(m, {-0.18f, legH * 0.5f,  0.34f}, {0.075f, legH * 0.5f, 0.08f}, kWhite, 0.035f, 12, 8);
    pb.begin(Part::LegFR, Part::Body, { 0.18f, legH, 0.34f});
    add_rounded_box(m, { 0.18f, legH * 0.5f,  0.34f}, {0.075f, legH * 0.5f, 0.08f}, kWhite, 0.035f, 12, 8);
    pb.begin(Part::LegBL, Part::Body, {-0.19f, legH, -0.34f});
    add_rounded_box(m, {-0.19f, legH * 0.5f, -0.34f}, {0.085f, legH * 0.5f, 0.09f}, kWhite, 0.04f, 12, 8);
    pb.begin(Part::LegBR, Part::Body, { 0.19f, legH, -0.34f});
    add_rounded_box(m, { 0.19f, legH * 0.5f, -0.34f}, {0.085f, legH * 0.5f, 0.09f}, kWhite, 0.04f, 12, 8);

    // ---- 道具。建在原点，动作系统用 offset 摆位置，不用时 scale 归零 ----
    pb.begin(Part::Ball, Part::Count, {0.0f, kBallRadius, 0.0f});
    add_sphere(m, {0.0f, kBallRadius, 0.0f}, kBallRadius, kTennis, kTennisSeam, 16, 10);

    pb.begin(Part::Bowl, Part::Count, {0.0f, 0.0f, 0.0f});
    add_box(m, {0.0f, kBowlHeight * 0.5f, 0.0f}, {kBowlRadius, kBowlHeight * 0.5f, kBowlRadius}, kBowlBlue);
    add_box(m, {0.0f, kBowlHeight - 0.012f, 0.0f}, {kBowlRadius - 0.03f, 0.012f, kBowlRadius - 0.03f}, kWater);

    // 水流：从左后腿根斜向外下方的细长条。枢轴在腿根，缩放 y 就是「流到哪了」。
    pb.begin(Part::Stream, Part::Body, {-0.19f, kLegTop, -0.34f});
    add_box(m, {-0.30f, kLegTop * 0.5f, -0.34f}, {0.012f, kLegTop * 0.5f, 0.012f}, kPee);
    // 一滩：扁平的方块，枢轴在中心，缩放 xz 就是「越来越大」。
    pb.begin(Part::Puddle, Part::Count, {0.0f, 0.0f, 0.0f});
    add_box(m, {0.0f, 0.006f, 0.0f}, {0.14f, 0.006f, 0.10f}, kPee);

    // 阴影：贴地的深灰椭圆，半透明。狗的在体心正下方，球的在球正下方。
    // 阴影不贴在 y=0：地面原点压在任务栏顶边以下 18 px，贴地就被裁掉了。
    // 抬到 kShadowLift，正好落在与任务栏的接触线上。
    pb.begin(Part::ShadowDog, Part::Count, {0.0f, 0.0f, 0.0f}, kShadowAlpha);
    add_disc(m, {0.0f, kShadowLift, 0.0f}, kShadowDogRx, kShadowDogRz, {0.05f, 0.04f, 0.04f}, 24);
    pb.begin(Part::ShadowBall, Part::Count, {0.0f, 0.0f, 0.0f}, kShadowAlpha);
    add_disc(m, {0.0f, kShadowLift, 0.0f}, kBallRadius * 1.05f, kBallRadius * 1.05f, {0.05f, 0.04f, 0.04f}, 16);

    // 垫子：扁平的圆角长方体，建在原点脚下，宿主用 offset 摆到狗脚下、用 scale 收放。
    pb.begin(Part::Mat, Part::Count, {0.0f, 0.0f, 0.0f});
    add_rounded_box(m, {0.0f, -0.045f, 0.05f}, {0.62f, 0.045f, 0.50f}, {0.72f, 0.30f, 0.30f}, 0.045f, 20, 8);
    add_rounded_box(m, {0.0f, 0.005f, 0.05f}, {0.52f, 0.012f, 0.40f}, {0.93f, 0.86f, 0.74f}, 0.012f, 16, 6);

    // 四颗心，建在原点，HeartFx 用 offset 摆位置。
    for (int i = 0; i < 4; ++i) {
        pb.begin(static_cast<Part>(static_cast<int>(Part::Heart0) + i), Part::Count, {0.0f, 0.0f, 0.0f}, 0.88f);
        add_heart(m, 0.075f, {0.96f, 0.30f, 0.42f});
    }
    pb.end();

    // 部件数组要按 Part 枚举顺序排好，compute_part_world 按下标存取。
    // 上面的 begin 顺序不是枚举顺序（眼睛的四个部件交错），这里按 id 重排。
    std::vector<MeshPart> ordered(static_cast<size_t>(Part::Count));
    for (const MeshPart& p : m.parts) ordered[static_cast<size_t>(p.id)] = p;
    m.parts = std::move(ordered);
    return m;
}

}  // namespace pet
