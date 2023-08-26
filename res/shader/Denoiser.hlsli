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
static const int2 kNeighborOffsets[8] =
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

static const float3 kPoisson8[8] =
{
    float3(-0.4706069, -0.4427112,  0.6461146),
    float3(-0.9057375,  0.3003471,  0.9542373),
    float3(-0.3487388,  0.4037880,  0.5335386),
    float3( 0.1023042,  0.6439373,  0.6520134),
    float3( 0.5699277,  0.3513750,  0.6695386),
    float3( 0.2939128, -0.1131226,  0.3149309),
    float3( 0.7836658, -0.4208784,  0.8895339),
    float3( 0.1564120, -0.8198990,  0.8346850)
};

//-----------------------------------------------------------------------------
//      速度ベクトルを取得します.
//-----------------------------------------------------------------------------
float2 GetVelocity(Texture2D<float2> map, SamplerState smp, float2 uv)
{
    float2 result       = map.SampleLevel(smp, uv, 0.0f);
    float  currLengthSq = dot(result, result);

    // 最も長い速度ベクトルを取得.
    [unroll] for(uint i=0; i<8; ++i)
    {
        float2 velocity = map.SampleLevel(smp, uv, 0.0f, kNeighborOffsets[i]);
        float  lengthSq = dot(velocity, velocity);
        if (lengthSq > currLengthSq)
        {
            result       = velocity;
            currLengthSq = lengthSq;
        }
    }

    return result;
}

//-----------------------------------------------------------------------------
//      隣接ピクセルを考慮した現在カラーを取得します.
//-----------------------------------------------------------------------------
float4 GetCurrentNeighborColor
(
    Texture2D       map,
    SamplerState    smp,
    float2          uv,
    float4          currentColor
)
{
    const float kCenterWeight = 4.0f;
    float4 accColor = currentColor * kCenterWeight;
    [unroll] 
    for(uint i=0; i<4; ++i)
    { accColor += map.SampleLevel(smp, uv, 0.0f, kNeighborOffsets[i]); }
    const float kInvWeight = 1.0f / (4.0f + kCenterWeight);
    accColor *= kInvWeight;
    return accColor;
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
        float4 newColor = RGBToYCoCg(map.SampleLevel(smp, uv, 0.0f, kNeighborOffsets[i]));
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
//      指数ウェイトを計算します.
//-----------------------------------------------------------------------------
float3 ComputeExponentialWeight(float3 x, float3 px, float3 py)
{ return exp(-3.0f * abs(x * px + py)); }

//-----------------------------------------------------------------------------
//      錐台サイズを計算します.
//-----------------------------------------------------------------------------
float CalcFrustumSize(float minRectDimMulUnproject, float orthoMode, float viewZ)
{
    // Ortho : 
    //      y0 = -plane[PLANE_BOTTOM].w;
    //      y1 =  plane[PLANE_TOP].w;
    // Perspecctive :
    //      y0 = plane[PLANE_BOTTOM].z / plane[PLANE_BOTTOM].y;
    //      y1 = plane[PLANE_TOP].z / plane[PLANE_TOP].y;
    // projectY  = 2.0f / (y1 - y0);
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
//      ラフネスウェイトパラメータを計算します.
//-----------------------------------------------------------------------------
float2 CalcRoughnessWeightParams(float roughness, float fraction)
{
    float a = rcp(lerp(0.01f, 1.0f, saturate(roughness * fraction)));
    float b = roughness * a;
    return float2(a, -b);
}

//-----------------------------------------------------------------------------
//      Specular Magic Curve.
//-----------------------------------------------------------------------------
float CalcSpecularMagicCurve(float roughness)
{
    float angle        = GetSpecularLobeHalfAngle(roughness, 0.987f);
    float almostHalfPi = GetSpecularLobeHalfAngle(1.0f, 0.987f);
    return saturate(angle / almostHalfPi);
}

//-----------------------------------------------------------------------------
//      ヒット距離に基づくウェイトパラメータを計算します.
//-----------------------------------------------------------------------------
float2 CalcHitDistanceWeightParams
(
    float hitDistance,
    float nonLinearAccumSpeed,
    float roughness = 1.0f
)
{
    float specularMagicCurve = CalcSpecularMagicCurve(roughness);
    float norm = lerp(1e-6f, 1.0f, min(nonLinearAccumSpeed, specularMagicCurve));
    float a = 1.0f / norm;
    float b = hitDistance * a;
    return float2(a, -b);
}

//-----------------------------------------------------------------------------
//      ジオメトリウェイトパラメータを計算します.
//-----------------------------------------------------------------------------
float2 CalcGeometryWeightParams
(
    float   planeDistSensitivity,   // 0.005f
    float   frustumSize,            // from CalcFrustumSize().
    float3  posVS,
    float3  normalVS,
    float   nonLinearAccumSpeed
)
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
    float angle = GetSpecularLobeHalfAngle(roughness, 0.75f);
    angle *= lerp(saturate(fraction), 1.0f, nonLinearAccumSpeed);
    return 1.0f / max(angle, 2.0f / 255.0f);
}

//-----------------------------------------------------------------------------
//      ラフネスウェイトを計算します.
//-----------------------------------------------------------------------------
float CalcRoughnessWeight(float2 params, float roughness)
{ return ComputeExponentialWeight(roughness, params.x, params.y); }

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
    return ComputeExponentialWeight(d, params.x, params.y);
}

