//-----------------------------------------------------------------------------
// File : scene.cpp
// Desc : Scene Data.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <scene.h>
#include <fnd/asdxLogger.h>
#include <gfx/asdxGraphicsSystem.h>
#include <generated/scene_format.h>
#include <Windows.h>


namespace {

///////////////////////////////////////////////////////////////////////////////
// TEXTURE_DIMENSION enum
///////////////////////////////////////////////////////////////////////////////
enum TEXTURE_DIMENSION
{
    TEXTURE_DIMENSION_UNKNOWN,
    TEXTURE_DIMENSION_1D,
    TEXTURE_DIMENSION_2D,
    TEXTURE_DIMENSION_3D,
    TEXTURE_DIMENSION_CUBE
};

//-----------------------------------------------------------------------------
//      必要な中間データサイズを取得します.
//-----------------------------------------------------------------------------
inline UINT64 GetRequiredIntermediateSize
(
    ID3D12Device*           pDevice,
    D3D12_RESOURCE_DESC*    pDesc,
    UINT                    firstSubresource,
    UINT                    subresourceCount
) noexcept
{
    UINT64 requiredSize = 0;
    pDevice->GetCopyableFootprints(
        pDesc,
        firstSubresource,
        subresourceCount,
        0,
        nullptr,
        nullptr,
        nullptr,
        &requiredSize);
    return requiredSize;
}

//-----------------------------------------------------------------------------
//      サブリソースのコピーを行います.
//-----------------------------------------------------------------------------
inline void CopySubresource
(
    const D3D12_MEMCPY_DEST*        pDst,
    const D3D12_SUBRESOURCE_DATA*   pSrc,
    SIZE_T                          rowSizeInBytes,
    UINT                            rowCount,
    UINT                            sliceCount
) noexcept
{
    for (auto z=0u; z<sliceCount; ++z)
    {
        auto pDstSlice = static_cast<BYTE*>(pDst->pData)       + pDst->SlicePitch * z;
        auto pSrcSlice = static_cast<const BYTE*>(pSrc->pData) + pSrc->SlicePitch * LONG_PTR(z);
        for (auto y=0u; y<rowCount; ++y)
        {
            memcpy(pDstSlice + pDst->RowPitch * y,
                   pSrcSlice + pSrc->RowPitch * LONG_PTR(y),
                   rowSizeInBytes);
        }
    }
}

//-----------------------------------------------------------------------------
//      テクスチャを更新します.
//-----------------------------------------------------------------------------
void UpdateTexture
(
    ID3D12GraphicsCommandList6*     pCmdList,
    ID3D12Resource*                 pDstResource,
    const r3d::ResTexture*          pResTexture
)
{
    if (pDstResource == nullptr || pResTexture == nullptr)
    { return; }

    auto device = asdx::GetD3D12Device();
    auto dstDesc = pDstResource->GetDesc();

    auto count = pResTexture->MipLevels() * pResTexture->SurfaceCount();

    D3D12_RESOURCE_DESC uploadDesc = {
        D3D12_RESOURCE_DIMENSION_BUFFER,
        0,
        GetRequiredIntermediateSize(device, &dstDesc, 0, count),
        1,
        1,
        1,
        DXGI_FORMAT_UNKNOWN,
        { 1, 0 },
        D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        D3D12_RESOURCE_FLAG_NONE
    };

    D3D12_HEAP_PROPERTIES props = {
        D3D12_HEAP_TYPE_UPLOAD,
        D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        D3D12_MEMORY_POOL_UNKNOWN,
        1,
        1
    };

    ID3D12Resource* pSrcResource = nullptr;
    auto hr = device->CreateCommittedResource(
        &props,
        D3D12_HEAP_FLAG_NONE,
        &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&pSrcResource));
    if (FAILED(hr))
    {
        ELOG("Error : ID3D12Device::CreateCommitedResource() Failed. errcode = 0x%x", hr);
        return;
    }

