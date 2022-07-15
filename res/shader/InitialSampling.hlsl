//-----------------------------------------------------------------------------
// File : InitialSampling.hlsl
// Desc : Initial Sampling.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <Reservoir.hlsli>
#include <Common.hlsli>


//-----------------------------------------------------------------------------
// Resources
//-----------------------------------------------------------------------------
RWStructuredBuffer<Sample>  InitialSampleBuffer : register(u0);
Texture2D<uint4>            VBuffer             : register(t4);


//-----------------------------------------------------------------------------
//      レイ生成シェーダです.
//-----------------------------------------------------------------------------
[shader("raygeneration")]
void OnGenerateRay()
{
    // バッファ番号を計算.
    uint2 dispatchId = DispatchRaysIndex().xy;
    uint  index = dispatchId.x + dispatchId.y * DispatchRaysDimensions().x;

    // 乱数初期化.
    uint4 seed = SetSeed(DispatchRaysIndex().xy, SceneParam.FrameIndex);

    uint4  visibleData   = VBuffer.Load(int3(dispatchId, 0));
    uint   instanceId    = visibleData.x;
    uint   triangleId    = visibleData.y;
    float2 barycentrices = asfloat(visibleData.zw);

    float3 throughput = float3(1.0f, 1.0f, 1.0f);

    // 可視点の頂点データ取得.
    Vertex visiblePoint = GetVertex(instanceId, triangleId, barycentrices);

    // マテリアル取得.
    Material material = GetMaterial(instanceId, visiblePoint.TexCoord, 0.0f);

    float4 cameraPos = float4(0.0f,  0.0f, 0.0f, 1.0f);
    cameraPos = mul(SceneParam.InvView, cameraPos);

    // 視線ベクトル.
    float3 V = normalize(visiblePoint.Position - cameraPos.xyz);

    // 出射方向をBSDFに基づきランダムにサンプリング.
    float3 u = float3(Random(seed), Random(seed), Random(seed));

    float3 dir;
    float  pdf_v;
    float3 Lv = throughput * material.Emissive;

    // NEE
    {
    }

    throughput *= EvaluateMaterial(V, visiblePoint.Normal, u, material, dir, pdf_v);

    // レイを設定.
    RayDesc ray;
    ray.Origin      = visiblePoint.Position.xyz;
    ray.Direction   = dir;
    ray.TMin        = 0.0f;
    ray.TMax        = FLT_MAX;

    // ペイロード初期化.
    Payload payload = (Payload)0;

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

    float3 Ls          = float3(0.0f, 0.0f, 0.0f);
    float  pdf_s       = 1.0f;
    Vertex samplePoint = (Vertex)0;

    if (payload.HasHit())
    {
        // 頂点データ取得.
        samplePoint = GetVertex(payload.InstanceId, payload.PrimitiveId, payload.Barycentrics);

        // マテリアル取得.
        Material material = GetMaterial(payload.InstanceId, samplePoint.TexCoord, 0.0f);

        Ls = throughput * material.Emissive;

        // NEE
        {
        }

        u = float3(Random(seed), Random(seed), Random(seed));

        // 交差位置での出射放射輝度を推定.
        throughput *= EvaluateMaterial(-ray.Direction, samplePoint.Normal, u, material, dir, pdf_s);
    }

    // 初期サンプルを設定.
    Sample z;
    z.P_v     = visiblePoint.Position;
    z.N_v     = visiblePoint.Normal;
    z.L_v     = Lv;
    z.Pdf_v   = pdf_v;
    z.P_s     = samplePoint.Position;
    z.N_s     = samplePoint.Normal;
    z.L_s     = Ls;
    z.Pdf_s   = pdf_s;
    z.Random  = u;

    InitialSampleBuffer[index] = z;
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
{ payload.InstanceId = INVALID_ID; }
