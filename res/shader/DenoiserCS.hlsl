//-----------------------------------------------------------------------------
// File : DenoiserCS.hlsl
// Desc : バイラテラルフィルタによるデノイザ.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <Math.hlsli>

#define THREAD_SIZE     (8)
#define KERNEL_RADIUS   (2)

//-----------------------------------------------------------------------------
// Constant Values.
//-----------------------------------------------------------------------------
static const uint2 kThreadSize = uint2(THREAD_SIZE, THREAD_SIZE);


//-----------------------------------------------------------------------------
// Resources
//-----------------------------------------------------------------------------
RWTexture2D<float4> Output      : register(u0);
Texture2D<float4>   InputColor  : register(t0);
Texture2D<float>    InputDepth  : register(t1);
Texture2D<float3>   InputNormal : register(t2);
SamplerState        PointClamp  : register(s0);
SamplerState        LinearClamp : register(s1);

cbuffer Parameter : register(b0)
{
    uint2   DispatchArgs;
    float2  InvTargetSize;
    float2  Offset;
};

//-----------------------------------------------------------------------------
//      現在ピクセルの周辺の 3x3 のガウスブラーの分散を求める.
//-----------------------------------------------------------------------------
float ComputeVarianceCenter(int2 pos)
{
    float sum = 0.0f;

    const float kernel[2][2] = {
        { 1.0f / 4.0f, 1.0f / 8.0f },
        { 1.0f / 8.0f, 1.0f / 16.0f },
    };

    const int radius = 1;
    for(int y=-radius; y<=radius; y++)
    {
        for(int x=-radius; x<=radius; x++)
        {
            const int2  p = pos + int2(x, y);
            const float k = kernel[abs(x)][abs(y)];
            sum += InputColor.Load(int3(p, 0)).a * k;
        }
    }

    return sum;
}

//-----------------------------------------------------------------------------
//      バイラテラルフィルタを行います.
//-----------------------------------------------------------------------------
float4 BilateralFilter(float2 uv, float2 offset, float sqrtVarL)
{
    float4 output = (float4)0;

    float4 c0 = InputColor .SampleLevel(PointClamp, uv, 0.0f);
    float  d0 = InputDepth .SampleLevel(PointClamp, uv, 0.0f);
    float3 n0 = InputNormal.SampleLevel(PointClamp, uv, 0.0f) * 2.0f - 1.0f;
    n0 = normalize(n0);

    float4 totalColor  = c0;
    float  totalWeight = 1.0f;

    const float eps = 1e-5f;
    const float sn  = 128.0f;
    const float sz  = 0.5f;
    const float sl  = 2.0f;

    float r = 1;
    [unroll]
    for(; r <= KERNEL_RADIUS/2; r+=1)
    {
        float2 st = uv + r * offset;

        float4 c = InputColor .SampleLevel(PointClamp, st, 0.0f);
        float  d = InputDepth .SampleLevel(PointClamp, st, 0.0f);
        float3 n = InputNormal.SampleLevel(PointClamp, st, 0.0f) * 2.0f - 1.0f;
        n = normalize(n);

        // Edge-Stopping Function.
        float wn = pow(max(0.0f, dot(n0, n)), sn);
        float wz = exp(-abs(d0 - d) / (sz + eps));
        float wl = exp(-abs(c0.a - c.a) / (sl * sqrtVarL + eps));

        float w = wn * wz * wl;

        totalColor  += w * c;
        totalWeight += w;
    }

    for(; r <= KERNEL_RADIUS; r+=2)
    {
        float2 st = uv + r * offset;

        float4 c = InputColor .SampleLevel(LinearClamp, st, 0.0f);
        float  d = InputDepth .SampleLevel(LinearClamp, st, 0.0f);
        float3 n = InputNormal.SampleLevel(LinearClamp, st, 0.0f) * 2.0f - 1.0f;
        n = normalize(n);

        // Edge-Stopping Function.
        float wn = pow(max(0.0f, dot(n0, n)), sn);
        float wz = exp(-abs(d0 - d) / (sz + eps));
        float wl = exp(-abs(c0.a - c.a) / (sl * sqrtVarL + eps));

        float w = wn * wz * wl;

        totalColor  += w * c;
        totalWeight += w;
    }

    r = 1;
    [unroll]
    for(; r <= KERNEL_RADIUS/2; r+=1)
    {
        float2 st = uv - r * offset;

        float4 c = InputColor .SampleLevel(PointClamp, st, 0.0f);
        float  d = InputDepth .SampleLevel(PointClamp, st, 0.0f);
        float3 n = InputNormal.SampleLevel(PointClamp, st, 0.0f) * 2.0f - 1.0f;
        n = normalize(n);

        // Edge-Stopping Function.
        float wn = pow(max(0.0f, dot(n0, n)), sn);
        float wz = exp(-abs(d0 - d) / (sz + eps));
        float wl = exp(-abs(c0.a - c.a) / (sl * sqrtVarL + eps));

        float w = wn * wz * wl;

        totalColor  += w * c;
        totalWeight += w;
    }

    for(; r <= KERNEL_RADIUS; r+=2)
    {
        float2 st = uv - r * offset;

        float4 c = InputColor .SampleLevel(LinearClamp, st, 0.0f);
        float  d = InputDepth .SampleLevel(LinearClamp, st, 0.0f);
        float3 n = InputNormal.SampleLevel(LinearClamp, st, 0.0f) * 2.0f - 1.0f;
        n = normalize(n);

        // Edge-Stopping Function.
        float wn = pow(max(0.0f, dot(n0, n)), sn);
        float wz = exp(-abs(d0 - d) / (sz + eps));
        float wl = exp(-abs(c0.a - c.a) / (sl * sqrtVarL + eps));

        float w = wn * wz * wl;

        totalColor  += w * c;
        totalWeight += w;
    }

    output.rgb = saturate(totalColor.rgb / totalWeight);
    output.a   = c0.a;

    return output;
}

//-----------------------------------------------------------------------------
//      メインエントリーポイントです.
//-----------------------------------------------------------------------------
[numthreads(THREAD_SIZE, THREAD_SIZE, 1)]
void main
(
    uint3 groupId       : SV_GroupID,
    uint3 groupThreadId : SV_GroupThreadID
)
{
    uint2 dispatchId = RemapThreadId(kThreadSize, DispatchArgs, 8, groupId.xy, groupThreadId.xy);

    // テクスチャ座標を求める.
    float2 uv = (dispatchId.xy + 0.5f) * InvTargetSize;

    // 3x3 のガウスの分散を求める.
    float var = ComputeVarianceCenter((int2)dispatchId);
    var = sqrt(var);

    float4 result = BilateralFilter(uv, Offset, var);

    // fpngがアルファ1.0で出力しないとバグるためデノイズ結果は1.0で出力.
    if (Offset.y > 0.0f)
    { result.a = 1.0f; }

    Output[dispatchId.xy] = result;
}