    // コマンドを生成.
    {
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts;
        std::vector<UINT>                               rows;
        std::vector<UINT64>                             rowSizeInBytes;

        layouts       .resize(count);
        rows          .resize(count);
        rowSizeInBytes.resize(count);

        UINT64 requiredSize = 0;
        device->GetCopyableFootprints(
            &dstDesc, 0, count, 0, layouts.data(), rows.data(), rowSizeInBytes.data(), &requiredSize);

        BYTE* pData = nullptr;
        hr = pSrcResource->Map(0, nullptr, reinterpret_cast<void**>(&pData));
        if (FAILED(hr))
        {
            ELOG("Error : ID3D12Resource::Map() Failed. errcode = 0x%x", hr);
            pSrcResource->Release();
            return;
        }

        for(auto i=0u; i<count; ++i)
        {
            D3D12_SUBRESOURCE_DATA srcData = {};
            srcData.pData       = pResTexture->Resources()->Get(i)->Pixels();
            srcData.RowPitch    = pResTexture->Resources()->Get(i)->Pitch();
            srcData.SlicePitch  = pResTexture->Resources()->Get(i)->SlicePitch();

            D3D12_MEMCPY_DEST dstData = {};
            dstData.pData       = pData + layouts[i].Offset;
            dstData.RowPitch    = layouts[i].Footprint.RowPitch;
            dstData.SlicePitch  = SIZE_T(layouts[i].Footprint.RowPitch) * SIZE_T(rows[i]);

            CopySubresource(
                &dstData,
                &srcData,
                SIZE_T(rowSizeInBytes[i]),
                rows[i],
                layouts[i].Footprint.Depth);
        }
        pSrcResource->Unmap(0, nullptr);

        if (dstDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        {
            pCmdList->CopyBufferRegion(
                pDstResource,
                0,
                pSrcResource,
                layouts[0].Offset,
                layouts[0].Footprint.Width);
        }
        else
        {
            for(auto i=0u; i<count; ++i)
            {
                D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
                dstLoc.pResource        = pDstResource;
                dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                dstLoc.SubresourceIndex = i;

                D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
                srcLoc.pResource        = pSrcResource;
                srcLoc.Type             = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                srcLoc.PlacedFootprint  = layouts[i];

                pCmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
            }
        }
    }

    asdx::Dispose(pSrcResource);
}

} // namespace


namespace r3d {

///////////////////////////////////////////////////////////////////////////////
// SceneTexture
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      初期化処理を行います.
//-----------------------------------------------------------------------------
bool SceneTexture::Init(ID3D12GraphicsCommandList6* pCmdList, const void* resTexture)
{
    const auto resource = reinterpret_cast<const r3d::ResTexture*>(resTexture);
    if (resource == nullptr)
    {
        ELOGA("Error : Invalid Argument.");
        return false;
    }

    auto pDevice = asdx::GetD3D12Device();

    auto dimension  = D3D12_RESOURCE_DIMENSION_UNKNOWN;
    auto isCube     = false;
    auto depth      = 1;
    auto format     = DXGI_FORMAT(resource->Format());

#if ASDX_IS_SCARLETT
    auto mostDetailedMip = resourc.MipMapCount - 1;
#else
    auto mostDetailedMip = 0u;
#endif

    D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
    viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    ID3D12Resource* pResource = nullptr;
    {
        D3D12_HEAP_PROPERTIES props = {
            D3D12_HEAP_TYPE_DEFAULT,
            D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
            D3D12_MEMORY_POOL_UNKNOWN,
            1,
            1
        };

        switch(resource->Dimension())
        {
        case TEXTURE_DIMENSION_1D:
            {
                dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
                if (resource->SurfaceCount() > 1)
                {
                    viewDesc.ViewDimension                      = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
                    viewDesc.Format                             = format;
                    viewDesc.Texture1DArray.ArraySize           = resource->SurfaceCount();
                    viewDesc.Texture1DArray.FirstArraySlice     = 0;
                    viewDesc.Texture1DArray.MipLevels           = resource->MipLevels();
                    viewDesc.Texture1DArray.MostDetailedMip     = mostDetailedMip;
                    viewDesc.Texture1DArray.ResourceMinLODClamp = 0;
                }
                else
                {
                    viewDesc.ViewDimension                  = D3D12_SRV_DIMENSION_TEXTURE1D;
                    viewDesc.Format                         = format;
                    viewDesc.Texture1D.MipLevels            = resource->MipLevels();
                    viewDesc.Texture1D.MostDetailedMip      = mostDetailedMip;
                    viewDesc.Texture1D.ResourceMinLODClamp  = 0;
                }
            }
            break;

        case TEXTURE_DIMENSION_2D:
            {
                dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                if (resource->SurfaceCount() > 1)
                {
                    viewDesc.ViewDimension                      = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                    viewDesc.Format                             = format;
                    viewDesc.Texture2DArray.ArraySize           = resource->SurfaceCount();
                    viewDesc.Texture2DArray.FirstArraySlice     = 0;
                    viewDesc.Texture2DArray.MipLevels           = resource->MipLevels();
                    viewDesc.Texture2DArray.MostDetailedMip     = mostDetailedMip;
                    viewDesc.Texture2DArray.PlaneSlice          = 0;
                    viewDesc.Texture2DArray.ResourceMinLODClamp = 0;
                }
                else
                {
                    viewDesc.ViewDimension                      = D3D12_SRV_DIMENSION_TEXTURE2D;
                    viewDesc.Format                             = format;
                    viewDesc.Texture2D.MipLevels                = resource->MipLevels();
                    viewDesc.Texture2D.MostDetailedMip          = mostDetailedMip;
                    viewDesc.Texture2D.PlaneSlice               = 0;
                    viewDesc.Texture2D.ResourceMinLODClamp      = 0;
                }
            }
            break;

        case TEXTURE_DIMENSION_3D:
            {
                dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
                depth = resource->Depth();
            }
            break;

        case TEXTURE_DIMENSION_CUBE:
            {
                dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                depth = resource->SurfaceCount();
            }
            break;
        }

        D3D12_RESOURCE_DESC desc = {
            dimension,
            0,
            resource->Width(),
            resource->Height(),
            UINT16(depth),
            UINT16(resource->MipLevels()),
            format,
            { 1, 0 },
            D3D12_TEXTURE_LAYOUT_UNKNOWN,
            D3D12_RESOURCE_FLAG_NONE
        };

        auto hr = pDevice->CreateCommittedResource(
            &props,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&pResource));
        if (FAILED(hr))
        {
            ELOG("Error : ID3D12Device::CreateCommitedResource() Failed. errcode = 0x%x", hr);
            return false;
        }

        pResource->SetName(L"asdxTexture");
    }

