//-----------------------------------------------------------------------------
// File : DebugPS.hlsl
// Desc : Pixel Shader For Debug.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------


///////////////////////////////////////////////////////////////////////////////
// VSOutput structure
///////////////////////////////////////////////////////////////////////////////
struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

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
    return ColorBuffer.SampleLevel(ColorSampler, input.TexCoord, 0.0f);
}
