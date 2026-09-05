// core 层的最小回归用例。不依赖 Windows，纯 C++，跑完打印通过/失败并以退出码返回。
// 覆盖：手势识别、亲密度规则、存档往返、性格不变量。

#include "core/action.h"
#include "core/action_catalog.h"
#include "core/bond.h"
#include "core/interaction.h"
#include "core/personality.h"
#include "core/save.h"
#include "core/sound_synth.h"
#include "plugins/health_plugin.h"
#include "plugins/memo_plugin.h"
#include "plugins/weather_plugin.h"
#include "plugins/tips_plugin.h"
#include "core/i18n.h"
#include "core/hearts.h"

#include <ctime>
#include <string>
#include <vector>

#include <cstdio>
#include <cstring>

namespace {

int g_failed = 0;

void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++g_failed;
}

// 喂一串样本，返回收到的各类事件计数。
struct Counts { int pet = 0, hit = 0, poke = 0, dragStart = 0, dragMove = 0, dragEnd = 0; };

Counts feed(pet::GestureTracker& g, const pet::PointerSample* s, int n) {
    Counts c;
    for (int i = 0; i < n; ++i) {
        const pet::GestureEvent e = g.update(s[i]);
        switch (e.g) {
            case pet::Gesture::PetTick:   ++c.pet; break;
            case pet::Gesture::Hit:       ++c.hit; break;
            case pet::Gesture::Poke:      ++c.poke; break;
            case pet::Gesture::DragStart: ++c.dragStart; break;
            case pet::Gesture::DragMove:  ++c.dragMove; break;
            case pet::Gesture::DragEnd:   ++c.dragEnd; break;
            default: break;
        }
    }
    return c;
}

void test_gestures() {
    std::printf("手势\n");
    // 戳：按下、不动、0.1 秒后松开。
    {
        pet::GestureTracker g;
        pet::PointerSample s[3];
        s[0] = {true, true, true, 100, 100, 0.016f, 1.0f};
        s[1] = {true, true, true, 101, 100, 0.05f, 1.0f};
        s[2] = {false, true, true, 101, 100, 0.05f, 1.0f};
        const Counts c = feed(g, s, 3);
        check(c.poke == 1 && c.pet == 0 && c.hit == 0, "按下不动松开 = 戳一下");
    }
    // 摸：头上每帧移动 3 像素（约 190 px/s），持续 1 秒。
    {
        pet::GestureTracker g;
        pet::PointerSample s[62];
        for (int i = 0; i < 62; ++i) s[i] = {i < 61, true, true, 100 + i * 3, 100, 0.016f, 1.0f};
        const Counts c = feed(g, s, 62);
        check(c.pet > 30 && c.hit == 0 && c.poke == 0 && c.dragStart == 0, "头上慢慢移动 = 抚摸，且不是拖动");
    }
    // 打：头上每帧左右横扫 40 像素（2500 px/s），来回反转。
    {
        pet::GestureTracker g;
        pet::PointerSample s[40];
        for (int i = 0; i < 40; ++i) {
            const int x = 100 + ((i / 4) % 2 == 0 ? (i % 4) * 40 : (3 - i % 4) * 40);
            s[i] = {i < 39, true, true, x, 100, 0.016f, 1.0f};
        }
        const Counts c = feed(g, s, 40);
        check(c.hit >= 1 && c.pet == 0, "头上快速来回 = 打");
    }
    // 拖：按在身体上（不在头上）移动。
    {
        pet::GestureTracker g;
        pet::PointerSample s[20];
        for (int i = 0; i < 20; ++i) s[i] = {i < 19, false, true, 100 + i * 5, 100 + i * 2, 0.016f, 1.0f};
        const Counts c = feed(g, s, 20);
        check(c.dragStart == 1 && c.dragMove > 5 && c.dragEnd == 1 && c.pet == 0, "身上按住移动 = 拖动窗口");
    }
    // 1.7：不按键，光标在头上慢慢移动 = 摸；快速来回 = 打；在身上（不在头上）移动什么都不是。
    {
        pet::GestureTracker g;
        pet::PointerSample s[60];
        for (int i = 0; i < 60; ++i) s[i] = {false, true, true, 100 + i * 3, 100, 0.016f, 1.0f};
        const Counts c = feed(g, s, 60);
        check(c.pet > 30 && c.hit == 0 && c.poke == 0 && c.dragStart == 0, "不按键在头上慢慢移动 = 摸");
    }
    {
        pet::GestureTracker g;
        pet::PointerSample s[40];
        for (int i = 0; i < 40; ++i) {
            const int x = 100 + ((i / 4) % 2 == 0 ? (i % 4) * 40 : (3 - i % 4) * 40);
            s[i] = {false, true, true, x, 100, 0.016f, 1.0f};
        }
        const Counts c = feed(g, s, 40);
        check(c.hit >= 1 && c.pet == 0, "不按键在头上快速来回 = 打");
    }
    {
        pet::GestureTracker g;
        pet::PointerSample s[30];
        for (int i = 0; i < 30; ++i) s[i] = {false, false, true, 100 + i * 3, 100, 0.016f, 1.0f};
        const Counts c = feed(g, s, 30);
        check(c.pet == 0 && c.hit == 0 && c.dragStart == 0, "不按键在身上移动什么都不算");
    }
    {
        // 光标从别处一步跳到头上：第一帧不算位移，不会因为跨屏的一大步误判成打。
        pet::GestureTracker g;
        pet::PointerSample s[3];
        s[0] = {false, false, false, 10, 10, 0.016f, 1.0f};
        s[1] = {false, true, true, 900, 300, 0.016f, 1.0f};
        s[2] = {false, true, true, 902, 300, 0.016f, 1.0f};
        const Counts c = feed(g, s, 3);
        check(c.hit == 0, "跳进头部区域的第一步不算打");
    }
}

