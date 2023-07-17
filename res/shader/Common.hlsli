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

#define ENABLE_TEXTURED_MATERIAL    (0) // テクスチャ付きマテリアルを有効にする場合は 1.

#define INVALID_ID          (-1)
#define STANDARD_RAY_INDEX  (0)
#define SHADOW_RAY_INDEX    (1)
#define T_MIN               (1e-6f) // シーンの大きさによって適切な値が変わるので，適宜調整.

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
    
    void Clear()
    {
        InstanceId   = INVALID_ID;
        PrimitiveId  = INVALID_ID;
        Barycentrics = 0.0f.xx;
    }

    bool HasHit(uint instanceId, uint primitiveId)
    {
        if (InstanceId == INVALID_ID) {
            return false;
        }
        if (InstanceId == instanceId && PrimitiveId == primitiveId) {
            return false;
        }
        return true;
    }
};

///////////////////////////////////////////////////////////////////////////////
// ResVertex structure
///////////////////////////////////////////////////////////////////////////////
struct ResVertex
{
    float3  Position;
    float3  Normal;
    float3  Tangent;
    float2  TexCoord;
};

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

///////////////////////////////////////////////////////////////////////////////
// SurfaceHit structure
///////////////////////////////////////////////////////////////////////////////
struct SurfaceHit
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
    uint    Type;           // ライトタイプ.
    float3  Intensity;      // 強度.
    float3  Position;       // 位置座標.
    float   Radius;         // 半径.
};

///////////////////////////////////////////////////////////////////////////////
// Material structure
///////////////////////////////////////////////////////////////////////////////
struct Material
{
    float4  BaseColor;      // ベースカラー.
    float3  Normal;         // 法線ベクトル.
    float   Roughness;      // ラフネス.
    float3  Emissive;       // エミッシブ.
    float   Metalness;      // メタルネス.
    float   Ior;            // 屈折率.
};

//=============================================================================
// Constants
//=============================================================================
#define LIGHT_TYPE_POINT        (1)
#define LIGHT_TYPE_DIRECTIONAL  (2)

#define VERTEX_STRIDE       (sizeof(ResVertex))
#define INDEX_STRIDE        (sizeof(uint3))
#define MATERIAL_STRIDE     (sizeof(ResMaterial))
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
StructuredBuffer<ResMaterial>   Materials   : register(t2);
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
float Random(inout uint4 seed)
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
//      表面交差情報を取得します.
//-----------------------------------------------------------------------------
SurfaceHit GetSurfaceHit(uint instanceId, uint triangleIndex, float2 barycentrices)
{
    uint2 id = Instances.Load2(instanceId * INSTANCE_STRIDE);

    uint3 indices = GetIndices(id.y, triangleIndex);
    SurfaceHit surfaceHit = (SurfaceHit)0;

    // 重心座標を求める.
    float3 factor = float3(
        1.0f - barycentrices.x - barycentrices.y,
        barycentrices.x,
        barycentrices.y);

    ByteAddressBuffer vertices = ResourceDescriptorHeap[id.x];

    float3 v[3];

    float4 row0 = asfloat(Transforms.Load4(instanceId * TRANSFORM_STRIDE));
    float4 row1 = asfloat(Transforms.Load4(instanceId * TRANSFORM_STRIDE + 16));
    float4 row2 = asfloat(Transforms.Load4(instanceId * TRANSFORM_STRIDE + 32));
    float3x4 world = float3x4(row0, row1, row2);

    [unroll]
    for(uint i=0; i<3; ++i)
    {
        uint address = indices[i] * VERTEX_STRIDE;

        v[i] = asfloat(vertices.Load3(address));
        v[i] = mul(world, float4(v[i], 1.0f)).xyz;

        surfaceHit.Position += v[i] * factor[i];
        surfaceHit.Normal   += asfloat(vertices.Load3(address + NORMAL_OFFSET))   * factor[i];
        surfaceHit.Tangent  += asfloat(vertices.Load3(address + TANGENT_OFFSET))  * factor[i];
        surfaceHit.TexCoord += asfloat(vertices.Load2(address + TEXCOORD_OFFSET)) * factor[i];
    }

    surfaceHit.Normal  = normalize(mul((float3x3)world, normalize(surfaceHit.Normal)));
    surfaceHit.Tangent = normalize(mul((float3x3)world, normalize(surfaceHit.Tangent)));

    float3 e0 = v[0] - v[1];
    float3 e1 = v[2] - v[1];
    surfaceHit.GeometryNormal = normalize(cross(e0, e1));

    return surfaceHit;
}

