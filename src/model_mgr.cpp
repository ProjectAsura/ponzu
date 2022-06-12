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
    uint32_t maxVertexCount,
    uint32_t maxIndexCount,
    uint32_t maxTransformCount,
    uint32_t maxMaterialCount
)
{
    auto pDevice = asdx::GetD3D12Device();

    const auto sizeVB = maxVertexCount    * sizeof(Vertex);
    const auto sizeIB = maxIndexCount     * sizeof(uint32_t);
    const auto sizeTB = maxTransformCount * sizeof(asdx::Transform3x4);
    const auto sizeMB = maxMaterialCount  * sizeof(Material);

    m_MaxCountVB = maxVertexCount;
    m_MaxCountIB = maxIndexCount;
    m_MaxCountTB = maxTransformCount;
    m_MaxCountMB = maxMaterialCount;

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

        auto hr = m_VB->Map(0, nullptr, reinterpret_cast<void**>(&m_pVertices));
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

    if (!asdx::CreateBufferSRV(pDevice, m_IB.GetPtr(), UINT(maxIndexCount), 0, m_IndexSRV.GetAddress()))
    {
        ELOGA("Error : Index SRV Create Failed.");
        return false;
    }

    {
        m_AddressIB = m_IB->GetGPUVirtualAddress();

        auto hr = m_IB->Map(0, nullptr, reinterpret_cast<void**>(&m_pIndices));
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

        auto hr = m_TB->Map(0, nullptr, reinterpret_cast<void**>(&m_pTransforms));
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

    m_pVertices   = nullptr;
    m_pIndices    = nullptr;
    m_pTransforms = nullptr;
    m_pMaterials  = nullptr;
}

//-----------------------------------------------------------------------------
//      メモリマッピングを解除します.
//-----------------------------------------------------------------------------
void ModelMgr::Fixed()
{
    m_VB->Unmap(0, nullptr);
    m_pVertices = nullptr;

    m_IB->Unmap(0, nullptr);
    m_pIndices = nullptr;

    m_TB->Unmap(0, nullptr);
    m_pTransforms = nullptr;

    m_MB->Unmap(0, nullptr);
    m_pMaterials = nullptr;
}

//-----------------------------------------------------------------------------
//      頂点データを追加します.
//-----------------------------------------------------------------------------
D3D12_GPU_VIRTUAL_ADDRESS ModelMgr::AddVertices(const Vertex* ptr, uint32_t count)
{
    assert(m_OffsetVB + count < m_MaxCountVB);
    D3D12_GPU_VIRTUAL_ADDRESS result = m_AddressVB + m_OffsetVB * sizeof(Vertex);

    for(uint32_t i=0; i<count; ++i)
    { m_pVertices[m_OffsetVB + i] = ptr[i]; }

    m_OffsetVB += count;

    return result;
}

//-----------------------------------------------------------------------------
//      インデックスデータを追加します.
//-----------------------------------------------------------------------------
D3D12_GPU_VIRTUAL_ADDRESS ModelMgr::AddInidices(const uint32_t* ptr, uint32_t count)
{
    assert(m_OffsetIB + count < m_MaxCountIB);
    D3D12_GPU_VIRTUAL_ADDRESS result = m_AddressIB + m_OffsetIB * sizeof(uint32_t);

    for(uint32_t i=0; i<count; ++i)
    { m_pIndices[m_OffsetIB + i] = ptr[i]; }

    m_OffsetIB += count;

    return result;
}

//-----------------------------------------------------------------------------
//      トランスフォームデータを追加します.
//-----------------------------------------------------------------------------
D3D12_GPU_VIRTUAL_ADDRESS ModelMgr::AddTransforms(const asdx::Transform3x4* ptr, uint32_t count)
{
    assert(m_OffsetTB + count < m_MaxCountTB);
    D3D12_GPU_VIRTUAL_ADDRESS result = m_AddressTB + m_OffsetTB * sizeof(asdx::Transform3x4);

    for(uint32_t i=0; i<count; ++i)
    { m_pTransforms[m_OffsetTB + i] = ptr[i]; }

    m_OffsetTB += count;

    return result;
}

//-----------------------------------------------------------------------------
//      マテリアルデータを追加します.
//-----------------------------------------------------------------------------
D3D12_GPU_VIRTUAL_ADDRESS ModelMgr::AddMaterials(const Material* ptr, uint32_t count)
{
    assert(m_OffsetMB + count < m_MaxCountMB);
    D3D12_GPU_VIRTUAL_ADDRESS result = m_AddressMB + m_OffsetMB * sizeof(Material);

    for(uint32_t i=0; i<count; ++i)
    { m_pMaterials[m_OffsetMB + i] = ptr[i]; }

    m_OffsetMB += count;

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

//-----------------------------------------------------------------------------
//      頂点バッファのGPU仮想アドレスを取得します.
//-----------------------------------------------------------------------------
D3D12_GPU_VIRTUAL_ADDRESS ModelMgr::GetAddressVB() const
{ return m_AddressVB; }

//-----------------------------------------------------------------------------
//      インデックスバッファのGPU仮想アドレスを取得します.
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
//      頂点バッファサイズを取得します.
//-----------------------------------------------------------------------------
uint32_t ModelMgr::GetSizeVB() const
{ return m_MaxCountVB * sizeof(Vertex); }

//-----------------------------------------------------------------------------
//      インデックスバッファサイズを取得します.
//-----------------------------------------------------------------------------
uint32_t ModelMgr::GetSizeIB() const
{ return m_MaxCountIB * sizeof(uint32_t); }

//-----------------------------------------------------------------------------
//      トランスフォームバッファサイズを取得します.
//-----------------------------------------------------------------------------
uint32_t ModelMgr::GetSizeTB() const
{ return m_MaxCountTB * sizeof(asdx::Transform3x4); }

//-----------------------------------------------------------------------------
//      マテリアルバッファサイズを取得します.
//-----------------------------------------------------------------------------
uint32_t ModelMgr::GetSizeMB() const
{ return m_MaxCountMB * sizeof(Material); }

} // namespace r3d
