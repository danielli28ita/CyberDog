// 把眼神控制器的输出翻译成代理体的部件姿态。
//
// 这是 GazeState 与 Part 枚举之间唯一的耦合点。换成蒙皮模型时，
// 只需要写一个对应骨骼名的版本，GazeController 本身不动。

#pragma once

#include "core/gaze.h"
#include "core/proxy_mesh.h"

namespace pet {

// poses 长度为 Part::Count。timeSeconds 用来驱动尾巴这类周期动作。
//
// 头、耳、尾的旋转是**累加**到 poses 里已有的值上，瞳孔和眼睑是直接赋值。
// 这样动作系统先写体态（低头拱球、翻身），眼神再叠上去，两边不打架。
// 调用顺序：先清零 poses，再动作，再这个。
void pose_from_gaze(const GazeState& g, GazeMood mood, float timeSeconds, PartPose* poses);

}  // namespace pet