//-----------------------------------------------------------------------------
//      マテリアルを取得します.
//-----------------------------------------------------------------------------
Material GetMaterial(uint instanceId, float2 uv, float mip)
{
    uint  materialId = Instances.Load(instanceId * INSTANCE_STRIDE + 8);

    ResMaterial mat = Materials[materialId];
    
 #if ENABLE_TEXTURED_MATERIAL
    Texture2D<float4> baseColorMap = ResourceDescriptorHeap[mat.Textures0.x];
    float4 bc = baseColorMap.SampleLevel(LinearWrap, uv, mip);

    Texture2D<float4> normalMap = ResourceDescriptorHeap[mat.Textures0.y];
    float3 n = normalMap.SampleLevel(LinearWrap, uv, mip).xyz;
    n = normalize(n * 2.0f - 1.0f);

    Texture2D<float4> ormMap = ResourceDescriptorHeap[mat.Textures0.z];
    float3 orm = ormiMap.SampleLevel(LinearWrap, uv, mip).rgb;

    Texture2D<float4> emissiveMap = ResourceDescriptorHeap[mat.Textures0.w];
    float3 e = emissiveMap.SampleLevel(LinearWrap, uv, mip).rgb;

    Material param;
    param.BaseColor = bc * mat.BaseColor;
    param.Normal    = n;
    param.Roughness = ormi.y * mat.Roughness;
    param.Metalness = ormi.z * mat.Metalness;
    param.Emissive  = e * mat.Emissive.rgb * mat.Emissive.a;
    param.Ior       = mat.Ior;
#else
    Material param;
    param.BaseColor = mat.BaseColor;
    param.Normal    = float3(0.0f, 0.0f, 1.0f);
    param.Roughness = mat.Roughness;
    param.Metalness = mat.Metalness;
    param.Emissive  = mat.Emissive.rgb * mat.Emissive.a;
    param.Ior       = mat.Ior;
#endif

    return param;
}

//-----------------------------------------------------------------------------
//      完全鏡面反射かどうかチェック.
//-----------------------------------------------------------------------------
bool IsPerfectSpecular(Material material)
{ return (material.Metalness == 1.0f && material.Roughness == 0.0f); }

//-----------------------------------------------------------------------------
//      完全拡散反射かどうかチェック.
//-----------------------------------------------------------------------------
bool IsPerfectDiffuse(Material material)
{ return (material.Metalness == 0.0f && material.Roughness == 1.0f); }

//-----------------------------------------------------------------------------
//      透明屈折マテリアルかどうかチェックします.
//-----------------------------------------------------------------------------
bool IsDielectric(Material material)
{ return (material.Ior > 0.0f); }

//-----------------------------------------------------------------------------
//      デルタ関数を持つかどうかチェックします.
//-----------------------------------------------------------------------------
bool HasDelta(Material material)
{ return IsPerfectSpecular(material) || IsDielectric(material); }

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
        lightVector   = -light.Position;
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
    RayDesc ray;
    ray.Origin      = GetPosition(SceneParam.View);
    ray.Direction   = CalcRayDir(pixel, SceneParam.View, SceneParam.Proj);
    ray.TMin        = T_MIN;
    ray.TMax        = FLT_MAX;

    return ray;
}

//-----------------------------------------------------------------------------
//      シャドウレイをキャストします.
//-----------------------------------------------------------------------------
bool CastShadowRay(float3 pos, float3 normal, float3 dir, float tmax, uint instanceId, uint primitiveId)
{
    RayDesc ray;
    ray.Origin      = pos + normal * T_MIN;
    ray.Direction   = dir;
    ray.TMin        = T_MIN;
    ray.TMax        = tmax;

    Payload payload;
    payload.Clear();
    
    uint flags = RAY_FLAG_SKIP_CLOSEST_HIT_SHADER;
    flags |= RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;
    flags |= RAY_FLAG_CULL_FRONT_FACING_TRIANGLES;

    TraceRay(
        SceneAS,
        flags,
        0xFF,
        SHADOW_RAY_INDEX,
        0,
        SHADOW_RAY_INDEX,
        ray,
        payload);

    return payload.HasHit(instanceId, primitiveId);
}

