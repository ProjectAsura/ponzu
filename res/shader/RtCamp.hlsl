//-----------------------------------------------------------------------------
// File : RtCamp.hlsl
// Desc : レイトレ合宿提出用シェーダ.
// Copyright(c) Project Asura. All right resreved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <Math.hlsli>
#include <BRDF.hlsli>

//-----------------------------------------------------------------------------
// Type Definitions
//-----------------------------------------------------------------------------
typedef BuiltInTriangleIntersectionAttributes HitArgs;
typedef RaytracingAccelerationStructure       RayTracingAS;


///////////////////////////////////////////////////////////////////////////////
// Payload structure
///////////////////////////////////////////////////////////////////////////////
struct Payload
{
    float4  Color;
};

///////////////////////////////////////////////////////////////////////////////
// ShadowPayload structure
///////////////////////////////////////////////////////////////////////////////
struct ShadowPayload
{
    float   Visible;
};

///////////////////////////////////////////////////////////////////////////////
// SceneParameter structure
///////////////////////////////////////////////////////////////////////////////
struct SceneParameter
{
    float4x4    InvView;
    float4x4    InvViewProj;
};

///////////////////////////////////////////////////////////////////////////////
// Vertex structure
///////////////////////////////////////////////////////////////////////////////
struct Vertex
{
    float3      Position;
    float3      Normal;
    float3      Tangent;
    float2      TexCoord;
};

///////////////////////////////////////////////////////////////////////////////
// Material structure
///////////////////////////////////////////////////////////////////////////////
struct Material
{
    uint BaseColor;
    uint Normal;
    uint ORM;
};

#define VERTEX_STRIDE       (44)
#define INDEX_STRIDE        (12)
#define MATERIAL_STRIDE     (12)
#define POSITION_OFFSET     (0)
#define NORMAL_OFFSET       (12)
#define TANGENT_OFFSET      (24)
#define TEXCOORD_OFFSET     (36)


//-----------------------------------------------------------------------------
// Resources
//-----------------------------------------------------------------------------
RWTexture2D<float4>             Canvas      : register(u0);
RayTracingAS                    SceneAS     : register(t0);
ByteAddressBuffer               Vertices    : register(t1);
ByteAddressBuffer               Indices     : register(t2);
ByteAddressBuffer               Materials   : register(t3);
Texture2D<float>                BackGround  : register(t4);
SamplerState                    LinearWrap  : register(s0);
ConstantBuffer<SceneParameter>  SceneParam  : register(b0);


//-----------------------------------------------------------------------------
//      頂点インデックスを取得します.
//-----------------------------------------------------------------------------
uint3 GetIndices(uint triangleIndex)
{
    uint address = triangleIndex * INDEX_STRIDE;
    return Indices.Load3(address);
}

//-----------------------------------------------------------------------------
//      マテリアルを取得します.
//-----------------------------------------------------------------------------
Material GetMaterial(uint triangleIndex)
{
    uint address = triangleIndex * MATERIAL_STRIDE;
    uint3 data = Materials.Load3(address);

    Material material;
    material.BaseColor = data.x;
    material.Normal    = data.y;
    material.ORM       = data.z;
    return material;
}

//-----------------------------------------------------------------------------
//      頂点データを取得します.
//-----------------------------------------------------------------------------
Vertex GetVertex(uint triangleIndex, float2 barycentrices)
{
    uint3 indices = GetIndices(triangleIndex);
    Vertex vertex = (Vertex)0;

    // 重心座標を求める.
    float3 factor = float3(
        1.0f - barycentrices.x - barycentrices.y,
        barycentrices.x,
        barycentrices.y);

    [unroll]
    for(uint i=0; i<3; ++i)
    {
        uint address = indices[i] * VERTEX_STRIDE;

        vertex.Position += asfloat(Vertices.Load3(address)) * factor[i];
        vertex.Normal   += asfloat(Vertices.Load3(address + NORMAL_OFFSET)) * factor[i];
        vertex.Tangent  += asfloat(Vertices.Load3(address + TANGENT_OFFSET)) * factor[i];
        vertex.TexCoord += asfloat(Vertices.Load2(address + TEXCOORD_OFFSET)) * factor[i];
    }

    return vertex;
}

//-----------------------------------------------------------------------------
//      頂点位置のみを取得します.
//-----------------------------------------------------------------------------
float3 GetPosition(uint triangleIndex, float2 barycentrices)
{
    uint3  indices  = GetIndices(triangleIndex);
    float3 position = float3(0.0f, 0.0f, 0.0f);

    // 重心座標を求める.
    float3 factor = float3(
        1.0f - barycentrices.x - barycentrices.y,
        barycentrices.x,
        barycentrices.y);

    [unroll]
    for(uint i=0; i<3; ++i)
    {
        uint address = indices[i] * VERTEX_STRIDE;
        position += asfloat(Vertices.Load3(address)) * factor[i];
    }

    return position;
}

//-----------------------------------------------------------------------------
//      スクリーン上へのレイを求めます.
//-----------------------------------------------------------------------------
void CalcRay(float2 index, out float3 pos, out float3 dir)
{
    float4 orig   = float4(0.0f, 0.0f, 0.0f, 1.0f);
    float4 screen = float4(-2.0f * index + 1.0f, 0.0f, 1.0f);

    // カメラ位置とスクリーン位置を求める.
    orig   = mul(SceneParam.InvView, orig);
    screen = mul(SceneParam.InvViewProj, screen);

    // w = 1 に射影.
    screen /= screen.w;

    // レイを設定.
    pos = orig.xyz;
    dir = normalize(screen.xyz - orig.xyz);
}

//-----------------------------------------------------------------------------
//      レイ生成シェーダです.
//-----------------------------------------------------------------------------
[shader("raygeneration")]
void OnTraceRay()
{
    float2 index = (float2)DispatchRaysIndex() / (float2)DispatchRaysDimensions();

    // レイを生成.
    float3 rayPos;
    float3 rayDir;
    CalcRay(index, rayPos, rayDir);

    // ペイロード初期化.
    Payload payload;
    payload.Color  = float4(0.0f, 0.0f, 0.0f, 0.0f);

    // レイを設定.
    RayDesc rayDesc;
    rayDesc.Origin      = rayPos;
    rayDesc.Direction   = rayDir;
    rayDesc.TMin        = 1e-3f;
    rayDesc.TMax        = 10000.0;

    // レイを追跡
    TraceRay(SceneAS, RAY_FLAG_NONE, ~0, 0, 1, 0, rayDesc, payload);

    // レンダーターゲットに格納.
    Canvas[DispatchRaysIndex().xy] = payload.Color;
}

//-----------------------------------------------------------------------------
//      ミスシェーダです.
//-----------------------------------------------------------------------------
[shader("miss")]
void OnMiss(inout Payload payload)
{
    // スフィアマップのテクスチャ座標を算出.
    float3 dir = WorldRayDirection();
    float2 uv  = ToSphereMapCoord(dir);

    // スフィアマップをサンプル.
    float4 color = BackGround.SampleLevel(LinearWrap, uv, 0.0f);

    // 色を設定.
    payload.Color = color;
}

