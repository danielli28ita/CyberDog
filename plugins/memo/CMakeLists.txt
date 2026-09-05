# 备忘录插件。纯 C++，只依赖 core 的插件契约。
add_library(pet_plugin_memo STATIC
    src/memo_plugin.cpp
)
target_include_directories(pet_plugin_memo PUBLIC include)
target_link_libraries(pet_plugin_memo PUBLIC pet_core)
set_target_properties(pet_plugin_memo PROPERTIES FOLDER "plugins")
