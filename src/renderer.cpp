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

namespace {

//-----------------------------------------------------------------------------
// Constant Values.
//-----------------------------------------------------------------------------
#include "../res/shader/Compile/TonemapVS.inc"
#include "../res/shader/Compile/TonemapCS.inc"
#include "../res/shader/Compile/RtCamp.inc"
#include "../res/shader/Compile/ModelVS.inc"
#include "../res/shader/Compile/ModelPS.inc"
#include "../res/shader/Compile/InitialSampling.inc"
#include "../res/shader/Compile/SpatialResampling.inc"
#include "../res/shader/Compile/ShadePixel.inc"
#include "../res/shader/Compile/CopyPS.inc"

static const D3D12_INPUT_ELEMENT_DESC kModelElements[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL"  , 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TANGENT" , 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT   , 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};

static_assert(sizeof(r3d::ResVertex) == sizeof(VertexOBJ), "Vertex size not matched!");

#if !(CAMP_RELEASE)
enum BUFFER_KIND
{
    BUFFER_KIND_CANVAS  = 0,
    BUFFER_KIND_ALBEDO,
    BUFFER_KIND_NORMAL,
    BUFFER_KIND_VELOCITY,
    #if ENABLE_RESTIR
    BUFFER_KIND_SPATIAL,
    BUFFER_KIND_TEMPORAL,
    #endif
};

static const char* kBufferKindItems[] = {
    u8"Canvas",
    u8"Albedo",
    u8"Normal",
    u8"Velocity",
    #if ENABLE_RESTIR
    u8"Spatial",
    u8"Temporal",
    #endif
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

//-----------------------------------------------------------------------------
//      画像に出力します.
//-----------------------------------------------------------------------------
unsigned Export(void* args)
{
    auto data = reinterpret_cast<r3d::Renderer::ExportData*>(args);
    if (data == nullptr)
    { return -1; }

    //asdx::StopWatch timer;
    //timer.Start();

    char path[256] = {};
    sprintf_s(path, "output_%03ld.png", data->FrameIndex);

    // Releaseモードで 5-28[ms]程度の揺れ.
    if (fpng::fpng_encode_image_to_memory(
        data->Pixels.data(),
        data->Width,
        data->Height,
        4,
        data->Converted,
        data->Temporary))
    {
        FILE* pFile = nullptr;
        auto err = fopen_s(&pFile, path, "wb");
        if (pFile != nullptr)
        {
            fwrite(data->Converted.data(), 1, data->Converted.size(), pFile);
            fclose(pFile);
        }
    }

    //timer.End();
    //ILOGA("timer = %lf", timer.GetElapsedMsec());

    return 0;
}

} // namespace


namespace r3d {

///////////////////////////////////////////////////////////////////////////////
// Renderer class
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      コンストラクタです.
//-----------------------------------------------------------------------------
Renderer::Renderer(const SceneDesc& desc)
: asdx::Application(L"r3d alpha 0.0", 1920, 1080, nullptr, nullptr, nullptr)
, m_SceneDesc(desc)
{
    m_SwapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

#if CAMP_RELEASE
    m_DeviceDesc.EnableBreakOnError   = false;
    m_DeviceDesc.EnableBreakOnWarning = false;
    m_DeviceDesc.EnableDRED           = false;
    m_DeviceDesc.EnableDebug          = false;
    m_DeviceDesc.EnableCapture        = false;
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
        m_AnimationElapsedTime = 0.0f;
        m_ReadBackIndex = 2;
        m_MapIndex      = 0;

        ChangeFrame(0);
    }

    // 標準出力をフラッシュ.
    std::fflush(stdout);
 
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

    m_GfxCmdList.Reset();

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

        for(auto i=0; i<3; ++i)
        {
            auto hr = pDevice->CreateCommittedResource(
                &props,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(m_ReadBackTexture[i].GetAddress()));

            if (FAILED(hr))
            {
                ELOGA("Error : ID3D12Device::CreateCommittedResource() Failed. errcode = 0x%x", hr);
                return false;
            }

            m_ExportData[i].Pixels.resize(m_SceneDesc.Width * m_SceneDesc.Height * 4);
            m_ExportData[i].FrameIndex  = 0;
            m_ExportData[i].Width       = m_SceneDesc.Width;
            m_ExportData[i].Height      = m_SceneDesc.Height;
        }

        m_ReadBackTexture[0]->SetName(L"ReadBackTexture0");
        m_ReadBackTexture[1]->SetName(L"ReadBackTexture1");
        m_ReadBackTexture[2]->SetName(L"ReadBackTexture2");

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

        m_ReadBackIndex = 0;
    }

    // フル矩形用頂点バッファの初期化.
    if (!asdx::Quad::Instance().Init(pDevice))
    {
        ELOGA("Error : Quad::Init() Failed.");
        return false;
    }

#if ASDX_ENABLE_IMGUI
    // GUI初期化.
    {
        const auto path = "../res/font/07やさしさゴシック.ttf";
        if (!asdx::GuiMgr::Instance().Init(m_GfxCmdList, m_hWnd, m_Width, m_Height, m_SwapChainFormat, path))
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

        if (!m_Canvas.Init(&desc))
        {
            ELOGA("Error : Canvas Init Failed.");
            return false;
        }

        m_Canvas.SetName(L"Canvas");
    }

    // ヒストリーバッファ.
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
        desc.InitState          = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        for(auto i=0; i<2; ++i)
        {
            if (!m_HistoryTarget[i].Init(&desc))
            {
                ELOGA("Error : HistoryTarget Init Failed.");
                return false;
            }
        }

        m_HistoryTarget[0].SetName(L"HistoryTarget0");
        m_HistoryTarget[1].SetName(L"HistoryTarget1");
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

        if (!m_TonemapBuffer.Init(&desc))
        {
            ELOGA("Error : FinalBuffer Init Failed.");
            return false;
        }

        m_TonemapBuffer.SetName(L"TonemapBuffer");
    }

    // レイトレ用ルートシグニチャ生成.
    {
        asdx::DescriptorSetLayout<7, 1> layout;
        layout.SetTableUAV(0, asdx::SV_ALL, 0);
        layout.SetSRV     (1, asdx::SV_ALL, 0);
        layout.SetSRV     (2, asdx::SV_ALL, 1);
        layout.SetSRV     (3, asdx::SV_ALL, 2);
        layout.SetSRV     (4, asdx::SV_ALL, 3);
        layout.SetTableSRV(5, asdx::SV_ALL, 4);
        layout.SetCBV     (6, asdx::SV_ALL, 0);
        layout.SetStaticSampler(0, asdx::SV_ALL, asdx::STATIC_SAMPLER_LINEAR_WRAP, 0);
        layout.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);

        if (!m_RayTracingRootSig.Init(pDevice, layout.GetDesc()))
        {
            ELOGA("Error : RayTracing RootSignature Failed.");
            return false;
        }
    }

    // レイトレ用パイプラインステート生成.
    {
        D3D12_EXPORT_DESC exports[] = {
            { L"OnGenerateRay"      , nullptr, D3D12_EXPORT_FLAG_NONE },
            { L"OnClosestHit"       , nullptr, D3D12_EXPORT_FLAG_NONE },
            { L"OnShadowClosestHit" , nullptr, D3D12_EXPORT_FLAG_NONE },
            { L"OnMiss"             , nullptr, D3D12_EXPORT_FLAG_NONE },
            { L"OnShadowMiss"       , nullptr, D3D12_EXPORT_FLAG_NONE },
        };

        D3D12_HIT_GROUP_DESC groups[2] = {};
        groups[0].ClosestHitShaderImport    = L"OnClosestHit";
        groups[0].HitGroupExport            = L"StandardHit";
        groups[0].Type                      = D3D12_HIT_GROUP_TYPE_TRIANGLES;

        groups[1].ClosestHitShaderImport    = L"OnShadowClosestHit";
        groups[1].HitGroupExport            = L"ShadowHit";
        groups[1].Type                      = D3D12_HIT_GROUP_TYPE_TRIANGLES;

        asdx::RayTracingPipelineStateDesc desc = {};
        desc.pGlobalRootSignature       = m_RayTracingRootSig.GetPtr();
        desc.DXILLibrary                = { RtCamp, sizeof(RtCamp) };
        desc.ExportCount                = _countof(exports);
        desc.pExports                   = exports;
        desc.HitGroupCount              = _countof(groups);
        desc.pHitGroups                 = groups;
        desc.MaxPayloadSize             = sizeof(Payload);
        desc.MaxAttributeSize           = sizeof(asdx::Vector2);
        desc.MaxTraceRecursionDepth     = 1;

        if (!m_RayTracingPSO.Init(pDevice, desc))
        {
            ELOGA("Error : RayTracing PSO Failed.");
            return false;
        }
    }

    // レイ生成テーブル.
    {
        asdx::ShaderRecord record = {};
        record.ShaderIdentifier = m_RayTracingPSO.GetShaderIdentifier(L"OnGenerateRay");

        asdx::ShaderTable::Desc desc = {};
        desc.RecordCount    = 1;
        desc.pRecords       = &record;

        if (!m_RayGenTable.Init(pDevice, &desc))
        {
            ELOGA("Error : RayGenTable Init Failed.");
            return false;
        }
    }

    // ミステーブル.
    {
        asdx::ShaderRecord record[2] = {};
        record[0].ShaderIdentifier = m_RayTracingPSO.GetShaderIdentifier(L"OnMiss");
        record[1].ShaderIdentifier = m_RayTracingPSO.GetShaderIdentifier(L"OnShadowMiss");

        asdx::ShaderTable::Desc desc = {};
        desc.RecordCount = 2;
        desc.pRecords    = record;

        if (!m_MissTable.Init(pDevice, &desc))
        {
            ELOGA("Error : MissTable Init Failed.");
            return false;
        }
    }

    // ヒットグループ.
    {
        asdx::ShaderRecord record[2];
        record[0].ShaderIdentifier = m_RayTracingPSO.GetShaderIdentifier(L"StandardHit");
        record[1].ShaderIdentifier = m_RayTracingPSO.GetShaderIdentifier(L"ShadowHit");

        asdx::ShaderTable::Desc desc = {};
        desc.RecordCount = 2;
        desc.pRecords    = record;

        if (!m_HitGroupTable.Init(pDevice, &desc))
        {
            ELOGA("Error : HitGroupTable Init Failed.");
            return false;
        }
    }

    // トーンマップ用ルートシグニチャ生成.
    {
        asdx::DescriptorSetLayout<3, 0> layout;
        layout.SetTableSRV(0, asdx::SV_ALL, 0);
        layout.SetCBV(1, asdx::SV_ALL, 0);
        layout.SetTableUAV(2, asdx::SV_ALL, 0);

        if (!m_TonemapRootSig.Init(pDevice, layout.GetDesc()))
        {
            ELOGA("Error : Tonemap RootSignature Failed.");
            return false;
        }
    }

    // トーンマップ用パイプラインステート生成.
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = m_TonemapRootSig.GetPtr();
        desc.CS             = { TonemapCS, sizeof(TonemapCS) };

        if (!m_TonemapPSO.Init(pDevice, &desc))
        {
            ELOGA("Error : Tonemap PipelineState Failed.");
            return false;
        }
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

