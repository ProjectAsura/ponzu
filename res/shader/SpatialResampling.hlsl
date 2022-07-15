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

//-----------------------------------------------------------------------------
// Resources
//-----------------------------------------------------------------------------
StructuredBuffer<Reservoir>     TemporalReservoirBuffer;
RWStructuredBuffer<Reservoir>   SpatioReservoirBuffer;
Texture2D<float2>               VelocityBuffer;

static const float kCosThreshold   = cos(radians(0.25));
static const float kDepthThreshold = 0.05f;

//-----------------------------------------------------------------------------
//      ヤコビアンの行列式を計算します
//-----------------------------------------------------------------------------
float CalcJacobian
(
    float3 x1q,     // first vertex of the reused path.
    float3 x2q,     // second vertex of the reused path.
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
uint2 ChooseNeighborPixel()
{
    // TODO
    return uint2(0, 0);
}

float3 Reprojection(float3 value)
{
    // TODO
    return value;
}

[numthreads(8, 8, 1)]
void main( uint3 dispatchId : SV_DispatchThreadID )
{
#if 0
    // 乱数初期化.
    uint4 seed = SetSeed(dispatchId.xy, SceneParam.FrameIndex);

    const int MaxIterations = 9;

    // for each pixel q do
    uint index = dispatchId.x + dispatchId.y * uint(SceneParam.Size.x);

    // R_s ← SpatioReservoirBuffer[q]
    Reservoir Rs = SpatioReservoirBuffer[index];

    float3 N0 = Rs.z.N_v;
    float depth0 = dot(Rs.z.P_v, ScemeParam.CameraDir);

    float Z = 0.0f;

    // for s=1 to maxIterations do
    for(int s=1; s<MaxIterations; ++s)
    {
        // Randomly choose a neighbor pixel q_n
        uint2 qn = ChooseNeighborPixel();
        uint index_n = qn.x + qn.y * uint(SceneParam.Size.x); // qn ---> index_n.

        // R_n ← TemporalReservoirBuffer[q_n]
        Reservoir Rn = TemporalReservoirBuffer[index_n];

        float3 N1 = Rn.z.N_v;
        float depth1 = dot(Rn.z.P_v, SceneParam.CameraDir);

        // Calculate geometric similarity between q and q_n
        bool similarity = (dot(N0, N1) < kCosThreshold) || (abs(depth0 - depth1) < kDepthThreshold);

        // if similarity is lower than the given threshold then
        if (similarity)
        {
            continue;
        }

        // Calculate |J_{q_n → q}| using Equation (11).
        float J = CalcJacobian(Rn.P_s, Rs.P_s, Rn.P_v, Rs.N_s);

        // hat_p'_q ← hat_p_q(Rn.z) / |J_{q_n → q}|.
        float hatPq = TargetPDF(Rn.z);
        float hatP_dash_q = hatPq / J;

        // if R_n's sample point is not visible to x_v at q then
        bool visible = false;
        if (!visible)
        {
            // hat_p'_q ← 0
            hatPq = 0.0f;
        }

        // R_s.MERGE(R_n, hat_p'_q)
        Rs.Merge(Rn, hatP_dash_q, Random(seed));

        if (TargetPDF(Rs.z) > 0.0f)
        {
            Z += Rn.M; // Bias correction.
        }
    }


    // R_s.W ← R_s.w / (Z * hat_p_q(R_s.z)) // Equation 7.
    Rs.W = Rs.w / (Z * TargetPDF(Rs.z));

    // SpatialReservoirBuffer[q] ← R_s
    SpatioReservoirBuffer[index] = Rs;
#endif
}