﻿//-----------------------------------------------------------------------------
// File : model_mgr.h
// Desc : Model Manager.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------
#pragma once

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <d3d12.h>
#include <fnd/asdxRef.h>
#include <gfx/asdxRayTracing.h>
#include <gfx/asdxCommandList.h>
#include <gfx/asdxTexture.h>
#include <generated/scene_format.h>


namespace r3d {

static constexpr uint32_t INVALID_MATERIAL_MAP = UINT32_MAX;


///////////////////////////////////////////////////////////////////////////////
// Mesh structure
///////////////////////////////////////////////////////////////////////////////
struct Mesh
{
    uint32_t      VertexCount;
    uint32_t      IndexCount;
    ResVertex*    Vertices;
    uint32_t*     Indices;
};


///////////////////////////////////////////////////////////////////////////////
// Material structure
///////////////////////////////////////////////////////////////////////////////
struct Material
{
    uint32_t        BaseColor0; //!< ベースカラー0.
    uint32_t        Normal0;    //!< 法線マップ0.
    uint32_t        ORM0;       //!< オクルージョン/ラフネス/メタリック 0.
    uint32_t        Emissive0;  //!< エミッシブ0.

    uint32_t        BaseColor1; //!< ベースカラー1.
    uint32_t        Normal1;    //!< 法線マップ1.
    uint32_t        ORM1;       //!< オクルージョン/ラフネス/メタリック 1.
    uint32_t        Emissive1;  //!< エミッシブ1.

    asdx::Vector2   UvScale0;   //!< UVスケール0.
    asdx::Vector2   UvScroll0;  //!< UVスクロール0.

    asdx::Vector2   UvScale1;   //!< UVスケール1.
    asdx::Vector2   UvScroll1;  //!< UVスクロール1.

    float           IntIor;     //!< 内部屈折率(屈折処理しない場合は0.0を代入).
    float           ExtIor;     //!< 外部屈折率(屈折処理しない場合は0.0を代入).
    uint32_t        LayerCount; //!< レイヤー数.
    uint32_t        LayerMask;  //!< レイヤーマスクマップ.

    //-------------------------------------------------------------------------
    //! @brief      デフォルト値を取得します.
    //-------------------------------------------------------------------------
    static Material Default()
    {
        Material mat = {};
        mat.BaseColor0  = INVALID_MATERIAL_MAP;
        mat.Normal0     = INVALID_MATERIAL_MAP;
        mat.ORM0        = INVALID_MATERIAL_MAP;
        mat.Emissive0   = INVALID_MATERIAL_MAP;

        mat.BaseColor1  = INVALID_MATERIAL_MAP;
        mat.Normal1     = INVALID_MATERIAL_MAP;
        mat.ORM1        = INVALID_MATERIAL_MAP;
        mat.Emissive1   = INVALID_MATERIAL_MAP;

        mat.UvScale0    = asdx::Vector2(1.0f, 1.0f);
        mat.UvScroll0   = asdx::Vector2(0.0f, 0.0f);

        mat.UvScale1    = asdx::Vector2(1.0f, 1.0f);
        mat.UvScroll1   = asdx::Vector2(0.0f, 0.0f);

        mat.IntIor      = 0.0f;
        mat.ExtIor      = 0.0f;

        mat.LayerCount  = 1;
        mat.LayerMask   = INVALID_MATERIAL_MAP;

        return mat;
    }
};

///////////////////////////////////////////////////////////////////////////////
// GeometryHandle structure
///////////////////////////////////////////////////////////////////////////////
struct GeometryHandle
{
    D3D12_GPU_VIRTUAL_ADDRESS   AddressVB  = 0;    //!< 頂点バッファのGPU仮想アドレスです.
    D3D12_GPU_VIRTUAL_ADDRESS   AddressIB  = 0;    //!< インデックスバッファのGPU仮想アドレスです.
    uint32_t                    IndexVB    = 0;    //!< 頂点バッファのハンドルです.
    uint32_t                    IndexIB    = 0;    //!< インデックスバッファのハンドルです.
};

///////////////////////////////////////////////////////////////////////////////
// CpuInstance structure
///////////////////////////////////////////////////////////////////////////////
struct CpuInstance
{
    uint32_t            MeshId;         //!< メッシュ番号.
    uint32_t            MaterialId;     //!< マテリアル番号.
    asdx::Transform3x4  Transform;      //!< 変換行列.
};

///////////////////////////////////////////////////////////////////////////////
// InstanceHandle structure
///////////////////////////////////////////////////////////////////////////////
struct InstanceHandle
{
    uint32_t                    InstanceId; //!< インスタンスID.
    D3D12_GPU_VIRTUAL_ADDRESS   AddressTB;  //!< トランスフォームバッファのGPU仮想アドレスです.
};

///////////////////////////////////////////////////////////////////////////////
// ModelMgr class
///////////////////////////////////////////////////////////////////////////////
class ModelMgr
{
    //=========================================================================
    // list of friend classes and methods.
    //=========================================================================
    /* NOTHING */

public:
    ///////////////////////////////////////////////////////////////////////////
    // MeshBuffer structure
    ///////////////////////////////////////////////////////////////////////////
    struct MeshBuffer
    {
        asdx::RefPtr<ID3D12Resource>            VB;
        asdx::RefPtr<ID3D12Resource>            IB;
        asdx::RefPtr<asdx::IShaderResourceView> VB_SRV;
        asdx::RefPtr<asdx::IShaderResourceView> IB_SRV;
        uint32_t                                VertexCount;
        uint32_t                                IndexCount;
    };

