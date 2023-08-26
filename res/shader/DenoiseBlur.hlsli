//-----------------------------------------------------------------------------
// File : DenoiseBlur.hlsli
// Desc : Denoise Blur
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <Denoiser.hlsli>


///////////////////////////////////////////////////////////////////////////////
// Parameters
///////////////////////////////////////////////////////////////////////////////
cbuffer Parameters : register(b0)
{
    uint2       ScreenSize;
    float       MinRectDimMulUnproject;
    float       PlaneDistSensitivity;

    float       OrthoMode;
    float       LobeAngleFraction;
    float       RoughnessFraction;
    float       Unproject;

    float       BlurRadius;
    float2      InvScreenSize;
    uint        IgnoreHistory;

    float4x4    Proj;
    float4x4    View;
    float       NearClip;
    float       FarClip;
    float2      UVToViewParam;

    float4      HitDistanceParams;
};

///////////////////////////////////////////////////////////////////////////////
// Constants
///////////////////////////////////////////////////////////////////////////////
cbuffer Constants : register(b1)
{
    float4  Rotator;
};

//-----------------------------------------------------------------------------
//  Resources
//-----------------------------------------------------------------------------
Texture2D<float>    DepthBuffer         : register(t0);
Texture2D<float2>   NormalBuffer        : register(t1);
Texture2D<float>    RoughnessBuffer     : register(t2);
Texture2D<float>    HitDistanceBuffer   : register(t3);
Texture2D<float4>   InputBuffer         : register(t4);
RWTexture2D<float4> DenoisedBuffer      : register(u0);
RWTexture2D<uint>   AccumCountBuffer    : register(u1);

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
    {
        return;
    }

    float2 uv = (float2(remappedId) + 0.5f.xx) / float2(ScreenSize);
    float  d  = DepthBuffer.SampleLevel(PointClamp, uv, 0.0f);

    // 背景なら処理しない.
    if (d >= 1.0f)
    {
        // ブラー結果を出力.
        DenoisedBuffer[remappedId] = InputBuffer.SampleLevel(PointClamp, uv, 0.0f);
        AccumCountBuffer[remappedId] = 0;
        return;
    }
 
    // ビュー空間深度を求める.
    float z = ToViewDepth(d, NearClip, FarClip);

    const float MinHitDistWeight = 0.1f;
    const uint  MaxAccumCount    = 128;

    // アキュムレーションフレーム数.
    uint accumCount = AccumCountBuffer[remappedId];
    accumCount = min(accumCount, MaxAccumCount);

    // ヒストリーが無効な場合.
    if (IgnoreHistory & 0x1)
    { accumCount = 1; }

    // アキュムレーションスピード.
    float accumSpeed = 1.0f / (1.0f + float(accumCount));

    // 法線とラフネスを取得.
    float3 N         = UnpackNormal(NormalBuffer.SampleLevel(PointClamp, uv, 0.0f));
    float  roughness = RoughnessBuffer.SampleLevel(PointClamp, uv, 0.0f);

    // ビュー空間の位置，法線，視線ベクトルを求める.
    float3 Xv = ToViewPos(uv, z, UVToViewParam);
    float3 Nv = mul((float3x3)View, N);
    float3 Vv = normalize(Xv);

    // プライマリーヒットからセカンダリ―ヒットまでの距離.
    float hitDistance = HitDistanceBuffer.SampleLevel(PointClamp, uv, 0.0f);

    float3 Dv  = GetSpecularDominantDir(Nv, Vv, roughness);
    float  NoD = abs(dot(Nv, Dv));
    float  specFactor = GetSpecularDominantFactor(abs(dot(Nv, Vv)), roughness);

    // 最小ブラー半径を求める.
    float lobeTanHalfAngle = GetSpecularLobeTanHalfAngle(roughness, 0.75f);
    float lobeRadius       = hitDistance * NoD * lobeTanHalfAngle;
    float minBlurRadius    = lobeRadius / PixelRadiusToWorld(Unproject, OrthoMode, 1.0f, z + hitDistance * specFactor);

    // 錐台サイズとヒット距離因子を計算.
    float frustumSize   = CalcFrustumSize(MinRectDimMulUnproject, OrthoMode, z);
    float hitDistFactor = CalcHitDistFactor(hitDistance * NoD, frustumSize);

    // ウェイトパラメータを計算.
    float2 paramGeometry  = CalcGeometryWeightParams(PlaneDistSensitivity, frustumSize, Xv, Nv, accumSpeed);
    float2 paramRoughness = CalcRoughnessWeightParams(roughness, RoughnessFraction);
    float2 paramHitDist   = CalcHitDistanceWeightParams(hitDistance, accumSpeed, roughness);
    float  paramNormal    = CalcNormalWeightParams(accumSpeed, LobeAngleFraction, roughness);

    // カーネル基底を計算.
    float2x3 kernelBasis = CalcKernelBasis(Nv, Dv, NoD, roughness, accumSpeed);

    float blurRadius  = BlurRadius * hitDistFactor * CalcSpecularMagicCurve(roughness);
    blurRadius = min(blurRadius, minBlurRadius);

    // ブラー半径を計算.
    float worldRadius = PixelRadiusToWorld(Unproject, OrthoMode, blurRadius, z);
    kernelBasis[0] *= worldRadius;
    kernelBasis[1] *= worldRadius;

    // 中心サンプル.
    float  weight = 1.0f;
    float4 result = InputBuffer.SampleLevel(PointClamp, uv, 0.0f);

