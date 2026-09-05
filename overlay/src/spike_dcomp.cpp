// P1 的回归用例，兼栖位检查。
//
// 任务 1（D3D12 + DirectComposition 透明窗）已于 2026-09-04 验证通过，含视觉确认。
// 本程序保留下来有两个用途：
//   1. 改动渲染或窗口层之后跑一遍，确认没有回归。
//   2. 检查 设计文档 §2.2 的栖位计算：窗口应当出现在桌面右下角，
//      底边压进任务栏 18 px，下半部分被任务栏遮住。
//
// 画面内容：三块同色不同 alpha 的方块（1.0 / 0.5 / 0.25），加一根来回移动的白条。
// 这些不是产品美术，只是让人能一眼判断透明与混合是否正确。

#include "overlay/win32_window.h"
#include "overlay/desktop_metrics.h"
#include "overlay/d3d_context.h"
#include "core/personality.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>  // std::strcmp

namespace {

constexpr int kWinW = 640;
constexpr int kWinH = 340;
constexpr int kEmbedPx = 18;  // 压进任务栏的深度，设计文档 §2.2 默认值

// 预乘 alpha：写进渲染目标的 RGB 必须已经乘过 alpha。
void premul(float r, float g, float b, float a, float out[4]) {
    out[0] = r * a; out[1] = g * a; out[2] = b * a; out[3] = a;
}

const char* edge_name(pet::win::TaskbarEdge e) {
    using E = pet::win::TaskbarEdge;
    switch (e) {
        case E::Left:   return "左";
        case E::Top:    return "上";
        case E::Right:  return "右";
        case E::Bottom: return "下";
        default:        return "未知";
    }
}

}  // namespace