//-----------------------------------------------------------------------------
//      輝度値を求めます.
//-----------------------------------------------------------------------------
float Luminance(float3 rgb)
{ return dot(rgb, float3(0.2126f, 0.7152f, 0.0722f)); }

float ShadowedF90(float3 F0)
{
    const float MIN_DIELECTRICS_F0 = 0.04f;
    const float t = 1.0f / MIN_DIELECTRICS_F0;
    return min(1.0f, t * Luminance(F0));
}

float Smith_G1_GGX(float alpha, float NdotS)
{
    float alpha2 = alpha * alpha;
    float NdotS2 = NdotS * NdotS;
    return 2.0f / (sqrt(((alpha2 * (1.0f - NdotS2)) + NdotS2) / NdotS2) + 1.0f);
}

float Smith_G2(float alpha, float NdotL, float NdotV)
{
    float G1V = Smith_G1_GGX(alpha, NdotV);
    float G1L = Smith_G1_GGX(alpha, NdotL);
    return G1L / (G1V + G1L - G1V * G1L);
}

float D_ggx(float3 N, float alpha_x, float alpha_y)
{
    float term = Pow2(N.x / alpha_x) + Pow2(N.y / alpha_y) + Pow2(N.z);
    return 1.0f / (F_PI * alpha_x * alpha_y * Pow2(term));
}

float Lambda(float3 V, float alpha_x, float alpha_y)
{
    return (-1.0f + sqrt(1.0f + (Pow2(alpha_x * V.x) + Pow2(alpha_y * V.y)) / Pow2(V.z))) * 0.5f;
}

float G1(float3 V, float alpha_x, float alpha_y)
{
    return 1.0f / (1.0f + Lambda(V, alpha_x, alpha_y));
}

float Dv(float3 N, float3 V, float alpha_x, float alpha_y)
{
    return G1(V, alpha_x, alpha_y) * max(0.0f, dot(V, N)) * D_ggx(N, alpha_x, alpha_y) / V.z;
}

float G2(float3 L, float3 V, float alpha_x, float alpha_y)
{
    return G1(L, alpha_x, alpha_y) * G1(V, alpha_x, alpha_y);
}

float ggxNormalDistribution(float NdotH, float roughness)
{
    float a2 = roughness * roughness;
    float d = (NdotH * a2 - NdotH) * NdotH + 1.0f;
    return a2 / (d * d * F_PI);
}

float schlickMaskingTerm(float NdotL, float NdotV, float roughness)
{
    float k = roughness * roughness / 2.0f;

    float g_v = NdotV / (NdotV * (1.0f - k) + k);
    float g_l = NdotL / (NdotL * (1.0f - k) + k);
    return g_v * g_l;
}

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
//      マテリアルを評価します.
//-----------------------------------------------------------------------------
float3 SampleMaterial
(
    float3   V,         // 視線ベクトル.
    float3   N,         // 法線ベクトル.
    float3   L,         // ライトベクトル.
    float    u,         // 乱数.
    float    ior,       // 屈折率.
    Material material   // マテリアル.
)
{
    // 物体からのレイの入出を考慮した法線.
    float3 Nm = dot(N, V) < 0.0f ? -N : N;

    // 透明屈折.
    if (IsDielectric(material))
    {
        // レイがオブジェクトから出射するか入射するか?
        bool into = dot(N, Nm) > 0.0f;
        
        // Snellの法則.
        float n1 = (into) ? ior : material.Ior;
        float n2 = (into) ? material.Ior : ior;

        // 相対屈折率.
        float eta = SaturateFloat(n1 / n2);

        // cos(θ_1).
        float cosT1 = dot(V, Nm);

        // cos^2(θ_2).
        float cos2T2 = 1.0f - Pow2(eta) * (1.0f - Pow2(cosT1));

        // 反射ベクトル.
        float3 reflection = normalize(reflect(-V, Nm));

        // 全反射チェック.
        if (cos2T2 <= 0.0f)
        {
            return material.BaseColor.rgb;
        }

        // 屈折ベクトル.
        float3 refraction = normalize(refract(-V, Nm, eta));

        float a = n2 - n1;
        float b = n2 + n1;
        float F0 = SaturateFloat(Pow2(a) / Pow2(b));

        // Schlickの近似によるフレネル項.
        float c = saturate((n1 > n2) ? dot(-Nm, refraction) : cosT1); // n1 > n2 なら cos(θ_2).
        float Fr = F_Schlick(F0, c);

        // 屈折光による放射輝度.
        float Tr = (1.0f - Fr) * Pow2(eta);

        float p = 0.25f + 0.5f * Fr;
        if (u < p)
        {
            return SaturateFloat(material.BaseColor.rgb * Fr / p);
        }
        else
        {
            return SaturateFloat(material.BaseColor.rgb * Tr / (1.0f - p));
        }
    }
    // 完全拡散反射.
    else if (IsPerfectDiffuse(material))
    {
        return material.BaseColor.rgb;
    }
    // 完全鏡面反射.
    else if (IsPerfectSpecular(material))
    {
        return material.BaseColor.rgb;
    }
    else
    {
        float3 diffuseColor  = ToKd(material.BaseColor.rgb, material.Metalness);
        float3 specularColor = ToKs(material.BaseColor.rgb, material.Metalness);

        float p = ProbabilityToSampleDiffuse(diffuseColor, specularColor);

        // Diffuse
        if (u < p)
        { return SaturateFloat((diffuseColor / p) * (1.0f.xxx - specularColor)); }

        float a = max(Pow2(material.Roughness), 0.01f);

        float3 H = normalize(V + L);

        float NdotL = abs(dot(Nm, L));
        float NdotH = abs(dot(Nm, H));
        float VdotH = abs(dot(V, H));
        float NdotV = abs(dot(Nm, V));
        
        float  G = G2_Smith(a, NdotL, NdotV) * (4.0f * NdotH * NdotV);
        float3 F = F_Schlick(specularColor, VdotH);

        return (F * G * NdotH / (NdotV * VdotH)) * (1.0f.xxx - diffuseColor) / (1.0f - p) ;
    }
}

