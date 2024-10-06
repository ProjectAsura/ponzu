//-----------------------------------------------------------------------------
// File : RTPM_PhotonCulling.hlsl
// Desc : Photon Culling Compute Shader.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include "Math.hlsli"
#include "RTPM_Common.hlsli"


///////////////////////////////////////////////////////////////////////////////
// PassParameter structure
///////////////////////////////////////////////////////////////////////////////
struct PassParameter
{
    float HashScaleFactor;
    uint  HashSize;
    float ProjTest;
    float GlobalRadius;
    uint  ExtentY;
    uint3 Reserved;
};

//=============================================================================
// Resources
//=============================================================================
ConstantBuffer<PassParameter>   PassParam         : register(b1);
Texture2D<uint4>                VBuffer           : register(t3);    // ビジビリティバッファ.
RWTexture2D<uint>               CullingHashBuffer : register(u0);    // ハッシュバッファ.

//-----------------------------------------------------------------------------
//      セル番号を取得します.
//-----------------------------------------------------------------------------
void GetArrayOfCells(float3 position, out int3 cells[8])
{
    float3 cell       = position * PassParam.HashScaleFactor;
    float  radius     = PassParam.GlobalRadius * PassParam.HashScaleFactor;
    float3 cellFloor  = floor(cell);
    float3 relCellPos = abs(cell - cellFloor);
    
#if __HLSL_VERSION >= 2021
    int3 offset = select(relCelPos < 0.5f, -1.0, 1.0);
#else
    int3 offset = (relCellPos < 0.5f) ? -1.0 : 1.0f;
#endif
    
    cells[0] = int3(cellFloor);
    cells[1] = int3(floor(cell + radius * float3(offset.x, 0       , 0       )));
    cells[2] = int3(floor(cell + radius * float3(offset.x, offset.y, 0       )));
    cells[3] = int3(floor(cell + radius * float3(offset.x, offset.y, offset.z)));
    cells[4] = int3(floor(cell + radius * float3(offset.x, 0       , offset.z)));
    cells[5] = int3(floor(cell + radius * float3(0       , offset.y, 0       )));
    cells[6] = int3(floor(cell + radius * float3(0       , offset.y, offset.z)));
    cells[7] = int3(floor(cell + radius * float3(0       , 0       , offset.z)));
}

//-----------------------------------------------------------------------------
//      メインエントリーポイントです.
//-----------------------------------------------------------------------------
[numthreads(8, 8, 1)]
void main
(
    uint3 dispatchId : SV_DispatchThreadID,
    uint  groupIndex : SV_GroupIndex
)
{
    uint2 remapId = RemapLane8x8(dispatchId.xy, groupIndex);

    // ヒット情報を取得.
    const HitInfo hit = UnpackHitInfo(VBuffer[remapId]);
    if (!hit.IsValid())
        return;

    // 位置情報を取得.
    SurfaceHit vertex = GetSurfaceHit(hit.InstanceId, hit.PrimitiveIndex, hit.BaryCentrics);
 
    // ビュー射影空間に変換.
    float4 projPos = mul(SceneParam.ViewProj, float4(vertex.Position, 1.0f));
    projPos /= projPos.w;
 
    // 標準ボリューム内かどうかチェック.
    if (any(abs(projPos.xy) > PassParam.ProjTest) || projPos.z > 1.0f || projPos.z < 0.0f)
    {
        int3 cells[8];
        GetArrayOfCells(vertex.Position, cells);

        [unroll] for(uint i=0; i<8; ++i)
        {
            uint hash = WangHash(cells[i]) & (PassParam.HashSize - 1);
            uint2 hashId = uint2(hash % PassParam.ExtentY, hash / PassParam.ExtentY);
            CullingHashBuffer[hashId] = 1;
        }
    }
}
