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
RWStructuredBuffer<Reservoir>   TemporalReservoirBuffer : register(u0);
Texture2D<float4>               BackGround              : register(t4);


//-----------------------------------------------------------------------------
//      IBLをサンプルします.
//-----------------------------------------------------------------------------
float3 SampleIBL(float3 dir)
{
    float2 uv = ToSphereMapCoord(dir);
    return BackGround.SampleLevel(LinearWrap, uv, 0.0f).rgb * SceneParam.SkyIntensity;
}

//-----------------------------------------------------------------------------
//      レイ生成シェーダです.
//-----------------------------------------------------------------------------
[shader("raygeneration")]
void OnGenerateRay()
{
    // [Ouyang 2021] Y.Ouyang, Si.Liu, M.Kettunen, M.Pharr, J.Pataleoni,
    // "ReSTIR GI: Path Resampling for Real-Time Path Tracing", HPG 2021.
    // Algorithm 2 と Algorithm 3 を実行.

    // 乱数初期化.
    uint4 seed = SetSeed(DispatchRaysIndex().xy, SceneParam.FrameIndex);

    // バッファ番号を計算.
    uint2 dispatchId = DispatchRaysIndex().xy;
    uint  index = dispatchId.x + dispatchId.y * DispatchRaysDimensions().x;

    float2 pixel = float2(DispatchRaysIndex().xy);
    const float2 resolution = float2(DispatchRaysDimensions().xy);

    // アンリエイリアシング.
    float2 offset = float2(Random(seed), Random(seed));
    pixel += lerp(-0.5f.xx, 0.5f.xx, offset);

    // テクスチャ座標算出.
    float2 uv = (pixel + 0.5f) / resolution;
    uv.y = 1.0f - uv.y;

    // [-1, 1]のピクセル座標.
    pixel = uv * 2.0f - 1.0f;

    // ペイロード初期化.
    Payload payload = (Payload)0;

    // レイを設定.
    RayDesc ray = GeneratePinholeCameraRay(pixel);

    float3 Lo         = float3(0.0f, 0.0f, 0.0f);
    float3 throughput = float3(1.0f, 1.0f, 1.0f);

    // カメラからレイをトレース.
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
        return;
    }

    //-------------------------------
    // 可視点を記録.
    //-------------------------------

    // 頂点データ取得.
    SurfaceHit visibleVertex = GetSurfaceHit(payload.InstanceId, payload.PrimitiveId, payload.Barycentrics);

    // マテリアル取得.
    Material material = GetMaterial(payload.InstanceId, visibleVertex.TexCoord, 0.0f);

    // 自己発光による放射輝度.
    Lo += throughput * material.Emissive;

    // Next Event Estimation.
    {
    }

    float3 V = -ray.Direction;
    float3 N = visibleVertex.GeometryNormal;

    float3 u = float3(Random(seed), Random(seed), Random(seed));
    float3 dir;
    float  pdf_v;

    // マテリアルを評価.
    float3 brdfWeight = EvaluateMaterial(V, N, u, material, dir, pdf_v);
    brdfWeight /= pdf_v;

    throughput *= brdfWeight;

    // レイを更新.
    ray.Origin    = OffsetRay(visibleVertex.Position, N);
    ray.Direction = dir;

    // 可視点からレイをトレース.
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
        return;
    }

    //-------------------------------
    // サンプル点を記録.
    //-------------------------------

    // 頂点データ取得.
    SurfaceHit sampleVertex = GetSurfaceHit(payload.InstanceId, payload.PrimitiveId, payload.Barycentrics);

    // マテリアル取得.
    material = GetMaterial(payload.InstanceId, sampleVertex.TexCoord, 0.0f);

    // 自己発光による放射輝度.
    Lo += throughput * material.Emissive;

    // Next Event Estimation.
    {
    }

    V = -ray.Direction;
    N = sampleVertex.GeometryNormal;

    u = float3(Random(seed), Random(seed), Random(seed));
    float pdf_s;

    // マテリアルを評価.
    brdfWeight = EvaluateMaterial(V, N, u, material, dir, pdf_s);
    brdfWeight /= pdf_s;

    throughput *= brdfWeight;

    //-------------------------------
    // 初期サンプルを記録.
    //-------------------------------
    Sample S;

    // Visible Point.
    S.PointV.P          = visibleVertex.Position;
    S.PointV.N          = visibleVertex.Normal;
    S.PointV.BsdfPdf    = pdf_v;
    S.PointV.LightPdf   = 1.0f;

    // Sampling Point.
    S.PointS.P          = sampleVertex.Position;
    S.PointS.N          = sampleVertex.Normal;
    S.PointS.BsdfPdf    = pdf_s;
    S.PointS.LightPdf   = 1.0f;

    // Other.
    S.Lo         = Lo;
    S.Wi         = dir;
    S.FrameIndex = SceneParam.FrameIndex;
    S.Flags      = RESERVOIR_FLAG_HIT;

    //-------------------------------
    // テンポラルリザーバー更新.
    //-------------------------------
    Reservoir R = TemporalReservoirBuffer[index];

    float w = TargetPDF(S) / SourcePDF(S);  // Equation (5)
    R.Update(S, w, Random(seed));
    R.W = R.w_sum / (R.M * TargetPDF(R.z)); // Equation (7)

    TemporalReservoirBuffer[index] = R;
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

//-----------------------------------------------------------------------------
//      シャドウレイヒット時のシェーダ.
//-----------------------------------------------------------------------------
[shader("closesthit")]
void OnShadowClosestHit(inout ShadowPayload payload, in HitArgs args)
{ payload.Visible = true; }

//-----------------------------------------------------------------------------
//      シャドウレイのミスシェーダです.
//-----------------------------------------------------------------------------
[shader("miss")]
void OnShadowMiss(inout ShadowPayload payload)
{ payload.Visible = false; }