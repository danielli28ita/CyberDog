// 用户数据目录与存档文件读写。
//
// 2.0 起：数据放在程序旁边的 CyberDog-data\ 目录（作者要求「和程序同目录，方便清理」）。
// 程序目录不可写（装在 Program Files 之类）时退回 %LOCALAPPDATA%\CyberDog。第一次切换时把老位置（Jdog 时代）的文件搬过来。
// 占用：cyberdog.ini 不到 1 KB，备忘录一行一条，已看过且过期 7 天的自动清掉；没有日志文件。

#pragma once

#include <string>

namespace pet::win {

// 数据目录（UTF-8，不带末尾反斜杠）。
std::string data_dir();

// 存档文件的完整路径（UTF-8）。目录不存在会建。
std::string save_path();

// 插件自己的存档路径：同一目录下的 <pluginId>.txt。插件 id 只允许字母数字点和下划线。
std::string plugin_data_path(const char* pluginId);

// 读整个文件。不存在返回 false。
bool read_text_file(const std::string& path, std::string& out);

// 写整个文件。先写临时文件再改名，中途断电不会留下半个存档。
bool write_text_file(const std::string& path, const std::string& text);

}  // namespace pet::win
