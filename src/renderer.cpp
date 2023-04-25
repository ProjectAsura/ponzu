//-----------------------------------------------------------------------------
// File : renderer.cpp
// Desc : Renderer.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <renderer.h>
#include <fnd/asdxLogger.h>
#include <fnd/asdxStopWatch.h>
#include <fnd/asdxMisc.h>
#include <process.h>
#include <fpng.h>
#include <OBJLoader.h>

#if ASDX_ENABLE_IMGUI
#include "../external/asdx12/external/imgui/imgui.h"
#endif

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 602;}
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }

#define MAX_RECURSION_DEPTH     (16)

namespace {

//-----------------------------------------------------------------------------
// Constant Values.
//-----------------------------------------------------------------------------
#include "../res/shader/Compile/TonemapVS.inc"
#include "../res/shader/Compile/TonemapCS.inc"
#include "../res/shader/Compile/RtCamp.inc"
#include "../res/shader/Compile/ModelVS.inc"
#include "../res/shader/Compile/ModelPS.inc"
#include "../res/shader/Compile/DebugPS.inc"
#include "../res/shader/Compile/DenoiserCS.inc"

static const D3D12_INPUT_ELEMENT_DESC kModelElements[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL"  , 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TANGENT" , 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT   , 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};

static_assert(sizeof(r3d::ResVertex) == sizeof(VertexOBJ), "Vertex size not matched!");

#if RTC_TARGET == RTC_DEVELOP
static const size_t     REQUEST_BIT_INDEX   = 0;
static const size_t     RELOADED_BIT_INDEX  = 1;
#endif

#ifdef ASDX_ENABLE_IMGUI
enum BUFFER_KIND
{
    BUFFER_KIND_RENDERED  = 0,
    BUFFER_KIND_ALBEDO,
    BUFFER_KIND_NORMAL,
    BUFFER_KIND_ROUGHNESS,
    BUFFER_KIND_VELOCITY,
};

enum SAMPLING_TYPE
{
    SAMPLING_TYPE_DEFAULT   = 0,    // RGBA.
    SAMPLING_TYPE_NORMAL    = 1,    // Octahedronをデコードして[0, 1]に変換.
    SAMPLING_TYPE_VELOCITY  = 2,    // [-1, 1] を[0, 1]に変換.
    SAMPLING_TYPE_R         = 3,    // (R, R, R, 1)で出力.
    SAMPLING_TYPE_G         = 4,    // (G, G, G, 1)で出力.
    SAMPLING_TYPE_B         = 5,    // (B, B, B, 1)で出力.
    SAMPLING_TYPE_A         = 6,    // (A, A, A, 1)で出力.
    SAMPLING_TYPE_HEAT_MAP  = 7,    // ヒートマップ表示.
};

static const char* kBufferKindItems[] = {
    u8"描画結果",
    u8"アルベド",
    u8"法線",
    u8"ラフネス",
    u8"速度",
};
#endif

///////////////////////////////////////////////////////////////////////////////
// Payload structure
///////////////////////////////////////////////////////////////////////////////
struct Payload
{
    asdx::Vector3   Position;
    uint32_t        MaterialId;
    asdx::Vector3   Normal;
    asdx::Vector3   Tangent;
    asdx::Vector2   TexCoord;
};

///////////////////////////////////////////////////////////////////////////////
// SceneParam structure
///////////////////////////////////////////////////////////////////////////////
struct SceneParam
{
    asdx::Matrix    View;
    asdx::Matrix    Proj;
    asdx::Matrix    InvView;
    asdx::Matrix    InvProj;
    asdx::Matrix    InvViewProj;

    asdx::Matrix    PrevView;
    asdx::Matrix    PrevProj;
    asdx::Matrix    PrevInvView;
    asdx::Matrix    PrevInvProj;
    asdx::Matrix    PrevInvViewProj;

    uint32_t        MaxBounce;
    uint32_t        MinBounce;
    uint32_t        FrameIndex;
    float           SkyIntensity;

    uint32_t        EnableAccumulation;
    uint32_t        AccumulatedFrames;
    float           ExposureAdjustment;
    uint32_t        LightCount;

    asdx::Vector4   Size;
    asdx::Vector3   CameraDir;
    uint32_t        MaxIteration;

    float           AnimationTime;
    float           Reserved[3];
};

///////////////////////////////////////////////////////////////////////////////
// HitInfo structure
///////////////////////////////////////////////////////////////////////////////
struct HitInfo
{
    asdx::Vector3   P;
    float           BsdfPdf;
    asdx::Vector3   N;
    float           LightPdf;
};

///////////////////////////////////////////////////////////////////////////////
// Sample structure
///////////////////////////////////////////////////////////////////////////////
struct Sample
{
    HitInfo         PointV;
    HitInfo         PointS;
    asdx::Vector3   Lo;
    uint32_t        Flags;
    asdx::Vector3   Wi;
    uint32_t        FrameIndex;
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
};

///////////////////////////////////////////////////////////////////////////////
// ShadeParam structure
///////////////////////////////////////////////////////////////////////////////
struct ShadeParam
{
    uint32_t    Width;
    uint32_t    Height;
    uint32_t    EnableAccumulation;
    uint32_t    AccumulationFrame;
};

///////////////////////////////////////////////////////////////////////////////
// DenoiseParam structure
///////////////////////////////////////////////////////////////////////////////
struct DenoiseParam
{
    uint32_t DispathArgsX;
    uint32_t DispathArgsY;
    float    InvTargetSizeX;
    float    InvTargetSizeY;
    float    OffsetX;
    float    OffsetY;
};

//-----------------------------------------------------------------------------
//      画像に出力します.
//-----------------------------------------------------------------------------
unsigned Export(void* args)
{
    auto data = reinterpret_cast<r3d::Renderer::ExportData*>(args);
    if (data == nullptr)
    { return -1; }

    // コピーコマンドの完了を待機.
    if (data->WaitPoint.IsValid())
    { data->pQueue->Sync(data->WaitPoint); }

    // メモリマッピング.
    uint8_t* ptr = nullptr;
    auto hr = data->pResources->Map(0, nullptr, reinterpret_cast<void**>(&ptr));
    if (FAILED(hr))
    { return -1; }

    if (fpng::fpng_encode_image_to_memory(
        ptr,
        data->Width,
        data->Height,
        4,
        data->Converted))
    {
        char path[256] = {};
        sprintf_s(path, "output_%03ld.png", data->FrameIndex);

        FILE* pFile = nullptr;
        auto err = fopen_s(&pFile, path, "wb");
        if (err == 0)
        {
            fwrite(data->Converted.data(), 1, data->Converted.size(), pFile);
            fclose(pFile);
        }
    }

    // メモリマッピング解除.
    data->pResources->Unmap(0, nullptr);

    return 0;
}

} // namespace


