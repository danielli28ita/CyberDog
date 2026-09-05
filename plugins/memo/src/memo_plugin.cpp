#include "plugins/memo_plugin.h"

#include "core/i18n.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace pet::plugins {
namespace {

// localtime 的可移植写法：MSVC 用 localtime_s，别处用 localtime_r。
bool local_tm(long long t, std::tm& out) {
    const std::time_t tt = static_cast<std::time_t>(t);
#ifdef _MSC_VER
    return localtime_s(&out, &tt) == 0;
#else
    return localtime_r(&tt, &out) != nullptr;
#endif
}

// 读一个十进制整数，成功时推进指针。不用 sscanf：MSVC 对它报 C4996，而且我们不许靠宏关警告。
bool read_int(const char*& p, int& out) {
    const char* q = p;
    int v = 0;
    bool any = false;
    while (*q >= '0' && *q <= '9') { v = v * 10 + (*q - '0'); ++q; any = true; if (v > 100000) return false; }
    if (!any) return false;
    out = v;
    p = q;
    return true;
}
bool expect(const char*& p, char c) {
    if (*p != c) return false;
    ++p;
    return true;
}
void skip_spaces(const char*& p) { while (*p == ' ') ++p; }

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

}  // namespace

bool parse_due(const std::string& raw, long long nowWall, long long& outDue) {
    const std::string s = trim(raw);
    if (s.empty()) return false;

    // "+N"：N 分钟后
    if (s[0] == '+') {
        char* end = nullptr;
        const long n = std::strtol(s.c_str() + 1, &end, 10);
        if (end == s.c_str() + 1 || n <= 0 || n > 60 * 24 * 365) return false;
        outDue = nowWall + static_cast<long long>(n) * 60;
        return true;
    }

    std::tm now{};
    if (!local_tm(nowWall, now)) return false;

    int y = 0, mo = 0, d = 0, h = 0, mi = 0;
    const char* p = s.c_str();
    const char* p0 = p;
    // "YYYY-MM-DD HH:MM"
    if (read_int(p, y) && expect(p, '-') && read_int(p, mo) && expect(p, '-') && read_int(p, d) &&
        (skip_spaces(p), read_int(p, h)) && expect(p, ':') && read_int(p, mi) && *p == '\0') {
        if (y < 2000 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59) return false;
        std::tm t = now;
        t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi; t.tm_sec = 0;
        t.tm_isdst = -1;
        const std::time_t r = std::mktime(&t);
        if (r == static_cast<std::time_t>(-1)) return false;
        outDue = static_cast<long long>(r);
        return true;
    }
    // "HH:MM"
    p = p0;
    if (read_int(p, h) && expect(p, ':') && read_int(p, mi) && *p == '\0') {
        if (h > 23 || mi > 59) return false;
        std::tm t = now;
        t.tm_hour = h; t.tm_min = mi; t.tm_sec = 0;
        t.tm_isdst = -1;
        std::time_t r = std::mktime(&t);
        if (r == static_cast<std::time_t>(-1)) return false;
        if (static_cast<long long>(r) <= nowWall) r += 24 * 3600;   // 过了就算明天
        outDue = static_cast<long long>(r);
        return true;
    }
    return false;
}

