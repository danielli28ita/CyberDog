// 存档：key=value 文本，一行一条。纯 C++，只做解析和序列化，读写文件由平台层做。
//
// 设计文档 §4 要求存档带版本号；写盘节流（≥30 秒）由调用方保证，
// 技能包 hot path 禁令第 1 条：不许在 tick 里同步写文件。

#pragma once

#include <string>
#include <vector>
#include <utility>

namespace pet {

class SaveData {
public:
    static constexpr int kVersion = 1;

    void set(const std::string& key, const std::string& value);
    void set_float(const std::string& key, float v);
    void set_u64(const std::string& key, unsigned long long v);

    bool has(const std::string& key) const;
    std::string get(const std::string& key, const std::string& def = "") const;
    float get_float(const std::string& key, float def) const;
    unsigned long long get_u64(const std::string& key, unsigned long long def) const;

    // 文本 <-> 结构。parse 遇到不认识的行跳过，不报错。
    std::string serialize() const;
    void parse(const std::string& text);

    bool dirty() const { return dirty_; }
    void clear_dirty() { dirty_ = false; }

private:
    std::vector<std::pair<std::string, std::string>> kv_;
    bool dirty_ = false;
};

}  // namespace pet