//-----------------------------------------------------------------------------
//      法線ウェイトを計算します.
//-----------------------------------------------------------------------------
float CalcNormalWeight(float param, float3 N, float3 n)
{
    float cosA = saturate(dot(N, n));
    float angle = acos(cosA);
    return ComputeExponentialWeight(angle, param, 0.0f);
}

//-----------------------------------------------------------------------------
//      ガウスの重みを求めます.
//-----------------------------------------------------------------------------
float CalcGaussWeight(float x)
{ return exp(-0.66f * x * x); }

//-----------------------------------------------------------------------------
//      結合した重みを求めます.
//-----------------------------------------------------------------------------
float CalcCombinedWeight
(
    float2 geometryParams,  float3 Nv, float3 Xvs,
    float  normalParams,    float3 N,  float3 Ns,
    float2 roughnessParams, float  rS
)
{
    // Nv  : View-Space Normal at center.
    // Xvs : View-Space Position at sampling location.
    // N   : World-Space Normal at center.
    // Ns  : World-Space Normal at sampling location.
    // rS  : roughness value at sampling location.
    float3 a = float3(geometryParams.x, normalParams, roughnessParams.x);
    float3 b = float3(geometryParams.y, 0.0f,         roughnessParams.y);

    float3 t;
    t.x = saturate(dot(Nv, Xvs));
    t.y = acos(saturate(dot(N, Ns.xyz)));
    t.z = rS;
 
    float3 w = ComputeExponentialWeight(t, a, b);
    return w.x * w.y * w.z;
}

//-----------------------------------------------------------------------------
//      ピクセル半径からワールド空間に変換します.
//-----------------------------------------------------------------------------
float PixelRadiusToWorld(float unproject, float orthoMode, float pixelRadius, float viewZ)
{ return pixelRadius * unproject * lerp(viewZ, 1.0f, abs(orthoMode)); }

//-----------------------------------------------------------------------------
//      カーネル基底を計算します.
//-----------------------------------------------------------------------------
float2x3 CalcKernelBasis(float3 N, float3 D, float NoD, float roughness, float anisoFade)
{
    float3 T, B;
    CalcONB(N, T, B);

    if (NoD < 0.999)
    {
        float3 R = reflect(-D, N);
        T = normalize(cross(N, R));
        B = cross(R, T);

        float skewFactor = lerp(0.5f + 0.5f * roughness, 1.0f, NoD);
        T *= lerp(skewFactor, 1.0f, anisoFade);
    }

    return float2x3(T, B);
}

//-----------------------------------------------------------------------------
//      回転を適用します.
//-----------------------------------------------------------------------------
float2 RotateVector(float4 rotator, float2 offset)
{ return offset.x * rotator.xz + offset.y * rotator.yw; }

//-----------------------------------------------------------------------------
//      カーネルサンプル座標を計算します.
//-----------------------------------------------------------------------------
float2 CalcKernelSampleCoord
(
    float4x4    proj,
    float2      offset,
    float3      X,
    float3      T,
    float3      B,
    float4      rotator
)
{
    // 回転を適用.
    offset.xy = RotateVector(rotator, offset);

    float3 p = X + T * offset.x + B * offset.y;
    float4 clip = mul(proj, float4(p, 1.0f));
    clip.xy /= clip.w;

    // テクスチャ座標に変換.
    return clip.xy * float2(0.5f, -0.5f) + 0.5f;
}

//-----------------------------------------------------------------------------
//      スペキュラーの支配的因子を求めます.
//-----------------------------------------------------------------------------
float GetSpecularDominantFactor(float NoV, float linearRoughness)
{
    // [Lagarde 2014] Sebastien Lagarde, Charles de Rousiers, 
    // "Moving Frostbite to Physically Based Rendering 3.2"
    // Listing 21.
    float a = 0.298475 * log(39.4115 - 39.0029 * linearRoughness);
    float dominantFactor = pow(saturate(1.0 - NoV), 10.8649) * (1.0 - a) + a;
    return saturate(dominantFactor);
}


#endif//DENOISER_HLSLI