//-----------------------------------------------------------------------------
//      マテリアルを評価します.
//-----------------------------------------------------------------------------
float3 EvaluateMaterial
(
    float3      V,          // 視線ベクトル.(-Vでレイの方向ベクトルになる).
    float3      T,          // 接線ベクトル.
    float3      B,          // 従接線ベクトル.
    float3      N,          // 法線ベクトル.
    float3      u,          // 乱数.
    float       ior,        // 屈折率. 
    Material    material,   // マテリアル.
    out float3  dir,        // 出射方向.
    out float   pdf         // 確率密度.
)
{
    // 物体からのレイの入出を考慮した法線.
    bool reverse = dot(N, V) < 0.0f;
    float3 Nm = (reverse) ? -N : N;
    float3 Tm = (reverse) ? -T : T;
    float3 Bm = (reverse) ? -B : B;
    
    // 屈折半透明.
    if (IsDielectric(material))
    {
        // レイがオブジェクトから出射するか入射するか?
        bool into = dot(N, Nm) > 0.0f;
    
        // Snellの法則.
        float n1 = (into) ? ior : material.Ior;
        float n2 = (into) ? material.Ior : ior;

        // 相対屈折率.
        float eta = SaturateFloat(n1 / n2);

        // cos(θ_1).
        float cosT1 = dot(V, Nm);

        // cos^2(θ_2).
        float cos2T2 = 1.0f - Pow2(eta) * (1.0f - Pow2(cosT1));

        // 反射ベクトル.
        float3 reflection = normalize(reflect(-V, Nm));

        // 全反射チェック.
        if (cos2T2 <= 0.0f)
        {
            dir = reflection;
            pdf = 1.0f;
            return material.BaseColor.rgb;
        }

        // 屈折ベクトル.
        float3 refraction = normalize(refract(-V, Nm, eta));

        float a = n2 - n1;
        float b = n2 + n1;
        float F0 = SaturateFloat(Pow2(a) / Pow2(b));

        // Schlickの近似によるフレネル項.
        float c  = saturate((n1 > n2) ? dot(-Nm, refraction) : cosT1); // n1 > n2 なら cos(θ_2).
        float Fr = F_Schlick(F0, c);

        // 屈折光による放射輝度.
        float Tr = (1.0f - Fr) * Pow2(eta);

        float p = 0.25f + 0.5f * Fr;
        if (u.z < p)
        {
            dir = reflection;
            pdf = p;
            return SaturateFloat(material.BaseColor.rgb * Fr);
        }
        else
        {
            dir = refraction;
            pdf = (1.0f - p);
            return SaturateFloat(material.BaseColor.rgb * Tr);
        }
    }
    // 完全拡散反射.
    else if (IsPerfectDiffuse(material))
    {
        float3 s = SampleLambert(u.xy);
        float3 L = normalize(Tm * s.x + Bm * s.y + Nm * s.z);

        float NoL = dot(Nm, L);

        dir = L;
        pdf = NoL / F_PI;

        return (material.BaseColor.rgb / F_PI) * NoL;
    }
    // 完全鏡面反射.
    else if (IsPerfectSpecular(material))
    {
        float3 L = normalize(reflect(-V, Nm));
        dir = L;
        pdf = 1.0f;

        return material.BaseColor.rgb;
    }
    else
    {
        float3 diffuseColor  = ToKd(material.BaseColor.rgb, material.Metalness);
        float3 specularColor = ToKs(material.BaseColor.rgb, material.Metalness);

        float p = ProbabilityToSampleDiffuse(diffuseColor, specularColor);

        float3 diffuseScale  = (1.0f.xxx - specularColor);
        float3 specularScale = (1.0f.xxx - diffuseColor);

        // Diffuse
        if (u.z < p)
        {
            float3 s = SampleLambert(u.xy);
            float3 L = normalize(Tm * s.x + Bm * s.y + Nm * s.z);

            float NoL = dot(Nm, L);

            dir = L;
            pdf = NoL / F_PI;
            pdf *= p;

            return (diffuseColor / F_PI) * NoL * diffuseScale;
        }

        float a = max(Pow2(material.Roughness), 0.01f);

        float3 s = SampleGGX(u.xy, a);
        float3 H = normalize(Tm * s.x + Bm * s.y + Nm * s.z);
        float3 L = normalize(reflect(-V, H));

        float NdotL = abs(dot(Nm, L));
        float NdotH = abs(dot(Nm, H));
        float VdotH = abs(dot(V, H));
        float NdotV = abs(dot(Nm, V));

        float  D = D_GGX(NdotH, a);
        float  G = G2_Smith(a, NdotL, NdotV);
        float3 F = F_Schlick(specularColor, VdotH);
        float3 brdf = (D * F * G) * NdotL;

        pdf = D * NdotH / (4.0f * VdotH);
        pdf *= (1.0f - p);

        dir = L;

        return SaturateFloat(brdf) * specularScale;
    }
}

