//-----------------------------------------------------------------------------
// File : TemporalStabilizationCS.hlsl
// Desc : Temporal Stabilization.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <Denoiser.hlsli>


//-----------------------------------------------------------------------------
// Constant Values.
//-----------------------------------------------------------------------------
static const float kVarianceIntersectionMaxT  = 100.0f;
static const float kFrameVelocityInPixelsDiff = 256.0f; // 1920 x 1080.
static const float kMaxHistoryLength          = 64.0f;

Texture2D<float4>   CurrColorMap    : register(t0);
Texture2D<float4>   HistoryColorMap : register(t1);
Texture2D<float2>   VelocityMap     : register(t2);
RWTexture2D<float4> OutColorMap     : register(u0);


///////////////////////////////////////////////////////////////////////////////
// CbParam
///////////////////////////////////////////////////////////////////////////////
cbuffer CbParam : register(b1)
{
    uint2  ScreenSize;
    float2 Jitter;
};

cbuffer CbFlags : register(b2)
{
    uint    Flags;
    uint3   Reserved;
};

//-----------------------------------------------------------------------------
//      ヒストリーカラーを取得します.
//-----------------------------------------------------------------------------
float4 GetHistoryColor(float2 uv)
{ return BicubicSampleCatmullRom(HistoryColorMap, LinearClamp, uv, ScreenSize); }

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
    // テクスチャキャッシュが効くように再マッピング.
    uint2 remappedId = RemapLane8x8(dispatchId.xy, groupIndex);
    if (any(remappedId >= ScreenSize)) 
    { return; }

    const float2 kInvScreenSize = 1.0f.xx / float2(ScreenSize);

    // 現在フレームのカラーを取得.
    float2 currUV = (float2(remappedId) + 0.5.xx) * kInvScreenSize;
    float4 currColor = CurrColorMap.SampleLevel(PointClamp, currUV + Jitter * kInvScreenSize, 0.0f);

    const float kSizeScale = ScreenSize.x / 1920.0f;

    // 速度ベクトルを取得.
    float2 velocity = GetVelocity(VelocityMap, LinearClamp, currUV);
    float  velocityDelta = saturate(1.0f - length(velocity)) / (kFrameVelocityInPixelsDiff * kSizeScale);
    
    // 前フレームのテクスチャ座標を計算.
    float2 prevUV = currUV + (velocity * kInvScreenSize);

    // スクリーン内かどうかチェック.
    float inScreen = all(saturate(prevUV) == prevUV) ? 1.0f : 0.0f;
    float resetHistory = (Flags & 0x1) ? 0.0f : 1.0f;

    // ヒストリーが有効かどうかチェック.
    bool isValidHistory = (velocityDelta * inScreen * resetHistory) > 0.0f;
    if (!isValidHistory)
    {
        float4 neighborColor = GetCurrentNeighborColor(CurrColorMap, PointClamp, currUV, currColor);
        OutColorMap[remappedId] = neighborColor;
        return;
    }

    // ヒストリーカラーを取得.
    float4 prevColor = GetHistoryColor(prevUV);

    // YCoCgに変換.
    currColor = RGBToYCoCg(currColor);
    prevColor = RGBToYCoCg(prevColor);

    // AABBを取得.
    float4 minColor, maxColor;
    CalcColorBoundingBox(CurrColorMap, LinearClamp, prevUV, currColor, 0.95f, minColor, maxColor);

    // AABBでクリップ.
    float t = IntersectAABB(currColor.rgb - prevColor.rgb, prevColor.rgb, minColor.rgb, maxColor.rgb);
    prevColor = lerp (prevColor, currColor, saturate(t));
    prevColor = clamp(prevColor, minColor, maxColor);

    // 重みを求める.
    float  blend      = saturate(max(0.1f, saturate(0.01f * prevColor.x / abs(currColor.x - prevColor.x))));
    float  currWeight = CalcHdrWeightY(currColor.rgb);
    float  prevWeight = CalcHdrWeightY(prevColor.rgb);
    float2 weights    = CalcBlendWeight(prevWeight, currWeight, blend);
    float4 finalColor = prevColor * weights.x + currColor * weights.y;

    // RGBに戻す.
    finalColor = YCoCgToRGB(finalColor);

    // NaNを潰しておく.
    finalColor = SaturateFloat(finalColor);

    // 出力.
    OutColorMap[remappedId] = finalColor;
}
