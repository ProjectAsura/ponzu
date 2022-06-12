//-----------------------------------------------------------------------------
// File : DebugPS.hlsl
// Desc : Pixel Shader For Debug Draw.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

#define MATERIAL_STRIDE     (12)

///////////////////////////////////////////////////////////////////////////////
// VSOutput structure
///////////////////////////////////////////////////////////////////////////////
struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 Normal   : NORMAL;
    float3 Tangent  : TANGENT;
    float2 TexCoord : TEXCOORD0;
};

///////////////////////////////////////////////////////////////////////////////
// PSOutput structure
///////////////////////////////////////////////////////////////////////////////
struct PSOutput
{
    float4 Albedo : SV_TARGET0;
    float4 Normal : SV_TARGET1;
};

///////////////////////////////////////////////////////////////////////////////
// ObjectParameter structure
///////////////////////////////////////////////////////////////////////////////
struct ObjectParameter
{
    uint   InstanceId;
};

//-----------------------------------------------------------------------------
// Resources
//-----------------------------------------------------------------------------
ConstantBuffer<ObjectParameter> ObjectParam : register(b1);
ByteAddressBuffer               Materials   : register(t1);
SamplerState                    LinearWrap  : register(s0);


//-----------------------------------------------------------------------------
//      マテリアルIDとジオメトリIDのパッキングを解除します.
//-----------------------------------------------------------------------------
void UnpackInstanceId(uint instanceId, out uint materialId, out uint geometryId)
{
    materialId = instanceId & 0x3FF;
    geometryId = (instanceId >> 10) & 0x3FFF;
}

//-----------------------------------------------------------------------------
//      メインエントリーポイントです.
//-----------------------------------------------------------------------------
PSOutput main(const VSOutput input)
{
    PSOutput output = (PSOutput)0;

#if 0
    uint materialId = 0;
    uint geometryId = 0;
    UnpackInstanceId(ObjectParam.InstanceId, materialId, geometryId);

    uint  address = materialId * MATERIAL_STRIDE;
    uint4 data    = Materials.Load4(address);

    output.Albedo = float4(1.0f, 1.0f, 1.0f, 1.0f);
    output.Normal = float4(input.Normal * 0.5f + 0.5f, 1.0f);

    if (data.x != INVALID_ID)
    {
        Texture2D<float4> baseColorMap = ResourceDescriptorHeap[data.x];
        output.Albedo = baseColorMap.SampleLevel(LinearWrap, uv, mip);
    }

    if (data.y != INVALID_ID)
    {
        Texture2D<float2> normalMap = ResourceDescriptorHeap[data.y];
        float2 n_xy = normalMap.SampleLevel(LinearWrap, uv, mip);
        float  n_z  = sqrt(abs(1.0f - dot(n_xy, n_xy)));
        output.Normal = float4(n_xy * 0.5f + 0.5f, n_z * 0.5f + 0.5f, 1.0f);
    }

    //if (data.z != INVALID_ID)
    //{
    //    Texture2D<float4> ormMap = ResourceDescriptorHeap[data.z];
    //    orm = ormMap.SampleLevel(LinearWrap, uv, mip).rgb;
    //}

    //if (data.w != INVALID_ID)
    //{
    //    Texture2D<float4> emissiveMap = ResourceDescriptorHeap[data.w];
    //    e = emissiveMap.SampleLevel(LinearWrap, uv, mip).rgb;
    //}
#else
    output.Albedo = float4(1.0f, 1.0f, 1.0f, 1.0f);
    output.Normal = float4(input.Normal * 0.5f + 0.5f, 1.0f);
#endif

    return output;
}