// 3D 网格渲染。P1 任务 4。
//
// 这一层只管「把一个网格画到给定的渲染目标上」：根签名、管线状态、
// 顶点索引缓冲、深度缓冲、着色器。设备与交换链的生命周期归 D3DContext 管，
// 这里只借用设备句柄（技能包 engineering/SKILL.md 的 C++ 约定）。
//
// 输出走预乘 alpha：着色器里 RGB 必须已经乘过 alpha，
// 否则 DirectComposition 合成出来的边缘会发亮。

#pragma once

#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include "core/math3d.h"
#include "core/proxy_mesh.h"

namespace pet::gfx {

class MeshRenderer {
public:
    bool init(ID3D12Device* device, DXGI_FORMAT rtvFormat, UINT width, UINT height,
              const Mesh& mesh);
    void release();
    bool ready() const { return pso_ != nullptr; }

    // 在已经绑定好渲染目标的命令列表上画一遍。
    // 调用方负责在这之前清渲染目标。深度缓冲由本类自己清。
    //
    // 按部件逐个画：partWorld 是每个部件的模型空间矩阵（compute_part_world 的输出），
    // 每个部件一次 DrawIndexed，用根常量传自己的 mvp。部件十几个，调用次数可以忽略。
    // partBehind：每个部件一个字节，非零表示这个部件在「栏杆」（任务栏）后面，画的时候套裁剪矩形；
    // 零表示在前面，不裁。空指针 = 全部都裁（1.6 及以前的行为）。
    // 1.7 的伪 3D：任务栏当栏杆，后腿被挡、前腿不被挡，冲出来时整条狗都在栏杆前。
    void draw(ID3D12GraphicsCommandList* cmd, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
              const Mesh& mesh, const Mat4* partWorld,
              const Mat4& viewProj, Vec3 lightDir, const unsigned char* partBehind = nullptr);

    // 渲染裁剪矩形（客户区像素）。设计文档 §2.2：任务栏遮挡用裁剪实现，不用 z 序。
    // 越过这个矩形的部分不画，狗压进任务栏的那一条就看不见了。
    void set_clip(const RECT& r) { clip_ = r; hasClip_ = true; }

    UINT index_count() const { return indexCount_; }
    UINT vertex_count() const { return vertexCount_; }

private:
    template <class T> using Ptr = Microsoft::WRL::ComPtr<T>;

    bool create_pipeline(ID3D12Device* device, DXGI_FORMAT rtvFormat);
    bool create_buffers(ID3D12Device* device, const Mesh& mesh);
    bool create_depth(ID3D12Device* device, UINT width, UINT height);

    Ptr<ID3D12RootSignature>  rootSig_;
    Ptr<ID3D12PipelineState>  pso_;
    Ptr<ID3D12PipelineState>  psoBlend_;   // 半透明部件（阴影）：预乘 alpha 混合，不写深度
    RECT clip_{};
    bool hasClip_ = false;
    Ptr<ID3D12Resource>       vb_;
    Ptr<ID3D12Resource>       ib_;
    Ptr<ID3D12Resource>       depth_;
    Ptr<ID3D12DescriptorHeap> dsvHeap_;

    D3D12_VERTEX_BUFFER_VIEW vbv_{};
    D3D12_INDEX_BUFFER_VIEW  ibv_{};
    UINT indexCount_ = 0;
    UINT vertexCount_ = 0;
    UINT width_ = 0, height_ = 0;
};

}  // namespace pet::gfx
