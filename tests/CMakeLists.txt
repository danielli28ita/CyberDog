# core 层回归用例。不依赖 Windows。
add_executable(pet_tests core_tests.cpp)
target_link_libraries(pet_tests PRIVATE pet_core pet_plugin_memo pet_plugin_health pet_plugin_weather pet_plugin_tips)
set_target_properties(pet_tests PROPERTIES FOLDER "tests")
