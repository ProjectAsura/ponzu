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
RWTexture2D<float4> OutColorMap     : register(u0);


cbuffer CbParam : register(b0)
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
//      速度ベクトルを取得します.
//-----------------------------------------------------------------------------
float2 GetVelocity(float2 uv)
{
    float2 result = VelocityMap.SampleLevel(PointClamp, uv, 0.0f);
    float  currLengthSq = dot(result, result);

    // 最も長い速度ベクトルを取得.
    [unroll] for(uint i=0; i<8; ++i)
    {
        float2 velocity = VelocityMap.SampleLevel(PointClamp, uv, 0.0f, kOffsets[i]);
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
float3 GetCurrentNeighborColor(float2 uv, float3 currentColor)
{
    const float centerWeight = 4.0f;
    float3 accColor = currentColor * centerWeight;
    [unroll] 
    for(uint i=0; i<4; ++i)
    { accColor += CurrColorMap.SampleLevel(PointClamp, uv, 0.0f, kOffsets[i]).rgb; }
    const float kInvWeight = 1.0f / (4.0f + centerWeight);
    accColor *= kInvWeight;
    return accColor;
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
    // テクスチャキャッシュが効くように再マッピング.
    uint2 remappedId = RemapLane8x8(dispatchId.xy, groupIndex);
    if (any(remappedId >= ScreenSize)) 
    { return; }

    // 現在フレームのカラーを取得.
    float2 currUV = (float2(remappedId) + 0.5.xx) * InvScreenSize;
    float4 currColor = CurrColorMap.SampleLevel(PointClamp, currUV, 0.0f);

    const float kSizeScale = ScreenSize.x / 1920.0f;

    // 速度ベクトルを取得.
    float2 velocity = GetVelocity(currUV);
    float  velocityDelta = saturate(1.0f - length(velocity)) / (kFrameVelocityInPixelsDiff * kSizeScale);
    
    // 前フレームのテクスチャ座標を計算.
    float2 prevUV = currUV + (velocity * InvScreenSize);

    // スクリーン内かどうかチェック.
    float inScreen = all(saturate(prevUV) == prevUV) ? 1.0f : 0.0f;

    // ヒストリーが有効かどうかチェック.
    bool isValidHistory = (velocityDelta * inScreen) > 0.0f;
    if (!isValidHistory)
    {
        float3 neighborColor = GetCurrentNeighborColor(currUV, currColor.rgb);
        float4 finalColor    = SaturateFloat(float4(neighborColor, 1.0f));
        OutColorMap[remappedId] = finalColor;
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
 
    float historyLength = prevColor.a;

    // ヒストリー成功回数をカウントアップ.
    historyLength += 1.0f;
    historyLength = min(historyLength, kMaxHistoryLength);
 
    // 重みを求める.
    float  blend      = saturate(1.0f / historyLength);
    float  currWeight = CalcHdrWeightY(currColor.rgb);
    float  prevWeight = CalcHdrWeightY(prevColor.rgb);
    float2 weights    = CalcBlendWeight(prevWeight, currWeight, blend);
    float4 finalColor = prevColor * weights.x + currColor * weights.y;

    // RGBに戻す.
    finalColor   = YCoCgToRGB(finalColor);
    finalColor.a = historyLength;

    // NaNを潰しておく.
    finalColor = SaturateFloat(finalColor);
    
    // 出力.
    OutColorMap[remappedId] = finalColor;
}