void test_bond() {
    std::printf("亲密度\n");
    pet::Bond b(30.0f);
    for (int i = 0; i < 10; ++i) b.apply(pet::BondEvent::Pet, 1.0f);
    check(b.affinity() > 32.9f && b.affinity() < 33.1f, "摸十秒只涨 3 点（一分钟内封顶 3 点）");
    b.decay(61.0f);
    b.apply(pet::BondEvent::Pet, 1.0f);
    check(b.affinity() > 34.4f, "一分钟后窗口重置，又能涨");
    const float before = b.affinity();
    for (int i = 0; i < 20; ++i) b.apply(pet::BondEvent::Hit);
    check(before - b.affinity() <= 3.01f, "连打二十下一分钟内最多掉 3 点（难掉）");
    check(b.obedient(), "打过之后处于「乖」状态");
    pet::Bond low(11.0f);
    for (int i = 0; i < 5; ++i) low.apply(pet::BondEvent::Hit);
    check(low.affinity() >= pet::Bond::kFloor, "打不穿软下限");
    pet::Bond d(50.0f);
    d.decay(600.0f);
    const float afterGrace = d.affinity();
    d.decay(3600.0f);
    check(afterGrace == 50.0f && d.affinity() < 49.5f && d.affinity() > 48.5f, "十分钟内不掉，之后每小时掉 1 点");
    pet::Bond top(99.5f);
    top.apply(pet::BondEvent::Pet, 5.0f);
    check(top.affinity() == 100.0f, "上限 100");
}

void test_save() {
    std::printf("存档\n");
    pet::SaveData s;
    s.set("name", "旺财");
    s.set_u64("seed", 123456789012345ull);
    s.set_float("affinity", 42.5f);
    const std::string text = s.serialize();
    pet::SaveData r;
    r.parse(text + "garbage line without equals\n# comment\n");
    check(r.get("name") == "旺财", "名字往返");
    check(r.get_u64("seed", 0) == 123456789012345ull, "种子往返");
    check(r.get_float("affinity", 0) > 42.49f && r.get_float("affinity", 0) < 42.51f, "亲密度往返");
    check(text.rfind("version=1", 0) == 0, "首行是版本号");
    check(!r.dirty(), "parse 之后不脏");
}

void test_personality() {
    std::printf("性格不变量\n");
    bool ok = true;
    for (std::uint64_t seed = 1; seed < 2000; ++seed) {
        const pet::Personality p = pet::personality_from_seed(seed);
        if (!(p.mischief > p.curiosity) || p.mischief >= 1.0f || p.mischief < pet::kMischiefFloor) { ok = false; break; }
    }
    check(ok, "两千个种子：捣蛋高于好奇，且不取满值");
    const pet::Personality a = pet::personality_from_seed(42), b = pet::personality_from_seed(42);
    check(std::memcmp(&a, &b, sizeof(a)) == 0, "同种子同性格");
}

