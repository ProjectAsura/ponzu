//-----------------------------------------------------------------------------
// File : scene.h
// Desc : Scene Data.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------
#pragma once

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <fnd/asdxMath.h>
#include <gfx/asdxTexture.h>
#include <gfx/asdxConstantBuffer.h>
#include <vector>
#include <model_mgr.h>


namespace r3d {

///////////////////////////////////////////////////////////////////////////////
// PinholeCamera structure
///////////////////////////////////////////////////////////////////////////////
struct PinholeCamera
{
    asdx::Vector3       Position;
    asdx::Vector3       Target;
    asdx::Vector3       Upward;
    float               FieldOfView;
    float               NearClip;
    float               FarClip;
};

///////////////////////////////////////////////////////////////////////////////
// DrawCall structure
///////////////////////////////////////////////////////////////////////////////
struct DrawCall
{
    uint32_t                    IndexCount;
    uint32_t                    InstanceId;
    D3D12_VERTEX_BUFFER_VIEW    VBV;
    D3D12_INDEX_BUFFER_VIEW     IBV;
};

///////////////////////////////////////////////////////////////////////////////
// Scene class
///////////////////////////////////////////////////////////////////////////////
class Scene
{
    //=========================================================================
    // list of friend classes and methods.
    //=========================================================================
    /* NOTHING */

public:
    //=========================================================================
    // public variables
    //=========================================================================
    /* NOTHING */

    //=========================================================================
    // public methods.
    //=========================================================================
    bool InitFromXml(const char* path);
    void Term();

    asdx::IConstantBufferView* GetParamCBV() const;
    asdx::IShaderResourceView* GetVB() const;
    asdx::IShaderResourceView* GetIB() const;
    asdx::IShaderResourceView* GetTB() const;
    asdx::IShaderResourceView* GetMB() const;
    asdx::IShaderResourceView* GetIBL() const;

    void Draw(ID3D12GraphicsCommandList* pCmdList);

private:
    //=========================================================================
    // private variables.
    //=========================================================================
    std::vector<DrawCall>       m_DrawCalls;
    std::vector<asdx::Blas>     m_BLAS;
    asdx::Tlas                  m_TLAS;
    asdx::Texture               m_IBL;
    asdx::ConstantBuffer        m_Param;
    ModelMgr                    m_ModelMgr;

    //=========================================================================
    // private methods.
    //=========================================================================
    /* NOTHING */
};

} // namespace r3d
