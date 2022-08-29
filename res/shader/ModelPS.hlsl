//-----------------------------------------------------------------------------
// File : ModelPS.hlsl
// Desc : Pixel Shader For Debug Draw.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <Math.hlsli>


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

///////////////////////////////////////////////////////////////////////////////
// MeshMaterial structure
///////////////////////////////////////////////////////////////////////////////
struct MeshMaterial
{
    uint4   Textures0;      // x:BaseColorMap, y:NormalMap, z:OrmiMap, w:EmissiveMap.
    uint4   Textures1;      // x:BaseColorMap, y:NormalMap, z:OrmiMap, w:EmissiveMap
    float4  UvControl0;     // xy:Scale, zw:Scroll;
    float4  UvControl1;     // xy:Scale, zw:Scroll;
    float   IntIor;         // Interior Index Of Refraction.
    float   ExtIor;         // Exterior Index Of Refraction.
    uint    LayerCount;     // Texture Layer Count.
    uint    LayerMask;      // Texture Layer Mask Map.
};

#define INSTANCE_STRIDE (sizeof(Instance))

//-----------------------------------------------------------------------------
// Resources
//-----------------------------------------------------------------------------
ConstantBuffer<ObjectParameter> ObjectParam : register(b1);
StructuredBuffer<MeshMaterial>  Materials   : register(t1);
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

    MeshMaterial material = Materials[materialId];

    float2 uv0 = input.TexCoord * material.UvControl0.xy + material.UvControl0.zw;

    Texture2D<float4> baseColorMap = ResourceDescriptorHeap[material.Textures0.x];
    float4 bc = baseColorMap.Sample(LinearWrap, uv0);

    Texture2D<float4> normalMap = ResourceDescriptorHeap[material.Textures0.y];
    float3 n = normalMap.Sample(LinearWrap, uv0).xyz;

    if (material.LayerCount > 1)
    {
        float2 uv1 = input.TexCoord * material.UvControl1.xy + material.UvControl1.zw;

        Texture2D<float4> normalMap1 = ResourceDescriptorHeap[material.Textures1.y];
        float3 n1 = normalMap1.Sample(LinearWrap, uv1).xyz;
        n = BlendNormal(n, n1);
    }
    else
    {
        n = n * 2.0f - 1.0f;
    }


    float3 bitangent = normalize(cross(input.Tangent, input.Normal));
    float3 normal = FromTangentSpaceToWorld(n, input.Tangent, bitangent, input.Normal);

    output.Albedo   = bc;
    output.Normal   = float4(normal * 0.5f + 0.5f, 1.0f);
    output.Velocity = velocity;

    return output;
}