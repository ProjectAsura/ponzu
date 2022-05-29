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
    uint64_t maxVertexCount,
    uint64_t maxIndexCount,
    uint64_t maxTransformCount,
    uint64_t maxMaterialCount
)
{
    auto pDevice = asdx::GetD3D12Device();

    auto sizeVB = sizeof(Vertex) * maxVertexCount;
    auto sizeIB = sizeof(uint32_t) * maxIndexCount;
    auto sizeTB = sizeof(asdx::Transform3x4) * maxTransformCount;
    auto sizeMB = sizeof(Material) * maxMaterialCount;

    if (!asdx::CreateUploadBuffer(pDevice, sizeVB, m_VB.GetAddress()))
    {
        ELOGA("Error : VertexBuffer Create Failed.");
        return false;
    }

    if (!asdx::CreateBufferSRV(pDevice, m_VB.GetPtr(), UINT(sizeVB / 4), 0, m_VertexSRV.GetAddress()))
    {
        ELOGA("Error : Vertex SRV Create Failed.");
        return false;
    }

    {
        m_AddressVB = m_VB->GetGPUVirtualAddress();

        auto hr = m_VB->Map(0, nullptr, reinterpret_cast<void**>(&m_pHeadVB));
        if (FAILED(hr))
        {
            ELOGA("Error : ID3D12Resource::Map() Failed. errcode = 0x%x", hr);
            return false;
        }
    }

    if (!asdx::CreateUploadBuffer(pDevice, sizeIB, m_IB.GetAddress()))
    {
        ELOGA("Error : IndexBuffer Create Failed.");
        return false;
    }

    if (!asdx::CreateBufferSRV(pDevice, m_IB.GetPtr(), maxIndexCount, 0, m_IndexSRV.GetAddress()))
    {
        ELOGA("Error : Index SRV Create Failed.");
        return false;
    }

    {
        m_AddressIB = m_IB->GetGPUVirtualAddress();

        auto hr = m_IB->Map(0, nullptr, reinterpret_cast<void**>(&m_pHeadIB));
        if (FAILED(hr))
        {
            ELOGA("Error : ID3D12Resource::Map() Failed. errcode = 0x%x", hr);
            return false;
        }
    }

    if (!asdx::CreateUploadBuffer(pDevice, sizeTB, m_TB.GetAddress()))
    {
        ELOGA("Error : TransformBuffer Create Failed.");
        return false;
    }

    if (!asdx::CreateBufferSRV(pDevice, m_TB.GetPtr(), UINT(sizeTB / 4), 0, m_TransformSRV.GetAddress()))
    {
        ELOGA("Error : Transform SRV Create Failed.");
        return false;
    }

    {
        m_AddressTB = m_TB->GetGPUVirtualAddress();

        auto hr = m_TB->Map(0, nullptr, reinterpret_cast<void**>(&m_pHeadTB));
        if (FAILED(hr))
        {
            ELOGA("Error : ID3D12Resource::Map() Failed. errcode = 0x%x", hr);
            return false;
        }
    }

    if (!asdx::CreateUploadBuffer(pDevice, sizeMB, m_MB.GetAddress()))
    {
        ELOGA("Error : MaterialBuffer Create Failed.");
        return false;
    }

    if (!asdx::CreateBufferSRV(pDevice, m_MB.GetPtr(), UINT(sizeMB / 4), 0, m_MaterialSRV.GetAddress()))
    {
        ELOGA("Error : Material SRV Create Failed.");
        return false;
    }

    {
        m_AddressMB = m_MB->GetGPUVirtualAddress();

        auto hr = m_MB->Map(0, nullptr, reinterpret_cast<void**>(&m_pHeadMB));
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
    m_VertexSRV   .Reset();
    m_IndexSRV    .Reset();
    m_TransformSRV.Reset();
    m_MaterialSRV .Reset();

    m_VB.Reset();
    m_IB.Reset();
    m_TB.Reset();
    m_MB.Reset();

    m_OffsetVB = 0;
    m_OffsetIB = 0;
    m_OffsetTB = 0;
    m_OffsetMB = 0;

    m_pHeadVB = nullptr;
    m_pHeadIB = nullptr;
    m_pHeadTB = nullptr;
    m_pHeadMB = nullptr;
}

//-----------------------------------------------------------------------------
//      メモリマッピングを解除します.
//-----------------------------------------------------------------------------
void ModelMgr::Fixed()
{
    m_VB->Unmap(0, nullptr);
    m_pHeadVB = nullptr;

    m_IB->Unmap(0, nullptr);
    m_pHeadIB = nullptr;

    m_TB->Unmap(0, nullptr);
    m_pHeadTB = nullptr;

    m_MB->Unmap(0, nullptr);
    m_pHeadMB = nullptr;
}

//-----------------------------------------------------------------------------
//      頂点データを追加します.
//-----------------------------------------------------------------------------
D3D12_GPU_VIRTUAL_ADDRESS ModelMgr::AddVertices(const Vertex* ptr, uint64_t count)
{
    D3D12_GPU_VIRTUAL_ADDRESS result = m_AddressVB + m_OffsetVB;

    memcpy(m_pHeadVB + m_OffsetVB, ptr, sizeof(Vertex) * count);
    m_OffsetVB += sizeof(Vertex) * count;

    return result;
}

//-----------------------------------------------------------------------------
//      インデックスデータを追加します.
//-----------------------------------------------------------------------------
D3D12_GPU_VIRTUAL_ADDRESS ModelMgr::AddInidices(const uint32_t* ptr, uint64_t count)
{
    D3D12_GPU_VIRTUAL_ADDRESS result = m_AddressIB + m_OffsetIB;

    memcpy(m_pHeadIB + m_OffsetIB, ptr, sizeof(uint32_t) * count);
    m_OffsetIB += sizeof(uint32_t) * count;

    return result;
}

//-----------------------------------------------------------------------------
//      トランスフォームデータを追加します.
//-----------------------------------------------------------------------------
D3D12_GPU_VIRTUAL_ADDRESS ModelMgr::AddTransforms(const asdx::Transform3x4* ptr, uint64_t count)
{
    D3D12_GPU_VIRTUAL_ADDRESS result = m_AddressTB + m_OffsetTB;

    memcpy(m_pHeadTB + m_OffsetTB, ptr, sizeof(asdx::Transform3x4) * count);
    m_OffsetTB += sizeof(asdx::Transform3x4) * count;

    return result;
}

//-----------------------------------------------------------------------------
//      マテリアルデータを追加します.
//-----------------------------------------------------------------------------
D3D12_GPU_VIRTUAL_ADDRESS ModelMgr::AddMaterials(const Material* ptr, uint64_t count)
{
    D3D12_GPU_VIRTUAL_ADDRESS result = m_AddressMB + m_OffsetMB;

    memcpy(m_pHeadMB + m_OffsetMB, ptr, sizeof(Material) * count);
    m_OffsetMB += sizeof(Material) * count;

    return result;
}

//-----------------------------------------------------------------------------
//      頂点バッファのシェーダリソースビューを取得します.
//-----------------------------------------------------------------------------
asdx::IShaderResourceView* ModelMgr::GetVertexSRV() const
{ return m_VertexSRV.GetPtr(); }

//-----------------------------------------------------------------------------
//      インデックスバッファのシェーダリソースビューを取得します.
//-----------------------------------------------------------------------------
asdx::IShaderResourceView* ModelMgr::GetIndexSRV() const
{ return m_IndexSRV.GetPtr(); }

//-----------------------------------------------------------------------------
//      トランスフォームバッファのシェーダリソースビューを取得します.
//-----------------------------------------------------------------------------
asdx::IShaderResourceView* ModelMgr::GetTransformSRV() const
{ return m_TransformSRV.GetPtr(); }

//-----------------------------------------------------------------------------
//      マテリアルバッファのシェーダリソースビューを取得します.
//-----------------------------------------------------------------------------
asdx::IShaderResourceView* ModelMgr::GetMaterialSRV() const
{ return m_MaterialSRV.GetPtr(); }

} // namespace r3d
