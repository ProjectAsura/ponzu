//-----------------------------------------------------------------------------
// File : RTPMPass.cpp
// Desc : RT Photon Map Pass.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

#define NOMINMAX

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <utility>
#include <RTPMPass.h>
#include <fnd/asdxLogger.h>
#include <gfx/asdxCommandList.h>


namespace {

//-----------------------------------------------------------------------------
// Constant Values.
//-----------------------------------------------------------------------------
#include "../res/shader/Compile/RTPM_PhotonCulling.inc"
#include "../res/shader/Compile/RTPM_GeneratePhoton.inc"
#include "../res/shader/Compile/RTPM_StochasticCollectPhoton.inc"
static constexpr float kMinPhotonRadius = 0.00001f;

} // namespace

namespace r3d {

///////////////////////////////////////////////////////////////////////////////
// PhotonBuffer structure.
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      初期化処理を行います.
//-----------------------------------------------------------------------------
bool RTPMPass::PhotonBuffer::Init(ID3D12Device* pDevice, uint32_t width, uint32_t height)
{
    if (pDevice == nullptr || width == 0 || height == 0)
    {
        ELOGA("Error : Invalid Argument.");
        return false;
    }

    return true;
}

//-----------------------------------------------------------------------------
//      終了処理を行います.
//-----------------------------------------------------------------------------
void RTPMPass::PhotonBuffer::Term()
{
    AABB      .Term();
    Flux      .Term();
    Direction .Term();
    FaceNormal.Term();
}

///////////////////////////////////////////////////////////////////////////////
// RTPMPass class
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      コンストラクタです.
//-----------------------------------------------------------------------------
RTPMPass::RTPMPass()
{
}

//-----------------------------------------------------------------------------
//      デストラクタです.
//-----------------------------------------------------------------------------
RTPMPass::~RTPMPass()
{ Term(); }

//-----------------------------------------------------------------------------
//      初期化処理を行います.
//-----------------------------------------------------------------------------
bool RTPMPass::Init(ID3D12Device5* pDevice, uint32_t w, uint32_t h)
{
    m_Width  = w;
    m_Height = h;

    // フォトンカリング用ルートシグニチャ生成.
    {
        auto cs = D3D12_SHADER_VISIBILITY_ALL;

    }

    // フォトンカリングパイプラインを初期化.
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = m_PhotonCullingRootSig.GetPtr();
        desc.CS             = { RTPM_PhotonCulling, sizeof(RTPM_PhotonCulling) };
        
        if (!m_PhotonCullingPipe.Init(pDevice, &desc))
        {
            ELOGA("Error : PhotonCulling Pipe Init Failed.");
            return false;
        }
    }

    // フォトン生成用ルートシグニチャ生成.
    {
        auto lib = D3D12_SHADER_VISIBILITY_ALL;

    }

    // フォトン生成レイトレーシングパイプラインを初期化.
    {
        struct Payload
        {
            asdx::Vector3 Throughput;
            uint32_t      EncodedFaceNormal;
            asdx::Vector3 Origin;
            uint32_t      Terminated;
            asdx::Vector3 Direction;
            uint32_t      DiffuseHit;
            uint32_t      Seed[4];
        };

        std::vector<D3D12_HIT_GROUP_DESC> groups;
        groups.resize(1);
        groups[0].ClosestHitShaderImport = L"OnClosestHit";
        groups[0].HitGroupExport         = L"StandardHit";
        groups[0].Type                   = D3D12_HIT_GROUP_TYPE_TRIANGLES;

        std::vector<std::wstring> miss{ L"OnMiss" };

        asdx::RayTracingPipelineStateDesc desc;
        desc.pGlobalRootSignature   = m_GeneratePhotonRootSig.GetPtr();
        desc.DXILLibrary            = { RTPM_GeneratePhoton, sizeof(RTPM_GeneratePhoton) };
        desc.RayGeneration          = L"OnRayGeneration";
        desc.HitGroups              = groups;
        desc.MissTable              = miss;
        desc.MaxPayloadSize         = sizeof(Payload);
        desc.MaxAttributeSize       = sizeof(asdx::Vector2);
        desc.MaxTraceRecursionDepth = 16;

        if (!m_GeneratePhotonPipe.Init(pDevice, desc))
        {
            ELOGA("Error : GeneratePhoton Pipe Init Failed.");
            return false;
        }
    }

    // フォトン収集用ルートシグニチャ生成.
    {
        auto lib = D3D12_SHADER_VISIBILITY_ALL;

    }

    // フォトン収集レイトレーシングパイプラインを初期化.
    {
        struct Payload
        {
            uint32_t    Counter;
            uint32_t    PhotonList[3];
            uint32_t    Seed[4];
        };

        std::vector<D3D12_HIT_GROUP_DESC> groups;
        groups.resize(1);
        groups[0].AnyHitShaderImport        = L"OnAnyHit";
        groups[0].IntersectionShaderImport  = L"OnIntersection";
        groups[0].HitGroupExport            = L"StandardHit";
        groups[0].Type                      = D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE;

        std::vector<std::wstring> miss{};

        asdx::RayTracingPipelineStateDesc desc;
        desc.pGlobalRootSignature   = m_CollectPhotonRootSig.GetPtr();
        desc.DXILLibrary            = { RTPM_StochasticCollectPhoton, sizeof(RTPM_StochasticCollectPhoton) };
        desc.RayGeneration          = L"OnRayGeneration";
        desc.HitGroups              = groups;
        desc.MissTable              = miss;
        desc.MaxPayloadSize         = sizeof(Payload);
        desc.MaxAttributeSize       = sizeof(asdx::Vector2);
        desc.MaxTraceRecursionDepth = 16;

        if (!m_CollectPhotonPipe.Init(pDevice, desc))
        {
            ELOGA("Error : CollectPhoton Pipe Init Failed.");
            return false;
        }
    }

