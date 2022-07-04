//-----------------------------------------------------------------------------
// File : Material.hlsli
// Desc : Material.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------
#ifndef MATERIAL_HLSLI
#define MATERIAL_HLSLI

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <Math.hlsli>
#include <BRDF.hlsli>


///////////////////////////////////////////////////////////////////////////////
// Material structure
///////////////////////////////////////////////////////////////////////////////
struct Material
{
    float4  BaseColor;
    float3  Normal;
    float   Roughness;
    float   Metalness;
    float3  Emissive;
};

//-----------------------------------------------------------------------------
//      輝度値を求めます.
//-----------------------------------------------------------------------------
float Luminance(float3 rgb)
{ return dot(rgb, float3(0.2126f, 0.7152f, 0.0722f)); }

float3 EvaluateMaterial(float3 input, float2 u, float3 N, float p, Material material, out float3 dir, out bool dice)
{
    // Lambert.
    float3 T, B;
    CalcONB(N, T, B);

    float3 s = SampleLambert(u);
    dir = normalize(T * s.x + B * s.y + material.Normal * s.z);

    dice = (p >= min(Luminance(material.BaseColor.rgb), 0.95f));

    return material.BaseColor.rgb;
}

#endif//MATERIAL_HLSLI