    //=========================================================================
    // public variables.
    //=========================================================================
    /* NOTHING */

    //=========================================================================
    // public methods.
    //=========================================================================

    //-------------------------------------------------------------------------
    //! @brief      コンストラクタ
    //-------------------------------------------------------------------------
    ModelMgr() = default;

    //-------------------------------------------------------------------------
    //! @brief      デストラクタ.
    //-------------------------------------------------------------------------
    ~ModelMgr() = default;

    //-------------------------------------------------------------------------
    //! @brief      初期化処理を行います.
    //! 
    //! @param[in]      maxInstanceCount    最大インスタンス数です.
    //! @param[in]      maxMaterialCount    最大マテリアル数です.
    //! @retval true    初期化に成功.
    //! @retval false   初期化に失敗.
    //-------------------------------------------------------------------------
    bool Init(
        asdx::CommandList& cmdList,
        uint32_t maxInstanceCount,
        uint32_t maxMaterialCount);

    //-------------------------------------------------------------------------
    //! @brief      終了処理を行います.
    //-------------------------------------------------------------------------
    void Term();

    //-------------------------------------------------------------------------
    //! @brief      メモリマッピングを解除します.
    //-------------------------------------------------------------------------
    void Fixed();

    //-------------------------------------------------------------------------
    //! @brief      メッシュを登録します.
    //! 
    //! @param[in]      mesh        登録するメッシュ.
    //! @return     ジオメトリハンドルを返却します.
    //-------------------------------------------------------------------------
    GeometryHandle AddMesh(const Mesh& mesh);

    //-------------------------------------------------------------------------
    //! @brief      インスタンスを登録します.
    //! 
    //! @param[in]      instance        インスタンスデータ.
    //! @param[in]      transform       変換行列.
    //! @return     インスタンスハンドルを返却します.
    //-------------------------------------------------------------------------
    InstanceHandle AddInstance(const CpuInstance& instance);

    //-------------------------------------------------------------------------
    //! @brief      マテリアルを登録します.
    //! 
    //! @param[in]      ptr     マテリアルデータ.
    //! @param[in]      count   マテリアル数.
    //! @return     GPU仮想アドレスを返却します.
    //-------------------------------------------------------------------------
    D3D12_GPU_VIRTUAL_ADDRESS AddMaterials(const Material* ptr, uint32_t count);

    //-------------------------------------------------------------------------
    //! @brief      インスタンスバッファのシェーダリソースビューを取得します.
    //-------------------------------------------------------------------------
    asdx::IShaderResourceView* GetIB() const;

    //-------------------------------------------------------------------------
    //! @brief      トランスフォームバッファのシェーダリソースビューを取得します.
    //-------------------------------------------------------------------------
    asdx::IShaderResourceView* GetTB() const;

    //-------------------------------------------------------------------------
    //! @brief      マテリアルバッファのシェーダリソースビューを取得します.
    //-------------------------------------------------------------------------
    asdx::IShaderResourceView* GetMB() const;

    //--------------------------------------------------------------------------
    //! @brief      インスタンスバッファのGPU仮想アドレスを取得します.
    //--------------------------------------------------------------------------
    D3D12_GPU_VIRTUAL_ADDRESS GetAddressIB() const;

    //--------------------------------------------------------------------------
    //! @brief      トランスフォームバッファのGPU仮想アドレスを取得します.
    //--------------------------------------------------------------------------
    D3D12_GPU_VIRTUAL_ADDRESS GetAddressTB() const;

    //--------------------------------------------------------------------------
    //! @brief      マテリアルバッファのGPU仮想アドレスを取得します.
    //--------------------------------------------------------------------------
    D3D12_GPU_VIRTUAL_ADDRESS GetAddressMB() const;

