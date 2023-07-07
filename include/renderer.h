//-----------------------------------------------------------------------------
// File : renderer.h
// Desc : Renderer.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------
#pragma once

// レイトレ合宿提出モード 
#ifndef CAMP_RELEASE
#define CAMP_RELEASE        (0)     // 1なら提出モード.
#endif

#if (CAMP_RELEASE == 0)
#define ASDX_ENABLE_IMGUI   (1)
#endif

//-----------------------------------------------------------------------------
// Include
//-----------------------------------------------------------------------------
#include <Macro.h>
#include <fnd/asdxBitFlags.h>
#include <fw/asdxApp.h>
#include <fw/asdxAppCamera.h>
#include <gfx/asdxPipelineState.h>
#include <gfx/asdxRayTracing.h>
#include <gfx/asdxCommandQueue.h>
#include <model_mgr.h>
#include <scene.h>

#if RTC_TARGET == RTC_DEVELOP
#include <gfx/asdxShaderCompiler.h>
#include <edit/asdxFileWatcher.h>
#endif

#ifdef ASDX_ENABLE_IMGUI
#include <edit/asdxGuiMgr.h>
#endif


namespace r3d {

///////////////////////////////////////////////////////////////////////////////
// SceneDesc structure
///////////////////////////////////////////////////////////////////////////////
struct SceneDesc
{
    double      RenderTimeSec;      // 描画時間[sec]
    uint32_t    Width;              // 横幅[px]
    uint32_t    Height;             // 縦幅[px]
    double      FPS;                // Frame Per Second.
    double      AnimationTimeSec;   // 総アニメーション時間.
    const char* Path;
};


///////////////////////////////////////////////////////////////////////////////
// Renderer class
///////////////////////////////////////////////////////////////////////////////
class Renderer : public asdx::Application
#if RTC_TARGET == RTC_TARGET_DEVELOP
    , public asdx::IFileUpdateListener
#endif
{
    //=========================================================================
    // list of friend classes and methods.
    //=========================================================================
    /* NOTHING */

public:
    //////////////////////////////////////////////////////////////////////////
    // ExportData structure
    //////////////////////////////////////////////////////////////////////////
    struct ExportData
    {
        std::vector<uint8_t>    Converted;
        uint32_t                FrameIndex;
        uint32_t                Width;
        uint32_t                Height;
        ID3D12Resource*         pResources;
        asdx::WaitPoint         WaitPoint;
        asdx::CommandQueue*     pQueue;
        bool                    Processed;
    };

    //=========================================================================
    // public variables.
    //=========================================================================
    /* NOTHING */

    //=========================================================================
    // public methods.
    //=========================================================================
    Renderer(const SceneDesc& desc);
    ~Renderer();

private:
    ///////////////////////////////////////////////////////////////////////////
    // RayTracingPipe structure
    ///////////////////////////////////////////////////////////////////////////
    struct RayTracingPipe
    {
        asdx::RayTracingPipelineState   PipelineState;
        asdx::ShaderTable               RayGen;
        asdx::ShaderTable               Miss;
        asdx::ShaderTable               HitGroup;

        bool Init(ID3D12RootSignature* pRootSig, const void* binary, size_t binarySize);
        void Term();
        void Dispatch(ID3D12GraphicsCommandList6* pCmd, uint32_t width, uint32_t height);
    };

    //=========================================================================
    // private variables.
    //=========================================================================
    SceneDesc                           m_SceneDesc;
    asdx::WaitPoint                     m_FrameWaitPoint;
    asdx::RefPtr<ID3D12RootSignature>   m_ModelRootSig;
    asdx::RefPtr<ID3D12RootSignature>   m_RtRootSig;
    asdx::RefPtr<ID3D12RootSignature>   m_TonemapRootSig;

    RayTracingPipe                  m_RtPipe;
    asdx::PipelineState             m_ModelPipe;
    asdx::PipelineState             m_TonemapPipe;

    asdx::ConstantBuffer            m_SceneParam;

    asdx::ComputeTarget             m_Radiance;         // 放射輝度.
    asdx::ColorTarget               m_Albedo;           // G-Buffer アルベド.
    asdx::ColorTarget               m_Normal;           // G-Buffer 法線.
    asdx::ColorTarget               m_Roughness;        // G-Buffer ラフネス.
    asdx::ColorTarget               m_Velocity;         // G-Buffer 速度.
    asdx::DepthTarget               m_Depth;            // 深度.
    asdx::ComputeTarget             m_Tonemapped;       // トーンマップ適用済み.
    asdx::ComputeTarget             m_ColorHistory[2];  // カラーヒストリーバッファ.
    asdx::ComputeTarget             m_CaptureTarget[3];

    Scene                           m_Scene;
    asdx::Camera                    m_Camera;


    asdx::RefPtr<ID3D12Resource>    m_ReadBackTexture;
    uint32_t                        m_ReadBackPitch = 0;
    std::vector<ExportData>         m_ExportData;
    size_t                          m_ExportIndex   = 0;
    uint32_t                        m_CaptureIndex  = 0;
    uint32_t                        m_CaptureTargetIndex = 0;
    uint32_t                        m_AccumulatedFrames = 0;

    double                          m_AnimationOneFrameTime = 0;
    double                          m_AnimationElapsedTime  = 0;
    float                           m_AnimationTime = 0.0f;

    asdx::Matrix        m_CurrView;
    asdx::Matrix        m_CurrProj;
    asdx::Matrix        m_CurrInvView;
    asdx::Matrix        m_CurrInvProj;
    asdx::Vector3       m_CameraZAxis;

    asdx::Matrix        m_PrevView;
    asdx::Matrix        m_PrevProj;
    asdx::Matrix        m_PrevInvView;
    asdx::Matrix        m_PrevInvProj;
    asdx::Matrix        m_PrevInvViewProj;

    uint8_t             m_CurrHistoryIndex = 0;
    uint8_t             m_PrevHistoryIndex = 1;

    D3D12_VIEWPORT      m_RendererViewport = {};
    D3D12_RECT          m_RendererScissor  = {};


    bool                m_Dirty = false;

    bool                m_EndRequest = false;
    bool                m_ForceChanged = false;
    uint64_t            m_MyFrameCount = 0;

#if RTC_TARGET == RTC_DEVELOP
    //+++++++++++++++++++
    //      開発用.
    //+++++++++++++++++++
    bool                            m_DebugSetting          = true;
    //bool                            m_ReloadShader          = false;
    //bool                            m_RequestReload         = false;
    bool                            m_ForceAccumulationOff  = false;

    int                             m_BufferKind = 0;

    asdx::AppCamera                     m_AppCamera;
    asdx::FileWatcher                   m_ShaderWatcher;
    RayTracingPipe                      m_DevPipe;
    asdx::RefPtr<ID3D12RootSignature>   m_DebugRootSig;
    asdx::PipelineState                 m_DebugPipe;

    asdx::BitFlags8                 m_RtShaderFlags;
    asdx::BitFlags8                 m_TonemapShaderFlags;
#endif

    //=========================================================================
    // private methods.
    //=========================================================================
    bool OnInit() override;
    void OnTerm() override;
    void OnFrameMove(asdx::FrameEventArgs& args) override;
    void OnFrameRender(asdx::FrameEventArgs& args) override;
    void OnResize(const asdx::ResizeEventArgs& args) override;
    void OnKey(const asdx::KeyEventArgs& args) override;
    void OnMouse(const asdx::MouseEventArgs& args) override;
    void OnTyping(uint32_t keyCode) override;

    void Draw2D();

    bool SystemSetup();
    bool BuildScene();
    void DispatchRays(ID3D12GraphicsCommandList6* pCmd);

    void ChangeFrame(uint32_t index);
    void CaptureScreen(ID3D12Resource* pResource);

#if RTC_TARGET == RTC_DEVELOP
    bool BuildTestScene();
    void ReloadShader();

    // ファイル更新コールバック.
    void OnUpdate(
        asdx::ACTION_TYPE actionType,
        const char* directoryPath,
        const char* relativePath) override;
#endif
};

} // namespace r3d