namespace r3d {

///////////////////////////////////////////////////////////////////////////////
// RayTracingPipe structure
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      初期化処理です.
//-----------------------------------------------------------------------------
bool Renderer::RayTracingPipe::Init
(
    ID3D12RootSignature*    pRootSig,
    const void*             binary,
    size_t                  binarySize
)
{
    auto pDevice = asdx::GetD3D12Device();

    // レイトレ用パイプラインステート生成.
    {
        D3D12_EXPORT_DESC exports[] = {
            { L"OnGenerateRay"      , nullptr, D3D12_EXPORT_FLAG_NONE },
            { L"OnClosestHit"       , nullptr, D3D12_EXPORT_FLAG_NONE },
            { L"OnShadowAnyHit"     , nullptr, D3D12_EXPORT_FLAG_NONE },
            { L"OnMiss"             , nullptr, D3D12_EXPORT_FLAG_NONE },
            { L"OnShadowMiss"       , nullptr, D3D12_EXPORT_FLAG_NONE },
        };

        D3D12_HIT_GROUP_DESC groups[2] = {};
        groups[0].ClosestHitShaderImport    = L"OnClosestHit";
        groups[0].HitGroupExport            = L"StandardHit";
        groups[0].Type                      = D3D12_HIT_GROUP_TYPE_TRIANGLES;

        groups[1].AnyHitShaderImport        = L"OnShadowAnyHit";
        groups[1].HitGroupExport            = L"ShadowHit";
        groups[1].Type                      = D3D12_HIT_GROUP_TYPE_TRIANGLES;

        asdx::RayTracingPipelineStateDesc desc = {};
        desc.pGlobalRootSignature       = pRootSig;
        desc.DXILLibrary                = { binary, binarySize };
        desc.ExportCount                = _countof(exports);
        desc.pExports                   = exports;
        desc.HitGroupCount              = _countof(groups);
        desc.pHitGroups                 = groups;
        desc.MaxPayloadSize             = sizeof(Payload);
        desc.MaxAttributeSize           = sizeof(asdx::Vector2);
        desc.MaxTraceRecursionDepth     = MAX_RECURSION_DEPTH;

        if (!PipelineState.Init(pDevice, desc))
        {
            ELOGA("Error : RayTracing PSO Failed.");
            return false;
        }
    }

    // レイ生成テーブル.
    {
        asdx::ShaderRecord record = {};
        record.ShaderIdentifier = PipelineState.GetShaderIdentifier(L"OnGenerateRay");

        asdx::ShaderTable::Desc desc = {};
        desc.RecordCount    = 1;
        desc.pRecords       = &record;

        if (!RayGen.Init(pDevice, &desc))
        {
            ELOGA("Error : RayGenTable Init Failed.");
            return false;
        }
    }

    // ミステーブル.
    {
        asdx::ShaderRecord record[2] = {};
        record[0].ShaderIdentifier = PipelineState.GetShaderIdentifier(L"OnMiss");
        record[1].ShaderIdentifier = PipelineState.GetShaderIdentifier(L"OnShadowMiss");

        asdx::ShaderTable::Desc desc = {};
        desc.RecordCount = 2;
        desc.pRecords    = record;

        if (!Miss.Init(pDevice, &desc))
        {
            ELOGA("Error : MissTable Init Failed.");
            return false;
        }
    }

    // ヒットグループ.
    {
        asdx::ShaderRecord record[2];
        record[0].ShaderIdentifier = PipelineState.GetShaderIdentifier(L"StandardHit");
        record[1].ShaderIdentifier = PipelineState.GetShaderIdentifier(L"ShadowHit");

        asdx::ShaderTable::Desc desc = {};
        desc.RecordCount = 2;
        desc.pRecords    = record;

        if (!HitGroup.Init(pDevice, &desc))
        {
            ELOGA("Error : HitGroupTable Init Failed.");
            return false;
        }
    }

    return true;
}

//-----------------------------------------------------------------------------
//      終了処理です.
//-----------------------------------------------------------------------------
void Renderer::RayTracingPipe::Term()
{
    HitGroup     .Term();
    Miss         .Term();
    RayGen       .Term();
    PipelineState.Term();
}

//-----------------------------------------------------------------------------
//      レイトレーシングパイプラインを起動します.
//-----------------------------------------------------------------------------
void Renderer::RayTracingPipe::Dispatch
(
    ID3D12GraphicsCommandList6* pCmd,
    uint32_t                    width,
    uint32_t                    height
)
{
    auto stateObject    = PipelineState.GetStateObject();
    auto rayGenTable    = RayGen  .GetRecordView();
    auto missTable      = Miss    .GetTableView();
    auto hitGroupTable  = HitGroup.GetTableView();

    D3D12_DISPATCH_RAYS_DESC desc = {};
    desc.RayGenerationShaderRecord  = rayGenTable;
    desc.MissShaderTable            = missTable;
    desc.HitGroupTable              = hitGroupTable;
    desc.Width                      = width;
    desc.Height                     = height;
    desc.Depth                      = 1;

    pCmd->SetPipelineState1(stateObject);
    pCmd->DispatchRays(&desc);
}


///////////////////////////////////////////////////////////////////////////////
// Renderer class
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      コンストラクタです.
//-----------------------------------------------------------------------------
Renderer::Renderer(const SceneDesc& desc)
: asdx::Application(L"Ponzu", 1920, 1080, nullptr, nullptr, nullptr)
, m_SceneDesc(desc)
{
    m_SwapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

#if RTC_TARGET == RTC_RELEASE
    m_DeviceDesc.EnableBreakOnError   = false;
    m_DeviceDesc.EnableBreakOnWarning = false;
    m_DeviceDesc.EnableDRED           = false;
    m_DeviceDesc.EnableDebug          = false;
    m_DeviceDesc.EnableCapture        = false;

    // 提出版はウィンドウを生成しない.
    m_CreateWindow                    = false;
#else
    m_DeviceDesc.EnableCapture        = true;
    m_DeviceDesc.EnableBreakOnWarning = false;
    m_DeviceDesc.EnableDRED           = true;
#endif
}

//-----------------------------------------------------------------------------
//      デストラクタです.
//-----------------------------------------------------------------------------
Renderer::~Renderer()
{
}

//-----------------------------------------------------------------------------
//      初期化処理を行います.
//-----------------------------------------------------------------------------
bool Renderer::OnInit()
{
    // 固有のセットアップ処理.
    if (!SystemSetup())
    { return false; }

    // シーンを構築.
    if (!BuildScene())
    { return false; }

    // 1フレームあたりの時間を算出.
    {
        auto setupTime  = m_Timer.GetRelativeSec();
        auto renderTime = m_SceneDesc.RenderTimeSec - setupTime;
        auto totalFrame = double(m_SceneDesc.FPS * m_SceneDesc.AnimationTimeSec);
        m_AnimationOneFrameTime = renderTime / totalFrame;
        m_AnimationElapsedTime  = 0.0f;

        ChangeFrame(0);
    }

    // 標準出力をフラッシュ.
    std::fflush(stdout);

    #if RTC_TARGET == RTC_DEVELOP
    // シェーダファイル監視.
    {
        auto path = asdx::ToFullPathA("../res/shader");

        asdx::FileWatcher::Desc desc = {};
        desc.DirectoryPath  = path.c_str();
        desc.BufferSize     = 4096;
        desc.WaitTimeMsec   = 16;
        desc.pListener      = this;

        if (!m_ShaderWatcher.Init(desc))
        {
            ELOG("Error : ShaderWatcher Failed.");
            return false;
        }
    }
    #endif
 
    // 正常終了.
    return true;
}

//-----------------------------------------------------------------------------
//      固有のセットアップ処理を行います.
//-----------------------------------------------------------------------------
bool Renderer::SystemSetup()
{
    asdx::StopWatch timer;
    timer.Start();
    printf_s("System setup ... ");

    auto pDevice = asdx::GetD3D12Device();

    // コマンドリストをリセット.
    m_GfxCmdList.Reset();
    auto pCmd = m_GfxCmdList.GetCommandList();

    // DXRが使用可能かどうかチェック.
    if (!asdx::IsSupportDXR(pDevice))
    {
        ELOGA("Error : DirectX Ray Tracing is not supported.");
        return false;
    }

    // ShaderModel 6.6以降対応かどうかチェック.
    {
        D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = { D3D_SHADER_MODEL_6_6 };
        auto hr = pDevice->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel));
        if (FAILED(hr) || (shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_6))
        {
            ELOG("Error : Shader Model 6.6 is not supported.");
            return false;
        }
    }

    // PNGライブラリ初期化.
    fpng::fpng_init();

    m_RendererViewport.TopLeftX = 0.0f;
    m_RendererViewport.TopLeftY = 0.0f;
    m_RendererViewport.Width    = FLOAT(m_SceneDesc.Width);
    m_RendererViewport.Height   = FLOAT(m_SceneDesc.Height);
    m_RendererViewport.MinDepth = 0.0f;
    m_RendererViewport.MaxDepth = 1.0f;

    m_RendererScissor.left      = 0;
    m_RendererScissor.right     = m_SceneDesc.Width;
    m_RendererScissor.top       = 0;
    m_RendererScissor.bottom    = m_SceneDesc.Height;

    // キャプチャー用ダブルバッファ.
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_SceneDesc.Width;
        desc.Height             = m_SceneDesc.Height;
        desc.DepthOrArraySize   = 1;
        desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.MipLevels          = 1;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        for(auto i=0; i<3; ++i)
        {
            if (!m_CaptureTarget[i].Init(&desc))
            {
                ELOGA("Error : CaptureTarget[%d] Init Failed.", i);
                return false;
            }
        }

        m_CaptureTarget[0].SetName(L"CaptureTarget0");
        m_CaptureTarget[1].SetName(L"CaptureTarget1");
        m_CaptureTarget[2].SetName(L"CaptureTarget2");
    }

    // リードバックテクスチャ生成.
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Alignment          = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        desc.Width              = m_SceneDesc.Width * m_SceneDesc.Height * 4;
        desc.Height             = 1;
        desc.DepthOrArraySize   = 1;
        desc.MipLevels          = 1;
        desc.Format             = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags              = D3D12_RESOURCE_FLAG_NONE;

        D3D12_HEAP_PROPERTIES props = {};
        props.Type = D3D12_HEAP_TYPE_READBACK;

        auto hr = pDevice->CreateCommittedResource(
            &props,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(m_ReadBackTexture.GetAddress()));

        if (FAILED(hr))
        {
            ELOGA("Error : ID3D12Device::CreateCommittedResource() Failed. errcode = 0x%x", hr);
            return false;
        }

        m_ReadBackTexture->SetName(L"ReadBackTexture");

        UINT   rowCount     = 0;
        UINT64 pitchSize    = 0;
        UINT64 resSize      = 0;

        D3D12_RESOURCE_DESC dstDesc = {};
        dstDesc.Dimension           = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        dstDesc.Alignment           = 0;
        dstDesc.Width               = m_SceneDesc.Width;
        dstDesc.Height              = m_SceneDesc.Height;
        dstDesc.DepthOrArraySize    = 1;
        dstDesc.MipLevels           = 1;
        dstDesc.Format              = DXGI_FORMAT_R8G8B8A8_UNORM;
        dstDesc.SampleDesc.Count    = 1;
        dstDesc.SampleDesc.Quality  = 0;
        dstDesc.Layout              = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        dstDesc.Flags               = D3D12_RESOURCE_FLAG_NONE;

        pDevice->GetCopyableFootprints(&dstDesc,
            0,
            1,
            0,
            nullptr,
            &rowCount,
            &pitchSize,
            &resSize);

        m_ReadBackPitch = static_cast<uint32_t>((pitchSize + 255) & ~0xFFu);

        m_ExportData.resize(2);
        for(auto i=0; i<m_ExportData.size(); ++i)
        {
            m_ExportData[i].FrameIndex  = 0;
            m_ExportData[i].Width       = m_SceneDesc.Width;
            m_ExportData[i].Height      = m_SceneDesc.Height;
        }
    }

    #ifdef ASDX_ENABLE_IMGUI
    // GUI初期化.
    {
        const auto path = "../res/font/07やさしさゴシック.ttf";
        if (!asdx::GuiMgr::Instance().Init(pCmd, m_hWnd, m_Width, m_Height, m_SwapChainFormat, path))
        {
            ELOGA("Error : GuiMgr::Init() Failed.");
            return false;
        }
    }
    #endif

    // コンピュートターゲット生成.
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_SceneDesc.Width;
        desc.Height             = m_SceneDesc.Height;
        desc.DepthOrArraySize   = 1;
        desc.Format             = DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.MipLevels          = 1;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        if (!m_Radiance.Init(&desc))
        {
            ELOGA("Error : Canvas Init Failed.");
            return false;
        }

        m_Radiance.SetName(L"Radiance");
    }

    // トーンマップターゲット.
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_SceneDesc.Width;
        desc.Height             = m_SceneDesc.Height;
        desc.DepthOrArraySize   = 1;
        desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.MipLevels          = 1;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        if (!m_Tonemapped.Init(&desc))
        {
            ELOGA("Error : FinalBuffer Init Failed.");
            return false;
        }

        m_Tonemapped.SetName(L"TonemapBuffer");
    }

    // レイトレ用ルートシグニチャ生成.
    {
        auto cs = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_DESCRIPTOR_RANGE ranges[3] = {};
        asdx::InitRangeAsUAV(ranges[0], 0);
        asdx::InitRangeAsSRV(ranges[1], 4);
        asdx::InitRangeAsSRV(ranges[2], 5);

        D3D12_ROOT_PARAMETER params[8] = {};
        asdx::InitAsTable(params[0], 1, &ranges[0], cs);
        asdx::InitAsSRV  (params[1], 0, cs);
        asdx::InitAsSRV  (params[2], 1, cs);
        asdx::InitAsSRV  (params[3], 2, cs);
        asdx::InitAsSRV  (params[4], 3, cs);
        asdx::InitAsTable(params[5], 1, &ranges[1], cs);
        asdx::InitAsCBV  (params[6], 0, cs);
        asdx::InitAsTable(params[7], 1, &ranges[2], cs);

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.pParameters        = params;
        desc.NumParameters      = _countof(params);
        desc.pStaticSamplers    = asdx::GetStaticSamplers();
        desc.NumStaticSamplers  = asdx::GetStaticSamplerCounts();
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

        if (!asdx::InitRootSignature(pDevice, &desc, m_RtRootSig.GetAddress()))
        {
            ELOG("Error : RayTracing RootSignature Init Failed.");
            return false;
        }

        if (!m_RtPipe.Init(m_RtRootSig.GetPtr(), RtCamp, sizeof(RtCamp)))
        {
            ELOG("Error : RayTracingPipe Init Failed.");
            return false;
        }
    }

    // トーンマップ用ルートシグニチャ生成.
    {
        auto cs = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_DESCRIPTOR_RANGE ranges[2] = {};
        asdx::InitRangeAsSRV(ranges[0], 0);
        asdx::InitRangeAsUAV(ranges[1], 0);

        D3D12_ROOT_PARAMETER params[3] = {};
        asdx::InitAsTable(params[0], 1, &ranges[0], cs);
        asdx::InitAsCBV  (params[1], 0, cs);
        asdx::InitAsTable(params[2], 1, &ranges[1], cs);

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.pParameters   = params;
        desc.NumParameters = _countof(params);

        if (!asdx::InitRootSignature(pDevice, &desc, m_TonemapRootSig.GetAddress()))
        {
            ELOG("Error : Tonemap Root Signature Init Failed.");
            return false;
        }
    }

    // トーンマップ用パイプラインステート生成.
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = m_TonemapRootSig.GetPtr();
        desc.CS             = { TonemapCS, sizeof(TonemapCS) };

        if (!m_TonemapPipe.Init(pDevice, &desc))
        {
            ELOGA("Error : Tonemap PipelineState Init Failed.");
            return false;
        }
    }

    // ヒストリーバッファ生成.
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_SceneDesc.Width;
        desc.Height             = m_SceneDesc.Height;
        desc.DepthOrArraySize   = 1;
        desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.MipLevels          = 1;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        for(auto i=0; i<2; ++i)
        {
            if (!m_ColorHistory[i].Init(&desc))
            {
                ELOGA("Error : ColorHistory[%d] Init Failed.", i);
                return false;
            }
        }

        m_CurrHistoryIndex = 0;
        m_PrevHistoryIndex = 1;

        m_ColorHistory[0].SetName(L"ColorHistory0");
        m_ColorHistory[1].SetName(L"ColorHistory1");
    }

    // 定数バッファ初期化.
    {
        auto size = asdx::RoundUp(sizeof(SceneParam), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        if (!m_SceneParam.Init(size))
        {
            ELOGA("Error : SceneParam Init Failed.");
            return false;
        }
    }

    // アルベドバッファ.
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_SceneDesc.Width;
        desc.Height             = m_SceneDesc.Height;
        desc.DepthOrArraySize   = 1;
        desc.MipLevels          = 1;
        desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_COMMON;
        desc.ClearColor[0]      = 0.0f;
        desc.ClearColor[1]      = 0.0f;
        desc.ClearColor[2]      = 0.0f;
        desc.ClearColor[3]      = 1.0f;

        if (!m_Albedo.Init(&desc))
        {
            ELOGA("Error : Albedo Buffer Init Failed.");
            return false;
        }

        m_Albedo.SetName(L"AlbedoBuffer");
    }

    // 法線バッファ.
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_SceneDesc.Width;
        desc.Height             = m_SceneDesc.Height;
        desc.DepthOrArraySize   = 1;
        desc.MipLevels          = 1;
        desc.Format             = DXGI_FORMAT_R16G16_FLOAT;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_COMMON;
        desc.ClearColor[0]      = 0.0f;
        desc.ClearColor[1]      = 0.0f;
        desc.ClearColor[2]      = 0.0f;
        desc.ClearColor[3]      = 0.0f;

        if (!m_Normal.Init(&desc))
        {
            ELOGA("Error : NormalBuffer Init Failed.");
            return false;
        }

        m_Normal.SetName(L"NormalBuffer");
    }

    // ラフネスバッファ
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_SceneDesc.Width;
        desc.Height             = m_SceneDesc.Height;
        desc.DepthOrArraySize   = 1;
        desc.MipLevels          = 1;
        desc.Format             = DXGI_FORMAT_R8_UNORM;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_COMMON;
        desc.ClearColor[0]      = 0.0f;
        desc.ClearColor[1]      = 0.0f;
        desc.ClearColor[2]      = 0.0f;
        desc.ClearColor[3]      = 0.0f;

        if (!m_Roughness.Init(&desc))
        {
            ELOGA("Error : Roughness Buffer Init Failed.");
            return false;
        }

        m_Roughness.SetName(L"RoughnessBuffer");
    }

    // 速度バッファ
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_SceneDesc.Width;
        desc.Height             = m_SceneDesc.Height;
        desc.DepthOrArraySize   = 1;
        desc.MipLevels          = 1;
        desc.Format             = DXGI_FORMAT_R16G16_FLOAT;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_COMMON;
        desc.ClearColor[0]      = 0.0f;
        desc.ClearColor[1]      = 0.0f;
        desc.ClearColor[2]      = 0.0f;
        desc.ClearColor[3]      = 0.0f;

        if (!m_Velocity.Init(&desc))
        {
            ELOGA("Error : Velocity Buffer Init Failed.");
            return false;
        }

        m_Velocity.SetName(L"VelocityBuffer");
    }

    // 深度ターゲット生成.
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_SceneDesc.Width;
        desc.Height             = m_SceneDesc.Height;
        desc.DepthOrArraySize   = 1;
        desc.MipLevels          = 1;
        desc.Format             = DXGI_FORMAT_D32_FLOAT;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        desc.ClearDepth         = 1.0f;
        desc.ClearStencil       = 0;

        if (!m_Depth.Init(&desc))
        {
            ELOGA("Error : Depth Buffer Init Failed.");
            return false;
        }

        m_Depth.SetName(L"DepthBuffer");
    }

    // G-Buffer ルートシグニチャ生成.
    {
        auto vs    = D3D12_SHADER_VISIBILITY_VERTEX;
        auto ps    = D3D12_SHADER_VISIBILITY_PIXEL;
        auto vs_ps = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_PARAMETER params[5] = {};
        asdx::InitAsCBV      (params[0], 0, vs);
        asdx::InitAsConstants(params[1], 1, 1, vs_ps);
        asdx::InitAsSRV      (params[2], 0, vs);
        asdx::InitAsSRV      (params[3], 1, ps);
        asdx::InitAsSRV      (params[4], 2, ps);

        auto flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.pParameters        = params;
        desc.NumParameters      = _countof(params);
        desc.pStaticSamplers    = asdx::GetStaticSamplers();
        desc.NumStaticSamplers  = asdx::GetStaticSamplerCounts();
        desc.Flags              = flags;

        if (!asdx::InitRootSignature(pDevice, &desc, m_ModelRootSig.GetAddress()))
        {
            ELOG("Error : Model Root Signature Init Failed.");
            return false;
        }
    }

    // G-Bufferパイプラインステート生成.
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature                 = m_ModelRootSig.GetPtr();
        desc.VS                             = { ModelVS, sizeof(ModelVS) };
        desc.PS                             = { ModelPS, sizeof(ModelPS) };
        desc.BlendState                     = asdx::BLEND_DESC(asdx::BLEND_STATE_OPAQUE);
        desc.DepthStencilState              = asdx::DEPTH_STENCIL_DESC(asdx::DEPTH_STATE_DEFAULT);
        desc.RasterizerState                = asdx::RASTERIZER_DESC(asdx::RASTERIZER_STATE_CULL_NONE);
        desc.SampleMask                     = D3D12_DEFAULT_SAMPLE_MASK;
        desc.PrimitiveTopologyType          = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets               = 4;
        desc.RTVFormats[0]                  = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;  // Albedo
        desc.RTVFormats[1]                  = DXGI_FORMAT_R16G16_FLOAT;         // Normal.
        desc.RTVFormats[2]                  = DXGI_FORMAT_R8_UNORM;             // Roughness.
        desc.RTVFormats[3]                  = DXGI_FORMAT_R16G16_FLOAT;         // Velocity.
        desc.DSVFormat                      = DXGI_FORMAT_D32_FLOAT;            // Depth.
        desc.InputLayout.NumElements        = _countof(kModelElements);
        desc.InputLayout.pInputElementDescs = kModelElements;
        desc.SampleDesc.Count               = 1;
        desc.SampleDesc.Quality             = 0;

        if (!m_ModelPipe.Init(pDevice, &desc))
        {
            ELOGA("Error : PipelineState Failed.");
            return false;
        }
    }

    #if RTC_TARGET == RTC_DEVELOP
    // デバッグ用ルートシグニチャ生成.
    {
        auto ps = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_DESCRIPTOR_RANGE range = {};
        asdx::InitRangeAsSRV(range, 0);

        D3D12_ROOT_PARAMETER param[2] = {};
        asdx::InitAsTable    (param[0], 1, &range, ps);
        asdx::InitAsConstants(param[1], 0, 1, ps);

        auto flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.pParameters        = param;
        desc.NumParameters      = _countof(param);
        desc.pStaticSamplers    = asdx::GetStaticSamplers();
        desc.NumStaticSamplers  = asdx::GetStaticSamplerCounts();
        desc.Flags              = flags;

        if (!asdx::InitRootSignature(pDevice, &desc, m_DebugRootSig.GetAddress()))
        {
            ELOG("Error : Copy Root Signature Init Failed.");
            return false;
        }
    }

    // デバッグ用パイプラインステート生成.
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature         = m_DebugRootSig.GetPtr();
        desc.VS                     = { TonemapVS, sizeof(TonemapVS) };
        desc.PS                     = { DebugPS, sizeof(DebugPS) };
        desc.BlendState             = asdx::BLEND_DESC(asdx::BLEND_STATE_OPAQUE);
        desc.DepthStencilState      = asdx::DEPTH_STENCIL_DESC(asdx::DEPTH_STATE_NONE);
        desc.RasterizerState        = asdx::RASTERIZER_DESC(asdx::RASTERIZER_STATE_CULL_NONE);
        desc.SampleMask             = D3D12_DEFAULT_SAMPLE_MASK;
        desc.PrimitiveTopologyType  = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets       = 1;
        desc.RTVFormats[0]          = m_SwapChainFormat;
        desc.DSVFormat              = DXGI_FORMAT_UNKNOWN;
        desc.InputLayout            = asdx::GetQuadLayout();
        desc.SampleDesc.Count       = 1;
        desc.SampleDesc.Quality     = 0;

        if (!m_DebugPipe.Init(pDevice, &desc))
        {
            ELOGA("Error : Debug PipelineState Failed.");
            return false;
        }
    }
    #endif

