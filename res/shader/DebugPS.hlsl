//-----------------------------------------------------------------------------
// File : DebugPS.hlsl
// Desc : Pixel Shader For Debug.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

#include <Math.hlsli>

///////////////////////////////////////////////////////////////////////////////
// VSOutput structure
///////////////////////////////////////////////////////////////////////////////
struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

static const uint TYPE_DEFAULT   = 0;
static const uint TYPE_NORMAL    = 1;
static const uint TYPE_VELOCITY  = 2;
static const uint TYPE_R         = 3;
static const uint TYPE_G         = 4;
static const uint TYPE_B         = 5;
static const uint TYPE_A         = 6;

cbuffer DebugParam : register(b0) {
    uint    Type;
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
    float4 output = 0.0f.xxxx;

    if (Type == TYPE_DEFAULT)
    {
        output = ColorBuffer.SampleLevel(ColorSampler, input.TexCoord, 0.0f);
    }
    else if (Type == TYPE_NORMAL)
    {
        float2 packedNormal = ColorBuffer.SampleLevel(ColorSampler, input.TexCoord, 0.0f).xy;
        float3 normal = UnpackNormal(packedNormal);
        output.xyz = normal * 0.5f + 0.5f;
        output.a   = 1.0f;
    }
    else if (Type == TYPE_VELOCITY)
    {
        float2 velocity = ColorBuffer.SampleLevel(ColorSampler, input.TexCoord, 0.0f).xy;
        output.xy = velocity * 0.5f + 0.5f;
        output.z  = 0.0f;
        output.a  = 1.0f;
    }
    else if (Type == TYPE_R)
    {
        output.rgb = ColorBuffer.SampleLevel(ColorSampler, input.TexCoord, 0.0f).rrr;
        output.a   = 1.0f;
    }
    else if (Type == TYPE_G)
    {
        output.rgb = ColorBuffer.SampleLevel(ColorSampler, input.TexCoord, 0.0f).ggg;
        output.a   = 1.0f;
    }
    else if (Type == TYPE_B)
    {
        output.rgb = ColorBuffer.SampleLevel(ColorSampler, input.TexCoord, 0.0f).bbb;
        output.a   = 1.0f;
    }
    else if (Type == TYPE_A)
    {
        output.rgb = ColorBuffer.SampleLevel(ColorSampler, input.TexCoord, 0.0f).aaa;
        output.a   = 1.0f;
    }

    return output;
}
