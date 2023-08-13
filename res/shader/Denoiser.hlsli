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

///////////////////////////////////////////////////////////////////////////////
// BilinearData structure
///////////////////////////////////////////////////////////////////////////////
struct BilinearData
{
    float2 origin;
    float2 weight;
};

//-----------------------------------------------------------------------------
//      バイリニア補間データを取得します.
//-----------------------------------------------------------------------------
BilinearData GetBilinearFilter(float2 uv, float2 screenSize)
{
    BilinearData result;
    result.origin = floor(uv * screenSize - 0.5f);
    result.weight = frac (uv * screenSize - 0.5f);
    return result;
}

//-----------------------------------------------------------------------------
//      カスタムウェイトを考慮したバイリニア補間のウェイトを取得します.
//-----------------------------------------------------------------------------
float4 GetBlinearCustomWeigths(BilinearData f, float4 customWeights)
{
    float4 weights;
    weights.x = (1.0f - f.weight.x) * (1.0f - f.weight.y);
    weights.y = f.weight.x * (1.0f - f.weight.y);
    weights.z = (1.0f - f.weight.x) * f.weight.y;
    weights.w = f.weight.x * f.weight.y;
    return weights * customWeights;
}

//-----------------------------------------------------------------------------
//      カスタムウェイトによるバイリニア補間を適用します.
//-----------------------------------------------------------------------------
float ApplyBilienarCustomWeigths(float s00, float s10, float s01, float s11, float4 w, bool normalize = true)
{
    float r = s00 * w.x + s10 * w.y + s01 * w.z + s11 * w.w;
    return r * (normalize ? rcp(dot(w, 1.0f)) : 1.0f);
}

//-----------------------------------------------------------------------------
//      カスタムウェイトによるバイリニア補間を適用します.
//-----------------------------------------------------------------------------
float2 ApplyBilienarCustomWeigths(float2 s00, float2 s10, float2 s01, float2 s11, float4 w, bool normalize = true)
{
    float2 r = s00 * w.x + s10 * w.y + s01 * w.z + s11 * w.w;
    return r * (normalize ? rcp(dot(w, 1.0f)) : 1.0f);
}

//-----------------------------------------------------------------------------
//      カスタムウェイトによるバイリニア補間を適用します.
//-----------------------------------------------------------------------------
float3 ApplyBilienarCustomWeigths(float3 s00, float3 s10, float3 s01, float3 s11, float4 w, bool normalize = true)
{
    float3 r = s00 * w.x + s10 * w.y + s01 * w.z + s11 * w.w;
    return r * (normalize ? rcp(dot(w, 1.0f)) : 1.0f);
}

//-----------------------------------------------------------------------------
//      カスタムウェイトによるバイリニア補間を適用します.
//-----------------------------------------------------------------------------
float4 ApplyBilienarCustomWeigths(float4 s00, float4 s10, float4 s01, float4 s11, float4 w, bool normalize = true)
{
    float4 r = s00 * w.x + s10 * w.y + s01 * w.z + s11 * w.w;
    return r * (normalize ? rcp(dot(w, 1.0f)) : 1.0f);
}

//-----------------------------------------------------------------------------
//      Karisのアンチファイアフライウェイトを計算します.
//-----------------------------------------------------------------------------
float KarisAntiFireflyWeight(float3 value, float exposure = 1.0f)
{ return rcp(4.0f + LuminanceBT709(value) * exposure); }

//-----------------------------------------------------------------------------
//      Karisのアンチファイアフライウェイトを計算します.
//-----------------------------------------------------------------------------
float KarisAntiFireflyWeightY(float luma, float exposure = 1.0f)
{ return rcp(4.0f + luma * exposure); }

//-----------------------------------------------------------------------------
//      ヒットポイントリプロジェクションを行います.
//-----------------------------------------------------------------------------
float2 HitPointReprojection
(
    float3   origVS,                // 反射レイの起点.
    float    reflectedRayLength,    // 衝突位置までの反射レイの長さ.
    float4x4 invView,               // ビュー行列の逆行列.
    float4x4 prevViewProj           // 前フレームのビュー射影行列.
)
{
    float3 posVS = origVS;
    float  primaryRayLength = length(posVS);
    posVS = normalize(posVS);   // 視線ベクトル方向.

    // Virtual Pointを求める.
    float rayLength = primaryRayLength + reflectedRayLength;
    posVS *= rayLength;
    
    // 前フレームでのUV座標を求める.
    float3 currPosWS = mul(invView, float4(posVS, 1.0f)).xyz;
    float4 prevPosCS = mul(prevViewProj, float4(currPosWS, 1.0f));
    float2 prevUV = (prevPosCS.xy / prevPosCS.w) * float2(0.5f, -0.5f) + 0.5f.xx;
    return prevUV;
}

