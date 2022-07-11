//-----------------------------------------------------------------------------
// File : Common.hlsli
// Desc : Common Uility.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------
#ifndef COMMON_HLSLI
#define COMMON_HLSLI

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
    bool   Visible;
};

///////////////////////////////////////////////////////////////////////////////
// MeshVertex structure
///////////////////////////////////////////////////////////////////////////////
struct MeshVertex
{
    float3  Position;
    float3  Normal;
    float3  Tangent;
    float2  TexCoord;
};

///////////////////////////////////////////////////////////////////////////////
// MeshMaterial structure
///////////////////////////////////////////////////////////////////////////////
struct MeshMaterial
{
    uint4   Textures;       // x:BaseColorMap, y:NormalMap, z:OrmMap, w:EmissiveMap.
};

///////////////////////////////////////////////////////////////////////////////
// Vertex structure
///////////////////////////////////////////////////////////////////////////////
struct Vertex
{
    float3  Position;       // 位置座標.
    float3  Normal;         // シェーディング法線.
    float3  Tangent;        // 接線ベクトル.
    float2  TexCoord;       // テクスチャ座標.
    float3  GeometryNormal; // ジオメトリ法線.
};

///////////////////////////////////////////////////////////////////////////////
// Instance structure
///////////////////////////////////////////////////////////////////////////////
struct Instance
{
    uint    VertexId;       // 頂点番号.
    uint    IndexId;        // 頂点インデックス番号.
    uint    MaterialId;     // マテリアル番号.
};

///////////////////////////////////////////////////////////////////////////////
// Light structure
///////////////////////////////////////////////////////////////////////////////
struct Light
{
    float3  Position;       // 位置座標.
    uint    Type;           // ライトタイプ.
    float3  Intensity;      // 強度.
    float   Radius;         // 半径.
};

///////////////////////////////////////////////////////////////////////////////
// Material structure
///////////////////////////////////////////////////////////////////////////////
struct Material
{
    float4  BaseColor;      // ベースカラー.
    //float3  Normal;
    float   Roughness;      // ラフネス.
    float   Metalness;      // メタルネス.
    float3  Emissive;       // エミッシブ.
};

//=============================================================================
// Constants
//=============================================================================
#define LIGHT_TYPE_POINT        (1)
#define LIGHT_TYPE_DIRECTIONAL  (2)

#define VERTEX_STRIDE       (sizeof(MeshVertex))
#define INDEX_STRIDE        (sizeof(uint3))
#define MATERIAL_STRIDE     (sizeof(MeshMaterial))
#define INSTANCE_STRIDE     (sizeof(Instance))
#define TRANSFORM_STRIDE    (sizeof(float3x4))

// For MeshVertex.
#define POSITION_OFFSET     (0)
#define NORMAL_OFFSET       (12)
#define TANGENT_OFFSET      (24)
#define TEXCOORD_OFFSET     (36)

// For Instance.
#define VERTEX_ID_OFFSET    (0)
#define INDEX_ID_OFFSET     (4)
#define MATERIAL_ID_OFFSET  (8)


//=============================================================================
// Resources
//=============================================================================
ConstantBuffer<SceneParameter>  SceneParam  : register(b0);
RayTracingAS                    SceneAS     : register(t0);
ByteAddressBuffer               Instances   : register(t1);
ByteAddressBuffer               Materials   : register(t2);
ByteAddressBuffer               Transforms  : register(t3);
SamplerState                    LinearWrap  : register(s0);


//-----------------------------------------------------------------------------
//      Permuted Congruential Generator (PCG)
//-----------------------------------------------------------------------------
uint4 PCG(uint4 v)
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
{ return asfloat(0x3f800000 | (x >> 9)) - 1.0f; }

//-----------------------------------------------------------------------------
//      乱数のシード値を設定します..
//-----------------------------------------------------------------------------
uint4 SetSeed(uint2 pixelCoords, uint frameIndex)
{ return uint4(pixelCoords.xy, frameIndex, 0); }

