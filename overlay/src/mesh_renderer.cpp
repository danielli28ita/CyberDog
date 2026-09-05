#include "overlay/mesh_renderer.h"

// 构建期由 fxc 生成的着色器字节码（见 overlay/CMakeLists.txt）。
#include "pet_vs.h"
#include "pet_ps.h"
#include <dxgi1_6.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace pet::gfx {
namespace {

bool ok(HRESULT hr, const char* what) {
    if (SUCCEEDED(hr)) return true;
    std::printf("  [FAIL] %s  hr=0x%08lX\n", what, static_cast<unsigned long>(hr));
    return false;
}

// 常量缓冲。用根常量直接塞进命令列表，省一个资源。
// 两个 4x4 加一个 float4，共 36 个 32 位值，在根签名 64 个的上限之内。
struct SceneConstants {
    Mat4  mvp;
    Mat4  model;        // 只用来转法线。部件只有旋转和轴向缩放，长方体的法线沿轴，
                        // 所以直接乘 model 再归一化就是对的，不需要逆转置
    float lightDir[4];
    float tint[4];      // rgb 乘色，a 是不透明度。阴影用：不受光、半透明
};

// 着色器源码在 overlay/shaders/pet.hlsl。矩阵是行主序，那边必须写 row_major。

}  // namespace

bool MeshRenderer::init(ID3D12Device* device, DXGI_FORMAT rtvFormat, UINT width, UINT height,
                        const Mesh& mesh) {
    width_ = width;
    height_ = height;
    if (!create_pipeline(device, rtvFormat)) return false;
    if (!create_buffers(device, mesh)) return false;
    if (!create_depth(device, width, height)) return false;
    return true;
}

void MeshRenderer::release() {
    dsvHeap_.Reset();
    depth_.Reset();
    ib_.Reset();
    vb_.Reset();
    psoBlend_.Reset();
    pso_.Reset();
    rootSig_.Reset();
    indexCount_ = 0;
    vertexCount_ = 0;
}

bool MeshRenderer::create_pipeline(ID3D12Device* device, DXGI_FORMAT rtvFormat) {
    // 根签名：一组根常量，没有描述符表。网格渲染就这点参数。
    D3D12_ROOT_PARAMETER param{};
    param.ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    param.Constants.ShaderRegister = 0;
    param.Constants.Num32BitValues = sizeof(SceneConstants) / 4;
    param.ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = 1;
    rsDesc.pParameters   = &param;
    rsDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Ptr<ID3DBlob> sig, err;
    if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err))) {
        if (err) std::printf("  [FAIL] 根签名序列化: %s\n", static_cast<const char*>(err->GetBufferPointer()));
        return false;
    }
    if (!ok(device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                        IID_PPV_ARGS(&rootSig_)), "CreateRootSignature")) return false;

    // 着色器在构建期由 fxc 编好（overlay/shaders/pet.hlsl → shaders_gen/pet_*.h），这里只是字节数组。
    const D3D12_SHADER_BYTECODE vsCode{g_pet_vs, sizeof(g_pet_vs)};
    const D3D12_SHADER_BYTECODE psCode{g_pet_ps, sizeof(g_pet_ps)};

    const D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature        = rootSig_.Get();
    pd.VS                    = vsCode;
    pd.PS                    = psCode;
    pd.InputLayout           = {layout, _countof(layout)};
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets      = 1;
    pd.RTVFormats[0]         = rtvFormat;
    pd.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
    pd.SampleDesc            = {1, 0};
    pd.SampleMask            = UINT_MAX;

    pd.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode              = D3D12_CULL_MODE_BACK;
    pd.RasterizerState.FrontCounterClockwise = FALSE;
    pd.RasterizerState.DepthClipEnable       = TRUE;

    pd.DepthStencilState.DepthEnable    = TRUE;
    pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pd.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;

    // 不混合。透明由 alpha 通道交给合成器处理，这里直接写。
    for (auto& rt : pd.BlendState.RenderTarget) {
        rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }

    if (!ok(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso_)),
            "CreateGraphicsPipelineState")) return false;

    // 半透明管线：预乘 alpha 的 SrcOver，不写深度（阴影贴在地上，不该挡住脚）。
    auto& rt0 = pd.BlendState.RenderTarget[0];
    rt0.BlendEnable    = TRUE;
    rt0.SrcBlend       = D3D12_BLEND_ONE;
    rt0.DestBlend      = D3D12_BLEND_INV_SRC_ALPHA;
    rt0.BlendOp        = D3D12_BLEND_OP_ADD;
    rt0.SrcBlendAlpha  = D3D12_BLEND_ONE;
    rt0.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    rt0.BlendOpAlpha   = D3D12_BLEND_OP_ADD;
    pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    return ok(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&psoBlend_)),
              "CreateGraphicsPipelineState(blend)");
}