#if RTC_TARGET == RTC_DEVELOP
    // カメラ初期化.
    {
        auto pos    = asdx::Vector3(0.0f, 0.0f, 300.5f);
        auto target = asdx::Vector3(0.0f, 0.0f, 0.0f);
        auto upward = asdx::Vector3(0.0f, 1.0f, 0.0f);
        m_AppCamera.Init(pos, target, upward, 0.1f, 10000.0f);
    }

    // 初回フレーム計算用に設定しておく.
    {
        auto fovY   = asdx::ToRadian(37.5f);
        auto aspect = float(m_SceneDesc.Width) / float(m_SceneDesc.Height);

        auto view = m_AppCamera.GetView();
        auto proj = asdx::Matrix::CreatePerspectiveFieldOfView(
            fovY,
            aspect,
            m_AppCamera.GetNearClip(),
            m_AppCamera.GetFarClip());

        m_PrevView          = view;
        m_PrevProj          = proj;
        m_PrevInvView       = asdx::Matrix::Invert(view);
        m_PrevInvProj       = asdx::Matrix::Invert(proj);
        m_PrevInvViewProj   = m_PrevInvProj * m_PrevInvView;
    }
#else
    // カメラ初期化.
    {
        //m_Camera.SetPosition(asdx::Vector3(-1425.195f, 1018.635f, 1710.176f));
        //m_Camera.SetTarget(asdx::Vector3(-64.474f, 86.644f, 370.234f));
        //m_Camera.SetUpward(asdx::Vector3(0.313f, 0.899f, -0.308f));
        //m_Camera.Update();
        asdx::Camera::Param param;
        param.Position = asdx::Vector3(-1325.207520, 995.419373, 1612.109253);
        param.Target   = asdx::Vector3(56.435787, -34.221855, 128.971191);
        param.Upward   = asdx::Vector3(0.308701, 0.891568, -0.331378);
        param.Rotate   = asdx::Vector2(2.391608, -0.470002);
        param.PanTilt  = asdx::Vector2(2.391608, -0.470002);
        param.Twist    = 0.000000;
        param.MinDist  = 0.100000;
        param.MaxDist  = 10000.000000;
        m_Camera.SetParam(param);
        m_Camera.Update();
    }

    // 初回フレーム計算用に設定しておく.
    {
        auto fovY   = asdx::ToRadian(37.5f);
        auto aspect = float(m_SceneDesc.Width) / float(m_SceneDesc.Height);

        auto view = m_Camera.GetView();
        auto proj = asdx::Matrix::CreatePerspectiveFieldOfView(
            fovY,
            aspect,
            0.1f,
            10000.0f);

        m_PrevView          = view;
        m_PrevProj          = proj;
        m_PrevInvView       = asdx::Matrix::Invert(view);
        m_PrevInvProj       = asdx::Matrix::Invert(proj);
        m_PrevInvViewProj   = m_PrevInvProj * m_PrevInvView;
    }
