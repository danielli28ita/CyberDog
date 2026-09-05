// 天气插件：启动时报一次当地天气。作者要求（1.3）。
//
// 这是本项目**唯一**会联网的部件，而且只在用户填了城市之后才联网（设计文档 M8 的例外，
// 见 §2.14）。数据源（都免费、无密钥、不需要注册）：
//   1. 地理编码  https://nominatim.openstreetmap.org/search?q=<城市>&format=jsonv2&limit=1&accept-language=zh
//               中文地名它认得准（1.6 起，「罗马」「都灵」都对）。失败退回
//               https://geocoding-api.open-meteo.com/v1/search?name=<城市>&count=1&language=zh
//   2. 天气      https://api.open-meteo.com/v1/forecast?latitude=..&longitude=..
//                  &current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min&timezone=auto
// 不做 IP 定位：城市是用户自己填的，服务器只知道一个城市名。
//
// 纯 C++：HTTP 由宿主的 fetch_text 服务做，本插件只拼 URL、解析结果、请求气泡。
// JSON 只取几个数，手写查找，不引第三方库。

#pragma once

#include <string>
#include <vector>

#include "core/plugin_api.h"

namespace pet::plugins {

// 从 JSON 文本里取 "key": 数值 / "key": "字符串"。找不到返回 false。够 Open-Meteo 的扁平结构用。
bool json_number(const std::string& json, const char* key, double& out);
bool json_string(const std::string& json, const char* key, std::string& out);
// 数组里的第一个数，如 "temperature_2m_max":[28.1,...]。
bool json_first_number_in_array(const std::string& json, const char* key, double& out);
// "key": { ... } 这一整段（含花括号）。不是对象或找不到返回空串。
// 用它先切出 current / daily，再在里面找键——外层还有 current_units 这种同名键的对象。
std::string json_object(const std::string& json, const char* key);
// 顶层数组里的每个对象各切一段（Nominatim 的响应是数组）。
std::vector<std::string> json_array_objects(const std::string& json);
// 在 Nominatim 的结果里挑一个：中国 > 欧洲 > 重要度。返回下标，没有返回 -1。
int pick_place(const std::string& body);

// WMO 天气代码 → 中文。
const char* weather_code_text(int code);

// 把两次响应拼成一句话，如「杭州 今天多云，现在 24°，最高 28° 最低 19°」。失败返回空串。
std::string compose_weather_line(const std::string& city, const std::string& forecastJson);

class WeatherPlugin final : public IPlugin {
public:
    const char* id() const override { return "plugin.weather"; }
    void on_load(IHostServices& host) override;
    void on_tick(const HostClock& clock) override;
    void on_unload() override;

    void set_city(const std::string& city) { city_ = city; }
    const std::string& city() const { return city_; }
    // 立刻查一次（启动后宿主调；改城市后也调）。
    void refresh();
    const std::string& last_line() const { return line_; }
    bool busy() const { return busy_; }

private:
    static void on_nominatim(void* user, bool ok, const std::string& body);
    static void on_geo(void* user, bool ok, const std::string& body);
    void request_forecast(double lat, double lon);
    static void on_forecast(void* user, bool ok, const std::string& body);
    void announce(const std::string& text);

    IHostServices* host_ = nullptr;
    std::string city_;
    std::string resolvedName_;
    std::string line_;
    bool busy_ = false;
    bool pendingAnnounce_ = false;
    double retryUntil_ = 0.0;
};

}  // namespace pet::plugins