float Load(Texture2D T, int x, int y, int mip)
{
    float3 color = T.Load(int3(x, y, mip)).rgb;
    return Luminance(color);
}

float2 SampleMipMap(Texture2D T, float2 u, out float weight) // weight = 1.0f / lightPDF.
{
    uint width, height, mipLevels;
    T.GetDimensions(0, width, height, mipLevels);

    // [Shirley 2019], Peter Shirley, Samuli Laine, Dvaid Hart, Matt Pharr,
    // Petrik Clarberg, Eric Haines, Matthias Raab, David Cline,
    // "Sampling Transformations Zoo", Ray Tracing Gems Ⅰ, pp.223-246, 2019.

    // Iterate over mipmaps of size 2x2 ... NxN.
    // Load(x, y, mip) loads a texel (mip 0 is the largest power of two).
    int x = 0;
    int y = 0;
    for (int mip=(int)mipLevels - 1; mip >=0; --mip)
    {
        x <<= 1;
        y <<= 1;
        float lhs = Load(T, x+0, y+0, mip) + Load(T, x+0, y+1, mip);
        float rhs = Load(T, x+1, y+0, mip) + Load(T, x+1, y+1, mip);
        float probLhs = lhs / (lhs + rhs);
        if (u.x < probLhs)
        {
            u.x /= probLhs;
            float probLower = Load(T, x, y, mip) / lhs;
            if (u.y < probLower)
            {
                u.y /= probLower;
            }
            else
            {
                y++;
                u.y = (u.y - probLower) / (1.0f - probLower);
            }
        }
        else
        {
            x++;
            u.x = (u.x - probLhs) / (1.0f - probLhs);
            float probLower = Load(T, x, y, mip) / rhs;
            if (u.y < probLower)
            {
                u.y /= probLower;
            }
            else
            {
                y++;
                u.y = (u.y - probLower) / (1.0f - probLower);
            }
        }
    }

    // We have found a texel (x, y) with probability  proportional to 
    // its normalized value. Compute the PDF and return the coordinates.
    float pdf = Load(T, x, y, 0) / Load(T, 0, 0, mipLevels - 1);
    weight = SaturateFloat(1.0f / pdf);
    return float2((float)x / (float)width, (float)y / (float)height);
}

#endif//COMMON_HLSLI