#if ENABLE_RESTIR
    // テンポラルリザーバーバッファとスパシャルリザーバーバッファの初期化.
    {
        auto stride = sizeof(Reservoir);

        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width              = stride * m_SceneDesc.Width * m_SceneDesc.Height;
        desc.Height             = 1;
        desc.DepthOrArraySize   = 1;
        desc.Format             = DXGI_FORMAT_UNKNOWN;
        desc.MipLevels          = 1;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_COMMON;

        if (!m_TemporalReservoirBuffer.Init(&desc, uint32_t(stride)))
        {
            ELOGA("Error : TemporalReservoirBuffer Init Failed.");
            return false;
        }

        if (!m_SpatialReservoirBuffer.Init(&desc, uint32_t(stride)))
        {
            ELOGA("Error : SpatialReservoirBuffer Init Failed.");
            return false;
        }
    }

    // 初期サンプル & テンポラルリユース用パイプライン.
    {
        D3D12_SHADER_BYTECODE shader = { InitialSampling, sizeof(InitialSampling) };

        if (!InitInitialSamplingPipeline(m_InitialSampling, shader))
        {
            ELOGA("Error : Init InitialSampling Pipeline Failed.");
            return false;
        }
    }

    // スパシャルリサンプリング用パイプライン.
    {
        D3D12_SHADER_BYTECODE shader = { SpatialResampling, sizeof(SpatialResampling) };

        if (!InitSpatialSamplingPipeline(m_SpatialSampling, shader))
        {
            ELOGA("Error : Init SpatialSampling Pipeline Failed.");
            return false;
        }
    }

    // ピクセルシェード用ルートシグニチャ.
    {
        asdx::DescriptorSetLayout<3, 0> layout;
        layout.SetContants(0, asdx::SV_ALL, 4, 0);
        layout.SetSRV(1, asdx::SV_ALL, 0);
        layout.SetTableUAV(2, asdx::SV_ALL, 0);

        if (!m_ShadePixelRootSig.Init(pDevice, layout.GetDesc()))
        {
            ELOGA("Error : ShadePixel RootSignature Failed.");
            return false;
        }
    }

    // ピクセルシェード用パイプライン.
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = m_ShadePixelRootSig.GetPtr();
        desc.CS             = { ShadePixel, sizeof(ShadePixel) };

        if (!m_ShadePixelPSO.Init(pDevice, &desc))
        {
            ELOGA("Error : ShadePixel PipelineState Failed.");
            return false;
        }
    }
