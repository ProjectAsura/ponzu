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
#include <fw/asdxApp.h>
#include <fw/asdxCameraController.h>
#include <gfx/asdxRootSignature.h>
#include <gfx/asdxPipelineState.h>
#include <gfx/asdxRayTracing.h>
#include <gfx/asdxCommandQueue.h>
#include <gfx/asdxQuad.h>
#include <renderer/asdxTaaRenderer.h>
#include <edit/asdxGuiMgr.h>
#include <model_mgr.h>
#include <target_wrapper.h>
#include <scene.h>

#if (!CAMP_RELEASE)
#include <gfx/asdxShaderCompiler.h>
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
        std::vector<uint8_t>    Pixels;
        std::vector<uint8_t>    Converted;
        uint32_t                FrameIndex;
        uint32_t                Width;
        uint32_t                Height;
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
    //=========================================================================
    // private variables.
    //=========================================================================
    SceneDesc                       m_SceneDesc;
    asdx::WaitPoint                 m_FrameWaitPoint;
    asdx::RootSignature             m_RayTracingRootSig;
    asdx::RayTracingPipelineState   m_RayTracingPSO;
    ComputeView             m_Canvas;
    asdx::ConstantBuffer            m_SceneParam;
    asdx::ShaderTable               m_RayGenTable;
    asdx::ShaderTable               m_MissTable;
    asdx::ShaderTable               m_HitGroupTable;
    ColorView               m_AlbedoTarget;
    ColorView               m_NormalTarget;
    ColorView               m_VelocityTarget;
    DepthView               m_ModelDepthTarget;
    asdx::RootSignature             m_ModelRootSig;
    asdx::PipelineState             m_ModelPSO;
    Scene                           m_Scene;
    asdx::Camera                    m_Camera;
    ComputeView             m_TonemapBuffer;


    ComputeView             m_CaptureTarget[3];
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

    asdx::RootSignature m_DenoiseRootSig;
    asdx::PipelineState m_DenoisePSO;
    ComputeView         m_DenoiseTarget[2];

    asdx::RootSignature m_TonemapRootSig;
    asdx::PipelineState m_TonemapPSO;

    asdx::TaaRenderer   m_TaaRenerer;
    ComputeView         m_HistoryTarget[2];
    uint8_t             m_CurrHistoryIndex = 0;
    uint8_t             m_PrevHistoryIndex = 1;

    asdx::RootSignature m_CopyRootSig;
    asdx::PipelineState m_CopyPSO;

    bool                m_EndRequest = false;
    bool                m_ForceChanged = false;
    uint64_t            m_MyFrameCount = 0;

#if (!CAMP_RELEASE)
    //+++++++++++++++++++
    //      開発用.
    //+++++++++++++++++++
    bool                            m_DebugSetting          = true;
    bool                            m_ReloadShader          = false;
    bool                            m_RequestReload         = false;
    bool                            m_ForceAccumulationOff  = false;
    asdx::RayTracingPipelineState   m_DevRayTracingPSO;
    asdx::ShaderTable               m_DevRayGenTable;
    asdx::ShaderTable               m_DevMissTable;
    asdx::ShaderTable               m_DevHitGroupTable;

    int                             m_BufferKind = 0;
    asdx::CameraController          m_CameraController;
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

    void ChangeFrame(uint32_t index);
    void CaptureScreen(ID3D12Resource* pResource, D3D12_RESOURCE_STATES state, bool forceSync = false);

    bool CreateRayTracingPipeline(
        const void*                     pBinary,
        size_t                          binarySize,
        asdx::RayTracingPipelineState&  pso,
        asdx::ShaderTable&              rayGen,
        asdx::ShaderTable&              missTable,
        asdx::ShaderTable&              hitGroup);

#if (!CAMP_RELEASE)
    bool BuildTestScene();
    void ReloadShader();
    bool CompileShader(const wchar_t* path, asdx::IBlob** ppBlob);
#endif
};

} // namespace r3d
