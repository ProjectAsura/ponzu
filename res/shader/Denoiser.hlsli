//-----------------------------------------------------------------------------
// File : Denoiser.hlsli
// Desc : Desnoiser Common Functions.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------
#ifndef DENOISER_HLSLI
#define DENOISER_HLSLI

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <Math.hlsli>
#include <Samplers.hlsli>
#include <BRDF.hlsli>
#include <TextureUtil.hlsli>


//-----------------------------------------------------------------------------
// Constant Values.
//-----------------------------------------------------------------------------
static const int2 kOffsets[8] =
{
    int2(-1, -1),
    int2(-1,  1),
    int2( 1, -1),
    int2( 1,  1),
    int2( 1,  0),
    int2( 0, -1),
    int2( 0,  1),
    int2(-1,  0)
};

//-----------------------------------------------------------------------------
//      Catmull-Rom フィルタリング.
//-----------------------------------------------------------------------------
float4 BicubicSampleCatmullRom(Texture2D map, SamplerState smp, float2 uv, float2 mapSize)
{
    const float2 kInvMapSize = rcp(mapSize);
    float2 samplePos = uv * mapSize;
    float2 tc = floor(samplePos - 0.5f) + 0.5f;
    float2 f  = samplePos - tc;
    float2 f2 = f * f;
    float2 f3 = f * f2;

    float2 w0 = f2 - 0.5f * (f3 + f);
    float2 w1 = 1.5f * f3 - 2.5f * f2 + 1;
    float2 w3 = 0.5f * (f3 - f2);
    float2 w2 = 1 - w0 - w1 - w3;

    float2 w12 = w1 + w2;

    float2 tc0  = (tc - 1) * kInvMapSize;
    float2 tc12 = (tc + w2 / w12) * kInvMapSize;
    float2 tc3  = (tc + 2) * kInvMapSize;

    float4 result = float4(0.0f, 0.0f, 0.0f, 0.0f);

    result += map.SampleLevel(smp, float2(tc0.x,  tc0.y),  0) * (w0.x  * w0.y);
    result += map.SampleLevel(smp, float2(tc0.x,  tc12.y), 0) * (w0.x  * w12.y);
    result += map.SampleLevel(smp, float2(tc0.x,  tc3.y),  0) * (w0.x  * w3.y);

    result += map.SampleLevel(smp, float2(tc12.x, tc0.y),  0) * (w12.x * w0.y);
    result += map.SampleLevel(smp, float2(tc12.x, tc12.y), 0) * (w12.x * w12.y);
    result += map.SampleLevel(smp, float2(tc12.x, tc3.y),  0) * (w12.x * w3.y);

    result += map.SampleLevel(smp, float2(tc3.x,  tc0.y),  0) * (w3.x * w0.y);
    result += map.SampleLevel(smp, float2(tc3.x,  tc12.y), 0) * (w3.x * w12.y);
    result += map.SampleLevel(smp, float2(tc3.x,  tc3.y),  0) * (w3.x * w3.y);

    return max(result, 0.0f.xxxx);
}

//-----------------------------------------------------------------------------
//      ヒストリーカラーをクリップするためのバウンディングボックスを求めます.
//-----------------------------------------------------------------------------
void CalcColorBoundingBox
(
    Texture2D       map,
    SamplerState    smp,
    float2          uv,
    float4          curColor,   // YCoCgとします.
    float           gamma,
    out float4      minColor,   // YCoCgとします.
    out float4      maxColor    // YCoCgとします.
)
{
    // 平均と分散を求める.
    float4 ave = curColor;
    float4 var = curColor * curColor;

    [unroll]
    for (uint i = 0; i < 8; ++i)
    {
        float4 newColor = RGBToYCoCg(map.SampleLevel(smp, uv, 0.0f, kOffsets[i]));
        ave += newColor;
        var += newColor * newColor;
    }

    const float kInvSamples = 1.0f / 9.0f;
    ave *= kInvSamples;
    var *= kInvSamples;
    var = sqrt(var - ave * ave) * gamma;

    // 分散クリッピング.
    minColor = float4(ave - var);
    maxColor = float4(ave + var);
}

//-----------------------------------------------------------------------------
//      重みを求めます.
//-----------------------------------------------------------------------------
float CalcHdrWeightY(float3 ycocg, float exposure = 1.0f)
{ return rcp(ycocg.x * exposure + 4.0f); }

//-----------------------------------------------------------------------------
//      ブレンドウェイトを求めます.
//-----------------------------------------------------------------------------
float2 CalcBlendWeight(float historyWeight, float currentWeight, float blend)
{
    float blendH = (1.0f - blend) * historyWeight;
    float blendC = blend * currentWeight;
    return float2(blendH, blendC) * rcp(blendH + blendC);
}