#ifdef PRE_BLUR
    float hitDistLerpFactor = LinearStep(0.5f, 1.0f, roughness);
#endif

    [unroll]
    for(uint n=0; n<8; ++n)
    {
        // ポアソンディスクサンプリング.
        float3 offset = kPoisson8[n];

        // テクスチャ座標を計算.
    #ifdef PRE_BLUR
        float2 st = uv + RotateVector(Rotator, offset.xy) * (InvScreenSize * blurRadius);
    #else
        float2 st = CalcKernelSampleCoord(Proj, offset.xy, Xv, kernelBasis[0], kernelBasis[1], Rotator);
    #endif

        // テクスチャをサンプリング.
        float  zs         = ToViewDepth(DepthBuffer.SampleLevel(PointClamp, st, 0.0f), NearClip, FarClip);
        float3 Ns         = UnpackNormal(NormalBuffer.SampleLevel(PointClamp, st, 0.0f));
        float  hitDistS   = HitDistanceBuffer.SampleLevel(PointClamp, st, 0.0f);
        float  roughnessS = RoughnessBuffer.SampleLevel(PointClamp, st, 0.0f);
        float4 color      = InputBuffer.SampleLevel(PointClamp, st, 0.0f);

        // ビュー空間位置を計算.
        float3 Xvs = ToViewPos(st, zs, UVToViewParam);

        // ブラーウェイトを求めます.
        float w = KarisAntiFireflyWeight(color.rgb, 1.0f);
        w *= CalcGaussWeight(offset.z);
        w *= CalcCombinedWeight(paramGeometry, Nv, Xvs, paramNormal, N, Ns, paramRoughness, roughnessS);
        w *= lerp(MinHitDistWeight, 1.0f, CalcHitDistanceWeight(paramHitDist, hitDistS));
        
    #ifdef PRE_BLUR
        float dist = length(Xvs - Xv);
        float t    = hitDistS / (hitDistance + dist);
        w *= lerp(saturate(t), 1.0f, hitDistLerpFactor);
    #endif

        // ウェイトが有効かどうかチェック.
        w = (IsInScreen(uv) && !isnan(w)) ? w : 0.0f;
        color = (w != 0.0f) ? color : 0.0f.xxxx;

        // ブラーウェイトを適用.
        weight += w;
        result += color * w;
    }

    // 正規化.
    result *= rcp(weight);

    // ブラー結果を出力.
    DenoisedBuffer[remappedId] = result;

    // アキュムレーションフレーム数を更新.
    accumCount++;
    AccumCountBuffer[remappedId] = accumCount;
}

