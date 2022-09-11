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
#include "../res/shader/Compile/CopyPS.inc"
#include "../res/shader/Compile/DenoiserCS.inc"

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
};

static const char* kBufferKindItems[] = {
    u8"Canvas",
    u8"Albedo",
    u8"Normal",
    u8"Velocity",
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

    char path[256] = {};
    sprintf_s(path, "output_%03ld.png", data->FrameIndex);

    if (fpng::fpng_encode_image_to_memory(
        data->Pixels.data(),
        data->Width,
        data->Height,
        4,
        data->Converted))
    {
        FILE* pFile = nullptr;
        auto err = fopen_s(&pFile, path, "wb");
        if (err == 0)
        {
            fwrite(data->Converted.data(), 1, data->Converted.size(), pFile);
            fclose(pFile);
        }
    }

    data->Processed = false;

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
: asdx::Application(L"r3d alpha 1.0", 1920, 1080, nullptr, nullptr, nullptr)
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
        m_AnimationElapsedTime  = 0.0f;

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
            m_ExportData[i].Pixels.resize(m_SceneDesc.Width * m_SceneDesc.Height * 4);
            m_ExportData[i].FrameIndex  = 0;
            m_ExportData[i].Width       = m_SceneDesc.Width;
            m_ExportData[i].Height      = m_SceneDesc.Height;
        }
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
        asdx::DescriptorSetLayout<8, 1> layout;
        layout.SetTableUAV(0, asdx::SV_ALL, 0);
        layout.SetSRV     (1, asdx::SV_ALL, 0);
        layout.SetSRV     (2, asdx::SV_ALL, 1);
        layout.SetSRV     (3, asdx::SV_ALL, 2);
        layout.SetSRV     (4, asdx::SV_ALL, 3);
        layout.SetTableSRV(5, asdx::SV_ALL, 4);
        layout.SetCBV     (6, asdx::SV_ALL, 0);
        layout.SetTableSRV(7, asdx::SV_ALL, 5);
        layout.SetStaticSampler(0, asdx::SV_ALL, asdx::STATIC_SAMPLER_LINEAR_WRAP, 0);
        layout.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);

        if (!m_RayTracingRootSig.Init(pDevice, layout.GetDesc()))
        {
            ELOGA("Error : RayTracing RootSignature Failed.");
            return false;
        }
    }

    if (!CreateRayTracingPipeline(
        RtCamp, sizeof(RtCamp),
        m_RayTracingPSO,
        m_RayGenTable,
        m_MissTable,
        m_HitGroupTable))
    {
        ELOGA("Error : CreateRayTracingPipeline() Failed.");
        return false;
    }

    // トーンマップ用ルートシグニチャ生成.
    {
        asdx::DescriptorSetLayout<3, 0> layout;
        layout.SetTableSRV(0, asdx::SV_ALL, 0);
        layout.SetCBV(1, asdx::SV_ALL, 0);
        layout.SetTableUAV(2, asdx::SV_ALL, 0);

        if (!m_TonemapRootSig.Init(pDevice, layout.GetDesc()))
        {
            ELOGA("Error : Tonemap RootSignature Init Failed.");
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
            ELOGA("Error : Tonemap PipelineState Init Failed.");
            return false;
        }
    }

    // デノイズ用ルートシグニチャ生成.
    {
        asdx::DescriptorSetLayout<5, 2> layout;
        layout.SetTableUAV(0, asdx::SV_ALL, 0);
        layout.SetTableSRV(1, asdx::SV_ALL, 0);
        layout.SetTableSRV(2, asdx::SV_ALL, 1);
        layout.SetTableSRV(3, asdx::SV_ALL, 2);
        layout.SetContants(4, asdx::SV_ALL, 6, 0);
        layout.SetStaticSampler(0, asdx::SV_ALL, asdx::STATIC_SAMPLER_POINT_CLAMP, 0);
        layout.SetStaticSampler(1, asdx::SV_ALL, asdx::STATIC_SAMPLER_LINEAR_CLAMP, 1);

        if (!m_DenoiseRootSig.Init(pDevice, layout.GetDesc()))
        {
            ELOGA("Error : Denoise RootSignature Init Failed.");
            return false;
        }
    }

    // デノイズ用パイプラインステート生成.
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = m_DenoiseRootSig.GetPtr();
        desc.CS             = { DenoiserCS, sizeof(DenoiserCS) };

        if (!m_DenoisePSO.Init(pDevice, &desc))
        {
            ELOGA("Error : Denoise PipelineState Failed.");
            return false;
        }
    }

    // デノイズ用バッファ.
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
            if (!m_DenoiseTarget[i].Init(&desc))
            {
                ELOGA("Error : Denoise[%d] Init Failed.", i);
                return false;
            }
        }

        m_DenoiseTarget[0].SetName(L"DenoiseTarget0");
        m_DenoiseTarget[1].SetName(L"DenoiseTarget1");
    }

    if (!m_TaaRenerer.InitCS())
    {
        ELOGA("Error : TaaRenderer::InitCS() Failed.");
        return false;
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
            if (!m_HistoryTarget[i].Init(&desc))
            {
                ELOGA("Error : History[%d] Init Failed.", i);
                return false;
            }
        }

        m_CurrHistoryIndex = 0;
        m_PrevHistoryIndex = 1;
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

    //// TAAレンダラー初期化.
    //if (!m_TaaRenderer.InitCS())
    //{
    //    ELOGA("Error : TaaRenderer::InitCS() Failed");
    //    return false;
    //}

