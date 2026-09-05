#include "overlay/d3d_context.h"

#include <algorithm>  // std::fill, std::max
#include <cstdio>
#include <cwchar>     // std::wcsncpy
#include <iterator>   // std::size

namespace pet::gfx {
namespace {

// 每个 HRESULT 都要检查，失败要打印十六进制值（技能包 engineering/SKILL.md 的 C++ 约定）。
bool ok(HRESULT hr, const char* what) {
    if (SUCCEEDED(hr)) return true;
    std::printf("  [FAIL] %s  hr=0x%08lX\n", what, static_cast<unsigned long>(hr));
    return false;
}

}  // namespace

D3DContext::~D3DContext() {
    release_device();
}

bool D3DContext::init(HWND hwnd, UINT width, UINT height, bool enableDebugLayer) {
    hwnd_ = hwnd;
    width_ = width;
    height_ = height;
    debugLayer_ = enableDebugLayer;
    return ensure_ready();
}

bool D3DContext::ensure_ready() {
    if (!device_ && !create_device()) return false;
    if (!swap_ && !create_swapchain()) return false;
    return true;
}

bool D3DContext::create_device() {
    UINT factoryFlags = 0;
    if (debugLayer_) {
        Ptr<ID3D12Debug> dbg;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) {
            dbg->EnableDebugLayer();
            factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }

    if (!ok(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory_)), "CreateDXGIFactory2")) return false;

    Ptr<IDXGIAdapter1> adapter;
    if (!ok(factory_->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                 IID_PPV_ARGS(&adapter)),
            "EnumAdapterByGpuPreference")) return false;

    DXGI_ADAPTER_DESC1 ad{};
    adapter->GetDesc1(&ad);
    // 用 _s 版本并显式截断，不靠 _CRT_SECURE_NO_WARNINGS 掩盖警告。
    wcsncpy_s(adapterName_, std::size(adapterName_), ad.Description, _TRUNCATE);
    dedicatedVramMB_ = ad.DedicatedVideoMemory / (1024 * 1024);

    if (!ok(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)),
            "D3D12CreateDevice")) return false;

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (!ok(device_->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue_)), "CreateCommandQueue")) return false;

    if (!ok(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc_)),
            "CreateCommandAllocator")) return false;
    if (!ok(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc_.Get(), nullptr,
                                       IID_PPV_ARGS(&cmd_)), "CreateCommandList")) return false;
    cmd_->Close();

    if (!ok(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "CreateFence")) return false;
    fenceEvt_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvt_) {
        std::printf("  [FAIL] CreateEventW  err=%lu\n", GetLastError());
        return false;
    }
    return true;
}

bool D3DContext::create_swapchain() {
    // 合成用交换链：预乘 alpha 是透明覆盖层的关键，见 设计文档 §3.3。
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width       = width_;
    sd.Height      = height_;
    sd.Format      = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = kFrameCount;
    sd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    sd.AlphaMode   = DXGI_ALPHA_MODE_PREMULTIPLIED;
    sd.Scaling     = DXGI_SCALING_STRETCH;
    sd.SampleDesc  = {1, 0};

    Ptr<IDXGISwapChain1> s1;
    if (!ok(factory_->CreateSwapChainForComposition(queue_.Get(), &sd, nullptr, &s1),
            "CreateSwapChainForComposition")) return false;
    if (!ok(s1.As(&swap_), "QueryInterface IDXGISwapChain3")) return false;

    // D3D12 没有 IDXGIDevice，第一个参数传 nullptr。
    if (!ok(DCompositionCreateDevice(nullptr, IID_PPV_ARGS(&dcomp_)), "DCompositionCreateDevice")) return false;
    if (!ok(dcomp_->CreateTargetForHwnd(hwnd_, TRUE, &dcompTarget_), "CreateTargetForHwnd")) return false;
    if (!ok(dcomp_->CreateVisual(&dcompVisual_), "CreateVisual")) return false;
    if (!ok(dcompVisual_->SetContent(swap_.Get()), "Visual::SetContent")) return false;
    if (!ok(dcompTarget_->SetRoot(dcompVisual_.Get()), "Target::SetRoot")) return false;
    if (!ok(dcomp_->Commit(), "DComp::Commit")) return false;

    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.NumDescriptors = kFrameCount;
    hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    if (!ok(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtvHeap_)), "CreateDescriptorHeap")) return false;
    rtvStride_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    for (UINT i = 0; i < kFrameCount; ++i) {
        if (!ok(swap_->GetBuffer(i, IID_PPV_ARGS(&backbuf_[i])), "SwapChain::GetBuffer")) return false;
        D3D12_CPU_DESCRIPTOR_HANDLE h = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(i) * rtvStride_;
        device_->CreateRenderTargetView(backbuf_[i].Get(), nullptr, h);
    }

    if (!create_readback()) return false;
    return true;
}

