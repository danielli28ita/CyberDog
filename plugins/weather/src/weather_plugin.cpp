#include "plugins/weather_plugin.h"

#include "core/i18n.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace pet::plugins {
namespace {

// 找 "key": 之后的位置；找不到返回 npos。
size_t find_key(const std::string& json, const char* key) {
    const std::string k = std::string("\"") + key + "\"";
    const size_t p = json.find(k);
    if (p == std::string::npos) return p;
    const size_t colon = json.find(':', p + k.size());
    if (colon == std::string::npos) return colon;
    size_t q = colon + 1;
    while (q < json.size() && (json[q] == ' ' || json[q] == '\n' || json[q] == '\r' || json[q] == '\t')) ++q;
    return q;
}

// 把 URL 里非 ASCII 和空格转成 %XX。城市名是中文，必须编码。
std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        if (keep) out += static_cast<char>(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

std::string fmt_deg(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0f°", v);
    return buf;
}

}  // namespace

bool json_number(const std::string& json, const char* key, double& out) {
    const size_t q = find_key(json, key);
    if (q == std::string::npos || q >= json.size()) return false;
    char* end = nullptr;
    const double v = std::strtod(json.c_str() + q, &end);
    if (end == json.c_str() + q) return false;
    out = v;
    return true;
}

bool json_string(const std::string& json, const char* key, std::string& out) {
    const size_t q = find_key(json, key);
    if (q == std::string::npos || q >= json.size() || json[q] != '"') return false;
    const size_t e = json.find('"', q + 1);
    if (e == std::string::npos) return false;
    out = json.substr(q + 1, e - q - 1);
    return true;
}

bool json_first_number_in_array(const std::string& json, const char* key, double& out) {
    const size_t q = find_key(json, key);
    if (q == std::string::npos || q >= json.size() || json[q] != '[') return false;
    char* end = nullptr;
    const double v = std::strtod(json.c_str() + q + 1, &end);
    if (end == json.c_str() + q + 1) return false;
    out = v;
    return true;
}

const char* weather_code_text(int code) {
    // WMO 4677 的常用子集，Open-Meteo 文档里的那张表。
    if (code == 0) return tr(Str::Wx0);
    if (code == 1) return tr(Str::Wx1);
    if (code == 2) return tr(Str::Wx2);
    if (code == 3) return tr(Str::Wx3);
    if (code == 45 || code == 48) return tr(Str::WxFog);
    if (code >= 51 && code <= 57) return tr(Str::WxDrizzle);
    if (code >= 61 && code <= 67) return tr(Str::WxRain);
    if (code >= 71 && code <= 77) return tr(Str::WxSnow);
    if (code >= 80 && code <= 82) return tr(Str::WxShowers);
    if (code == 85 || code == 86) return tr(Str::WxSnowShowers);
    if (code >= 95) return tr(Str::WxThunder);
    return tr(Str::WxUnknown);
}

std::string json_object(const std::string& json, const char* key) {
    const size_t q = find_key(json, key);
    if (q == std::string::npos || q >= json.size() || json[q] != '{') return "";
    int depth = 0;
    for (size_t i = q; i < json.size(); ++i) {
        if (json[i] == '{') ++depth;
        else if (json[i] == '}' && --depth == 0) return json.substr(q, i - q + 1);
    }
    return "";
}

std::string compose_weather_line(const std::string& city, const std::string& js) {
    // 真实响应里 current 前面还有 current_units：{"temperature_2m":"°C",...}，
    // 扁平查找会先撞上单位字符串，数值解析失败。1.3、1.4 的天气就是这么一直「没取到」的
    // （回归用例的样本没带 _units，没拦住）。先切出 current / daily 两个对象再找键。
    const std::string cur = json_object(js, "current");
    const std::string day = json_object(js, "daily");
    if (cur.empty()) return "";
    double now = 0, code = 0, hi = 0, lo = 0;
    if (!json_number(cur, "temperature_2m", now)) return "";
    if (!json_number(cur, "weather_code", code)) return "";
    // daily 里的键带后缀 _max/_min，且是数组。
    const bool hasHi = json_first_number_in_array(day, "temperature_2m_max", hi);
    const bool hasLo = json_first_number_in_array(day, "temperature_2m_min", lo);
    char buf[256];
    std::snprintf(buf, sizeof(buf), tr(Str::WeatherLineFmt), city.c_str(), weather_code_text(static_cast<int>(std::lround(code))), fmt_deg(now).c_str());
    std::string s = buf;
    if (hasHi && hasLo) {
        std::snprintf(buf, sizeof(buf), tr(Str::WeatherHiLoFmt), fmt_deg(hi).c_str(), fmt_deg(lo).c_str());
        s += buf;
    }
    return s;
}

// ---------------------------------------------------------------------------

void WeatherPlugin::on_load(IHostServices& host) { host_ = &host; }
void WeatherPlugin::on_unload() { host_ = nullptr; }

void WeatherPlugin::refresh() {
    if (!host_ || city_.empty() || busy_) return;
    busy_ = true;
    line_.clear();
    // 地理编码先问 OSM Nominatim：中文地名它认得准（「罗马」「都灵」都对），
    // Open-Meteo 自己的地理编码按中文搜会撞上同名小地方（「罗马」给了澳大利亚的 Roma）。
    // Nominatim 失败再退回 Open-Meteo。两家都是免费无密钥；每次启动最多各一次请求。
    const std::string url = "https://nominatim.openstreetmap.org/search?q=" + url_encode(city_) + "&format=jsonv2&limit=5&addressdetails=1&accept-language=" + lang_code(language());
    if (!host_->fetch_text(url, &WeatherPlugin::on_nominatim, this)) busy_ = false;
}

std::vector<std::string> json_array_objects(const std::string& json) {
    // 顶层数组里的每个 {...} 切成一段。字符串里的花括号不管（Nominatim 的字段值里没有）。
    std::vector<std::string> out;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i < json.size(); ++i) {
        if (json[i] == '{') { if (depth++ == 0) start = i; }
        else if (json[i] == '}' && --depth == 0) out.push_back(json.substr(start, i - start + 1));
    }
    return out;
}