void test_sound() {
    std::printf("音效合成\n");
    bool ok = true;
    for (int i = 0; i < static_cast<int>(pet::SoundId::Count); ++i) {
        const pet::PcmClip c = pet::synthesize(static_cast<pet::SoundId>(i), 1.0f, 7);
        if (c.samples.empty() || c.seconds() > 3.0f) { ok = false; break; }
        int peak = 0;
        for (auto v : c.samples) peak = v > peak ? v : (-v > peak ? -v : peak);
        // 峰值 = 30000 × 配方的 level（打呼这种小声的 level 0.3）。
        const float level = pet::sound_recipe(static_cast<pet::SoundId>(i)).level;
        if (static_cast<float>(peak) < 20000.0f * level) { ok = false; std::printf("    %s 峰值 %d\n", pet::sound_name(static_cast<pet::SoundId>(i)), peak); break; }
    }
    check(ok, "配方表里每段音效都非空、不超过 3 秒、峰值按 level 归一");
}

void test_catalog() {
    std::printf("动作目录表\n");
    bool namesOk = true;
    for (int i = 0; i < static_cast<int>(pet::ActionKind::Count); ++i) {
        const auto k = static_cast<pet::ActionKind>(i);
        if (pet::action_spec(k).id != k || pet::action_name(k)[0] == '\0') namesOk = false;
    }
    check(namesOk, "每一行的 id 与下标一致、都有名字");

    // 乖着的时候两个捣蛋动作不可选；光标不在时扑光标不可选；提醒类永远不可选。
    pet::ActionSelector sel(pet::personality_from_seed(9), 9);
    pet::ActionContext withCursor;
    withCursor.cursorInside = true;
    pet::ActionContext noCursor;
    sel.set_bond(0.5f, /*obedient=*/true);
    check(sel.score(pet::ActionKind::Pounce, withCursor, 0) < -50.0f &&
          sel.score(pet::ActionKind::FlipBowl, withCursor, 0) < -50.0f, "乖着时不捣蛋");
    sel.set_bond(0.5f, false);
    check(sel.score(pet::ActionKind::Pounce, noCursor, 0) < -50.0f &&
          sel.score(pet::ActionKind::Pounce, withCursor, 0) > -50.0f, "扑光标需要光标在");
    check(sel.score(pet::ActionKind::RemindWater, withCursor, 0) < -50.0f &&
          sel.score(pet::ActionKind::Sleep, noCursor, 0) > -50.0f, "提醒不自选，睡觉可自选");
    // 亲密度对翻肚皮的影响：低亲密显著低于高亲密。
    sel.set_bond(0.1f, false);
    const float lo = sel.score(pet::ActionKind::CharmBelly, withCursor, 0);
    sel.set_bond(0.9f, false);
    const float hi = sel.score(pet::ActionKind::CharmBelly, withCursor, 0);
    check(hi > lo + 0.3f, "亲密度高更爱翻肚皮");
}

// 假宿主：记下每次请求，可以设置拒绝；save 服务用内存。
struct FakeHost : pet::IHostServices {
    std::vector<pet::ActionKind> requests;
    std::vector<std::string> bubbles;
    bool refuse = false;
    std::string store;
    bool request_pet_action(const pet::PetActionRequest& r) override {
        if (refuse) return false;
        requests.push_back(r.action);
        bubbles.push_back(r.bubbleText ? r.bubbleText : "");
        return true;
    }
    bool read_plugin_data(const char*, std::string& out) override { out = store; return !store.empty(); }
    bool write_plugin_data(const char*, const std::string& t) override { store = t; return true; }
    std::vector<std::string> urls;
    bool fetch_text(const std::string& url, FetchDone, void*) override { urls.push_back(url); return true; }
};

