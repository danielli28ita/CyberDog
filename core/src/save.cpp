#include "core/save.h"

#include <cstdio>
#include <cstdlib>

namespace pet {

void SaveData::set(const std::string& key, const std::string& value) {
    for (auto& p : kv_) {
        if (p.first == key) {
            if (p.second != value) { p.second = value; dirty_ = true; }
            return;
        }
    }
    kv_.emplace_back(key, value);
    dirty_ = true;
}

void SaveData::set_float(const std::string& key, float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(v));
    set(key, buf);
}

void SaveData::set_u64(const std::string& key, unsigned long long v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%llu", v);
    set(key, buf);
}

bool SaveData::has(const std::string& key) const {
    for (const auto& p : kv_) if (p.first == key) return true;
    return false;
}

std::string SaveData::get(const std::string& key, const std::string& def) const {
    for (const auto& p : kv_) if (p.first == key) return p.second;
    return def;
}

float SaveData::get_float(const std::string& key, float def) const {
    if (!has(key)) return def;
    return static_cast<float>(std::atof(get(key).c_str()));
}

unsigned long long SaveData::get_u64(const std::string& key, unsigned long long def) const {
    if (!has(key)) return def;
    return std::strtoull(get(key).c_str(), nullptr, 10);
}

std::string SaveData::serialize() const {
    std::string out = "version=" + std::to_string(kVersion) + "\n";
    for (const auto& p : kv_) {
        if (p.first == "version") continue;
        out += p.first + "=" + p.second + "\n";
    }
    return out;
}

void SaveData::parse(const std::string& text) {
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        set(line.substr(0, eq), line.substr(eq + 1));
    }
    dirty_ = false;
}

}  // namespace pet