    // シェーダリソースビューの生成.
    {
        if (!CreateShaderResourceView(pResource, &viewDesc, m_View.GetAddress()))
        {
            pResource->Release();
            pResource = nullptr;
            return false;
        }
    }

    UpdateTexture(pCmdList, pResource, resource);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                    = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags                   = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource    = pResource;
    barrier.Transition.Subresource  = 0;
    barrier.Transition.StateBefore  = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter   = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;

    pCmdList->ResourceBarrier(1, &barrier);

    pResource->Release();
    pResource = nullptr;

    return false;
}

//-----------------------------------------------------------------------------
//      終了処理を行います.
//-----------------------------------------------------------------------------
void SceneTexture::Term()
{ m_View.Reset(); }

//-----------------------------------------------------------------------------
//      シェーダリソースビューを取得します.
//-----------------------------------------------------------------------------
asdx::IShaderResourceView* SceneTexture::GetView() const
{ return m_View.GetPtr(); }


///////////////////////////////////////////////////////////////////////////////
// Scene class
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      バイナリからからロードします.
//-----------------------------------------------------------------------------
bool Scene::Init(const char* path, ID3D12GraphicsCommandList6* pCmdList)
{
    // ファイル読み込み.
    {
        auto hFile = CreateFileA(
            path,
            GENERIC_READ,
            0,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            ELOGA("Error : File Open Failed. path = %s");
            return false;
        }

        auto size = GetFileSize(hFile, NULL);
        m_pBinary = malloc(size);
        DWORD readSize = 0;
        auto ret = ReadFile(hFile, m_pBinary, size, &readSize, NULL);
        if (!ret)
        { ELOGA("Error : Read Failed. path = %s", path); }

        CloseHandle(hFile);
    }

    // シーンリソース取得.
    auto resScene = GetResScene(m_pBinary);
    assert(resScene != nullptr);

    auto pDevice   = asdx::GetD3D12Device();
    auto buildFlag = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

    // IBLテクスチャのセットアップ.
    {
        if (!m_IBL.Init(pCmdList, resScene->IblTexture()))
        {
            ELOGA("Error : IBL Initialize Failed.");
            return false;
        }
    }

    // テクスチャセットアップ.
    {
        auto count       = resScene->TextureCount();
        auto resTextures = resScene->Textures();
        assert(resTextures != nullptr);
        m_Textures.resize(count);

        for(auto i=0u; i<count; ++i)
        {
            if (!m_Textures[i].Init(pCmdList, resTextures->Get(i)))
            {
                ELOGA("Error : SceneTexture::Init() Failed. index = %u", i);
                return false;
            }
        }
    }

    // マテリアル登録.
    {
        auto count        = resScene->MaterialCount();
        auto resMaterials = resScene->Materials();
        assert(resMaterials != nullptr);

        for(auto i=0u; i<count; ++i)
        {
            auto srcMaterial = resMaterials->Get(i);
            assert(srcMaterial != nullptr);

            r3d::Material material = {};
            material.BaseColor = GetTextureHandle(srcMaterial->BaseColor());
            material.Normal    = GetTextureHandle(srcMaterial->Normal());
            material.ORM       = GetTextureHandle(srcMaterial->Orm());
            material.Emissive  = GetTextureHandle(srcMaterial->Emissive());

            m_ModelMgr.AddMaterials(&material, 1);
        }
    }

    // BLAS構築.
    {
        auto count = resScene->MeshCount();

        m_BLAS     .resize(count);
        m_DrawCalls.resize(count);

        auto resMeshes = resScene->Meshes();
        assert(resMeshes != nullptr);

        for(auto i=0u; i<count; ++i)
        {
            auto srcMesh = resMeshes->Get(i);
            assert(srcMesh != nullptr);

            r3d::Mesh mesh = {};
            mesh.VertexCount = srcMesh->VertexCount();
            mesh.IndexCount  = srcMesh->IndexCount();
            mesh.Vertices    = reinterpret_cast<const r3d::Vertex*>(srcMesh->Vertices()->Data());
            mesh.Indices     = reinterpret_cast<const uint32_t*>(srcMesh->Indices()->Data());

            auto geometryHandle = m_ModelMgr.AddMesh(mesh);

            D3D12_RAYTRACING_GEOMETRY_DESC desc = {};
            desc.Type                                   = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            desc.Flags                                  = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
            desc.Triangles.IndexFormat                  = DXGI_FORMAT_R32_UINT;
            desc.Triangles.IndexCount                   = mesh.IndexCount;
            desc.Triangles.IndexBuffer                  = geometryHandle.AddressIB;
            desc.Triangles.VertexFormat                 = DXGI_FORMAT_R32G32B32_FLOAT;
            desc.Triangles.VertexBuffer.StartAddress    = geometryHandle.AddressVB;
            desc.Triangles.VertexBuffer.StrideInBytes   = sizeof(Vertex);
            desc.Triangles.VertexCount                  = mesh.VertexCount;

            if (!m_BLAS[i].Init(pDevice, 1, &desc, buildFlag))
            {
                ELOGA("Error : Blas::Init() Failed. index = %llu", i);
                return false;
            }

            m_BLAS[i].Build(pCmdList);

            D3D12_VERTEX_BUFFER_VIEW vbv = {};
            vbv.BufferLocation  = geometryHandle.AddressVB;
            vbv.SizeInBytes     = sizeof(Vertex) * mesh.VertexCount;
            vbv.StrideInBytes   = sizeof(Vertex);

            D3D12_INDEX_BUFFER_VIEW ibv = {};
            ibv.BufferLocation  = geometryHandle.AddressIB;
            ibv.SizeInBytes     = sizeof(uint32_t) * mesh.IndexCount;
            ibv.Format          = DXGI_FORMAT_R32_UINT;

            m_DrawCalls[i].IndexCount   = mesh.IndexCount;
            m_DrawCalls[i].VBV          = vbv;
            m_DrawCalls[i].IBV          = ibv;
            m_DrawCalls[i].IndexVB      = geometryHandle.IndexVB;
            m_DrawCalls[i].IndexIB      = geometryHandle.IndexIB;
            m_DrawCalls[i].MaterialId   = srcMesh->MateiralId();
        }
    }

    // TLAS構築.
    {
        auto count = resScene->InstanceCount();

        std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
        instanceDescs.resize(count);
        m_Instances.resize(count);

        auto resInstances = resScene->Instances();

        for(auto i=0u; i<count; ++i)
        {
            auto srcInstance = resInstances->Get(i);
            auto& dstDesc = instanceDescs[i];

            auto& r0 = srcInstance->Transform().row0();
            auto& r1 = srcInstance->Transform().row1();
            auto& r2 = srcInstance->Transform().row2();

            asdx::Transform3x4 transform;
            transform.m[0][0] = r0.x();
            transform.m[0][1] = r0.y();
            transform.m[0][2] = r0.z();
            transform.m[0][3] = r0.w();

            transform.m[1][0] = r1.x();
            transform.m[1][1] = r1.y();
            transform.m[1][2] = r1.z();
            transform.m[1][3] = r1.w();

            transform.m[2][0] = r2.x();
            transform.m[2][1] = r2.y();
            transform.m[2][2] = r2.z();
            transform.m[2][3] = r2.w();

            auto meshId = srcInstance->MeshIndex();
            assert(meshId < resScene->MeshCount());

            r3d::Instance instance;
            instance.VertexBufferId = m_DrawCalls[meshId].IndexVB;
            instance.IndexBufferId  = m_DrawCalls[meshId].IndexIB;
            instance.MaterialId     = m_DrawCalls[meshId].MaterialId;

            auto instanceHandle = m_ModelMgr.AddInstance(instance, transform);

            memcpy(dstDesc.Transform, transform.m, sizeof(float) * 12);
            dstDesc.InstanceID                          = instanceHandle.InstanceId;
            dstDesc.InstanceMask                        = 0xFF;
            dstDesc.InstanceContributionToHitGroupIndex = 0;
            dstDesc.Flags                               = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
            dstDesc.AccelerationStructure               = m_BLAS[meshId].GetResource()->GetGPUVirtualAddress();

            m_Instances[i].InstanceId = instanceHandle.InstanceId;
            m_Instances[i].MeshId     = meshId;
        }

        if (!m_TLAS.Init(pDevice, resScene->InstanceCount(), instanceDescs.data(), buildFlag))
        {
            ELOGA("Error : Tlas::Init() Failed.");
            return false;
        }

        m_TLAS.Build(pCmdList);
    }

    return true;
}

