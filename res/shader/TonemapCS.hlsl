//-----------------------------------------------------------------------------
// File : TonemapCS.hlsl
// Desc : Compute Shader For Tonemap.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <SceneParam.hlsli>

//-----------------------------------------------------------------------------
// Resources
//-----------------------------------------------------------------------------
ConstantBuffer<SceneParameter>  SceneParam   : register(b0);
Texture2D                       ColorBuffer  : register(t0);
RWTexture2D<float4>             OutputBuffer : register(u0);

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

    OutputBuffer[dispatchId.xy] = float4(output, 1.0f);
}
