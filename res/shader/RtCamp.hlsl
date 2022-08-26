//-----------------------------------------------------------------------------
// File : RtCamp.hlsl
// Desc : レイトレ合宿提出用シェーダ.
// Copyright(c) Project Asura. All right resreved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <Common.hlsli>

#define FURNANCE_TEST           (0)
#define RIS_CANDIDATES_LIGHTS   (8)
#define DEBUG_RAY               (0)

#if DEBUG_RAY
#define OUT_DEFAULT     (0)     // 放射輝度 Lo
#define OUT_POSITION    (1)     // 位置座標.
#define OUT_NORMAL      (2)     // 法線ベクトル.
#define OUT_TANGENT     (3)     // 接線ベクトル.
#define OUT_TEXCOORD    (4)     // テクスチャ座標.
#define OUT_RAY_DIR     (5)     // レイの方向ベクトル.
#define OUT_BRDF        (6)     // BRDF
#define OUT_PDF         (7)     // 確率密度.
#define OUT_WEIGHT      (8)     // 重み W (throughput).

//------------------------------------------------
// ここを書き換えて，HotReloadする.
#define DEBUG_DEPTH_INDEX   SceneParam.MaxBounce
//#define DEBUG_DEPTH_INDEX   1
#define DEBUG_OUT_FLAG      OUT_DEFAULT
//#define DEBUG_OUT_FLAG      OUT_RAY_DIR
//------------------------------------------------
#endif


#if FURNANCE_TEST
static const float3 kFurnaceColor = float3(0.5f, 0.5f, 0.5f);
#endif

//-----------------------------------------------------------------------------
// Resources
//-----------------------------------------------------------------------------
RWTexture2D<float4>     Canvas      : register(u0);
Texture2D<float4>       BackGround  : register(t4);
StructuredBuffer<Light> Lights      : register(t5);


//-----------------------------------------------------------------------------
//      IBLをサンプルします.
//-----------------------------------------------------------------------------
float3 SampleIBL(float3 dir)
{
    float2 uv = ToSphereMapCoord(dir);
    return BackGround.SampleLevel(LinearWrap, uv, 0.0f).rgb * SceneParam.SkyIntensity;
}

//-----------------------------------------------------------------------------
//      ランダムにライトを一つ選択します.
//-----------------------------------------------------------------------------
bool SampleLightUniform
(
    inout uint4 seed,
    out Light   light,
    out float   lightSampleWeight
)
{
    if (SceneParam.LightCount == 0)
    { return false; }

    uint index = min(SceneParam.LightCount - 1, uint(Random(seed) * SceneParam.LightCount));
    light = Lights[index];

    // PDF of uniform distribution is (1/light count).
    lightSampleWeight = float(SceneParam.LightCount); // PDFの逆数にして，割らずに掛ければ済むように.

    return true;
}

//-----------------------------------------------------------------------------
//      RIS(Resampled Importance Sampling) を用いてランダムにライトを選択します.
//-----------------------------------------------------------------------------
bool SampleLightRIS
(
    inout uint4 seed,
    float3      hitPosition,
    float3      surfaceNormal,
    out Light   selectedSample,
    out float   lightSampleWeight
)
{
    if (SceneParam.LightCount == 0)
    { return false; }

    selectedSample = (Light)0;
    float totalWeights = 0.0f;
    float samplePdfG   = 0.0f;

    const int count = RIS_CANDIDATES_LIGHTS;

    for(int i=0; i<count; ++i)
    {
        float candidateWeight;
        Light candidateLight;

        if (SampleLightUniform(seed, candidateLight, candidateWeight))
        {
            float3 lightVector;
            float  lightDistance;
            GetLightData(candidateLight, hitPosition, lightVector, lightDistance);

            // 裏面向きのライトは無視.
            float3 L = normalize(lightVector);
            if (dot(surfaceNormal, L) < 1e-5f)
            { continue; }

            float candidatePdfG = Luminance(GetLightIntensity(candidateLight, lightDistance));
            const float candidateRISWeight = candidatePdfG * candidateWeight;

            totalWeights += candidateRISWeight;
            if (Random(seed) < (candidateRISWeight / totalWeights))
            {
                selectedSample = candidateLight;
                samplePdfG     = candidatePdfG;
            }
        }
    }

    if (totalWeights == 0.0f)
    { return false; }

    lightSampleWeight = (totalWeights / float(count)) / samplePdfG;
    return true;
}

