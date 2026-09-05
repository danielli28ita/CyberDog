cmake_minimum_required(VERSION 3.24)
project(desktop_pet LANGUAGES CXX)

# C++20：与 UE5 一致，将来 core 层要被 UE 项目链接。
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
    add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    # 静态链接 CRT：单个 exe 发给别人不用装 VC 运行库。Debug 也静态，两边行为一致。
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# core：纯 C++，不依赖平台和引擎。桌宠和将来的 UE 项目都链接它。
add_subdirectory(core)

# plugins：业务插件，只依赖 core 的插件契约。
add_subdirectory(plugins/health)
add_subdirectory(plugins/memo)
add_subdirectory(plugins/weather)
add_subdirectory(plugins/tips)

# tests：core 层回归用例。
add_subdirectory(tests)

# overlay：Win32 + D3D12 + DirectComposition 的常驻覆盖层。仅 Windows。
if(WIN32)
    add_subdirectory(overlay)
endif()