    // コースティクフォトンバッファを初期化.
    if (!m_CausticBuffer.Init(pDevice, w, h))
    {
        ELOGA("Error : Caustic Buffer Init Failed.");
        return false;
    }

    // グローバルフォトンバッファを初期化.
    if (!m_GlobalBuffer.Init(pDevice, w, h))
    {
        ELOGA("Error : Global Buffer Init Failed.");
        return false;
    }

    // スループットバッファ生成.
    {
    }

    // カリングハッシュバッファ生成.
    {
    }

    return true;
}

//-----------------------------------------------------------------------------
//      終了処理を行います.
//-----------------------------------------------------------------------------
void RTPMPass::Term()
{
    m_PhotonCullingPipe .Term();
    m_GeneratePhotonPipe.Term();
    m_CollectPhotonPipe .Term();

    m_PhotonCullingRootSig .Reset();
    m_GeneratePhotonRootSig.Reset();
    m_CollectPhotonRootSig .Reset();

    m_ThroughputBuffer  .Term();
    m_CullingHashBuffer .Term();

    m_CausticBuffer.Term();
    m_GlobalBuffer .Term();

    for(size_t i=0; i<m_ScratchBLAS.size(); ++i)
    { m_ScratchBLAS[i].Term(); }
    m_ScratchBLAS.clear();

    m_ScratchTLAS.Term();

    for(size_t i=0; i<m_PhotonBLAS.size(); ++i)
    { m_PhotonBLAS[i].Term(); }
    m_PhotonBLAS.clear();

    m_PhotonTLAS.Term();
}

//-----------------------------------------------------------------------------
//      描画処理を行います.
//-----------------------------------------------------------------------------
void RTPMPass::Render(ID3D12GraphicsCommandList4* pCmd, const Scene& scene,D3D12_GPU_VIRTUAL_ADDRESS sceneParamAddress, bool reset)
{
    if (pCmd == nullptr || scene.GetTLAS() == nullptr)
    { return; }

    // 半径をリセット.
    if (reset)
    {
        m_FrameCount    = 0;
        m_CausticRadius = m_CausticRadiusStart;
        m_GlobalRadius  = m_GlobalRadiusStart;
    }

    if (m_RebuildAS)
    {
        //CreateAS(pCmd);
    }

    // フォトンカリング実行.
    PhotonCulling(pCmd);

    // フォトン生成パス実行.
    GeneratePhoton(pCmd);

    // 検索用AccelerationStructureをビルド.
    BuildPhotonAS(pCmd);

    // フォトン収集を実行.
    CollectPhoton(pCmd);

    // 半径を更新.
    UpdateRadius();
}

//-----------------------------------------------------------------------------
//      フォトンカリングを行います.
//-----------------------------------------------------------------------------
void RTPMPass::PhotonCulling(ID3D12GraphicsCommandList4* pCmd)
{
    // カリングハッシュバッファをクリア.
    {
    }

    // 定数バッファ更新.
    {
    }

    // フォトンカリングを実行.
    {
        auto threadX = (m_Width  + 7) / 8;
        auto threadY = (m_Height + 7) / 8;

        m_PhotonCullingPipe.SetState(pCmd);
        pCmd->Dispatch(threadX, threadY, 1);
    }

    asdx::UAVBarrier(pCmd, m_CullingHashBuffer.GetResource());
}

//-----------------------------------------------------------------------------
//      フォトン生成を行います.
//-----------------------------------------------------------------------------
void RTPMPass::GeneratePhoton(ID3D12GraphicsCommandList4* pCmd)
{
    // カウンターバッファをリセット.
    {
    }

    // フォトンバッファをクリア.
    {
    }

    // フォトントレーシング.
    {


        m_GeneratePhotonPipe.DispatchRays(pCmd, m_Width, m_Height);
    }

    asdx::UAVBarrier(pCmd, m_GlobalBuffer .AABB.GetResource());
    asdx::UAVBarrier(pCmd, m_CausticBuffer.AABB.GetResource());
}

//-----------------------------------------------------------------------------
//      BLASとTLASを構築します.
//-----------------------------------------------------------------------------
void RTPMPass::BuildPhotonAS(ID3D12GraphicsCommandList4* pCmd)
{
}

//-----------------------------------------------------------------------------
//      フォトンを収集します.
//-----------------------------------------------------------------------------
void RTPMPass::CollectPhoton(ID3D12GraphicsCommandList4* pCmd)
{

    m_CollectPhotonPipe.DispatchRays(pCmd, m_Width, m_Height);
}

//-----------------------------------------------------------------------------
//      半径を更新します.
//-----------------------------------------------------------------------------
void RTPMPass::UpdateRadius()
{
    // フレーム数をカウントアップ.
    m_FrameCount++;

    auto frameCount = float(m_FrameCount);
    m_GlobalRadius  *= sqrt((frameCount + m_SPPM_AlphaGlobal)  / (frameCount + 1.0f));
    m_CausticRadius *= sqrt((frameCount + m_SPPM_AlphaCaustic) / (frameCount + 1.0f));

    m_GlobalRadius  = std::max(m_GlobalRadius,  kMinPhotonRadius);
    m_CausticRadius = std::max(m_CausticRadius, kMinPhotonRadius);
}

} // namespace r3d
