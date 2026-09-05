// CyberDog 桌宠主程序（1.0–1.7 叫 Jdog，2.0 改名；狗的默认名字仍是 Jdog）。
//
// 已完成的 P1 任务（设计文档 §6.1）：1 透明窗、2 托盘、3 模块拆分、4 代理体渲染（glTF 待接）、
// 5 命中测试、7 空闲三级停止。还没做：6 闲置回巢与遮挡、8 边界情况、9 资源实测。
// 作者要求提前的：眼神、动作、健康提醒插件、鼠标互动与亲密度、音效、出场、存档（名字、种子、亲密度）。
//
// 主循环的两种状态（设计文档 §2.2 的 Free / Docked 在窗口不动的简化版）：
//   Free    持续渲染。动作选择器按性格自己决定做什么。
//   Docked  T_idle 秒没交互后停止呈现。每 20–90 秒唤醒一次播一个微动作，播完立刻停。
//           健康提醒也走唤醒。唤醒不算交互，不会把狗拉回 Free。

#include "overlay/win32_window.h"
#include "overlay/desktop_metrics.h"
#include "overlay/d3d_context.h"
#include "overlay/tray_icon.h"
#include "overlay/idle_controller.h"
#include "overlay/mesh_renderer.h"
#include "overlay/bubble_window.h"
#include "overlay/audio.h"
#include "overlay/user_data.h"
#include "overlay/rename_dialog.h"
#include "overlay/stats_panel.h"
#include "overlay/memo_dialog.h"
#include "overlay/http.h"
#include "plugins/memo_plugin.h"
#include "plugins/weather_plugin.h"
#include "plugins/tips_plugin.h"
#include "core/hearts.h"
#include "core/i18n.h"

#include <shellapi.h>   // ShellExecuteW：托盘「打开数据目录」

#include <ctime>
#include "core/personality.h"
#include "core/proxy_mesh.h"
#include "core/proxy_pose.h"
#include "core/gaze.h"
#include "core/action.h"
#include "core/bond.h"
#include "core/interaction.h"
#include "core/save.h"
#include "core/plugin_api.h"
#include "core/rng.h"
#include "plugins/health_plugin.h"

#include <cstdio>
#include <cwchar>
#include <functional>
#include <vector>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace {

constexpr const char* kVersion = "1.0";   // CyberDog 1.0（改名前的 Jdog 版本号到 2.0 为止，作者定名后从 1.0 重新计）

// 开机自启：HKCU\Software\Microsoft\Windows\CurrentVersion\Run 下一个键。用户自己在托盘菜单里开关。
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunName[] = L"CyberDog";

bool autostart_enabled() {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    wchar_t buf[1024]{};
    DWORD sz = sizeof(buf);
    const bool has = RegQueryValueExW(k, kRunName, nullptr, nullptr, reinterpret_cast<BYTE*>(buf), &sz) == ERROR_SUCCESS;
    RegCloseKey(k);
    return has;
}

bool set_autostart(bool on) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    bool ok;
    if (on) {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";
        ok = RegSetValueExW(k, kRunName, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                            static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    } else {
        const LSTATUS r = RegDeleteValueW(k, kRunName);
        ok = r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
    }
    RegCloseKey(k);
    return ok;
}

// 没互动、没提醒时狗缩到这个比例，少挡东西（作者要求）。1.0 是原大小。
constexpr float kSmallPresence = 0.55f;
// 狗的「基准画框」：1.0/1.1 的窗口大小。1.2 起窗口是整个显示器，但狗的大小、
// 相机角度都按这个画框定，再把画框摆到原来的栖位上。
constexpr int kFrameW = 640;
constexpr int kFrameH = 340;
constexpr int kEmbedPx = 18;
constexpr int kPartCount = static_cast<int>(pet::Part::Count);
constexpr float kFovY = 0.58f;

// 基准相机：3/4 正面视角。狗朝 +Z，所以相机放在 +Z 一侧才看得到脸。
// 1.2 把相机沿这个方向拉远 H/340 倍，视角不变，狗在屏幕上的大小不变，
// 但整个屏幕都在画框里。拉远后透视更平，所以「冲屏」要蹿得更远才显得大。
const pet::Vec3 kEye{1.30f, 0.95f, 2.05f};
const pet::Vec3 kTarget{0.00f, 0.62f, 0.15f};
// 光的行进方向。z 分量必须为负：相机在 +z 一侧看狗脸，光要从相机那边打过去。
const pet::Vec3 kLightDir{-0.35f, -0.75f, -0.55f};

// 把一个点乘视图投影，返回 NDC（x 右 y 上）。w<=0 时返回 false。
bool project_ndc(const pet::Mat4& m, pet::Vec3 p, float& nx, float& ny) {
    const float cx = p.x * m.m[0][0] + p.y * m.m[1][0] + p.z * m.m[2][0] + m.m[3][0];
    const float cy = p.x * m.m[0][1] + p.y * m.m[1][1] + p.z * m.m[2][1] + m.m[3][1];
    const float cw = p.x * m.m[0][3] + p.y * m.m[1][3] + p.z * m.m[2][3] + m.m[3][3];
    if (cw <= 1e-4f) return false;
    nx = cx / cw;
    ny = cy / cw;
    return true;
}

// 系统空闲秒数。只取时长，不读任何输入内容（M8）。
float user_idle_seconds() {
    LASTINPUTINFO li{sizeof(LASTINPUTINFO), 0};
    if (!GetLastInputInfo(&li)) return 0.0f;
    return static_cast<float>(GetTickCount() - li.dwTime) / 1000.0f;
}

bool env_flag(const wchar_t* name) { return GetEnvironmentVariableW(name, nullptr, 0) != 0; }

float env_float(const wchar_t* name, float def) {
    wchar_t buf[32]{};
    if (!GetEnvironmentVariableW(name, buf, 32)) return def;
    char nb[32]{};
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, nb, 32, nullptr, nullptr);
    const float v = static_cast<float>(std::atof(nb));
    return v > 0.0f ? v : def;
}

std::wstring widen(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}
std::string narrow(const std::wstring& w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

// 把模型空间的一个轴对齐盒投影到客户区像素，返回包围矩形（外扩 pad 像素）。
RECT project_box(const pet::Mat4& world, const pet::Mat4& viewProj, pet::Vec3 lo, pet::Vec3 hi,
                 int winW, int winH, int pad) {
    const pet::Mat4 m = world * viewProj;
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    for (int c = 0; c < 8; ++c) {
        const pet::Vec3 p{(c & 1) ? hi.x : lo.x, (c & 2) ? hi.y : lo.y, (c & 4) ? hi.z : lo.z};
        float nx, ny;
        if (!project_ndc(m, p, nx, ny)) continue;
        const float px = (nx * 0.5f + 0.5f) * static_cast<float>(winW);
        const float py = (0.5f - ny * 0.5f) * static_cast<float>(winH);
        minx = std::fmin(minx, px); maxx = std::fmax(maxx, px);
        miny = std::fmin(miny, py); maxy = std::fmax(maxy, py);
    }
    if (minx > maxx) return RECT{0, 0, 0, 0};
    return RECT{static_cast<LONG>(minx) - pad, static_cast<LONG>(miny) - pad,
                static_cast<LONG>(maxx) + pad, static_cast<LONG>(maxy) + pad};
}

// 宿主给插件的服务。插件只能通过它让宠物做事。
struct HostServices final : pet::IHostServices {
    pet::ActionPlayer* player = nullptr;
    pet::win::BubbleWindow* bubble = nullptr;
    pet::win::IdleController* idle = nullptr;
    HWND hwnd = nullptr;
    UINT dpi = 96;
    POINT* headTop = nullptr;   // 狗头顶的屏幕坐标，每帧更新，气泡锚在这里
    bool* petVisible = nullptr;
    const bool* dragging = nullptr;
    std::wstring* petName = nullptr;
    ULONGLONG bubbleUntilMs = 0;
    // 延后显示的气泡（先叫一声再出字幕）。
    std::wstring pendingText;
    ULONGLONG pendingShowMs = 0;
    float pendingSeconds = 0.0f;
    ULONGLONG lastReminderMs = 0;
    unsigned accepted = 0, refused = 0;
    pet::ActionKind lastRequested = pet::ActionKind::Idle;

    // net 服务：只有天气插件用，用户没填城市时插件根本不会调。
    pet::win::HttpClient* http = nullptr;
    bool fetch_text(const std::string& url, pet::IHostServices::FetchDone done, void* user) override {
        if (!http) return false;
        std::printf("  [net] GET %s\n", url.c_str());
        http->get(url, [done, user](bool ok, const std::string& body) { done(user, ok, body); });
        return true;
    }

    // save 服务：每个插件一个文件，放用户目录。
    bool read_plugin_data(const char* pluginId, std::string& out) override {
        return pet::win::read_text_file(pet::win::plugin_data_path(pluginId), out);
    }
    bool write_plugin_data(const char* pluginId, const std::string& text) override {
        return pet::win::write_text_file(pet::win::plugin_data_path(pluginId), text);
    }

    void show_bubble(const std::wstring& text, float seconds) {
        bubble->show(*headTop, text.c_str(), dpi);
        bubbleUntilMs = GetTickCount64() + static_cast<ULONGLONG>(seconds * 1000.0f);
    }
    // 每轮循环调：到点就把延后的气泡放出来。
    void tick_bubbles(ULONGLONG now) {
        if (pendingShowMs != 0 && now >= pendingShowMs) {
            show_bubble(pendingText, pendingSeconds);
            pendingShowMs = 0;
        }
        if (bubbleUntilMs != 0 && now >= bubbleUntilMs) {
            bubble->hide();
            bubbleUntilMs = 0;
        }
    }
    ULONGLONG next_deadline() const {
        ULONGLONG d = 0;
        if (pendingShowMs) d = pendingShowMs;
        if (bubbleUntilMs && (d == 0 || bubbleUntilMs < d)) d = bubbleUntilMs;
        return d;
    }

    bool request_pet_action(const pet::PetActionRequest& req) override {
        // 拒绝条件：隐藏、全屏或投影中、正在做别的动作、正被拖拽。
        const char* why = nullptr;
        if (!*petVisible) why = "宠物隐藏";
        else if (*dragging) why = "正被拖拽";
        else if (player->active()) why = "宠物在忙";
        else if (pet::win::fullscreen_or_presenting()) why = "全屏中";
        if (why) {
            ++refused;
            std::printf("  [pet-action] 拒绝 %s（%s）\n", pet::action_name(req.action), why);
            return false;
        }
        pet::ActionContext ctx;
        if (!player->start(req.action, ctx)) { ++refused; return false; }

        const ULONGLONG now = GetTickCount64();
        lastReminderMs = now;
        lastRequested = req.action;
        idle->request_burst(now, static_cast<unsigned>((req.bubbleSeconds + req.bubbleDelaySeconds) * 1000.0f) + 1000);
        if (req.bubbleText) {
            const std::wstring text = *petName + widen(pet::tr(pet::Str::Colon)) + widen(req.bubbleText);
            if (req.bubbleDelaySeconds > 0.0f) {
                pendingText = text;
                pendingSeconds = req.bubbleSeconds;
                pendingShowMs = now + static_cast<ULONGLONG>(req.bubbleDelaySeconds * 1000.0f);
            } else {
                show_bubble(text, req.bubbleSeconds);
            }
        }
        ++accepted;
        std::printf("  [pet-action] 接受 %s：%s\n", pet::action_name(req.action),
                    req.bubbleText ? req.bubbleText : "");
        return true;
    }
};

}  // namespace

