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


float CalcSourcePDF(Sample value)
{ return value.Pdf_v; }

float CalcTargetPDF(Sample value)
{ return Luminance(value.L_s); }     // Equation (10).

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
    return uint2(0, 0);
}

[numthreads(8, 8, 1)]
void main( uint3 dispatchId : SV_DispatchThreadID )
{
#if 0
    // 乱数初期化.
    uint4 seed = SetSeed(dispatchId.xy, SceneParam.FrameIndex);

    const int MaxIterations = 4;
    int mergedCount = 0;
    uint Q[MaxIterations + 1];

    // for each pixel q do
    uint index = dispatchId.x + dispatchId.y * uint(SceneParam.Size.x);

    // R_s ← SpatioReservoirBuffer[q]
    Reservoir Rs = SpatioReservoirBuffer[index];

    // Q ← q.

    // for s=1 to maxIterations do
    for(int s=1; s<MaxIterations; ++s)
    {
        // Randomly choose a neighbor pixel q_n
        uint2 q_n = ChooseNeighborPixel();
        uint index_n = qn.x + qn.y * uint(SceneParam.Size.x); // qn ---> index_n.

        // Calculate geometric similarity between q and q_n
        bool similarity = (dot(N0, N1) < cos(radians(0.25)) || abs(depth0 - depth1) < 0.05f

        // if similarity is lower than the given threshold then
        if (similarity)
        {
            continue;
        }

        // R_n ← TemporalReservoirBuffer[q_n]
        Reservoir Rn = TemporalReservoirBuffer[index_n];

        // Calculate |J_{q_n → q}| using Equation (11).
        float J = CalcJacobian();

        // hat_p'_q ← hat_p_q(Rn.z) / |J_{q_n → q}|.
        float hatPq = CalcTargetPDF(Rn.z) / J;

        // if R_n's sample point is not visible to x_v at q then
        //if (1)
        {
            // hat_p'_q ← 0
            hatPq = 0.0f;
        }

        // R_s.MERGE(R_n, hat_p'_q)
        Rs.Merge(Rn, hatPq, Random(seed));

        // Q ← Q ∩ q_n
        Q[mergedCount] = index_n;
        mergedCount++;
    }

    // Z ← 0.
    float Z = 0.0f;

    // for each q_n in Q do
    for(int i=0; i<mergedCount; ++i)
    {
        // if hat_p_q_n(R_s.z) > 0 then
        // if ()
        {
            // Z ←　Z + R_n.M    // Bias correction.
            Z += Rn.M;
        }
    }

    // R_s.W ← R_s.w / (Z * hat_p_q(R_s.z)) // Equation 7.
    Rs.W = Rs.w / (Z * );

    // SpatialReservoirBuffer[q] ← R_s
    SpatioReservoirBuffer[index] = Rs;
#endif
}