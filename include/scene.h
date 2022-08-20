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
#include <gfx/asdxStructuredBuffer.h>
#include <gfx/asdxCommandList.h>
#include <vector>
#include <model_mgr.h>


namespace r3d {

///////////////////////////////////////////////////////////////////////////////
// LIGHT_TYPE enum
///////////////////////////////////////////////////////////////////////////////
enum LIGHT_TYPE
{
    LIGHT_TYPE_POINT        = 0,
    LIGHT_TYPE_DIRECTIONAL  = 1,
};

///////////////////////////////////////////////////////////////////////////////
// Light structure
///////////////////////////////////////////////////////////////////////////////
struct Light
{
    uint32_t        Type;
    asdx::Vector3   Position;
    asdx::Vector3   Intensity;
    float           Radius;
};

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
    bool Init(const char* path, asdx::CommandList& cmdList);
    void Term();

    asdx::IConstantBufferView* GetParamCBV() const;
    asdx::IShaderResourceView* GetVB() const;
    asdx::IShaderResourceView* GetIB() const;
    asdx::IShaderResourceView* GetTB() const;
    asdx::IShaderResourceView* GetMB() const;
    asdx::IShaderResourceView* GetIBL() const;
    asdx::IShaderResourceView* GetLB() const;

    void Draw(ID3D12GraphicsCommandList6* pCmdList);

    uint32_t GetLightCount() const;

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
    asdx::StructuredBuffer                  m_LB;
    std::vector<GeometryHandle>             m_GeometryHandles;

    //=========================================================================
    // private methods.
    //=========================================================================
    uint32_t GetTextureHandle(uint32_t index);
};

#if !CAMP_RELEASE
///////////////////////////////////////////////////////////////////////////////
// SceneExporter class
///////////////////////////////////////////////////////////////////////////////
class SceneExporter
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
    bool Export(const char* path);
    void Reset();

    void AddLight   (const Light& value);
    void AddMesh    (const Mesh& value);
    void AddMaterial(const Material& value);
    void AddInstance(const CpuInstance& value);
    void AddTexture (const char* path);
    void SetIBL     (const char* path);

private:
    //=========================================================================
    // private variables.
    //=========================================================================
    std::vector<Light>          m_Lights;
    std::vector<Mesh>           m_Meshes;
    std::vector<Material>       m_Materials;
    std::vector<CpuInstance>    m_Instances;
    std::vector<std::string>    m_Textures;
    std::string                 m_IBL;
};
#endif

} // namespace r3d
