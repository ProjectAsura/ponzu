//------------------------------------------------- ----------------------------
// File : RTPM_GeneratePhoton.hlsl
// Desc : Photon Generation Pass.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include "RTPM_Common.hlsli"


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
    
    void Init()
    {
        Throughput          = 1.0f.xxx;
        EncodedFaceNormal   = 0;
        Origin              = 0.0f.xxx;
        Terminated          = false;
        Direction           = 0.0f.xxx;
        DiffuseHit          = false;
        Seed                = 0.xxxx;
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
    float   GlobaRejection;
    float   AnalyticInvPdf;
    uint    CullingHashSize;
    uint    CullingExtentY;
    float   CullingProjTest;
};

///////////////////////////////////////////////////////////////////////////////
// PhotonInfo structure
///////////////////////////////////////////////////////////////////////////////
struct PhotonInfo
{
    float3  Flux;
    float3  Dir;
    float3  FaceNormal;
};

//=============================================================================
//  Resources.
//=============================================================================
ConstantBuffer<PassParameter>   PassParam           : register(b1);
Texture2D<uint>                 CullingHashBuffer   : register(t3);
RWTexture2D<float4>             PhotonFluxMap       : register(u0);
RWTexture2D<float4>             PhotonDirMap        : register(u1);
RWStructuredBuffer<AABB>        PhotonAABBBuffer    : register(u2);
RWStructuredBuffer<uint>        PhotonCounterBuffer : register(u3);


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

    float3 gN = vertex.GeometryNormal;
    float3 V = -payload.Direction;

    // 法線が潜る場合は反転させる.
    if (dot(gN, V) < 0.0f)
    { gN = -gN; }

    // 乱数生成.
    float3 u = float3(Random(payload.Seed), Random(payload.Seed), Random(payload.Seed));
 
    // BSDFをサンプル.
    float3 dir;
    float pdf;
    float3 bsdf = EvaluateMaterial(V, T, B, N, u, 1.0f, material, dir, pdf);
 
    // レイデータを更新.
    payload.Origin      = OffsetRay(vertex.Position, gN);
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


    #if 0
    // ライト番号とタイプを取得.
    int lightId = LightSample[launchId];
    if (lightId == 0)
        return;
    
    // ライト設定.



    // 方向ベクトルを求める.
    RayDesc ray;
    float lightDirPdf = 0.0f;
    
    
    // フォトンを生成.
    float3 photonPos = 0.0f.xxx;
    PhotoInfo photon;
    photon.Dir = 0.0f.xxx;
    photon.Flux = 0.0f.xxx;
    
    // ライトの光束.
    float3 lightFlux = lightIntensity * invPdf;
    if (analytic)
        lightFlux /= float(launchDim.x * launchDim.y) * lightDirPdf;
    else
        lightFlux *= lightArea * F_PI; // Lambertian Emitter.
    
    ray.Origin = lightPos + 0.01f * ray.Direction;
    ray.TMin   = 0.01f;
    ray.TMax   = kRayTMax;
  
    uint rayFlags = 0;
    
    for(uint i=0; i<MaxDepth && !rayPayload.Terminate; ++i)
    {
        TraceRay(ScenAS, rayFlags, 0xff, 0, rayTypeCount, 0, ray, rayPayload);
        if (rayPayload.Terminate)
            break;
        
        // 棄却処理.
        
        // フォトンを格納.
        {
            // フォトンカリング.
            if (!Culling(photonPos))
            {
                
                // フォトンマップに格納.
            }
            
        }
        
        // ロシアンルーレット
        
        
        // 次のイテレーションのために，レイを更新.
    }
#endif
}
