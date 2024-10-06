//-----------------------------------------------------------------------------
// File : RTPM_StochasticCollectPhoton.hlsl
// Desc : Stochastic Photon Collection Pass.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include "RTPM_Common.hlsli"


//=============================================================================
// Constant Values.
//=============================================================================
static const float kCollectTMin     = 0.000001f;
static const float kCollectTMax     = 0.000002f;
static const float kMinCosTheta     = 1e-6f;
static const uint  kInfoTexHeight   = 512;
static const uint  kPhotonListSize  = 3;


///////////////////////////////////////////////////////////////////////////////
// Payload structure
///////////////////////////////////////////////////////////////////////////////
struct Payload
{
    uint  Counter;
    uint3 PhotonList;
    uint4 Seed;
};

///////////////////////////////////////////////////////////////////////////////
// SphereAttribute structure
///////////////////////////////////////////////////////////////////////////////
struct SphereAttribute
{
    float2 Reserved;
};

///////////////////////////////////////////////////////////////////////////////
// PassParameter structure
///////////////////////////////////////////////////////////////////////////////
struct PassParameter
{
    float CausticRadius;
    float GlobalRadius;
    bool  CollectCausticPhotons;
    bool  CollectGlobalPhotons;
};


//=============================================================================
// Resources
//=============================================================================
ConstantBuffer<PassParameter>   PassParam       : register(b1);
RayTracingAS                    PhotonAS        : register(t3);
Texture2D<uint4>                VBuffer         : register(t4);
Texture2D<float4>               WorldViewDir    : register(t5);
Texture2D<float4>               ThroughputMap   : register(t6);
Texture2D<float4>               EmissiveMap     : register(t7);
Texture2D<float4>               PhotonFlux[2]   : register(t8);
Texture2D<float4>               PhotonDir[2]    : register(t10);
StructuredBuffer<AABB>          PhotonAABB[2]   : register(t12);
RWTexture2D<float4>             PhotonImage     : register(u0);

//-----------------------------------------------------------------------------
//      球との交差判定を行います.
//-----------------------------------------------------------------------------
bool TestSphere(float3 center, float radius, float3 p)
{
    float3 v = p - center;
    return (dot(v, v) < radius * radius);
}

//-----------------------------------------------------------------------------
//      フォトンの寄与を求めます.
//-----------------------------------------------------------------------------
float3 PhotonContribution(inout Payload payload, float3 bsdf, float3 N, bool isCaustic)
{
    const uint maxIndex = min(payload.Counter, kPhotonListSize);

    float3 radiance = 0.0f.xxx;

    if (maxIndex == 0)
        return radiance;

    for(uint i=0; i<maxIndex; ++i)
    {
        uint  photonId      = payload.PhotonList[i];
        uint2 fetchIndex    = uint2(photonId / kInfoTexHeight, photonId % kInfoTexHeight);
        uint  instanceIndex = isCaustic ? 0 : 1;

        float3 photonFlux = PhotonFlux[instanceIndex][fetchIndex].xyz;
        float3 photonDir  = PhotonDir [instanceIndex][fetchIndex].xyz;

        float NoL = dot(N, -photonDir);
        if (NoL > kMinCosTheta)
        { radiance += bsdf * photonFlux; }
    }

    return radiance * (float(payload.Counter) / float(maxIndex));
}

//-----------------------------------------------------------------------------
//      任意物体との衝突時の処理です.
//-----------------------------------------------------------------------------
[shader("anyhit")]
void OnAnyHit(inout Payload payload, SphereAttribute attrib)
{
    uint index = payload.Counter;
    if (index > kPhotonListSize)
    { index = uint(Random(payload.Seed) * payload.Counter); }

    if (index < kPhotonListSize)
    { payload.PhotonList[index] = PrimitiveIndex(); }

    IgnoreHit();
}

//-----------------------------------------------------------------------------
//      交差判定処理です.
//-----------------------------------------------------------------------------
[shader("intersection")]
void OnIntersection()
{
    const float3 origin         = ObjectRayOrigin();
    const uint   instanceIndex  = InstanceIndex ();
    const uint   primitiveIndex = PrimitiveIndex();
    
    {
        const uint2  index    = uint2(primitiveIndex / kInfoTexHeight, primitiveIndex % kInfoTexHeight);
        const float  theta    = PhotonFlux[instanceIndex][index].w;
        const float  phi      = PhotonDir[instanceIndex][index].w;
        const float  sinTheta = sin(theta);
        const float3 faceN    = float3(cos(phi) * sinTheta, cos(theta), sin(phi) * sinTheta);

        if (dot(WorldRayDirection(), faceN) < 0.9f)
            return;
    }
    
    AABB photonAABB = PhotonAABB[instanceIndex][primitiveIndex];
    float radius = (instanceIndex == 0) ? PassParam.CausticRadius : PassParam.GlobalRadius;
    
    bool isHit = TestSphere(photonAABB.Center(), radius, origin);

    SphereAttribute attrib;
    attrib.Reserved = 0.0f.xx;

    if (isHit)
    { ReportHit(RayTCurrent(), 0, attrib); }
}

