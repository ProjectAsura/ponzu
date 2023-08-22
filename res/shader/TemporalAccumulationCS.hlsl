//-----------------------------------------------------------------------------
// File : TemporalAccumulationCS.hlsl
// Desc : Temporal Accumulation.
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
Texture2D<uint>     AccumCountMap   : register(t3);
RWTexture2D<float4> OutColorMap     : register(u0);


///////////////////////////////////////////////////////////////////////////////
// CbParam
///////////////////////////////////////////////////////////////////////////////
cbuffer CbParam : register(b1)
{
    uint2  ScreenSize;
    float2 InvScreenSize;
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

    // 現在フレームのカラーを取得.
    float2 currUV = (float2(remappedId) + 0.5.xx) * InvScreenSize;
    float4 currColor = CurrColorMap.SampleLevel(PointClamp, currUV, 0.0f);

    const float kSizeScale = ScreenSize.x / 1920.0f;

    // 速度ベクトルを取得.
    float2 velocity = GetVelocity(VelocityMap, LinearClamp, currUV);
    float  velocityDelta = saturate(1.0f - length(velocity)) / (kFrameVelocityInPixelsDiff * kSizeScale);
    
    // 前フレームのテクスチャ座標を計算.
    float2 prevUV = currUV + (velocity * InvScreenSize);

    // スクリーン内かどうかチェック.
    float inScreen = all(saturate(prevUV) == prevUV) ? 1.0f : 0.0f;
    
    uint historyLength = AccumCountMap.Load(int3(remappedId, 0));
    float isAccumValid = (historyLength > 1) ? 1.0f : 0.0f;

    // ヒストリーが有効かどうかチェック.
    bool isValidHistory = (velocityDelta * inScreen * isAccumValid) > 0.0f;
    if (!isValidHistory)
    {
        float4 neighborColor = GetCurrentNeighborColor(CurrColorMap, PointClamp, currUV, currColor);
        OutColorMap[remappedId] = SaturateFloat(neighborColor);
        return;
    }

    // ヒストリーカラーを取得.
    float4 prevColor = GetHistoryColor(prevUV);

    // YCoCgに変換.
    currColor = RGBToYCoCg(currColor);
    prevColor = RGBToYCoCg(prevColor);

    // AABBを取得.
    float4 minColor, maxColor;
    CalcColorBoundingBox(CurrColorMap, PointClamp, prevUV, currColor, 0.95f, minColor, maxColor);

    // AABBでクリップ.
    float t = IntersectAABB(currColor.rgb - prevColor.rgb, prevColor.rgb, minColor.rgb, maxColor.rgb);
    prevColor = lerp (prevColor, currColor, saturate(t));
    prevColor = clamp(prevColor, minColor, maxColor);
 
    // 重みを求める.
    float  blend      = saturate(1.0f / (float)historyLength);
    float  currWeight = CalcHdrWeightY(currColor.rgb);
    float  prevWeight = CalcHdrWeightY(prevColor.rgb);
    float2 weights    = CalcBlendWeight(prevWeight, currWeight, blend);
    float4 finalColor = prevColor * weights.x + currColor * weights.y;

    // RGBに戻す.
    finalColor   = YCoCgToRGB(finalColor);

    // NaNを潰しておく.
    finalColor = SaturateFloat(finalColor);
    
    // 出力.
    OutColorMap[remappedId] = finalColor;
}
