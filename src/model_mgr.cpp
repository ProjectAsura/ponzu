//-----------------------------------------------------------------------------
// File : model_mgr.cpp
// Desc : Model Manager.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <model_mgr.h>
#include <gfx/asdxGraphicsSystem.h>
#include <fnd/asdxLogger.h>


namespace r3d {

///////////////////////////////////////////////////////////////////////////////
// ModelMgr class
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      初期化処理を行います.
//-----------------------------------------------------------------------------
bool ModelMgr::Init
(
    uint32_t maxInstanceCount,
    uint32_t maxMaterialCount
)
{
    auto pDevice = asdx::GetD3D12Device();

    const auto sizeIB = maxInstanceCount * sizeof(Instance);
    const auto sizeTB = maxInstanceCount * sizeof(asdx::Transform3x4);
    const auto sizeMB = maxMaterialCount * sizeof(Material);

    m_MaxInstanceCount = maxInstanceCount;
    m_MaxMaterialCount = maxMaterialCount;

    m_OffsetInstance = 0;
    m_OffsetMaterial = 0;

    //--------------------
    // インスタンスデータ.
    //--------------------
    if (!asdx::CreateUploadBuffer(pDevice, sizeIB, m_IB.GetAddress()))
    {
        ELOGA("Error : InstanceBuffer Create Failed.");
        return false;
    }

    if (!asdx::CreateBufferSRV(pDevice, m_IB.GetPtr(), UINT(sizeIB / 4), 0, m_IB_SRV.GetAddress()))
    {
        ELOGA("Error : InstanceBuffer SRV Create Failed.");
        return false;
    }

    {
        m_AddressIB = m_IB->GetGPUVirtualAddress();

        auto hr = m_IB->Map(0, nullptr, reinterpret_cast<void**>(&m_pInstances));
        if (FAILED(hr))
        {
            ELOGA("Error : ID3D12Resource::Map() Failed. errcode = 0x%x", hr);
            return false;
        }
    }

    //--------------------
    // トランスフォームデータ.
    //--------------------
    if (!asdx::CreateUploadBuffer(pDevice, sizeTB, m_TB.GetAddress()))
    {
        ELOGA("Error : TransformBuffer Create Failed.");
        return false;
    }

    if (!asdx::CreateBufferSRV(pDevice, m_TB.GetPtr(), UINT(sizeTB / 4), 0, m_TB_SRV.GetAddress()))
    {
        ELOGA("Error : Transform SRV Create Failed.");
        return false;
    }

    {
        m_AddressTB = m_TB->GetGPUVirtualAddress();

        auto hr = m_TB->Map(0, nullptr, reinterpret_cast<void**>(&m_pTransforms));
        if (FAILED(hr))
        {
            ELOGA("Error : ID3D12Resource::Map() Failed. errcode = 0x%x", hr);
            return false;
        }
    }

    //--------------------
    // マテリアルデータ.
    //--------------------
    if (!asdx::CreateUploadBuffer(pDevice, sizeMB, m_MB.GetAddress()))
    {
        ELOGA("Error : MaterialBuffer Create Failed.");
        return false;
    }

    if (!asdx::CreateBufferSRV(pDevice, m_MB.GetPtr(), UINT(sizeMB / 4), 0, m_MB_SRV.GetAddress()))
    {
        ELOGA("Error : Material SRV Create Failed.");
        return false;
    }

    {
        m_AddressMB = m_MB->GetGPUVirtualAddress();

        auto hr = m_MB->Map(0, nullptr, reinterpret_cast<void**>(&m_pMaterials));
        if (FAILED(hr))
        {
            ELOGA("Error : ID3D12Resource::Map() Failed. errcode = 0x%x", hr);
            return false;
        }
    }

    return true;
}

//-----------------------------------------------------------------------------
//      終了処理を行います.
//-----------------------------------------------------------------------------
void ModelMgr::Term()
{
    m_IB_SRV.Reset();
    m_TB_SRV.Reset();
    m_MB_SRV.Reset();

    m_IB.Reset();
    m_TB.Reset();
    m_MB.Reset();

    m_OffsetInstance = 0;
    m_OffsetMaterial = 0;

    m_pInstances  = nullptr;
    m_pTransforms = nullptr;
    m_pMaterials  = nullptr;

    for(size_t i=0; i<m_Meshes.size(); ++i)
    {
        m_Meshes[i].VB.Reset();
        m_Meshes[i].IB.Reset();
        m_Meshes[i].VB_SRV.Reset();
        m_Meshes[i].IB_SRV.Reset();
    }

    m_Meshes.clear();
    m_Meshes.shrink_to_fit();
}

//-----------------------------------------------------------------------------
//      メモリマッピングを解除します.
//-----------------------------------------------------------------------------
void ModelMgr::Fixed()
{
    m_IB->Unmap(0, nullptr);
    m_pInstances = nullptr;

    m_TB->Unmap(0, nullptr);
    m_pTransforms = nullptr;

    m_MB->Unmap(0, nullptr);
    m_pMaterials = nullptr;
}

//-----------------------------------------------------------------------------
//      メッシュを登録します.
//-----------------------------------------------------------------------------
GeometryHandle ModelMgr::AddMesh(const Mesh& mesh)
{
    auto pDevice = asdx::GetD3D12Device();

    MeshBuffer item;
    GeometryHandle result = {};

    // 頂点バッファ生成.
    {
        auto vbSize = mesh.VertexCount * sizeof(Vertex);
        if (!asdx::CreateUploadBuffer(pDevice, vbSize, item.VB.GetAddress()))
        {
            ELOGA("Error : CreateUploadBuffer() Failed.");
            return result;
        }

        if (!asdx::CreateBufferSRV(pDevice, item.VB.GetPtr(), UINT(vbSize/4), 0, item.VB_SRV.GetAddress()))
        {
            ELOGA("Error : CreateBufferSRV() Failed.");
            return result;
        }

        uint8_t* ptr = nullptr;
        auto hr = item.VB->Map(0, nullptr, reinterpret_cast<void**>(&ptr));
        if (FAILED(hr))
        {
            ELOGA("Error : ID3D12Resource::Map() Failed. errcode = 0x%x", hr);
            return result;
        }

        memcpy(ptr, mesh.Vertices, vbSize);

        item.VB->Unmap(0, nullptr);
    }

    // インデックスバッファ生成.
    {
        auto ibSize = mesh.VertexCount * sizeof(uint32_t);
        if (!asdx::CreateUploadBuffer(pDevice, ibSize, item.IB.GetAddress()))
        {
            ELOGA("Error : CreateUploadBuffer() Failed.");
            return result;
        }

        if (!asdx::CreateBufferSRV(pDevice, item.IB.GetPtr(), UINT(ibSize/4), 0, item.IB_SRV.GetAddress()))
        {
            ELOGA("Error : CreateBufferSRV() Failed.");
            return result;
        }

        uint8_t* ptr = nullptr;
        auto hr = item.IB->Map(0, nullptr, reinterpret_cast<void**>(&ptr));
        if (FAILED(hr))
        {
            ELOGA("Error : ID3D12Resource::Map() Failed. errcode = 0x%x", hr);
            return result;
        }

        memcpy(ptr, mesh.Indices, ibSize);

        item.IB->Unmap(0, nullptr);
    }

    result.AddressVB    = item.VB->GetGPUVirtualAddress();
    result.AddressIB    = item.IB->GetGPUVirtualAddress();
    result.IndexVB      = item.VB_SRV->GetDescriptorIndex();
    result.IndexIB      = item.IB_SRV->GetDescriptorIndex();

    m_Meshes.emplace_back(item);
    return result;
}

//-----------------------------------------------------------------------------
//      インスタンスを登録します.
//-----------------------------------------------------------------------------
InstanceHandle ModelMgr::AddInstance(const Instance& instance, const asdx::Transform3x4& transform)
{
    assert(m_OffsetInstance + 1 < m_MaxInstanceCount);

    auto idx = m_OffsetInstance;
    m_pInstances [idx] = instance;
    m_pTransforms[idx] = transform;

    m_OffsetInstance++;

    InstanceHandle result = {};
    result.InstanceId = idx;
    result.AddressTB  = m_AddressTB + idx * sizeof(asdx::Transform3x4);
    return result;
}

//-----------------------------------------------------------------------------
//      マテリアルデータを追加します.
//-----------------------------------------------------------------------------
D3D12_GPU_VIRTUAL_ADDRESS ModelMgr::AddMaterials(const Material* ptr, uint32_t count)
{
    assert(m_OffsetMaterial + count < m_MaxMaterialCount);
    D3D12_GPU_VIRTUAL_ADDRESS result = m_AddressMB + m_OffsetMaterial * sizeof(Material);

    for(uint32_t i=0; i<count; ++i)
    { m_pMaterials[m_OffsetMaterial + i] = ptr[i]; }

    m_OffsetMaterial += count;

    return result;
}

//-----------------------------------------------------------------------------
//      インスタンスバッファのシェーダリソースビューを取得します.
//-----------------------------------------------------------------------------
asdx::IShaderResourceView* ModelMgr::GetIB() const
{ return m_IB_SRV.GetPtr(); }

//-----------------------------------------------------------------------------
//      トランスフォームバッファのシェーダリソースビューを取得します.
//-----------------------------------------------------------------------------
asdx::IShaderResourceView* ModelMgr::GetTB() const
{ return m_TB_SRV.GetPtr(); }

//-----------------------------------------------------------------------------
//      マテリアルバッファのシェーダリソースビューを取得します.
//-----------------------------------------------------------------------------
asdx::IShaderResourceView* ModelMgr::GetMB() const
{ return m_MB_SRV.GetPtr(); }

//-----------------------------------------------------------------------------
//      インスタンスバッファのGPU仮想アドレスを取得します.
//-----------------------------------------------------------------------------
D3D12_GPU_VIRTUAL_ADDRESS ModelMgr::GetAddressIB() const
{ return m_AddressIB; }

//-----------------------------------------------------------------------------
//      トランスフォームバッファのGPU仮想アドレスを取得します.
//-----------------------------------------------------------------------------
D3D12_GPU_VIRTUAL_ADDRESS ModelMgr::GetAddressTB() const
{ return m_AddressTB; }

//-----------------------------------------------------------------------------
//      マテリアルバッファのGPU仮想アドレスを取得します.
//-----------------------------------------------------------------------------
D3D12_GPU_VIRTUAL_ADDRESS ModelMgr::GetAddressMB() const
{ return m_AddressMB; }

//-----------------------------------------------------------------------------
//      インスタンスバッファサイズを取得します.
//-----------------------------------------------------------------------------
uint32_t ModelMgr::GetSizeIB() const
{ return m_MaxInstanceCount * sizeof(Instance); }

//-----------------------------------------------------------------------------
//      トランスフォームバッファサイズを取得します.
//-----------------------------------------------------------------------------
uint32_t ModelMgr::GetSizeTB() const
{ return m_MaxInstanceCount * sizeof(asdx::Transform3x4); }

//-----------------------------------------------------------------------------
//      マテリアルバッファサイズを取得します.
//-----------------------------------------------------------------------------
uint32_t ModelMgr::GetSizeMB() const
{ return m_MaxMaterialCount * sizeof(Material); }

} // namespace r3d