void test_weather() {
    std::printf("天气\n");
    // 样本照 2026-09-05 真实响应的形状：current 前面有 current_units，daily 前面有 daily_units。
    // 1.3 的样本没带 _units，扁平查找先撞上 "temperature_2m":"°C"，线上一直「没取到」，用例却是绿的。
    const std::string js = R"({"latitude":30.25,"longitude":120.17,"timezone":"Asia/Shanghai",)"
                           R"("current_units":{"time":"iso8601","interval":"seconds","temperature_2m":"°C","weather_code":"wmo code"},)"
                           R"("current":{"time":"2026-09-05T00:30","interval":900,"temperature_2m":24.3,"weather_code":2},)"
                           R"("daily_units":{"time":"iso8601","temperature_2m_max":"°C","temperature_2m_min":"°C"},)"
                           R"("daily":{"time":["2026-09-05"],"temperature_2m_max":[28.1],"temperature_2m_min":[19.4]}})";
    double v = 0;
    const std::string cur = pet::plugins::json_object(js, "current");
    check(!cur.empty() && cur.front() == '{' && cur.back() == '}' && cur.find("current_units") == std::string::npos, "切出 current 对象");
    check(pet::plugins::json_number(cur, "temperature_2m", v) && v > 24.2 && v < 24.4, "对象内取数值");
    check(!pet::plugins::json_number(js, "temperature_2m", v), "扁平查找会撞上单位字符串（这就是 1.3 的坑）");
    check(pet::plugins::json_first_number_in_array(pet::plugins::json_object(js, "daily"), "temperature_2m_max", v) && v > 28.0, "取数组首个数值");
    const std::string line = pet::plugins::compose_weather_line("杭州", js);
    check(line == "杭州 今天多云，现在 24°，最高 28° 最低 19°", ("拼句：" + line).c_str());
    check(pet::plugins::compose_weather_line("x", "{}").empty(), "缺字段返回空");
    check(pet::plugins::json_object(js, "nothing").empty(), "没有的对象返回空");
    FakeHost host;
    pet::plugins::WeatherPlugin w;
    w.on_load(host);
    w.refresh();
    check(host.urls.empty(), "没填城市不联网");
    w.set_city("杭州");
    w.refresh();
    check(host.urls.size() == 1 && host.urls[0].find("nominatim.openstreetmap.org") != std::string::npos &&
          host.urls[0].find("%E6%9D%AD%E5%B7%9E") != std::string::npos, "填了城市才请求，先问 Nominatim，中文已编码");
    // Nominatim 的响应（2026-09-05 真实形状裁短）：数组，lat / lon 是字符串。
    const std::string nomi = R"([{"place_id":80874660,"lat":"41.8933203","lon":"12.4829321","addresstype":"city","name":"罗马","display_name":"罗马, Roma Capitale, 拉齐奥大区, 意大利"}])";
    std::string sv;
    check(pet::plugins::json_string(nomi, "lat", sv) && sv == "41.8933203", "Nominatim 的 lat 是字符串，取得到");
    check(pet::plugins::json_string(nomi, "name", sv) && sv == "罗马", "Nominatim 的中文名");
    // 同名地方：美国的柏林重要度更高，也要选德国的（1.7，作者要求优先中国和欧洲）。
    const std::string two = R"([{"lat":"44.47","lon":"-71.18","importance":0.62,"name":"柏林","address":{"country":"美国","country_code":"us"}},)"
                            R"({"lat":"52.52","lon":"13.40","importance":0.55,"name":"柏林","address":{"country":"德国","country_code":"de"}}])";
    check(pet::plugins::json_array_objects(two).size() == 2, "数组切成两个对象");
    check(pet::plugins::pick_place(two) == 1, "同名优先欧洲");
    const std::string cnFirst = R"([{"lat":"1","lon":"1","importance":0.9,"address":{"country_code":"de"}},{"lat":"2","lon":"2","importance":0.4,"address":{"country_code":"cn"}}])";
    check(pet::plugins::pick_place(cnFirst) == 1, "中国压过欧洲");
    check(pet::plugins::pick_place("[]") == -1, "空结果返回 -1");
    check(pet::action_roams(pet::ActionKind::Walk) && pet::action_roams(pet::ActionKind::Pounce) &&
          !pet::action_roams(pet::ActionKind::Sit) && !pet::action_roams(pet::ActionKind::Sleep), "离开原地的动作表");
}

void test_hearts() {
    std::printf("心形\n");
    pet::HeartFx fx;
    pet::PartPose poses[static_cast<int>(pet::Part::Count)];
    fx.apply(poses);
    check(poses[static_cast<int>(pet::Part::Heart0)].scale.x == 0.0f, "没冒心时缩放为 0");
    fx.spawn(0, 0);
    fx.update(0.3f);
    fx.apply(poses);
    check(poses[static_cast<int>(pet::Part::Heart0)].scale.x > 0.5f && poses[static_cast<int>(pet::Part::Heart0)].offset.y > 1.35f, "冒出后在飘");
    fx.update(2.0f);
    check(!fx.any_alive(), "1.4 秒后消失");
}

