//-----------------------------------------------------------------------------
// File : ModelPS.hlsl
// Desc : Pixel Shader For Debug Draw.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <Math.hlsli>


#define ENABLE_TEXTURED_MATERIAL (0)

///////////////////////////////////////////////////////////////////////////////
// PSInput structure
///////////////////////////////////////////////////////////////////////////////
struct PSInput
{
    float4 Position     : SV_POSITION;
    float3 Normal       : NORMAL;
    float3 Tangent      : TANGENT;
    float2 TexCoord     : TEXCOORD0;
    float4 CurrProjPos  : CURR_PROJ_POS;
    float4 PrevProjPos  : PREV_PROJ_POS;

    float3                 Barycentrics : SV_Barycentrics;
    nointerpolation uint   PrimitiveId  : SV_PrimitiveID;
};

///////////////////////////////////////////////////////////////////////////////
// PSOutput structure
///////////////////////////////////////////////////////////////////////////////
struct PSOutput
{
    float4 Albedo     : SV_TARGET0;
    float2 Normal     : SV_TARGET1;
    float  Roughness  : SV_TARGET2;
    float2 Velocity   : SV_TARGET3;
    uint4  Visibility : SV_TARGET4;
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

#if 0
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
#else
///////////////////////////////////////////////////////////////////////////////
// ResMaterial structure
///////////////////////////////////////////////////////////////////////////////
struct ResMaterial
{
    uint4   TextureMaps;    // x:BaseColorMap, y:NormalMap, z:OrmMap, w:EmissiveMap.
    float4  BaseColor;
    float   Occlusion;
    float   Roughness;
    float   Metalness;
    float   Ior;
    float4  Emissive;
};


#endif

#define INSTANCE_STRIDE (sizeof(Instance))

//-----------------------------------------------------------------------------
// Resources
//-----------------------------------------------------------------------------
ConstantBuffer<ObjectParameter> ObjectParam : register(b1);
StructuredBuffer<ResMaterial>   Materials   : register(t1);
ByteAddressBuffer               Instances   : register(t2);
SamplerState                    LinearWrap  : register(s0);

//-----------------------------------------------------------------------------
//      メインエントリーポイントです.
//-----------------------------------------------------------------------------
PSOutput main(const PSInput input)
{
    PSOutput output = (PSOutput)0;

    float2 currPosCS = input.CurrProjPos.xy / input.CurrProjPos.w;
    float2 prevPosCS = input.PrevProjPos.xy / input.PrevProjPos.w;
    float2 velocity  = (currPosCS - prevPosCS);

    uint materialId = Instances.Load(ObjectParam.InstanceId * INSTANCE_STRIDE + 8);
    float2 uv = input.TexCoord;

    ResMaterial material = Materials[materialId];

#if ENABLE_TEXTURED_MATERIAL
    Texture2D<float4> baseColorMap = ResourceDescriptorHeap[material.Textures0.x];
    float4 bc = baseColorMap.Sample(LinearWrap, uv);

    Texture2D<float4> normalMap = ResourceDescriptorHeap[material.Textures0.y];
    float3 n = normalMap.Sample(LinearWrap, uv).xyz;
    n = n * 2.0f - 1.0f;

    Texture2D<float4> ormMap = ResourceDescriptorHeap[material.Textures0.z];
    float4 orm = ormMap.Sample(LinearWrap, uv);

    float3 bitangent = normalize(cross(input.Tangent, input.Normal));
    float3 normal = FromTangentSpaceToWorld(n, input.Tangent, bitangent, input.Normal);

    output.Albedo    = bc;
    output.Normal    = PackNormal(normal);
    output.Roughness = orm.y;
    output.Velocity  = velocity;
#else
    output.Albedo    = material.BaseColor;
    output.Normal    = PackNormal(input.Normal);
    output.Roughness = material.Roughness;
    output.Velocity  = velocity;
#endif
    
    output.Visibility.x = ObjectParam.InstanceId;
    output.Visibility.y = input.PrimitiveId;
    output.Visibility.z = asuint(input.Barycentrics.x);
    output.Visibility.w = asuint(input.Barycentrics.y);

    return output;
}