#endif

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

        if (!m_AlbedoTarget.Init(&desc))
        {
            ELOGA("Error : DebugColorTarget Init Failed.");
            return false;
        }

        m_GfxCmdList.BarrierTransition(
            m_AlbedoTarget.GetResource(), 0,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    // 法線バッファ.
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_SceneDesc.Width;
        desc.Height             = m_SceneDesc.Height;
        desc.DepthOrArraySize   = 1;
        desc.MipLevels          = 1;
        desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_COMMON;
        desc.ClearColor[0]      = 0.5f;
        desc.ClearColor[1]      = 0.5f;
        desc.ClearColor[2]      = 1.0f;
        desc.ClearColor[3]      = 1.0f;

        if (!m_NormalTarget.Init(&desc))
        {
            ELOGA("Error : NormalTarget Init Failed.");
            return false;
        }

        m_GfxCmdList.BarrierTransition(
            m_NormalTarget.GetResource(), 0,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
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

        if (!m_ModelDepthTarget.Init(&desc))
        {
            ELOGA("Error : DebugDepthTarget Init Failed.");
            return false;
        }
    }

    // 速度ターゲット生成.
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

        if (!m_VelocityTarget.Init(&desc))
        {
            ELOGA("Error : NormalTarget Init Failed.");
            return false;
        }

        m_GfxCmdList.BarrierTransition(
            m_VelocityTarget.GetResource(), 0,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    }

    // G-Bufferルートシグニチャ生成.
    {
        auto flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

        asdx::DescriptorSetLayout<5, 1> layout;
        layout.SetCBV     (0, asdx::SV_VS,  0);
        layout.SetContants(1, asdx::SV_ALL, 1, 1);
        layout.SetSRV     (2, asdx::SV_VS,  0);
        layout.SetSRV     (3, asdx::SV_PS,  1);
        layout.SetSRV     (4, asdx::SV_PS,  2);
        layout.SetStaticSampler(0, asdx::SV_ALL, asdx::STATIC_SAMPLER_LINEAR_WRAP, 0);
        layout.SetFlags(flags);

        if (!m_ModelRootSig.Init(pDevice, layout.GetDesc()))
        {
            ELOGA("Error : RayTracing RootSignature Failed.");
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
        desc.NumRenderTargets               = 3;
        desc.RTVFormats[0]                  = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        desc.RTVFormats[1]                  = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.RTVFormats[2]                  = DXGI_FORMAT_R16G16_FLOAT;
        desc.DSVFormat                      = DXGI_FORMAT_D32_FLOAT;
        desc.InputLayout.NumElements        = _countof(kModelElements);
        desc.InputLayout.pInputElementDescs = kModelElements;
        desc.SampleDesc.Count               = 1;
        desc.SampleDesc.Quality             = 0;

        if (!m_ModelPSO.Init(pDevice, &desc))
        {
            ELOGA("Error : PipelineState Failed.");
            return false;
        }
    }

    // コピー用ルートシグニチャ生成.
    {
        asdx::DescriptorSetLayout<1, 1> layout;
        layout.SetTableSRV(0, asdx::SV_PS, 0);
        layout.SetStaticSampler(0, asdx::SV_PS, asdx::STATIC_SAMPLER_LINEAR_CLAMP, 0);
        layout.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        if (!m_CopyRootSig.Init(pDevice, layout.GetDesc()))
        {
            ELOGA("Error : Debug RootSignature Failed.");
            return false;
        }
    }

    // コピー用パイプラインステート生成.
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature         = m_CopyRootSig.GetPtr();
        desc.VS                     = { TonemapVS, sizeof(TonemapVS) };
        desc.PS                     = { CopyPS, sizeof(CopyPS) };
        desc.BlendState             = asdx::BLEND_DESC(asdx::BLEND_STATE_OPAQUE);
        desc.DepthStencilState      = asdx::DEPTH_STENCIL_DESC(asdx::DEPTH_STATE_NONE);
        desc.RasterizerState        = asdx::RASTERIZER_DESC(asdx::RASTERIZER_STATE_CULL_NONE);
        desc.SampleMask             = D3D12_DEFAULT_SAMPLE_MASK;
        desc.PrimitiveTopologyType  = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets       = 1;
        desc.RTVFormats[0]          = m_SwapChainFormat;
        desc.DSVFormat              = DXGI_FORMAT_UNKNOWN;
        desc.InputLayout            = asdx::Quad::kInputLayout;
        desc.SampleDesc.Count       = 1;
        desc.SampleDesc.Quality     = 0;

        if (!m_CopyPSO.Init(pDevice, &desc))
        {
            ELOGA("Error : Debug PipelineState Failed.");
            return false;
        }
    }

    // TAAレンダラー初期化.
    if (!m_TaaRenderer.InitCS())
    {
        ELOGA("Error : TaaRenderer::InitCS() Failed");
        return false;
    }

#if !(CAMP_RELEASE)
    // カメラ初期化.
    {
        auto pos    = asdx::Vector3(0.0f, 0.0f, 2.5f);
        auto target = asdx::Vector3(0.0f, 0.0f, 0.0f);
        auto upward = asdx::Vector3(0.0f, 1.0f, 0.0f);
        m_CameraController.Init(pos, target, upward, 0.1f, 10000.0f);
    }

    // 初回フレーム計算用に設定しておく.
    {
        auto fovY   = asdx::ToRadian(37.5f);
        auto aspect = float(m_SceneDesc.Width) / float(m_SceneDesc.Height);

        auto view = m_CameraController.GetView();
        auto proj = asdx::Matrix::CreatePerspectiveFieldOfView(
            fovY,
            aspect,
            m_CameraController.GetNearClip(),
            m_CameraController.GetFarClip());

        m_PrevView          = view;
        m_PrevProj          = proj;
        m_PrevInvView       = asdx::Matrix::Invert(view);
        m_PrevInvProj       = asdx::Matrix::Invert(proj);
        m_PrevInvViewProj   = m_PrevInvProj * m_PrevInvView;
    }
#else
    // カメラ初期化.
    {
        m_Camera.SetPosition(asdx::Vector3(0.0f, 0.0f, 2.5f));
        m_Camera.SetTarget(asdx::Vector3(0.0f, 0.0f, 0.0f));
        m_Camera.SetUpward(asdx::Vector3(0.0f, 1.0f, 0.0f));
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
//      Initial Sampling / Temporal reuse 用パイプライン初期化.
//-----------------------------------------------------------------------------
bool Renderer::InitInitialSamplingPipeline(RtPipeline& value, D3D12_SHADER_BYTECODE shader)
{
    auto pDevice = asdx::GetD3D12Device();

    // レイトレ用ルートシグニチャ生成.
    {
        asdx::DescriptorSetLayout<7, 1> layout;
        // 共通.
        layout.SetCBV(0, asdx::SV_ALL, 0);
        layout.SetSRV(1, asdx::SV_ALL, 0);
        layout.SetSRV(2, asdx::SV_ALL, 1);
        layout.SetSRV(3, asdx::SV_ALL, 2);
        layout.SetSRV(4, asdx::SV_ALL, 3);
        layout.SetStaticSampler(0, asdx::SV_ALL, asdx::STATIC_SAMPLER_LINEAR_WRAP, 0);
        layout.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);

        // 個別.
        layout.SetUAV     (5, asdx::SV_ALL, 0);  // TemporalReservoirBuffer.
        layout.SetTableSRV(6, asdx::SV_ALL, 4);  // BackGround

        if (!value.RootSig.Init(pDevice, layout.GetDesc()))
        {
            ELOGA("Error : RayTracing RootSignature Failed.");
            return false;
        }
    }

    // レイトレ用パイプラインステート生成.
    {
        D3D12_EXPORT_DESC exports[] = {
            { L"OnGenerateRay"      , nullptr, D3D12_EXPORT_FLAG_NONE },
            { L"OnClosestHit"       , nullptr, D3D12_EXPORT_FLAG_NONE },
            { L"OnShadowClosestHit" , nullptr, D3D12_EXPORT_FLAG_NONE },
            { L"OnMiss"             , nullptr, D3D12_EXPORT_FLAG_NONE },
            { L"OnShadowMiss"       , nullptr, D3D12_EXPORT_FLAG_NONE },
        };

        D3D12_HIT_GROUP_DESC groups[2] = {};
        groups[0].ClosestHitShaderImport    = L"OnClosestHit";
        groups[0].HitGroupExport            = L"StandardHit";
        groups[0].Type                      = D3D12_HIT_GROUP_TYPE_TRIANGLES;

        groups[1].ClosestHitShaderImport    = L"OnShadowClosestHit";
        groups[1].HitGroupExport            = L"ShadowHit";
        groups[1].Type                      = D3D12_HIT_GROUP_TYPE_TRIANGLES;

        asdx::RayTracingPipelineStateDesc desc = {};
        desc.pGlobalRootSignature       = value.RootSig.GetPtr();
        desc.DXILLibrary                = shader;
        desc.ExportCount                = _countof(exports);
        desc.pExports                   = exports;
        desc.HitGroupCount              = _countof(groups);
        desc.pHitGroups                 = groups;
        desc.MaxPayloadSize             = sizeof(Payload);
        desc.MaxAttributeSize           = sizeof(asdx::Vector2);
        desc.MaxTraceRecursionDepth     = 1;

        if (!value.PSO.Init(pDevice, desc))
        {
            ELOGA("Error : RayTracing PSO Failed.");
            return false;
        }
    }

    // レイ生成テーブル.
    {
        asdx::ShaderRecord record = {};
        record.ShaderIdentifier = value.PSO.GetShaderIdentifier(L"OnGenerateRay");

        asdx::ShaderTable::Desc desc = {};
        desc.RecordCount    = 1;
        desc.pRecords       = &record;

        if (!value.RayGenTable.Init(pDevice, &desc))
        {
            ELOGA("Error : RayGenTable Init Failed.");
            return false;
        }
    }

    // ミステーブル.
    {
        asdx::ShaderRecord record[2] = {};
        record[0].ShaderIdentifier = value.PSO.GetShaderIdentifier(L"OnMiss");
        record[1].ShaderIdentifier = value.PSO.GetShaderIdentifier(L"OnShadowMiss");

        asdx::ShaderTable::Desc desc = {};
        desc.RecordCount = 2;
        desc.pRecords    = record;

        if (!value.MissTable.Init(pDevice, &desc))
        {
            ELOGA("Error : MissTable Init Failed.");
            return false;
        }
    }

    // ヒットグループ.
    {
        asdx::ShaderRecord record[2];
        record[0].ShaderIdentifier = value.PSO.GetShaderIdentifier(L"StandardHit");
        record[1].ShaderIdentifier = value.PSO.GetShaderIdentifier(L"ShadowHit");

        asdx::ShaderTable::Desc desc = {};
        desc.RecordCount = 2;
        desc.pRecords    = record;

        if (!value.HitGroupTable.Init(pDevice, &desc))
        {
            ELOGA("Error : HitGroupTable Init Failed.");
            return false;
        }
    }

    return true;
}

//-----------------------------------------------------------------------------
//      Spatial Sampling 用パイプライン初期化.
//-----------------------------------------------------------------------------
bool Renderer::InitSpatialSamplingPipeline(RtPipeline& value, D3D12_SHADER_BYTECODE shader)
{
    auto pDevice = asdx::GetD3D12Device();

    // レイトレ用ルートシグニチャ生成.
    {
        asdx::DescriptorSetLayout<7, 1> layout;

        // 共通.
        layout.SetCBV(0, asdx::SV_ALL, 0);
        layout.SetSRV(1, asdx::SV_ALL, 0);
        layout.SetSRV(2, asdx::SV_ALL, 1);
        layout.SetSRV(3, asdx::SV_ALL, 2);
        layout.SetSRV(4, asdx::SV_ALL, 3);
        layout.SetStaticSampler(0, asdx::SV_ALL, asdx::STATIC_SAMPLER_LINEAR_WRAP, 0);
        layout.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);

        // 個別.
        layout.SetUAV(5, asdx::SV_ALL, 0);  // SpatialReservoirSample.
        layout.SetSRV(6, asdx::SV_ALL, 4);  // TemporalReservoirBuffer

        if (!value.RootSig.Init(pDevice, layout.GetDesc()))
        {
            ELOGA("Error : RayTracing RootSignature Failed.");
            return false;
        }
    }

    // レイトレ用パイプラインステート生成.
    {
        D3D12_EXPORT_DESC exports[] = {
            { L"OnGenerateRay"      , nullptr, D3D12_EXPORT_FLAG_NONE },
            { L"OnShadowClosestHit" , nullptr, D3D12_EXPORT_FLAG_NONE },
            { L"OnShadowMiss"       , nullptr, D3D12_EXPORT_FLAG_NONE },
        };

        D3D12_HIT_GROUP_DESC groups = {};
        groups.ClosestHitShaderImport    = L"OnShadowClosestHit";
        groups.HitGroupExport            = L"ShadowHit";
        groups.Type                      = D3D12_HIT_GROUP_TYPE_TRIANGLES;

        asdx::RayTracingPipelineStateDesc desc = {};
        desc.pGlobalRootSignature       = value.RootSig.GetPtr();
        desc.DXILLibrary                = shader;
        desc.ExportCount                = _countof(exports);
        desc.pExports                   = exports;
        desc.HitGroupCount              = 1;
        desc.pHitGroups                 = &groups;
        desc.MaxPayloadSize             = sizeof(Payload);
        desc.MaxAttributeSize           = sizeof(asdx::Vector2);
        desc.MaxTraceRecursionDepth     = 1;

        if (!value.PSO.Init(pDevice, desc))
        {
            ELOGA("Error : RayTracing PSO Failed.");
            return false;
        }
    }

    // レイ生成テーブル.
    {
        asdx::ShaderRecord record = {};
        record.ShaderIdentifier = value.PSO.GetShaderIdentifier(L"OnGenerateRay");

        asdx::ShaderTable::Desc desc = {};
        desc.RecordCount    = 1;
        desc.pRecords       = &record;

        if (!value.RayGenTable.Init(pDevice, &desc))
        {
            ELOGA("Error : RayGenTable Init Failed.");
            return false;
        }
    }

    // ミステーブル.
    {
        asdx::ShaderRecord record = {};
        record.ShaderIdentifier = value.PSO.GetShaderIdentifier(L"OnShadowMiss");

        asdx::ShaderTable::Desc desc = {};
        desc.RecordCount = 1;
        desc.pRecords    = &record;

        if (!value.MissTable.Init(pDevice, &desc))
        {
            ELOGA("Error : MissTable Init Failed.");
            return false;
        }
    }

    // ヒットグループ.
    {
        asdx::ShaderRecord record;
        record.ShaderIdentifier = value.PSO.GetShaderIdentifier(L"ShadowHit");

        asdx::ShaderTable::Desc desc = {};
        desc.RecordCount = 1;
        desc.pRecords    = &record;

        if (!value.HitGroupTable.Init(pDevice, &desc))
        {
            ELOGA("Error : HitGroupTable Init Failed.");
            return false;
        }
    }

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

#if 1
    auto pDevice = asdx::GetD3D12Device();

    Material dummy0 = {};
    dummy0.Normal    = INVALID_MATERIAL_MAP;
    dummy0.BaseColor = INVALID_MATERIAL_MAP;
    dummy0.ORM       = INVALID_MATERIAL_MAP;
    dummy0.Emissive  = INVALID_MATERIAL_MAP;
    dummy0.IntIor    = 1.4f;
    dummy0.ExtIor    = 1.0f;
    dummy0.UvScale   = asdx::Vector2(1.0f, 1.0f);

    Material dummy1 = {};
    dummy1.Normal    = INVALID_MATERIAL_MAP;
    dummy1.BaseColor = 0;
    dummy1.ORM       = INVALID_MATERIAL_MAP;
    dummy1.Emissive  = INVALID_MATERIAL_MAP;
    //dummy0.IntIor    = 1.4f;
    //dummy0.ExtIor    = 1.0f;
    dummy1.UvScale   = asdx::Vector2(10.0f, 10.0f);

    std::vector<r3d::Mesh> meshes;
    if (!LoadMesh("../res/model/dragon.obj", meshes))
    {
        ELOGA("Error : LoadMesh() Failed.");
        return false;
    }

    std::vector<r3d::CpuInstance> instances;
    instances.resize(meshes.size());

    for(size_t i=0; i<meshes.size(); ++i)
    {
        instances[i].MaterialId = 0;
        instances[i].MeshId     = 0;
        instances[i].Transform  = asdx::Transform3x4();
    }

    SceneExporter exporter;
    exporter.SetIBL("../res/ibl/studio_garden_2k.dds");
    exporter.AddTexture("../res/texture/floor_tiles_08_diff_2k.dds");
    exporter.AddMeshes(meshes);
    exporter.AddMaterial(dummy0);
    exporter.AddMaterial(dummy1);
    exporter.AddInstances(instances);

    const char* exportPath = "../res/scene/rtcamp.scn";

    if (!exporter.Export(exportPath))
    {
        ELOGA("Error : SceneExporter::Export() Failed.");
        return false;
    }

    // シーン構築.
    {
        if (!m_Scene.Init(exportPath, m_GfxCmdList))
        {
            ELOGA("Error : Scene::Init() Failed.");
            return false;
        }
    }

    //// IBL読み込み.
    //{
    //    std::string path;
    //    if (!asdx::SearchFilePathA("../res/ibl/studio_garden_2k.dds", path))
    //    {
    //        ELOGA("Error : IBL File Not Found.");
    //        return false;
    //    }

    //    asdx::ResTexture res;
    //    if (!res.LoadFromFileA(path.c_str()))
    //    {
    //        ELOGA("Error : IBL Load Failed.");
    //        return false;
    //    }

    //    if (!m_IBL.Init(m_GfxCmdList, res))
    //    {
    //        ELOGA("Error : IBL Init Failed.");
    //        return false;
    //    }
    //}

    //// テクスチャ.
    //{
    //    std::string path;
    //    if (!asdx::SearchFilePathA("../res/texture/floor_tiles_08_diff_2k.dds", path))
    //    {
    //        ELOGA("Error : Texture Not Found.");
    //        return false;
    //    }

    //    asdx::ResTexture res;
    //    if (!res.LoadFromFileA(path.c_str()))
    //    {
    //        ELOGA("Error : Texture Load Failed.");
    //        return false;
    //    }

    //    if (!m_PlaneBC.Init(m_GfxCmdList, res))
    //    {
    //        ELOGA("Error : Texture Init Failed.");
    //        return false;
    //    }
    //}

    //// Test
    //{
    //    const char* rawPath = "../res/model/dosei_with_ground.obj";
    //    ModelOBJ model;
    //    OBJLoader loader;
    //    std::string path;
    //    if (!asdx::SearchFilePathA(rawPath, path))
    //    {
    //        ELOGA("Error : File Path Not Found. path = %s", rawPath);
    //        return false;
    //    }

    //    if (!loader.Load(path.c_str(), model))
    //    {
    //        ELOGA("Error : Model Load Failed.");
    //        return false;
    //    }

    //    Material dummy0 = {};
    //    dummy0.Normal    = INVALID_MATERIAL_MAP;
    //    dummy0.BaseColor = INVALID_MATERIAL_MAP;
    //    dummy0.ORM       = INVALID_MATERIAL_MAP;
    //    dummy0.Emissive  = INVALID_MATERIAL_MAP;
    //    dummy0.IntIor    = 1.4f;
    //    dummy0.ExtIor    = 1.0f;
    //    dummy0.UvScale   = asdx::Vector2(1.0f, 1.0f);
    //    m_ModelMgr.AddMaterials(&dummy0, 1);

    //    Material dummy1 = {};
    //    dummy1.Normal    = INVALID_MATERIAL_MAP;
    //    dummy1.BaseColor = m_PlaneBC.GetView()->GetDescriptorIndex();
    //    dummy1.ORM       = INVALID_MATERIAL_MAP;
    //    dummy1.Emissive  = INVALID_MATERIAL_MAP;
    //    //dummy0.IntIor    = 1.4f;
    //    //dummy0.ExtIor    = 1.0f;
    //    dummy1.UvScale   = asdx::Vector2(10.0f, 10.0f);
    //    m_ModelMgr.AddMaterials(&dummy1, 1);

    //    auto meshCount = model.Meshes.size();

    //    m_BLAS.resize(meshCount);

    //    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
    //    instanceDescs.resize(meshCount);

    //    m_MeshDrawCalls.resize(meshCount);
 
    //    for(size_t i=0; i<meshCount; ++i)
    //    {
    //        r3d::Mesh mesh = {};
    //        mesh.VertexCount = uint32_t(model.Meshes[i].Vertices.size());
    //        mesh.Vertices    = reinterpret_cast<Vertex*>(model.Meshes[i].Vertices.data());
    //        mesh.IndexCount  = uint32_t(model.Meshes[i].Indices.size());
    //        mesh.Indices     = model.Meshes[i].Indices.data();

    //        auto geometryHandle = m_ModelMgr.AddMesh(mesh);

    //        r3d::CpuInstance instance = {};
    //        instance.Transform      = asdx::Transform3x4();
    //        instance.MeshId         = uint32_t(i);
    //        instance.MaterialId     = (i != 3) ? 0 : 1;
    //        //instance.MaterialId     = 0;

    //        auto instanceHandle = m_ModelMgr.AddInstance(instance);

    //        D3D12_RAYTRACING_GEOMETRY_DESC desc = {};
    //        desc.Type                                   = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    //        desc.Flags                                  = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    //        desc.Triangles.Transform3x4                 = instanceHandle.AddressTB;
    //        desc.Triangles.IndexFormat                  = DXGI_FORMAT_R32_UINT;
    //        desc.Triangles.IndexCount                   = mesh.IndexCount;
    //        desc.Triangles.IndexBuffer                  = geometryHandle.AddressIB;
    //        desc.Triangles.VertexFormat                 = DXGI_FORMAT_R32G32B32_FLOAT;
    //        desc.Triangles.VertexBuffer.StartAddress    = geometryHandle.AddressVB;
    //        desc.Triangles.VertexBuffer.StrideInBytes   = sizeof(Vertex);
    //        desc.Triangles.VertexCount                  = mesh.VertexCount;

    //        if (!m_BLAS[i].Init(
    //            pDevice,
    //            1,
    //            &desc, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE))
    //        {
    //            ELOGA("Error : Blas::Init() Failed.");
    //            return false;
    //        }

    //        // ビルドコマンドを積んでおく.
    //        m_BLAS[i].Build(m_GfxCmdList.GetCommandList());

    //        memcpy(instanceDescs[i].Transform, instance.Transform.m, sizeof(float) * 12);
    //        instanceDescs[i].InstanceID                             = instanceHandle.InstanceId;
    //        instanceDescs[i].InstanceMask                           = 0xFF;
    //        instanceDescs[i].InstanceContributionToHitGroupIndex    = 0;
    //        instanceDescs[i].Flags                                  = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
    //        instanceDescs[i].AccelerationStructure                  = m_BLAS[i].GetResource()->GetGPUVirtualAddress();

    //        D3D12_VERTEX_BUFFER_VIEW vbv = {};
    //        vbv.BufferLocation = geometryHandle.AddressVB;
    //        vbv.SizeInBytes    = sizeof(Vertex) * mesh.VertexCount;
    //        vbv.StrideInBytes  = sizeof(Vertex);

    //        D3D12_INDEX_BUFFER_VIEW ibv = {};
    //        ibv.BufferLocation = geometryHandle.AddressIB;
    //        ibv.SizeInBytes    = sizeof(uint32_t) * mesh.IndexCount;
    //        ibv.Format         = DXGI_FORMAT_R32_UINT;

    //        m_MeshDrawCalls[i].StartIndex = 0;
    //        m_MeshDrawCalls[i].IndexCount = mesh.IndexCount;
    //        m_MeshDrawCalls[i].BaseVertex = 0;
    //        m_MeshDrawCalls[i].InstanceId = instanceHandle.InstanceId;
    //        m_MeshDrawCalls[i].VBV        = vbv;
    //        m_MeshDrawCalls[i].IBV        = ibv;
    //    }

    //    auto instanceCount = uint32_t(instanceDescs.size());
    //    if (!m_TLAS.Init(
    //        pDevice,
    //        instanceCount,
    //        instanceDescs.data(),
    //        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE))
    //    {
    //        ELOGA("Error : Tlas::Init() Failed.");
    //        return false;
    //    }

    //    // ビルドコマンドを積んでおく.
    //    m_TLAS.Build(m_GfxCmdList.GetCommandList());
    //}

    //// ライト生成.
    //{
    //}

#else
    // シーン構築.
    {
        if (!m_Scene.Init(m_SceneDesc.Path, m_GfxCmdList))
        {
            ELOGA("Error : Scene::Init() Failed.");
            return false;
        }
    }
#endif

    // セットアップコマンド実行.
    {
        // コマンド記録終了.
        m_GfxCmdList.Close();

        ID3D12CommandList* pCmds[] = {
            m_GfxCmdList.GetCommandList()
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

#if ASDX_ENABLE_IMGUI
    asdx::GuiMgr::Instance().Term();
#endif

    asdx::Quad::Instance().Term();

#if (!CAMP_RELEASE)
    m_DevRayTracingPSO  .Term();
    m_DevRayGenTable    .Term();
    m_DevMissTable      .Term();
    m_DevHitGroupTable  .Term();
#endif

    m_PlaneBC.Term();

    m_TaaRenderer.Term();
    for(auto i=0; i<2; ++i)
    { m_HistoryTarget[i].Term(); }

#if ENABLE_RESTIR
    m_SpatialReservoirBuffer .Term();
    m_TemporalReservoirBuffer.Term();
    m_InitialSampling.Reset();
    m_SpatialSampling.Rest();
    m_ShadePixelRootSig .Term();
    m_ShadePixelPSO     .Term();
#endif

    m_TonemapBuffer.Term();

    m_TonemapPSO        .Term();
    m_TonemapRootSig    .Term();

    m_RayTracingPSO     .Term();
    m_RayTracingRootSig .Term();
    
    m_CopyRootSig       .Term();
    m_CopyPSO           .Term();

    m_Canvas.Term();

    m_SceneParam.Term();

    m_RayGenTable   .Term();
    m_MissTable     .Term();
    m_HitGroupTable .Term();

    for(auto i=0; i<3; ++i)
    {
        m_ReadBackTexture[i].Reset();
        m_ExportData[i].Pixels.clear();
    }

    m_AlbedoTarget    .Term();
    m_NormalTarget    .Term();
    m_VelocityTarget  .Term();
    m_ModelDepthTarget.Term();
    m_ModelRootSig    .Term();
    m_ModelPSO        .Term();

    timer.End();
    printf_s("Terminate Process ... done! %lf[msec]\n", timer.GetElapsedMsec());
    printf_s("Total Time        ... %lf[sec]\n", m_Timer.GetRelativeSec());
}

//-----------------------------------------------------------------------------
//      フレーム遷移時の処理です.
//-----------------------------------------------------------------------------
void Renderer::OnFrameMove(asdx::FrameEventArgs& args)
{
#if (CAMP_RELEASE)
    // 制限時間を超えた
    if (args.Time >= m_SceneDesc.RenderTimeSec)
    {
        PostQuitMessage(0);
        m_EndRequest = true;

        uint8_t* ptr = nullptr;
        auto idx = m_MapIndex;
        auto hr  = m_ReadBackTexture[idx]->Map(0, nullptr, reinterpret_cast<void**>(&ptr));
        if (SUCCEEDED(hr))
        {
            memcpy(m_ExportData[idx].Pixels.data(), ptr, m_ExportData[idx].Pixels.size());
            m_ExportData[idx].FrameIndex = m_CaptureIndex;
        }
        m_ReadBackTexture[idx]->Unmap(0, nullptr);

        // 画像に出力.
        _beginthreadex(nullptr, 0, Export, &m_ExportData[idx], 0, nullptr);

        return;
    }

    // CPUで読み取り.
    m_AnimationElapsedTime += args.ElapsedTime;
    if (m_AnimationElapsedTime >= m_AnimationOneFrameTime)
    {
        uint8_t* ptr = nullptr;
        auto idx = m_MapIndex;
        auto hr  = m_ReadBackTexture[idx]->Map(0, nullptr, reinterpret_cast<void**>(&ptr));
        if (SUCCEEDED(hr))
        {
            memcpy(m_ExportData[idx].Pixels.data(), ptr, m_ExportData[idx].Pixels.size());
            m_ExportData[idx].FrameIndex = m_CaptureIndex;
        }
        m_ReadBackTexture[idx]->Unmap(0, nullptr);

        // 画像に出力.
        _beginthreadex(nullptr, 0, Export, &m_ExportData[idx], 0, nullptr);

        m_CaptureIndex++;
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
#if (CAMP_RELEASE)
    asdx::CameraEvent camEvent = {};
    camEvent.Flags |= asdx::CameraEvent::EVENT_ROTATE;
    camEvent.Rotate.x += 2.0f * asdx::F_PI / 600.0f; 

    m_Camera.UpdateByEvent(camEvent);

    m_CurrView = m_Camera.GetView();
    m_CurrProj = asdx::Matrix::CreatePerspectiveFieldOfView(
        asdx::ToRadian(37.5f),
        float(m_SceneDesc.Width) / float(m_SceneDesc.Height),
        0.1f,
        10000.0f);
    m_CameraZAxis = m_Camera.GetAxisZ();
#else
    m_CurrView = m_CameraController.GetView();
    m_CurrProj = asdx::Matrix::CreatePerspectiveFieldOfView(
        asdx::ToRadian(37.5f),
        float(m_SceneDesc.Width) / float(m_SceneDesc.Height),
        m_CameraController.GetNearClip(),
        m_CameraController.GetFarClip());
    m_CameraZAxis = m_CameraController.GetAxisZ();
#endif

    m_CurrInvView = asdx::Matrix::Invert(m_CurrView);
    m_CurrInvProj = asdx::Matrix::Invert(m_CurrProj);
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

    bool enableAccumulation = true;

    // 定数バッファ更新.
    {
        auto changed = memcmp(&m_CurrView, &m_PrevView, sizeof(asdx::Matrix)) != 0;
    #if !(CAMP_RELEASE)
        if (m_RequestReload)
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
        param.MaxBounce             = 16;
        param.MinBounce             = 3;
        param.FrameIndex            = GetFrameCount();
        param.SkyIntensity          = 1.0f;
        param.EnableAccumulation    = enableAccumulation;
        param.AccumulatedFrames     = m_AccumulatedFrames;
        param.ExposureAdjustment    = 1.0f;
        param.LightCount            = m_Scene.GetLightCount();
        param.Size.x                = float(m_SceneDesc.Width);
        param.Size.y                = float(m_SceneDesc.Height);
        param.Size.z                = 1.0f / param.Size.x;
        param.Size.w                = 1.0f / param.Size.y;
        param.CameraDir             = m_CameraZAxis;
        param.MaxIteration          = 9;

        m_SceneParam.SwapBuffer();
        m_SceneParam.Update(&param, sizeof(param));

        // 次のフレーム用に記録.
        m_PrevView          = param.View;
        m_PrevProj          = param.Proj;
        m_PrevInvView       = param.InvView;
        m_PrevInvProj       = param.InvProj;
        m_PrevInvViewProj   = param.InvViewProj;
    }

    // デノイズ用 G-Buffer.
    {
        asdx::ScopedBarrier barrier0(m_GfxCmdList, m_AlbedoTarget  .GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        asdx::ScopedBarrier barrier1(m_GfxCmdList, m_NormalTarget  .GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        asdx::ScopedBarrier barrier2(m_GfxCmdList, m_VelocityTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

        auto pRTV0 = m_AlbedoTarget    .GetRTV();
        auto pRTV1 = m_NormalTarget    .GetRTV();
        auto pRTV2 = m_VelocityTarget  .GetRTV();
        auto pDSV  = m_ModelDepthTarget.GetDSV();

        float clearColor [4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        float clearNormal[4] = { 0.5f, 0.5f, 1.0f, 1.0f };
        float clearVelocity[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

        m_GfxCmdList.ClearRTV(pRTV0, clearColor);
        m_GfxCmdList.ClearRTV(pRTV1, clearNormal);
        m_GfxCmdList.ClearRTV(pRTV2, clearVelocity);
        m_GfxCmdList.ClearDSV(pDSV, 1.0f);

        D3D12_CPU_DESCRIPTOR_HANDLE handleRTVs[] = {
            pRTV0->GetHandleCPU(),
            pRTV1->GetHandleCPU(),
            pRTV2->GetHandleCPU(),
        };

        auto handleDSV = pDSV->GetHandleCPU();

        m_GfxCmdList.GetCommandList()->OMSetRenderTargets(
            _countof(handleRTVs), 
            handleRTVs,
            FALSE,
            &handleDSV);

        m_GfxCmdList.SetViewport(m_AlbedoTarget.GetResource());
        m_GfxCmdList.SetRootSignature(m_ModelRootSig.GetPtr(), false);
        m_GfxCmdList.SetPipelineState(m_ModelPSO.GetPtr());

        m_GfxCmdList.SetCBV(0, m_SceneParam.GetResource());
        m_GfxCmdList.SetSRV(2, m_Scene.GetTB());
        m_GfxCmdList.SetSRV(3, m_Scene.GetMB());
        m_GfxCmdList.SetSRV(4, m_Scene.GetIB());
        m_GfxCmdList.SetPrimitiveToplogy(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        auto count = m_MeshDrawCalls.size();
        for(size_t i=0; i<count; ++i)
        {
            auto& info = m_MeshDrawCalls[i];

            m_GfxCmdList.SetVertexBuffers(0, 1, &info.VBV);
            m_GfxCmdList.SetIndexBuffer(&info.IBV);

            m_GfxCmdList.SetConstants(1, 1, &info.InstanceId, 0);
            m_GfxCmdList.DrawIndexedInstanced(
                info.IndexCount,
                1,
                info.StartIndex,
                info.BaseVertex,
                0);
        }
    }

#if ENABLE_RESTIR
    // Initial Sampling & Temporal reuse
    {
        auto stateObject    = m_InitialSampling.PSO          .GetStateObject();
        auto rayGenTable    = m_InitialSampling.RayGenTable  .GetRecordView();
        auto missTable      = m_InitialSampling.MissTable    .GetTableView();
        auto hitGroupTable  = m_InitialSampling.HitGroupTable.GetTableView();
        auto rootSignature  = m_InitialSampling.RootSig      .GetPtr();

    #if (!CAMP_RELEASE)
        if (m_ReloadShader)
        {
            stateObject     = m_DevInitialSampling.PSO          .GetStateObject();
            rayGenTable     = m_DevInitialSampling.RayGenTable  .GetRecordView();
            missTable       = m_DevInitialSampling.MissTable    .GetTableView();
            hitGroupTable   = m_DevInitialSampling.HitGroupTable.GetTableView();
            rootSignature   = m_DevInitialSampling.RootSig      .GetPtr();
        }
    #endif

        m_GfxCmdList.SetStateObject(stateObject);
        m_GfxCmdList.SetRootSignature(rootSignature, true);

        // 共通.
        m_GfxCmdList.SetCBV(0, m_SceneParam.GetResource(), true);
        m_GfxCmdList.SetSRV(1, m_TLAS.GetResource(), true);
        m_GfxCmdList.SetSRV(2, m_ModelMgr.GetIB(), true);
        m_GfxCmdList.SetSRV(3, m_ModelMgr.GetMB(), true);
        m_GfxCmdList.SetSRV(4, m_ModelMgr.GetTB(), true);

        // 個別.
        m_GfxCmdList.SetUAV(5, m_TemporalReservoirBuffer.GetUAV(), true);
        //m_GfxCmdList.SetSRV(6, m_Scene.GetIBL(), true);
        m_GfxCmdList.SetTable(6, m_IBL.GetView(), true);

        D3D12_DISPATCH_RAYS_DESC desc = {};
        desc.RayGenerationShaderRecord  = rayGenTable;
        desc.MissShaderTable            = missTable;
        desc.HitGroupTable              = hitGroupTable;
        desc.Width                      = m_SceneDesc.Width;
        desc.Height                     = m_SceneDesc.Height;
        desc.Depth                      = 1;

        m_GfxCmdList.DispatchRays(&desc);

        // バリアを張っておく.
        m_GfxCmdList.BarrierUAV(m_TemporalReservoirBuffer.GetResource());
    }

    // Spatial reuse
    {
        auto stateObject    = m_SpatialSampling.PSO          .GetStateObject();
        auto rayGenTable    = m_SpatialSampling.RayGenTable  .GetRecordView();
        auto missTable      = m_SpatialSampling.MissTable    .GetTableView();
        auto hitGroupTable  = m_SpatialSampling.HitGroupTable.GetTableView();
        auto rootSignature  = m_SpatialSampling.RootSig      .GetPtr();

    #if (!CAMP_RELEASE)
        if (m_ReloadShader)
        {
            stateObject     = m_DevSpatialSampling.PSO          .GetStateObject();
            rayGenTable     = m_DevSpatialSampling.RayGenTable  .GetRecordView();
            missTable       = m_DevSpatialSampling.MissTable    .GetTableView();
            hitGroupTable   = m_DevSpatialSampling.HitGroupTable.GetTableView();
            rootSignature   = m_DevSpatialSampling.RootSig      .GetPtr();
        }
    #endif

        m_GfxCmdList.SetStateObject(stateObject);
        m_GfxCmdList.SetRootSignature(rootSignature, true);

        // 共通.
        m_GfxCmdList.SetCBV(0, m_SceneParam.GetResource(), true);
        m_GfxCmdList.SetSRV(1, m_TLAS.GetResource(), true);
        m_GfxCmdList.SetSRV(2, m_ModelMgr.GetIB(), true);
        m_GfxCmdList.SetSRV(3, m_ModelMgr.GetMB(), true);
        m_GfxCmdList.SetSRV(4, m_ModelMgr.GetTB(), true);

        // 個別.
        m_GfxCmdList.SetUAV(5, m_SpatialReservoirBuffer.GetUAV(), true);
        m_GfxCmdList.SetSRV(6, m_TemporalReservoirBuffer.GetSRV(), true);

        D3D12_DISPATCH_RAYS_DESC desc = {};
        desc.RayGenerationShaderRecord  = rayGenTable;
        desc.MissShaderTable            = missTable;
        desc.HitGroupTable              = hitGroupTable;
        desc.Width                      = m_SceneDesc.Width;
        desc.Height                     = m_SceneDesc.Height;
        desc.Depth                      = 1;

        m_GfxCmdList.DispatchRays(&desc);

        // バリアを張っておく.
        m_GfxCmdList.BarrierUAV(m_SpatialReservoirBuffer.GetResource());
    }

    // Shade Pixel.
    {
        auto pReservoirBuffer = m_SpatialReservoirBuffer.GetSRV();

        ShadeParam param = {};
        param.Width                 = m_SceneDesc.Width;
        param.Height                = m_SceneDesc.Height;
        param.EnableAccumulation    = enableAccumulation;
        param.AccumulationFrame     = m_AccumulatedFrames;

        uint32_t threadX = (m_SceneDesc.Width  + 7) / 8;
        uint32_t threadY = (m_SceneDesc.Height + 7) / 8;

        m_GfxCmdList.SetRootSignature(m_ShadePixelRootSig.GetPtr(), true);
        m_GfxCmdList.SetPipelineState(m_ShadePixelPSO.GetPtr());
        m_GfxCmdList.SetConstants(0, 4, &param, 0, true);
        m_GfxCmdList.SetSRV(1, pReservoirBuffer, true);
        m_GfxCmdList.SetTable(2, m_Canvas.GetUAV(), true);
        m_GfxCmdList.Dispatch(threadX, threadY, 1);

        m_GfxCmdList.BarrierUAV(m_Canvas.GetResource());
    }
#else

    // レイトレ実行.
    {
        auto stateObject    = m_RayTracingPSO.GetStateObject();
        auto rayGenTable    = m_RayGenTable  .GetRecordView();
        auto missTable      = m_MissTable    .GetTableView();
        auto hitGroupTable  = m_HitGroupTable.GetTableView();

    #if (!CAMP_RELEASE)
        // リロードしたシェーダに差し替え.
        if (m_ReloadShader)
        {
            stateObject   = m_DevRayTracingPSO.GetStateObject();
            rayGenTable   = m_DevRayGenTable  .GetRecordView();
            missTable     = m_DevMissTable    .GetTableView();
            hitGroupTable = m_DevHitGroupTable.GetTableView();
        }
    #endif

        m_GfxCmdList.SetStateObject(stateObject);
        m_GfxCmdList.SetRootSignature(m_RayTracingRootSig.GetPtr(), true);
        m_GfxCmdList.SetTable(0, m_Canvas.GetUAV(), true);
        m_GfxCmdList.SetSRV(1, m_Scene.GetTLAS(), true);
        m_GfxCmdList.SetSRV(2, m_Scene.GetIB(), true);
        m_GfxCmdList.SetSRV(3, m_Scene.GetMB(), true);
        m_GfxCmdList.SetSRV(4, m_Scene.GetTB(), true);
        m_GfxCmdList.SetTable(5, m_Scene.GetIBL(), true);
        m_GfxCmdList.SetCBV(6, m_SceneParam.GetResource(), true);

        D3D12_DISPATCH_RAYS_DESC desc = {};
        desc.RayGenerationShaderRecord  = rayGenTable;
        desc.MissShaderTable            = missTable;
        desc.HitGroupTable              = hitGroupTable;
        desc.Width                      = m_SceneDesc.Width;
        desc.Height                     = m_SceneDesc.Height;
        desc.Depth                      = 1;

        m_GfxCmdList.DispatchRays(&desc);

        // バリアを張っておく.
        m_GfxCmdList.BarrierUAV(m_Canvas.GetResource());
    }
#endif

    // トーンマップ実行.
    {
        asdx::ScopedBarrier barrier0(
            m_GfxCmdList,
            m_Canvas.GetResource(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        auto threadX = (m_SceneDesc.Width + 7) / 8;
        auto threadY = (m_SceneDesc.Height + 7) / 8;

        m_GfxCmdList.SetRootSignature(m_TonemapRootSig.GetPtr(), true);
        m_GfxCmdList.SetPipelineState(m_TonemapPSO.GetPtr());
        m_GfxCmdList.SetTable(0, m_Canvas.GetSRV(), true);
        m_GfxCmdList.SetCBV(1, m_SceneParam.GetResource(), true);
        m_GfxCmdList.SetTable(2, m_TonemapBuffer.GetUAV(), true);
        m_GfxCmdList.Dispatch(threadX, threadY, 1);

        m_GfxCmdList.BarrierUAV(m_TonemapBuffer.GetResource());
    }

    // テンポラルアンチエイリアシング実行.
    {
        auto jitter = asdx::TaaRenderer::CalcJitter(m_JitterIndex, m_SceneDesc.Width, m_SceneDesc.Height);

        m_JitterIndex++;

        asdx::ScopedBarrier barrier0(
            m_GfxCmdList,
            m_HistoryTarget[m_PrevHistoryBufferIndex].GetResource(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        m_TaaRenderer.RenderCS(
            m_GfxCmdList.GetCommandList(),
            m_HistoryTarget[m_CurrHistoryBufferIndex].GetUAV(),
            m_TonemapBuffer.GetSRV(),
            m_HistoryTarget[m_PrevHistoryBufferIndex].GetSRV(),
            m_VelocityTarget.GetSRV(),
            m_DepthTarget.GetSRV(),
            1.0f,
            0.1f,
            jitter);

        m_GfxCmdList.BarrierUAV(m_HistoryTarget[m_CurrHistoryBufferIndex].GetResource());
    }

    // リードバック実行.
    {
        m_GfxCmdList.BarrierTransition(
            m_HistoryTarget[m_CurrHistoryBufferIndex].GetResource(), 0,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_SOURCE);

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.Type                                = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.pResource                           = m_ReadBackTexture[m_ReadBackIndex].GetPtr();
        dst.PlacedFootprint.Footprint.Width     = static_cast<UINT>(m_SceneDesc.Width);
        dst.PlacedFootprint.Footprint.Height    = m_SceneDesc.Height;
        dst.PlacedFootprint.Footprint.Depth     = 1;
        dst.PlacedFootprint.Footprint.RowPitch  = m_ReadBackPitch;
        dst.PlacedFootprint.Footprint.Format    = DXGI_FORMAT_R8G8B8A8_UNORM;

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.Type                = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource           = m_HistoryTarget[m_CurrHistoryBufferIndex].GetResource();
        src.SubresourceIndex    = 0;

        D3D12_BOX box = {};
        box.left    = 0;
        box.right   = m_SceneDesc.Width;
        box.top     = 0;
        box.bottom  = m_SceneDesc.Height;
        box.front   = 0;
        box.back    = 1;

        // 読み戻し用ターゲットにコピー.
        m_GfxCmdList.CopyTextureRegion(&dst, 0, 0, 0, &src, &box);

        m_ReadBackIndex = (m_ReadBackIndex + 1) % 3;
        m_MapIndex      = (m_MapIndex + 1) % 3;
    }

    // スワップチェインに描画.
    {
        m_GfxCmdList.BarrierTransition(
            m_ColorTarget[idx].GetResource(), 0,
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET);

        asdx::ScopedBarrier barrier0(
            m_GfxCmdList,
            m_HistoryTarget[m_CurrHistoryBufferIndex].GetResource(),
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    #if !(CAMP_RELEASE)
        const asdx::IShaderResourceView* pSRV = nullptr;
        switch(m_BufferKind)
        {
        case BUFFER_KIND_CANVAS:
            pSRV = m_HistoryTarget[m_CurrHistoryBufferIndex].GetSRV();
            break;

        #if ENABLE_RESTIR
        case BUFFER_KIND_SPATIAL:
            pSRV = m_SpatialReservoirBuffer.GetSRV();
            break;

        case BUFFER_KIND_TEMPORAL:
            pSRV = m_TemporalReservoirBuffer.GetSRV();
            break;
        #endif

        case BUFFER_KIND_ALBEDO:
            pSRV = m_AlbedoTarget.GetSRV();
            break;

        case BUFFER_KIND_NORMAL:
            pSRV = m_NormalTarget.GetSRV();
            break;

        case BUFFER_KIND_VELOCITY:
            pSRV = m_VelocityTarget.GetSRV();
            break;
        }

        // デバッグ表示.
        m_GfxCmdList.SetTarget(m_ColorTarget[idx].GetRTV(), nullptr);
        m_GfxCmdList.SetViewport(m_ColorTarget[idx].GetResource());
        m_GfxCmdList.SetRootSignature(m_CopyRootSig.GetPtr(), false);
        m_GfxCmdList.SetPipelineState(m_CopyPSO.GetPtr());
        m_GfxCmdList.SetTable(0, pSRV);
        asdx::Quad::Instance().Draw(m_GfxCmdList.GetCommandList());
    #else
        m_GfxCmdList.SetTarget(m_ColorTarget[idx].GetRTV(), nullptr);
        m_GfxCmdList.SetViewport(m_ColorTarget[idx].GetResource());
        m_GfxCmdList.SetRootSignature(m_CopyRootSig.GetPtr(), false);
        m_GfxCmdList.SetPipelineState(m_CopyPSO.GetPtr());
        m_GfxCmdList.SetTable(0, m_HistoryTarget[m_CurrHistoryBufferIndex].GetSRV());
        asdx::Quad::Instance().Draw(m_GfxCmdList.GetCommandList());
    #endif

        // 2D描画.
        Draw2D();

        m_GfxCmdList.BarrierTransition(
            m_ColorTarget[idx].GetResource(), 0,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT);
    }

    m_PrevHistoryBufferIndex = m_CurrHistoryBufferIndex;
    m_CurrHistoryBufferIndex = (m_CurrHistoryBufferIndex + 1) & 0x1;

    // コマンド記録終了.
    m_GfxCmdList.Close();

    ID3D12CommandList* pCmds[] = {
        m_GfxCmdList.GetCommandList()
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

#if (!CAMP_RELEASE)
    // シェーダリロード.
    if (m_RequestReload)
    {
        ReloadShader();
        m_RequestReload = false;
    }
#endif
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
#if (!CAMP_RELEASE)
    #if ASDX_ENABLE_IMGUI
        asdx::GuiMgr::Instance().OnKey(
            args.IsKeyDown, args.IsAltDown, args.KeyCode);
    #endif

    m_CameraController.OnKey(args.KeyCode, args.IsKeyDown, args.IsAltDown);

    if (args.IsKeyDown)
    {
        switch(args.KeyCode)
        {
        case VK_F7:
            {
                // シェーダをリロードします.
                m_RequestReload = true;
            }
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
        m_CameraController.OnMouse(
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
#if (!CAMP_RELEASE)
    #if ASDX_ENABLE_IMGUI
        asdx::GuiMgr::Instance().OnTyping(keyCode);
    #endif
#endif
}

//-----------------------------------------------------------------------------
//      2D描画を行います.
//-----------------------------------------------------------------------------
void Renderer::Draw2D()
{
#if (!CAMP_RELEASE)
    #if ASDX_ENABLE_IMGUI
        asdx::GuiMgr::Instance().Update(m_Width, m_Height);

        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowSize(ImVec2(140, 0));
        if (ImGui::Begin(u8"フレーム情報", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar))
        {
            ImGui::Text(u8"FPS   : %.3lf", GetFPS());
            ImGui::Text(u8"Frame : %ld", GetFrameCount());
            ImGui::Text(u8"Accum : %ld", m_AccumulatedFrames);
        }
        ImGui::End();

        if (ImGui::Begin(u8"デバッグ設定", &m_DebugSetting))
        {
            int count = _countof(kBufferKindItems);
            ImGui::Combo(u8"表示バッファ", &m_BufferKind, kBufferKindItems, count);
        }
        ImGui::End();


        asdx::GuiMgr::Instance().Draw(m_GfxCmdList.GetCommandList());
    #endif
#endif
}

#if (!CAMP_RELEASE)
//-----------------------------------------------------------------------------
//      シェーダをコンパイルします.
//-----------------------------------------------------------------------------
bool Renderer::CompileShader(const wchar_t* path, asdx::IBlob** ppBlob)
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

    if (!asdx::CompileFromFile(resolvePath.c_str(), includeDirs, "", "lib_6_6", ppBlob))
    {
        ELOGA("Error : Compile Shader Failed. path = %ls", resolvePath.c_str());
        m_ReloadShader = false;
        return false;
    }

    return true;
}

//-----------------------------------------------------------------------------
//      シェーダをリロードします.
//-----------------------------------------------------------------------------
void Renderer::ReloadShader()
{
    auto pDevice = asdx::GetD3D12Device();

    // リロード成功フラグを下す.
    m_ReloadShader = false;

    // 開発用オブジェクトを破棄.
    m_DevRayTracingPSO  .Term();
    m_DevRayGenTable    .Term();
    m_DevMissTable      .Term();
    m_DevHitGroupTable  .Term();

#if ENABLE_RESTIR
    m_DevInitialSampling.Reset();
    m_DevSpatialSampling.Reset();
#endif

    asdx::RefPtr<asdx::IBlob> DevShader;

    // シェーダコンパイル.
    if (!CompileShader(L"../res/shader/RtCamp.hlsl", DevShader.GetAddress()))
    {
        return;
    }

    // レイトレ用パイプラインステート生成.
    {
        D3D12_EXPORT_DESC exports[] = {
            { L"OnGenerateRay"      , nullptr, D3D12_EXPORT_FLAG_NONE },
            { L"OnClosestHit"       , nullptr, D3D12_EXPORT_FLAG_NONE },
            { L"OnShadowClosestHit" , nullptr, D3D12_EXPORT_FLAG_NONE },
            { L"OnMiss"             , nullptr, D3D12_EXPORT_FLAG_NONE },
            { L"OnShadowMiss"       , nullptr, D3D12_EXPORT_FLAG_NONE },
        };

        D3D12_HIT_GROUP_DESC groups[2] = {};
        groups[0].ClosestHitShaderImport    = L"OnClosestHit";
        groups[0].HitGroupExport            = L"StandardHit";
        groups[0].Type                      = D3D12_HIT_GROUP_TYPE_TRIANGLES;

        groups[1].ClosestHitShaderImport    = L"OnShadowClosestHit";
        groups[1].HitGroupExport            = L"ShadowHit";
        groups[1].Type                      = D3D12_HIT_GROUP_TYPE_TRIANGLES;

        asdx::RayTracingPipelineStateDesc desc = {};
        desc.pGlobalRootSignature       = m_RayTracingRootSig.GetPtr();
        desc.DXILLibrary                = { DevShader->GetBufferPointer(), DevShader->GetBufferSize() };
        desc.ExportCount                = _countof(exports);
        desc.pExports                   = exports;
        desc.HitGroupCount              = _countof(groups);
        desc.pHitGroups                 = groups;
        desc.MaxPayloadSize             = sizeof(Payload);
        desc.MaxAttributeSize           = sizeof(asdx::Vector2);
        desc.MaxTraceRecursionDepth     = 1;

        if (!m_DevRayTracingPSO.Init(pDevice, desc))
        {
            ELOGA("Error : RayTracing PSO Failed.");
            m_ReloadShader = false;
            return;
        }
    }

    // レイ生成テーブル.
    {
        asdx::ShaderRecord record = {};
        record.ShaderIdentifier = m_DevRayTracingPSO.GetShaderIdentifier(L"OnGenerateRay");

        asdx::ShaderTable::Desc desc = {};
        desc.RecordCount    = 1;
        desc.pRecords       = &record;

        if (!m_DevRayGenTable.Init(pDevice, &desc))
        {
            ELOGA("Error : RayGenTable Init Failed.");
            m_ReloadShader = false;
            return;
        }
    }

    // ミステーブル.
    {
        asdx::ShaderRecord record[2] = {};
        record[0].ShaderIdentifier = m_DevRayTracingPSO.GetShaderIdentifier(L"OnMiss");
        record[1].ShaderIdentifier = m_DevRayTracingPSO.GetShaderIdentifier(L"OnShadowMiss");

        asdx::ShaderTable::Desc desc = {};
        desc.RecordCount = 2;
        desc.pRecords    = record;

        if (!m_DevMissTable.Init(pDevice, &desc))
        {
            ELOGA("Error : MissTable Init Failed.");
            m_ReloadShader = false;
            return;
        }
    }

    // ヒットグループ.
    {
        asdx::ShaderRecord record[2];
        record[0].ShaderIdentifier = m_DevRayTracingPSO.GetShaderIdentifier(L"StandardHit");
        record[1].ShaderIdentifier = m_DevRayTracingPSO.GetShaderIdentifier(L"ShadowHit");

        asdx::ShaderTable::Desc desc = {};
        desc.RecordCount = 2;
        desc.pRecords    = record;

        if (!m_DevHitGroupTable.Init(pDevice, &desc))
        {
            ELOGA("Error : HitGroupTable Init Failed.");
            m_ReloadShader = false;
            return;
        }
    }

#if ENABLE_RESTIR
    // 初期サンプル & テンポラルリユース.
    {
        asdx::RefPtr<asdx::IBlob> binary;
        if (!CompileShader(L"../res/shader/InitialSampling.hlsl", binary.GetAddress()))
        {
            m_ReloadShader = false;
            return;
        }

        D3D12_SHADER_BYTECODE shader = {};
        shader.BytecodeLength  = binary->GetBufferSize();
        shader.pShaderBytecode = binary->GetBufferPointer();

        if (!InitInitialSamplingPipeline(m_DevInitialSampling, shader))
        {
            ELOGA("Error : Init InitialSampling Pipeline Failed.");
            m_ReloadShader = false;
            return;
        }
    }

    // スパシャルリユース.
    {
        asdx::RefPtr<asdx::IBlob> binary;
        if (!CompileShader(L"../res/shader/SpatialResampling.hlsl", binary.GetAddress()))
        {
            m_ReloadShader = false;
            return;
        }

        D3D12_SHADER_BYTECODE shader = {};
        shader.BytecodeLength  = binary->GetBufferSize();
        shader.pShaderBytecode = binary->GetBufferPointer();

        if (!InitSpatialSamplingPipeline(m_DevSpatialSampling, shader))
        {
            ELOGA("Error : Init SpatialSampling Pipeline Failed.");
            m_ReloadShader = false;
            return;
        }
    }
#endif

    // リロードフラグを立てる.
    m_ReloadShader = true;

    // リロード完了時刻を取得.
    tm local_time = {};
    auto t   = time(nullptr);
    auto err = localtime_s( &local_time, &t );

    // 成功ログを出力.
    ILOGA("Info : Shader Reload Successs!! [%04d/%02d/%02d %02d:%02d:%02d]",
        local_time.tm_year + 1900,
        local_time.tm_mon + 1,
        local_time.tm_mday,
        local_time.tm_hour,
        local_time.tm_min,
        local_time.tm_sec);
}
#endif//(!CAMP_RELEASE)

} // namespace r3d
