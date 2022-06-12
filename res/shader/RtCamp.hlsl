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

#define INVALID_ID          (-1)
#define STANDARD_RAY_INDEX  (0)
#define SHADOW_RAY_INDEX    (1)

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
    float3      Position;
    uint        MaterialId;
    float3      Normal;
    float3      Tangent;
    float2      TexCoord;

    bool HasHit()
    { return MaterialId != INVALID_ID; }
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
    float4x4 View;
    float4x4 Proj;
    float4x4 InvView;
    float4x4 InvProj;
    float4x4 InvViewProj;

    uint    MaxBounce;
    uint    FrameIndex;
    float   SkyIntensity;
    float   Exposure;
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
    float3  BaseColor;
    float3  Normal;
    float   Occlusion;
    float   Roughness;
    float   Metalness;
    float3  Emissive;
    float   Opacity;
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
Texture2D<float4>               BackGround  : register(t4);
SamplerState                    LinearWrap  : register(s0);
ConstantBuffer<SceneParameter>  SceneParam  : register(b0);

//-----------------------------------------------------------------------------
//      マテリアルIDとジオメトリをIDをパッキングします.
//-----------------------------------------------------------------------------
uint PackInstanceId(uint materialId, uint geometryId)
{
    return ((geometryId & 0x3FFF) << 10) | (materialId & 0x3FF);
}

//-----------------------------------------------------------------------------
//      マテリアルIDとジオメトリIDのパッキングを解除します.
//-----------------------------------------------------------------------------
void UnpackInstanceId(uint instanceId, out uint materialId, out uint geometryId)
{
    materialId = instanceId & 0x3FF;
    geometryId = (instanceId >> 10) & 0x3FFF;
}

//-----------------------------------------------------------------------------
//      PCG
//-----------------------------------------------------------------------------
uint4 Pcg(uint4 v)
{
    v = v * 1664525u + 101390422u;

    v.x += v.y * v.w;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.w += v.y * v.z;

    v = v ^ (v >> 16u);
    v.x += v.y * v.w;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.w += v.y * v.z;

    return v;
}

//-----------------------------------------------------------------------------
//      floatに変換します.
//-----------------------------------------------------------------------------
float ToFloat(uint x)
{
    return asfloat(0x3f800000 | (x >> 9)) - 1.0f;
}

//-----------------------------------------------------------------------------
//      乱数を初期化します.
//-----------------------------------------------------------------------------
uint4 InitRandom(uint2 pixelCoords, uint frameIndex)
{
    return uint4(pixelCoords.xy, frameIndex, 0);
}

//-----------------------------------------------------------------------------
//      疑似乱数を取得します.
//-----------------------------------------------------------------------------
float Random(uint4 seed)
{
    seed.w++;
    return ToFloat(Pcg(seed).x);
}

//-----------------------------------------------------------------------------
//      レイのオフセット値を取得します.
//-----------------------------------------------------------------------------
float3 OffsetRay(const float3 p, const float3 n)
{
    // Ray Tracing Gems, Chapter 6.
    static const float origin       = 1.0f / 32.0f;
    static const float float_scale  = 1.0f / 65536.0f;
    static const float int_scale    = 256.0f;

    int3 of_i = int3(int_scale * n.x, int_scale * n.y, int_scale * n.z);

    float3 p_i = float3(
        asfloat(asint(p.x) + ((p.x < 0) ? -of_i.x : of_i.x)),
        asfloat(asint(p.y) + ((p.y < 0) ? -of_i.y : of_i.y)),
        asfloat(asint(p.z) + ((p.z < 0) ? -of_i.z : of_i.z)));

    return float3(
        abs(p.x) < origin ? p.x + float_scale * n.x : p_i.x,
        abs(p.y) < origin ? p.y + float_scale * n.y : p_i.y,
        abs(p.z) < origin ? p.z + float_scale * n.z : p_i.z);
}

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
Material GetMaterial(uint materialId, float2 uv, float mip)
{
    uint  address = materialId * MATERIAL_STRIDE;
    uint4 data    = Materials.Load4(address);

    float4 bc  = float4(1.0f, 1.0f, 1.0f, 1.0f);
    float3 n   = float3(0.0f, 0.0f, 1.0f);
    float3 orm = float3(1.0f, 1.0f, 0.0f);
    float3 e   = float3(0.0f, 0.0f, 0.0f);

    if (data.x != INVALID_ID)
    {
        Texture2D<float4> baseColorMap = ResourceDescriptorHeap[data.x];
        bc = baseColorMap.SampleLevel(LinearWrap, uv, mip);
    }

    if (data.y != INVALID_ID)
    {
        Texture2D<float2> normalMap = ResourceDescriptorHeap[data.y];
        float2 n_xy = normalMap.SampleLevel(LinearWrap, uv, mip);
        float  n_z  = sqrt(abs(1.0f - dot(n_xy, n_xy)));
        n = float3(n_xy, n_z);
    }

    if (data.z != INVALID_ID)
    {
        Texture2D<float4> ormMap = ResourceDescriptorHeap[data.z];
        orm = ormMap.SampleLevel(LinearWrap, uv, mip).rgb;
    }

    if (data.w != INVALID_ID)
    {
        Texture2D<float4> emissiveMap = ResourceDescriptorHeap[data.w];
        e = emissiveMap.SampleLevel(LinearWrap, uv, mip).rgb;
    }

    Material param;
    param.BaseColor = bc.rgb;
    param.Opacity   = bc.a;
    param.Normal    = normalize(n);
    param.Occlusion = orm.x;
    param.Roughness = orm.y;
    param.Metalness = orm.z;
    param.Emissive  = e;

    return param;
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

        float4 pos = float4(asfloat(Vertices.Load3(address)), 1.0f);

        vertex.Position += mul(ObjectToWorld3x4(), pos).xyz * factor[i];
        vertex.Normal   += asfloat(Vertices.Load3(address + NORMAL_OFFSET)) * factor[i];
        vertex.Tangent  += asfloat(Vertices.Load3(address + TANGENT_OFFSET)) * factor[i];
        vertex.TexCoord += asfloat(Vertices.Load2(address + TEXCOORD_OFFSET)) * factor[i];
    }

    vertex.Normal  = normalize(mul(ObjectToWorld3x4(), float4(vertex.Normal,  0.0f)).xyz);
    vertex.Tangent = normalize(mul(ObjectToWorld3x4(), float4(vertex.Tangent, 0.0f)).xyz);

    return vertex;
}

