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
#include <SceneParam.hlsli>

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
    uint    InstanceId;
    uint    PrimitiveId;
    float2  Barycentrics;

    bool HasHit()
    { return InstanceId != INVALID_ID; }
};

///////////////////////////////////////////////////////////////////////////////
// ShadowPayload structure
///////////////////////////////////////////////////////////////////////////////
struct ShadowPayload
{
    float   Visible;
};

///////////////////////////////////////////////////////////////////////////////
// Vertex structure
///////////////////////////////////////////////////////////////////////////////
struct Vertex
{
    float3  Position;
    float3  Normal;
    float3  Tangent;
    float2  TexCoord;
    float3  GeometryNormal;
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
// Material structure
///////////////////////////////////////////////////////////////////////////////
struct Material
{
    float4  BaseColor;
    float3  Normal;
    float   Occlusion;
    float   Roughness;
    float   Metalness;
    float3  Emissive;
};

#define VERTEX_STRIDE       (44)
#define INDEX_STRIDE        (12)
#define MATERIAL_STRIDE     (12)
#define INSTANCE_STRIDE     (12)

#define POSITION_OFFSET     (0)
#define NORMAL_OFFSET       (12)
#define TANGENT_OFFSET      (24)
#define TEXCOORD_OFFSET     (36)

#define VERTEX_ID_OFFSET    (0)
#define INDEX_ID_OFFSET     (4)
#define MATERIAL_ID_OFFSET  (8)

//-----------------------------------------------------------------------------
// Resources
//-----------------------------------------------------------------------------
RWTexture2D<float4>             Canvas      : register(u0);
RayTracingAS                    SceneAS     : register(t0);
ByteAddressBuffer               Instances   : register(t1);
ByteAddressBuffer               Materials   : register(t2);
Texture2D<float4>               BackGround  : register(t3);
SamplerState                    LinearWrap  : register(s0);
ConstantBuffer<SceneParameter>  SceneParam  : register(b0);

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
uint3 GetIndices(uint indexId, uint triangleIndex)
{
    uint address = triangleIndex * INDEX_STRIDE;
    ByteAddressBuffer indices = ResourceDescriptorHeap[indexId];
    return indices.Load3(address);
}

//-----------------------------------------------------------------------------
//      マテリアルを取得します.
//-----------------------------------------------------------------------------
Material GetMaterial(uint instanceId, float2 uv, float mip)
{
    uint  materialId = Instances.Load(instanceId * INSTANCE_STRIDE + 8);

    uint  address = materialId * MATERIAL_STRIDE;
    uint4 data    = Materials.Load4(address);

    Texture2D<float4> baseColorMap = ResourceDescriptorHeap[data.x];
    float4 bc = baseColorMap.SampleLevel(LinearWrap, uv, mip);

    Texture2D<float4> normalMap = ResourceDescriptorHeap[data.y];
    float3 n = normalMap.SampleLevel(LinearWrap, uv, mip).xyz * 2.0f - 1.0f;
    n = normalize(n);

    Texture2D<float4> ormMap = ResourceDescriptorHeap[data.z];
    float3 orm = ormMap.SampleLevel(LinearWrap, uv, mip).rgb;

    Texture2D<float4> emissiveMap = ResourceDescriptorHeap[data.w];
    float3 e = emissiveMap.SampleLevel(LinearWrap, uv, mip).rgb;

    Material param;
    param.BaseColor = bc;
    param.Normal    = n;
    param.Occlusion = orm.x;
    param.Roughness = orm.y;
    param.Metalness = orm.z;
    param.Emissive  = e;

    return param;
}

//-----------------------------------------------------------------------------
//      頂点データを取得します.
//-----------------------------------------------------------------------------
Vertex GetVertex(uint instanceId, uint triangleIndex, float2 barycentrices)
{
    uint2 resId = Instances.Load2(instanceId * INSTANCE_STRIDE);

    uint3 indices = GetIndices(resId.y, triangleIndex);
    Vertex vertex = (Vertex)0;

    // 重心座標を求める.
    float3 factor = float3(
        1.0f - barycentrices.x - barycentrices.y,
        barycentrices.x,
        barycentrices.y);

    ByteAddressBuffer vertices = ResourceDescriptorHeap[resId.x];

    float3 v[3];

    [unroll]
    for(uint i=0; i<3; ++i)
    {
        uint address = indices[i] * VERTEX_STRIDE;

        v[i] = asfloat(vertices.Load3(address));
        float4 pos = float4(v[i], 1.0f);

        vertex.Position += mul(ObjectToWorld3x4(), pos).xyz * factor[i];
        vertex.Normal   += asfloat(vertices.Load3(address + NORMAL_OFFSET)) * factor[i];
        vertex.Tangent  += asfloat(vertices.Load3(address + TANGENT_OFFSET)) * factor[i];
        vertex.TexCoord += asfloat(vertices.Load2(address + TEXCOORD_OFFSET)) * factor[i];
    }

    vertex.Normal  = normalize(mul(ObjectToWorld3x4(), float4(vertex.Normal,  0.0f)).xyz);
    vertex.Tangent = normalize(mul(ObjectToWorld3x4(), float4(vertex.Tangent, 0.0f)).xyz);

    float3 e0 = v[2] - v[0];
    float3 e1 = v[1] - v[0];
    vertex.GeometryNormal = normalize(cross(e0, e1));

    return vertex;
}

//-----------------------------------------------------------------------------
//      スクリーン上へのレイを求めます.
//-----------------------------------------------------------------------------
RayDesc GeneratePinholeCameraRay(float2 pixel)
{
    float4 orig   = float4(0.0f,  0.0f, 0.0f, 1.0f); // カメラの位置.
    float4 screen = float4(pixel, 0.0f, 1.0f);       // スクリーンの位置.

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
    return BackGround.SampleLevel(LinearWrap, uv, 0.0f) * SceneParam.SkyIntensity;
}

//-----------------------------------------------------------------------------
//      輝度値を求めます.
//-----------------------------------------------------------------------------
float Luminance(float3 rgb)
{ return dot(rgb, float3(0.2126f, 0.7152f, 0.0722f)); }

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

#if 1
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
    Canvas[DispatchRaysIndex().xy] = payload.HasHit() ? float4(1.0f, 0.0f, 0.0f, 1.0f) : SampleIBL(ray.Direction);
    //Canvas[DispatchRaysIndex().xy] = SampleIBL(ray.Direction);
#else
    uint4 randState = InitRandom(DispatchRaysIndex().xy, SceneParam.FrameIndex);

