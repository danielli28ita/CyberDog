# 天气插件。纯 C++，HTTP 由宿主的 net 服务提供。本项目唯一联网的部件，用户填了城市才联。
add_library(pet_plugin_weather STATIC
    src/weather_plugin.cpp
)
target_include_directories(pet_plugin_weather PUBLIC include)
target_link_libraries(pet_plugin_weather PUBLIC pet_core)
set_target_properties(pet_plugin_weather PROPERTIES FOLDER "plugins")
