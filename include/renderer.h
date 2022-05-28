//-----------------------------------------------------------------------------
// File : renderer.h
// Desc : Renderer.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------
#pragma once

#define ASDX_ENABLE_IMGUI   (1)

//-----------------------------------------------------------------------------
// Include
//-----------------------------------------------------------------------------
#include <fw/asdxApp.h>
#include <gfx/asdxRootSignature.h>
#include <gfx/asdxPipelineState.h>
#include <gfx/asdxRayTracing.h>
#include <gfx/asdxTarget.h>
#include <gfx/asdxCommandQueue.h>
#include <gfx/asdxQuad.h>
#include <edit/asdxGuiMgr.h>


namespace r3d {

struct SceneDesc
{
    uint32_t    TimeSec;
    uint32_t    Width;
    uint32_t    Height;
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
    asdx::RootSignature             m_TonemapRootSig;
    asdx::PipelineState             m_TonemapPSO;
    asdx::RayTracingPipelineState   m_RayTracingPSO;
    std::vector<asdx::Blas>         m_BLAS;
    asdx::Tlas                      m_TLAS;
    asdx::ComputeTarget             m_Canvas;
    double                          m_RenderTimeSec;

    uint8_t                         m_ReadBackIndex = 0;
    uint8_t                         m_MapIndex      = 0;
    asdx::RefPtr<ID3D12Resource>    m_ReadBackTexture[3];
    uint32_t                        m_ReadBackPitch = 0;
    ExportData                      m_ExportData[3];
    uint32_t                        m_CaptureIndex  = 0;

#if ASDX_ENABLE_IMGUI
    bool    m_DebugSetting = true;
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
};

} // namespace r3d
