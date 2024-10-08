﻿//------------------------------------------------- ----------------------------
// File : RTPM_GeneratePhoton.hlsl
// Desc : Photon Generation Pass.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include "RTPM_Common.hlsli"


//-----------------------------------------------------------------------------
// Constant Values.
//-----------------------------------------------------------------------------
static const uint kInfoTexHeight = 512;


///////////////////////////////////////////////////////////////////////////////
// Payload structure
///////////////////////////////////////////////////////////////////////////////
struct Payload
{
    float3  Throughput;
    uint    EncodedFaceNormal;
    float3  Origin;
    bool    Terminated;
    float3  Direction;
    bool    DiffuseHit;
    uint4   Seed;
    uint    Ior;
    
    void Init()
    {
        Throughput          = 1.0f.xxx;
        EncodedFaceNormal   = 0;
        Origin              = 0.0f.xxx;
        Terminated          = false;
        Direction           = 0.0f.xxx;
        DiffuseHit          = false;
        Seed                = 0.xxxx;
        Ior                 = 1.0f;
    }
};

///////////////////////////////////////////////////////////////////////////////
// PassParameter structure
///////////////////////////////////////////////////////////////////////////////
struct PassParameter
{
    float   GlobalRadius;
    float   CausticRadius;
    float   HashScaleFactor;
    float   GlobalRejection;
    float   AnalyticInvPdf;
    uint    CullingHashSize;
    uint    CullingExtentY;
    float   CullingProjTest;
    uint    MaxPhotonIndexCaustic;
    uint    MaxPhotonIndexGlobal;
};

///////////////////////////////////////////////////////////////////////////////
// PhotonInfo structure
///////////////////////////////////////////////////////////////////////////////
struct PhotonInfo
{
    float3  Flux;       // 光束.
    float   FnTheta;    // 面法線 (θ角).
    float3  Dir;        // 照射方向ベクトル.
    float   FnPhi;      // 面法線 (φ角).
};

//=============================================================================
//  Resources.
//=============================================================================
ConstantBuffer<PassParameter>   PassParam           : register(b1);
RayTracingAS                    SceneAS             : register(t3);
Texture2D<uint>                 CullingHashBuffer   : register(t4);
Texture2D<int>                  LightSampleMap      : register(t5);
RWTexture2D<float4>             PhotonFluxMap[2]    : register(u0);
RWTexture2D<float4>             PhotonDirMap[2]     : register(u2);
RWStructuredBuffer<AABB>        PhotonAABBBuffer[2] : register(u4);
RWStructuredBuffer<uint>        PhotonCounterBuffer : register(u6); // 0:caustic, 1:global.


//-----------------------------------------------------------------------------
//      カリングを行います.
//-----------------------------------------------------------------------------
bool Culling(float3 origin)
{
    float4 projPos = mul(float4(origin, 1.0f), SceneParam.ViewProj);
    projPos /= projPos.w;

    // カメラ錐台の内部にある場合はカリングしない.
    if (!any(abs(projPos.xy) > PassParam.CullingProjTest) && projPos.z <= 1.0f && projPos.z >= 0.0f)
        return false;
    
    int3 cell = int3(floor(origin * PassParam.HashScaleFactor));
    uint hash = WangHash(cell) & (PassParam.CullingHashSize - 1);
    uint2 hashId = uint2(hash & PassParam.CullingExtentY, hash / PassParam.CullingExtentY);

    return (CullingHashBuffer[hashId] == 0);
}

//-----------------------------------------------------------------------------
//      非交差時の処理です.
//-----------------------------------------------------------------------------
[shader("miss")]
void OnMiss(inout Payload payload)
{
    payload.Terminated = true;
}

//-----------------------------------------------------------------------------
//      ヒット時の処理です.
//-----------------------------------------------------------------------------
[shader("closesthit")]
void OnClosestHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attribs)
{
    uint instanceId  = InstanceID();
    uint primitiveId = PrimitiveIndex();
    
    // 頂点データを取得.
    SurfaceHit vertex = GetSurfaceHit(instanceId, primitiveId, attribs.barycentrics);

    // マテリアルを取得.
    Material material = GetMaterial(instanceId, vertex.TexCoord, 0.0f);

    // 接線空間を求める.
    float3 B = normalize(cross(vertex.Tangent, vertex.Normal));
    float3 N = FromTangentSpaceToWorld(material.Normal, vertex.Tangent, B, vertex.Normal);
    float3 T = RecalcTangent(N, vertex.Tangent);
    B = normalize(cross(T, N));

    // 視線ベクトル.
    float3 V = payload.Direction;

    // 幾何法線.
    float3 Ng = vertex.GeometryNormal;

    // 物体からのレイの入出を考慮した法線.
    Ng = (dot(Ng, V) <= 0.0f) ? Ng : -Ng;

    // 乱数生成.
    float3 u = float3(Random(payload.Seed), Random(payload.Seed), Random(payload.Seed));
 
    // BSDFをサンプル.
    float3 dir;
    float  pdf;
    float3 bsdf = EvaluateMaterial(V, T, B, N, u, payload.Ior, material, dir, pdf);

    if (IsDielectric(material))
    {
        // 屈折率更新.
        payload.Ior = material.Ior;
 
        // 屈折側になるので，オフセット計算に使用する法線は逆転する.
        Ng = -Ng;
    }
 
    // レイデータを更新.
    payload.Origin      = OffsetRay(vertex.Position, Ng);
    payload.Throughput *= SaturateFloat(bsdf / pdf);
    payload.Direction   = dir;
    payload.DiffuseHit  = IsPerfectDiffuse(material);
    
    // スループットが0なら終了.
    if (any(payload.Throughput <= 0.0f))
        payload.Terminated = true;
}