#endif

    timer.End();
    printf_s("done! --- %lf[msec]\n", timer.GetElapsedMsec());

    return true;
}

//-----------------------------------------------------------------------------
//      シーンを構築します.
//-----------------------------------------------------------------------------
bool Renderer::BuildScene()
{
    asdx::StopWatch timer;
    timer.Start();
    printf_s("Build scene  ... ");

#if RTC_TARGET == RTC_DEVELOP
    if (!BuildTestScene())
    {
        return false;
    }
#else
    // シーン構築.
    {
        std::string path;
        if (!asdx::SearchFilePathA(m_SceneDesc.Path, path))
        {
            ELOGA("Error : File Not Found. path = %s", path);
            return false;
        }

        if (!m_Scene.Init(path.c_str(), m_GfxCmdList))
        {
            ELOGA("Error : Scene::Init() Failed.");
            return false;
        }

        ILOGA("Scene Binary Loaded.");
    }
#endif

    // セットアップコマンド実行.
    {
        // コマンド記録終了.
        auto pCmd = m_GfxCmdList.GetCommandList();
        pCmd->Close();

        ID3D12CommandList* pCmds[] = {
            pCmd
        };

        auto pGraphicsQueue = asdx::GetGraphicsQueue();

        // コマンドを実行.
        pGraphicsQueue->Execute(_countof(pCmds), pCmds);

        // 待機点を発行.
        m_FrameWaitPoint = pGraphicsQueue->Signal();

        // 完了を待機.
        pGraphicsQueue->Sync(m_FrameWaitPoint);
    }

    timer.End();
    printf_s("done! --- %lf[msec]\n", timer.GetElapsedMsec());

    return true;
}