#if DEBUG_RAY
//-----------------------------------------------------------------------------
//      デバッグ用レイ生成シェーダです.
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

    float3 W  = float3(1.0f, 1.0f, 1.0f);
    float3 Lo = float3(0.0f, 0.0f, 0.0f);
    float4 output = float4(0.0f, 0.0f, 0.0f, 0.0f);

    [loop]
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
        #if FURNANCE_TEST
            Lo += W * kFurnaceColor;
        #else
            Lo += W * SampleIBL(ray.Direction);
        #endif
            break;
        }

        // 頂点データ取得.
        SurfaceHit vertex = GetSurfaceHit(payload.InstanceId, payload.PrimitiveId, payload.Barycentrics);

        // マテリアル取得.
        Material material = GetMaterial(payload.InstanceId, vertex.TexCoord, 0.0f);

        float3 B = normalize(cross(vertex.Tangent, vertex.Normal));
        float3 N = FromTangentSpaceToWorld(material.Normal, vertex.Tangent, B, vertex.Normal);

        float3 geometryNormal = vertex.GeometryNormal;
        float3 V = -ray.Direction;

        if (dot(geometryNormal, V) < 0.0f)
        { geometryNormal = -geometryNormal; }

        // 自己発光による放射輝度.
        Lo += W * material.Emissive;

        // シェーディング処理.
        float3 u = float3(Random(seed), Random(seed), Random(seed));
        float3 dir;
        float  pdf;
        float3 brdf = EvaluateMaterial(V, N, u, material, dir, pdf);

        bool endLoop = false;

        // ロシアンルーレット.
        if (bounce > SceneParam.MinBounce)
        {
            float p = min(0.95f, Luminance(brdf));
            if (p < Random(seed))
            { endLoop = true; }

            brdf /= p;
        }

        W *= brdf / pdf;

        // 重みがゼロに成ったら以降の更新は無駄なので打ち切りにする.
        if (all(W <= (0.0f).xxx))
        { endLoop = true; }

        // レイを更新.
        ray.Origin    = OffsetRay(vertex.Position, geometryNormal);
        ray.Direction = dir;

        if (bounce == DEBUG_DEPTH_INDEX)
        {
            switch(DEBUG_OUT_FLAG)
            {
            case OUT_POSITION:
                output = float4(vertex.Position, 0.0f);
                break;

            case OUT_NORMAL:
                output = float4(vertex.Normal, 0.0f);
                break;

            case OUT_TANGENT:
                output = float4(vertex.Tangent, 0.0f);
                break;

            case OUT_TEXCOORD:
                output = float4(vertex.TexCoord.xy, 0.0f, 0.0f);
                break;

            case OUT_RAY_DIR:
                output = float4(ray.Direction, 0.0f);
                break;

            case OUT_BRDF:
                output = float4(brdf, 0.0f);
                break;

            case OUT_PDF:
                output = float4(pdf, 0.0f, 0.0f, 0.0f);
                break;

            case OUT_WEIGHT:
                output = float4(W, 0.0f);
                break;
            }

            endLoop = true;
        }

        if (endLoop)
        { break; }
    }

    if (DEBUG_OUT_FLAG == OUT_DEFAULT)
    { output = float4(Lo, 0.0f); }

    uint2 launchId = DispatchRaysIndex().xy;
    Canvas[launchId] = float4(output);
}
#else
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

    float3 W  = float3(1.0f, 1.0f, 1.0f);  // 重み.
    float3 Lo = float3(0.0f, 0.0f, 0.0f);  // 放射輝度.

    [loop]
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
        #if FURNANCE_TEST
            Lo += W * kFurnaceColor;
        #else
            Lo += W * SampleIBL(ray.Direction);
        #endif
            break;
        }

        // 頂点データ取得.
        SurfaceHit vertex = GetSurfaceHit(payload.InstanceId, payload.PrimitiveId, payload.Barycentrics);

        // マテリアル取得.
        Material material = GetMaterial(payload.InstanceId, vertex.TexCoord, 0.0f);

        float3 B = normalize(cross(vertex.Tangent, vertex.Normal));
        float3 N = FromTangentSpaceToWorld(material.Normal, vertex.Tangent, B, vertex.Normal);

        float3 geometryNormal = vertex.GeometryNormal;
        float3 V = -ray.Direction;

        if (dot(geometryNormal, V) < 0.0f)
        { geometryNormal = -geometryNormal; }

        // 自己発光による放射輝度.
        Lo += W * material.Emissive;

