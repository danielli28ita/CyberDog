# 操作提示插件（CyberDog 1.0，作者要求「操作建议或教程随机出现」）。纯 C++，只依赖 core 的插件契约。

add_library(pet_plugin_tips STATIC
    src/tips_plugin.cpp
)
target_include_directories(pet_plugin_tips PUBLIC include)
target_link_libraries(pet_plugin_tips PUBLIC pet_core)
set_target_properties(pet_plugin_tips PROPERTIES FOLDER "plugins")
