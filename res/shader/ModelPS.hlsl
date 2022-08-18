//-----------------------------------------------------------------------------
// File : DebugPS.hlsl
// Desc : Pixel Shader For Debug Draw.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <Math.hlsli>

#define MATERIAL_STRIDE     (32)
#define INSTANCE_STRIDE     (12)

///////////////////////////////////////////////////////////////////////////////
// VSOutput structure
///////////////////////////////////////////////////////////////////////////////
struct VSOutput
{
    float4 Position     : SV_POSITION;
    float3 Normal       : NORMAL;
    float3 Tangent      : TANGENT;
    float2 TexCoord     : TEXCOORD0;
    float4 CurrProjPos  : CURR_PROJ_POS;
    float4 PrevProjPos  : PREV_PROJ_POS;
};

///////////////////////////////////////////////////////////////////////////////
// PSOutput structure
///////////////////////////////////////////////////////////////////////////////
struct PSOutput
{
    float4 Albedo   : SV_TARGET0;
    float4 Normal   : SV_TARGET1;
    float2 Velocity : SV_TARGET2;
};

///////////////////////////////////////////////////////////////////////////////
// ObjectParameter structure
///////////////////////////////////////////////////////////////////////////////
struct ObjectParameter
{
    uint   InstanceId;
};

///////////////////////////////////////////////////////////////////////////////
// Instance structure
///////////////////////////////////////////////////////////////////////////////
struct Instance
{
    uint    VertexId;
    uint    IndexId;
    uint    MaterialId;
};

//-----------------------------------------------------------------------------
// Resources
//-----------------------------------------------------------------------------
ConstantBuffer<ObjectParameter> ObjectParam : register(b1);
ByteAddressBuffer               Materials   : register(t1);
ByteAddressBuffer               Instances   : register(t2);
SamplerState                    LinearWrap  : register(s0);

//-----------------------------------------------------------------------------
//      メインエントリーポイントです.
//-----------------------------------------------------------------------------
PSOutput main(const VSOutput input)
{
    PSOutput output = (PSOutput)0;

    float2 currPosCS = input.CurrProjPos.xy / input.CurrProjPos.w;
    float2 prevPosCS = input.PrevProjPos.xy / input.PrevProjPos.w;
    float2 velocity  = (currPosCS - prevPosCS);

    uint materialId = Instances.Load(ObjectParam.InstanceId * INSTANCE_STRIDE + 8);

    uint   address = materialId * MATERIAL_STRIDE;
    uint4  data    = Materials.Load4(address);
    float4 prop    = asfloat(Materials.Load4(address + 16));

    float2 uv = input.TexCoord * prop.zw;

    Texture2D<float4> baseColorMap = ResourceDescriptorHeap[data.x];
    float4 bc = baseColorMap.Sample(LinearWrap, uv);

    //Texture2D<float4> normalMap = ResourceDescriptorHeap[data.y];
    //float3 n = normalMap.Sample(LinearWrap, input.TexCoord).xyz * 2.0f - 1.0f;
    //float3 bitangent = normalize(cross(input.Tangent, input.Normal));
    //float3 normal = FromTangentSpaceToWorld(n, input.Tangent, bitangent, input.Normal);
    float3 normal = input.Normal;

    output.Albedo   = bc;
    output.Normal   = float4(normal * 0.5f + 0.5f, 1.0f);
    output.Velocity = velocity;

    return output;
}