//-----------------------------------------------------------------------------
//      レイ生成時の処理です.
//-----------------------------------------------------------------------------
[shader("raygeneration")]
void OnRayGeneration()
{
    uint2 launchId  = DispatchRaysIndex().xy;
    uint2 launchDim = DispatchRaysDimensions().xy;

    // 乱数のシードを取得.
    uint4 seed = SetSeed(launchId, SceneParam.FrameIndex);

    // Payloadを準備.
    Payload payload;
    payload.Init();
    payload.Seed = seed;

    // ライト番号とタイプを取得.
    int lightId = LightSampleMap[launchId];
    if (lightId == 0)
        return;
    
    // ライト設定.
    {
        
    }

    // ライトの方向ベクトルを求める.
    float lightDirPdf = 0.0f;
    float3 lightRandom = float3(Random(payload.Seed), Random(payload.Seed), Random(payload.Seed));
    // spotAngle
    //CalcLightDirection(lightDirPdf, lightRandom, ray.Direction, lightDirPdf, type);

    // フォトンを生成.
    PhotoInfo photon;
    photon.Dir     = 0.0f.xxx;
    photon.Flux    = 0.0f.xxx;
    photon.FnTheta = 1.0f;
    photon.FnPhi   = 1.0f;
    float3 photonPos = 0.0f.xxx;

    // ライトの光束.
    float3 lightFlux = lightIntensity * invPdf;
    if (analytic)
        lightFlux /= float(launchDim.x * launchDim.y) * lightDirPdf;
    else
        lightFlux *= lightArea * F_PI; // Lambertian Emitter.

    // レイの設定.
    RayDesc ray;
    ray.Origin      = lightPos + 0.01f * lightDir;
    ray.Direction   = lightDirPdf;
    ray.TMin        = 0.01f;
    ray.TMax        = kRayTMax;

    uint rayFlags          = RAY_FLAG_NONE;
    bool reflectedSpecular = false;
    bool reflectedDiffuse  = false;

    for(uint i=0; i<SceneParam.MaxBounce && !payload.Terminated; ++i)
    {
        TraceRay(SceneAS, rayFlags, 0xff, 0, 0, 0, ray, payload);
        if (payload.Terminated)
            break;

        photonPos        = payload.Origin;
        photon.Dir       = payload.Direction;
        reflectedDiffuse = payload.DiffuseHit;

        // 棄却処理.
        float rnd      = Random(payload.Seed);
        bool  roulette = (rnd <= PassParam.GlobalRejection);

        // フォトンを格納.
        if (reflectedDiffuse && (roulette || reflectedSpecular))
        {
            // フォトンカリングされなければ，フォトンマップに格納.
            if (!Culling(photonPos))
            {
                uint  photonIndex = 0;
                uint  insertIndex = reflectedSpecular ? 0 : 1; // 0:caustic, 1:global.

                InterlockedAdd(PhotonCounterBuffer[insertIndex], 1u, photonIndex);
                photonIndex = min(photonIndex, reflectedSpecular ? PassParam.MaxPhotonIndexCaustic : PassParam.MaxPhotonIndexGlobal);

                // エンコード済み法線角度をfloatに変換.
                {
                    uint encTheta  = (payload.EncodedFaceNormal >> 16) & 0xFFFF;
                    uint encPhi    = payload.EncodedFaceNormal & 0xFFFF;
                    photon.FnTheta = f16tof32(encTheta);
                    photon.FnPhi   = f16tof32(encPhi);
                }

                photon.Flux = reflectedSpecular ? photon.Flux : photon.Flux / PassParam.GlobalRejection;

                uint2 mapIndex = uint2(photonIndex / kInfoTexHeight, photonIndex & kInfoTexHeight);
                PhotonFluxMap[insertIndex][mapIndex] = float4(photon.Flux, photon.FnTheta);
                PhotonDirMap [insertIndex][mapIndex] = float4(photon.Dir,  photon.FnPhi);

                float radius     = reflectedSpecular ? PassParam.CausticRadius : PassParam.GlobalRadius;
                AABB  photonAABB = CalcPhotonAABB(photonPos, radius);
                PhotonAABBBuffer[insertIndex][photonIndex] = photonAABB;
            }
        }

        // ロシアンルーレット
        const float threshold = Luminance(payload.Throughput);
        const float prob      = max(0.1f, 1.0f - threshold);
        rnd = Random(payload.Seed);
        if (rnd < prob)
        { break; }

        // 確率密度で割る.
        payload.Throughput /= (1.0f - prob);

        // ディフューズヒットでなければスペキュラー扱い.
        reflectedSpecular = !reflectedDiffuse;

        // 次のイテレーションのために，レイを更新.
        ray.Origin    = payload.Origin;
        ray.Direction = payload.Direction;
    }
}
