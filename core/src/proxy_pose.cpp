#include "core/proxy_pose.h"

#include <cmath>

namespace pet {
namespace {

// 挑眉角度。内眼角压低：左眼内角在 +x 侧，压低 +x 侧是负角；右眼相反。
constexpr float kBrowAngle = 0.35f;

// 眼睑缩放。闭合度 c：0 睁开只留眉线，1 全盖住；负值是瞪大，眉线再缩。
float lid_scale(float c) {
    if (c >= 0.0f) return kLidOpenScale + (1.0f - kLidOpenScale) * c;
    return clampf(kLidOpenScale * (1.0f + 2.5f * c), 0.05f, kLidOpenScale);
}

}  // namespace

void pose_from_gaze(const GazeState& g, GazeMood mood, float t, PartPose* poses) {
    const auto P = [&](Part id) -> PartPose& { return poses[static_cast<int>(id)]; };

    P(Part::Head).rotation = P(Part::Head).rotation + Vec3{g.headPitch, g.headYaw, g.headRoll};

    // 瞳孔平移。offset 在父部件（眼白→头）的局部坐标里，跟着头转。
    P(Part::PupilL).offset = {g.pupilX[0] * kPupilTravelX, g.pupilY[0] * kPupilTravelY, 0.0f};
    P(Part::PupilR).offset = {g.pupilX[1] * kPupilTravelX, g.pupilY[1] * kPupilTravelY, 0.0f};

    P(Part::LidL).scale = {1.0f, lid_scale(g.lidClose[0]), 1.0f};
    P(Part::LidR).scale = {1.0f, lid_scale(g.lidClose[1]), 1.0f};
    P(Part::LidL).rotation.z = -kBrowAngle * g.browTilt;
    P(Part::LidR).rotation.z = +kBrowAngle * g.browTilt;

    // 耳朵：头转动时略微反向拖一点，是次级运动最简单的近似。P2 换弹簧阻尼器。
    const Vec3 earAdd{-g.headPitch * 0.3f, 0.0f, -g.headRoll * 0.4f};
    P(Part::EarL).rotation = P(Part::EarL).rotation + earAdd;
    P(Part::EarR).rotation = P(Part::EarR).rotation + earAdd;

    // 尾巴：有人看着就快摇，没人理就慢摆，犯困不动。
    float amp = 0.12f, hz = 1.5f;
    switch (mood) {
        case GazeMood::Track:
        case GazeMood::Viewer:  amp = 0.38f; hz = 4.5f; break;
        case GazeMood::Startle: amp = 0.05f; hz = 1.0f; break;
        case GazeMood::SideEye: amp = 0.20f; hz = 2.5f; break;
        case GazeMood::Sleepy:  amp = 0.0f;  hz = 0.0f; break;
        case GazeMood::Wander:  break;
    }
    const float w = t * hz * 6.2831853f;
    P(Part::Tail).rotation = P(Part::Tail).rotation +
                             Vec3{0.0f, amp * std::sin(w), amp * 0.35f * std::sin(w + 1.2f)};
}

}  // namespace pet