    float3 W = float4(1.0f, 1.0f, 1.0f);  // 重み.
    float3 L = float4(0.0f, 0.0f, 0.0f);  // 放射輝度.

    for(int bounce=0; depth<SceneParam.MaxBounce; ++bounce)
    {
        // 交差判定.
        TraceRay(
            SceneAS,
            RAY_FLAG_NONE,
            0xFF,
            STANDARD_RAY_INDEX,
            0,
            STANDARD_RAY_INDEX,
            ray,
            payload);

        if (!payload.HasHit())
        {
            L += W * SampleIBL(ray.Direction);
            break;
        }

        // 頂点データ取得.
        Vertex vertex = GetVertex(payload.InstanceId, payload.PrimitiveId, payload.Barycentrics);

        float3 geometryNormal = vertex.GeometryNormal;
        float3 N = vertex.Normal;
        float3 T = vertex.Tangent;
        float3 V = -ray.Direction;
        if (dot(geometryNormal, V) < 0.0f) geometryNormal = -geometryNormal;
        if (dot(geometryNormal, N) < 0.0f) 
        {
            N = -N;
            T = -T;
        }

        // マテリアル取得.
        Material material = GetMaterial(payload.InstanceId, vertex.TexCoord, 0.0f);

        // 自己発光による放射輝度.
        L += W * material.Emissive;

        // RIS
        {
        }

        // 法線ベクトルを計算.
        float3 B = normalize(cross(T, N));
        float3 normal = FromTangentSpaceToWorld(material.Normal, T, B, N);

        // シェーディング処理.
        float3 dir;


        // ロシアンルーレット.
        if (bounce > SceneParam.MinBounce)
        {
            float p = min(0.95f, Lumiance(W));
            if (p < Random(randState))
            { break; }
            else
            { W /= p; }
        }

        // 重みがゼロに成ったら以降の更新は無駄なので打ち切りにする.
        if (all(W < FLT_EPSILON.xxx))
        { break; }

        // レイを更新.
        ray.Orig      = OffsetRay(vertex.Position, geometryNormal);
        ray.Direction = dir;
    }

    float invSampleCount = 1.0f; // TODO.
    float3 prevL = Canvas[DispatchRaysIndex().xy].rgb;
    Canvas[DispatchRaysIndex().xy] = float4(prevL + L * invSampleCount, 1.0f);
#endif
}

//-----------------------------------------------------------------------------
//      ヒット時のシェーダ.
//-----------------------------------------------------------------------------
[shader("closesthit")]
void OnClosestHit(inout Payload payload, in HitArgs args)
{
    payload.InstanceId   = InstanceID();
    payload.PrimitiveId  = PrimitiveIndex();
    payload.Barycentrics = args.barycentrics;
}

//-----------------------------------------------------------------------------
//      ミスシェーダです.
//-----------------------------------------------------------------------------
[shader("miss")]
void OnMiss(inout Payload payload)
{
    payload.InstanceId = INVALID_ID;
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
//      シャドウレイのミスシェーダです.
//-----------------------------------------------------------------------------
[shader("miss")]
void OnShadowMiss(inout ShadowPayload payload)
{
    payload.Visible = false;
}

