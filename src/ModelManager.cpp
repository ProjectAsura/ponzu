//-----------------------------------------------------------------------------
// File : model_mgr.cpp
// Desc : Model Manager.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <ModelManager.h>
#include <gfx/asdxDevice.h>
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
    ID3D12GraphicsCommandList4* pCmdList,
    uint32_t maxInstanceCount,
    uint32_t maxMaterialCount
)
{
    auto pDevice = asdx::GetD3D12Device();

    const auto sizeIB = maxInstanceCount * sizeof(GpuInstance);
    const auto sizeTB = maxInstanceCount * sizeof(asdx::Transform3x4);
    const auto sizeMB = maxMaterialCount * sizeof(Material);

    m_MaxInstanceCount = maxInstanceCount;
    m_MaxMaterialCount = maxMaterialCount;

    m_OffsetInstance = 0;
    m_OffsetMaterial = 0;

    // デフォルトベースカラー生成.
    {
        asdx::ResTexture res;
        res.Dimension       = asdx::TEXTURE_DIMENSION_2D;
        res.Width           = 16;
        res.Height          = 16;
        res.Depth           = 0;
        res.Format          = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        res.MipMapCount     = 1;
        res.SurfaceCount    = 1;
        res.pResources      = new asdx::SubResource[1];

        auto& subRes = res.pResources[0];

        subRes.Width        = 16;
        subRes.Height       = 16;
        subRes.MipIndex     = 0;
        subRes.Pitch        = sizeof(uint8_t) * 4 * subRes.Width;
        subRes.SlicePitch   = subRes.Pitch * subRes.Height;
        subRes.pPixels      = new uint8_t [subRes.SlicePitch];

        for(auto y=0u; y<res.Height; y++)
        {
            for(auto x=0u; x<res.Width; x++)
            {
                auto idx = y * 4 * res.Width + x * 4;
        #if 0
                // 18%グレー.
                subRes.pPixels[idx + 0] = 119;
                subRes.pPixels[idx + 1] = 119;
                subRes.pPixels[idx + 2] = 119;
                subRes.pPixels[idx + 3] = 255;
        #else
                // White
                subRes.pPixels[idx + 0] = 255;
                subRes.pPixels[idx + 1] = 255;
                subRes.pPixels[idx + 2] = 255;
                subRes.pPixels[idx + 3] = 255;
        #endif
            }
        }

        if (!m_DefaultBaseColor.Init(pCmdList, res))
        {
            ELOGA("Error : Default Base Color Init Failed.");
            res.Dispose();
            return false;
        }

        res.Dispose();
    }

    // フラット法線生成.
    {
        asdx::ResTexture res;
        res.Dimension       = asdx::TEXTURE_DIMENSION_2D;
        res.Width           = 16;
        res.Height          = 16;
        res.Depth           = 0;
        res.Format          = DXGI_FORMAT_R8G8B8A8_UNORM;
        res.MipMapCount     = 1;
        res.SurfaceCount    = 1;
        res.pResources      = new asdx::SubResource[1];

        auto& subRes = res.pResources[0];

        subRes.Width        = 16;
        subRes.Height       = 16;
        subRes.MipIndex     = 0;
        subRes.Pitch        = sizeof(uint8_t) * 4 * subRes.Width;
        subRes.SlicePitch   = subRes.Pitch * subRes.Height;
        subRes.pPixels      = new uint8_t [subRes.SlicePitch];

        for(auto y=0u; y<res.Height; y++)
        {
            for(auto x=0u; x<res.Width; x++)
            {
                auto idx = y * 4 * res.Width + x * 4;
                subRes.pPixels[idx + 0] = 128;
                subRes.pPixels[idx + 1] = 128;
                subRes.pPixels[idx + 2] = 255;
                subRes.pPixels[idx + 3] = 255;
            }
        }

        if (!m_DefaultNormal.Init(pCmdList, res))
        {
            ELOGA("Error : Default Normal Init Failed.");
            res.Dispose();
            return false;
        }

        res.Dispose();
    }

    // デフォルトORM生成.
    {
        asdx::ResTexture res;
        res.Dimension       = asdx::TEXTURE_DIMENSION_2D;
        res.Width           = 16;
        res.Height          = 16;
        res.Depth           = 0;
        res.Format          = DXGI_FORMAT_R8G8B8A8_UNORM;
        res.MipMapCount     = 1;
        res.SurfaceCount    = 1;
        res.pResources      = new asdx::SubResource[1];

        auto& subRes = res.pResources[0];

        subRes.Width        = 16;
        subRes.Height       = 16;
        subRes.MipIndex     = 0;
        subRes.Pitch        = sizeof(uint8_t) * 4 * subRes.Width;
        subRes.SlicePitch   = subRes.Pitch * subRes.Height;
        subRes.pPixels      = new uint8_t [subRes.SlicePitch];

        for(auto y=0u; y<res.Height; y++)
        {
            for(auto x=0u; x<res.Width; x++)
            {
                auto idx = y * 4 * res.Width + x * 4;
                subRes.pPixels[idx + 0] = 255;
                subRes.pPixels[idx + 1] = 255;
                subRes.pPixels[idx + 2] = 0;
                subRes.pPixels[idx + 3] = 255;
            }
        }

        if (!m_DefaultORM.Init(pCmdList, res))
        {
            ELOGA("Error : Default ORM Init Failed.");
            res.Dispose();
            return false;
        }

        res.Dispose();
    }

    // デフォルトエミッシブ生成.
    {
        asdx::ResTexture res;
        res.Dimension       = asdx::TEXTURE_DIMENSION_2D;
        res.Width           = 16;
        res.Height          = 16;
        res.Depth           = 0;
        res.Format          = DXGI_FORMAT_R8G8B8A8_UNORM;
        res.MipMapCount     = 1;
        res.SurfaceCount    = 1;
        res.pResources      = new asdx::SubResource[1];

        auto& subRes = res.pResources[0];

        subRes.Width        = 16;
        subRes.Height       = 16;
        subRes.MipIndex     = 0;
        subRes.Pitch        = sizeof(uint8_t) * 4 * subRes.Width;
        subRes.SlicePitch   = subRes.Pitch * subRes.Height;
        subRes.pPixels      = new uint8_t [subRes.SlicePitch];

        for(auto y=0u; y<res.Height; y++)
        {
            for(auto x=0u; x<res.Width; x++)
            {
                auto idx = y * 4 * res.Width + x * 4;
                subRes.pPixels[idx + 0] = 0;
                subRes.pPixels[idx + 1] = 0;
                subRes.pPixels[idx + 2] = 0;
                subRes.pPixels[idx + 3] = 0;
            }
        }

        if (!m_Black.Init(pCmdList, res))
        {
            ELOGA("Error : Default Emissive Init Failed.");
            res.Dispose();
            return false;
        }

        res.Dispose();
    }

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

    if (!asdx::CreateBufferSRV(pDevice, m_MB.GetPtr(), UINT(sizeMB / sizeof(Material)), sizeof(Material), m_MB_SRV.GetAddress()))
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
        m_Meshes[i].VertexCount = 0;
        m_Meshes[i].IndexCount  = 0;
    }

    m_Meshes.clear();
    m_Meshes.shrink_to_fit();

    m_DefaultBaseColor  .Term();
    m_DefaultNormal     .Term();
    m_DefaultORM        .Term();
    m_Black             .Term();

    m_GeometryHandles.clear();
    m_InstanceHandles.clear();
    m_CpuInstances   .clear();
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
        auto vbSize = mesh.VertexCount * sizeof(ResVertex);
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

        item.VB->SetName(L"ModelMangaer::VB");

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

        item.IB->SetName(L"ModelManager::IB");

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

    item.VertexCount = mesh.VertexCount;
    item.IndexCount  = mesh.IndexCount;

    result.AddressVB    = item.VB->GetGPUVirtualAddress();
    result.AddressIB    = item.IB->GetGPUVirtualAddress();
    result.IndexVB      = item.VB_SRV->GetDescriptorIndex();
    result.IndexIB      = item.IB_SRV->GetDescriptorIndex();

    m_GeometryHandles.push_back(result);

    m_Meshes.emplace_back(item);
    return result;
}

