//-----------------------------------------------------------------------------
// File : TonemapCS.hlsl
// Desc : Compute Shader For Tonemap.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <SceneParam.hlsli>
#include <Math.hlsli>

//-----------------------------------------------------------------------------
// Resources
//-----------------------------------------------------------------------------
ConstantBuffer<SceneParameter>  SceneParam   : register(b0);
Texture2D                       ColorBuffer  : register(t0);
RWTexture2D<float4>             OutputBuffer : register(u0);

//-----------------------------------------------------------------------------
//      ACESフィルミックトーンマップ近似.
//-----------------------------------------------------------------------------
float3 ACESFilm(float3 color)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    const float f = 0.665406f;
    const float g = 12.0f;

    return saturate((color * (a * color * f / g + b)) / (color * f / g * (c * color * f + d) + e));
}

//-----------------------------------------------------------------------------
//      エントリーポイントです.
//-----------------------------------------------------------------------------
[numthreads(8, 8, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (any(dispatchId.xy >= (uint2)SceneParam.Size.xy)) {
        return;
    }

    float4 color = ColorBuffer.Load(int3(dispatchId.xy, 0));
    float3 output = (color.rgb / SceneParam.AccumulatedFrames) * SceneParam.ExposureAdjustment;
    output = ACESFilm(output);
    output = Linear_To_SRGB(output);

    OutputBuffer[dispatchId.xy] = float4(output, 1.0f);
}