//-----------------------------------------------------------------------------
//      終了処理を行います.
//-----------------------------------------------------------------------------
void Renderer::OnTerm()
{
    asdx::StopWatch timer;
    timer.Start();

    #ifdef ASDX_ENABLE_IMGUI
    asdx::GuiMgr::Instance().Term();
    #endif

    #if RTC_TARGET == RTC_DEVELOP
    // 開発用関連.
    {
        m_DevPipe       .Term();
        m_ShaderWatcher .Term();
        m_DebugPipe     .Term();
        m_DebugRootSig  .Reset();
    }
    #endif

    m_ReadBackTexture.Reset();

    // シーン関連.
    {
        m_Scene.Term();
    }

    // レンダーターゲット関連.
    {
        for(auto i=0; i<3; ++i)
        { m_CaptureTarget[i].Term(); }

        for(auto i=0; i<2; ++i)
        {
            m_ColorHistory[i].Term();
        }

        m_Tonemapped.Term();
        m_Depth     .Term();
        m_Velocity  .Term();
        m_Roughness .Term();
        m_Normal    .Term();
        m_Albedo    .Term();
        m_Radiance  .Term();
    }

    // 出力データ関連.
    {
        for(auto i=0; i<m_ExportData.size(); ++i)
        {
            m_ExportData[i].Converted.clear();
        }
        m_ExportData.clear();
    }

    // 定数バッファ関連.
    {
        m_SceneParam.Term();
    }

    // パイプライン関連.
    {
        m_TonemapPipe.Term();
        m_ModelPipe  .Term();
        m_RtPipe     .Term();
    }

    // ルートシグニチャ関連.
    {
        m_TonemapRootSig.Reset();
        m_RtRootSig     .Reset();
        m_ModelRootSig  .Reset();
    }
 
    timer.End();
    printf_s("Terminate Process ... done! %lf[msec]\n", timer.GetElapsedMsec());
    printf_s("Total Time        ... %lf[sec]\n", m_Timer.GetRelativeSec());
}