#if 1
        // Next Event Estimation.
        {
            // BSDFがデルタ関数を持たない場合のみ.
            //if (!HasDelta(material))
            {
                // 物体からのレイの入出を考慮した法線.
                float3 Nm = dot(N, V) > 0.0f ? N : -N;

                // 光源をサンプリング.
                float lightWeight;
                float2 st = SampleMipMap(BackGround, float2(Random(seed), Random(seed)), lightWeight);

                // 方向ベクトルに変換.
                float3 dir = FromSphereMapCoord(st);

                if (!CastShadowRay(vertex.Position, geometryNormal, dir, FLT_MAX))
                {
                    // シャドウレイを飛ばして，光源上のサンプリングとレイ原点の間に遮断が無い場合.
                    float cosShadow = dot(Nm, dir);
                    float cosLight  = 1.0f;

                    // BSDF.
                    float3 fs = SampleMaterial(V, Nm, dir, Random(seed), material);

                    // 幾何項.
                    float G = (cosShadow * cosLight);

                    // ライト.
                #if FURNANCE_TEST
                    float3 Le = kFurnaceColor;
                #else
                    float3 Le = SampleIBL(dir);
                #endif

                    Lo += W * (fs * Le * G) * lightWeight;
                }
            }
        }
#else
        // Next Event Estimation.
        if (!HasDelta(material))
        {
            // 物体からのレイの入出を考慮した法線.
            float3 Nm = dot(N, -V) < 0.0f ? N : -N;


            Light light;
            float lightWeight;
            if (SampleLightRIS(seed, vertex.Position, geometryNormal, light, lightWeight))
            {
                float3 lightVector;
                float  lightDistance;
                GetLightData(light, vertex.Position, lightVector, lightDistance);

                float3 dir = normalize(lightVector);

                if (!CastShadowRay(vertex.Position, geometryNormal, dir, lightDistance))
                {
                    // BSDF.
                    float3 fs = SampleMaterial(V, Nm, dir, Random(seed), material);

                    // Light
                    float3 Le = GetLightIntensity(light, lightDistance);

                    Lo += W * fs * Le * lightWeight;
                }
            }
        }
#endif

        // 最後のバウンスであれば早期終了(BRDFをサンプルしてもロジック的に反映されないため).
        if (bounce == SceneParam.MaxBounce - 1)
        { break; }

        // シェーディング処理.
        float3 u = float3(Random(seed), Random(seed), Random(seed));
        float3 dir;
        float  pdf;
        float3 brdf = EvaluateMaterial(V, N, u, material, dir, pdf);

        // ロシアンルーレット.
        if (bounce > SceneParam.MinBounce)
        {
            float p = min(0.95f, Luminance(brdf));
            if (p < Random(seed))
            { break; }

            brdf /= p;
        }

        W *= brdf / pdf;

        // 重みがゼロに成ったら以降の更新は無駄なので打ち切りにする.
        if (all(W <= (0.0f).xxx))
        { break; }

        // レイを更新.
        ray.Origin    = OffsetRay(vertex.Position, geometryNormal);
        ray.Direction = dir;
    }

    uint2 launchId = DispatchRaysIndex().xy;

    float3 prevLo = Canvas[launchId].rgb;
    float3 color  = (SceneParam.EnableAccumulation) ? (prevLo + Lo) : Lo;

    Canvas[launchId] = float4(SaturateFloat(color), 1.0f);
}
#endif

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
[shader("anyhit")]
void OnShadowAnyHit(inout ShadowPayload payload, in HitArgs args)
{
    payload.Visible = true;
    AcceptHitAndEndSearch();
}

//-----------------------------------------------------------------------------
//      シャドウレイのミスシェーダです.
//-----------------------------------------------------------------------------
[shader("miss")]
void OnShadowMiss(inout ShadowPayload payload)
{ payload.Visible = false; }

