//-----------------------------------------------------------------------------
// File : RendererApp.cpp
// Desc : Renderer.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <RendererApp.h>
#include <fnd/asdxLogger.h>
#include <fnd/asdxMisc.h>
#include <process.h>
#include <fpng.h>
#include <OBJLoader.h>

#if ASDX_ENABLE_IMGUI
#include "../external/asdx12/external/imgui/imgui.h"
#endif

#if RTC_TARGET == RTC_DEVELOP
#include <pix3.h>
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
#include "../res/shader/Compile/PreBlurCS.inc"
#include "../res/shader/Compile/TemporalAccumulationCS.inc"
#include "../res/shader/Compile/DenoiserCS.inc"
#include "../res/shader/Compile/TemporalStabilizationCS.inc"
#include "../res/shader/Compile/PostBlurCS.inc"
#include "../asdx12/res/shaders/Compiled/TaaCS.inc"
#include "../asdx12/res/shaders/Compiled/CopyPS.inc"

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
#define SCENE_SETTING_PATH     ("../res/scene/scene_setting.txt")
#define CAMERA_SETTING_PATH    ("../res/scene/camera_setting.txt")
#define RELOAD_SHADER_STATE_NONE    (0)
#define RELOAD_SHADER_STATE_SUCCESS (1)
#define RELOAD_SHADER_STATE_FAILED  (-1)


///////////////////////////////////////////////////////////////////////////////
// ScopedMarker class
///////////////////////////////////////////////////////////////////////////////
class ScopedMarker
{
public:
    ScopedMarker(ID3D12GraphicsCommandList* pCmd, const char* tag)
    {
        m_pCmd = pCmd;
        PIXBeginEvent(pCmd, 0, tag);
    }

    ~ScopedMarker()
    {
        PIXEndEvent(m_pCmd);
        m_pCmd = nullptr;
    }

private:
    ID3D12GraphicsCommandList* m_pCmd = nullptr;
};
#endif

#ifdef ASDX_ENABLE_IMGUI
///////////////////////////////////////////////////////////////////////////////
// BUFFER_KIND enum
///////////////////////////////////////////////////////////////////////////////
enum BUFFER_KIND
{
    BUFFER_KIND_RENDERED  = 0,
    BUFFER_KIND_ALBEDO,
    BUFFER_KIND_NORMAL,
    BUFFER_KIND_ROUGHNESS,
    BUFFER_KIND_VELOCITY,
};

///////////////////////////////////////////////////////////////////////////////
// SAMPLING_TYPE enum
///////////////////////////////////////////////////////////////////////////////
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
// DENOISER_PARAM enum
///////////////////////////////////////////////////////////////////////////////
enum DENOISER_PARAM
{
    DENOISER_PARAM_CBV0,
    DENOISER_PARAM_CBV1,
    DENOISER_PARAM_CBV2,
    DENOISER_PARAM_SRV0,
    DENOISER_PARAM_SRV1,
    DENOISER_PARAM_SRV2,
    DENOISER_PARAM_SRV3,
    DENOISER_PARAM_SRV4,
    DENOISER_PARAM_SRV5,
    DENOISER_PARAM_UAV0,
    DENOISER_PARAM_UAV1,
    MAX_DENOISER_PARAM_COUNT,
};

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
    float           FovY;
    float           NearClip;
    float           FarClip;
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
    uint32_t        ScreenWidth;
    uint32_t        ScreenHeight;
    uint32_t        IgnoreHistory;
    float           Sharpness;
    asdx::Matrix    Proj;
    asdx::Matrix    View;
    float           NearClip;
    float           FarClip;
    asdx::Vector2   UVToViewParam;
};

///////////////////////////////////////////////////////////////////////////////
// TaaParam structure
///////////////////////////////////////////////////////////////////////////////
struct TaaParam
{
    float           Gamma;
    float           BlendFactor;
    asdx::Vector2   MapSize;
    asdx::Vector2   InvMapSize;
    asdx::Vector2   Jitter;
    uint32_t        Flags;
    uint32_t        Reserved[3];
};

//-----------------------------------------------------------------------------
//      回転量を計算します.
//-----------------------------------------------------------------------------
asdx::Vector4 CalcRotator(float angleRad, float angleScale)
{
    float ca = std::cos(angleRad);
    float sa = std::sin(angleRad);

    asdx::Vector4 result;
    result.x =  ca * angleScale;
    result.y =  sa * angleScale;
    result.z = -sa * angleScale;
    result.w =  ca * angleScale;
    return result;
}

//-----------------------------------------------------------------------------
//      画像に出力します.
//-----------------------------------------------------------------------------
unsigned Export(void* args)
{
    auto data = reinterpret_cast<r3d::Renderer::ExportData*>(args);
    if (data == nullptr)
    { return -1; }

    // 解放されると不味いので参照カウンタを上げる.
    data->pResources->AddRef();

    // メモリマッピング.
    uint8_t* ptr = nullptr;
    auto hr = data->pResources->Map(0, nullptr, reinterpret_cast<void**>(&ptr));
    if (FAILED(hr))
    {
        data->pResources->Release();
        return -1;
    }

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

    // 参照カウンタを下げる.
    data->pResources->Release();

    return 0;
}

//-----------------------------------------------------------------------------
//      Radical Inverse Function of Sobol.
//-----------------------------------------------------------------------------
double Sobol(uint32_t i, uint32_t r=0)
{
    // [Kollig 2002] Thomas Kollig, Alexander Keller
    // "Efficient Multidimensional Sampling",
    // Eurographics 2002, Vol.21, No.3.
    // Section 7. Appendix: Algorithms.

    for(uint32_t v = 1 << 31; i; i >> 1, v ^= v >> 1)
    {
        if (i & 1)
        { r ^= v; }
    }

    return (double)r / (double)0x100000000LL;
}

//-----------------------------------------------------------------------------
//      Radical Inverse Function of Larcher and Pillichshammer
//-----------------------------------------------------------------------------
double LarcherPillichshammer(uint32_t i, uint32_t r=0)
{
    // [Kollig 2002] Thomas Kollig, Alexander Keller
    // "Efficient Multidimensional Sampling",
    // Eurographics 2002, Vol.21, No.3.
    // Section 7. Appendix: Algorithms.

    for(uint32_t v = 1 << 31; i; i >>= 1, v |= v >> 1)
    {
        if (i & 1)
        { r ^= v; }
    }

    return (double)r / (double)0x100000000LL;
}

