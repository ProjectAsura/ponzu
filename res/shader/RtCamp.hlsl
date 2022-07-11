//-----------------------------------------------------------------------------
// File : RtCamp.hlsl
// Desc : レイトレ合宿提出用シェーダ.
// Copyright(c) Project Asura. All right resreved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <Common.hlsli>


//-----------------------------------------------------------------------------
// Resources
//-----------------------------------------------------------------------------
RWTexture2D<float4>             Canvas      : register(u0);
Texture2D<float4>               BackGround  : register(t4);


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
    // 乱数初期化.
    uint4 seed = SetSeed(DispatchRaysIndex().xy, SceneParam.FrameIndex);

    float2 pixel = float2(DispatchRaysIndex().xy);
    const float2 resolution = float2(DispatchRaysDimensions().xy);

    // アンリエイリアシング.
    float2 offset = float2(Random(seed), Random(seed));
    pixel += lerp(-0.5f.xx, 0.5f.xx, offset);

    float2 uv = (pixel + 0.5f) / resolution;
    uv.y = 1.0f - uv.y;
    pixel = uv * 2.0f - 1.0f;

    // ペイロード初期化.
    Payload payload = (Payload)0;

    // レイを設定.
    RayDesc ray = GeneratePinholeCameraRay(pixel);

#if 0
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

    float3 color = SampleIBL(ray.Direction);

    if (payload.HasHit())
    {
        Vertex vertex = GetVertex(payload.InstanceId, payload.PrimitiveId, payload.Barycentrics);
        Material material = GetMaterial(payload.InstanceId, vertex.TexCoord, 0.0f);
        //float3 B = normalize(cross(vertex.Tangent, vertex.Normal));
        //float3 N = FromTangentSpaceToWorld(material.Normal, vertex.Tangent, B, vertex.Normal);

        //color = N * 0.5f + 0.5f;
        color = material.BaseColor.rgb;
    }

    // レンダーターゲットに格納.
    Canvas[DispatchRaysIndex().xy] = float4(color, 1.0f);
    //Canvas[DispatchRaysIndex().xy] = SampleIBL(ray.Direction);
#else

    float3 W = float3(1.0f, 1.0f, 1.0f);  // 重み.
    float3 L = float3(0.0f, 0.0f, 0.0f);  // 放射輝度.

    for(int bounce=0; bounce<SceneParam.MaxBounce; ++bounce)
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
        if (dot(geometryNormal, V) < 0.0f)
        {
            geometryNormal = -geometryNormal;
        }
        if (dot(geometryNormal, N) < 0.0f)
        {
            N = -N;
            T = -T;
        }

        // 法線ベクトルを計算.
        float3 B = normalize(cross(T, N));

        // マテリアル取得.
        Material material = GetMaterial(payload.InstanceId, vertex.TexCoord, 0.0f);

        // 自己発光による放射輝度.
        L += W * material.Emissive;

        // RIS
        {
        }

        // 最後のバウンスであれば早期終了(BRDFをサンプルしてもロジック的に反映されないため).
        if (bounce == SceneParam.MaxBounce - 1)
        { break; }

        // シェーディング処理.
        float3 u = float3(Random(seed), Random(seed), Random(seed));
        float3 dir;
        float  pdf;
        float3 brdfWeight = EvaluateMaterial(V, N, u, material, dir, pdf);

        // ロシアンルーレット.
        if (bounce > SceneParam.MinBounce)
        {
            float p = min(0.95f, Luminance(brdfWeight));
            if (p > Random(seed))
            { break; }
            else
            { brdfWeight /= pdf; }
        }

        W *= brdfWeight;

        // 重みがゼロに成ったら以降の更新は無駄なので打ち切りにする.
        if (all(W < (1e-6f).xxx))
        { break; }

        // レイを更新.
        ray.Origin    = OffsetRay(vertex.Position, geometryNormal);
        ray.Direction = dir;
    }

    uint2 launchId = DispatchRaysIndex().xy;

    float3 prevL = Canvas[launchId].rgb;
    float3 color = (SceneParam.EnableAccumulation) ? (prevL + L) : L;

    Canvas[launchId] = float4(color, 1.0f);
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