//-----------------------------------------------------------------------------
//      終了処理を行います.
//-----------------------------------------------------------------------------
void Scene::Term()
{
    for(size_t i=0; i<m_BLAS.size(); ++i)
    { m_BLAS[i].Term(); }
    m_BLAS.clear();

    for(size_t i=0; i<m_Textures.size(); ++i)
    { m_Textures[i].Term(); }
    m_Textures.clear();

    m_TLAS    .Term();
    m_Param   .Term();
    m_ModelMgr.Term();
    m_IBL     .Term();

    m_DrawCalls.clear();
    m_Instances.clear();

    if (m_pBinary != nullptr)
    {
        free(m_pBinary);
        m_pBinary = nullptr;
    }
}

//-----------------------------------------------------------------------------
//      定数バッファを取得します.
//-----------------------------------------------------------------------------
asdx::IConstantBufferView* Scene::GetParamCBV() const
{ return m_Param.GetView(); }

//-----------------------------------------------------------------------------
//      インスタンスバッファのシェーダリソースビューを取得します.
//-----------------------------------------------------------------------------
asdx::IShaderResourceView* Scene::GetIB() const
{ return m_ModelMgr.GetIB(); }

//-----------------------------------------------------------------------------
//      トランスフォームバッファのシェーダリソースビューを取得します.
//-----------------------------------------------------------------------------
asdx::IShaderResourceView* Scene::GetTB() const
{ return m_ModelMgr.GetTB(); }

