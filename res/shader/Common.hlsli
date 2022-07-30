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
#define USE_GGX             (1)

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
    float2 Barycentrics;
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
    float3  Normal;
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
    param.Roughness = orm.y;
    param.Metalness = orm.z;
    param.Emissive  = e;

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

// [Heitz 2018] Eric Heitz, "Sampling the GGX Distribution of Visible Normals", JCGT, vol.7, no.4, 1-13, 2018.
float3 SampleGGXVNDF(float3 Ve, float alpha_x, float alpha_y, float2 u)
{
    // Input Ve: view direction
    // Input alpha_x, alpha_y: roughness parameters
    // Input u: uniform random numbers
    // Output Ne: normal sampled with PDF D_Ve(Ne) = G1(Ve) * max(0, dot(Ve, Ne)) * D(Ne) / Ve.z

    // Section 3.2: transforming the view direction to the hemisphere configuration
    float3 Vh = normalize(float3(alpha_x * Ve.x, alpha_y * Ve.y, Ve.z));

    // Section 4.1: orthonormal basis (with special case if cross product is zero)
    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1 = (lensq > 0.0f) ? float3(-Vh.y, Vh.x, 0.0f) * rsqrt(lensq) : float3(1.0f, 0.0f, 0.0f);
    float3 T2 = cross(Vh, T1);

    // Section 4.2: parameterization of the projected area
    float r = sqrt(u.x);
    float phi = 2.0f * F_PI * u.y;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5f * (1.0f + Vh.z);
    t2 = (1.0f - s) * sqrt(1.0f - t1*t1) + s*t2;

    // Section 4.3: reprojection onto hemisphere
    float3 Nh = t1*T1 + t2*T2 + sqrt(max(0.0f, 1.0f - t1*t1 - t2*t2))*Vh;

    // Section 3.4: transforming the normal back to the ellipsoid configuration
    float3 Ne = normalize(float3(alpha_x * Nh.x, alpha_y * Nh.y, max(0.0f, Nh.z)));

    return Ne;
}


//-----------------------------------------------------------------------------
//      出射方向をサンプリングします.
//-----------------------------------------------------------------------------
float3 SampleDir(float3 V, float3 N, float3 u, Material material)
{
    // 完全拡散反射.
    if (IsPerfectDiffuse(material))
    {
        float3 T, B;
        CalcONB(N, T, B);

        float3 s = SampleLambert(u.xy);
        return normalize(T * s.x + B * s.y + N * s.z);
    }
    // 完全鏡面反射.
    else if (IsPerfectSpecular(material))
    {
        return normalize(-reflect(V, N));
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

        float a = max(Pow2(material.Roughness), 0.01f);

#if USE_GGX
        float3 T, B;
        CalcONB(N, T, B);

        float3 s = SampleGGX(u.xy, a);
        float3 H = normalize(T * s.x + B * s.y + N * s.z);
        return normalize(reflect(-V, H));
#else
        // Phong. 
        float3 R = normalize(reflect(-V, N));
    
        float3 T, B;
        CalcONB(R, T, B);

        float shininess = ToSpecularPower(a);
        float3 s = SamplePhong(u.xy, shininess);
        return normalize(T * s.x + B * s.y + R * s.z);
#endif
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
    out float   pdf         // 確率密度.
)
{
    // 完全拡散反射.
    if (IsPerfectDiffuse(material))
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
    else if (IsPerfectSpecular(material))
    {
        float3 L = normalize(reflect(-V, N));
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
        if (u.z <= p)
        {
            float3 T, B;
            CalcONB(N, T, B);

            float3 s = SampleLambert(u.xy);
            float3 L = normalize(T * s.x + B * s.y + N * s.z);

            float NoL = abs(dot(N, L));

            dir = L;
            pdf = (NoL / F_PI);
            pdf *= p;

            return (diffuseColor / F_PI) * NoL;
        }

        float a = max(Pow2(material.Roughness), 0.01f);

#if USE_GGX
        float3 T, B;
        CalcONB(N, T, B);

        float3 s = SampleGGX(u.xy, a);
        float3 H = normalize(T * s.x + B * s.y + N * s.z);
        float3 L = normalize(reflect(-V, H));

        float NdotL = abs(dot(N, L));
        float NdotH = abs(dot(N, H));
        float VdotH = abs(dot(V, H));
        float NdotV = abs(dot(N, V));

        float  D = D_GGX(NdotH, a);
        float  G = G_SmithGGX(NdotL, NdotV, a);
        float3 F = F_Schlick(specularColor, VdotH);
        float3 ggxTerm = D * F * G / (4 * NdotV); // (D * G * F / (4 * NdotL * NdotV)) * NdotL ---> Cancel out NdotL.
        float  ggxProb = D * NdotH / (4 * VdotH);

        dir = L;
        pdf = ggxProb;
        pdf *= (1.0f - p);

        return ggxTerm;
#else
        // Phong. 
        float3 R = normalize(reflect(-V, N));
    
        float3 T, B;
        CalcONB(R, T, B);

        float shininess = ToSpecularPower(a);
        float3 s = SamplePhong(u.xy, shininess);
        float3 L = normalize(T * s.x + B * s.y + R * s.z);

        float LoR = abs(dot(L, R));
        float normalizeTerm = (shininess + 1.0f) / (2.0f * F_PI);
        float phongTerm = pow(LoR, shininess) * normalizeTerm;

        dir = L;
        pdf = phongTerm;
        pdf *= (1.0f - p);

        return specularColor * phongTerm;
#endif
    }
}

float Load(Texture2D T, int x, int y, int mip)
{
    float3 color = T.Load(int3(x, y, mip)).rgb;
    return Luminance(color);
}

float2 SampleMipMap(Texture2D T, float2 u, out float pdf)
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
    pdf = Load(T, x, y, 0) / Load(T, 0, 0, mipLevels);
    return float2((float)x / (float)width, (float)y / (float)height);
}

#endif//COMMON_HLSLI