void test_memo() {
    std::printf("备忘录\n");
    const long long now = static_cast<long long>(std::time(nullptr));
    long long due = 0;
    check(pet::plugins::parse_due("+30", now, due) && due == now + 1800, "+30 = 30 分钟后");
    check(pet::plugins::parse_due("14:30", now, due) && due > now && due <= now + 24 * 3600, "HH:MM 落在未来 24 小时内");
    check(pet::plugins::parse_due("2030-01-02 09:00", now, due) && due > now, "完整日期时间");
    check(!pet::plugins::parse_due("abc", now, due) && !pet::plugins::parse_due("25:00", now, due) &&
          !pet::plugins::parse_due("+0", now, due), "垃圾输入被拒");

    FakeHost host;
    pet::plugins::MemoPlugin memo;
    memo.on_load(host);
    const unsigned id = memo.add("交房租", now + 1);
    check(id != 0 && memo.items().size() == 1, "添加一条");
    pet::HostClock c;
    c.wallClock = now;
    memo.on_tick(c);
    check(host.requests.empty(), "没到点不提醒");
    c.wallClock = now + 5;
    c.petBusy = true;
    memo.on_tick(c);
    check(host.requests.empty(), "宿主在忙时不提醒、也不丢");
    c.petBusy = false;
    memo.on_tick(c);
    check(host.requests.size() == 1 && host.requests[0] == pet::ActionKind::RemindMemo &&
          host.bubbles[0] == "备忘：交房租", "到点提醒，气泡带内容");
    memo.on_tick(c);
    check(host.requests.size() == 1, "同一条本次运行只提醒一次");
    memo.acknowledge(id);
    check(memo.items()[0].done, "点了狗算已看");

    // 存档往返：重新加载后条目还在，已完成状态保留。
    pet::plugins::MemoPlugin again;
    again.on_load(host);
    check(again.items().size() == 1 && again.items()[0].text == "交房租" && again.items()[0].done, "存档往返");
    check(again.remove(id) && again.items().empty(), "删除");
}

void test_tips() {
    std::printf("操作提示\n");
    FakeHost host;
    pet::plugins::TipsPlugin t(1);
    t.on_load(host);
    pet::HostClock c;
    c.nowSeconds = 10.0;
    t.on_tick(c);
    check(host.requests.empty(), "启动 3 分钟内不提示");
    c.nowSeconds = 200.0;
    t.on_tick(c);
    check(host.requests.size() == 1 && host.bubbles[0] == pet::tr(pet::Str::TipPet), "久没摸头先教摸头");
    c.nowSeconds = 201.0;
    t.on_tick(c);
    check(host.requests.size() == 1, "刚说过的不马上重复");
    c.sinceLastPetSeconds = 10.0;
    c.nowSeconds = 200.0 + 36.0 * 60.0;
    t.on_tick(c);
    check(host.requests.size() == 2 && host.bubbles[1] != pet::tr(pet::Str::TipPet), "摸过就换别的提示");
}

void test_health_two_timers() {
    std::printf("健康提醒双计时器\n");
    FakeHost host;
    pet::plugins::HealthConfig cfg;
    cfg.intervalMinutes = 30.0f;
    pet::plugins::HealthPlugin h(cfg);
    h.on_load(host);
    pet::HostClock c;
    // 每 10 秒一 tick，走 31 分钟。
    for (int s = 0; s <= 31 * 60; s += 10) { c.nowSeconds = s; h.on_tick(c); }
    check(host.requests.size() == 2 && host.requests[0] == pet::ActionKind::RemindStand &&
          host.requests[1] == pet::ActionKind::RemindWater, "第 15 分钟久坐、第 30 分钟喝水，各一次");
    // 离开 6 分钟再回来：两个计时器都重置。
    c.userIdleSeconds = 400.0f;
    c.nowSeconds += 10; h.on_tick(c);
    c.userIdleSeconds = 0.0f;
    c.nowSeconds += 10; h.on_tick(c);
    const size_t before = host.requests.size();
    for (int s = 0; s <= 14 * 60; s += 10) { c.nowSeconds += 10; h.on_tick(c); }
    check(host.requests.size() == before, "回来后 14 分钟内不提醒");
}

}  // namespace

int main() {
    test_gestures();
    test_bond();
    test_save();
    test_personality();
    test_sound();
    test_catalog();
    test_memo();
    test_health_two_timers();
    test_tips();
    test_weather();
    test_hearts();
    std::printf("\n%s：%d 项失败\n", g_failed == 0 ? "全部通过" : "有失败", g_failed);
    return g_failed == 0 ? 0 : 1;
}
