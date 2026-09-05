// 用户数据目录与存档文件读写。
//
// 1.1：启动时优先用程序同目录的 CyberDog-data\。
// 找不到 cyberdog.ini 时弹出选择：新建狗 / 从其他目录导入 / 退出。
// 不再静默从 %LOCALAPPDATA% 搬数据。
//
// 测试用环境变量（跳过对话框）：
//   PET_DATA_DIR=路径     强制使用该目录
//   PET_NEW_DOG=1         在程序旁新建空数据目录
//   PET_IMPORT_DIR=路径   从该目录复制到程序旁 CyberDog-data\

#pragma once

#include <string>

namespace pet::win {

// 在读存档之前调用一次。返回 false = 用户取消，进程应退出。
bool ensure_data_ready();

// 数据目录（UTF-8，不带末尾反斜杠）。须先 ensure_data_ready。
std::string data_dir();

// 存档文件的完整路径（UTF-8）。
std::string save_path();

// 插件自己的存档路径：同一目录下的 <pluginId>.txt。
std::string plugin_data_path(const char* pluginId);

bool read_text_file(const std::string& path, std::string& out);
bool write_text_file(const std::string& path, const std::string& text);

}  // namespace pet::win