bool MeshRenderer::create_buffers(ID3D12Device* device, const Mesh& mesh) {
    vertexCount_ = static_cast<UINT>(mesh.vertices.size());
    indexCount_  = static_cast<UINT>(mesh.indices.size());
    const UINT vbBytes = vertexCount_ * sizeof(MeshVertex);
    const UINT ibBytes = indexCount_ * sizeof(std::uint32_t);

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;  // 网格很小且不变，上传堆够用，省一次拷贝

    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc       = {1, 0};
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    auto make = [&](UINT bytes, Ptr<ID3D12Resource>& out, const void* src, const char* what) {
        rd.Width = bytes;
        if (!ok(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                IID_PPV_ARGS(&out)), what)) return false;
        void* p = nullptr;
        D3D12_RANGE noRead{0, 0};
        if (!ok(out->Map(0, &noRead, &p), "Map")) return false;
        std::memcpy(p, src, bytes);
        out->Unmap(0, nullptr);
        return true;
    };

    if (!make(vbBytes, vb_, mesh.vertices.data(), "创建顶点缓冲")) return false;
    if (!make(ibBytes, ib_, mesh.indices.data(), "创建索引缓冲")) return false;

    vbv_ = {vb_->GetGPUVirtualAddress(), vbBytes, sizeof(MeshVertex)};
    ibv_ = {ib_->GetGPUVirtualAddress(), ibBytes, DXGI_FORMAT_R32_UINT};
    return true;
}

bool MeshRenderer::create_depth(ID3D12Device* device, UINT width, UINT height) {
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width            = width;
    rd.Height           = height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_D32_FLOAT;
    rd.SampleDesc       = {1, 0};
    rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;

    if (!ok(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                            D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
                                            IID_PPV_ARGS(&depth_)), "创建深度缓冲")) return false;

    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.NumDescriptors = 1;
    hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    if (!ok(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&dsvHeap_)), "CreateDescriptorHeap(DSV)"))
        return false;

    device->CreateDepthStencilView(depth_.Get(), nullptr,
                                   dsvHeap_->GetCPUDescriptorHandleForHeapStart());
    return true;
}

void MeshRenderer::draw(ID3D12GraphicsCommandList* cmd, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                        const Mesh& mesh, const Mat4* partWorld,
                        const Mat4& viewProj, Vec3 lightDir, const unsigned char* partBehind) {
    if (!pso_) return;

    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    const D3D12_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_),
                            0.0f, 1.0f};
    const D3D12_RECT full{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
    D3D12_RECT sc = full;
    if (hasClip_) {
        sc.left = (std::max)(sc.left, clip_.left);
        sc.top = (std::max)(sc.top, clip_.top);
        sc.right = (std::min)(sc.right, clip_.right);
        sc.bottom = (std::min)(sc.bottom, clip_.bottom);
    }
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);
    // 栏杆前的部件不裁；每次切换裁剪矩形只是一条命令，部件二十来个，无所谓。
    bool scissorIsClip = true;
    auto use_clip = [&](bool clipIt) {
        if (clipIt == scissorIsClip) return;
        cmd->RSSetScissorRects(1, clipIt ? &sc : &full);
        scissorIsClip = clipIt;
    };

    SceneConstants cb{};
    cb.lightDir[0] = lightDir.x;
    cb.lightDir[1] = lightDir.y;
    cb.lightDir[2] = lightDir.z;
    cb.tint[0] = cb.tint[1] = cb.tint[2] = cb.tint[3] = 1.0f;

    cmd->SetGraphicsRootSignature(rootSig_.Get());
    cmd->SetPipelineState(pso_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 1, &vbv_);
    cmd->IASetIndexBuffer(&ibv_);

    if (mesh.parts.empty()) {
        // 没有部件信息的网格：整个当一个部件画。
        cb.model = Mat4::identity();
        cb.mvp   = viewProj;
        cmd->SetGraphicsRoot32BitConstants(0, sizeof(SceneConstants) / 4, &cb, 0);
        cmd->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
        return;
    }
    // 两遍：先画不透明部件，再换混合管线画半透明的（阴影）。
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1) cmd->SetPipelineState(psoBlend_.Get());
        for (size_t i = 0; i < mesh.parts.size(); ++i) {
            const MeshPart& part = mesh.parts[i];
            if (part.indexCount == 0) continue;
            const bool translucent = part.alpha < 1.0f;
            if (translucent != (pass == 1)) continue;
            use_clip(!partBehind || partBehind[i] != 0);
            cb.model = partWorld[i];
            cb.mvp   = partWorld[i] * viewProj;
            cb.tint[3] = part.alpha;
            cmd->SetGraphicsRoot32BitConstants(0, sizeof(SceneConstants) / 4, &cb, 0);
            cmd->DrawIndexedInstanced(part.indexCount, 1, part.firstIndex, 0, 0);
        }
    }
}

}  // namespace pet::gfx