//-----------------------------------------------------------------------------
//      テンポラルジッタ―オフセットを計算します.
//-----------------------------------------------------------------------------
asdx::Vector2 CalcTemporalJitterOffset(uint8_t index)
{
    auto sampleX = LarcherPillichshammer(index + 1, 2) - 0.5;
    auto sampleY = LarcherPillichshammer(index + 1, 3) - 0.5;

    return asdx::Vector2(float(sampleX), float(sampleY));
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
: asdx::Application(L"Ponzu Renderer", desc.OutputWidth, desc.OutputHeight, nullptr, nullptr, nullptr)
, m_SceneDesc(desc)
, m_PcgRandom(1234567)
{
    m_RenderingTimer.Start();

    m_SwapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    m_FovY            = asdx::ToRadian(37.5f);

#if RTC_TARGET == RTC_RELEASE
    m_DeviceDesc.EnableBreakOnError   = false;
    m_DeviceDesc.EnableBreakOnWarning = false;
    m_DeviceDesc.EnableDRED           = false;
    m_DeviceDesc.EnableDebug          = false;
    m_DeviceDesc.EnableCapture        = false;

    // 提出版はウィンドウを生成しない.
    //m_CreateWindow                    = false;
#else
    m_DeviceDesc.EnableCapture        = true;
    m_DeviceDesc.EnableBreakOnWarning = false;
    m_DeviceDesc.EnableDRED           = true;
#endif


    m_Viewport.Width  = float(m_SceneDesc.OutputWidth);
    m_Viewport.Height = float(m_SceneDesc.OutputHeight);

    m_ScissorRect.right  = m_SceneDesc.OutputWidth;
    m_ScissorRect.bottom = m_SceneDesc.OutputHeight;
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

        DLOG("Animation One Frame Time = %lf[sec]", m_AnimationOneFrameTime);

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
    m_RendererViewport.Width    = FLOAT(m_SceneDesc.RenderWidth);
    m_RendererViewport.Height   = FLOAT(m_SceneDesc.RenderHeight);
    m_RendererViewport.MinDepth = 0.0f;
    m_RendererViewport.MaxDepth = 1.0f;

    m_RendererScissor.left      = 0;
    m_RendererScissor.right     = m_SceneDesc.RenderWidth;
    m_RendererScissor.top       = 0;
    m_RendererScissor.bottom    = m_SceneDesc.RenderHeight;

    // キャプチャー用ターゲット.
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_SceneDesc.OutputWidth;
        desc.Height             = m_SceneDesc.OutputHeight;
        desc.DepthOrArraySize   = 1;
        desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.MipLevels          = 1;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        if (!m_CaptureTarget.Init(&desc))
        {
            ELOGA("Error : CaptureTarget Init Failed.");
            return false;
        }

        m_CaptureTarget.SetName(L"CaptureTarget");
    }

    // リードバックテクスチャ生成.
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Alignment          = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        desc.Width              = m_SceneDesc.OutputWidth * m_SceneDesc.OutputHeight * 4;
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
        dstDesc.Width               = m_SceneDesc.OutputWidth;
        dstDesc.Height              = m_SceneDesc.OutputHeight;
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
            m_ExportData[i].Width       = m_SceneDesc.OutputWidth;
            m_ExportData[i].Height      = m_SceneDesc.OutputHeight;
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
        desc.Width              = m_SceneDesc.RenderWidth;
        desc.Height             = m_SceneDesc.RenderHeight;
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
        desc.Width              = m_SceneDesc.RenderWidth;
        desc.Height             = m_SceneDesc.RenderHeight;
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

        D3D12_DESCRIPTOR_RANGE ranges[4] = {};
        asdx::InitRangeAsUAV(ranges[0], 0);
        asdx::InitRangeAsSRV(ranges[1], 4);
        asdx::InitRangeAsSRV(ranges[2], 5);
        asdx::InitRangeAsUAV(ranges[3], 1);

        D3D12_ROOT_PARAMETER params[9] = {};
        asdx::InitAsTable(params[0], 1, &ranges[0], cs);
        asdx::InitAsSRV  (params[1], 0, cs);
        asdx::InitAsSRV  (params[2], 1, cs);
        asdx::InitAsSRV  (params[3], 2, cs);
        asdx::InitAsSRV  (params[4], 3, cs);
        asdx::InitAsTable(params[5], 1, &ranges[1], cs);
        asdx::InitAsCBV  (params[6], 0, cs);
        asdx::InitAsTable(params[7], 1, &ranges[2], cs);
        asdx::InitAsTable(params[8], 1, &ranges[3], cs);

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
        desc.Width              = m_SceneDesc.OutputWidth;
        desc.Height             = m_SceneDesc.OutputHeight;
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
        desc.Width              = m_SceneDesc.RenderWidth;
        desc.Height             = m_SceneDesc.RenderHeight;
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
        desc.Width              = m_SceneDesc.RenderWidth;
        desc.Height             = m_SceneDesc.RenderHeight;
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
        desc.Width              = m_SceneDesc.RenderWidth;
        desc.Height             = m_SceneDesc.RenderHeight;
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
        desc.Width              = m_SceneDesc.RenderWidth;
        desc.Height             = m_SceneDesc.RenderHeight;
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
        desc.Width              = m_SceneDesc.RenderWidth;
        desc.Height             = m_SceneDesc.RenderHeight;
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

    // ヒット距離ターゲット生成.
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_SceneDesc.RenderWidth;
        desc.Height             = m_SceneDesc.RenderHeight;
        desc.DepthOrArraySize   = 1;
        desc.MipLevels          = 1;
        desc.Format             = DXGI_FORMAT_R32_FLOAT;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        desc.ClearColor[0]      = 0.0f;
        desc.ClearColor[1]      = 0.0f;
        desc.ClearColor[2]      = 0.0f;
        desc.ClearColor[3]      = 0.0f;

        if (!m_HitDistance.Init(&desc))
        {
            ELOGA("Error : Hit Distance Buffer Init Failed.");
            return false;
        }

        m_HitDistance.SetName(L"HitDistanceBuffer");
    }

    // アキュムレーションカウントターゲット生成.
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_SceneDesc.RenderWidth;
        desc.Height             = m_SceneDesc.RenderHeight;
        desc.DepthOrArraySize   = 1;
        desc.MipLevels          = 1;
        desc.Format             = DXGI_FORMAT_R8_UINT;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        desc.ClearColor[0]      = 0.0f;
        desc.ClearColor[1]      = 0.0f;
        desc.ClearColor[2]      = 0.0f;
        desc.ClearColor[3]      = 0.0f;

        if (!m_AccumulationCount.Init(&desc))
        {
            ELOG("Error : Accumulation Count Init Failed.");
            return false;
        }

        m_AccumulationCount.SetName(L"AccumulationCount");
    }

    // アキュムレーションカラーターゲット生成.
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_SceneDesc.RenderWidth;
        desc.Height             = m_SceneDesc.RenderHeight;
        desc.DepthOrArraySize   = 1;
        desc.MipLevels          = 1;
        desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;//DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        desc.ClearColor[0]      = 0.0f;
        desc.ClearColor[1]      = 0.0f;
        desc.ClearColor[2]      = 0.0f;
        desc.ClearColor[3]      = 0.0f;

        for(auto i=0; i<2; ++i)
        {
            if (!m_AccumulationColorHistory[i].Init(&desc))
            {
                ELOGA("Error : Accumulation Color History Init Failed.");
                return false;
            }
        }

        m_AccumulationColorHistory[0].SetName(L"AccumulationColor0");
        m_AccumulationColorHistory[1].SetName(L"AccumulationColor1");
    }

    // スタビライゼーションカラーターゲット生成.
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_SceneDesc.RenderWidth;
        desc.Height             = m_SceneDesc.RenderHeight;
        desc.DepthOrArraySize   = 1;
        desc.MipLevels          = 1;
        desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;//DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        desc.ClearColor[0]      = 0.0f;
        desc.ClearColor[1]      = 0.0f;
        desc.ClearColor[2]      = 0.0f;
        desc.ClearColor[3]      = 0.0f;

        for(auto i=0; i<2; ++i)
        {
            if (!m_StabilizationColorHistory[i].Init(&desc))
            {
                ELOGA("Error : Stabilization Color History Init Failed.");
                return false;
            }
        }

        m_StabilizationColorHistory[0].SetName(L"StabilizationColor0");
        m_StabilizationColorHistory[1].SetName(L"StabilizationColor1");
    }

    // ブラーターゲット生成.
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_SceneDesc.RenderWidth;
        desc.Height             = m_SceneDesc.RenderHeight;
        desc.DepthOrArraySize   = 1;
        desc.MipLevels          = 1;
        desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;//DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        desc.ClearColor[0]      = 0.0f;
        desc.ClearColor[1]      = 0.0f;
        desc.ClearColor[2]      = 0.0f;
        desc.ClearColor[3]      = 0.0f;

        if (!m_BlurTarget0.Init(&desc))
        {
            ELOGA("Error : BlurTarget Init Failed.");
            return false;
        }

        if (!m_BlurTarget1.Init(&desc))
        {
            ELOGA("Error : BlurTarget Init Failed.");
            return false;
        }

        m_BlurTarget0.SetName(L"BlurTarget0");
        m_BlurTarget1.SetName(L"BlurTarget1");
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
            ELOGA("Error : Model Pipe Init Failed.");
            return false;
        }
    }

    // テンポラルAA用ルートシグニチャ生成.
    {
        auto cs = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_DESCRIPTOR_RANGE ranges[6] = {};
        asdx::InitRangeAsSRV(ranges[0], 0);
        asdx::InitRangeAsSRV(ranges[1], 1);
        asdx::InitRangeAsSRV(ranges[2], 2);
        asdx::InitRangeAsSRV(ranges[3], 3);
        asdx::InitRangeAsUAV(ranges[4], 0);
        asdx::InitRangeAsUAV(ranges[5], 1);

        D3D12_ROOT_PARAMETER params[7] = {};
        asdx::InitAsCBV  (params[0], 0, cs);
        asdx::InitAsTable(params[1], 1, &ranges[0], cs);
        asdx::InitAsTable(params[2], 1, &ranges[1], cs);
        asdx::InitAsTable(params[3], 1, &ranges[2], cs);
        asdx::InitAsTable(params[4], 1, &ranges[3], cs);
        asdx::InitAsTable(params[5], 1, &ranges[4], cs);
        asdx::InitAsTable(params[6], 1, &ranges[5], cs);

        auto flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.pParameters        = params;
        desc.NumParameters      = _countof(params);
        desc.pStaticSamplers    = asdx::GetStaticSamplers();
        desc.NumStaticSamplers  = asdx::GetStaticSamplerCounts();
        desc.Flags              = flags;

        if (!asdx::InitRootSignature(pDevice, &desc, m_TaaRootSig.GetAddress()))
        {
            ELOG("Error : TemporalAA Signature Init Failed.");
            return false;
        }
    }

    // テンポラルAA用パイプラインステート生成.
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = m_TaaRootSig.GetPtr();
        desc.CS             = { TaaCS, sizeof(TaaCS) };

        if (!m_TaaPipe.Init(pDevice, &desc))
        {
            ELOGA("Error : Taa Pipe Init Failed.");
            return false;
        }
    }

    // テンポラルAA用定数バッファ.
    {
        auto size = asdx::RoundUp(sizeof(TaaParam), 256);
        if (!m_TaaParam.Init(size))
        {
            ELOGA("Error : Taa Constant Buffer Init Failed.");
            return false;
        }
    }

    // デノイザー用ルートシグニチャ生成.
    {
        auto cs = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_DESCRIPTOR_RANGE ranges[8] = {};
        asdx::InitRangeAsSRV(ranges[0], 0);
        asdx::InitRangeAsSRV(ranges[1], 1);
        asdx::InitRangeAsSRV(ranges[2], 2);
        asdx::InitRangeAsSRV(ranges[3], 3);
        asdx::InitRangeAsSRV(ranges[4], 4);
        asdx::InitRangeAsSRV(ranges[5], 5);
        asdx::InitRangeAsUAV(ranges[6], 0);
        asdx::InitRangeAsUAV(ranges[7], 1);

        D3D12_ROOT_PARAMETER params[MAX_DENOISER_PARAM_COUNT] = {};
        asdx::InitAsCBV      (params[DENOISER_PARAM_CBV0], 0, cs);
        asdx::InitAsConstants(params[DENOISER_PARAM_CBV1], 1, 4, cs);
        asdx::InitAsConstants(params[DENOISER_PARAM_CBV2], 2, 4, cs);
        asdx::InitAsTable    (params[DENOISER_PARAM_SRV0], 1, &ranges[0], cs);
        asdx::InitAsTable    (params[DENOISER_PARAM_SRV1], 1, &ranges[1], cs);
        asdx::InitAsTable    (params[DENOISER_PARAM_SRV2], 1, &ranges[2], cs);
        asdx::InitAsTable    (params[DENOISER_PARAM_SRV3], 1, &ranges[3], cs);
        asdx::InitAsTable    (params[DENOISER_PARAM_SRV4], 1, &ranges[4], cs);
        asdx::InitAsTable    (params[DENOISER_PARAM_SRV5], 1, &ranges[5], cs);
        asdx::InitAsTable    (params[DENOISER_PARAM_UAV0], 1, &ranges[6], cs);
        asdx::InitAsTable    (params[DENOISER_PARAM_UAV1], 1, &ranges[7], cs);

        auto flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.pParameters        = params;
        desc.NumParameters      = _countof(params);
        desc.pStaticSamplers    = asdx::GetStaticSamplers();
        desc.NumStaticSamplers  = asdx::GetStaticSamplerCounts();
        desc.Flags              = flags;

        if (!asdx::InitRootSignature(pDevice, &desc, m_DenoiserRootSig.GetAddress()))
        {
            ELOG("Error : Denoiser Signature Init Failed.");
            return false;
        }
    }

    // プレブラー用パイプラインステート.
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = m_DenoiserRootSig.GetPtr();
        desc.CS             = { PreBlurCS, sizeof(PreBlurCS) };

        if (!m_PreBlurPipe.Init(pDevice, &desc))
        {
            ELOGA("Error : PreBlur Pipe Init Failed.");
            return false;
        }
    }

    // テンポラルアキュムレーション用パイプラインステート.
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = m_DenoiserRootSig.GetPtr();
        desc.CS             = { TemporalAccumulationCS, sizeof(TemporalAccumulationCS) };

        if (!m_TemporalAccumulationPipe.Init(pDevice, &desc))
        {
            ELOGA("Error : TemporalAccumulation Pipe Init Failed.");
            return false;
        }
    }

    // デノイズブラー用パイプラインステート.
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = m_DenoiserRootSig.GetPtr();
        desc.CS             = { DenoiserCS, sizeof(DenoiserCS) };

        if (!m_DenoiserPipe.Init(pDevice, &desc))
        {
            ELOGA("Error : Denoiser Pipe Init Failed.");
            return false;
        }
    }

    // テンポラルスタビライゼーション用パイプラインステート.
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = m_DenoiserRootSig.GetPtr();
        desc.CS             = { TemporalStabilizationCS, sizeof(TemporalStabilizationCS) };

        if (!m_TemporalStabilizationPipe.Init(pDevice, &desc))
        {
            ELOGA("Error : TemporalStabilization Pipe Init Failed.");
            return false;
        }
    }

    // ポストブラー用パイプラインステート.
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = m_DenoiserRootSig.GetPtr();
        desc.CS             = { PostBlurCS, sizeof(PostBlurCS) };

        if (!m_PostBlurPipe.Init(pDevice, &desc))
        {
            ELOGA("Error : PostBlur Pipe Init Failed.");
            return false;
        }
    }

    // デノイズ用定数バッファ.
    {
        auto size = asdx::RoundUp(sizeof(DenoiseParam), 256);
        if (!m_DenoiseParam.Init(size))
        {
            ELOG("Error : DenoiseParam Init Failed.");
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

    // ワイヤーフレーム描画用 G-Bufferパイプラインステート.
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature                 = m_ModelRootSig.GetPtr();
        desc.VS                             = { ModelVS, sizeof(ModelVS) };
        desc.PS                             = { ModelPS, sizeof(ModelPS) };
        desc.BlendState                     = asdx::BLEND_DESC(asdx::BLEND_STATE_OPAQUE);
        desc.DepthStencilState              = asdx::DEPTH_STENCIL_DESC(asdx::DEPTH_STATE_DEFAULT);
        desc.RasterizerState                = asdx::RASTERIZER_DESC(asdx::RASTERIZER_STATE_CULL_NONE);
        desc.SampleMask                     = D3D12_DEFAULT_SAMPLE_MASK;
        desc.PrimitiveTopologyType          = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
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

        if (!m_WireFramePipe.Init(pDevice, &desc))
        {
            ELOGA("Error : PipelineState Failed.");
            return false;
        }
    }
    #else
    if (m_CreateWindow)
    {
        // コピー用ルートシグニチャ.
        {
            auto ps = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_DESCRIPTOR_RANGE range = {};
            asdx::InitRangeAsSRV(range, 0);

            D3D12_ROOT_PARAMETER param[2] = {};
            asdx::InitAsTable    (param[0], 1, &range, ps);

            D3D12_STATIC_SAMPLER_DESC sampler = {};
            sampler.Filter              = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU            = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressV            = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressW            = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.MipLODBias          = 0.0f;
            sampler.ComparisonFunc      = D3D12_COMPARISON_FUNC_NEVER;
            sampler.BorderColor         = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
            sampler.MinLOD              = 0.0f;
            sampler.MaxLOD              = D3D12_FLOAT32_MAX;
            sampler.ShaderRegister      = 0;
            sampler.RegisterSpace       = 0;
            sampler.ShaderVisibility    = ps;

            auto flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            D3D12_ROOT_SIGNATURE_DESC desc = {};
            desc.pParameters        = param;
            desc.NumParameters      = _countof(param);
            desc.pStaticSamplers    = &sampler;
            desc.NumStaticSamplers  = 1;
            desc.Flags              = flags;

            if (!asdx::InitRootSignature(pDevice, &desc, m_CopyRootSig.GetAddress()))
            {
                ELOG("Error : Copy Root Signature Init Failed.");
                return false;
            }
        }

        // コピー用パイプラインステート.
        {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
            desc.pRootSignature         = m_CopyRootSig.GetPtr();
            desc.VS                     = { TonemapVS, sizeof(TonemapVS) };
            desc.PS                     = { CopyPS, sizeof(CopyPS) };
            desc.BlendState             = asdx::BLEND_DESC(asdx::BLEND_STATE_OPAQUE);
            desc.DepthStencilState      = asdx::DEPTH_STENCIL_DESC(asdx::DEPTH_STATE_DEFAULT);
            desc.RasterizerState        = asdx::RASTERIZER_DESC(asdx::RASTERIZER_STATE_CULL_NONE);
            desc.SampleMask             = D3D12_DEFAULT_SAMPLE_MASK;
            desc.PrimitiveTopologyType  = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            desc.NumRenderTargets       = 1;
            desc.RTVFormats[0]          = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.InputLayout            = asdx::GetQuadLayout();
            desc.SampleDesc.Count       = 1;
            desc.SampleDesc.Quality     = 0;

            if (!m_CopyPipe.Init(pDevice, &desc))
            {
                ELOGA("Error : PipelineState Failed.");
                return false;
            }
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
        auto aspect = float(m_SceneDesc.RenderWidth) / float(m_SceneDesc.RenderHeight);

        auto view = m_AppCamera.GetView();
        auto proj = asdx::Matrix::CreatePerspectiveFieldOfView(
            m_FovY,
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
    auto aspectRatio = float(m_SceneDesc.RenderWidth) / float(m_SceneDesc.RenderHeight);

    if (!m_Camera.Init(m_SceneDesc.CameraFilePath, aspectRatio))
    {
        ELOG("Error : CameraSequence::Init() Failed.");
        return false;
    }

    m_PrevView        = m_Camera.GetPrevView();
    m_PrevProj        = m_Camera.GetPrevView();
    m_PrevInvView     = asdx::Matrix::Invert(m_PrevView);
    m_PrevInvProj     = asdx::Matrix::Invert(m_PrevProj);
    m_PrevInvViewProj = m_PrevInvProj * m_PrevInvView;
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
    SceneExporter sceneExporter;
    std::string sceneExportPath;
    if (!sceneExporter.LoadFromTXT(SCENE_SETTING_PATH, sceneExportPath))
    {
        ELOG("Error : Scene Load Failed.");
        return false;
    }

    if (!m_Scene.Init(sceneExportPath.c_str(), m_GfxCmdList.GetCommandList()))
    {
        ELOG("Error : Scene::Init() Failed.");
        return false;
    }

    CameraSequenceExporter cameraExporter;
    std::string cameraExportPath;
    if (!cameraExporter.LoadFromTXT(CAMERA_SETTING_PATH, cameraExportPath))
    {
        ELOG("Error : Camera Load Failed.");
        return false;
    }

    auto aspectRatio = float(m_SceneDesc.RenderWidth) / float(m_SceneDesc.RenderHeight);
    if (!m_Camera.Init(cameraExportPath.c_str(), aspectRatio))
    {
        ELOG("Error : CameraSequence::Init() Failed.");
        return false;
    }

#else
    // シーン構築.
    {
        std::string path;
        if (!asdx::SearchFilePathA(m_SceneDesc.SceneFilePath, path))
        {
            ELOGA("Error : File Not Found. path = %s", path);
            return false;
        }

        if (!m_Scene.Init(path.c_str(), m_GfxCmdList.GetCommandList()))
        {
            ELOGA("Error : Scene::Init() Failed.");
            return false;
        }
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
    DLOGA("Scene Path = %s", m_SceneDesc.SceneFilePath);

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
        m_WireFramePipe .Term();
    }
    #endif

    for(auto i=0; i<3; ++i) 
    {
        m_ReadBackTexture[i].Reset();
    }

    // シーン関連.
    {
        m_Scene .Term();
        m_Camera.Term();
    }

    // レンダーターゲット関連.
    {
        m_CaptureTarget.Term();

        for(auto i=0; i<2; ++i)
        {
            m_ColorHistory[i]               .Term();
            m_AccumulationColorHistory [i]  .Term();
            m_StabilizationColorHistory[i]  .Term();
        }

        m_BlurTarget0       .Term();
        m_BlurTarget1       .Term();
        m_HitDistance       .Term();
        m_AccumulationCount .Term();
        m_Tonemapped        .Term();
        m_Depth             .Term();
        m_Velocity          .Term();
        m_Roughness         .Term();
        m_Normal            .Term();
        m_Albedo            .Term();
        m_Radiance          .Term();
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
        m_DenoiseParam  .Term();
        m_TaaParam      .Term();
        m_SceneParam    .Term();
    }

    // パイプライン関連.
    {
        m_PostBlurPipe              .Term();
        m_TemporalStabilizationPipe .Term();
        m_DenoiserPipe              .Term();
        m_TemporalAccumulationPipe  .Term();
        m_PreBlurPipe               .Term();

        m_CopyPipe   .Term();
        m_TaaPipe    .Term();
        m_TonemapPipe.Term();
        m_ModelPipe  .Term();
        m_RtPipe     .Term();
    }

    // ルートシグニチャ関連.
    {
        m_DenoiserRootSig   .Reset();
        m_CopyRootSig       .Reset();
        m_TaaRootSig        .Reset();
        m_TonemapRootSig    .Reset();
        m_RtRootSig         .Reset();
        m_ModelRootSig      .Reset();
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
            CaptureScreen(m_ReadBackTexture[m_ReadBackTargetIndex].GetPtr());
        }

        ILOG("Rendering Finished.");

        PostQuitMessage(0);
        m_EndRequest = true;

        return;
    }

    // CPUで読み取り.
    m_AnimationElapsedTime += args.ElapsedTime;
    if (m_AnimationElapsedTime >= m_AnimationOneFrameTime && GetFrameCount() > 0)
    {
        // キャプチャー実行.
        CaptureScreen(m_ReadBackTexture[m_ReadBackTargetIndex].GetPtr());

        // 次のフレームに切り替え.
        m_AnimationElapsedTime = 0.0;

        // 適宜調整する.
        auto totalFrame = double(m_SceneDesc.FPS * m_SceneDesc.AnimationTimeSec - (m_CaptureIndex - 1));
        m_AnimationOneFrameTime = (m_SceneDesc.RenderTimeSec - m_Timer.GetRelativeSec()) / totalFrame;
    }

    ChangeFrame(m_CaptureIndex);
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

    float nearClip;
    float farClip;
    float fovY;

#if RTC_TARGET == RTC_RELEASE
    auto aspectRatio = float(m_SceneDesc.RenderWidth) / float(m_SceneDesc.RenderHeight);
    m_Camera.Update(index, aspectRatio);

    m_CurrView    = m_Camera.GetCurrView();
    m_CurrProj    = m_Camera.GetCurrProj();
    m_CameraZAxis = m_Camera.GetCameraDir();

    nearClip = m_Camera.GetNearClip();
    farClip  = m_Camera.GetFarlip();
    fovY     = m_Camera.GetFovY();
#else
    m_CurrView = m_AppCamera.GetView();
    m_CurrProj = asdx::Matrix::CreatePerspectiveFieldOfView(
        m_FovY,
        float(m_SceneDesc.RenderWidth) / float(m_SceneDesc.RenderHeight),
        m_AppCamera.GetNearClip(),
        m_AppCamera.GetFarClip());
    m_CameraZAxis = m_AppCamera.GetAxisZ();

    if (!m_ForceAccumulationOff)
    { m_AnimationTime = float(m_Timer.GetRelativeSec()); }

    nearClip = m_AppCamera.GetNearClip();
    farClip  = m_AppCamera.GetFarClip ();
    fovY     = m_FovY;
#endif

    m_CurrInvView = asdx::Matrix::Invert(m_CurrView);
    m_CurrInvProj = asdx::Matrix::Invert(m_CurrProj);

    auto changed = memcmp(&m_CurrView, &m_PrevView, sizeof(asdx::Matrix)) != 0;

    // 定数バッファ更新.
    {
        auto enableAccumulation = true;

        if (GetFrameCount() == 0)
        { changed = true; }

        if (GetFrameCount() <= 1)
        { m_ResetHistory = true; }

    #if RTC_TARGET == RTC_DEVELOP
        if (m_Dirty)
        {
            changed = true;
            m_Dirty = false;
        }
        if (m_ForceAccumulationOff)
        {
            changed = true;
        }
        if (m_Scene.IsReloading())
        {
            changed = true;
        }
    #endif

        // カメラ変更があったかどうか?
        if (changed)
        {
            enableAccumulation  = false;
            m_AccumulatedFrames = 0;
            m_RenderingTimer.Start();
            m_ResetHistory = true;
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
        param.MinBounce             = 4;
        param.FrameIndex            = GetFrameCount();
        param.SkyIntensity          = 5.0f;
        param.EnableAccumulation    = enableAccumulation;
        param.AccumulatedFrames     = m_AccumulatedFrames;
        param.ExposureAdjustment    = 1.0f;
        param.LightCount            = m_Scene.GetLightCount();
        param.Size.x                = float(m_SceneDesc.RenderWidth);
        param.Size.y                = float(m_SceneDesc.RenderHeight);
        param.Size.z                = 1.0f / param.Size.x;
        param.Size.w                = 1.0f / param.Size.y;
        param.CameraDir             = m_CameraZAxis;
        param.MaxIteration          = MAX_RECURSION_DEPTH;
        param.AnimationTime         = m_AnimationTime;
        param.FovY                  = fovY;
        param.NearClip              = nearClip;
        param.FarClip               = farClip;

        m_SceneParam.SwapBuffer();
        m_SceneParam.Update(&param, sizeof(param));
    }

    // デノイズ用定数バッファ更新.
    {
        DenoiseParam param = {};
        param.ScreenWidth   = m_SceneDesc.RenderWidth;
        param.ScreenHeight  = m_SceneDesc.RenderHeight;
        param.IgnoreHistory = (changed) ? 0x1 : 0;
        param.Sharpness     = (farClip - nearClip) * 0.1f;
        param.View          = m_CurrView;
        param.Proj          = m_CurrProj;
        param.NearClip      = nearClip;
        param.FarClip       = farClip;
        param.UVToViewParam = asdx::Vector2(1.0f / m_CurrProj._11, 1.0f / m_CurrProj._22);

        m_DenoiseParam.SwapBuffer();
        m_DenoiseParam.Update(&param, sizeof(param));
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
#if RTC_TARGET == RTC_DEVELOP
    if (!m_Scene.IsReloading())
#endif
    {
        RTC_DEBUG_CODE(ScopedMarker marker(pCmd, "G-Buffer"));

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
#if RTC_TARGET == RTC_DEVELOP
        if (m_EnableWireFrame)
        {
            pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
            m_WireFramePipe.SetState(pCmd);
        }
        else
        {
            pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            m_ModelPipe.SetState(pCmd);
        }
#else
        pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_ModelPipe.SetState(pCmd);
#endif

        pCmd->SetGraphicsRootConstantBufferView(0, m_SceneParam.GetResource()->GetGPUVirtualAddress());
        pCmd->SetGraphicsRootShaderResourceView(2, m_Scene.GetTB()->GetResource()->GetGPUVirtualAddress());
        pCmd->SetGraphicsRootShaderResourceView(3, m_Scene.GetMB()->GetResource()->GetGPUVirtualAddress());
        pCmd->SetGraphicsRootShaderResourceView(4, m_Scene.GetIB()->GetResource()->GetGPUVirtualAddress());

        m_Scene.Draw(m_GfxCmdList.GetCommandList());
    }
    RTC_DEBUG_CODE(m_Scene.Polling(m_GfxCmdList.GetCommandList()));

    // レイトレ実行.
#if RTC_TARGET == RTC_DEVELOP
    if (!m_Scene.IsReloading())
#endif
    {
        RTC_DEBUG_CODE(ScopedMarker marker(pCmd, "PathTracing"));

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
        pCmd->SetComputeRootDescriptorTable(8, m_HitDistance.GetUAV()->GetHandleGPU());

        DispatchRays(pCmd);

        asdx::UAVBarrier(pCmd, m_Radiance.GetResource());
    }

    auto threadX = (m_SceneDesc.RenderWidth  + 7) / 8;
    auto threadY = (m_SceneDesc.RenderHeight + 7) / 8;

    // トーンマップ実行.
    {
        RTC_DEBUG_CODE(ScopedMarker marker(pCmd, "ToneMapping"));

        auto& inputBuffer = m_Radiance;

        inputBuffer.Transition(pCmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_Tonemapped.Transition(pCmd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        pCmd->SetComputeRootSignature(m_TonemapRootSig.GetPtr());
        m_TonemapPipe.SetState(pCmd);
        pCmd->SetComputeRootDescriptorTable(0, inputBuffer.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootConstantBufferView(1, m_SceneParam.GetResource()->GetGPUVirtualAddress());
        pCmd->SetComputeRootDescriptorTable(2, m_Tonemapped.GetUAV()->GetHandleGPU());
        pCmd->Dispatch(threadX, threadY, 1);

        asdx::UAVBarrier(pCmd, m_Tonemapped.GetResource());
    }

    asdx::Vector3 randomAngle;
    randomAngle.x = m_PcgRandom.GetAsF32() * asdx::ToRadian(360.0f);
    randomAngle.y = m_PcgRandom.GetAsF32() * asdx::ToRadian(360.0f);
    randomAngle.z = m_PcgRandom.GetAsF32() * asdx::ToRadian(360.0f);

    asdx::Vector3 randomScale;
    randomScale.x = 1.0f + (m_PcgRandom.GetAsF32() * 2.0f - 1.0f) * 0.25f;
    randomScale.y = 1.0f + (m_PcgRandom.GetAsF32() * 2.0f - 1.0f) * 0.25f;
    randomScale.z = 1.0f + (m_PcgRandom.GetAsF32() * 2.0f - 1.0f) * 0.25f;

    auto jitterOffset = CalcTemporalJitterOffset(m_TemporalJitterIndex);

    // プレブラー処理.
    {
        RTC_DEBUG_CODE(ScopedMarker marker(pCmd, "PreBlur"));

        auto rotator = CalcRotator(randomAngle.x, randomScale.x);

        auto& inputBuffer = m_Tonemapped;

        asdx::Vector4 blurOffset = {};
        blurOffset.x = 1.0f / float(m_SceneDesc.RenderWidth);
        blurOffset.y = 0.0f;
        blurOffset.z = 1.0f;

        m_BlurTarget0.Transition(pCmd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_Depth      .Transition(pCmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_Normal     .Transition(pCmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_Roughness  .Transition(pCmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_HitDistance.Transition(pCmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_AccumulationCount.Transition(pCmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        inputBuffer.Transition(pCmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        pCmd->SetComputeRootSignature(m_DenoiserRootSig.GetPtr());
        m_PreBlurPipe.SetState(pCmd);
        pCmd->SetComputeRootConstantBufferView(DENOISER_PARAM_CBV0, m_DenoiseParam.GetResource()->GetGPUVirtualAddress());
        pCmd->SetComputeRoot32BitConstants(DENOISER_PARAM_CBV1, 3, &blurOffset, 0);
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV0, m_Depth.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV1, m_Normal.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV2, m_Roughness.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV3, m_HitDistance.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV4, inputBuffer.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV5, m_AccumulationCount.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_UAV0, m_BlurTarget0.GetUAV()->GetHandleGPU());
        pCmd->Dispatch(threadX, threadY, 1);

        asdx::UAVBarrier(pCmd, m_BlurTarget0.GetResource());

        blurOffset.x = 0.0f;
        blurOffset.y = 1.0f / float(m_SceneDesc.RenderHeight);

        m_BlurTarget1.Transition(pCmd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_BlurTarget0.Transition(pCmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        pCmd->SetComputeRoot32BitConstants(DENOISER_PARAM_CBV1, 3, &blurOffset, 0);
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV4, m_BlurTarget0.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_UAV0, m_BlurTarget1.GetUAV()->GetHandleGPU());
        pCmd->Dispatch(threadX, threadY, 1);

        asdx::UAVBarrier(pCmd, m_BlurTarget1.GetResource());
    }

    // テンポラルアキュムレーション処理.
    {
        RTC_DEBUG_CODE(ScopedMarker marker(pCmd, "TemporalAccumulation"));

        struct Constants
        {
            uint32_t        ScreenWidth;
            uint32_t        ScreenHeight;
            asdx::Vector2   Jitter;
        };
        Constants constants = {};
        constants.ScreenWidth   = m_SceneDesc.RenderWidth;
        constants.ScreenHeight  = m_SceneDesc.RenderHeight;
        constants.Jitter        = jitterOffset;

        uint32_t flags = (m_ResetHistory) ? 0x1 : 0;

        m_AccumulationColorHistory[m_PrevHistoryIndex].Transition(pCmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_AccumulationColorHistory[m_CurrHistoryIndex].Transition(pCmd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_Velocity.Transition(pCmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_AccumulationCount.Transition(pCmd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_BlurTarget1.Transition(pCmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        pCmd->SetComputeRootSignature(m_DenoiserRootSig.GetPtr());
        m_TemporalAccumulationPipe.SetState(pCmd);
        pCmd->SetComputeRoot32BitConstants(DENOISER_PARAM_CBV1, 4, &constants, 0);
        pCmd->SetComputeRoot32BitConstants(DENOISER_PARAM_CBV2, 1, &flags, 0);
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV0, m_BlurTarget1.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV1, m_AccumulationColorHistory[m_PrevHistoryIndex].GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV2, m_Velocity.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_UAV0, m_AccumulationColorHistory[m_CurrHistoryIndex].GetUAV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_UAV1, m_AccumulationCount.GetUAV()->GetHandleGPU());
        pCmd->Dispatch(threadX, threadY, 1);

        asdx::UAVBarrier(pCmd, m_AccumulationColorHistory[m_CurrHistoryIndex].GetResource());
    }

    //// Mip Generation and History Fix.
    //{
    //}

    // ブラー処理.
    {
        RTC_DEBUG_CODE(ScopedMarker marker(pCmd, "DenoiseBlur"));

        m_BlurTarget0.Transition(pCmd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_AccumulationCount.Transition(pCmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        asdx::Vector4 blurOffset = {};
        blurOffset.x = 1.0f / float(m_SceneDesc.RenderWidth);
        blurOffset.y = 0.0f;
        blurOffset.z = 1.0f;

        pCmd->SetComputeRootSignature(m_DenoiserRootSig.GetPtr());
        m_DenoiserPipe.SetState(pCmd);
        pCmd->SetComputeRootConstantBufferView(DENOISER_PARAM_CBV0, m_DenoiseParam.GetResource()->GetGPUVirtualAddress());
        pCmd->SetComputeRoot32BitConstants(DENOISER_PARAM_CBV1, 3, &blurOffset, 0);
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV0, m_Depth.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV1, m_Normal.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV2, m_Roughness.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV3, m_HitDistance.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV4, m_AccumulationColorHistory[m_CurrHistoryIndex].GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV5, m_AccumulationCount.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_UAV0, m_BlurTarget0.GetUAV()->GetHandleGPU());
        pCmd->Dispatch(threadX, threadY, 1);

        asdx::UAVBarrier(pCmd, m_BlurTarget0.GetResource());

        blurOffset.x = 0.0f;
        blurOffset.y = 1.0f / float(m_SceneDesc.RenderHeight);

        m_BlurTarget0.Transition(pCmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_BlurTarget1.Transition(pCmd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        pCmd->SetComputeRoot32BitConstants(DENOISER_PARAM_CBV1, 3, &blurOffset, 0);
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV4, m_BlurTarget0.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_UAV0, m_BlurTarget1.GetUAV()->GetHandleGPU());
        pCmd->Dispatch(threadX, threadY, 1);

        asdx::UAVBarrier(pCmd, m_BlurTarget1.GetResource());
    }

    // ポストブラー処理.
    {
        RTC_DEBUG_CODE(ScopedMarker marker(pCmd, "PostBlur"));

        asdx::Vector4 blurOffset = {};
        blurOffset.x = 1.0f / float(m_SceneDesc.RenderWidth);
        blurOffset.y = 0.0f;
        blurOffset.z = 0.5f;

        m_BlurTarget0.Transition(pCmd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_BlurTarget1.Transition(pCmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        pCmd->SetComputeRootSignature(m_DenoiserRootSig.GetPtr());
        m_PostBlurPipe.SetState(pCmd);
        pCmd->SetComputeRootConstantBufferView(DENOISER_PARAM_CBV0, m_DenoiseParam.GetResource()->GetGPUVirtualAddress());
        pCmd->SetComputeRoot32BitConstants(DENOISER_PARAM_CBV1, 3, &blurOffset, 0);
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV0, m_Depth.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV1, m_Normal.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV2, m_Roughness.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV3, m_HitDistance.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV4, m_BlurTarget1.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV5, m_AccumulationCount.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_UAV0, m_BlurTarget0.GetUAV()->GetHandleGPU());
        pCmd->Dispatch(threadX, threadY, 1);

        asdx::UAVBarrier(pCmd, m_BlurTarget0.GetResource());

        blurOffset.x = 0.0f;
        blurOffset.y = 1.0f / float(m_SceneDesc.RenderHeight);

        m_BlurTarget0.Transition(pCmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_BlurTarget1.Transition(pCmd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        pCmd->SetComputeRoot32BitConstants(DENOISER_PARAM_CBV1, 3, &blurOffset, 0);
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV4, m_BlurTarget0.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_UAV0, m_BlurTarget1.GetUAV()->GetHandleGPU());
        pCmd->Dispatch(threadX, threadY, 1);

        asdx::UAVBarrier(pCmd, m_BlurTarget1.GetResource());
    }

    // テンポラルスタビライゼーション処理.
    {
        RTC_DEBUG_CODE(ScopedMarker marker(pCmd, "TemporalStabilization"));

        struct Constants
        {
            uint32_t        ScreenWidth;
            uint32_t        ScreenHeight;
            asdx::Vector2   Jitter;
        };
        Constants constants = {};
        constants.ScreenWidth   = m_SceneDesc.RenderWidth;
        constants.ScreenHeight  = m_SceneDesc.RenderHeight;
        constants.Jitter        = jitterOffset;

        uint32_t flags = (m_ResetHistory) ? 0x1 : 0;

        m_StabilizationColorHistory[m_PrevHistoryIndex].Transition(pCmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_StabilizationColorHistory[m_CurrHistoryIndex].Transition(pCmd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_BlurTarget1.Transition(pCmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        pCmd->SetComputeRootSignature(m_DenoiserRootSig.GetPtr());
        m_TemporalStabilizationPipe.SetState(pCmd);
        pCmd->SetComputeRoot32BitConstants(DENOISER_PARAM_CBV1, 4, &constants, 0);
        pCmd->SetComputeRoot32BitConstants(DENOISER_PARAM_CBV2, 1, &flags, 0);
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV0, m_BlurTarget1.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV1, m_StabilizationColorHistory[m_PrevHistoryIndex].GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_SRV2, m_Velocity.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(DENOISER_PARAM_UAV0, m_StabilizationColorHistory[m_CurrHistoryIndex].GetUAV()->GetHandleGPU());
        pCmd->Dispatch(threadX, threadY, 1);

        asdx::UAVBarrier(pCmd, m_StabilizationColorHistory[m_CurrHistoryIndex].GetResource());
    }

    // ポストエフェクト処理.
    {
    }

    //==========================
    // ここから先は最終解像度.
    //==========================
    threadX = (m_SceneDesc.OutputWidth  + 7) / 8;
    threadY = (m_SceneDesc.OutputHeight + 7) / 8;

    // テンポラルアンチエリアス実行.
    {
        RTC_DEBUG_CODE(ScopedMarker marker(pCmd, "TemporalAntiAliasing"));

        auto& inputBuffer = m_StabilizationColorHistory[m_CurrHistoryIndex];

        TaaParam param = {};
        param.Gamma         = 0.95f;
        param.BlendFactor   = 0.9f;
        param.MapSize       = asdx::Vector2(float(m_SceneDesc.OutputWidth), float(m_SceneDesc.OutputHeight));
        param.InvMapSize    = asdx::Vector2(1.0f / float(m_SceneDesc.OutputWidth), 1.0f / float(m_SceneDesc.OutputHeight));
        param.Jitter        = jitterOffset;
        param.Flags         = (m_ResetHistory) ? 0x1 : 0;

        m_TaaParam.SwapBuffer();
        m_TaaParam.Update(&param, sizeof(param));

        inputBuffer.Transition(pCmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_ColorHistory[m_PrevHistoryIndex].Transition(pCmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_ColorHistory[m_CurrHistoryIndex].Transition(pCmd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_CaptureTarget.Transition(pCmd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        pCmd->SetComputeRootSignature(m_TaaRootSig.GetPtr());
        m_TaaPipe.SetState(pCmd);

        pCmd->SetComputeRootConstantBufferView(0, m_TaaParam.GetResource()->GetGPUVirtualAddress());
        pCmd->SetComputeRootDescriptorTable(1, inputBuffer.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(2, m_ColorHistory[m_PrevHistoryIndex].GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(3, m_Velocity.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(4, m_Depth.GetSRV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(5, m_CaptureTarget.GetUAV()->GetHandleGPU());
        pCmd->SetComputeRootDescriptorTable(6, m_ColorHistory[m_CurrHistoryIndex].GetUAV()->GetHandleGPU());

        pCmd->Dispatch(threadX, threadY, 1);

        asdx::UAVBarrier(pCmd, m_ColorHistory[m_CurrHistoryIndex].GetResource());
        asdx::UAVBarrier(pCmd, m_CaptureTarget.GetResource());
    }

    // リードバックテクスチャにコピー.
    {
        RTC_DEBUG_CODE(ScopedMarker marker(pCmd, "ReadBack"));

        m_CaptureTarget.Transition(pCmd, D3D12_RESOURCE_STATE_COPY_SOURCE);

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.Type                                = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.pResource                           = m_ReadBackTexture[m_CaptureTargetIndex].GetPtr();
        dst.PlacedFootprint.Footprint.Width     = static_cast<UINT>(m_SceneDesc.OutputWidth);
        dst.PlacedFootprint.Footprint.Height    = m_SceneDesc.OutputHeight;
        dst.PlacedFootprint.Footprint.Depth     = 1;
        dst.PlacedFootprint.Footprint.RowPitch  = m_ReadBackPitch;
        dst.PlacedFootprint.Footprint.Format    = DXGI_FORMAT_R8G8B8A8_UNORM;

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.Type                = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource           = m_CaptureTarget.GetResource();
        src.SubresourceIndex    = 0;

        D3D12_BOX box = {};
        box.left    = 0;
        box.right   = m_SceneDesc.OutputWidth;
        box.top     = 0;
        box.bottom  = m_SceneDesc.OutputHeight;
        box.front   = 0;
        box.back    = 1;

        pCmd->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
    }

    // スワップチェインに描画.
    #if RTC_TARGET == RTC_DEVELOP
    {
        RTC_DEBUG_CODE(ScopedMarker marker(pCmd, "DebugOutput"));

        m_ColorTarget[idx].Transition(pCmd, D3D12_RESOURCE_STATE_RENDER_TARGET);

        const asdx::IShaderResourceView* pSRV = nullptr;
        uint32_t type = 0;

        switch(m_BufferKind)
        {
        case BUFFER_KIND_RENDERED:
            m_CaptureTarget.Transition(pCmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            pSRV = m_CaptureTarget.GetSRV();
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
        pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        asdx::DrawQuad(pCmd);

        // 2D描画.
        Draw2D(float(args.ElapsedTime));

        m_ColorTarget[idx].Transition(pCmd, D3D12_RESOURCE_STATE_PRESENT);
    }
    #else
    // カラーターゲットにコピー.
    if (m_CreateWindow)
    {
        RTC_DEBUG_CODE(ScopedMarker marker(pCmd, "WindowOutput"));

        m_CaptureTarget.Transition(pCmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_ColorTarget[idx].Transition(pCmd, D3D12_RESOURCE_STATE_RENDER_TARGET);

        auto pRTV = m_ColorTarget[idx].GetRTV()->GetHandleCPU();
        pCmd->OMSetRenderTargets(1, &pRTV, FALSE, nullptr);
        pCmd->RSSetViewports(1, &m_Viewport);
        pCmd->RSSetScissorRects(1, &m_ScissorRect);
        pCmd->SetGraphicsRootSignature(m_CopyRootSig.GetPtr());
        m_CopyPipe.SetState(pCmd);
        pCmd->SetGraphicsRootDescriptorTable(0, m_CaptureTarget.GetSRV()->GetHandleGPU());
        pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        asdx::DrawQuad(pCmd);

        m_ColorTarget[idx].Transition(pCmd, D3D12_RESOURCE_STATE_PRESENT);
    }
    #endif

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
    Present(0);

    // フレーム同期.
    asdx::FrameSync();

    // シェーダをリロードします.
    RTC_DEBUG_CODE(ReloadShader());

    m_ReadBackTargetIndex = (m_ReadBackTargetIndex + 1) % 3;
    m_CaptureTargetIndex  = (m_CaptureTargetIndex  + 1) % 3;

    m_PrevHistoryIndex = m_CurrHistoryIndex;
    m_CurrHistoryIndex = (m_CurrHistoryIndex + 1) & 0x1;

    m_TemporalJitterIndex = (m_TemporalJitterIndex + 1) % 8;

    m_ResetHistory = false;
}

//-----------------------------------------------------------------------------
//      レイトレーサーを起動します.
//-----------------------------------------------------------------------------
void Renderer::DispatchRays(ID3D12GraphicsCommandList6* pCmd)
{
#if RTC_TARGET == RTC_DEVELOP
    if (m_RtShaderFlags.Get(RELOADED_BIT_INDEX))
    {
        m_DevPipe.Dispatch(pCmd, m_SceneDesc.RenderWidth, m_SceneDesc.RenderHeight);
        return;
    }
#endif
    m_RtPipe.Dispatch(pCmd, m_SceneDesc.RenderWidth, m_SceneDesc.RenderHeight);
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
                m_PreBlurShaderFlags.Set(REQUEST_BIT_INDEX, true);
                m_TemporalAccumulationShaderFlags.Set(REQUEST_BIT_INDEX, true);
                m_DenoiserShaderFlags.Set(REQUEST_BIT_INDEX, true);
                m_PostBlurShaderFlags.Set(REQUEST_BIT_INDEX, true);
                m_TemporalStabilizationShaderFlags.Set(REQUEST_BIT_INDEX, true);
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
#if RTC_TARGET == RTC_DEVELOP
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
void Renderer::Draw2D(float elapsedSec)
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
            m_RenderingTimer.End();
            auto renderingSec = m_RenderingTimer.GetElapsedSec();

            ImGui::Text(u8"FPS   : %.3lf", GetFPS());
            ImGui::Text(u8"Frame : %ld", GetFrameCount());
            ImGui::Text(u8"Accum : %ld", m_AccumulatedFrames);
            ImGui::Text(u8"Render : %.2lf [sec]", renderingSec);
            ImGui::Text(u8"Camera : (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);
            ImGui::Text(u8"Target : (%.2f, %.2f, %.2f)", target.x, target.y, target.z);
            ImGui::Text(u8"Upward : (%.2f, %.2f, %.2f)", upward.x, upward.y, upward.z);

            if (m_ReloadShaderDisplaySec > 0.0f)
            {
                float alpha = (m_ReloadShaderDisplaySec > 1.0f) ? 1.0f : m_ReloadShaderDisplaySec;
                if (m_ReloadShaderState == RELOAD_SHADER_STATE_SUCCESS)
                {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, alpha), u8"Shader Reload Success!!");
                }
                else if (m_ReloadShaderState == RELOAD_SHADER_STATE_FAILED)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, alpha), u8"Shader Reload Failed...");
                }
                m_ReloadShaderDisplaySec -= elapsedSec;
            }
            else
            {
                ImGui::Text(u8"---");
                m_ReloadShaderState = RELOAD_SHADER_STATE_NONE;
                m_ReloadShaderDisplaySec = 0.0f;
            }
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(10, 140), ImGuiCond_Once);
        if (ImGui::Begin(u8"デバッグ設定", &m_DebugSetting))
        {
            int count = _countof(kBufferKindItems);
            ImGui::Combo(u8"ビュー", &m_BufferKind, kBufferKindItems, count);
            ImGui::Checkbox(u8"Accumulation 強制OFF", &m_ForceAccumulationOff);
            ImGui::Checkbox(u8"ワイヤーフレーム", &m_EnableWireFrame);
            if (ImGui::Button(u8"カメラ情報出力"))
            {
                auto& param = m_AppCamera.GetParam();
                printf_s("camera {\n");
                printf_s("    -FrameIndex:\n");
                printf_s("    -Position: %f %f %f\n", param.Position.x, param.Position.y, param.Position.z);
                printf_s("    -Target: %f %f %f\n", param.Target.x, param.Target.y, param.Target.z);
                printf_s("    -Upward: %f %f %f\n", param.Upward.x, param.Upward.y, param.Upward.z);
                printf_s("    -FieldOfView: %f\n", asdx::ToDegree(m_FovY));
                printf_s("    -NearClip: %f\n", param.MinDist);
                printf_s("    -FarClip: %f\n", param.MaxDist);
                printf_s("};\n");
            }
            if (ImGui::Button(u8"シーン設定 リロード"))
            {
                SceneExporter exporter;
                std::string exportPath;
                if (exporter.LoadFromTXT(SCENE_SETTING_PATH, exportPath))
                {
                    m_Scene.Reload(exportPath.c_str());
                }
            }
            if (ImGui::Button(u8"シェーダ リロード"))
            {
                m_RtShaderFlags.Set(REQUEST_BIT_INDEX, true);
                m_TonemapShaderFlags.Set(REQUEST_BIT_INDEX, true);
                m_PreBlurShaderFlags.Set(REQUEST_BIT_INDEX, true);
                m_TemporalAccumulationShaderFlags.Set(REQUEST_BIT_INDEX, true);
                m_DenoiserShaderFlags.Set(REQUEST_BIT_INDEX, true);
                m_PostBlurShaderFlags.Set(REQUEST_BIT_INDEX, true);
                m_TemporalStabilizationShaderFlags.Set(REQUEST_BIT_INDEX, true);
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

    auto idx = m_ExportIndex;
    {
        m_ExportData[idx].pResources = pResource;
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

    {
        const char* paths[] = {
            "Math.hlsli",
            "BRDF.hlsli",
            "TextureUtil.hlsli",
            "Denoiser.hlsli",
            "DenoiserBlur.hlsli",
            "PreBlurCS.hlsl"
        };

        CheckModify(relativePath, m_PreBlurShaderFlags, paths, _countof(paths));
    }

    {
        const char* paths[] = {
            "Math.hlsli",
            "BRDF.hlsli",
            "TextureUtil.hlsli",
            "Denoiser.hlsli",
            "TemporalAccumulationCS.hlsl",
        };

        CheckModify(relativePath, m_TemporalAccumulationShaderFlags, paths, _countof(paths));
    }

    // デノイザーシェーダ.
    {
        const char* paths[] = {
            "Math.hlsli",
            "BRDF.hlsli",
            "TextureUtil.hlsli",
            "Denoiser.hlsli",
            "DenoiserBlur.hlsli",
            "DenoiserCS.hlsl",
        };

        CheckModify(relativePath, m_DenoiserShaderFlags, paths, _countof(paths));
    }

    {
        const char* paths[] = {
            "Math.hlsli",
            "BRDF.hlsli",
            "TextureUtil.hlsli",
            "Denoiser.hlsli",
            "DenoiserBlur.hlsli",
            "TemporalStabilizationCS.hlsl",
        };

        CheckModify(relativePath, m_TemporalStabilizationShaderFlags, paths, _countof(paths));
    }

    {
        const char* paths[] = {
            "Math.hlsli",
            "BRDF.hlsli",
            "TextureUtil.hlsli",
            "Denoiser.hlsli",
            "DenoiserBlur.hlsli",
            "PostBlurCS.hlsl",
        };

        CheckModify(relativePath, m_PostBlurShaderFlags, paths, _countof(paths));
    }
}

//-----------------------------------------------------------------------------
//      シェーダをリロードします.
//-----------------------------------------------------------------------------
void Renderer::ReloadShader()
{
    auto pDevice = asdx::GetD3D12Device();

    auto failedCount  = 0;
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
        else
        {
            failedCount++;
        }

        // コンパイル要求フラグを下げる.
        m_RtShaderFlags.Set(REQUEST_BIT_INDEX, false);
    }

    if (m_PreBlurShaderFlags.Get(REQUEST_BIT_INDEX))
    {
        asdx::RefPtr<asdx::IBlob> blob;
        if (CompileShader(L"../res/shader/PreBlurCS.hlsl", "main", "cs_6_6", blob.GetAddress()))
        {
            m_PreBlurShaderFlags.Set(RELOADED_BIT_INDEX, false);

            m_PreBlurPipe.ReplaceShader(
                asdx::SHADER_TYPE_CS,
                blob->GetBufferPointer(),
                blob->GetBufferSize());
            m_PreBlurPipe.Rebuild();

            m_PreBlurShaderFlags.Set(RELOADED_BIT_INDEX, true);
            successCount++;
        }
        else
        {
            failedCount++;
        }
        m_PreBlurShaderFlags.Set(REQUEST_BIT_INDEX, false);
    }

    if (m_TemporalAccumulationShaderFlags.Get(REQUEST_BIT_INDEX))
    {
        asdx::RefPtr<asdx::IBlob> blob;
        if (CompileShader(L"../res/shader/TemporalAccumulationCS.hlsl", "main", "cs_6_6", blob.GetAddress()))
        {
            m_TemporalAccumulationShaderFlags.Set(RELOADED_BIT_INDEX, false);

            m_TemporalAccumulationPipe.ReplaceShader(
                asdx::SHADER_TYPE_CS,
                blob->GetBufferPointer(),
                blob->GetBufferSize());
            m_TemporalAccumulationPipe.Rebuild();

            m_TemporalAccumulationShaderFlags.Set(RELOADED_BIT_INDEX, true);
            successCount++;
        }
        else
        {
            failedCount++;
        }
        m_TemporalAccumulationShaderFlags.Set(REQUEST_BIT_INDEX, false);
    }

    if (m_DenoiserShaderFlags.Get(REQUEST_BIT_INDEX))
    {
        asdx::RefPtr<asdx::IBlob> blob;
        if (CompileShader(L"../res/shader/DenoiserCS.hlsl", "main", "cs_6_6", blob.GetAddress()))
        {
            m_DenoiserShaderFlags.Set(RELOADED_BIT_INDEX, false);

            m_DenoiserPipe.ReplaceShader(
                asdx::SHADER_TYPE_CS,
                blob->GetBufferPointer(),
                blob->GetBufferSize());
            m_DenoiserPipe.Rebuild();

            m_DenoiserShaderFlags.Set(RELOADED_BIT_INDEX, true);
            successCount++;
        }
        else
        {
            failedCount++;
        }
        m_DenoiserShaderFlags.Set(REQUEST_BIT_INDEX, false);
    }

    if (m_TemporalStabilizationShaderFlags.Get(REQUEST_BIT_INDEX))
    {
        asdx::RefPtr<asdx::IBlob> blob;
        if (CompileShader(L"../res/shader/TemporalStabilizationCS.hlsl", "main", "cs_6_6", blob.GetAddress()))
        {
            m_TemporalStabilizationShaderFlags.Set(RELOADED_BIT_INDEX, false);

            m_TemporalAccumulationPipe.ReplaceShader(
                asdx::SHADER_TYPE_CS,
                blob->GetBufferPointer(),
                blob->GetBufferSize());
            m_TemporalAccumulationPipe.Rebuild();

            m_TemporalAccumulationShaderFlags.Set(RELOADED_BIT_INDEX, true);
            successCount++;
        }
        else
        {
            failedCount++;
        }
        m_TemporalStabilizationShaderFlags.Set(REQUEST_BIT_INDEX, false);
    }

    if (m_PostBlurShaderFlags.Get(REQUEST_BIT_INDEX))
    {
        asdx::RefPtr<asdx::IBlob> blob;
        if (CompileShader(L"../res/shader/PostBlurCS.hlsl", "main", "cs_6_6", blob.GetAddress()))
        {
            m_PostBlurShaderFlags.Set(RELOADED_BIT_INDEX, false);

            m_PostBlurPipe.ReplaceShader(
                asdx::SHADER_TYPE_CS,
                blob->GetBufferPointer(),
                blob->GetBufferSize());
            m_PostBlurPipe.Rebuild();

            m_PostBlurShaderFlags.Set(RELOADED_BIT_INDEX, true);
            successCount++;
        }
        else
        {
            failedCount++;
        }
        m_PostBlurShaderFlags.Set(REQUEST_BIT_INDEX, false);
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
        else
        {
            failedCount++;
        }

        // コンパイル要求フラグを下げる.
        m_TonemapShaderFlags.Set(REQUEST_BIT_INDEX, false);
    }

    if (failedCount == 0 && successCount > 0) {
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

        m_Dirty = true;
        m_ReloadShaderState = RELOAD_SHADER_STATE_SUCCESS;

        // 5秒間表示.
        m_ReloadShaderDisplaySec = 5.0;

    }
    else if (failedCount > 0)
    {
        m_ReloadShaderState = RELOAD_SHADER_STATE_FAILED;

        // 5秒間表示.
        m_ReloadShaderDisplaySec = 5.0;
    }

}
#endif//(!CAMP_RELEASE)

} // namespace r3d