//-----------------------------------------------------------------------------
//      インスタンスを登録します.
//-----------------------------------------------------------------------------
InstanceHandle ModelMgr::AddInstance(const CpuInstance& instance)
{
    assert(m_OffsetInstance + 1 < m_MaxInstanceCount);
    assert(instance.MeshId < m_Meshes.size());

    auto idx = m_OffsetInstance;
    m_pInstances[idx].VertexBufferId = m_Meshes[instance.MeshId].VB_SRV->GetDescriptorIndex();
    m_pInstances[idx].IndexBufferId  = m_Meshes[instance.MeshId].IB_SRV->GetDescriptorIndex();
    m_pInstances[idx].MaterialId     = instance.MaterialId;

    m_pTransforms[idx] = instance.Transform;

    m_OffsetInstance++;

    InstanceHandle result = {};
    result.InstanceId = idx;
    result.AddressTB  = m_AddressTB + idx * sizeof(asdx::Transform3x4);

    m_InstanceHandles.push_back(result);
    m_CpuInstances   .push_back(instance);

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
    {
        auto& src = ptr[i];
        auto& dst = m_pMaterials[m_OffsetMaterial + i];

        dst.BaseColorMap = GetBaseColor(src.BaseColorMap);
        dst.NormalMap    = GetNormal(src.NormalMap);
        dst.OrmMap       = GetOrm(src.OrmMap);
        dst.EmissiveMap  = GetEmissive(src.EmissiveMap);

        dst.BaseColor  = src.BaseColor;
        dst.Occlusion  = src.Occlusion;
        dst.Roughness  = src.Roughness;
        dst.Metalness  = src.Metalness;

        dst.Emissive   = src.Emissive;
        dst.Ior        = src.Ior;
    }

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
{ return m_MaxInstanceCount * sizeof(GpuInstance); }

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

//-----------------------------------------------------------------------------
//      ベースカラーハンドルを取得します.
//-----------------------------------------------------------------------------
uint32_t ModelMgr::GetBaseColor(uint32_t handle)
{
    return (handle == INVALID_MATERIAL_MAP) 
        ? m_DefaultBaseColor.GetView()->GetDescriptorIndex()
        : handle;
}

//-----------------------------------------------------------------------------
//      法線ハンドルを取得します.
//-----------------------------------------------------------------------------
uint32_t ModelMgr::GetNormal(uint32_t handle)
{
    return (handle == INVALID_MATERIAL_MAP)
        ? m_DefaultNormal.GetView()->GetDescriptorIndex()
        : handle;
}

//-----------------------------------------------------------------------------
//      ORMハンドルを取得します.
//-----------------------------------------------------------------------------
uint32_t ModelMgr::GetOrm(uint32_t handle)
{
    return (handle == INVALID_MATERIAL_MAP)
        ? m_DefaultORM.GetView()->GetDescriptorIndex()
        : handle;
}

//-----------------------------------------------------------------------------
//      エミッシブハンドルを取得します.
//-----------------------------------------------------------------------------
uint32_t ModelMgr::GetEmissive(uint32_t handle)
{
    return (handle == INVALID_MATERIAL_MAP)
        ? m_Black.GetView()->GetDescriptorIndex()
        : handle;
}

//-----------------------------------------------------------------------------
//      マスクハンドルを取得します.
//-----------------------------------------------------------------------------
uint32_t ModelMgr::GetMask(uint32_t handle)
{
    return (handle == INVALID_MATERIAL_MAP)
        ? m_Black.GetView()->GetDescriptorIndex()
        : handle;
}

//-----------------------------------------------------------------------------
//      メッシュを取得します.
//-----------------------------------------------------------------------------
const ModelMgr::MeshBuffer& ModelMgr::GetMesh(size_t index) const
{ return m_Meshes[index]; }

//-----------------------------------------------------------------------------
//      メッシュ数を取得します.
//-----------------------------------------------------------------------------
size_t ModelMgr::GetMeshCount() const
{ return m_Meshes.size(); }

//-----------------------------------------------------------------------------
//      インスタンス数を取得します.
//-----------------------------------------------------------------------------
size_t ModelMgr::GetInstanceCount() const
{ return m_CpuInstances.size(); }

//-----------------------------------------------------------------------------
//      ジオメトリハンドルを取得します.
//-----------------------------------------------------------------------------
GeometryHandle ModelMgr::GetGeometryHandle(uint32_t index) const
{
    assert(index < m_GeometryHandles.size());
    return m_GeometryHandles[index];
}

//-----------------------------------------------------------------------------
//      インスタンスハンドルを取得します.
//-----------------------------------------------------------------------------
InstanceHandle ModelMgr::GetInstanceHandle(uint32_t index) const
{
    assert(index < m_InstanceHandles.size());
    return m_InstanceHandles[index];
}

//-----------------------------------------------------------------------------
//      CPUインスタンスを取得します.
//-----------------------------------------------------------------------------
CpuInstance ModelMgr::GetCpuInstance(uint32_t index) const
{
    assert(index < m_CpuInstances.size());
    return m_CpuInstances[index];
}

} // namespace r3d
