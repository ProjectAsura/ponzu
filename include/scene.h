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
// SceneTexture class
///////////////////////////////////////////////////////////////////////////////
class SceneTexture
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

    //-------------------------------------------------------------------------
    //! @brief      初期化処理を行います.
    //-------------------------------------------------------------------------
    bool Init(ID3D12GraphicsCommandList6* pCmdList, const void* resTexture);

    //-------------------------------------------------------------------------
    //! @brief      終了処理を行います.
    //-------------------------------------------------------------------------
    void Term();

    //-------------------------------------------------------------------------
    //! @brief      シェーダリソースビューを取得します.
    //-------------------------------------------------------------------------
    asdx::IShaderResourceView* GetView() const;

private:
    //=========================================================================
    // private variables.
    //=========================================================================
    asdx::RefPtr<asdx::IShaderResourceView> m_View;

    //=========================================================================
    // private methods.
    //=========================================================================
    /* NOTHING */
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
    bool Init(const char* path, ID3D12GraphicsCommandList6* pCmdList);
    void Term();

    asdx::IConstantBufferView* GetParamCBV() const;
    asdx::IShaderResourceView* GetVB() const;
    asdx::IShaderResourceView* GetIB() const;
    asdx::IShaderResourceView* GetTB() const;
    asdx::IShaderResourceView* GetMB() const;
    asdx::IShaderResourceView* GetIBL() const;

    void Draw(ID3D12GraphicsCommandList6* pCmdList);

private:
    ///////////////////////////////////////////////////////////////////////////
    // SceneInstance structure
    ///////////////////////////////////////////////////////////////////////////
    struct SceneInstance
    {
        uint32_t    InstanceId;
        uint32_t    MeshId;
    };

    ///////////////////////////////////////////////////////////////////////////
    // DrawCall structure
    ///////////////////////////////////////////////////////////////////////////
    struct DrawCall
    {
        uint32_t                    IndexCount;
        D3D12_VERTEX_BUFFER_VIEW    VBV;
        D3D12_INDEX_BUFFER_VIEW     IBV;
        uint32_t                    IndexVB;
        uint32_t                    IndexIB;
        uint32_t                    MaterialId;
    };

    //=========================================================================
    // private variables.
    //=========================================================================
    void*                                   m_pBinary = nullptr;
    std::vector<DrawCall>                   m_DrawCalls;
    std::vector<SceneInstance>              m_Instances;
    std::vector<asdx::Blas>                 m_BLAS;
    asdx::Tlas                              m_TLAS;
    SceneTexture                            m_IBL;
    std::vector<SceneTexture>               m_Textures;
    asdx::ConstantBuffer                    m_Param;
    ModelMgr                                m_ModelMgr;

    //=========================================================================
    // private methods.
    //=========================================================================
    uint32_t GetTextureHandle(uint32_t index);
};

} // namespace r3d
