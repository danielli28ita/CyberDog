# 健康提醒插件（设计文档 §2.7，M10）。纯 C++，只依赖 core 的插件契约。
# 现在静态链接进宿主；P5 做插件宿主时改成动态加载，本目录不用动。

add_library(pet_plugin_health STATIC
    src/health_plugin.cpp
)
target_include_directories(pet_plugin_health PUBLIC include)
target_link_libraries(pet_plugin_health PUBLIC pet_core)
set_target_properties(pet_plugin_health PROPERTIES FOLDER "plugins")
