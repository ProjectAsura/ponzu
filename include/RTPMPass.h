//-----------------------------------------------------------------------------
// File : RTPMPass.h
// Desc : RT Photon Map Pass.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------
#pragma once

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <cstdint>
#include <gfx/asdxTarget.h>
#include <gfx/asdxRayTracing.h>
#include <gfx/asdxPipelineState.h>
#include <Scene.h>


namespace r3d {

///////////////////////////////////////////////////////////////////////////////
// RTPMPass class
///////////////////////////////////////////////////////////////////////////////
class RTPMPass
{
    //=========================================================================
    // list of friend classes and methods.
    //=========================================================================
    /* NOTHING */

public:
    //=========================================================================
    // public variables.
    //=========================================================================
    /* NOTHING */

    //=========================================================================
    // public methods.
    //=========================================================================
    RTPMPass();
    ~RTPMPass();

    bool Init(ID3D12Device5* pDevice, uint32_t width, uint32_t height);
    void Term();
    void Render(
        ID3D12GraphicsCommandList4* pCmd,
        const Scene& scene,
        D3D12_GPU_VIRTUAL_ADDRESS sceneParamAddress,
        bool reset);

private:
    //=========================================================================
    // private variables.
    //=========================================================================
    struct PhotonBuffer
    {
        asdx::ComputeTarget AABB;
        asdx::ComputeTarget Flux;
        asdx::ComputeTarget Direction;
        asdx::ComputeTarget FaceNormal;

        bool Init(ID3D12Device* pDevice, uint32_t width, uint32_t height);
        void Term();
    };

    asdx::RefPtr<ID3D12RootSignature>   m_PhotonCullingRootSig;
    asdx::RefPtr<ID3D12RootSignature>   m_GeneratePhotonRootSig;
    asdx::RefPtr<ID3D12RootSignature>   m_CollectPhotonRootSig;

    asdx::PipelineState             m_PhotonCullingPipe;
    asdx::RayTracingPipelineState   m_GeneratePhotonPipe;
    asdx::RayTracingPipelineState   m_CollectPhotonPipe;

    asdx::ComputeTarget m_ThroughputBuffer; //!< Throughput (RGBA16_FLOAT).
    asdx::ComputeTarget m_CullingHashBuffer;

    PhotonBuffer m_CausticBuffer;
    PhotonBuffer m_GlobalBuffer;

    std::vector<asdx::AsScratchBuffer>  m_ScratchBLAS;
    asdx::AsScratchBuffer               m_ScratchTLAS;
    std::vector<asdx::Blas>             m_PhotonBLAS;
    asdx::Tlas                          m_PhotonTLAS;

    float       m_HashScaleFacator      = 1.0f;
    uint32_t    m_HashSize              = 22;
    float       m_CullingProjTest       = 1.01f;
    uint32_t    m_CullingExtentY        = 512;
    float       m_CausticRadiusStart    = 0.01f;
    float       m_GlobalRadiusStart     = 0.05f;
    float       m_GlobalRadius          = 1.0f;
    float       m_CausticRadius         = 1.0f;
    float       m_SPPM_AlphaGlobal      = 0.7f;
    float       m_SPPM_AlphaCaustic     = 0.7f;
    bool        m_RebuildAS             = false;
    uint32_t    m_PhotonCount           = 2000000;
    uint32_t    m_Width                 = 0;
    uint32_t    m_Height                = 0;
    uint32_t    m_FrameCount            = 0;

    //=========================================================================
    // private methods.
    //=========================================================================
    void PhotonCulling (ID3D12GraphicsCommandList4* pCmd);
    void GeneratePhoton(ID3D12GraphicsCommandList4* pCmd);
    void BuildPhotonAS (ID3D12GraphicsCommandList4* pCmd);
    void CollectPhoton (ID3D12GraphicsCommandList4* pCmd);
    void UpdateRadius  ();
};

} // namespace r3d