std::string format_due(long long due, long long nowWall) {
    std::tm t{}, n{};
    if (!local_tm(due, t) || !local_tm(nowWall, n)) return "?";
    char buf[32];
    if (t.tm_year == n.tm_year && t.tm_yday == n.tm_yday) {
        std::snprintf(buf, sizeof(buf), "%s %02d:%02d", tr(Str::RowToday), t.tm_hour, t.tm_min);
    } else if (t.tm_year == n.tm_year && t.tm_yday == n.tm_yday + 1) {
        std::snprintf(buf, sizeof(buf), "%s %02d:%02d", tr(Str::Tomorrow), t.tm_hour, t.tm_min);
    } else {
        std::snprintf(buf, sizeof(buf), "%02d-%02d %02d:%02d", t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
    }
    return buf;
}

// ---------------------------------------------------------------------------

void MemoPlugin::on_load(IHostServices& host) {
    host_ = &host;
    std::string text;
    if (host_->read_plugin_data(id(), text)) parse(text);
}

void MemoPlugin::on_unload() {
    save();
    host_ = nullptr;
}

void MemoPlugin::save() {
    if (host_) host_->write_plugin_data(id(), serialize());
}

std::string MemoPlugin::serialize() const {
    std::string out = "version=1\n";
    for (const MemoItem& m : items_) {
        out += std::to_string(m.id) + "|" + std::to_string(m.due) + "|" + (m.done ? "1" : "0") + "|" + m.text + "\n";
    }
    return out;
}

void MemoPlugin::parse(const std::string& text) {
    items_.clear();
    nextId_ = 1;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#' || line.rfind("version=", 0) == 0) continue;
        const size_t p1 = line.find('|');
        const size_t p2 = p1 == std::string::npos ? p1 : line.find('|', p1 + 1);
        const size_t p3 = p2 == std::string::npos ? p2 : line.find('|', p2 + 1);
        if (p3 == std::string::npos) continue;
        MemoItem m;
        m.id = static_cast<unsigned>(std::strtoul(line.substr(0, p1).c_str(), nullptr, 10));
        m.due = std::strtoll(line.substr(p1 + 1, p2 - p1 - 1).c_str(), nullptr, 10);
        m.done = line.substr(p2 + 1, p3 - p2 - 1) == "1";
        m.text = line.substr(p3 + 1);
        if (m.id == 0 || m.text.empty()) continue;
        items_.push_back(m);
        if (m.id >= nextId_) nextId_ = m.id + 1;
    }
}

unsigned MemoPlugin::add(const std::string& rawText, long long due) {
    std::string text = trim(rawText);
    for (char& c : text) if (c == '|' || c == '\n' || c == '\r') c = ' ';
    if (text.empty()) return 0;
    MemoItem m;
    m.id = nextId_++;
    m.due = due;
    m.text = text;
    items_.push_back(m);
    save();
    return m.id;
}

bool MemoPlugin::remove(unsigned id) {
    for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].id == id) {
            items_.erase(items_.begin() + static_cast<long long>(i));
            save();
            return true;
        }
    }
    return false;
}

void MemoPlugin::acknowledge(unsigned id) {
    for (MemoItem& m : items_) {
        if (m.id == id && !m.done) { m.done = true; save(); return; }
    }
}

void MemoPlugin::on_tick(const HostClock& clock) {
    if (!host_) return;
    // 本地数据别越攒越多（2.0，作者要求）：已看过且到点超过 7 天的条目每次运行清一次。
    if (!cleaned_ && clock.wallClock > 0) {
        cleaned_ = true;
        const long long cutoff = clock.wallClock - 7LL * 86400LL;
        bool removed = false;
        for (size_t i = 0; i < items_.size();) {
            if (items_[i].done && items_[i].due < cutoff) { items_.erase(items_.begin() + static_cast<long long>(i)); removed = true; }
            else ++i;
        }
        if (removed) save();
    }
    // 一次只提醒一条：最早到点、未完成、本次运行没提醒过的。
    MemoItem* due = nullptr;
    for (MemoItem& m : items_) {
        if (m.done || m.fired || m.due > clock.wallClock) continue;
        if (!due || m.due < due->due) due = &m;
    }
    if (!due) return;
    // 宿主在忙就等；超过一分钟还没接受也照样留着，下次 tick 再试——备忘不能丢。
    if (clock.petBusy || !clock.petVisible) return;

    PetActionRequest req;
    req.action = ActionKind::RemindMemo;
    const std::string bubble = std::string(tr(Str::MemoPrefix)) + due->text;
    req.bubbleText = bubble.c_str();
    req.bubbleSeconds = 20.0f;
    req.bubbleDelaySeconds = 1.6f;   // 跑到中间坐下再说
    if (host_->request_pet_action(req)) {
        due->fired = true;
        lastFired_ = due->id;
    }
}

}  // namespace pet::plugins
