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

#ifndef ENABLE_RESTIR
#define ENABLE_RESTIR       (0)
#endif//ENABLE_RESTIR

//-----------------------------------------------------------------------------
// Include
//-----------------------------------------------------------------------------
#include <fw/asdxApp.h>
#include <fw/asdxCameraController.h>
#include <gfx/asdxRootSignature.h>
#include <gfx/asdxPipelineState.h>
#include <gfx/asdxRayTracing.h>
#include <gfx/asdxTarget.h>
#include <gfx/asdxCommandQueue.h>
#include <gfx/asdxQuad.h>
#include <edit/asdxGuiMgr.h>
#include <model_mgr.h>
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
// MeshDrawCall structure
///////////////////////////////////////////////////////////////////////////////
struct MeshDrawCall
{
    uint32_t                    StartIndex;
    uint32_t                    IndexCount;
    uint32_t                    BaseVertex;
    uint32_t                    InstanceId;
    D3D12_VERTEX_BUFFER_VIEW    VBV;
    D3D12_INDEX_BUFFER_VIEW     IBV;
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
        std::vector<uint8_t>    Temporary;
        uint32_t                FrameIndex;
        uint32_t                Width;
        uint32_t                Height;
        bool                    Processed;
    };

    ///////////////////////////////////////////////////////////////////////////
    // RtPipeline structure
    ///////////////////////////////////////////////////////////////////////////
    struct RtPipeline
    {
        asdx::RootSignature             RootSig;
        asdx::RayTracingPipelineState   PSO;
        asdx::ShaderTable               RayGenTable;
        asdx::ShaderTable               MissTable;
        asdx::ShaderTable               HitGroupTable;

        void Reset()
        {
            RootSig      .Term();
            PSO          .Term();
            RayGenTable  .Term();
            MissTable    .Term();
            HitGroupTable.Term();
        }
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
    asdx::ComputeTarget             m_Canvas;
    asdx::ConstantBuffer            m_SceneParam;
    asdx::ShaderTable               m_RayGenTable;
    asdx::ShaderTable               m_MissTable;
    asdx::ShaderTable               m_HitGroupTable;
    asdx::ColorTarget               m_AlbedoTarget;
    asdx::ColorTarget               m_NormalTarget;
    asdx::ColorTarget               m_VelocityTarget;
    asdx::DepthTarget               m_ModelDepthTarget;
    asdx::RootSignature             m_ModelRootSig;
    asdx::PipelineState             m_ModelPSO;
    Scene                           m_Scene;
    asdx::Camera                    m_Camera;
    asdx::ComputeTarget             m_TonemapBuffer;

#if ENABLE_RESTIR
    asdx::ComputeTarget             m_TemporalReservoirBuffer;
    asdx::ComputeTarget             m_SpatialReservoirBuffer;
    RtPipeline                      m_InitialSampling;
    RtPipeline                      m_SpatialSampling;
    asdx::RootSignature             m_ShadePixelRootSig;
    asdx::PipelineState             m_ShadePixelPSO;
#endif

    asdx::RefPtr<ID3D12Resource>    m_CaptureTexture[3];
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
    asdx::ComputeTarget m_DenoiseTarget[2];

    asdx::RootSignature m_TonemapRootSig;
    asdx::PipelineState m_TonemapPSO;

    asdx::RootSignature m_CopyRootSig;
    asdx::PipelineState m_CopyPSO;

    bool                m_EndRequest = false;
    bool                m_ForceChanged = false;

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

#if ENABLE_RESTIR
    RtPipeline                      m_DevInitialSampling;
    RtPipeline                      m_DevSpatialSampling;
#endif

    int                             m_BufferKind;
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
    bool InitInitialSamplingPipeline(RtPipeline& value, D3D12_SHADER_BYTECODE shader);
    bool InitSpatialSamplingPipeline(RtPipeline& value, D3D12_SHADER_BYTECODE shader);

    void ChangeFrame(uint32_t index);
    void CaptureScreen(ID3D12Resource* pResource, D3D12_RESOURCE_STATES state, bool forceSync = false);

#if (!CAMP_RELEASE)
    void ReloadShader();
    bool CompileShader(const wchar_t* path, asdx::IBlob** ppBlob);
#endif
};

} // namespace r3d
