# overlay：Win32 + D3D12 + DirectComposition 的常驻覆盖层。

# 着色器构建期编译：fxc 把 HLSL 编成字节数组头文件，运行时不再依赖 d3dcompiler_47.dll。
# fxc 随 Windows SDK 提供，不在 PATH 上。
set(_kits "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin")
find_program(FXC_EXE fxc
    HINTS "${_kits}/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
          "${_kits}/10.0.22621.0/x64"
          "${_kits}/x64")
if(NOT FXC_EXE)
    message(FATAL_ERROR "找不到 fxc.exe（Windows SDK）。着色器要在构建期编译。")
endif()
set(SHADER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pet.hlsl)
set(SHADER_GEN ${CMAKE_CURRENT_BINARY_DIR}/shaders_gen)
file(MAKE_DIRECTORY ${SHADER_GEN})
add_custom_command(
    OUTPUT ${SHADER_GEN}/pet_vs.h ${SHADER_GEN}/pet_ps.h
    COMMAND ${FXC_EXE} /nologo /O3 /T vs_5_0 /E VSMain /Vn g_pet_vs /Fh ${SHADER_GEN}/pet_vs.h ${SHADER_SRC}
    COMMAND ${FXC_EXE} /nologo /O3 /T ps_5_0 /E PSMain /Vn g_pet_ps /Fh ${SHADER_GEN}/pet_ps.h ${SHADER_SRC}
    DEPENDS ${SHADER_SRC}
    COMMENT "fxc: pet.hlsl -> pet_vs.h / pet_ps.h")

add_library(pet_overlay STATIC
    ${SHADER_GEN}/pet_vs.h
    ${SHADER_GEN}/pet_ps.h
    src/desktop_metrics.cpp
    src/win32_window.cpp
    src/d3d_context.cpp
    src/tray_icon.cpp
    src/idle_controller.cpp
    src/mesh_renderer.cpp
    src/bubble_window.cpp
    src/audio.cpp
    src/user_data.cpp
    src/rename_dialog.cpp
    src/personality_dialog.cpp
    src/memo_dialog.cpp
    src/stats_panel.cpp
    src/ui_style.cpp
    src/http.cpp
)

target_include_directories(pet_overlay PUBLIC include PRIVATE ${SHADER_GEN})

target_link_libraries(pet_overlay PUBLIC
    pet_core
    d3d12
    dxgi
    dxguid
    dcomp
    shcore      # GetDpiForMonitor
    shell32     # Shell_NotifyIcon
    xaudio2     # 音效。Windows 10 起随系统提供 xaudio2_9.dll
    ole32       # CoInitializeEx / CoTaskMemFree
    winhttp     # 天气插件的 HTTP，用户填了城市才用
    advapi32    # 开机自启的注册表项
    wtsapi32    # 锁屏 / 解锁通知
)

set_target_properties(pet_overlay PROPERTIES FOLDER "overlay")

# 主程序。托盘常驻，右键菜单退出。
# Release 配置：不带控制台窗口（入口仍是 main），产物名 CyberDog.exe，单文件分发。
add_executable(pet src/main_app.cpp res/cyberdog.rc)
target_link_libraries(pet PRIVATE pet_overlay pet_plugin_health pet_plugin_memo pet_plugin_weather pet_plugin_tips)
set_target_properties(pet PROPERTIES
    FOLDER "overlay"
    OUTPUT_NAME_RELEASE "CyberDog"
    LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")

# 回归用例。跑固定秒数后自动退出，用来验渲染与栖位没回归。
add_executable(spike_dcomp src/spike_dcomp.cpp)
target_link_libraries(spike_dcomp PRIVATE pet_overlay)
set_target_properties(spike_dcomp PROPERTIES FOLDER "overlay")
