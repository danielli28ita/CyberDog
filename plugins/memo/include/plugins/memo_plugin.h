// 备忘录插件。设计文档 §1.1 M6 提到的「备忘录提醒」，原排 P6，按作者要求提前到 1.2。
//
// 它是第二个真实插件，逼出了契约里的两项：save 服务（自己的存档命名空间）和墙钟时间。
// 纯 C++：不知道窗口，界面由宿主做（P5 的 panel 服务到位后改成插件自己注册）。
//
// 一条备忘 = 内容 + 到点时刻。到点时请求宠物做 RemindMemo 动作并显示内容 20 秒；
// 宿主在气泡期间把「点了狗一下」当作已看到（acknowledge）。到点过的备忘保留在列表里
// 标为已完成，用户自己删。
//
// 存档格式（每行一条）：id|due|done|text。text 里的换行和竖线会被替换成空格。

#pragma once

#include <ctime>
#include <string>
#include <vector>

#include "core/plugin_api.h"

namespace pet::plugins {

struct MemoItem {
    unsigned    id = 0;
    long long   due = 0;      // Unix 秒
    bool        done = false;
    bool        fired = false;  // 本次运行里已经提醒过（不存盘；重启后到点未完成的会再提醒一次）
    std::string text;
};

// 把用户输入的时间解析成 Unix 秒。支持：
//   "HH:MM"              今天这个时刻，已经过了就算明天
//   "+N"                 N 分钟后
//   "YYYY-MM-DD HH:MM"   指定日期时间
// 解析失败返回 false。
bool parse_due(const std::string& input, long long nowWall, long long& outDue);

// 到点时刻的显示文字，如「明天 09:30」「今天 14:00」「09-12 08:00」。
std::string format_due(long long due, long long nowWall);

class MemoPlugin final : public IPlugin {
public:
    const char* id() const override { return "plugin.memo"; }
    void on_load(IHostServices& host) override;
    void on_tick(const HostClock& clock) override;
    void on_unload() override;

    // 给宿主界面用的模型。
    const std::vector<MemoItem>& items() const { return items_; }
    unsigned add(const std::string& text, long long due);
    bool remove(unsigned id);
    void acknowledge(unsigned id);   // 用户看到了提醒
    unsigned last_fired_id() const { return lastFired_; }

    // 存档往返（宿主给的文本）。
    std::string serialize() const;
    void parse(const std::string& text);

private:
    void save();

    IHostServices* host_ = nullptr;
    std::vector<MemoItem> items_;
    unsigned nextId_ = 1;
    unsigned lastFired_ = 0;
    bool cleaned_ = false;   // 本次运行清理过过期条目了吗
    double retryUntil_ = 0.0;
};

}  // namespace pet::plugins