//-----------------------------------------------------------------------------
//      疑似乱数を取得します.
//-----------------------------------------------------------------------------
float Random(uint4 seed)
{
    seed.w++;
    return ToFloat(PCG(seed).x);
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
//      頂点データを取得します.
//-----------------------------------------------------------------------------
Vertex GetVertex(uint instanceId, uint triangleIndex, float2 barycentrices)
{
    uint2 id = Instances.Load2(instanceId * INSTANCE_STRIDE);

    uint3 indices = GetIndices(id.y, triangleIndex);
    Vertex vertex = (Vertex)0;

    // 重心座標を求める.
    float3 factor = float3(
        1.0f - barycentrices.x - barycentrices.y,
        barycentrices.x,
        barycentrices.y);

    ByteAddressBuffer vertices = ResourceDescriptorHeap[id.x];

    float3 v[3];

    float4 row0 = Transforms.Load4(instanceId * TRANSFORM_STRIDE);
    float4 row1 = Transforms.Load4(instanceId * TRANSFORM_STRIDE + 16);
    float4 row2 = Transforms.Load4(instanceId * TRANSFORM_STRIDE + 16);
    float3x4 world = float3x4(row0, row1, row2);

    [unroll]
    for(uint i=0; i<3; ++i)
    {
        uint address = indices[i] * VERTEX_STRIDE;

        v[i] = asfloat(vertices.Load3(address));
        float4 pos = float4(v[i], 1.0f);

        vertex.Position += mul(world, pos).xyz * factor[i];
        vertex.Normal   += asfloat(vertices.Load3(address + NORMAL_OFFSET)) * factor[i];
        vertex.Tangent  += asfloat(vertices.Load3(address + TANGENT_OFFSET)) * factor[i];
        vertex.TexCoord += asfloat(vertices.Load2(address + TEXCOORD_OFFSET)) * factor[i];
    }

    vertex.Normal  = normalize(mul(world, float4(vertex.Normal,  0.0f)).xyz);
    vertex.Tangent = normalize(mul(world, float4(vertex.Tangent, 0.0f)).xyz);

    float3 e0 = v[2] - v[0];
    float3 e1 = v[1] - v[0];
    vertex.GeometryNormal = normalize(cross(e0, e1));

    return vertex;
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

    //Texture2D<float4> normalMap = ResourceDescriptorHeap[data.y];
    //float3 n = normalMap.SampleLevel(LinearWrap, uv, mip).xyz * 2.0f - 1.0f;
    //n = normalize(n);

    Texture2D<float4> ormMap = ResourceDescriptorHeap[data.z];
    float3 orm = ormMap.SampleLevel(LinearWrap, uv, mip).rgb;

    Texture2D<float4> emissiveMap = ResourceDescriptorHeap[data.w];
    float3 e = emissiveMap.SampleLevel(LinearWrap, uv, mip).rgb;

    Material param;
    param.BaseColor = bc;
    //param.Normal    = n;
    param.Roughness = orm.y;
    param.Metalness = orm.z;
    param.Emissive  = e;

    return param;
}

//-----------------------------------------------------------------------------
//      ライト強度を取得します.
//-----------------------------------------------------------------------------
float3 GetLightIntensity(Light light, float dist)
{
    if (light.Type == LIGHT_TYPE_POINT)
    {
        const float radiusSq    = light.Radius * light.Radius;
        const float distSq      = dist * dist;
        const float attenuation = 2.0f / (distSq + radiusSq + dist * sqrt(distSq + radiusSq));
        return light.Intensity * attenuation;
    }
    else if (light.Type == LIGHT_TYPE_DIRECTIONAL)
    {
        return light.Intensity;
    }
    else
    {
        return float3(1.0f, 1.0f, 1.0f);
    }
}

//-----------------------------------------------------------------------------
//      ライトデータを取得します.
//-----------------------------------------------------------------------------
void GetLightData(Light light, float3 hitPos, out float3 lightVector, out float lightDistance)
{
    if (light.Type == LIGHT_TYPE_POINT)
    {
        lightVector   = light.Position - hitPos;
        lightDistance = length(lightVector);
    }
    else if (light.Type == LIGHT_TYPE_DIRECTIONAL)
    {
        lightVector   = light.Position;
        lightDistance = FLT_MAX;
    }
    else
    {
        lightVector   = float3(0.0f, 1.0f, 0.0f);
        lightDistance = FLT_MAX;
    }
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
//      輝度値を求めます.
//-----------------------------------------------------------------------------
float Luminance(float3 rgb)
{ return dot(rgb, float3(0.2126f, 0.7152f, 0.0722f)); }

//-----------------------------------------------------------------------------
//      Diffuseをサンプルするかどうかの確率を求めます.
//-----------------------------------------------------------------------------
float ProbabilityToSampleDiffuse(float3 diffuseColor, float3 specularColor)
{
    // DirectX Raytracing, Tutorial 14.
    // http://cwyman.org/code/dxrTutors/tutors/Tutor14/tutorial14.md.html
    float lumDiffuse  = max(0.01f, Luminance(diffuseColor));
    float lumSpecular = max(0.01f, Luminance(specularColor));
    return lumDiffuse / (lumDiffuse + lumSpecular);
}

//-----------------------------------------------------------------------------
//      出射方向をサンプリングします.
//-----------------------------------------------------------------------------
float3 SampleDir(float3 V, float3 N, float3 u, Material material)
{
    // 完全拡散反射.
    if (material.Metalness == 0.0f && material.Roughness == 1.0f)
    {
        float3 T, B;
        CalcONB(N, T, B);

        float3 s = SampleLambert(u.xy);
        return normalize(T * s.x + B * s.y + N * s.z);
    }
    // 完全鏡面反射.
    else if (material.Metalness == 1.0f && material.Roughness == 0.0f)
    {
        return normalize(reflect(V, N));
    }
    else
    {
        float3 diffuseColor  = ToKd(material.BaseColor.rgb, material.Metalness);
        float3 specularColor = ToKs(material.BaseColor.rgb, material.Metalness);

        float p = ProbabilityToSampleDiffuse(diffuseColor, specularColor);

        // Diffuse.
        if (u.z < p)
        {
            float3 T, B;
            CalcONB(N, T, B);

            float3 s = SampleLambert(u.xy);
            return normalize(T * s.x + B * s.y + N * s.z);
        }

        // Specular.
        float3 H = SampleGGX(u.xy, material.Roughness);
        return normalize(reflect(V, H));
    }
}

//-----------------------------------------------------------------------------
//      マテリアルを評価します.
//-----------------------------------------------------------------------------
float3 EvaluateMaterial
(
    float3      V,          // 入射方向.
    float3      N,          // 法線ベクトル.
    float3      u,          // 乱数.
    Material    material,   // マテリアル.
    out float3  dir,        // 出射方向.
    out float   pdf         // 確率密度関数.
)
{
    // 完全拡散反射.
    if (material.Metalness == 0.0f && material.Roughness == 1.0f)
    {
        float3 T, B;
        CalcONB(N, T, B);

        float3 s = SampleLambert(u.xy);
        float3 L = normalize(T * s.x + B * s.y + N * s.z);

        float NoL = abs(dot(N, L));

        dir = L;
        pdf = NoL / F_PI;

        return (material.BaseColor.rgb / F_PI) * NoL;
    }
    // 完全鏡面反射.
    else if (material.Metalness == 1.0f && material.Roughness == 0.0f)
    {
        float3 L = normalize(reflect(V, N));
        dir = L;
        pdf = 1.0f;

        return material.BaseColor.rgb;
    }
    else
    {
        float3 diffuseColor  = ToKd(material.BaseColor.rgb, material.Metalness);
        float3 specularColor = ToKs(material.BaseColor.rgb, material.Metalness);

        float p = ProbabilityToSampleDiffuse(diffuseColor, specularColor);

        // Diffuse
        if (u.z < p)
        {
            float3 T, B;
            CalcONB(N, T, B);

            float3 s = SampleLambert(u.xy);
            float3 L = normalize(T * s.x + B * s.y + N * s.z);

            float NoL = abs(dot(N, L));

            dir = L;
            pdf = (NoL / F_PI) / p;

            return (diffuseColor / F_PI) * NoL;
        }

        // Specular.
        float3 H = SampleGGX(u.xy, material.Roughness);
        float3 L = normalize(reflect(V, H));

        float NoH = abs(dot(N, H));
        float NoV = abs(dot(N, V));
        float NoL = abs(dot(N, L));
        float LoH = abs(dot(L, H));

        float  a   = max(Pow2(material.Roughness), 0.01f);
        float  f90 = saturate(50.0f * dot(specularColor, 0.33f));
        float  D   = D_GGX(NoH, a);
        float  G   = G_SmithGGX(NoL, NoV, a);
        float3 F   = F_Schlick(specularColor, f90, LoH);

        dir = L;
        pdf = (D * NoH / (4.0f * LoH)) / (1.0f - p);

        return ((D * F * G) / F_PI) * NoL;
    }
}

#endif//COMMON_HLSLI
