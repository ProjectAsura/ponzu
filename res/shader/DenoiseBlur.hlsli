//-----------------------------------------------------------------------------
// File : DenoiseBlur.hlsli
// Desc : Denoise Blur
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <Denoiser.hlsli>

#ifndef KERNEL_RADIUS
#define KERNEL_RADIUS   (3)
#endif//KERNEL_RADIUS


///////////////////////////////////////////////////////////////////////////////
// Parameters
///////////////////////////////////////////////////////////////////////////////
cbuffer Parameters : register(b0)
{
    uint2       ScreenSize;
    uint        IgnoreHistory;
    float       Sharpness;

    float4x4    Proj;
    float4x4    View;
    float       NearClip;
    float       FarClip;
    float2      UVToViewParam;
};

///////////////////////////////////////////////////////////////////////////////
// Constants
///////////////////////////////////////////////////////////////////////////////
cbuffer Constants : register(b1)
{
    float2  BlurOffset;
    float   BlurRadius;
    float   Reserved;
};

//-----------------------------------------------------------------------------
//  Resources
//-----------------------------------------------------------------------------
Texture2D<float>    DepthBuffer         : register(t0);
Texture2D<float2>   NormalBuffer        : register(t1);
Texture2D<float>    RoughnessBuffer     : register(t2);
Texture2D<float>    HitDistanceBuffer   : register(t3);
Texture2D<float4>   InputBuffer         : register(t4);
Texture2D<uint>     AccumCountBuffer    : register(t5);
RWTexture2D<float4> DenoisedBuffer      : register(u0);


//-----------------------------------------------------------------------------
//      深度の重みを求めます.
//-----------------------------------------------------------------------------
float CalcDepthWeight(float r, float d, float d0)
{
    // 参考. "Stable SSAO in Battlefield 3 with Scelective Temporal Filtering", GDC 2012,
    // https://www.ea.com/frostbite/news/stable-ssao-in-battlefield-3-with-selective-temporal-filtering

    // fxcで最適化される
    const float BlurSigma = ((float)KERNEL_RADIUS + 1.0f) * 0.5f;
    const float BlurFallOff = 1.0 / (2.0f * BlurSigma * BlurSigma);

    // dとd0は線形深度値とする.
    float dz = (d0 - d) * Sharpness;
    return exp2(-r * r * BlurFallOff - dz * dz);
}

//-----------------------------------------------------------------------------
//      法線の重みを求めます.
//-----------------------------------------------------------------------------
float CalcNormalWeight(float3 n, float3 n0)
{
    return pow(max(0.0f, dot(n, n0)), 128);
}

//-----------------------------------------------------------------------------
//      ラフネスの重みを求めます.
//-----------------------------------------------------------------------------
float CalcRoughnessWeight(float roughness, float roughness0)
{
    float norm = Pow2(roughness0) * 0.99f + 0.01f;
    float w = abs(roughness0 - roughness) * rcp(norm);
    return saturate(1.0f - w);
}

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

    // スクリーン範囲外なら処理しない.
    if (any(remappedId >= ScreenSize))
    { return; }

    float2 uv = (float2(remappedId) + 0.5f.xx) / float2(ScreenSize);
    float  z  = DepthBuffer.SampleLevel(PointClamp, uv, 0.0f);

    // 背景なら処理しない.
    if (z >= 1.0f)
    {
        // ブラー結果を出力.
        DenoisedBuffer  [remappedId] = InputBuffer.SampleLevel(PointClamp, uv, 0.0f);
        //AccumCountBuffer[remappedId] = 0;
        return;
    }
    const uint  MaxAccumCount = 128;

    // アキュムレーションフレーム数.
    uint accumCount = AccumCountBuffer[remappedId];
    accumCount = min(accumCount, MaxAccumCount);

    // ヒストリーが無効な場合.
    if (IgnoreHistory & 0x1)
    { accumCount = 1; }

    // アキュムレーションスピード.
    float accumSpeed = 1.0f / (1.0f + float(accumCount));

    float4 color  = InputBuffer.SampleLevel(PointClamp, uv, 0.0f);
    float  weight = KarisAntiFireflyWeight(color.rgb, 1.0f);

    float3 n     = UnpackNormal(NormalBuffer.SampleLevel(PointClamp, uv, 0.0f));
    float  rough = RoughnessBuffer.SampleLevel(PointClamp, uv, 0.0f);
    
    float  totalWeight = weight;
    float4 result      = color * weight;
    
    float3 posVS     = ToViewPos(uv, ToViewDepth(z, NearClip, FarClip), UVToViewParam);
    float  dist      = length(posVS);
    float  hitDist   = HitDistanceBuffer.SampleLevel(PointClamp, uv, 0.0f);
    float  factor    = hitDist / (hitDist + dist);
    float  blurScale = BlurRadius * lerp(rough, 1.0f, factor) * accumSpeed;
    
 
    [unroll] for(float r = 1.0f; r <= KERNEL_RADIUS; r += 1.0f)
    {
        float2 st0 = uv + BlurOffset * r * blurScale;
        float2 st1 = uv - BlurOffset * r * blurScale;

        float z0 = DepthBuffer.SampleLevel(PointClamp, st0, 0.0f);
        float z1 = DepthBuffer.SampleLevel(PointClamp, st1, 0.0f);

        float3 n0 = UnpackNormal(NormalBuffer.SampleLevel(LinearClamp, st0, 0.0f));
        float3 n1 = UnpackNormal(NormalBuffer.SampleLevel(LinearClamp, st1, 0.0f));

        float rough0 = RoughnessBuffer.SampleLevel(LinearClamp, st0, 0.0f);
        float rough1 = RoughnessBuffer.SampleLevel(LinearClamp, st1, 0.0f);

        float4 c0 = InputBuffer.SampleLevel(LinearClamp, st0, 0.0f);
        float4 c1 = InputBuffer.SampleLevel(LinearClamp, st1, 0.0f);

        float w0 = KarisAntiFireflyWeight(c0.rgb, 1.0f);
        w0 *= CalcDepthWeight(r, z0, z);
        w0 *= CalcNormalWeight(n0, n);
        w0 *= CalcRoughnessWeight(rough0, rough);
        
        float w1 = KarisAntiFireflyWeight(c1.rgb, 1.0f);
        w1 *= CalcDepthWeight(r, z1, z);
        w1 *= CalcNormalWeight(n1, n);
        w1 *= CalcRoughnessWeight(rough1, rough);

        totalWeight += w0;
        result += c0 * w0;

        totalWeight += w1;
        result += c1 * w1;
    }

    if (totalWeight > 0)
    { result *= rcp(totalWeight); }

    //accumCount++;
    DenoisedBuffer  [remappedId] = result;
    //AccumCountBuffer[remappedId] = accumCount;
}