bool D3DContext::create_readback() {
    // 回读目标的布局由 GetCopyableFootprints 决定，不要自己算行距。
    // D3D12 要求每行按 256 字节对齐，手算很容易差一点。
    const D3D12_RESOURCE_DESC src = backbuf_[0]->GetDesc();
    device_->GetCopyableFootprints(&src, 0, 1, 0, &footprint_, nullptr, nullptr, &readbackBytes_);

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = readbackBytes_;
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc       = {1, 0};
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (!ok(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&readback_)),
            "CreateCommittedResource(READBACK)")) return false;

    mask_.block = 8;
    mask_.cols  = (static_cast<int>(width_) + mask_.block - 1) / mask_.block;
    mask_.rows  = (static_cast<int>(height_) + mask_.block - 1) / mask_.block;
    mask_.cell.assign(static_cast<size_t>(mask_.cols) * mask_.rows, 0);
    return true;
}

bool D3DContext::begin_frame(D3D12_CPU_DESCRIPTOR_HANDLE* outRtv, ID3D12GraphicsCommandList** outCmd) {
    if (!swap_ || !device_) return false;

    frameIndex_ = swap_->GetCurrentBackBufferIndex();
    if (!ok(alloc_->Reset(), "CommandAllocator::Reset")) return false;
    if (!ok(cmd_->Reset(alloc_.Get(), nullptr), "CommandList::Reset")) return false;

    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = backbuf_[frameIndex_].Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd_->ResourceBarrier(1, &b);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(frameIndex_) * rtvStride_;
    cmd_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    *outRtv = rtv;
    *outCmd = cmd_.Get();
    return true;
}

bool D3DContext::end_frame() {
    if (!swap_) return false;

    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = backbuf_[frameIndex_].Get();
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    copiedThisFrame_ = false;
    if (wantReadback_ && readback_) {
        // 渲染目标 -> 拷贝源 -> 拷进回读缓冲 -> 呈现态。
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
        cmd_->ResourceBarrier(1, &b);

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource       = readback_.Get();
        dst.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = footprint_;

        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource        = backbuf_[frameIndex_].Get();
        srcLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLoc.SubresourceIndex = 0;

        // 只拷请求的区域（夹到窗口内，对齐到掩码格子），落在回读缓冲的同一位置。
        RECT r{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
        if (hasRegion_) {
            const LONG blk = mask_.block;
            r.left   = (std::max)(0L, (region_.left / blk) * blk);
            r.top    = (std::max)(0L, (region_.top / blk) * blk);
            r.right  = (std::min)(static_cast<LONG>(width_), ((region_.right + blk - 1) / blk) * blk);
            r.bottom = (std::min)(static_cast<LONG>(height_), ((region_.bottom + blk - 1) / blk) * blk);
        }
        if (r.right > r.left && r.bottom > r.top) {
            D3D12_BOX box{static_cast<UINT>(r.left), static_cast<UINT>(r.top), 0,
                          static_cast<UINT>(r.right), static_cast<UINT>(r.bottom), 1};
            cmd_->CopyTextureRegion(&dst, static_cast<UINT>(r.left), static_cast<UINT>(r.top), 0, &srcLoc, &box);
        }
        copied_ = r;

        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
        cmd_->ResourceBarrier(1, &b);

        copiedThisFrame_ = true;
        wantReadback_ = false;
    } else {
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
        cmd_->ResourceBarrier(1, &b);
    }

    if (!ok(cmd_->Close(), "CommandList::Close")) return false;
    ID3D12CommandList* lists[] = {cmd_.Get()};
    queue_->ExecuteCommandLists(1, lists);

    // 带脏矩形呈现：合成器只重合成变化的区域。第一帧或没设时整幅呈现。
    HRESULT hr;
    if (hasDirty_ && dirty_.right > dirty_.left && dirty_.bottom > dirty_.top) {
        DXGI_PRESENT_PARAMETERS pp{};
        pp.DirtyRectsCount = 1;
        pp.pDirtyRects = &dirty_;
        hr = swap_->Present1(1, 0, &pp);
        // 第一次带脏矩形失败（比如交换链不支持）就退回整幅呈现，以后也不再试。
        if (hr == DXGI_ERROR_INVALID_CALL) { hasDirty_ = false; hr = swap_->Present(1, 0); }
    } else {
        hr = swap_->Present(1, 0);
    }
    hasDirty_ = false;
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        // 驱动更新或休眠唤醒会触发。约定是重建，不是退出进程。
        std::printf("  [WARN] 设备丢失 hr=0x%08lX，需要重建\n", static_cast<unsigned long>(hr));
        return false;
    }
    if (!ok(hr, "Present")) return false;

    wait_gpu();
    if (copiedThisFrame_) resolve_readback();
    return true;
}