    //-------------------------------------------------------------------------
    //! @brief      インスタンスバッファサイズを取得します.
    //-------------------------------------------------------------------------
    uint32_t GetSizeIB() const;

    //-------------------------------------------------------------------------
    //! @brief      トランスフォームバッファサイズを取得します.
    //-------------------------------------------------------------------------
    uint32_t GetSizeTB() const;

    //-------------------------------------------------------------------------
    //! @brief      マテリアルバッファサイズを取得します.
    //-------------------------------------------------------------------------
    uint32_t GetSizeMB() const;

    //-------------------------------------------------------------------------
    //! @brief      メッシュを取得します.
    //-------------------------------------------------------------------------
    const MeshBuffer& GetMesh(size_t index) const;

    //-------------------------------------------------------------------------
    //! @brief      メッシュ数を取得します.
    //-------------------------------------------------------------------------
    size_t GetMeshCount() const;

    //-------------------------------------------------------------------------
    //! @brief      インスタンス数を取得します.
    //-------------------------------------------------------------------------
    size_t GetInstanceCount() const;

    //-------------------------------------------------------------------------
    //! @brief      ジオメトリハンドルを取得します.
    //! 
    //! @param[in]      index       メッシュ番号.
    //! @return     ジオメトリハンドルを返却します.
    //-------------------------------------------------------------------------
    GeometryHandle GetGeometryHandle(uint32_t index) const;

    //-------------------------------------------------------------------------
    //! @brief      インスタンスハンドルを取得します.
    //! 
    //! @param[in]      index       インスタンス番号.
    //! @return     インスタンスハンドルを返却します.
    //-------------------------------------------------------------------------
    InstanceHandle GetInstanceHandle(uint32_t index) const;

    //-------------------------------------------------------------------------
    //! @brief      CPUインスタンスを取得します.
    //! 
    //! @param[in]      index       インスタンス番号.
    //! @return     CPUインスタンスを返却します.
    //-------------------------------------------------------------------------
    CpuInstance GetCpuInstance(uint32_t index) const;

private:
    ///////////////////////////////////////////////////////////////////////////////
    // GpuInstance structure
    ///////////////////////////////////////////////////////////////////////////////
    struct GpuInstance
    {
        uint32_t        VertexBufferId; //!< 頂点バッファのハンドルです.
        uint32_t        IndexBufferId;  //!< インデックスバッファの
        uint32_t        MaterialId;     //!< マテリアルID.
    };

    //=========================================================================
    // private variables.
    //=========================================================================
    asdx::RefPtr<ID3D12Resource>    m_IB;   //!< インスタンスバッファ.
    asdx::RefPtr<ID3D12Resource>    m_TB;   //!< トランスフォームバッファ.
    asdx::RefPtr<ID3D12Resource>    m_MB;   //!< マテリアルバッファ.

    asdx::RefPtr<asdx::IShaderResourceView> m_IB_SRV;
    asdx::RefPtr<asdx::IShaderResourceView> m_TB_SRV;
    asdx::RefPtr<asdx::IShaderResourceView> m_MB_SRV;

    std::vector<MeshBuffer>     m_Meshes;

    uint32_t    m_OffsetInstance = 0;
    uint32_t    m_OffsetMaterial = 0;

    uint32_t    m_MaxInstanceCount;
    uint32_t    m_MaxMaterialCount;

    GpuInstance*            m_pInstances    = nullptr;
    asdx::Transform3x4*     m_pTransforms   = nullptr;
    Material*               m_pMaterials    = nullptr;

    D3D12_GPU_VIRTUAL_ADDRESS m_AddressIB = 0;
    D3D12_GPU_VIRTUAL_ADDRESS m_AddressTB = 0;
    D3D12_GPU_VIRTUAL_ADDRESS m_AddressMB = 0;

    asdx::Texture   m_DefaultBaseColor;
    asdx::Texture   m_DefaultNormal;
    asdx::Texture   m_DefaultORM;
    asdx::Texture   m_Black;

    std::vector<GeometryHandle> m_GeometryHandles;
    std::vector<InstanceHandle> m_InstanceHandles;
    std::vector<CpuInstance>    m_CpuInstances;

    //=========================================================================
    // private methods.
    //=========================================================================
    uint32_t GetBaseColor   (uint32_t handle);
    uint32_t GetNormal      (uint32_t handle);
    uint32_t GetOrm         (uint32_t handle);
    uint32_t GetEmissive    (uint32_t handle);
    uint32_t GetMask        (uint32_t handle);
};

} // namespace r3d