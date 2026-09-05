# core：纯 C++，不依赖 Windows、不依赖任何引擎。
# 桌宠覆盖层链接它；将来的 UE 项目也链接它。
# 这里不许出现 <windows.h>、D3D、UE 的任何头文件。

add_library(pet_core STATIC
    src/personality.cpp
    src/proxy_mesh.cpp
    src/gaze.cpp
    src/proxy_pose.cpp
    src/action.cpp
    src/action_catalog.cpp
    src/bond.cpp
    src/hearts.cpp
    src/interaction.cpp
    src/save.cpp
    src/sound_synth.cpp
    src/i18n.cpp
)

target_include_directories(pet_core PUBLIC include)
set_target_properties(pet_core PROPERTIES FOLDER "core")