//-----------------------------------------------------------------------------
//      フレーム遷移時の処理です.
//-----------------------------------------------------------------------------
void Renderer::OnFrameMove(asdx::FrameEventArgs& args)
{
#if RTC_TARGET == RTC_RELEASE
    // 制限時間を超えた
    if (args.Time >= m_SceneDesc.RenderTimeSec)
    {
        // キャプチャー実行.
        uint32_t totalFrame = uint32_t(m_SceneDesc.FPS * m_SceneDesc.AnimationTimeSec);
        if (m_CaptureIndex <= totalFrame)
        {
            auto idx = (m_CaptureTargetIndex + 2) % 3; // 2フレーム前のインデックス.
            CaptureScreen(m_CaptureTarget[idx].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
        }

        PostQuitMessage(0);
        m_EndRequest = true;

        return;
    }

    m_ForceChanged = false;

    // CPUで読み取り.
    m_AnimationElapsedTime += args.ElapsedTime;
    if (m_AnimationElapsedTime >= m_AnimationOneFrameTime && GetFrameCount() > 0)
    {
        // キャプチャー実行.
        auto idx = (m_CaptureTargetIndex + 2) % 3; // 2フレーム前のインデックス.
        assert(m_CaptureTarget[idx].GetState() == D3D12_RESOURCE_STATE_COPY_SOURCE);
        CaptureScreen(m_CaptureTarget[idx].GetResource());

        // 次のフレームに切り替え.
        m_AnimationElapsedTime = 0.0;
        ChangeFrame(m_CaptureIndex);

        // 適宜調整する.
        auto totalFrame = double(m_SceneDesc.FPS * m_SceneDesc.AnimationTimeSec - (m_CaptureIndex - 1));
        m_AnimationOneFrameTime = (m_SceneDesc.RenderTimeSec - m_Timer.GetRelativeSec()) / totalFrame;
    }
#else
    ChangeFrame(GetFrameCount());
#endif
}

//-----------------------------------------------------------------------------
//      アニメーション用の変更処理です.
//-----------------------------------------------------------------------------
void Renderer::ChangeFrame(uint32_t index)
{
    m_PrevView          = m_CurrView;
    m_PrevProj          = m_CurrProj;
    m_PrevInvView       = m_CurrInvView;
    m_PrevInvViewProj   = m_CurrInvProj * m_CurrInvView;

#if RTC_TARGET == RTC_RELEASE
    //asdx::CameraEvent camEvent = {};
    //camEvent.Flags |= asdx::CameraEvent::EVENT_ROTATE;
    //camEvent.Rotate.x += 2.0f * asdx::F_PI / 600.0f; 

    //m_Camera.UpdateByEvent(camEvent);

    if (m_MyFrameCount >= 200 && m_MyFrameCount < 400)
    {
        asdx::Camera::Param param;
        param.Position = asdx::Vector3(-8698.803711, 166.165710, 13402.062500);
        param.Target   = asdx::Vector3(-6625.559082, 825.442749, 11306.008789);
        param.Upward   = asdx::Vector3(-0.153466, 0.975897, 0.155155);
        param.Rotate   = asdx::Vector2(2.361665, 0.220002);
        param.PanTilt  = asdx::Vector2(2.361665, 0.220002);
        param.Twist    = 0.000000;
        param.MinDist  = 0.100000;
        param.MaxDist  = 10000.000000;

        m_Camera.SetParam(param);
        m_Camera.Update();
    }
    else if (m_MyFrameCount >= 400)
    {
        asdx::Camera::Param param;
        param.Position = asdx::Vector3(-6236.219238, 643.296875, 12190.804688);
        param.Target   = asdx::Vector3(-6761.750977, 363.141693, 11173.848633);
        param.Upward   = asdx::Vector3(-0.109136, 0.971333, -0.211189);
        param.Rotate   = asdx::Vector2(3.618566, -0.240019);
        param.PanTilt  = asdx::Vector2(3.618566, -0.240019);
        param.Twist    = 0.000000;
        param.MinDist  = 0.100000;
        param.MaxDist  = 10000.000000;

        m_Camera.SetParam(param);
        m_Camera.Update();
    }


    m_CurrView = m_Camera.GetView();
    m_CurrProj = asdx::Matrix::CreatePerspectiveFieldOfView(
        asdx::ToRadian(37.5f),
        float(m_SceneDesc.Width) / float(m_SceneDesc.Height),
        0.1f,
        10000.0f);
    m_CameraZAxis = m_Camera.GetAxisZ();

    if (m_MyFrameCount < 400)
    {
        m_AnimationTime += 1.0f / 30.0f;
        m_ForceChanged = true;
    }

    m_MyFrameCount++;
#else
    m_CurrView = m_AppCamera.GetView();
    m_CurrProj = asdx::Matrix::CreatePerspectiveFieldOfView(
        asdx::ToRadian(37.5f),
        float(m_SceneDesc.Width) / float(m_SceneDesc.Height),
        m_AppCamera.GetNearClip(),
        m_AppCamera.GetFarClip());
    m_CameraZAxis = m_AppCamera.GetAxisZ();

    if (!m_ForceAccumulationOff)
    {
        m_AnimationTime = float(m_Timer.GetRelativeSec());
    }
#endif

    m_CurrInvView = asdx::Matrix::Invert(m_CurrView);
    m_CurrInvProj = asdx::Matrix::Invert(m_CurrProj);


    // 定数バッファ更新.
    {
        auto enableAccumulation = true;

        auto changed = memcmp(&m_CurrView, &m_PrevView, sizeof(asdx::Matrix)) != 0;

        if (GetFrameCount() == 0)
        { changed = true; }

    //#if RTC_TARGET == RTC_DEVELOP
    //    if (m_RequestReload)
    //    { changed = true; }
    //#endif

    #if RTC_TARGET == RTC_RELEASE
        if (m_ForceChanged)
        { changed = true; }
    #endif

        // カメラ変更があったかどうか?
        if (changed)
        {
            enableAccumulation  = false;
            m_AccumulatedFrames = 0;
        }

        m_AccumulatedFrames++;

        SceneParam param = {};
        param.View                  = m_CurrView;
        param.Proj                  = m_CurrProj;
        param.InvView               = m_CurrInvView;
        param.InvProj               = m_CurrInvProj;
        param.InvViewProj           = m_CurrInvProj * m_CurrInvView;
        param.PrevView              = m_PrevView;
        param.PrevProj              = m_PrevProj;
        param.PrevInvView           = m_PrevInvView;
        param.PrevInvProj           = m_PrevInvProj;
        param.PrevInvViewProj       = m_PrevInvViewProj;
        param.MaxBounce             = MAX_RECURSION_DEPTH;
        param.MinBounce             = 3;
        param.FrameIndex            = GetFrameCount();
        param.SkyIntensity          = 5.0f;
        param.EnableAccumulation    = enableAccumulation;
        param.AccumulatedFrames     = m_AccumulatedFrames;
        param.ExposureAdjustment    = 1.0f;
        param.LightCount            = m_Scene.GetLightCount();
        param.Size.x                = float(m_SceneDesc.Width);
        param.Size.y                = float(m_SceneDesc.Height);
        param.Size.z                = 1.0f / param.Size.x;
        param.Size.w                = 1.0f / param.Size.y;
        param.CameraDir             = m_CameraZAxis;
        param.MaxIteration          = MAX_RECURSION_DEPTH;
        param.AnimationTime         = m_AnimationTime;

        m_SceneParam.SwapBuffer();
        m_SceneParam.Update(&param, sizeof(param));
    }
}

//-----------------------------------------------------------------------------
//      フレーム描画時の処理です.
//-----------------------------------------------------------------------------
void Renderer::OnFrameRender(asdx::FrameEventArgs& args)
{
    if (m_EndRequest)
    { return; }

    auto idx = GetCurrentBackBufferIndex();

    // コマンド記録開始.
    m_GfxCmdList.Reset();
    auto pCmd = m_GfxCmdList.GetCommandList();

    // G-Buffer描画.
    {
        m_Albedo    .Transition(pCmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_Normal    .Transition(pCmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_Roughness .Transition(pCmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_Velocity  .Transition(pCmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_Depth     .Transition(pCmd, D3D12_RESOURCE_STATE_DEPTH_WRITE);

        auto pRTV0 = m_Albedo   .GetRTV();
        auto pRTV1 = m_Normal   .GetRTV();
        auto pRTV2 = m_Roughness.GetRTV();
        auto pRTV3 = m_Velocity .GetRTV();
        auto pDSV  = m_Depth    .GetDSV();

        float clearColor [4] = { 0.0f, 0.0f, 0.0f, 1.0f };

        pCmd->ClearRenderTargetView(pRTV0->GetHandleCPU(), clearColor, 0, nullptr);
        pCmd->ClearRenderTargetView(pRTV1->GetHandleCPU(), clearColor, 0, nullptr);
        pCmd->ClearRenderTargetView(pRTV2->GetHandleCPU(), clearColor, 0, nullptr);
        pCmd->ClearRenderTargetView(pRTV3->GetHandleCPU(), clearColor, 0, nullptr);
        pCmd->ClearDepthStencilView(pDSV->GetHandleCPU(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        D3D12_CPU_DESCRIPTOR_HANDLE handleRTVs[] = {
            pRTV0->GetHandleCPU(),
            pRTV1->GetHandleCPU(),
            pRTV2->GetHandleCPU(),
            pRTV3->GetHandleCPU(),
        };

        auto handleDSV = pDSV->GetHandleCPU();

        pCmd->OMSetRenderTargets(_countof(handleRTVs), handleRTVs, FALSE, &handleDSV);
        pCmd->RSSetViewports(1, &m_RendererViewport);
        pCmd->RSSetScissorRects(1, &m_RendererScissor);
        pCmd->SetGraphicsRootSignature(m_ModelRootSig.GetPtr());
        m_ModelPipe.SetState(pCmd);

        pCmd->SetGraphicsRootConstantBufferView(0, m_SceneParam.GetResource()->GetGPUVirtualAddress());
        pCmd->SetGraphicsRootShaderResourceView(2, m_Scene.GetTB()->GetResource()->GetGPUVirtualAddress());
        pCmd->SetGraphicsRootShaderResourceView(3, m_Scene.GetMB()->GetResource()->GetGPUVirtualAddress());
        pCmd->SetGraphicsRootShaderResourceView(4, m_Scene.GetIB()->GetResource()->GetGPUVirtualAddress());
        pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        m_Scene.Draw(m_GfxCmdList.GetCommandList());
    }

    // レイトレ実行.
    {
        m_Radiance.Transition(pCmd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        pCmd->SetComputeRootSignature(m_RtRootSig.GetPtr());
        pCmd->SetComputeRootDescriptorTable(0, m_Radiance.GetUAV()->GetHandleGPU());
        pCmd->SetComputeRootShaderResourceView(1, m_Scene.GetTLAS()->GetGPUVirtualAddress());
        pCmd->SetComputeRootShaderResourceView(2, m_Scene.GetIB()->GetResource()->GetGPUVirtualAddress());
        pCmd->SetComputeRootShaderResourceView(3, m_Scene.GetMB()->GetResource()->GetGPUVirtualAddress());
        pCmd->SetComputeRootShaderResourceView(4, m_Scene.GetTB()->GetResource()->GetGPUVirtualAddress());
        pCmd->SetComputeRootDescriptorTable(5, m_Scene.GetIBL()->GetHandleGPU());
        pCmd->SetComputeRootConstantBufferView(6, m_SceneParam.GetResource()->GetGPUVirtualAddress());
        pCmd->SetComputeRootDescriptorTable(7, m_Scene.GetLB()->GetHandleGPU());

        DispatchRays(pCmd);

        asdx::UAVBarrier(pCmd, m_Radiance.GetResource());
    }

    auto threadX = (m_SceneDesc.Width  + 7) / 8;
    auto threadY = (m_SceneDesc.Height + 7) / 8;

    // トーンマップ実行.
    {
        m_Radiance  .Transition(pCmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_Tonemapped.Transition(pCmd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        pCmd->SetComputeRootSignature(m_TonemapRootSig.GetPtr());
        m_TonemapPipe.SetState(pCmd);

        pCmd->SetComputeRootDescriptorTable(0, m_Radiance.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootConstantBufferView(1, m_SceneParam.GetResource()->GetGPUVirtualAddress());
        pCmd->SetComputeRootDescriptorTable(2, m_Tonemapped.GetUAV()->GetHandleGPU());

        pCmd->Dispatch(threadX, threadY, 1);

        asdx::UAVBarrier(pCmd, m_Tonemapped.GetResource());
    }

    // スワップチェインに描画.
    #if RTC_TARGET == RTC_DEVELOP
    {
        m_ColorTarget[idx].Transition(pCmd, D3D12_RESOURCE_STATE_RENDER_TARGET);

        const asdx::IShaderResourceView* pSRV = nullptr;
        uint32_t type = 0;

        switch(m_BufferKind)
        {
        case BUFFER_KIND_RENDERED:
            m_Tonemapped.Transition(pCmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            pSRV = m_Tonemapped.GetSRV();
            type = SAMPLING_TYPE_DEFAULT;
            break;

        case BUFFER_KIND_ALBEDO:
            m_Albedo.Transition(pCmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            pSRV = m_Albedo.GetSRV();
            type = SAMPLING_TYPE_DEFAULT;
            break;

        case BUFFER_KIND_NORMAL:
            m_Normal.Transition(pCmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            pSRV = m_Normal.GetSRV();
            type = SAMPLING_TYPE_NORMAL;
            break;

        case BUFFER_KIND_ROUGHNESS:
            m_Roughness.Transition(pCmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            pSRV = m_Roughness.GetSRV();
            type = SAMPLING_TYPE_R;
            break;

        case BUFFER_KIND_VELOCITY:
            m_Velocity.Transition(pCmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            pSRV = m_Velocity.GetSRV();
            type = SAMPLING_TYPE_VELOCITY;
            break;
        }
        assert(pSRV != nullptr);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = {
            m_ColorTarget[idx].GetRTV()->GetHandleCPU()
        };

        pCmd->OMSetRenderTargets(_countof(rtvs), rtvs, FALSE, nullptr);
        pCmd->RSSetViewports(1, &m_Viewport);
        pCmd->RSSetScissorRects(1, &m_ScissorRect);
        pCmd->SetGraphicsRootSignature(m_DebugRootSig.GetPtr());
        m_DebugPipe.SetState(pCmd);
        pCmd->SetGraphicsRootDescriptorTable(0, pSRV->GetHandleGPU());
        pCmd->SetGraphicsRoot32BitConstant(1, type, 0);
        asdx::DrawQuad(pCmd);

        // 2D描画.
        Draw2D();

        m_ColorTarget[idx].Transition(pCmd, D3D12_RESOURCE_STATE_PRESENT);
    }
    #endif

    // コピー可能状態に遷移させておく.
    m_CaptureTarget[m_CaptureTargetIndex].Transition(pCmd, D3D12_RESOURCE_STATE_COPY_SOURCE);

    // コマンド記録終了.
    pCmd->Close();

    ID3D12CommandList* pCmds[] = {
        pCmd
    };

    auto pGraphicsQueue = asdx::GetGraphicsQueue();

    // 前フレームの描画の完了を待機.
    if (m_FrameWaitPoint.IsValid())
    { pGraphicsQueue->Sync(m_FrameWaitPoint); }

    // コマンドを実行.
    pGraphicsQueue->Execute(_countof(pCmds), pCmds);

    // 待機点を発行.
    m_FrameWaitPoint = pGraphicsQueue->Signal();

    // 画面に表示.
    Present(1);

    // フレーム同期.
    asdx::FrameSync();

    // シェーダをリロードします.
    RTC_DEBUG_CODE(ReloadShader());

    m_CaptureTargetIndex = (m_CaptureTargetIndex + 1) % 3;
}

//-----------------------------------------------------------------------------
//      レイトレーサーを起動します.
//-----------------------------------------------------------------------------
void Renderer::DispatchRays(ID3D12GraphicsCommandList6* pCmd)
{
#if RTC_TARGET == RTC_DEVELOP
    if (m_RtShaderFlags.Get(RELOADED_BIT_INDEX))
    {
        m_DevPipe.Dispatch(pCmd, m_SceneDesc.Width, m_SceneDesc.Height);
        return;
    }
#endif
    m_RtPipe.Dispatch(pCmd, m_SceneDesc.Width, m_SceneDesc.Height);
}

//-----------------------------------------------------------------------------
//      リサイズ処理です.
//-----------------------------------------------------------------------------
void Renderer::OnResize(const asdx::ResizeEventArgs& args)
{
}

//-----------------------------------------------------------------------------
//      キー処理です.
//-----------------------------------------------------------------------------
void Renderer::OnKey(const asdx::KeyEventArgs& args)
{
#if RTC_TARGET == RTC_DEVELOP
    #ifdef ASDX_ENABLE_IMGUI
    {
        asdx::GuiMgr::Instance().OnKey(
            args.IsKeyDown, args.IsAltDown, args.KeyCode);
    }
    #endif

    m_AppCamera.OnKey(args.KeyCode, args.IsKeyDown, args.IsAltDown);

    if (args.IsKeyDown)
    {
        switch(args.KeyCode)
        {
        case VK_F7:
            {
                // シェーダを明示的にリロードします.
                // ※ VisualStudioで編集すると正しいパスが来ないため回避策.
                m_RtShaderFlags.Set(REQUEST_BIT_INDEX, true);
                m_TonemapShaderFlags.Set(REQUEST_BIT_INDEX, true);
            }
            break;

        case VK_ESCAPE:
            {
            }
            break;

        default:
            break;
        }
    }
#endif
}

//-----------------------------------------------------------------------------
//      マウス処理です.
//-----------------------------------------------------------------------------
void Renderer::OnMouse(const asdx::MouseEventArgs& args)
{
#if (!CAMP_RELEASE)
    auto isAltDown = !!(GetKeyState(VK_MENU) & 0x8000);

    #if ASDX_ENABLE_IMGUI
        if (!isAltDown)
        {
            asdx::GuiMgr::Instance().OnMouse(
                args.X, args.Y, args.WheelDelta,
                args.IsLeftButtonDown,
                args.IsMiddleButtonDown,
                args.IsRightButtonDown);
        }
    #endif

    if (isAltDown)
    {
        m_AppCamera.OnMouse(
            args.X,
            args.Y,
            args.WheelDelta,
            args.IsLeftButtonDown,
            args.IsRightButtonDown,
            args.IsMiddleButtonDown,
            args.IsSideButton1Down,
            args.IsSideButton2Down);
    }
#endif
}

//-----------------------------------------------------------------------------
//      タイピング処理です.
//-----------------------------------------------------------------------------
void Renderer::OnTyping(uint32_t keyCode)
{
#if RTC_TARGET == RTC_DEVELOP
    #ifdef ASDX_ENABLE_IMGUI
        asdx::GuiMgr::Instance().OnTyping(keyCode);
    #endif
#endif
}

//-----------------------------------------------------------------------------
//      2D描画を行います.
//-----------------------------------------------------------------------------
void Renderer::Draw2D()
{
#if RTC_TARGET == RTC_DEVELOP
    #ifdef ASDX_ENABLE_IMGUI
        asdx::GuiMgr::Instance().Update(m_Width, m_Height);

        auto pos    = m_AppCamera.GetPosition();
        auto target = m_AppCamera.GetTarget();
        auto upward = m_AppCamera.GetUpward();

        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowSize(ImVec2(250, 0));
        if (ImGui::Begin(u8"フレーム情報", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar))
        {
            ImGui::Text(u8"FPS   : %.3lf", GetFPS());
            ImGui::Text(u8"Frame : %ld", GetFrameCount());
            ImGui::Text(u8"Accum : %ld", m_AccumulatedFrames);
            ImGui::Text(u8"Camera : (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);
            ImGui::Text(u8"Target : (%.2f, %.2f, %.2f)", target.x, target.y, target.z);
            ImGui::Text(u8"Upward : (%.2f, %.2f, %.2f)", upward.x, upward.y, upward.z);
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(10, 130), ImGuiCond_Once);
        if (ImGui::Begin(u8"デバッグ設定", &m_DebugSetting))
        {
            int count = _countof(kBufferKindItems);
            ImGui::Combo(u8"ビュー", &m_BufferKind, kBufferKindItems, count);
            ImGui::Checkbox(u8"Accumulation 強制OFF", &m_ForceAccumulationOff);
            if (ImGui::Button(u8"カメラ情報出力"))
            {
                auto& param = m_AppCamera.GetParam();
                printf_s("// Camera Parameter\n");
                printf_s("asdx::Camera::Param param;\n");
                printf_s("param.Position = asdx::Vector3(%f, %f, %f);\n", param.Position.x, param.Position.y, param.Position.z);
                printf_s("param.Target   = asdx::Vector3(%f, %f, %f);\n", param.Target.x, param.Target.y, param.Target.z);
                printf_s("param.Upward   = asdx::Vector3(%f, %f, %f);\n", param.Upward.x, param.Upward.y, param.Upward.z);
                printf_s("param.Rotate   = asdx::Vector2(%f, %f);\n", param.Rotate.x, param.Rotate.y);
                printf_s("param.PanTilt  = asdx::Vector2(%f, %f);\n", param.PanTilt.x, param.PanTilt.y);
                printf_s("param.Twist    = %f;\n", param.Twist);
                printf_s("param.MinDist  = %f;\n", param.MinDist);
                printf_s("param.MaxDist  = %f;\n", param.MaxDist);
                printf_s("\n");
            }
        }
        ImGui::End();

        // 描画コマンド発行.
        asdx::GuiMgr::Instance().Draw(m_GfxCmdList.GetCommandList());
    #endif
#endif
}

//-----------------------------------------------------------------------------
//      スクリーンキャプチャーを行います.
//-----------------------------------------------------------------------------
void Renderer::CaptureScreen(ID3D12Resource* pResource)
{
    if (pResource == nullptr)
    { return; }

    auto pQueue = asdx::GetCopyQueue();
    if (pQueue == nullptr)
    { return; }

    m_CopyCmdList.Reset();
    auto pCmd = m_CopyCmdList.GetCommandList();

    // 読み戻し実行.
    {
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.Type                                = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.pResource                           = m_ReadBackTexture.GetPtr();
        dst.PlacedFootprint.Footprint.Width     = static_cast<UINT>(m_SceneDesc.Width);
        dst.PlacedFootprint.Footprint.Height    = m_SceneDesc.Height;
        dst.PlacedFootprint.Footprint.Depth     = 1;
        dst.PlacedFootprint.Footprint.RowPitch  = m_ReadBackPitch;
        dst.PlacedFootprint.Footprint.Format    = DXGI_FORMAT_R8G8B8A8_UNORM;

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.Type                = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource           = pResource;
        src.SubresourceIndex    = 0;

        D3D12_BOX box = {};
        box.left    = 0;
        box.right   = m_SceneDesc.Width;
        box.top     = 0;
        box.bottom  = m_SceneDesc.Height;
        box.front   = 0;
        box.back    = 1;

        pCmd->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
    }

    pCmd->Close();

    ID3D12CommandList* pCmds[] = {
        pCmd
    };

    // 基本的に2フレーム前のテクスチャを参照するため, 
    // 実行前の同期は完了されていることが保証されているため必要ない.

    // コマンドを実行.
    pQueue->Execute(_countof(pCmds), pCmds);

    // 待機点を発行.
    auto waitPoint = pQueue->Signal();

    auto idx = m_ExportIndex;
    {
        m_ExportData[idx].pResources = m_ReadBackTexture.GetPtr();
        m_ExportData[idx].WaitPoint  = waitPoint;
        m_ExportData[idx].pQueue     = pQueue;
        m_ExportData[idx].FrameIndex = m_CaptureIndex;

        // 別スレッドで待機とファイル出力を実行.
        _beginthreadex(nullptr, 0, Export, &m_ExportData[idx], 0, nullptr);
    }

    m_CaptureIndex++;
    m_ExportIndex = (m_ExportIndex + 1) % m_ExportData.size();
}

#if RTC_TARGET == RTC_DEVELOP
//-----------------------------------------------------------------------------
//      シェーダをコンパイルします.
//-----------------------------------------------------------------------------
bool CompileShader
(
    const wchar_t*  path,
    const char*     entryPoint,
    const char*     profile,
    asdx::IBlob**   ppBlob
)
{
    std::wstring resolvePath;

    if (!asdx::SearchFilePathW(path, resolvePath))
    {
        ELOGA("Error : File Not Found. path = %ls", path);
        return false;
    }

    std::vector<std::wstring> includeDirs;
    includeDirs.push_back(asdx::ToFullPathW(L"../external/asdx12/res/shaders"));
    includeDirs.push_back(asdx::ToFullPathW(L"../res/shader"));

    if (!asdx::CompileFromFile(resolvePath.c_str(), includeDirs, entryPoint, profile, ppBlob))
    {
        ELOGA("Error : Compile Shader Failed. path = %ls", resolvePath.c_str());
        return false;
    }

    return true;
}

//-----------------------------------------------------------------------------
//      更新ファイル対象があればフラグを立てます.
//-----------------------------------------------------------------------------
void CheckModify
(
    const char*         relativePath,
    asdx::BitFlags8&    flags,
    const char*         paths[],
    uint32_t            countPaths
)
{
    bool detect = false;

    // 更新対象に入っているかチェック.
    for(auto i=0u; i<countPaths; ++i)
    { detect |= (_stricmp(paths[i], relativePath) == 0); }

    // 対象ファイルを検出したらリロードフラグを立てる.
    if (detect)
    { flags.Set(REQUEST_BIT_INDEX, true); }
}

//-----------------------------------------------------------------------------
//      ファイル更新コールバック関数.
//-----------------------------------------------------------------------------
void Renderer::OnUpdate
(
    asdx::ACTION_TYPE actionType,
    const char* directoryPath,
    const char* relativePath
)
{
    if (actionType != asdx::ACTION_MODIFIED)
    { return; }

    // レイトレーシングシェーダ.
    {
        const char* paths[] = {
            "Math.hlsli",
            "BRDF.hlsli",
            "SceneParam.hlsli",
            "Common.hlsli"
            "RtCamp.hlsl"
        };

        CheckModify(relativePath, m_RtShaderFlags, paths, _countof(paths));
    }

    // トーンマップシェーダ.
    {
        const char* paths[] = {
            "SceneParam.hlsli",
            "Math.hlsli",
            "TonemapCS.hlsl"
        };

        CheckModify(relativePath, m_TonemapShaderFlags, paths, _countof(paths));
    }
}

//-----------------------------------------------------------------------------
//      シェーダをリロードします.
//-----------------------------------------------------------------------------
void Renderer::ReloadShader()
{
    auto pDevice = asdx::GetD3D12Device();

    auto successCount = 0;

    if (m_RtShaderFlags.Get(REQUEST_BIT_INDEX))
    {
        asdx::RefPtr<asdx::IBlob> blob;
        if (CompileShader(L"../res/shader/RtCamp.hlsl", "", "lib_6_6", blob.GetAddress()))
        {
            // リロード済みフラグを下げておく.
            m_RtShaderFlags.Set(RELOADED_BIT_INDEX, false);

            m_DevPipe.Term();

            if (m_DevPipe.Init(
                m_RtRootSig.GetPtr(),
                blob->GetBufferPointer(),
                blob->GetBufferSize()))
            {
                // リロード済みフラグを立てる.
                m_RtShaderFlags.Set(RELOADED_BIT_INDEX, true);
                successCount++;
            }
        }

        // コンパイル要求フラグを下げる.
        m_RtShaderFlags.Set(REQUEST_BIT_INDEX, false);
    }

    if (m_TonemapShaderFlags.Get(REQUEST_BIT_INDEX))
    {
        asdx::RefPtr<asdx::IBlob> blob;
        if (CompileShader(L"../res/shader/TonemapCS.hlsl", "main", "cs_6_6", blob.GetAddress()))
        {
            m_TonemapShaderFlags.Set(RELOADED_BIT_INDEX, false);

            m_TonemapPipe.ReplaceShader(
                asdx::SHADER_TYPE_CS,
                blob->GetBufferPointer(),
                blob->GetBufferSize());
            m_TonemapPipe.Rebuild();

            m_TonemapShaderFlags.Set(RELOADED_BIT_INDEX, true);
            successCount++;
        }

        // コンパイル要求フラグを下げる.
        m_TonemapShaderFlags.Set(REQUEST_BIT_INDEX, false);
    }

    if (successCount > 0) {
        // リロード完了時刻を取得.
        tm local_time = {};
        auto t   = time(nullptr);
        auto err = localtime_s( &local_time, &t );

        // 成功ログを出力.
        ILOGA("Info : Shader Reload Successs!! [%04d/%02d/%02d %02d:%02d:%02d], successCount = %u",
            local_time.tm_year + 1900,
            local_time.tm_mon + 1,
            local_time.tm_mday,
            local_time.tm_hour,
            local_time.tm_min,
            local_time.tm_sec,
            successCount);
    }
}
#endif//(!CAMP_RELEASE)

} // namespace r3d