//-----------------------------------------------------------------------------
//      レイ生成時の処理です.
//-----------------------------------------------------------------------------
[shader("raygeneration")]
void OnRayGeneration()
{
    const uint2 launchId  = DispatchRaysIndex().xy;
    const uint2 launchDim = DispatchRaysDimensions().xy;

    float3 V = -WorldViewDir[launchId].xyz;

    // Payloadを準備.
    Payload payload;
    payload.Counter     = 0;
    payload.Seed        = SetSeed(launchId, SceneParam.FrameIndex);
    payload.PhotonList  = int3(0, 0, 0);

    const HitInfo hit = UnpackHitInfo(VBuffer[launchId]);
    bool valid = hit.IsValid();
    
    // 頂点データ取得.
    SurfaceHit vertex = GetSurfaceHit(hit.InstanceId, hit.PrimitiveIndex, hit.BaryCentrics);

    // マテリアルデータ取得.
    Material material = GetMaterial(hit.InstanceId, vertex.TexCoord, 0);
    
    // 接線空間を求める.
    float3 B = normalize(cross(vertex.Tangent, vertex.Normal));
    float3 N = FromTangentSpaceToWorld(material.Normal, vertex.Tangent, B, vertex.Normal);
    float3 T = RecalcTangent(N, vertex.Tangent);
    B = normalize(cross(T, N));

    // 幾何法線.
    float3 gN = vertex.GeometryNormal;

    // 法線が潜る場合は反転させる.
    if (dot(gN, V) < 0.0f)
    { gN = -gN; }
 
    // 乱数生成.
    float3 u = float3(Random(payload.Seed), Random(payload.Seed), Random(payload.Seed));

    // BSDFを取得.
    float3 dir;
    float  pdf;
    float3 bsdf = EvaluateMaterial(V, T, B, N, u, 1.0f, material, dir, pdf);
    bsdf /= pdf;

    // レイを設定.
    RayDesc ray;
    ray.Origin      = vertex.Position;
    ray.TMin        = kCollectTMin;
    ray.TMax        = kCollectTMax;
    ray.Direction   = vertex.GeometryNormal;

    // レイフラグ設定.
    uint rayFlags = RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_SKIP_TRIANGLES;

    // 放射輝度.
    float3 radiance = 0.0f.xxx;

    // コースティックフォトンを収集.
    if (PassParam.CollectCausticPhotons && valid)
    {
        // 検索の為にトレース.
        TraceRay(PhotonAS, rayFlags, 1, 0, 0, 0, ray, payload);

        float3 radiancePhotons = PhotonContribution(payload, bsdf, gN, true);
        float  w = 1.0f / (F_PI * PassParam.CausticRadius * PassParam.CausticRadius);
        radiance += w * radiancePhotons;

        // カウンターリセット.
        payload.Counter = 0;
    }

    // グローバルフォトンを収集.
    if (PassParam.CollectGlobalPhotons && valid)
    {
        // 検索の為にトレース.
        TraceRay(PhotonAS, rayFlags, 2, 0, 0, 0, ray, payload);

        float3 radiancePhotons = PhotonContribution(payload, bsdf, gN, false);
        float  w = 1.0f / (F_PI * PassParam.GlobalRadius * PassParam.GlobalRadius);
        radiance += w * radiancePhotons;

        // カウンターリセット.
        payload.Counter = 0;
    }

    float3 throughput = ThroughputMap[launchId].rgb;
    radiance *= throughput;

    // Emissiveを加算.
    radiance += (EmissiveMap[launchId].rgb * throughput);

    // アキュムレーション.
    if (SceneParam.AccumulatedFrames > 0)
    {
        float3 last = PhotonImage[launchId].xyz;
        float frameCount = float(SceneParam.AccumulatedFrames);
        last *= frameCount;
        radiance += last;
        radiance /= (frameCount + 1.0f);
    }

    // ライティング結果を格納.
    PhotonImage[launchId] = float4(radiance, 1.0f);
}