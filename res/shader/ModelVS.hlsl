//-----------------------------------------------------------------------------
// File : SimpleVS.hlsl
// Desc : Vertex Shader For Denoiser.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

#define TRANSFORM_STRIDE    (48)


///////////////////////////////////////////////////////////////////////////////
// VSInput structure
///////////////////////////////////////////////////////////////////////////////
struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float3 Tangent  : TANGENT;
    float2 TexCoord : TEXCOORD0;
};

///////////////////////////////////////////////////////////////////////////////
// VSOutput structure
///////////////////////////////////////////////////////////////////////////////
struct VSOutput
{
    float4 Position     : SV_POSITION;
    float3 Normal       : NORMAL;
    float3 Tangent      : TANGENT;
    float2 TexCoord     : TEXCOORD0;
};

///////////////////////////////////////////////////////////////////////////////
// SceneParameter structure
///////////////////////////////////////////////////////////////////////////////
struct SceneParameter
{
    float4x4 View;
    float4x4 Proj;

    uint    MaxBounce;
    uint    FrameIndex;
    float   SkyIntensity;
    float   Exposure;
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
ConstantBuffer<SceneParameter>  SceneParam  : register(b0);
ConstantBuffer<ObjectParameter> ObjectParam : register(b1);
ByteAddressBuffer               Transforms  : register(t0);

//-----------------------------------------------------------------------------
//      マテリアルIDとジオメトリIDのパッキングを解除します.
//-----------------------------------------------------------------------------
void UnpackInstanceId(uint instanceId, out uint materialId, out uint geometryId)
{
    materialId = instanceId & 0x3FF;
    geometryId = (instanceId >> 10) & 0x3FFF;
}

//-----------------------------------------------------------------------------
//      ワールド行列を取得します.
//-----------------------------------------------------------------------------
float3x4 GetWorldMatrix(uint geometryId)
{
    uint index = geometryId * TRANSFORM_STRIDE;
    float4 row0 = asfloat(Transforms.Load4(index));
    float4 row1 = asfloat(Transforms.Load4(index + 16));
    float4 row2 = asfloat(Transforms.Load4(index + 32));

    return float3x4(row0, row1, row2);
}

//-----------------------------------------------------------------------------
//      メインエントリーポイントです.
//-----------------------------------------------------------------------------
VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput)0;

    uint materialId = 0;
    uint geometryId = 0;
    UnpackInstanceId(ObjectParam.InstanceId, materialId, geometryId);

    float3x4 world = GetWorldMatrix(geometryId);

    float4 localPos = float4(input.Position, 1.0f);
    float4 worldPos = float4(mul(world, localPos), 1.0f);
    float4 viewPos  = mul(SceneParam.View, worldPos);
    float4 projPos  = mul(SceneParam.Proj, viewPos);

    float3 worldNormal  = normalize(mul((float3x3)world, input.Normal));
    float3 worldTangent = normalize(mul((float3x3)world, input.Tangent));

    output.Position = projPos;
    output.Normal   = worldNormal;
    output.Tangent  = worldTangent;
    output.TexCoord = input.TexCoord;

    return output;
}