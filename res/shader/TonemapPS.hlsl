//-----------------------------------------------------------------------------
// File : TonemapPS.hlsl
// Desc : Pixel Shader For Tonemap.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <SceneParam.hlsli>


///////////////////////////////////////////////////////////////////////////////
// VSOutput structure
///////////////////////////////////////////////////////////////////////////////
struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};


ConstantBuffer<SceneParameter>  SceneParam : register(b0);

//-----------------------------------------------------------------------------
// Textures and Samplers.
//-----------------------------------------------------------------------------
Texture2D       ColorBuffer  : register(t0);
SamplerState    ColorSampler : register(s0);

//-----------------------------------------------------------------------------
//      エントリーポイントです.
//-----------------------------------------------------------------------------
float4 main(const VSOutput input) : SV_TARGET0
{
    float4 color = ColorBuffer.SampleLevel(ColorSampler, input.TexCoord, 0.0f);
    float3 output = (color.rgb / SceneParam.AccumulatedFrames) * SceneParam.ExposureAdjustment;

    return float4(output, 1.0f);
}