int main(int argc, char** argv) {
    const int run_seconds = (argc > 1) ? std::atoi(argv[1]) : 20;
    // 第二个参数给 force 时，不管光标在哪都回读。
    // 用来在没有人操作鼠标的情况下验证回读路径与掩码内容。
    const bool forceReadback = (argc > 2) && std::strcmp(argv[2], "force") == 0;

    pet::win::enable_utf8_console();

    std::printf("=== P1 回归用例：透明覆盖层 + 右下角栖位 ===\n\n");

    // 开发机跑在非 100% 缩放下，进程一启动就打开 DPI 感知，避免系统做位图拉伸。
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    pet::win::OverlayWindow win;
    if (!win.create(L"pet overlay", kWinW, kWinH)) return 1;
    std::printf("  [ok]   窗口已创建\n");

    // 栖位计算。所有平台查询都在 desktop_metrics 里，这里只拿结果。
    const auto m = pet::win::query(win.hwnd());
    std::printf("  显示器 DPI %u（缩放 %.0f%%）\n", m.dpi, m.dpi * 100.0 / 96.0);
    std::printf("  工作区 %ld,%ld - %ld,%ld\n",
                m.workArea.left, m.workArea.top, m.workArea.right, m.workArea.bottom);
    if (m.taskbarValid) {
        std::printf("  任务栏 %ld,%ld - %ld,%ld  边缘=%s  自动隐藏=%s\n",
                    m.taskbar.left, m.taskbar.top, m.taskbar.right, m.taskbar.bottom,
                    edge_name(m.edge), m.taskbarAutoHide ? "是" : "否");
    } else {
        std::printf("  任务栏矩形取不到，退回工作区右下角\n");
    }
    if (m.notifyArea.right > m.notifyArea.left) {
        std::printf("  通知区域宽 %ld px，栖位会让开它\n", m.notifyArea.right - m.notifyArea.left);
    }
    std::printf("  全屏应用或投影中：%s\n", pet::win::fullscreen_or_presenting() ? "是" : "否");

    const POINT dock = pet::win::dock_position(m, kWinW, kWinH, kEmbedPx);
    std::printf("  栖位左上角 %ld,%ld（底边 %ld，压入 %d px）\n\n",
                dock.x, dock.y, dock.y + kWinH, kEmbedPx);
    win.move_to(dock);

    pet::gfx::D3DContext gfx;
    if (!gfx.init(win.hwnd(), kWinW, kWinH, /*enableDebugLayer=*/true)) return 1;
    char name[256] = {};
    WideCharToMultiByte(CP_UTF8, 0, gfx.adapter_name(), -1, name, sizeof(name), nullptr, nullptr);
    std::printf("  [ok]   图形上下文已初始化\n");
    std::printf("  适配器 %s，专用显存 %llu MB\n\n",
                name, static_cast<unsigned long long>(gfx.dedicated_vram_mb()));

    // P1 任务 5：命中测试改用 alpha 掩码，不再用矩形近似。
    // 掩码由 D3DContext 在回读时更新，这里只查表，不做任何 GPU 操作——
    // WM_NCHITTEST 是同步消息，在里面等 GPU 会卡住整个桌面的鼠标。
    win.set_hit_test([&gfx](int x, int y) {
        const auto& m = gfx.alpha_mask();
        if (!m.valid()) return true;  // 掩码还没建立时先当作本体，避免开头点不到
        return m.opaque_at(x, y);
    });
    win.set_draggable(true);

    const pet::Personality p = pet::personality_from_seed(20260904ull);
    std::printf("  core 自检 seed=20260904 -> laziness=%.3f 回巢阈值=%.1f 秒\n\n", p.laziness,
                pet::idle_timeout_seconds(p));

    win.show_no_activate();
    std::printf("------------------------------------------------------------\n");
    std::printf("  运行 %d 秒后自动退出（Esc 提前退出）。请核对：\n", run_seconds);
    std::printf("   1. 窗口在桌面右下角，让开了右下的时钟与托盘图标\n");
    std::printf("   2. 窗口下半部分被任务栏挡住，看不到压进去的那 %d px\n", kEmbedPx);
    std::printf("   3. 方块之外完全看得见桌面\n");
    std::printf("   4. 在方块上按住能拖动；在方块之间的空白处点击，点到的是下层窗口\n");
    std::printf("------------------------------------------------------------\n\n");

    const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(run_seconds) * 1000ull;
    unsigned frames = 0;

    ULONGLONG lastReadback = 0;

    while (win.pump()) {
        if (GetTickCount64() >= deadline) break;

        // 只在光标进入窗口矩形时回读，且限到 15 Hz（设计文档 §3.3）。
        // 光标在别处时完全不回读，这是省开销的关键。
        POINT cur{};
        RECT wr{};
        const bool cursorInside =
            GetCursorPos(&cur) && GetWindowRect(win.hwnd(), &wr) && PtInRect(&wr, cur);
        if (cursorInside || forceReadback) {
            const ULONGLONG now = GetTickCount64();
            if (now - lastReadback >= 66) {
                gfx.request_alpha_readback();
                lastReadback = now;
            }
        }

        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        ID3D12GraphicsCommandList* cmd = nullptr;
        if (!gfx.begin_frame(&rtv, &cmd)) break;

        const float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        cmd->ClearRenderTargetView(rtv, clear, 0, nullptr);

        const float alphas[3] = {1.00f, 0.50f, 0.25f};
        for (int k = 0; k < 3; ++k) {
            float c[4];
            premul(0.10f, 0.80f, 0.95f, alphas[k], c);
            const LONG x0 = 40 + k * 200;
            D3D12_RECT box{x0, 60, x0 + 160, 220};
            cmd->ClearRenderTargetView(rtv, c, 1, &box);
        }

        const double t = static_cast<double>(GetTickCount64() % 3000) / 3000.0;
        const double sweep = (t < 0.5) ? (t * 2.0) : (2.0 - t * 2.0);
        const LONG bx = 20 + static_cast<LONG>(sweep * (kWinW - 60));
        float bar[4];
        premul(1.0f, 1.0f, 1.0f, 1.0f, bar);
        D3D12_RECT barRect{bx, 260, bx + 20, 320};
        cmd->ClearRenderTargetView(rtv, bar, 1, &barRect);

        if (!gfx.end_frame()) break;
        ++frames;
    }

    const auto& mask = gfx.alpha_mask();
    std::printf("\n=== 结束 ===\n");
    std::printf("  呈现 %u 帧\n", frames);
    std::printf("  alpha 回读 %u 次，掩码 %dx%d 格（每格 %d px），最后一次 CPU 耗时 %.3f ms\n",
                gfx.readback_count(), mask.cols, mask.rows, mask.block, gfx.last_readback_ms());
    if (gfx.readback_count() == 0) {
        std::printf("  回读 0 次说明光标一直没进窗口，命中测试这条没验到\n");
    } else if (forceReadback && mask.valid()) {
        // 把掩码打成字符图，用来核对它跟画出来的形状是否一致。
        // '#' 不透明，'+' 半透明，'.' 接近透明，' ' 完全透明。
        std::printf("\n  掩码内容（横向每 2 格取 1 格，纵向每 2 格取 1 格）：\n");
        for (int cy = 0; cy < mask.rows; cy += 2) {
            std::printf("  ");
            for (int cx = 0; cx < mask.cols; cx += 2) {
                const std::uint8_t a = mask.cell[static_cast<size_t>(cy) * mask.cols + cx];
                char c = ' ';
                if (a >= 200)     c = '#';
                else if (a >= 96) c = '+';
                else if (a >= 24) c = '.';
                std::putchar(c);
            }
            std::putchar('\n');
        }
        std::printf("\n  预期：三块方块 alpha 分别 1.0 / 0.5 / 0.25，应当显示为 # / + / .\n");
        std::printf("  下方那根来回移动的白条是 #，位置每帧都在变。\n");
    }

    gfx.release_device();
    win.destroy();
    return 0;
}