int main(int argc, char** argv) {
    // 可选参数：跑多少秒后自动退出（测试用）；T_idle 覆盖（秒）；多少秒后自动隐藏。
    const int autoExitSeconds = (argc > 1) ? std::atoi(argv[1]) : 0;
    const int idleOverrideSeconds = (argc > 2) ? std::atoi(argv[2]) : 0;
    const int autoHideSeconds = (argc > 3) ? std::atoi(argv[3]) : 0;

    // 环境变量：
    //   PET_DEBUG_MASK=1       每帧回读 alpha，退出时打字符图；打印眼神与动作切换
    //   PET_IGNORE_CURSOR=1    忽略光标位置，资源实测用
    //   PET_HEALTH_MINUTES=x   健康提醒间隔（分钟），测试时给 0.2 之类的小数
    //   PET_ACTION=n           启动后强制播第 n 号动作（见 ActionKind 顺序）
    //   PET_NO_ENTRANCE=1      跳过出场动画
    const bool debugMask = env_flag(L"PET_DEBUG_MASK");
    const bool ignoreCursor = env_flag(L"PET_IGNORE_CURSOR");
    const float healthMinutesEnv = env_float(L"PET_HEALTH_MINUTES", 0.0f);
    const int forcedAction = static_cast<int>(env_float(L"PET_ACTION", -1.0f));
    const bool noEntrance = env_flag(L"PET_NO_ENTRANCE");
    //   PET_IGNORE_IDLE=1      把系统空闲时长当 0，健康提醒不会因为「用户离开」而暂停（自动测试用）
    const bool ignoreIdle = env_flag(L"PET_IGNORE_IDLE");
    //   PET_OPEN=stats|memo|rename|weather|menu  启动 1.5 秒后打开对应窗口 / 托盘菜单（截图验界面用）
    wchar_t openAtBuf[32]{};
    GetEnvironmentVariableW(L"PET_OPEN", openAtBuf, 32);
    const std::wstring openAt = openAtBuf;
    bool openAtDone = openAt.empty();
    //   PET_DEBUG_HEARTS=1     每 0.6 秒冒一颗心（截图验心形用）
    const bool debugHearts = env_flag(L"PET_DEBUG_HEARTS");
    ULONGLONG lastDebugHeartMs = 0;

    pet::win::enable_utf8_console();
    setvbuf(stdout, nullptr, _IONBF, 0);

    // 单实例：第二次启动直接退出。测试时和作者的实例同屏跑过两条狗。
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\CyberDog.single-instance");
    if (single && GetLastError() == ERROR_ALREADY_EXISTS && !env_flag(L"PET_ALLOW_MULTI")) {
        std::printf("  已经有一个 CyberDog 在跑了，退出。\n");
        CloseHandle(single);
        return 0;
    }

    // ---- 存档 ----
    pet::SaveData save;
    const std::string savePath = pet::win::save_path();
    {
        std::string text;
        if (pet::win::read_text_file(savePath, text)) save.parse(text);
    }
    std::wstring petName = widen(save.get("name", "Jdog"));
    if (petName.empty()) petName = L"Jdog";
    std::uint64_t seed = save.get_u64("seed", 0);
    if (seed == 0) {
        // 第一次运行：种子按时间取，存下来。以后每次启动都是同一条狗（M7 的「每次安装不同」）。
        seed = GetTickCount64() ^ (static_cast<std::uint64_t>(GetCurrentProcessId()) << 32);
        save.set_u64("seed", seed);
    }
    pet::Bond bond(save.get_float("affinity", 30.0f));
    bool soundOn = save.get("sound", "1") != "0";
    // 成长记录：累计被摸 / 被打 / 玩球次数、启动次数。养成（P3）会读这些。
    unsigned long long totalPets = save.get_u64("pets_total", 0), totalHits = save.get_u64("hits_total", 0);
    unsigned long long totalBalls = save.get_u64("balls_total", 0), launches = save.get_u64("launches", 0) + 1;
    save.set_u64("launches", launches);
    // 领养日：第一次运行的日期，属性面板算「相处天数」用。
    std::string adopted = save.get("adopted", "");
    if (adopted.empty()) {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
        adopted = buf;
        save.set("adopted", adopted);
    }
    std::string weatherCity = save.get("weather_city", "");
    // 语言（1.7）：存档里有就用；没有按系统界面语言猜：中文 → zh，意大利语 → it，其余 en。
    {
        std::string langCode = save.get("lang", "");
        if (langCode.empty()) {
            const WORD primary = PRIMARYLANGID(GetUserDefaultUILanguage());
            langCode = primary == LANG_CHINESE ? "zh" : primary == LANG_ITALIAN ? "it" : "en";
            save.set("lang", langCode);
        }
        pet::set_language(pet::lang_from_code(langCode.c_str()));
    }
    // 当前语言的文案转宽字符。
    auto T = [](pet::Str s) { return widen(pet::tr(s)); };
    auto Tf = [](pet::Str s, auto... a) {
        char buf[512];
        std::snprintf(buf, sizeof(buf), pet::tr(s), a...);
        return widen(buf);
    };
    const float healthMinutes = healthMinutesEnv > 0.0f ? healthMinutesEnv : save.get_float("health_minutes", 30.0f);
    save.set("name", narrow(petName));
    // 环境变量的覆盖值不写进存档，否则测试用的 0.1 分钟会留下来。
    save.set_float("health_minutes", save.get_float("health_minutes", 30.0f));

    std::printf("=== CyberDog %s ===\n", kVersion);
    std::printf("  名字：%s   存档：%s\n", narrow(petName).c_str(), savePath.c_str());
    std::printf("  右键托盘图标：显示/隐藏、改名、声音、退出。按住身体拖动；头上慢慢摸是抚摸，快速来回是打。\n");
    if (autoExitSeconds > 0) std::printf("  %d 秒后自动退出。\n", autoExitSeconds);
    std::printf("\n");

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // 1.2 起覆盖层窗口是整个主显示器：狗不再被一个框限住，球能飞到屏幕任何地方。
    // 透明区点击穿透，所以全屏窗口不挡任何东西。
    const auto metrics = pet::win::query(nullptr);
    const int kWinW = metrics.monitor.right - metrics.monitor.left;
    const int kWinH = metrics.monitor.bottom - metrics.monitor.top;
    pet::win::OverlayWindow win;
    if (!win.create(L"CyberDog", kWinW, kWinH)) return 1;
    win.move_to({metrics.monitor.left, metrics.monitor.top});

    // 狗的基准画框仍按 1.0 的算法摆在右下角栖位，压进任务栏 18 px。
    const POINT dock = pet::win::dock_position(metrics, kFrameW, kFrameH, kEmbedPx);
    const POINT dockClient{dock.x - metrics.monitor.left, dock.y - metrics.monitor.top};
    std::printf("  显示器 %dx%d，栖位画框 %ld,%ld（DPI %u，缩放 %.0f%%）\n",
                kWinW, kWinH, dock.x, dock.y, metrics.dpi, metrics.dpi * 100.0 / 96.0);
    const float dpiScale = static_cast<float>(metrics.dpi) / 96.0f;
    // 看得见的任务栏顶边（客户区坐标）。窗口矩形顶边再往下 7 逻辑像素，见 set_clip 处的说明。
    const int taskbarVisibleTop = metrics.taskbar.top - metrics.monitor.top + static_cast<int>(7.0f * dpiScale);

    pet::gfx::D3DContext gfx;
    if (!gfx.init(win.hwnd(), static_cast<UINT>(kWinW), static_cast<UINT>(kWinH), /*enableDebugLayer=*/false)) return 1;

    const pet::Mesh mesh = pet::build_proxy_beagle();
    pet::gfx::MeshRenderer renderer;
    if (!renderer.init(gfx.device(), DXGI_FORMAT_B8G8R8A8_UNORM, static_cast<UINT>(kWinW), static_cast<UINT>(kWinH), mesh)) return 1;

    // 任务栏遮挡：渲染裁剪到任务栏顶边（设计文档 §2.2，P1 任务 6 的遮挡部分）。
    if (metrics.taskbarValid && !metrics.taskbarAutoHide && metrics.edge == pet::win::TaskbarEdge::Bottom) {
        // Win11 任务栏窗口矩形比看得见的任务栏高一条（本机 150% 下是 11 px），
        // 按窗口矩形裁会在脚和任务栏之间留一条空隙。栏杆线用「看得见的顶边」：矩形顶边再往下 7 逻辑像素。
        // 用 tools/pixel_column.ps1 量的。
        renderer.set_clip(RECT{0, 0, kWinW, taskbarVisibleTop});
    }
    std::printf("  [ok] 代理体已上传：%u 顶点 %u 索引 %zu 个部件\n",
                renderer.vertex_count(), renderer.index_count(), mesh.parts.size());

    const pet::Personality p = pet::personality_from_seed(seed);
    std::printf("  性格（邪恶比格基线 ±%.2f）捣蛋=%.2f 好奇=%.2f 卖萌=%.2f 活泼=%.2f 懒散=%.2f 胆小=%.2f\n",
                pet::kPersonalitySpread, p.mischief, p.curiosity, p.charm,
                p.liveliness, p.laziness, p.timidity);
    std::printf("  亲密度 %.1f  第 %llu 次启动  累计摸 %llu 打 %llu 玩球 %llu\n",
                bond.affinity(), launches, totalPets, totalHits, totalBalls);

    float idleSeconds = pet::idle_timeout_seconds(p);
    if (idleOverrideSeconds > 0) idleSeconds = static_cast<float>(idleOverrideSeconds);
    std::printf("  回巢阈值 %.0f 秒\n", idleSeconds);

    pet::GazeController gaze(p, seed ^ 0x5EEDull);
    pet::ActionSelector selector(p, seed ^ 0xAC710Aull);
    pet::ActionPlayer player(seed ^ 0x9AAAull);
    pet::GestureTracker gesture;
    pet::Rng rng(seed ^ 0x77ull);
    pet::PartPose poses[kPartCount];
    pet::Mat4 partWorld[kPartCount];
    bool cursorWasInside = false;
    pet::GazeMood lastMood = gaze.mood();
    pet::ActionKind lastAction = pet::ActionKind::Idle;
    float restTimer = 1.0f;
    ULONGLONG lastFrameMs = GetTickCount64();

    // 音效。活泼的狗声音略高。
    pet::win::Audio audio;
    audio.prepare(0.9f + 0.25f * p.liveliness, seed ^ 0x50DAull);
    audio.set_enabled(soundOn);
    // 22:00–08:00 不出声，作者要求，没有开关（Audio::quiet_now）。
    if (pet::win::Audio::quiet_now()) std::printf("  现在是安静时段（22:00–08:00），不出声\n");

    // 命名提醒：没改过名、也没关掉提醒时，每次启动自我介绍之后提一句。
    bool named = save.get("named", "0") == "1";
    bool nameReminder = save.get("name_reminder", "1") != "0";
    float presence = 1.0f;   // 当前显示比例，平滑过渡
    // 2.0：狗可以被上下左右拖，拖完的位置就是它的基础位置。groundY 是地面抬高多少（模型单位），
    // 0 = 站在任务栏上（栏杆效果）；离任务栏近就吸附回 0。存档 home_x / home_y。
    float groundY = save.get_float("home_y", 0.0f);
    const float homeXSaved = save.get_float("home_x", 1e9f);   // 1e9 = 没存过，用默认的右下角
    // 睡觉时整条狗抬到任务栏之上，不挡时钟（作者要求）。按上一帧的矩形逐帧收敛。
    float sleepLift = 0.0f, sleepLiftTarget = 0.0f;
    float matScale = 0.0f;   // 垫子的显示比例
    float sleepShiftX = 0.0f;   // 睡觉时为了不出屏幕往左挪的量（模型单位）

    bool petVisible = true;
    bool dragging = false;
    POINT dragOrigin{};
    RECT headRect{};
    RECT dogRect{};       // 整条狗的屏幕矩形（客户区），回读区域与「光标在附近」判定用
    RECT prevScene{};     // 上一帧画过的区域（狗 + 球、心、一滩），算脏矩形和回读范围用
    int  sceneSpeedPx = 0;     // 画面矩形上一帧动了多少像素，回读范围和窗口区域按它多留余量
    std::vector<unsigned char> partBehind(static_cast<size_t>(pet::Part::Count), 1);   // 每帧算：部件在任务栏后面吗
    RECT lastReadbackRect{};   // 上次回读时画面在哪，动了就要再读
    POINT headTop{};      // 狗头顶的屏幕坐标，气泡锚点
    bool mousePressed = false;
    POINT mousePos{};

    // ---- 相机与投影 ----
    // 相机沿基准方向拉远 kWinH/kFrameH 倍，视角不变。
    const float camK = static_cast<float>(kWinH) / static_cast<float>(kFrameH);
    const pet::Vec3 eyeBase = kTarget + (kEye - kTarget) * camK;
    const pet::Mat4 projBase = pet::perspective(kFovY, static_cast<float>(kWinW) / static_cast<float>(kWinH), 0.1f, 200.0f);
    // 用 1.0 的相机算出地面原点在基准画框里的像素位置，摆到栖位画框上，
    // 再算新投影下需要的 NDC 平移，写进投影矩阵的第 3 行（clip = ... + z·shift，即镜头平移）。
    pet::Mat4 proj = projBase;
    float ppu = 244.0f;   // 每个模型单位多少像素，下面实测
    {
        const pet::Mat4 oldVP = pet::look_at(kEye, kTarget, {0, 1, 0}) *
                                pet::perspective(kFovY, static_cast<float>(kFrameW) / static_cast<float>(kFrameH), 0.1f, 50.0f);
        float ox, oy, nx, ny;
        project_ndc(oldVP, {0, 0, 0}, ox, oy);
        const float px = static_cast<float>(dockClient.x) + (ox * 0.5f + 0.5f) * kFrameW;
        // 竖向不再按画框摆：地面原点直接放到任务栏顶边，下面再按后脚的位置微调。
        // 1.6 及以前按画框摆，脚和任务栏之间空一条（作者反馈「既没被挡又不显示狗的空间」）。
        (void)oy;
        const float py = static_cast<float>(taskbarVisibleTop);
        const float wantX = px / static_cast<float>(kWinW) * 2.0f - 1.0f;
        const float wantY = 1.0f - py / static_cast<float>(kWinH) * 2.0f;
        const pet::Mat4 newVP = pet::look_at(eyeBase, kTarget, {0, 1, 0}) * projBase;
        project_ndc(newVP, {0, 0, 0}, nx, ny);
        proj.m[2][0] += wantX - nx;
        proj.m[2][1] += wantY - ny;
        // 像素/单位：投影 (1,0,0) 与 (0,0,0) 的横向差。
        // 栏杆效果：后脚（z=-0.34）要在任务栏顶边之下 14 px（按 DPI 放大），前脚更低，被画在任务栏上面。
        // 相机俯视，后脚在画面里比原点高，所以整体还要往下推一点。
        {
            const pet::Mat4 vp1 = pet::look_at(eyeBase, kTarget, {0, 1, 0}) * proj;
            float hx, hy;
            project_ndc(vp1, {0, 0, -0.34f}, hx, hy);
            const float hindY = (0.5f - hy * 0.5f) * static_cast<float>(kWinH);
            const float wantHindY = static_cast<float>(taskbarVisibleTop) + 14.0f * dpiScale;
            proj.m[2][1] -= (wantHindY - hindY) / static_cast<float>(kWinH) * 2.0f;
        }
        const pet::Mat4 vp2 = pet::look_at(eyeBase, kTarget, {0, 1, 0}) * proj;
        float ax, ay, bx, by;
        project_ndc(vp2, {0, 0, 0}, ax, ay);
        project_ndc(vp2, {1, 0, 0}, bx, by);
        ppu = std::fabs(bx - ax) * 0.5f * static_cast<float>(kWinW);
        // 舞台范围：模型 +x 是屏幕左。两边各留 160 px。
        const float originPx = (ax * 0.5f + 0.5f) * static_cast<float>(kWinW);
        player.set_stage(-((static_cast<float>(kWinW) - originPx) - 160.0f) / ppu, (originPx - 160.0f) / ppu);
        std::printf("  舞台：%.0f 像素/单位，x ∈ [%.1f, %.1f]\n", ppu, player.stage_min(), player.stage_max());
        // 巢在右下角：舞台右端再往里 0.25（原大时右边缘离屏幕边约 80 px），就是时钟上方。
        // 用户拖过之后存档里有 home_x，就用它（夹回舞台范围）。
        const float defaultHome = player.stage_min() + 0.25f;
        const float homeX = homeXSaved > 1e8f ? defaultHome : pet::clampf(homeXSaved, player.stage_min(), player.stage_max());
        player.set_home(homeX, 0.0f);
        // 冲屏方向：舞台原点指向相机（xz 平面）。
        const float vx = eyeBase.x, vz = eyeBase.z;
        const float vn = std::sqrt(vx * vx + vz * vz);
        player.set_toward_viewer(vx / vn, vz / vn);
    }
    pet::Vec3 parallax{0, 0, 0};   // 光标视差：相机随光标微微平移

    pet::win::IdleController idle;
    {
        pet::win::IdleController::Config c;
        c.idleStopPresentMs = static_cast<unsigned>(idleSeconds * 1000.0f);
        if (idleOverrideSeconds > 0) {
            const unsigned s = static_cast<unsigned>(idleOverrideSeconds) * 1000u;
            c.hiddenReleaseSwapchainMs = s * 2;
            c.hiddenReleaseDeviceMs = s * 3;
        }
        idle.set_config(c);
    }
    idle.notify_activity(GetTickCount64());

    pet::win::BubbleWindow bubble;
    pet::win::BubbleWindow fxTag;      // 「好感度 +1」小标签，和心一起出
    ULONGLONG fxTagUntilMs = 0;
    pet::win::RenameDialog rename;
    pet::win::MemoDialog memoDialog;
    pet::win::StatsPanel statsPanel;   // 属性面板，右键狗打开
    std::function<void()> openStatsPanel;   // 托盘菜单也能开；整理行的函数在存档变量都齐了之后才定义
    pet::plugins::MemoPlugin memo;
    pet::plugins::WeatherPlugin weather;
    pet::win::HttpClient http;
    pet::HeartFx hearts;
    float affinitySeen = bond.affinity();   // 每涨 1 点冒一颗心
    HostServices host;   // 字段在插件加载前填，托盘回调里就要用到它
    host.http = &http;

    pet::win::TrayIcon tray;
    tray.set_sound_enabled(soundOn);
    tray.set_on_command([&](pet::win::TrayCommand c) {
        switch (c) {
            case pet::win::TrayCommand::Exit:
                std::printf("  托盘菜单：退出\n");
                win.request_quit();
                break;
            case pet::win::TrayCommand::ToggleVisible:
                petVisible = !petVisible;
                tray.set_pet_visible(petVisible);
                if (petVisible) win.show_no_activate(); else { win.hide(); bubble.hide(); }
                idle.set_visible(petVisible, GetTickCount64());
                std::printf("  托盘菜单：%s宠物\n", petVisible ? "显示" : "隐藏");
                break;
            case pet::win::TrayCommand::Rename:
                rename.open(petName, metrics.dpi, [&](const std::wstring& n) {
                    petName = n;
                    named = true;
                    save.set("name", narrow(petName));
                    save.set("named", "1");
                    tray.set_tip(petName.c_str());
                    host.show_bubble(Tf(pet::Str::RenamedFmt, narrow(petName).c_str()), 5.0f);
                    std::printf("  改名：%s\n", narrow(petName).c_str());
                    // 叫到名字了，抬头看你。
                    pet::ActionContext ctx;
                    if (!player.active()) player.start(pet::ActionKind::Poked, ctx);
                    idle.request_burst(GetTickCount64(), 1500);
                    audio.play(pet::SoundId::Bark);
                });
                break;
            case pet::win::TrayCommand::ToggleSound:
                soundOn = !soundOn;
                audio.set_enabled(soundOn);
                tray.set_sound_enabled(soundOn);
                save.set("sound", soundOn ? "1" : "0");
                std::printf("  声音：%s\n", soundOn ? "开" : "关");
                break;
            case pet::win::TrayCommand::Memo: {
                pet::win::MemoDialog::Callbacks cb;
                cb.list = [&]() {
                    std::vector<pet::win::MemoDialog::Row> rows;
                    const long long nowWall = static_cast<long long>(std::time(nullptr));
                    for (const auto& m : memo.items()) {
                        std::string label = (m.done ? pet::tr(pet::Str::MemoSeen) : "") + pet::plugins::format_due(m.due, nowWall) + "  " + m.text;
                        rows.push_back({m.id, widen(label)});
                    }
                    return rows;
                };
                cb.add = [&](const std::wstring& text, const std::wstring& when) -> std::wstring {
                    long long due = 0;
                    const long long nowWall = static_cast<long long>(std::time(nullptr));
                    if (!pet::plugins::parse_due(narrow(when), nowWall, due)) return T(pet::Str::MemoBadTime);
                    if (memo.add(narrow(text), due) == 0) return T(pet::Str::MemoEmpty);
                    std::printf("  备忘：%s @ %s\n", narrow(text).c_str(), pet::plugins::format_due(due, nowWall).c_str());
                    return L"";
                };
                cb.remove = [&](unsigned id) { memo.remove(id); };
                memoDialog.open(metrics.dpi, cb);
                break;
            }
            case pet::win::TrayCommand::WeatherCity:
                rename.open(widen(weatherCity), metrics.dpi, [&](const std::wstring& c) {
                    weatherCity = narrow(c);
                    save.set("weather_city", weatherCity);
                    weather.set_city(weatherCity);
                    tray.set_weather_city(c);
                    std::printf("  天气城市：%s\n", weatherCity.c_str());
                    weather.refresh();
                }, T(pet::Str::WeatherTitle).c_str(), T(pet::Str::WeatherHint).c_str(), 32);
                break;
            case pet::win::TrayCommand::ToggleAutostart: {
                const bool on = !autostart_enabled();
                if (set_autostart(on)) {
                    tray.set_autostart(on);
                    host.show_bubble(T(on ? pet::Str::AutostartOn : pet::Str::AutostartOff), 4.0f);
                    std::printf("  开机自启：%s\n", on ? "开" : "关");
                } else {
                    host.show_bubble(T(pet::Str::AutostartFail), 4.0f);
                }
                break;
            }
            case pet::win::TrayCommand::Stats:
                if (openStatsPanel) openStatsPanel();   // 在下面定义，这里只留一个钩子
                break;
            case pet::win::TrayCommand::OpenData:
                ShellExecuteW(nullptr, L"open", widen(pet::win::data_dir()).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                break;
            case pet::win::TrayCommand::LangZh:
            case pet::win::TrayCommand::LangEn:
            case pet::win::TrayCommand::LangIt: {
                const pet::Lang l = c == pet::win::TrayCommand::LangZh ? pet::Lang::Zh :
                                    c == pet::win::TrayCommand::LangEn ? pet::Lang::En : pet::Lang::It;
                pet::set_language(l);
                save.set("lang", pet::lang_code(l));
                if (statsPanel.is_open()) statsPanel.hide();
                std::printf("  语言：%s\n", pet::lang_code(l));
                break;
            }
            case pet::win::TrayCommand::ToggleNameReminder:
                nameReminder = !nameReminder;
                tray.set_name_reminder(nameReminder);
                save.set("name_reminder", nameReminder ? "1" : "0");
                if (!nameReminder && host.bubbleUntilMs != 0) { bubble.hide(); host.bubbleUntilMs = 0; }
                std::printf("  命名提醒：%s\n", nameReminder ? "开" : "关");
                break;
        }
    });
    tray.set_name_reminder(nameReminder);
    win.set_message_hook([&tray](HWND h, UINT m, WPARAM w, LPARAM l, LRESULT& r) {
        return tray.handle_message(h, m, w, l, r);
    });
    if (!tray.create(win.hwnd(), petName.c_str())) return 1;
    tray.set_dpi_scale(dpiScale);
    tray.set_weather_city(widen(weatherCity));   // 1.5 及以前启动时没同步，菜单一直显示「未设」

    win.set_hit_test([&gfx](int x, int y) {
        const auto& mask = gfx.alpha_mask();
        if (!mask.valid()) return false;   // 掩码没准备好就穿透，不能把整个屏幕当成狗
        return mask.opaque_at(x, y);
    });
    win.set_hand_cursor(true);

    // 边界 3、5：锁屏时当作隐藏（停 tick、释放）；解锁恢复。显示变化就重启自己——
    // 窗口尺寸、交换链、投影、舞台范围全和显示器绑定，重建比逐项改可靠。
    bool lockedHidden = false;
    bool restartRequested = false;
    win.set_session_handler([&](const pet::win::OverlayWindow::SessionEvent& e) {
        using T = pet::win::OverlayWindow::SessionEvent::Type;
        if (e.type == T::Locked && petVisible && !lockedHidden) {
            lockedHidden = true;
            win.hide();
            bubble.hide();
            idle.set_visible(false, GetTickCount64());
            std::printf("  锁屏：暂停\n");
        } else if (e.type == T::Unlocked && lockedHidden) {
            lockedHidden = false;
            win.show_no_activate();
            idle.set_visible(true, GetTickCount64());
            std::printf("  解锁：恢复\n");
        } else if (e.type == T::DisplayChanged) {
            restartRequested = true;
        }
    });
    // 启动自检：覆盖层不许是前台窗口，且必须带 WS_EX_NOACTIVATE（技能包 §2「交互怎么验」第 1 条）。
    {
        const LONG_PTR ex = GetWindowLongPtrW(win.hwnd(), GWL_EXSTYLE);
        const bool noActivate = (ex & WS_EX_NOACTIVATE) != 0;
        const bool notForeground = GetForegroundWindow() != win.hwnd();
        if (!noActivate || !notForeground) {
            std::printf("  [FAIL] 覆盖层自检：WS_EX_NOACTIVATE=%d 非前台=%d。这个状态会让用户操作不了别的窗口\n",
                        noActivate, notForeground);
        } else if (debugMask) {
            std::printf("  [ok] 覆盖层自检：不激活、非前台\n");
        }
    }
    bool cancelRequested = false;
    // 最近一次真互动（按下鼠标）的时刻。之后 60 秒内狗可以离开原地闲逛、玩球、扑光标；
    // 过了就只做原地动作，待在右下角。1.6，作者要求「不互动就在右下角待着」。
    ULONGLONG lastRealInteractionMs = 0;   // 0 = 还没人碰过，启动后就待在巢里
    constexpr ULONGLONG kRoamAfterInteractionMs = 60000;
    win.set_mouse_handler([&](const pet::win::OverlayWindow::MouseEvent& e) {
        using T = pet::win::OverlayWindow::MouseEvent::Type;
        mousePos = {e.x, e.y};
        if (e.type == T::Down) { mousePressed = true; lastRealInteractionMs = GetTickCount64(); }
        if (e.type == T::Up) mousePressed = false;
        if (e.type == T::RightDown) { mousePressed = false; cancelRequested = true; }
    });

    // 窗口区域 = alpha 掩码里有内容的格子。区域外不属于本窗口，点击落到下面的程序。
    // 启动时区域为空：掩码还没回读，整个屏幕都不该是狗。
    win.set_region(CreateRectRgn(0, 0, 0, 0));
    unsigned regionFromReadback = 0;
    auto rebuild_region = [&]() {
        const auto& mk = gfx.alpha_mask();
        if (!mk.valid()) return;
        // 区域比画面宽一圈：格子取「任何 alpha」而不是 24 以上，再向外膨胀一格（8 px）。
        // 原因：区域来自 66 ms 前的回读，狗一动，前沿的新像素还在区域外，会被裁成锯齿；
        // 半透明的边缘像素也会被 24 的阈值切掉。多出来的那一圈只影响点击（点在那圈里会被狗吃掉），
        // 不影响画面。命中测试仍用 24 的阈值，所以那一圈里点不到狗，也不会穿透——可接受。
        // 动得快时多膨胀几格：区域是回读那一帧的，贴到窗口上时画面又走了 sceneSpeedPx。
        const int dil = 1 + (std::min)(3, sceneSpeedPx / mk.block);
        auto solid = [&](int cx, int cy) {
            for (int dy = -dil; dy <= dil; ++dy)
                for (int dx = -dil; dx <= dil; ++dx) {
                    const int x = cx + dx, y = cy + dy;
                    if (x < 0 || y < 0 || x >= mk.cols || y >= mk.rows) continue;
                    if (mk.cell[static_cast<size_t>(y) * mk.cols + x] >= 1) return true;
                }
            return false;
        };
        // 每行把相邻的实心格子并成一个矩形，矩形数从几百降到几十。
        std::vector<RECT> rects;
        for (int cy = 0; cy < mk.rows; ++cy) {
            int runStart = -1;
            for (int cx = 0; cx <= mk.cols; ++cx) {
                const bool on = cx < mk.cols && solid(cx, cy);
                if (on && runStart < 0) runStart = cx;
                if (!on && runStart >= 0) {
                    rects.push_back(RECT{runStart * mk.block, cy * mk.block, cx * mk.block, (cy + 1) * mk.block});
                    runStart = -1;
                }
            }
        }
        HRGN rgn = CreateRectRgn(0, 0, 0, 0);
        if (!rects.empty()) {
            const size_t bytes = sizeof(RGNDATAHEADER) + rects.size() * sizeof(RECT);
            std::vector<unsigned char> buf(bytes);
            auto* rd = reinterpret_cast<RGNDATA*>(buf.data());
            rd->rdh.dwSize = sizeof(RGNDATAHEADER);
            rd->rdh.iType = RDH_RECTANGLES;
            rd->rdh.nCount = static_cast<DWORD>(rects.size());
            rd->rdh.nRgnSize = 0;
            rd->rdh.rcBound = RECT{0, 0, kWinW, kWinH};
            std::memcpy(rd->Buffer, rects.data(), rects.size() * sizeof(RECT));
            if (HRGN r2 = ExtCreateRegion(nullptr, static_cast<DWORD>(bytes), rd)) { DeleteObject(rgn); rgn = r2; }
        }
        win.set_region(rgn);
    };
    win.show_no_activate();

    // ---- 插件：健康提醒 ----
    host.player = &player;
    host.bubble = &bubble;
    host.idle = &idle;
    host.hwnd = win.hwnd();
    host.dpi = metrics.dpi;
    host.headTop = &headTop;
    headTop = {dock.x + kFrameW * 3 / 4, dock.y + kFrameH / 6};   // 第一帧之前的估计值
    host.petVisible = &petVisible;
    host.dragging = &dragging;
    host.petName = &petName;

    pet::plugins::HealthConfig hc;
    hc.intervalMinutes = healthMinutes;
    pet::plugins::HealthPlugin health(hc);
    health.on_load(host);
    std::printf("  [ok] 插件 %s：每 %.1f 分钟提醒，空闲 %.0f 秒算离开\n",
                health.id(), hc.intervalMinutes, hc.awayAfterSeconds);
    memo.on_load(host);
    pet::plugins::TipsPlugin tips(seed ^ 0x7195ull);
    tips.on_load(host);
    ULONGLONG lastPetMs = 0;   // 上次被摸；0 = 本次运行还没摸过
    std::printf("  [ok] 插件 %s：%zu 条备忘\n", memo.id(), memo.items().size());
    weather.on_load(host);
    weather.set_city(weatherCity);
    std::printf("  [ok] 插件 %s：%s\n\n", weather.id(), weatherCity.empty() ? "未设城市，不联网" : weatherCity.c_str());
    bool weatherRequested = false;

    ULONGLONG lastReadback = 0;
    ULONGLONG lastHealthTickMs = 0;
    ULONGLONG lastFullscreenCheckMs = 0;
    bool fullscreenHidden = false;
    ULONGLONG nextMicroMs = 0;
    ULONGLONG lastSaveMs = GetTickCount64();
    unsigned frames = 0, microActions = 0, pets = 0, hits = 0, pokes = 0;
    unsigned long long idleWakes = 0;
    const ULONGLONG startMs = GetTickCount64();
    const ULONGLONG deadline =
        autoExitSeconds > 0 ? startMs + static_cast<ULONGLONG>(autoExitSeconds) * 1000ull : 0;

    // 出场：从任务栏下面钻上来，抖一下，看你一眼，叫一声（§2.6）。
    // 出场完自我介绍；没起过名就再提一句可以改名（可在托盘菜单关掉这条提醒）。
    int introStage = 0;   // 0 等出场结束 1 已介绍 2 已提醒命名
    // 一开始就在巢（右下角），不在舞台中间（作者要求，1.7）。出场动作会把位置清零，所以在它之后设。
    if (!noEntrance && forcedAction < 0) {
        pet::ActionContext ctx;
        player.start(pet::ActionKind::Entrance, ctx);
        lastAction = pet::ActionKind::Entrance;
        player.drag_to(player.home_x());
    } else {
        player.drag_to(player.home_x());
    }
    ULONGLONG lastTopmostMs = 0;
    bool sleepingDocked = false;

    auto tick_health = [&](ULONGLONG now) {
        pet::HostClock clock;
        clock.nowSeconds = static_cast<double>(now - startMs) / 1000.0;
        clock.wallClock = static_cast<long long>(std::time(nullptr));
        clock.userIdleSeconds = ignoreIdle ? 0.0f : user_idle_seconds();
        clock.petVisible = petVisible;
        clock.petBusy = player.active() || dragging;
        clock.sinceLastPetSeconds = lastPetMs ? static_cast<double>(now - lastPetMs) / 1000.0 : 1e9;
        clock.sinceLastInteractionSeconds = lastRealInteractionMs ? static_cast<double>(now - lastRealInteractionMs) / 1000.0 : 1e9;
        health.on_tick(clock);
        tips.on_tick(clock);
        memo.on_tick(clock);
        weather.on_tick(clock);
        lastHealthTickMs = now;
    };
    auto flush_save = [&](bool force) {
        save.set_float("affinity", bond.affinity());
        save.set_u64("pets_total", totalPets);
        save.set_u64("hits_total", totalHits);
        save.set_u64("balls_total", totalBalls);
        if (!save.dirty() && !force) return;
        if (pet::win::write_text_file(savePath, save.serialize())) save.clear_dirty();
    };
    // 属性面板：把存档、亲密度、性格整理成行。每次打开重新整理，所以总是最新的。
    auto open_stats_panel = [&]() {
        using Row = pet::win::StatsPanel::Row;
        std::vector<Row> rows;
        SYSTEMTIME st{};
        GetLocalTime(&st);
        wchar_t buf[96];
        auto fmt = [&](const wchar_t* f, auto... a) { std::swprintf(buf, 96, f, a...); return std::wstring(buf); };
        auto days_since = [&](const std::string& ymd) -> long long {
            unsigned y = 0, m = 0, d = 0;
            if (sscanf_s(ymd.c_str(), "%u-%u-%u", &y, &m, &d) != 3) return 0;
            SYSTEMTIME a{};
            a.wYear = static_cast<WORD>(y); a.wMonth = static_cast<WORD>(m); a.wDay = static_cast<WORD>(d);
            SYSTEMTIME b = st;
            b.wHour = b.wMinute = b.wSecond = b.wMilliseconds = 0;
            FILETIME fa{}, fb{};
            SystemTimeToFileTime(&a, &fa);
            SystemTimeToFileTime(&b, &fb);
            const long long ta = (static_cast<long long>(fa.dwHighDateTime) << 32) | fa.dwLowDateTime;
            const long long tb = (static_cast<long long>(fb.dwHighDateTime) << 32) | fb.dwLowDateTime;
            return (tb - ta) / (10000000LL * 86400LL);
        };
        using pet::Str;
        const Str tierId = bond.affinity() < 20 ? Str::Tier0 : bond.affinity() < 40 ? Str::Tier1 :
                           bond.affinity() < 60 ? Str::Tier2 : bond.affinity() < 80 ? Str::Tier3 : Str::Tier4;
        std::wstring status;
        if (player.current() == pet::ActionKind::Sleep) status = T(Str::StatusSleep);
        else if (player.active()) status = widen(pet::action_name_tr(player.current()));
        else status = T(Str::StatusIdle);
        if (bond.obedient()) status += Tf(Str::StatusObedientFmt, bond.obedient_seconds());

        rows.push_back({T(Str::HeadBasic), L"", -1, true});
        rows.push_back({T(Str::RowName), petName});
        rows.push_back({T(Str::RowBreed), T(Str::Breed)});
        rows.push_back({T(Str::RowToday), fmt(L"%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay)});
        rows.push_back({T(Str::RowAdopted), widen(adopted) + Tf(Str::AdoptedDaysFmt, days_since(adopted) + 1)});
        rows.push_back({T(Str::RowStatus), status});
        rows.push_back({T(Str::HeadAffinity), L"", -1, true});
        rows.push_back({T(Str::RowAffinity), fmt(L"%.1f", bond.affinity()), bond.affinity01(), false, true});
        rows.push_back({T(Str::RowStage), T(tierId)});
        rows.push_back({T(Str::HeadPersonality), L"", -1, true});
        const struct { Str n; float v; } traits[] = {
            {Str::TraitMischief, p.mischief}, {Str::TraitCuriosity, p.curiosity}, {Str::TraitCharm, p.charm}, {Str::TraitExtroversion, p.extroversion},
            {Str::TraitLiveliness, p.liveliness}, {Str::TraitClinginess, p.clinginess}, {Str::TraitLaziness, p.laziness}, {Str::TraitTimidity, p.timidity}};
        for (const auto& t : traits) rows.push_back({T(t.n), fmt(L"%.0f%%", t.v * 100.0f), t.v});
        rows.push_back({T(Str::HeadGrowth), L"", -1, true});
        rows.push_back({T(Str::RowLaunch), Tf(Str::TimesFmt, launches)});
        rows.push_back({T(Str::RowPetsHits), Tf(Str::PetsHitsFmt, totalPets, totalHits)});
        rows.push_back({T(Str::RowBalls), Tf(Str::TimesFmt, totalBalls)});
        rows.push_back({T(Str::RowSave), widen(pet::win::data_dir())});

        RECT anchor = dogRect;
        RECT wr{};
        GetWindowRect(win.hwnd(), &wr);
        OffsetRect(&anchor, wr.left, wr.top);
        statsPanel.show(anchor, Tf(Str::StatsTitleFmt, narrow(petName).c_str()), rows, metrics.dpi);
    };
    openStatsPanel = open_stats_panel;
    auto play_sound = [&](int id, bool mischief) {
        if (id < 0 || id >= static_cast<int>(pet::SoundId::Count)) return;
        audio.play(static_cast<pet::SoundId>(id), mischief);
    };

    while (!win.quit_requested()) {
        if (!win.pump()) break;
        if (win.quit_requested()) break;
        if (deadline != 0 && GetTickCount64() >= deadline) {
            std::printf("  到时自动退出\n");
            break;
        }

        const ULONGLONG now = GetTickCount64();

        if (autoHideSeconds > 0 && petVisible &&
            now >= startMs + static_cast<ULONGLONG>(autoHideSeconds) * 1000ull) {
            petVisible = false;
            tray.set_pet_visible(false);
            win.hide();
            bubble.hide();
            idle.set_visible(false, now);
            std::printf("  自动隐藏（测试用）\n");
        }
        host.tick_bubbles(now);
        if (!openAtDone && now - startMs >= 1500) {
            openAtDone = true;
            if (openAt == L"stats") openStatsPanel();
            else if (openAt == L"memo") tray.invoke(pet::win::TrayCommand::Memo);
            else if (openAt == L"rename") tray.invoke(pet::win::TrayCommand::Rename);
            else if (openAt == L"weather") tray.invoke(pet::win::TrayCommand::WeatherCity);
            else if (openAt == L"menu") PostMessageW(win.hwnd(), WM_APP + 1, 1, WM_RBUTTONUP);   // 托盘消息号见 tray_icon.cpp
        }
        if (fxTagUntilMs != 0 && (now >= fxTagUntilMs || !petVisible)) { fxTag.hide(); fxTagUntilMs = 0; }
        audio.tick(now);
        http.drain();

        if (restartRequested) {
            std::printf("  显示器变了，重启自己\n");
            break;
        }

        // 边界 2：全屏应用或投影时整窗隐藏、释放；退出全屏后回来。每 2 秒查一次。
        if (now - lastFullscreenCheckMs >= 2000) {
            lastFullscreenCheckMs = now;
            const bool fs = pet::win::fullscreen_or_presenting();
            if (fs && petVisible && !fullscreenHidden && !lockedHidden) {
                fullscreenHidden = true;
                win.hide();
                bubble.hide();
                idle.set_visible(false, now);
                std::printf("  全屏应用：隐藏\n");
            } else if (!fs && fullscreenHidden) {
                fullscreenHidden = false;
                if (petVisible && !lockedHidden) { win.show_no_activate(); idle.set_visible(true, now); }
                std::printf("  全屏结束：恢复\n");
            }
        }
        // 启动天气：自我介绍说完（约 9 秒）再查，避免和命名提醒抢气泡。
        if (!weatherRequested && !weatherCity.empty() && now - startMs >= 9000) {
            weatherRequested = true;
            weather.refresh();
        }

        // 始终在最上层：有些程序会把自己设成置顶盖住我们，每 2 秒重新贴一次。不激活。
        if (petVisible && now - lastTopmostMs >= 10000) {
            SetWindowPos(win.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            lastTopmostMs = now;
        }

        // 自我介绍与命名提醒：出场结束后依次来。
        if (introStage == 0 && petVisible && !player.active() && now - startMs >= 3000) {
            introStage = 1;
            host.show_bubble(Tf(pet::Str::IntroFmt, narrow(petName).c_str()), 5.0f);
            if (!named && nameReminder) {
                host.pendingText = T(pet::Str::NameReminder);
                host.pendingSeconds = 12.0f;
                host.pendingShowMs = now + 5500;
            }
            // 不申请唤醒：启动后本来就在 T_idle 之内持续渲染。申请了反而会被当成停靠态，
            // 动作选择停摆 18 秒——第一版就是这么错的。
        }
        // 存档节流：最少 30 秒一次，不在渲染里同步写。
        if (now - lastSaveMs >= 30000) { flush_save(false); lastSaveMs = now; }

        // 光标到狗附近算交互，把降级计时推回去。按着鼠标也算。
        // 窗口是整个屏幕，所以「附近」= 狗的屏幕矩形外扩 140 px，不是窗口矩形。
        POINT cur{};
        RECT wr{};
        GetCursorPos(&cur);
        GetWindowRect(win.hwnd(), &wr);
        const POINT curClient{cur.x - wr.left, cur.y - wr.top};
        // 按键状态以系统为准：按下期间光标可能离开狗，窗口收不到 WM_LBUTTONUP（见 win32_window.cpp）。
        if (mousePressed) {
            if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) mousePressed = false;
            mousePos = curClient;
        }
        // 保险：万一什么地方捕获了鼠标，立刻放掉。覆盖层永远不许捕获。
        if (GetCapture() == win.hwnd()) ReleaseCapture();
        RECT nearRect = dogRect;
        InflateRect(&nearRect, 140, 140);
        const bool cursorOnScreen = PtInRect(&wr, cur) != 0;
        const bool cursorInside = !ignoreCursor && cursorOnScreen && PtInRect(&nearRect, curClient);
        if (cursorInside || mousePressed) idle.notify_activity(now);

        // 睡着时有人来就醒：先伸个懒腰。
        if ((cursorInside || mousePressed) && player.current() == pet::ActionKind::Sleep) {
            player.cancel();
            pet::ActionContext c;
            player.start(pet::ActionKind::Stretch, c);
            lastAction = pet::ActionKind::Stretch;
            sleepingDocked = false;
        }

        if (forcedAction >= 0 && now - startMs >= 1000 && !player.active() && lastAction == pet::ActionKind::Idle) {
            pet::ActionContext fctx;
            fctx.cursorInside = true;
            fctx.cursorStageX = 0.4f;
            fctx.cursorYaw = -0.3f;
            const auto k = static_cast<pet::ActionKind>(forcedAction);
            if (player.start(k, fctx)) { lastAction = k; std::printf("  强制动作：%s\n", pet::action_name(k)); }
        }

        // 停靠态的微动作与健康提醒：不渲染时也要按时醒来。
        const pet::win::PowerState prev = idle.state();
        pet::win::PowerState st = idle.update(now);
        if (st == pet::win::PowerState::PresentStopped && petVisible) {
            if (nextMicroMs == 0) {
                nextMicroMs = now + static_cast<ULONGLONG>(
                    pet::micro_action_interval_seconds(p, rng.unit()) * 1000.0f);
                const pet::DogState& d = player.dog();
                const float hx = d.x - player.home_x(), hz = d.z - player.home_z();
                if (!player.active() && (std::fabs(hx) > 0.15f || std::fabs(hz) > 0.15f)) {
                    // 回巢（设计文档 §2.2）：不在巢就先走回去，走完再决定睡不睡。巢在屏幕右下角。
                    pet::ActionContext c;
                    if (player.start(pet::ActionKind::ReturnHome, c)) {
                        lastAction = pet::ActionKind::ReturnHome;
                        idle.request_burst(now, static_cast<unsigned>((std::sqrt(hx * hx + hz * hz) / 0.8f + 2.0f) * 1000.0f));
                        nextMicroMs = now + 100;   // 走完立刻再进这里一次，做睡觉判定
                        st = idle.update(now);
                    }
                } else if (!player.active() && rng.chance(0.35f + 0.5f * p.laziness)) {
                    // 刚进入停靠：懒的狗大概率趴下睡觉。渲染一小段把趴下的姿势画出来再停。
                    player.hold(pet::ActionKind::Sleep, 3600.0f);
                    sleepingDocked = true;
                    idle.request_burst(now, 1500);
                    st = idle.update(now);
                }
            }
            if (now >= nextMicroMs) {
                unsigned dur = 1200;
                if (sleepingDocked) {
                    // 睡着的微动作：醒来画一小段（呼吸、打呼），不换动作。
                    dur = 1400;
                } else if (!player.active()) {
                    const float r = rng.unit();
                    pet::ActionContext mctx;
                    if (r < 0.25f) { player.start(pet::ActionKind::Shake, mctx); dur = 1500; }
                    else if (r < 0.40f) { player.start(pet::ActionKind::Stretch, mctx); dur = 2600; }
                    else if (r < 0.55f) { player.hold(pet::ActionKind::Sleep, 3600.0f); sleepingDocked = true; dur = 1500; }
                }
                idle.request_burst(now, dur);
                nextMicroMs = now + static_cast<ULONGLONG>(
                    pet::micro_action_interval_seconds(p, rng.unit()) * 1000.0f);
                ++microActions;
                st = idle.update(now);
            }
            if (now - lastHealthTickMs >= 30000) tick_health(now);
        } else {
            nextMicroMs = 0;
        }

        if (st != prev) {
            if (debugMask) std::printf("  电源状态：%s -> %s\n", pet::win::power_state_name(prev),
                                       pet::win::power_state_name(st));
            if (st == pet::win::PowerState::SwapchainReleased) gfx.release_swapchain();
            if (st == pet::win::PowerState::DeviceReleased) {
                // 渲染器持有深度缓冲（全屏时 16 MB）和顶点缓冲，属于设备，要一起放。
                // 1.4 实测发现释放态还留着 23 MB 显存，就是它。
                renderer.release();
                gfx.release_device();
            }
            if (st == pet::win::PowerState::Rendering && !gfx.has_swapchain()) {
                if (!gfx.ensure_ready()) { std::printf("  [FAIL] 醒来时重建图形资源失败\n"); break; }
            }
            if (st == pet::win::PowerState::Rendering && !renderer.ready()) {
                if (!renderer.init(gfx.device(), DXGI_FORMAT_B8G8R8A8_UNORM, static_cast<UINT>(kWinW), static_cast<UINT>(kWinH), mesh)) {
                    std::printf("  [FAIL] 醒来时重建渲染器失败\n");
                    break;
                }
            }
            if (st == pet::win::PowerState::Rendering) lastFrameMs = now;
        }

        if (st != pet::win::PowerState::Rendering) {
            DWORD wait = idle.wait_timeout_ms(now);
            auto capBy = [&](ULONGLONG at) {
                if (at <= now) { wait = 0; return; }
                const DWORD d = static_cast<DWORD>(at - now);
                if (wait == INFINITE || d < wait) wait = d;
            };
            if (deadline != 0) capBy(deadline);
            if (autoHideSeconds > 0 && petVisible) capBy(startMs + static_cast<ULONGLONG>(autoHideSeconds) * 1000ull);
            if (nextMicroMs != 0) capBy(nextMicroMs);
            if (petVisible) capBy(lastHealthTickMs + 30000);
            if (host.next_deadline() != 0) capBy(host.next_deadline());
            if (fxTagUntilMs != 0) capBy(fxTagUntilMs);
            if (save.dirty()) capBy(lastSaveMs + 30000);
            if (http.inflight() > 0 || (!weatherRequested && !weatherCity.empty())) capBy(now + 500);
            if (petVisible) capBy(lastTopmostMs + 10000);
            capBy(lastFullscreenCheckMs + 2000);
            ++idleWakes;
            pet::win::wait_message_timeout(wait);
            continue;
        }

        // ---- 渲染态 ----
        // 回读时机：光标在附近，或者画面动了（窗口区域要跟着画面走，否则旧位置挡点击、新位置点不到）。
        // 范围是上一帧画过的全部，不只是狗：窗口区域还会裁画面，1.4 只读狗的矩形，
        // 心飘出头顶、球踢远之后就落在区域外，被裁没了。动得快时读得更勤、范围更宽。
        const RECT& sceneNow = prevScene;
        const bool sceneMoved = std::abs(sceneNow.left - lastReadbackRect.left) > 6 || std::abs(sceneNow.top - lastReadbackRect.top) > 6 ||
                                std::abs(sceneNow.right - lastReadbackRect.right) > 6 || std::abs(sceneNow.bottom - lastReadbackRect.bottom) > 6;
        const ULONGLONG readbackGapMs = sceneMoved ? 33 : 66;
        if ((cursorInside || debugMask || sceneMoved) && now - lastReadback >= readbackGapMs) {
            RECT rr = sceneNow;
            UnionRect(&rr, &rr, &lastReadbackRect);   // 旧位置也要读，才能把旧格子清掉
            const int margin = 24 + 2 * sceneSpeedPx;   // 拷贝发生在这一帧画完之后，画面又动了一帧
            InflateRect(&rr, margin, margin);
            gfx.request_alpha_readback(sceneNow.right > sceneNow.left ? &rr : nullptr);
            lastReadback = now;
            lastReadbackRect = sceneNow;
        }
        if (gfx.readback_count() != regionFromReadback) {
            regionFromReadback = gfx.readback_count();
            rebuild_region();
        }

        // 右键：放开一切互动，狗回到站姿，然后开 / 关属性面板（1.6，作者要求）。
        if (cancelRequested) {
            cancelRequested = false;
            dragging = false;
            if (player.active()) player.cancel();
            if (statsPanel.is_open()) statsPanel.hide();
            else open_stats_panel();
            std::printf("  右键：取消互动，属性面板%s\n", statsPanel.is_open() ? "打开" : "关闭");
        }

        const float dt = pet::clampf(static_cast<float>(now - lastFrameMs) / 1000.0f, 0.0f, 0.1f);
        lastFrameMs = now;
        const float t = static_cast<float>(now - startMs) / 1000.0f;

        if (now - lastHealthTickMs >= 1000) tick_health(now);
        bond.decay(dt);
        selector.set_bond(bond.affinity01(), bond.obedient());
        if (mousePressed || dragging) lastRealInteractionMs = now;
        selector.set_roam_allowed(lastRealInteractionMs != 0 && now - lastRealInteractionMs < kRoamAfterInteractionMs);
        // 好感度每涨满 1 点冒一颗心。
        hearts.update(dt);
        while (bond.affinity() >= affinitySeen + 1.0f) {
            affinitySeen += 1.0f;
            hearts.spawn(player.dog().x, player.dog().z);
            // 字幕和心同步：红色「好感度 +1」，挂在头顶左上，1.5 秒。
            // 放左边是给心让路：心从头顶正上方往上飘，标签摆在右上时正好把心盖住（截图验出来的）。
            const POINT at{headTop.x - static_cast<int>(120 * dpiScale), headTop.y - static_cast<int>(20 * dpiScale)};
            fxTag.show(at, T(pet::Str::AffinityUp).c_str(), metrics.dpi, pet::win::BubbleWindow::Style::Tag);
            fxTagUntilMs = now + 1500;
        }
        if (bond.affinity() < affinitySeen) affinitySeen = bond.affinity();
        // 调试：PET_DEBUG_HEARTS=1 每 0.6 秒冒一颗心，用来截图验心形本身画不画得出来。
        if (debugHearts && now - lastDebugHeartMs >= 600) {
            lastDebugHeartMs = now;
            hearts.spawn(player.dog().x, player.dog().z);
        }

        // ---- 鼠标手势 ----
        {
            pet::PointerSample ps;
            ps.pressed = mousePressed;
            ps.x = mousePos.x;
            ps.y = mousePos.y;
            ps.dt = dt;
            ps.dpiScale = dpiScale;
            const bool onHead = PtInRect(&headRect, mousePos) != 0;
            const auto& mask = gfx.alpha_mask();
            ps.onBody = onHead || (mask.valid() && mask.opaque_at(mousePos.x, mousePos.y));
            ps.onHead = onHead;
            const pet::GestureEvent ev = gesture.update(ps);
            switch (ev.g) {
                case pet::Gesture::PetTick:
                    lastPetMs = now;
                    bond.apply(pet::BondEvent::Pet, ev.amount);
                    player.hold(pet::ActionKind::Petted, 0.6f);
                    break;
                case pet::Gesture::Hit:
                    ++hits;
                    ++totalHits;
                    bond.apply(pet::BondEvent::Hit);
                    { pet::ActionContext c; player.start(pet::ActionKind::Cower, c); }
                    std::printf("  打了一下。亲密度 %.1f（一分钟内第 %u 次）\n", bond.affinity(), bond.hits_last_minute());
                    break;
                case pet::Gesture::Poke:
                    ++pokes;
                    bond.apply(pet::BondEvent::Poke);
                    // 备忘提醒期间点它一下 = 看到了。
                    if (host.lastRequested == pet::ActionKind::RemindMemo && host.bubbleUntilMs != 0 && memo.last_fired_id() != 0) {
                        memo.acknowledge(memo.last_fired_id());
                        bond.apply(pet::BondEvent::Reminded);
                        host.show_bubble(T(pet::Str::SeenIt), 2.5f);
                        host.lastRequested = pet::ActionKind::Idle;
                        if (player.current() == pet::ActionKind::RemindMemo) player.cancel();
                    }
                    if (!player.active() || player.current() == pet::ActionKind::Petted) {
                        pet::ActionContext c; player.start(pet::ActionKind::Poked, c);
                    }
                    break;
                case pet::Gesture::DragStart:
                    dragging = true;
                    dragOrigin = cur;
                    break;
                case pet::Gesture::DragMove:
                    if (dragging) {
                        // 拖狗：按光标的位移移动狗，偏移不累积（V-P1-2）。屏幕右 = 模型 -x，屏幕上 = 地面抬高。
                        player.drag_to(player.dog().x - static_cast<float>(cur.x - dragOrigin.x) / ppu);
                        groundY = pet::clampf(groundY + static_cast<float>(dragOrigin.y - cur.y) / ppu, 0.0f,
                                              static_cast<float>(kWinH) / ppu - 1.6f);
                        dragOrigin = cur;
                    }
                    break;
                case pet::Gesture::DragEnd:
                    dragging = false;
                    // 拖完的位置就是基础位置；离任务栏近（40 逻辑像素内）就吸附回任务栏上。
                    if (groundY * ppu < 40.0f * dpiScale) groundY = 0.0f;
                    player.set_home(player.dog().x, 0.0f);
                    save.set_float("home_x", player.dog().x);
                    save.set_float("home_y", groundY);
                    std::printf("  拖到 x=%.2f 抬高 %.2f，作为新的巢\n", player.dog().x, groundY);
                    break;
                case pet::Gesture::Release:
                    if (gesture.petting()) {}
                    if (player.current() == pet::ActionKind::Petted) { ++pets; ++totalPets; }
                    break;
                default: break;
            }
        }

        // 舞台上下文：光标换算到舞台 x（屏幕右 = -x），偏航按「狗到光标脚下那点」算。
        pet::ActionContext ctx;
        ctx.cursorInside = cursorInside;
        float originPx = 0.0f;
        {
            float ax, ay;
            project_ndc(pet::look_at(eyeBase, kTarget, {0, 1, 0}) * proj, {0, 0, 0}, ax, ay);
            originPx = (ax * 0.5f + 0.5f) * static_cast<float>(kWinW);
        }
        if (cursorInside) {
            ctx.cursorStageX = -(static_cast<float>(curClient.x) - originPx) / ppu;
            ctx.cursorYaw = pet::clampf(std::atan2(ctx.cursorStageX - player.dog().x, 1.6f), -1.2f, 1.2f);
        }

        // 光标视差：相机随光标在屏幕上的位置微微平移，前后景位移不同，是最便宜的深度提示。
        {
            pet::Vec3 goal{0, 0, 0};
            if (cursorOnScreen && !ignoreCursor) {
                const float nx = (static_cast<float>(curClient.x) / static_cast<float>(kWinW) - 0.5f) * 2.0f;
                const float ny = (static_cast<float>(curClient.y) / static_cast<float>(kWinH) - 0.5f) * 2.0f;
                goal = {-nx * 0.9f, -ny * 0.5f, 0.0f};
            }
            parallax.x = pet::approach(parallax.x, goal.x, dt, 0.35f);
            parallax.y = pet::approach(parallax.y, goal.y, dt, 0.35f);
        }
        const pet::Mat4 view = pet::look_at(eyeBase + parallax, kTarget, {0, 1, 0});
        const pet::Mat4 viewProj = view * proj;

        // 动作选择（L1）：只在 Free 态自选；停靠态的唤醒不选新动作。
        const bool freeState = !idle.in_burst(now);
        selector.drift(dt);
        const bool wasActive = player.active();
        if (wasActive) lastAction = player.current();
        player.update(dt, ctx);
        {
            const int s = player.take_sound();
            const bool mischief = lastAction == pet::ActionKind::Pounce || lastAction == pet::ActionKind::FlipBowl;
            play_sound(s, mischief);
        }
        if (wasActive && !player.active()) {
            selector.on_finished(lastAction, t);
            restTimer = selector.rest_seconds();
            if (lastAction == pet::ActionKind::PlayBall) { bond.apply(pet::BondEvent::PlayedBall); audio.play(pet::SoundId::Pant); ++totalBalls; }
            if (debugMask) std::printf("  动作结束：%s\n", pet::action_name(lastAction));
            lastAction = pet::ActionKind::Idle;
        }
        if (freeState && !player.active() && forcedAction < 0 && !dragging && introStage != 0) {
            restTimer -= dt;
            if (restTimer <= 0.0f) {
                const pet::ActionKind k = selector.choose(ctx);
                if (k != pet::ActionKind::Idle && player.start(k, ctx)) {
                    lastAction = k;
                    if (debugMask) {
                        const auto& n = selector.needs();
                        std::printf("  动作：%s（精力 %.2f 社交 %.2f 无聊 %.2f 亲密 %.0f）\n",
                                    pet::action_name(k), n.energy, n.social, n.boredom, bond.affinity());
                    }
                } else {
                    restTimer = selector.rest_seconds();
                }
            }
        }

        // 眼神：光标目标要换算到狗的局部空间。
        pet::GazeInput gin;
        gin.hasTarget = cursorInside;
        gin.targetAppeared = cursorInside && !cursorWasInside;
        if (cursorInside) {
            // 俯仰：光标在狗头上方还是下方，按像素差算。
            const float dy = static_cast<float>(headTop.y - metrics.monitor.top - curClient.y);
            float rel = ctx.cursorYaw - player.dog().yaw;
            while (rel > 3.14159265f) rel -= 6.2831853f;
            while (rel < -3.14159265f) rel += 6.2831853f;
            gin.targetYaw = rel;
            gin.targetPitch = pet::clampf(dy / 260.0f, -0.5f, 0.5f);
        }
        cursorWasInside = cursorInside;
        player.gaze_override(gin);
        gaze.update(dt, gin);
        if (gaze.mood() != lastMood) {
            lastMood = gaze.mood();
            if (debugMask) std::printf("  眼神：%s\n", pet::gaze_mood_name(lastMood));
        }

        // 姿态：清零 → 动作 → 眼神 → 部件树。
        for (auto& ps : poses) ps = pet::PartPose{};
        player.apply(poses);
        pet::pose_from_gaze(gaze.state(), gaze.mood(), t, poses);
        hearts.apply(poses);
        // 垫子（2.0）：睡觉抬起来、或被拖到半空时，脚下有个垫子，不像悬空。收放用缩放。
        {
            const bool wantMat = player.current() == pet::ActionKind::Sleep;   // 只有睡觉才有垫子（作者要求）
            matScale = pet::approach(matScale, wantMat ? 1.0f : 0.0f, dt, 0.25f);
            auto& mp = poses[static_cast<int>(pet::Part::Mat)];
            mp.offset = {player.dog().x, 0.0f, player.dog().z};
            mp.scale = {matScale, matScale, matScale};
        }
        pet::compute_part_world(mesh, poses, partWorld);

        // 显示比例：有互动、在提醒、出场介绍时是原大小；其余时候缩小，少挡东西。
        // 缩放绕狗自己的脚做，脚不离地、位置不变。1.5 及以前是绕原点缩再平移到角落，
        // 结果鼠标一靠近它放大就从光标底下滑走，右键落到下面的程序上。去角落的事交给回巢（巢设在右下角）。
        {
            // 1.6 起光标靠近不算「有互动」：作者反馈鼠标一靠近它就变大占地方。按下、拖、提醒、气泡才算。
            const bool engaged = mousePressed || dragging ||
                                 now - host.lastReminderMs < 20000 || introStage == 0 ||
                                 host.bubbleUntilMs != 0 || host.pendingShowMs != 0 ||
                                 lastAction == pet::ActionKind::Entrance;   // 摸和打不放大：1.7 起不用按键就能摸，放大会挡事
            presence = pet::approach(presence, engaged ? 1.0f : kSmallPresence, dt, 0.6f);
        }
        // 睡觉时抬到任务栏之上：上一帧的矩形底边超过栏杆线就把目标抬高相应的量，几帧就收敛。
        {
            const bool sleeping = player.current() == pet::ActionKind::Sleep;
            if (sleeping && groundY < 0.02f) {
                // 用「没抬之前的底边」算需要抬多少，不累积：累积版在抬的过程中越加越多，狗抬到屏幕外去了。
                const float limit = static_cast<float>(taskbarVisibleTop) - 6.0f * dpiScale;
                const float bottomUnlifted = static_cast<float>(dogRect.bottom) + sleepLift * ppu;
                sleepLiftTarget = pet::clampf((bottomUnlifted - limit) / ppu, 0.0f, 1.5f);
            } else if (!sleeping) {
                sleepLiftTarget = 0.0f;
            }
            sleepLift = pet::approach(sleepLift, sleepLiftTarget, dt, 0.35f);
            // 睡觉不能出屏幕（作者要求）：垫子比狗宽，靠右边时按「没挪之前的右边」往左挪；醒了挪回去。
            float shiftTarget = 0.0f;
            if (sleeping && prevScene.right > prevScene.left) {
                const float rightUnshifted = static_cast<float>(prevScene.right) + sleepShiftX * ppu;
                const float edge = static_cast<float>(kWinW) - 10.0f * dpiScale;
                shiftTarget = pet::clampf((rightUnshifted - edge) / ppu, 0.0f, 2.0f);   // +x 是屏幕左
            }
            sleepShiftX = pet::approach(sleepShiftX, shiftTarget, dt, 0.35f);
        }
        const pet::Mat4 viewProjEff =
            pet::translate(-player.dog().x, 0.0f, -player.dog().z) *
            pet::scale(presence, presence, presence) *
            pet::translate(player.dog().x + sleepShiftX, groundY + sleepLift, player.dog().z) * viewProj;

        // 头与整条狗的屏幕矩形，下一帧手势判定、回读区域、气泡锚点用。
        headRect = project_box(partWorld[static_cast<int>(pet::Part::Head)], viewProjEff,
                               {-0.24f, 0.74f, 0.50f}, {0.24f, 1.16f, 1.12f}, kWinW, kWinH, 8);
        dogRect = project_box(partWorld[static_cast<int>(pet::Part::Body)], viewProjEff,
                              {-0.45f, -0.05f, -0.75f}, {0.45f, 1.30f, 1.25f}, kWinW, kWinH, 12);
        UnionRect(&dogRect, &dogRect, &headRect);
        headTop = {metrics.monitor.left + (headRect.left + headRect.right) / 2, metrics.monitor.top + headRect.top};
        if (host.bubbleUntilMs != 0) bubble.move_anchor(headTop);
        if (fxTagUntilMs != 0) fxTag.move_anchor({headTop.x - static_cast<int>(120 * dpiScale), headTop.y - static_cast<int>(20 * dpiScale)});

        // 脏矩形：这一帧和上一帧画过的所有东西（狗、球、碗、一滩、阴影）的并集。
        {
            RECT scene = project_box(partWorld[static_cast<int>(pet::Part::Body)], viewProjEff,
                                     {-0.9f, -0.2f, -1.0f}, {0.9f, 1.6f, 1.6f}, kWinW, kWinH, 16);
            const pet::Part props[] = {pet::Part::Ball, pet::Part::Bowl, pet::Part::Puddle, pet::Part::ShadowBall,
                                       pet::Part::Heart0, pet::Part::Heart1, pet::Part::Heart2, pet::Part::Heart3, pet::Part::Mat};
            for (pet::Part pr : props) {
                if (poses[static_cast<int>(pr)].scale.x <= 0.001f) continue;
                RECT r = project_box(partWorld[static_cast<int>(pr)], viewProjEff,
                                     {-0.3f, -0.05f, -0.3f}, {0.3f, 0.3f, 0.3f}, kWinW, kWinH, 12);
                UnionRect(&scene, &scene, &r);
            }
            RECT dirty = scene;
            if (prevScene.right > prevScene.left) {
                UnionRect(&dirty, &dirty, &prevScene);
                sceneSpeedPx = (std::max)({std::abs(scene.left - prevScene.left), std::abs(scene.top - prevScene.top),
                                           std::abs(scene.right - prevScene.right), std::abs(scene.bottom - prevScene.bottom)});
                sceneSpeedPx = (std::min)(sceneSpeedPx, 96);
            }
            RECT full{0, 0, kWinW, kWinH};
            IntersectRect(&dirty, &dirty, &full);
            gfx.set_dirty_rect(dirty);
            prevScene = scene;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        ID3D12GraphicsCommandList* cmd = nullptr;
        if (!gfx.begin_frame(&rtv, &cmd)) break;
        const float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        cmd->ClearRenderTargetView(rtv, clear, 0, nullptr);
        // 栏杆（任务栏）前后：部件原点的舞台 z 在巢的 z 之后就在栏杆后面，套裁剪；之前的不裁。
        // 站着时后腿、躯干、阴影在后面，前腿、头在前面；冲屏时整条狗都在前面。
        {
            // 部件矩阵是 T(-pivot)·S·R·T(pivot+offset)，不转不动时接近单位阵，平移行不是位置；
            // 要把枢轴点本身变换过去才是它在舞台上的位置。
            // 地面抬高了（被拖到任务栏上方）就没有栏杆，全不裁；睡觉时全裁，抬上去之前不许压到时钟。
            const float railZ = player.home_z() + 0.05f;
            const bool sleeping = player.current() == pet::ActionKind::Sleep;
            for (size_t i = 0; i < mesh.parts.size() && i < partBehind.size(); ++i) {
                if (sleeping) { partBehind[i] = 1; continue; }
                if (groundY > 0.02f) { partBehind[i] = 0; continue; }
                const pet::Vec3 pv = mesh.parts[i].pivot;
                const pet::Mat4& w = partWorld[i];
                const float z = pv.x * w.m[0][2] + pv.y * w.m[1][2] + pv.z * w.m[2][2] + w.m[3][2];
                partBehind[i] = z <= railZ ? 1 : 0;
            }
        }
        renderer.draw(cmd, rtv, mesh, partWorld, viewProjEff, kLightDir, partBehind.data());
        if (!gfx.end_frame()) break;
        ++frames;
    }

    flush_save(true);

    // 顺序要紧：先卸插件、摘托盘图标，再放图形资源，最后销毁窗口。
    health.on_unload();
    memo.on_unload();
    weather.on_unload();
    tips.on_unload();
    memoDialog.close();
    rename.close();
    bubble.destroy();
    tray.destroy();
    audio.release();
    renderer.release();
    gfx.release_device();
    win.destroy();
    if (single) CloseHandle(single);

    if (restartRequested) {
        // 互斥量已放掉，起一个新的自己再退出。
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";
        if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        return 0;
    }

    if (debugMask) {
        const auto& mk = gfx.alpha_mask();
        if (mk.valid() && gfx.readback_count() > 0) {
            std::printf("\n  渲染轮廓（alpha 掩码，横纵各隔一格取样）：\n");
            for (int cy = 0; cy < mk.rows; cy += 2) {
                std::printf("  ");
                for (int cx = 0; cx < mk.cols; cx += 2) {
                    std::putchar(mk.cell[static_cast<size_t>(cy) * mk.cols + cx] >= 24 ? '#' : '.');
                }
                std::putchar('\n');
            }
        }
    }

    std::printf("\n  退出。共呈现 %u 帧，alpha 回读 %u 次，停止呈现后循环 %llu 圈，微动作 %u 次，眨眼 %u 次。\n",
                frames, gfx.readback_count(), idleWakes, microActions, gaze.blink_count());
    std::printf("  互动：摸 %u 次、戳 %u 次、打 %u 次。亲密度 %.1f。音效放了 %u 次、压掉 %u 次。\n",
                pets, pokes, hits, bond.affinity(), audio.played_count(), audio.suppressed_count());
    std::printf("  健康提醒：发出 %u 次，宿主接受 %u 次、拒绝 %u 次。\n",
                health.reminders_sent(), host.accepted, host.refused);
    return 0;
}