//-----------------------------------------------------------------------------
//      モーションリプロジェクションを行います.
//-----------------------------------------------------------------------------
float2 MotionReprojection(float2 currUV, float2 motionVector)
{ return currUV + motionVector; }

//-----------------------------------------------------------------------------
//      リプロジェクションが有効かどうかチェックします.
//-----------------------------------------------------------------------------
bool CheckReprojectionValid
(
    int2        pixelPos,           // ピクセル位置 [0, 0] - [ScreenWidth, ScreenHeight]
    float3      prevPosWS,          // 前フレームのワールド空間位置.
    float3      prevNormal,         // 前フレームのワールド空間法線.
    float3      currPosWS,          // 現フレームのワールド空間位置.
    float3      currNormal,         // 現フレームのワールド空間法線.
    float       currLinearZ,        // 現フレームの線形深度.
    const int2  screenSize,         // スクリーンサイズ.
    const float distanceThreshold   // 平面距離の閾値.
)
{
    // 画面ないかどうか判定.
    if (any(pixelPos < int2(0, 0)) || any(pixelPos >= screenSize))
    { return false; }

    // 裏面ならヒストリーを棄却.
    if (dot(currNormal, prevNormal) < 0.0f)
    { return false; }

    // 平面距離が許容範囲かチェック.
    float3 posDiff      = currPosWS - prevPosWS;
    float  planeDist1   = abs(dot(posDiff, prevNormal));
    float  planeDist2   = abs(dot(posDiff, currNormal));
    float  maxPlaneDist = max(planeDist1, planeDist2);
    return (maxPlaneDist / currLinearZ) > distanceThreshold;
}

//-----------------------------------------------------------------------------
//      RGBからYCoCgに変換します.
//-----------------------------------------------------------------------------
float4 RGBToYCoCg(float4 value)
{
    float Y  = dot(value.rgb, float3( 1, 2,  1)) * 0.25;
    float Co = dot(value.rgb, float3( 2, 0, -2)) * 0.25 + (0.5 * 256.0 / 255.0);
    float Cg = dot(value.rgb, float3(-1, 2, -1)) * 0.25 + (0.5 * 256.0 / 255.0);
    return float4(Y, Co, Cg, value.a);
}

//-----------------------------------------------------------------------------
//      YCoCgからRGBに変換します.
//-----------------------------------------------------------------------------
float4 YCoCgToRGB(float4 YCoCg)
{
    float Y  = YCoCg.x;
    float Co = YCoCg.y - (0.5 * 256.0 / 255.0);
    float Cg = YCoCg.z - (0.5 * 256.0 / 255.0);
    float R  = Y + Co - Cg;
    float G  = Y + Cg;
    float B  = Y - Co - Cg;
    return float4(R, G, B, YCoCg.a);
}

//-----------------------------------------------------------------------------
//      ヒストリーカラーをクリップするためのバウンディングボックスを求めます.
//-----------------------------------------------------------------------------
void CalcColorBoundingBox
(
    Texture2D       map,
    SamplerState    smp,
    float2          uv,
    float4          curColor,   // YCoCg.
    float           gamma,
    out float4      minColor,   // YCoCg.
    out float4      maxColor    // YCoCg.
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

    const float invSamples = 1.0f / 9.0f;
    ave *= invSamples;
    var *= invSamples;
    var = sqrt(var - ave * ave) * gamma;

    // 分散クリッピング.
    minColor = float4(ave - var);
    maxColor = float4(ave + var);
}

//-----------------------------------------------------------------------------
//      AABBとの交差判定を行います.
//-----------------------------------------------------------------------------
float IntersectAABB(float3 dir, float3 orig, float3 mini, float3 maxi)
{
    float3 invDir = rcp(dir);
    float3 p0 = (mini - orig) * invDir;
    float3 p1 = (maxi - orig) * invDir;
    float3 t = min(p0, p1);
    return Max3(t);
}

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
//      スペキュラーローブの半角を求めます.
//-----------------------------------------------------------------------------
float GetSpecularLobeHalfAngle(float linearRoughess, float percentOfVolume = 0.75f)
{
    // Moving Frostbite to PBR v3.2 p.72
    float a = linearRoughess * linearRoughess;
    return atan(a * percentOfVolume / (1.0f - percentOfVolume));
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