//-----------------------------------------------------------------------------
//      非指数ウェイトを計算します.
//-----------------------------------------------------------------------------
float ComputeNonExponentialWeight(float x, float px, float py)
{ return SmoothStep(0.999f, 0.001f, abs(x * px + py)); }

//-----------------------------------------------------------------------------
//      非指数ウェイトを計算します.
//-----------------------------------------------------------------------------
float3 ComputeNonExponentialWeight(float3 x, float3 px, float3 py)
{ return SmoothStep(0.999f, 0.001f, abs(x * px + py)); }

//-----------------------------------------------------------------------------
//      指数ウェイトを計算します.
//-----------------------------------------------------------------------------
float ComputeExponentialWeight(float x, float px, float py)
{ return exp(-3.0f * abs(x * px + py)); }

//-----------------------------------------------------------------------------
//      ラフネスウェイトパラメータを計算します.
//-----------------------------------------------------------------------------
float2 CalcRoughnessWeightParams(float roughness, float fraction)
{
    float a = rcp(lerp(0.01f, 1.0f, saturate(roughness * fraction)));
    float b = roughness * a;
    return float2(a, -b);
}

//-----------------------------------------------------------------------------
//      ヒット距離に基づくウェイトパラメータを計算します.
//-----------------------------------------------------------------------------
float2 CalcHitDistanceWeightParams(float hitDist, float nonLinearAccumSpeed, float roughness = 1.0f)
{
    const float percentOfVolume = 0.987f;
    float angle        = GetSpecularLobeHalfAngle(roughness, percentOfVolume);
    float almostHalfPi = GetSpecularLobeHalfAngle(1.0f, percentOfVolume);

    float specularMagicCurve = saturate(angle / almostHalfPi);
    float norm = lerp(1e-6f, 1.0f, min(nonLinearAccumSpeed, specularMagicCurve));
    float a = 1.0f / norm;
    float b = hitDist * a;
    return float2(a, -b);
}

//-----------------------------------------------------------------------------
//      錐台サイズを計算します.
//-----------------------------------------------------------------------------
float CalcFrustumSize(float minRectDimMulUnproject, float orthoMode, float viewZ)
{
    // projectY  = abs(2.0f / (top - bottom));
    // unproject = 1.0f / (0.5f * rectH * projectY);
    // minRectDimMulUnproject = min(rectW, rectH) * unproject;
    return minRectDimMulUnproject * lerp(viewZ, 1.0f, abs(orthoMode));
}

//-----------------------------------------------------------------------------
//      ヒット距離因子を求めます.
//-----------------------------------------------------------------------------
float CalcHitDistFactor(float hitDist, float frustumSize)
{ return saturate(hitDist / frustumSize); }

//-----------------------------------------------------------------------------
//      ジオメトリウェイトパラメータを計算します.
//-----------------------------------------------------------------------------
float2 CalcGeometryWeightParams(float planeDistSensitivity, float frustumSize, float3 posVS, float3 normalVS, float nonLinearAccumSpeed)
{
    float relaxation = lerp(1.0f, 0.25f, nonLinearAccumSpeed);
    float a = relaxation / (planeDistSensitivity * frustumSize);
    float b = dot(normalVS, posVS) * a;
    return float2(a, -b);
}

//-----------------------------------------------------------------------------
//      法線ウェイトパラメータを計算します.
//-----------------------------------------------------------------------------
float CalcNormalWeightParams(float nonLinearAccumSpeed, float fraction, float roughness = 1.0f)
{
    float angle = GetSpecularLobeHalfAngle(roughness);
    angle *= lerp(saturate(fraction), 1.0f, nonLinearAccumSpeed);
    return 1.0f / max(angle, 2.0f / 255.0f);
}

//-----------------------------------------------------------------------------
//      ラフネスウェイトを計算します.
//-----------------------------------------------------------------------------
float CalcRoughnessWeight(float2 params, float roughness)
{ return ComputeNonExponentialWeight(roughness, params.x, params.y); }

//-----------------------------------------------------------------------------
//      ヒット距離ウェイトを計算します.
//-----------------------------------------------------------------------------
float CalcHitDistanceWeight(float2 params, float hitDist)
{ return ComputeExponentialWeight(hitDist, params.x, params.y); }

//-----------------------------------------------------------------------------
//      ジオメトリウェイトを計算します.
//-----------------------------------------------------------------------------
float CalcGeometryWeight(float2 params, float3 n0, float3 p)
{
    float d = dot(n0, p);
    return ComputeNonExponentialWeight(d, params.x, params.y);
}

//-----------------------------------------------------------------------------
//      法線ウェイトを計算します.
//-----------------------------------------------------------------------------
float CalcNormalWeight(float param, float3 N, float3 n)
{
    float cosA = saturate(dot(N, n));
    float angle = acos(cosA);
    return ComputeNonExponentialWeight(angle, param, 0.0f);
}

#endif//DENOISER_HLSLI