#if !(CAMP_RELEASE)
    // カメラ初期化.
    {
        auto pos    = asdx::Vector3(0.0f, 0.0f, 300.5f);
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

#if !(CAMP_RELEASE)
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

    m_Scene.Term();

#if ENABLE_RESTIR
    m_SpatialReservoirBuffer .Term();
    m_TemporalReservoirBuffer.Term();
    m_InitialSampling.Reset();
    m_SpatialSampling.Rest();
    m_ShadePixelRootSig .Term();
    m_ShadePixelPSO     .Term();
#endif

    for(auto i=0; i<3; ++i)
    { m_CaptureTarget[i].Term(); }

    m_ReadBackTexture.Reset();

    for(auto i=0; i<2; ++i)
    { m_DenoiseTarget[i].Term(); }

    m_DenoisePSO    .Term();
    m_DenoiseRootSig.Term();

    m_TonemapBuffer.Term();

    m_TonemapPSO        .Term();
    m_TonemapRootSig    .Term();

    m_RayTracingPSO     .Term();
    m_RayTracingRootSig .Term();
    
    m_CopyRootSig       .Term();
    m_CopyPSO           .Term();

    for(auto i=0; i<2; ++i)
    { m_HistoryTarget[i].Term(); }

    m_TaaRenerer.Term();

    m_Canvas.Term();

    m_SceneParam.Term();

    m_RayGenTable   .Term();
    m_MissTable     .Term();
    m_HitGroupTable .Term();

    for(auto i=0; i<m_ExportData.size(); ++i)
    {
        m_ExportData[i].Pixels   .clear();
        m_ExportData[i].Converted.clear();
    }
    m_ExportData.clear();

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
        CaptureScreen(m_CaptureTarget[idx].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

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
#if (CAMP_RELEASE)
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
    m_CurrView = m_CameraController.GetView();
    m_CurrProj = asdx::Matrix::CreatePerspectiveFieldOfView(
        asdx::ToRadian(37.5f),
        float(m_SceneDesc.Width) / float(m_SceneDesc.Height),
        m_CameraController.GetNearClip(),
        m_CameraController.GetFarClip());
    m_CameraZAxis = m_CameraController.GetAxisZ();

    if (!m_ForceAccumulationOff)
    {
        m_AnimationTime = float(m_Timer.GetRelativeSec());
    }
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

        if (GetFrameCount() == 0)
        { changed = true; }

    #if !(CAMP_RELEASE)
        if (m_RequestReload)
        { changed = true; }

    #endif

    #if CAMP_RELEASE
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

        // 次のフレーム用に記録.
        m_PrevView          = param.View;
        m_PrevProj          = param.Proj;
        m_PrevInvView       = param.InvView;
        m_PrevInvProj       = param.InvProj;
        m_PrevInvViewProj   = param.InvViewProj;
    }

    // デノイズ用 G-Buffer.
    {
        m_AlbedoTarget      .Transition(m_GfxCmdList.GetCommandList(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_NormalTarget      .Transition(m_GfxCmdList.GetCommandList(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_VelocityTarget    .Transition(m_GfxCmdList.GetCommandList(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_ModelDepthTarget  .Transition(m_GfxCmdList.GetCommandList(), D3D12_RESOURCE_STATE_DEPTH_WRITE);

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

        m_Scene.Draw(m_GfxCmdList.GetCommandList());
    }

    // レイトレ実行.
    {
        m_Canvas.Transition(m_GfxCmdList.GetCommandList(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

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
        m_GfxCmdList.SetSRV  (1, m_Scene.GetTLAS(), true);
        m_GfxCmdList.SetSRV  (2, m_Scene.GetIB(), true);
        m_GfxCmdList.SetSRV  (3, m_Scene.GetMB(), true);
        m_GfxCmdList.SetSRV  (4, m_Scene.GetTB(), true);
        m_GfxCmdList.SetTable(5, m_Scene.GetIBL(), true);
        m_GfxCmdList.SetCBV  (6, m_SceneParam.GetResource(), true);
        m_GfxCmdList.SetTable(7, m_Scene.GetLB(), true);

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

    auto threadX = (m_SceneDesc.Width  + 7) / 8;
    auto threadY = (m_SceneDesc.Height + 7) / 8;

    // トーンマップ実行.
    {
        m_Canvas       .Transition(m_GfxCmdList.GetCommandList(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_TonemapBuffer.Transition(m_GfxCmdList.GetCommandList(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        m_GfxCmdList.SetRootSignature(m_TonemapRootSig.GetPtr(), true);
        m_GfxCmdList.SetPipelineState(m_TonemapPSO.GetPtr());
        m_GfxCmdList.SetTable(0, m_Canvas.GetSRV(), true);
        m_GfxCmdList.SetCBV  (1, m_SceneParam.GetResource(), true);
        m_GfxCmdList.SetTable(2, m_TonemapBuffer.GetUAV(), true);
        m_GfxCmdList.Dispatch(threadX, threadY, 1);

        m_GfxCmdList.BarrierUAV(m_TonemapBuffer.GetResource());
    }

    // デノイズ Horizontal-Pass
    {
        m_DenoiseTarget[0].Transition(m_GfxCmdList.GetCommandList(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_TonemapBuffer   .Transition(m_GfxCmdList.GetCommandList(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_ModelDepthTarget.Transition(m_GfxCmdList.GetCommandList(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_NormalTarget    .Transition(m_GfxCmdList.GetCommandList(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        DenoiseParam param = {};
        param.DispathArgsX      = threadX;
        param.DispathArgsY      = threadY;
        param.InvTargetSizeX    = 1.0f / float(m_SceneDesc.Width);
        param.InvTargetSizeY    = 1.0f / float(m_SceneDesc.Height);
        param.OffsetX           = 1.0f / float(m_SceneDesc.Width);
        param.OffsetY           = 0.0f;

        m_GfxCmdList.SetRootSignature(m_DenoiseRootSig.GetPtr(), true);
        m_GfxCmdList.SetPipelineState(m_DenoisePSO.GetPtr());

        m_GfxCmdList.SetTable(0, m_DenoiseTarget[0].GetUAV(), true);
        m_GfxCmdList.SetTable(1, m_TonemapBuffer.GetSRV(), true);
        m_GfxCmdList.SetTable(2, m_ModelDepthTarget.GetSRV(), true);
        m_GfxCmdList.SetTable(3, m_NormalTarget.GetSRV(), true);
        m_GfxCmdList.SetConstants(4, 6, &param, 0, true);

        m_GfxCmdList.Dispatch(threadX, threadY, 1);

        m_GfxCmdList.BarrierUAV(m_DenoiseTarget[0].GetResource());
    }

    // デノイズ Vertical-Pass
    {
        m_DenoiseTarget[1].Transition(m_GfxCmdList.GetCommandList(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_DenoiseTarget[0].Transition(m_GfxCmdList.GetCommandList(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_ModelDepthTarget.Transition(m_GfxCmdList.GetCommandList(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_NormalTarget    .Transition(m_GfxCmdList.GetCommandList(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        DenoiseParam param = {};
        param.DispathArgsX      = threadX;
        param.DispathArgsY      = threadY;
        param.InvTargetSizeX    = 1.0f / float(m_SceneDesc.Width);
        param.InvTargetSizeY    = 1.0f / float(m_SceneDesc.Height);
        param.OffsetX           = 0.0f;
        param.OffsetY           = 1.0f / float(m_SceneDesc.Height);

        m_GfxCmdList.SetRootSignature(m_DenoiseRootSig.GetPtr(), true);
        m_GfxCmdList.SetPipelineState(m_DenoisePSO.GetPtr());

        m_GfxCmdList.SetTable(0, m_DenoiseTarget[1].GetUAV(), true);
        m_GfxCmdList.SetTable(1, m_DenoiseTarget[0].GetSRV(), true);
        m_GfxCmdList.SetTable(2, m_ModelDepthTarget.GetSRV(), true);
        m_GfxCmdList.SetTable(3, m_NormalTarget.GetSRV(), true);
        m_GfxCmdList.SetConstants(4, 6, &param, 0, true);

        m_GfxCmdList.Dispatch(threadX, threadY, 1);

        m_GfxCmdList.BarrierUAV(m_DenoiseTarget[1].GetResource());
    }

    // TemporalAA
    {
        m_CaptureTarget[m_CaptureTargetIndex].Transition(m_GfxCmdList.GetCommandList(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_HistoryTarget[m_CurrHistoryIndex]  .Transition(m_GfxCmdList.GetCommandList(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_DenoiseTarget[1]                   .Transition(m_GfxCmdList.GetCommandList(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_HistoryTarget[m_PrevHistoryIndex]  .Transition(m_GfxCmdList.GetCommandList(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_VelocityTarget                     .Transition(m_GfxCmdList.GetCommandList(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_ModelDepthTarget                   .Transition(m_GfxCmdList.GetCommandList(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        auto jitter = asdx::TaaRenderer::CalcJitter(GetFrameCount(), m_SceneDesc.Width, m_SceneDesc.Height);

        m_TaaRenerer.RenderCS(
            m_GfxCmdList.GetCommandList(),
            m_CaptureTarget[m_CaptureTargetIndex].GetUAV(),
            m_HistoryTarget[m_CurrHistoryIndex].GetUAV(),
            m_DenoiseTarget[1].GetSRV(),
            m_HistoryTarget[m_PrevHistoryIndex].GetSRV(),
            m_VelocityTarget.GetSRV(),
            m_ModelDepthTarget.GetSRV(),
            1.0f,
            0.1f,
            jitter);

        m_GfxCmdList.BarrierUAV(m_CaptureTarget[m_CaptureTargetIndex].GetUAV());
        m_GfxCmdList.BarrierUAV(m_HistoryTarget[m_CurrHistoryIndex].GetUAV());
    }

    //{
    //    auto cap_idx = m_CaptureTargetIndex;

    //    m_GfxCmdList.BarrierTransition(
    //        m_DenoiseTarget[1].GetResource(), 0,
    //        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
    //        D3D12_RESOURCE_STATE_COPY_SOURCE);

    //    m_GfxCmdList.CopyResource(
    //        m_CaptureTexture[cap_idx].GetPtr(),
    //        m_DenoiseTarget[1].GetResource());
    //}

    // スワップチェインに描画.
    #if !(CAMP_RELEASE)
    {
        auto& target = m_CaptureTarget[m_CaptureTargetIndex];


        m_GfxCmdList.BarrierTransition(
            m_ColorTarget[idx].GetResource(), 0,
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET);

        target.Transition(m_GfxCmdList.GetCommandList(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        const asdx::IShaderResourceView* pSRV = nullptr;
        switch(m_BufferKind)
        {
        case BUFFER_KIND_CANVAS:
            pSRV = target.GetSRV();
            break;

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

        // 2D描画.
        Draw2D();

        m_GfxCmdList.BarrierTransition(
            m_ColorTarget[idx].GetResource(), 0,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT);
    }
    #endif

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

    m_CaptureTargetIndex = (m_CaptureTargetIndex + 1) % 3;
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

        auto pos    = m_CameraController.GetPosition();
        auto target = m_CameraController.GetTarget();
        auto upward = m_CameraController.GetUpward();

        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowSize(ImVec2(140, 0));
        if (ImGui::Begin(u8"フレーム情報", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar))
        {

            ImGui::Text(u8"FPS   : %.3lf", GetFPS());
            ImGui::Text(u8"Frame : %ld", GetFrameCount());
            ImGui::Text(u8"Accum : %ld", m_AccumulatedFrames);
            ImGui::Text(u8"Camera : %.3f", pos.x);
            ImGui::Text(u8"       : %.3f", pos.y);
            ImGui::Text(u8"       : %.3f", pos.z);
            ImGui::Text(u8"Target : %.3f", target.x);
            ImGui::Text(u8"       : %.3f", target.y);
            ImGui::Text(u8"       : %.3f", target.z);
            ImGui::Text(u8"Upward : %.3f", upward.x);
            ImGui::Text(u8"       : %.3f", upward.y);
            ImGui::Text(u8"       : %.3f", upward.z);

        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(10, 130), ImGuiCond_Once);
        if (ImGui::Begin(u8"デバッグ設定", &m_DebugSetting))
        {
            int count = _countof(kBufferKindItems);
            ImGui::Combo(u8"表示バッファ", &m_BufferKind, kBufferKindItems, count);
            ImGui::Checkbox(u8"Accumulation 強制OFF", &m_ForceAccumulationOff);
            if (ImGui::Button(u8"カメラ情報出力"))
            {
                auto& param = m_CameraController.GetParam();
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


        asdx::GuiMgr::Instance().Draw(m_GfxCmdList.GetCommandList());
    #endif
#endif
}

//-----------------------------------------------------------------------------
//      スクリーンキャプチャーを行います.
//-----------------------------------------------------------------------------
void Renderer::CaptureScreen(ID3D12Resource* pResource, D3D12_RESOURCE_STATES state, bool forceSync)
{
    if (pResource == nullptr)
    { return; }

    auto pQueue = asdx::GetGraphicsQueue();
    if (pQueue == nullptr)
    { return; }

    m_GfxCmdList.Reset();

    // 読み戻し実行.
    {
        asdx::ScopedBarrier b0(
            m_GfxCmdList,
            pResource,
            state,
            D3D12_RESOURCE_STATE_COPY_SOURCE);

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

        m_GfxCmdList.CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
    }

    m_GfxCmdList.Close();

    ID3D12CommandList* pCmds[] = {
        m_GfxCmdList.GetCommandList()
    };

    // 基本的に2フレーム前のテクスチャを参照するため, 
    // 実行前の同期は完了されていることが保証されているため必要ない.

    // コマンドを実行.
    pQueue->Execute(_countof(pCmds), pCmds);

    // 待機点を発行.
    auto waitPoint = pQueue->Signal();

    // コピーコマンドの完了を待機.
    if (waitPoint.IsValid())
    { pQueue->Sync(waitPoint); }

    // ファイルに出力.
    {
        uint8_t* ptr = nullptr;
        auto idx = m_ExportIndex;
        auto hr  = m_ReadBackTexture->Map(0, nullptr, reinterpret_cast<void**>(&ptr));

        // 処理に空きがあるかどうかチェック.
        if (!m_ExportData[idx].Processed && !forceSync)
        {
            // 処理中フラグを立てる.
            m_ExportData[idx].Processed = true;

            if (SUCCEEDED(hr))
            {
                memcpy(m_ExportData[idx].Pixels.data(), ptr, m_ExportData[idx].Pixels.size());
                m_ExportData[idx].FrameIndex = m_CaptureIndex;
            }
            m_ReadBackTexture->Unmap(0, nullptr);

            // 画像に出力.
            _beginthreadex(nullptr, 0, Export, &m_ExportData[idx], 0, nullptr);
        }
        else
        {
            // 空きが無いので同期処理.
            ExportData exportData;
            exportData.Pixels.resize(m_SceneDesc.Width * m_SceneDesc.Height * 4);
            exportData.FrameIndex  = 0;
            exportData.Width       = m_SceneDesc.Width;
            exportData.Height      = m_SceneDesc.Height;
            exportData.Processed   = true;

            if (SUCCEEDED(hr))
            {
                memcpy(exportData.Pixels.data(), ptr, exportData.Pixels.size());
                exportData.FrameIndex = m_CaptureIndex;
            }
            m_ReadBackTexture->Unmap(0, nullptr);

            // 同期でファイルを出力.
            Export(&exportData);
        }

        m_CaptureIndex++;
        m_ExportIndex = (m_ExportIndex + 1) % m_ExportData.size();
    }
}

bool Renderer::CreateRayTracingPipeline
(
    const void*                     pBinary,
    size_t                          binarySize,
    asdx::RayTracingPipelineState&  pso,
    asdx::ShaderTable&              rayGen,
    asdx::ShaderTable&              miss,
    asdx::ShaderTable&              hitGroup
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
        desc.pGlobalRootSignature       = m_RayTracingRootSig.GetPtr();
        desc.DXILLibrary                = { pBinary, binarySize };
        desc.ExportCount                = _countof(exports);
        desc.pExports                   = exports;
        desc.HitGroupCount              = _countof(groups);
        desc.pHitGroups                 = groups;
        desc.MaxPayloadSize             = sizeof(Payload);
        desc.MaxAttributeSize           = sizeof(asdx::Vector2);
        desc.MaxTraceRecursionDepth     = 1;

        if (!pso.Init(pDevice, desc))
        {
            ELOGA("Error : RayTracing PSO Failed.");
            return false;
        }
    }

    // レイ生成テーブル.
    {
        asdx::ShaderRecord record = {};
        record.ShaderIdentifier = pso.GetShaderIdentifier(L"OnGenerateRay");

        asdx::ShaderTable::Desc desc = {};
        desc.RecordCount    = 1;
        desc.pRecords       = &record;

        if (!rayGen.Init(pDevice, &desc))
        {
            ELOGA("Error : RayGenTable Init Failed.");
            return false;
        }
    }

    // ミステーブル.
    {
        asdx::ShaderRecord record[2] = {};
        record[0].ShaderIdentifier = pso.GetShaderIdentifier(L"OnMiss");
        record[1].ShaderIdentifier = pso.GetShaderIdentifier(L"OnShadowMiss");

        asdx::ShaderTable::Desc desc = {};
        desc.RecordCount = 2;
        desc.pRecords    = record;

        if (!miss.Init(pDevice, &desc))
        {
            ELOGA("Error : MissTable Init Failed.");
            return false;
        }
    }

    // ヒットグループ.
    {
        asdx::ShaderRecord record[2];
        record[0].ShaderIdentifier = pso.GetShaderIdentifier(L"StandardHit");
        record[1].ShaderIdentifier = pso.GetShaderIdentifier(L"ShadowHit");

        asdx::ShaderTable::Desc desc = {};
        desc.RecordCount = 2;
        desc.pRecords    = record;

        if (!hitGroup.Init(pDevice, &desc))
        {
            ELOGA("Error : HitGroupTable Init Failed.");
            return false;
        }
    }

    return true;
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

    asdx::RefPtr<asdx::IBlob> DevShader;

    // シェーダコンパイル.
    if (!CompileShader(L"../res/shader/RtCamp.hlsl", DevShader.GetAddress()))
    {
        return;
    }

    if (!CreateRayTracingPipeline(
        DevShader->GetBufferPointer(),
        DevShader->GetBufferSize(),
        m_DevRayTracingPSO,
        m_DevRayGenTable,
        m_DevMissTable,
        m_DevHitGroupTable))
    {
        return;
    }

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
