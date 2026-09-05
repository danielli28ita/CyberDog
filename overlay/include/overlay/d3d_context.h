// D3D12 加 DirectComposition 的透明覆盖层渲染上下文。
//
// 对应 设计文档 §3.3。窗口用 WS_EX_NOREDIRECTIONBITMAP，交换链走
// CreateSwapChainForComposition 加 DXGI_ALPHA_MODE_PREMULTIPLIED，
// 由 IDCompositionVisual 交给桌面合成器，不走 UpdateLayeredWindow。
//
// 设备生命周期集中在这里管（技能包 engineering/SKILL.md 的 C++ 约定）。
// 上层只调 begin_frame / end_frame，不自己建设备。

#pragma once

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dcomp.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

namespace pet::gfx {

// 下采样后的 alpha 掩码，供逐像素命中测试用（设计文档 §3.3）。
// 按 block×block 的格子存每格的最大 alpha。用最大值而不是平均，
// 是为了不把耳朵、尾巴这类细长部位判成透明。
struct AlphaMask {
    int cols = 0;
    int rows = 0;
    int block = 8;
    std::vector<std::uint8_t> cell;

    bool valid() const { return cols > 0 && rows > 0 && !cell.empty(); }

    // 客户区坐标是否落在不透明部分。threshold 以下算透明，点击穿透。
    bool opaque_at(int x, int y, std::uint8_t threshold = 24) const {
        if (!valid() || x < 0 || y < 0) return false;
        const int cx = x / block, cy = y / block;
        if (cx >= cols || cy >= rows) return false;
        return cell[static_cast<size_t>(cy) * cols + cx] >= threshold;
    }
};

class D3DContext {
public:
    D3DContext() = default;
    ~D3DContext();

    D3DContext(const D3DContext&) = delete;
    D3DContext& operator=(const D3DContext&) = delete;

    // enableDebugLayer 为真时尝试开启 D3D12 调试层。
    // 调试层属于「图形工具」可选功能，没装不算失败。
    bool init(HWND hwnd, UINT width, UINT height, bool enableDebugLayer);

    // 空闲释放之后重新用，调这个。缺什么建什么，已经有的不动。
    // 释放到哪一级都能靠它醒回来。
    bool ensure_ready();

    // 换显示器时改交换链尺寸。设备保留，只重建 swapchain / DComp / 回读缓冲。
    bool resize(UINT width, UINT height);

    // 开始一帧。返回当前后台缓冲的 RTV 句柄，并把资源转到 RENDER_TARGET。
    // 未初始化时返回 false。
    bool begin_frame(D3D12_CPU_DESCRIPTOR_HANDLE* outRtv, ID3D12GraphicsCommandList** outCmd);

    // 结束一帧：转回 PRESENT、提交、Present、等围栏。
    // 设备丢失时返回 false，调用方应重建（设计文档 §3.3 最后一行）。
    bool end_frame();

    // 三级空闲释放（设计文档 §3.3）。
    void release_swapchain();   // 二级：释放交换链与后台缓冲，设备保留
    void release_device();      // 三级：释放整个图形设备

    // 请求在下一次 end_frame 里回读 alpha。调用方负责限频，
    // 并且只在光标进入窗口矩形时才请求（设计文档 §3.3）。
    // region 是客户区像素矩形：窗口是整个显示器时只回读狗所在的那一块，
    // 全屏回读一次要拷 16 MB、扫 400 万像素，做不到 15 Hz。nullptr 表示整个窗口。
    void request_alpha_readback(const RECT* region = nullptr) {
        wantReadback_ = true;
        if (region) { region_ = *region; hasRegion_ = true; } else { hasRegion_ = false; }
    }

    // 这一帧画面变化的区域（客户区像素）。设了就用 Present1 带脏矩形呈现，
    // 桌面合成器只重合成这一块。窗口是整个屏幕时，不带脏矩形每帧都是全屏合成，整机会卡。
    void set_dirty_rect(const RECT& r) { dirty_ = r; hasDirty_ = true; }

    const AlphaMask& alpha_mask() const { return mask_; }
    double last_readback_ms() const { return lastReadbackMs_; }
    unsigned readback_count() const { return readbackCount_; }

    ID3D12Device* device() const { return device_.Get(); }
    bool has_device() const { return device_ != nullptr; }
    bool has_swapchain() const { return swap_ != nullptr; }

    const wchar_t* adapter_name() const { return adapterName_; }
    std::uint64_t dedicated_vram_mb() const { return dedicatedVramMB_; }

private:
    void wait_gpu();
    bool create_device();
    bool create_swapchain();
    bool create_readback();
    void resolve_readback();

    static constexpr UINT kFrameCount = 2;

    template <class T> using Ptr = Microsoft::WRL::ComPtr<T>;

    HWND hwnd_ = nullptr;
    UINT width_ = 0, height_ = 0;
    bool debugLayer_ = false;

    Ptr<IDXGIFactory6>            factory_;
    Ptr<ID3D12Device>             device_;
    Ptr<ID3D12CommandQueue>       queue_;
    Ptr<IDXGISwapChain3>          swap_;
    Ptr<IDCompositionDevice>      dcomp_;
    Ptr<IDCompositionTarget>      dcompTarget_;
    Ptr<IDCompositionVisual>      dcompVisual_;
    Ptr<ID3D12DescriptorHeap>     rtvHeap_;
    Ptr<ID3D12Resource>           backbuf_[kFrameCount];
    Ptr<ID3D12CommandAllocator>   alloc_;
    Ptr<ID3D12GraphicsCommandList> cmd_;
    Ptr<ID3D12Fence>              fence_;

    UINT   rtvStride_ = 0;
    UINT   frameIndex_ = 0;
    UINT64 fenceVal_ = 0;
    HANDLE fenceEvt_ = nullptr;

    wchar_t       adapterName_[128] = L"";
    std::uint64_t dedicatedVramMB_ = 0;

    // alpha 回读
    Ptr<ID3D12Resource>                readback_;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint_{};
    UINT64                             readbackBytes_ = 0;
    bool                               wantReadback_ = false;
    bool                               copiedThisFrame_ = false;
    bool                               hasRegion_ = false;
    bool                               hasDirty_ = false;
    RECT                               dirty_{};
    RECT                               region_{};
    RECT                               copied_{};   // 这一帧实际拷了哪块
    AlphaMask                          mask_;
    double                             lastReadbackMs_ = 0.0;
    unsigned                           readbackCount_ = 0;
};

}  // namespace pet::gfx
