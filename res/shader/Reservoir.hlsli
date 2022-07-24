//-----------------------------------------------------------------------------
// File : Reservoir.hlsli
// Desc : Reservoir
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------
#ifndef RESERVOIR_HLSLI
#define RESERVOIR_HLSLI

// Reservoir flags.
#define RESERVOIR_FLAG_HIT  (0x1 << 0)


///////////////////////////////////////////////////////////////////////////////
// HitInfo structure
///////////////////////////////////////////////////////////////////////////////
struct HitInfo
{
    float3  P;          // 位置座標.
    float   BsdfPdf;    // BSDFの確率密度関数.
    float3  N;          // 法線ベクトル.
    float   LightPdf;   // ライトの確率密度関数.
};

///////////////////////////////////////////////////////////////////////////////
// Sample structure
///////////////////////////////////////////////////////////////////////////////
struct Sample
{
    HitInfo PointV;     // 可視点.
    HitInfo PointS;     // サンプル点.
    float3  Lo;         // サンプル点において出射される放射輝度.
    uint    Flags;      // フラグ.
    float3  Wi;         // サンプル点からの次の方向.
    uint    FrameIndex; // 乱数生成の為のフレーム番号.
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

    //-------------------------------------------------------------------------
    //      ヒットしたかどうかチェックします.
    //-------------------------------------------------------------------------
    bool IsHit()
    { return (z.Flags & RESERVOIR_FLAG_HIT); }
};

float SourcePDF(Sample value)
{ return value.PointV.BsdfPdf; }

float TargetPDF(Sample value)
{ return dot(value.Lo, float3(0.2126f, 0.7152f, 0.0722f)); }

#endif//RESERVOIR_HLLSI