//-----------------------------------------------------------------------------
//      スクリーン上へのレイを求めます.
//-----------------------------------------------------------------------------
RayDesc GeneratePinholeCameraRay(float2 pixel)
{
    float4 orig   = float4(0.0f, 0.0f, 0.0f, 1.0f);           // カメラの位置.
    float4 screen = float4(pixel, 0.0f, 1.0f); // スクリーンの位置.

    orig   = mul(SceneParam.InvView, orig);
    screen = mul(SceneParam.InvViewProj, screen);
    screen.xyz /= screen.w;

    RayDesc ray;
    ray.Origin      = orig.xyz;
    ray.Direction   = normalize(screen.xyz - orig.xyz);
    ray.TMin        = 0.0f;
    ray.TMax        = FLT_MAX;

    return ray;
}

//-----------------------------------------------------------------------------
//      シャドウレイをキャストします.
//-----------------------------------------------------------------------------
bool CastShadowRay(float3 pos, float3 normal, float3 dir, float tmax)
{
    RayDesc ray;
    ray.Origin      = OffsetRay(pos, normal);
    ray.Direction   = dir;
    ray.TMin        = 0.0f;
    ray.TMax        = tmax;

    ShadowPayload payload;
    payload.Visible = true;

    TraceRay(
        SceneAS,
        RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        0xFF,
        SHADOW_RAY_INDEX,
        0,
        SHADOW_RAY_INDEX,
        ray,
        payload);

    return payload.Visible;
}

//-----------------------------------------------------------------------------
//      IBLをサンプルします.
//-----------------------------------------------------------------------------
float4 SampleIBL(float3 dir)
{
    float2 uv = ToSphereMapCoord(dir);
    return BackGround.SampleLevel(LinearWrap, uv, 0.0f);
}

//-----------------------------------------------------------------------------
//      レイ生成シェーダです.
//-----------------------------------------------------------------------------
[shader("raygeneration")]
void OnGenerateRay()
{
    float2 pixel = float2(DispatchRaysIndex().xy);
    const float2 resolution = float2(DispatchRaysDimensions().xy);

    float2 uv = (pixel + 0.5f) / resolution;
    uv.y = 1.0f - uv.y;
    pixel = uv * 2.0f - 1.0f;

    // ペイロード初期化.
    Payload payload = (Payload)0;

    // レイを設定.
    RayDesc ray = GeneratePinholeCameraRay(pixel);

    // レイを追跡
    TraceRay(
        SceneAS,
        RAY_FLAG_NONE,
        0xFF,
        STANDARD_RAY_INDEX,
        0,
        STANDARD_RAY_INDEX,
        ray,
        payload);

    // レンダーターゲットに格納.
    Canvas[DispatchRaysIndex().xy] = payload.HasHit() ? float4(1.0f, 0.0f, 0.0f, 1.0f) : float4(0.0f, 0.0f, 0.0f, 1.0f);
    //Canvas[DispatchRaysIndex().xy] = SampleIBL(ray.Direction);
}

//-----------------------------------------------------------------------------
//      ヒット時のシェーダ.
//-----------------------------------------------------------------------------
[shader("closesthit")]
void OnClosestHit(inout Payload payload, in HitArgs args)
{
    Vertex vert = GetVertex(PrimitiveIndex(), args.barycentrics);

    payload.Position   = vert.Position;
    payload.Normal     = vert.Normal;
    payload.Tangent    = vert.Tangent;
    payload.TexCoord   = vert.TexCoord;
    payload.MaterialId = InstanceID();
}

//-----------------------------------------------------------------------------
//      シャドウレイヒット時のシェーダ.
//-----------------------------------------------------------------------------
[shader("closesthit")]
void OnShadowClosestHit(inout ShadowPayload payload, in HitArgs args)
{
    payload.Visible = true;
}

//-----------------------------------------------------------------------------
//      ミスシェーダです.
//-----------------------------------------------------------------------------
[shader("miss")]
void OnMiss(inout Payload payload)
{
    payload.MaterialId = INVALID_ID;
}

//-----------------------------------------------------------------------------
//      シャドウレイのミスシェーダです.
//-----------------------------------------------------------------------------
[shader("miss")]
void OnShadowMiss(inout ShadowPayload payload)
{
    payload.Visible = false;
}

