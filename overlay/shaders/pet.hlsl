// 代理体的顶点与像素着色器。构建期由 fxc 编译成字节数组（见 overlay/CMakeLists.txt），
// 运行时不再依赖 d3dcompiler_47.dll。
// 矩阵在 C++ 侧是行主序，这里必须写 row_major，否则会被静默转置。

cbuffer Scene : register(b0)
{
    row_major float4x4 mvp;
    row_major float4x4 model;
    float4 lightDir;   // xyz 为方向，w 未用
    float4 tint;       // rgb 乘色，a 不透明度；a<1 的部件走混合管线且不受光
};

struct VSIn  { float3 pos : POSITION; float3 nrm : NORMAL; float3 col : COLOR; };
struct VSOut { float4 pos : SV_POSITION; float3 nrm : NORMAL; float3 col : COLOR; };

VSOut VSMain(VSIn i)
{
    VSOut o;
    o.pos = mul(float4(i.pos, 1.0), mvp);
    o.nrm = mul(float4(i.nrm, 0.0), model).xyz;
    o.col = i.col;
    return o;
}

float4 PSMain(VSOut i) : SV_TARGET
{
    float3 n = normalize(i.nrm);
    float  ndl = saturate(dot(n, -normalize(lightDir.xyz)));
    // 半兰伯特加一点环境光，避免背光面纯黑。半透明部件（阴影）不受光。
    float  lit = tint.a < 1.0 ? 1.0 : (0.30 + 0.70 * ndl);
    float3 c = i.col * tint.rgb * lit;

    // 预乘 alpha：交换链是 DXGI_ALPHA_MODE_PREMULTIPLIED，
    // 输出的 RGB 必须已经乘过 alpha，否则边缘发亮。
    float a = tint.a;
    return float4(c * a, a);
}
