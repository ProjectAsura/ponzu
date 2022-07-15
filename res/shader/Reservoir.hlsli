//-----------------------------------------------------------------------------
// File : Reservoir.hlsli
// Desc : Reservoir
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------
#ifndef RESERVOIR_HLSLI
#define RESERVOIR_HLSLI

///////////////////////////////////////////////////////////////////////////////
// Sample structure
///////////////////////////////////////////////////////////////////////////////
struct Sample
{
    float3  P_v;    // Visible point.
    float3  N_v;    // Visible surface normal.
    float3  L_v;    // Outgoing radiance at visible point in RGB.
    float   Pdf_v;

    float3  P_s;    // Sample point.
    float3  N_s;    // Sample surface normal.
    float3  L_s;    // Outgoing radiance at sample point in RGB.
    float   Pdf_s;

    float3  Random; // Random numbers used for path.
};

///////////////////////////////////////////////////////////////////////////////
// Reservoir structure
///////////////////////////////////////////////////////////////////////////////
struct Reservoir
{
    Sample  z;      // The output sample.
    float   w_sum;  // The sum of weights.
    float   M;      // The number of samples seen so far.
    float   W;      // Equation (7).

    //-------------------------------------------------------------------------
    //      リザーバーを更新します.
    //-------------------------------------------------------------------------
    void Update(Sample s_new, float w_new, float u)
    {
        // [Ouyang 2021] Y.Ouyang, Si.Liu, M.Kettunen, M.Pharr, J.Pataleoni,
        // "ReSTIR GI: Path Resampling for Real-Time Path Tracing", HPG 2021.
        w_sum += w_new;
        M += 1.0f;
        if (u < (w_new / w_sum))
        { z = s_new; }
    }

    //-------------------------------------------------------------------------
    //      リザーバーをマージします.
    //-------------------------------------------------------------------------
    void Merge(Reservoir r, float hat_p, float u)
    {
        // [Ouyang 2021] Y.Ouyang, Si.Liu, M.Kettunen, M.Pharr, J.Pataleoni,
        // "ReSTIR GI: Path Resampling for Real-Time Path Tracing", HPG 2021.
        float M0 = M;
        Update(r.z, hat_p * r.W * r.M, u);
        M = M0 + r.M;
    }
};

float SourcePDF(Sample value)
{ return value.Pdf_v; }

float TargetPDF(Sample value)
{ return dot(value.L_s, float3(0.2126f, 0.7152f, 0.0722f)); }

#endif//RESERVOIR_HLLSI