void D3DContext::resolve_readback() {
    LARGE_INTEGER freq{}, t0{}, t1{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    // 只读，Map 的范围给整个缓冲。GPU 已经在 wait_gpu 里等完了。
    void* p = nullptr;
    D3D12_RANGE readRange{0, static_cast<SIZE_T>(readbackBytes_)};
    if (FAILED(readback_->Map(0, &readRange, &p)) || !p) return;

    const auto* base = static_cast<const std::uint8_t*>(p);
    const UINT rowPitch = footprint_.Footprint.RowPitch;
    const int  w = static_cast<int>(width_);
    const int  h = static_cast<int>(height_);
    const int  blk = mask_.block;

    // 只扫这一帧拷过的那块；其余格子清零（狗已经不在那儿了）。
    const int cy0 = static_cast<int>(copied_.top) / blk, cy1 = (static_cast<int>(copied_.bottom) + blk - 1) / blk;
    const int cx0 = static_cast<int>(copied_.left) / blk, cx1 = (static_cast<int>(copied_.right) + blk - 1) / blk;
    std::fill(mask_.cell.begin(), mask_.cell.end(), std::uint8_t{0});

    // 每格取最大 alpha。格内按 2 像素步长抽样，够用且省一半以上时间。
    for (int cy = cy0; cy < cy1 && cy < mask_.rows; ++cy) {
        for (int cx = cx0; cx < cx1 && cx < mask_.cols; ++cx) {
            std::uint8_t m = 0;
            const int y1 = (cy + 1) * blk < h ? (cy + 1) * blk : h;
            const int x1 = (cx + 1) * blk < w ? (cx + 1) * blk : w;
            for (int y = cy * blk; y < y1; y += 2) {
                const std::uint8_t* row = base + static_cast<size_t>(y) * rowPitch;
                for (int x = cx * blk; x < x1; x += 2) {
                    // 格式是 B8G8R8A8_UNORM，alpha 在每像素第 4 字节。
                    const std::uint8_t a = row[static_cast<size_t>(x) * 4 + 3];
                    if (a > m) m = a;
                }
            }
            mask_.cell[static_cast<size_t>(cy) * mask_.cols + cx] = m;
        }
    }

    D3D12_RANGE noWrite{0, 0};
    readback_->Unmap(0, &noWrite);

    QueryPerformanceCounter(&t1);
    lastReadbackMs_ = (t1.QuadPart - t0.QuadPart) * 1000.0 / freq.QuadPart;
    ++readbackCount_;
}

void D3DContext::wait_gpu() {
    if (!queue_ || !fence_) return;
    const UINT64 v = ++fenceVal_;
    queue_->Signal(fence_.Get(), v);
    if (fence_->GetCompletedValue() < v) {
        fence_->SetEventOnCompletion(v, fenceEvt_);
        WaitForSingleObject(fenceEvt_, INFINITE);
    }
}

void D3DContext::release_swapchain() {
    wait_gpu();
    readback_.Reset();
    for (auto& b : backbuf_) b.Reset();
    rtvHeap_.Reset();
    dcompVisual_.Reset();
    dcompTarget_.Reset();
    dcomp_.Reset();
    swap_.Reset();
}

void D3DContext::release_device() {
    release_swapchain();
    dcomp_.Reset();
    cmd_.Reset();
    alloc_.Reset();
    fence_.Reset();
    if (fenceEvt_) {
        CloseHandle(fenceEvt_);
        fenceEvt_ = nullptr;
    }
    queue_.Reset();
    device_.Reset();
    factory_.Reset();
}

}  // namespace pet::gfx
