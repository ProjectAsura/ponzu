//-----------------------------------------------------------------------------
// File : ShadePixel.hlsl
// Desc : Shade Pixel.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <Reservoir.hlsli>


///////////////////////////////////////////////////////////////////////////////
// ShaderParam structure
///////////////////////////////////////////////////////////////////////////////
struct ShaderParam
{
    uint2   Size;                   //!< レンダーターゲットサイズ.
    uint    EnableAccumulation;
    uint    AccumulationFrame;
};

//-----------------------------------------------------------------------------
// Resources
//-----------------------------------------------------------------------------
RWTexture2D<float4>             Canvas          : register(u0); // レンダーターゲット.
StructuredBuffer<Reservoir>     ReservoirBuffer : register(t0); // TemporalReservoirBuffer or SpatialReservoirBuffer.
ConstantBuffer<ShaderParam>     Param           : register(b0); // 定数バッファ.


//-----------------------------------------------------------------------------
//      ピクセルをシェーディングします.
//-----------------------------------------------------------------------------
float3 ShadePixel(Reservoir r)
{
    // fq(x) = brdf * Le * G / pdf;
    // return fq(r) * r.W;

    // Radianceを計算し, r.Wを掛ける.
    return r.z.Lo * r.W;
}

//-----------------------------------------------------------------------------
//      メインエントリーポイントです.
//-----------------------------------------------------------------------------
[numthreads(8, 8, 1)]
void main( uint3 dispatchId : SV_DispatchThreadID )
{
    // 範囲外なら処理しない.
    if (any(dispatchId.xy > Param.Size))
    { return; }

    // バッファ番号計算.
    uint index = dispatchId.x + dispatchId.y * Param.Size.y;

    // リザーバー取得.
    Reservoir r = ReservoirBuffer[index];

    // シェーディング.
    Canvas[dispatchId.xy] = float4(ShadePixel(r), 1.0f);
}
