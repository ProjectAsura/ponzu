//-----------------------------------------------------------------------------
// File : PreBlurCS.hlsl
// Desc : Pre-Blur Pass.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <Math.hlsli>
#include <Samplers.hlsli>


//-----------------------------------------------------------------------------
// Resources
//-----------------------------------------------------------------------------
Texture2D<float4>   InputBuffer  : register(t0);
RWTexture2D<float4> OutputBuffer : register(u0);

cbuffer CbParam : register(b3)
{
    uint2   ScreenSize;
    float2  InvScreenSize;
};

//-----------------------------------------------------------------------------
//      メインエントリーポイントです.
//-----------------------------------------------------------------------------
[numthreads(8, 8, 1)]
void main
(
    uint3 dispatchId : SV_DispatchThreadID,
    uint  groupIndex : SV_GroupIndex
)
{
    uint2 remappedId = RemapLane8x8(dispatchId.xy, groupIndex);

    // スクリーン外なら処理しない.
    if (any(remappedId >= ScreenSize))
    { return; }

    float2 uv = (float2(remappedId) + 0.5f.xx) * InvScreenSize;

    float4 c0 = InputBuffer.SampleLevel(LinearClamp, uv - float2(0.5f, 0.0f) * InvScreenSize, 0.0f);
    float4 c1 = InputBuffer.SampleLevel(LinearClamp, uv + float2(0.5f, 0.0f) * InvScreenSize, 0.0f);
    float4 c2 = InputBuffer.SampleLevel(LinearClamp, uv - float2(0.0f, 0.5f) * InvScreenSize, 0.0f);
    float4 c3 = InputBuffer.SampleLevel(LinearClamp, uv + float2(0.0f, 0.5f) * InvScreenSize, 0.0f);

    float4 result = 0.0f.xxxx;
    float  weight = 0.0f;

    float w = KarisAntiFireflyWeight(c0.rgb);
    result += c0 * w;
    weight += w;

    w = KarisAntiFireflyWeight(c1.rgb);
    result += c1 * w;
    weight += w;

    w = KarisAntiFireflyWeight(c2.rgb);
    result += c2 * w;
    weight += w;

    w = KarisAntiFireflyWeight(c3.rgb);
    result += c3 * w;
    weight += w;

    if (weight > 0)
    { result /= weight; }

    OutputBuffer[remappedId] = result;
}