int pick_place(const std::string& body) {
    // 同名地方优先中国，其次欧洲，再按 Nominatim 的重要度（作者要求：都灵是意大利的，柏林是德国的）。
    static const char* kEurope[] = {"it", "de", "fr", "es", "pt", "gb", "ie", "nl", "be", "lu", "ch", "at", "cz", "pl",
                                    "hu", "sk", "si", "hr", "ro", "bg", "gr", "dk", "se", "no", "fi", "is", "ee", "lv",
                                    "lt", "ua", "rs", "ba", "mk", "al", "me", "md", "by", "mt", "cy", "va", "sm", "mc", "li", "ad"};
    const std::vector<std::string> places = json_array_objects(body);
    int best = -1;
    double bestScore = -1.0;
    for (size_t i = 0; i < places.size(); ++i) {
        double importance = 0.0;
        json_number(places[i], "importance", importance);
        std::string cc;
        json_string(places[i], "country_code", cc);
        double score = importance;
        // importance 在 0–1 之间；中国加 1.2 保证压过一切，欧洲加 0.5 压过美洲同名城。
        if (cc == "cn") score += 1.2;
        else for (const char* e : kEurope) if (cc == e) { score += 0.5; break; }
        if (score > bestScore) { bestScore = score; best = static_cast<int>(i); }
    }
    return best;
}

void WeatherPlugin::on_nominatim(void* user, bool ok, const std::string& body) {
    auto* self = static_cast<WeatherPlugin*>(user);
    std::string latS, lonS, nm;
    // 响应是数组，lat / lon 是字符串："lat":"41.89"。先按国家偏好挑一个。
    const int idx = ok ? pick_place(body) : -1;
    const std::string place = idx >= 0 ? json_array_objects(body)[static_cast<size_t>(idx)] : std::string();
    if (!place.empty() && json_string(place, "lat", latS) && json_string(place, "lon", lonS) && !latS.empty() && !lonS.empty()) {
        const double lat = std::strtod(latS.c_str(), nullptr), lon = std::strtod(lonS.c_str(), nullptr);
        self->resolvedName_ = json_string(place, "name", nm) && !nm.empty() ? nm : self->city_;
        self->request_forecast(lat, lon);
        return;
    }
    const std::string url = "https://geocoding-api.open-meteo.com/v1/search?name=" + url_encode(self->city_) + "&count=1&language=" + std::string(lang_code(language())) + "&format=json";
    if (!self->host_->fetch_text(url, &WeatherPlugin::on_geo, self)) self->busy_ = false;
}

void WeatherPlugin::on_geo(void* user, bool ok, const std::string& body) {
    auto* self = static_cast<WeatherPlugin*>(user);
    double lat = 0, lon = 0;
    if (!ok || !json_number(body, "latitude", lat) || !json_number(body, "longitude", lon)) {
        char msg[320];
        std::snprintf(msg, sizeof(msg), tr(Str::WeatherNotFoundFmt), self->city_.c_str());
        self->line_ = msg;
        self->busy_ = false;
        self->pendingAnnounce_ = true;
        return;
    }
    std::string nm;
    self->resolvedName_ = json_string(body, "name", nm) ? nm : self->city_;
    self->request_forecast(lat, lon);
}

void WeatherPlugin::request_forecast(double lat, double lon) {
    WeatherPlugin* self = this;
    char url[512];
    std::snprintf(url, sizeof(url),
                  "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
                  "&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1",
                  lat, lon);
    if (!self->host_->fetch_text(url, &WeatherPlugin::on_forecast, self)) self->busy_ = false;
}

void WeatherPlugin::on_forecast(void* user, bool ok, const std::string& body) {
    auto* self = static_cast<WeatherPlugin*>(user);
    self->busy_ = false;
    self->line_ = ok ? compose_weather_line(self->resolvedName_, body) : "";
    // 网络失败和数据看不懂分开说，不然排查时分不清是哪一头的问题。
    if (!ok) self->line_ = tr(Str::WeatherNetFail);
    else if (self->line_.empty()) self->line_ = tr(Str::WeatherDataBad);
    self->pendingAnnounce_ = true;
}

void WeatherPlugin::on_tick(const HostClock& clock) {
    if (!host_ || !pendingAnnounce_) return;
    if (clock.petBusy || !clock.petVisible) return;
    if (clock.nowSeconds < retryUntil_) return;
    PetActionRequest req;
    req.action = ActionKind::Poked;          // 抬头看你一下，就说一句
    req.bubbleText = line_.c_str();
    req.bubbleSeconds = 12.0f;
    if (host_->request_pet_action(req)) pendingAnnounce_ = false;
    else retryUntil_ = clock.nowSeconds + 5.0;
}

void WeatherPlugin::announce(const std::string& text) { line_ = text; pendingAnnounce_ = true; }

}  // namespace pet::plugins
