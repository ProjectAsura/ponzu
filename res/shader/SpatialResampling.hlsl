//-----------------------------------------------------------------------------
// File : SpatialResampling.hlsl
// Desc : Spatial Resampling.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <Reservoir.hlsli>
#include <Common.hlsli>


#define PICK_RADIUS     (30)    // サンプリング半径 30 pix.    [Bitterlie 2020].
#define PICK_SAMPLES    (5)     // サンプリング数   5 samples. [Bitterlie 2020].

//-----------------------------------------------------------------------------
// Resources
//-----------------------------------------------------------------------------
RWStructuredBuffer<Reservoir>   SpatioReservoirBuffer   : register(u0);
StructuredBuffer<Reservoir>     TemporalReservoirBuffer : register(t4);

static const float kCosThreshold   = cos(radians(0.25f));
static const float kDepthThreshold = 0.05f;

//-----------------------------------------------------------------------------
//      ヤコビアンの行列式を計算します
//-----------------------------------------------------------------------------
float CalcJacobianDeterminant
(
    float3 x1q,     // first vertex of the reused path. (visible point of q).
    float3 x2q,     // second vertex of the reused path. (sample point of q).
    float3 x1r,     // visible point form the destination pixel.
    float3 n2q      // surface normal at x2q. (see Figure 6).
)
{
    float3 dq = x1q - x2q;
    float3 dr = x1r - x2q;

    float cosPhi_r = abs(dot(dq, n2q));
    float cosPhi_q = abs(dot(dr, n2q));

    // Equation (11).
    return (cosPhi_r / cosPhi_q) * (dot(dq, dq) / dot(dr, dr));
}

//-----------------------------------------------------------------------------
//      ランダムに隣接ピクセルを選択します.
//-----------------------------------------------------------------------------
uint2 PickTemporalNeighbor(uint2 pix, uint index)
{ return pix + uint2(Hammersley(index, PICK_SAMPLES) * PICK_RADIUS); }

//-----------------------------------------------------------------------------
//      ピクセルIDからバッファ番号に変換します.
//-----------------------------------------------------------------------------
uint ToIndex(uint2 dispatchId)
{ return dispatchId.x + dispatchId.y * DispatchRaysDimensions().x; }

//-----------------------------------------------------------------------------
//      指定された点が見えるかどうかチェックします.
//-----------------------------------------------------------------------------
bool CastShadowRay(float3 x0, float3 x1)
{
    float3 dif  = x1 - x0;
    float  tMax = length(dif);
    float3 dir  = dif / tMax;

    RayDesc ray;
    ray.Origin      = x0;
    ray.Direction   = dir;
    ray.TMin        = 0.0f;
    ray.TMax        = tMax;

    ShadowPayload payload;
    payload.Visible = false;

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
//      レイ生成シェーダです.
//-----------------------------------------------------------------------------
[shader("raygeneration")]
void OnGenerateRay()
{
    const uint2 dispatchId = DispatchRaysIndex().xy;

    // 乱数初期化.
    uint4 seed = SetSeed(dispatchId.xy, SceneParam.FrameIndex);

    // for each pixel q do
    uint index = ToIndex(dispatchId.xy);

    // R_s ← SpatioReservoirBuffer[q]
    Reservoir Rs = SpatioReservoirBuffer[index];

    // ヒット情報が無い場合は計算できない.
    if (!Rs.IsHit())
    {
        //RayDesc ray = GeneratePinholeCameraRay(pixel);
        //Canvas[dispathId] = SampleIBL(ray.Dir);
        return;
    }

    float3 N0 = Rs.z.PointV.N;
    float depth0 = dot(Rs.z.PointV.P, SceneParam.CameraDir);

    float Z = 0.0f;

    // for s=1 to maxIterations do
    for(int s=1; s<SceneParam.MaxIteration; ++s)
    {
        // Randomly choose a neighbor pixel q_n
        for(uint i=0; i<PICK_SAMPLES; ++i)
        {
            uint2 qn = PickTemporalNeighbor(dispatchId.xy, i);
            uint index_n = ToIndex(qn); // qn ---> index_n.

            // R_n ← TemporalReservoirBuffer[q_n]
            Reservoir Rn = TemporalReservoirBuffer[index_n];

            float3 N1 = Rn.z.PointV.N;
            float depth1 = dot(Rn.z.PointV.P, SceneParam.CameraDir);

            // Calculate geometric similarity between q and q_n
            bool similarity = (dot(N0, N1) < kCosThreshold) && (abs(depth0 - depth1) < kDepthThreshold);

            // if similarity is lower than the given threshold then
            if (similarity)
            {
                continue;
            }

            // Calculate |J_{q_n → q}| using Equation (11).
            float jacobianDet = CalcJacobianDeterminant(Rn.z.PointV.P, Rn.z.PointS.P, Rs.z.PointV.P, Rs.z.PointV.N);

            // {hat_p'}_q ← hat_p_q(Rn.z) / |J_{q_n → q}|.
            float hatPq = TargetPDF(Rn.z);
            float hatP_dash_q = hatPq / jacobianDet;

            // if R_n's sample point is not visible to x_v at q then
            bool visible = CastShadowRay(Rs.z.PointV.P, Rn.z.PointS.P);

            if (!visible)
            {
                // {hat_p'}_q ← 0
                hatP_dash_q = 0.0f;
            }

            // R_s.MERGE(R_n, {hat_p'}_q)
            Rs.Merge(Rn, hatP_dash_q, Random(seed));

            if (TargetPDF(Rs.z) > 0.0f)
            {
                Z += Rn.M; // Bias correction.
            }
        }
    }


    // R_s.W ← R_s.w / (Z * hat_p_q(R_s.z)) // Equation 7.
    Rs.W = Rs.w_sum / (Z * TargetPDF(Rs.z));

    // SpatialReservoirBuffer[q] ← R_s
    SpatioReservoirBuffer[index] = Rs;


}

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