//-----------------------------------------------------------------------------
//      マテリアルバッファのシェーダリソースビューを取得します.
//-----------------------------------------------------------------------------
asdx::IShaderResourceView* Scene::GetMB() const
{ return m_ModelMgr.GetMB(); }

//-----------------------------------------------------------------------------
//      IBLのシェーダリソースビューを取得します.
//-----------------------------------------------------------------------------
asdx::IShaderResourceView* Scene::GetIBL() const
{ return m_IBL.GetView(); }

//-----------------------------------------------------------------------------
//      描画処理を行います.
//-----------------------------------------------------------------------------
void Scene::Draw(ID3D12GraphicsCommandList6* pCmdList)
{
    pCmdList->SetGraphicsRootConstantBufferView(0, m_Param.GetResource()->GetGPUVirtualAddress());
    pCmdList->SetGraphicsRootShaderResourceView(2, m_ModelMgr.GetAddressTB());
    pCmdList->SetGraphicsRootShaderResourceView(3, m_ModelMgr.GetAddressMB());
    pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    auto count = m_Instances.size();
    for(auto i=0u; i<count; ++i)
    {
        auto& instance = m_Instances[i];
        pCmdList->SetGraphicsRoot32BitConstant(1, instance.InstanceId, 0);

        auto& dc = m_DrawCalls[instance.MeshId];
        pCmdList->IASetVertexBuffers(0, 1, &dc.VBV);
        pCmdList->IASetIndexBuffer(&dc.IBV);

        pCmdList->DrawIndexedInstanced(dc.IndexCount, 1, 0, 0, 0);
    }
}

//-----------------------------------------------------------------------------
//      テクスチャハンドルを取得します.
//-----------------------------------------------------------------------------
uint32_t Scene::GetTextureHandle(uint32_t index)
{
    return (index != INVALID_MATERIAL_MAP)
        ? m_Textures[index].GetView()->GetDescriptorIndex()
        : INVALID_MATERIAL_MAP;
}

} // namespace r3d
