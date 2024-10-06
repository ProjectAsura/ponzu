//-----------------------------------------------------------------------------
// File : Scene.cpp
// Desc : Scene Data.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <Scene.h>
#include <fnd/asdxLogger.h>
#include <fnd/asdxMisc.h>
#include <gfx/asdxDevice.h>
#include <generated/scene_format.h>
#include <xxhash.h>
#include <Windows.h>
#include <Macro.h>

#if !CAMP_RELEASE
#include <OBJLoader.h>
#include <fstream>
#include <map>
#include <ctime>
#endif//!CAMP_RELEASE


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
    auto dstPtr = static_cast<BYTE*>(pDst->pData);
    auto srcPtr = static_cast<const BYTE*>(pSrc->pData);
    assert(dstPtr != nullptr);
    assert(srcPtr != nullptr);

    for (auto z=0u; z<sliceCount; ++z)
    {
        auto pDstSlice = dstPtr + pDst->SlicePitch * LONG_PTR(z);
        auto pSrcSlice = srcPtr + pSrc->SlicePitch * LONG_PTR(z);
        for (auto y=0u; y<rowCount; ++y)
        {
            memcpy(pDstSlice + pDst->RowPitch * LONG_PTR(y),
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
    ID3D12GraphicsCommandList*      pCmdList,
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
            auto srcResource = pResTexture->Resources()->Get(i);

            D3D12_SUBRESOURCE_DATA srcData = {};
            srcData.pData       = srcResource->Pixels();
            srcData.RowPitch    = srcResource->Pitch();
            srcData.SlicePitch  = srcResource->SlicePitch();
            assert(layouts[i].Footprint.Width  == srcResource->Width());
            assert(layouts[i].Footprint.Height == srcResource->Height());

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

asdx::Vector4 FromBinaryFormat(const r3d::Vector4& value)
{ return asdx::Vector4(value.x(), value.y(), value.z(), value.w()); }

asdx::Vector3 FromBinaryFormat(const r3d::Vector3& value)
{ return asdx::Vector3(value.x(), value.y(), value.z()); }

asdx::Vector2 FromBinaryFormat(const r3d::Vector2& value)
{ return asdx::Vector2(value.x(), value.y()); }

r3d::Vector4 ToBinaryFormat(const asdx::Vector4& value)
{ return r3d::Vector4(value.x, value.y, value.z, value.w); }

r3d::Vector3 ToBinaryFormat(const asdx::Vector3& value)
{ return r3d::Vector3(value.x, value.y, value.z); }

r3d::Vector2 ToBinaryFormat(const asdx::Vector2& value)
{ return r3d::Vector2(value.x, value.y); }

} // namespace


namespace r3d {

//-----------------------------------------------------------------------------
//      ハッシュタグを計算します.
//-----------------------------------------------------------------------------
uint32_t CalcHashTag(const char* name, size_t nameLength)
{ return XXH32(name, nameLength, 12345); }

//-----------------------------------------------------------------------------
//      ハッシュタグを計算します.
//-----------------------------------------------------------------------------
uint32_t CalcHashTag(const std::string& name)
{ return CalcHashTag(name.c_str(), name.length()); }

///////////////////////////////////////////////////////////////////////////////
// SceneTexture
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      初期化処理を行います.
//-----------------------------------------------------------------------------
bool SceneTexture::Init
(
    ID3D12GraphicsCommandList4* pCmdList,
    const void*                 resTexture,
    uint32_t                    componentMapping
)
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

    auto mostDetailedMip = 0u;

    D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
    viewDesc.Shader4ComponentMapping = componentMapping;

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

                viewDesc.ViewDimension                  = D3D12_SRV_DIMENSION_TEXTURE3D;
                viewDesc.Format                         = format;
                viewDesc.Texture3D.MipLevels            = resource->MipLevels();
                viewDesc.Texture3D.MostDetailedMip      = 0;
                viewDesc.Texture3D.ResourceMinLODClamp  = 0.0f;
            }
            break;

        case TEXTURE_DIMENSION_CUBE:
            {
                dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                depth = resource->SurfaceCount();

                if (resource->SurfaceCount() > 6)
                {
                   viewDesc.ViewDimension                           = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
                   viewDesc.Format                                  = format;
                   viewDesc.TextureCubeArray.First2DArrayFace       = 0;
                   viewDesc.TextureCubeArray.MipLevels              = resource->MipLevels();
                   viewDesc.TextureCubeArray.MostDetailedMip        = 0;
                   viewDesc.TextureCubeArray.NumCubes               = resource->SurfaceCount() / 6;
                   viewDesc.TextureCubeArray.ResourceMinLODClamp    = 0.0f;
                }
                else
                {
                    viewDesc.ViewDimension                      = D3D12_SRV_DIMENSION_TEXTURECUBE;
                    viewDesc.Format                             = format;
                    viewDesc.TextureCube.MipLevels              = resource->MipLevels();
                    viewDesc.TextureCube.MostDetailedMip        = 0;
                    viewDesc.TextureCube.ResourceMinLODClamp    = 0;
                }
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

    return true;
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
bool Scene::Init(const char* path, ID3D12GraphicsCommandList4* pCmdList)
{
    if (!m_ModelMgr.Init(pCmdList, UINT16_MAX, UINT16_MAX))
    {
        ELOGA("Error : ModelMgr::Init() Failed.");
        return false;
    }

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
            material.BaseColorMap = GetTextureHandle(srcMaterial->BaseColorMap());
            material.NormalMap    = GetTextureHandle(srcMaterial->NormalMap());
            material.OrmMap       = GetTextureHandle(srcMaterial->OrmMap());
            material.EmissiveMap  = GetTextureHandle(srcMaterial->EmissiveMap());

            material.BaseColor = FromBinaryFormat(srcMaterial->BaseColor());
            material.Occlusion = srcMaterial->Occlusion();
            material.Roughness = srcMaterial->Roughness();
            material.Metalness = srcMaterial->Metalness();
            material.Ior       = srcMaterial->Ior();
            material.Emissive  = FromBinaryFormat(srcMaterial->Emissive());

            m_ModelMgr.AddMaterials(&material, 1);
        }
    }

    // BLAS構築.
    {
        auto count = resScene->MeshCount();

        m_BLAS.resize(count);
        m_ScratchBLAS.resize(count);

        auto resMeshes = resScene->Meshes();
        assert(resMeshes != nullptr);

        for(auto i=0u; i<count; ++i)
        {
            auto srcMesh = resMeshes->Get(i);
            assert(srcMesh != nullptr);

            r3d::Mesh mesh = {};
            mesh.VertexCount = srcMesh->VertexCount();
            mesh.IndexCount  = srcMesh->IndexCount();
            mesh.Vertices    = const_cast<r3d::ResVertex*>(reinterpret_cast<const r3d::ResVertex*>(srcMesh->Vertices()->Data()));
            mesh.Indices     = const_cast<uint32_t*>(reinterpret_cast<const uint32_t*>(srcMesh->Indices()->Data()));

            auto geometryHandle = m_ModelMgr.AddMesh(mesh);

            D3D12_RAYTRACING_GEOMETRY_DESC desc = {};
            desc.Type                                   = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            desc.Flags                                  = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
            desc.Triangles.IndexFormat                  = DXGI_FORMAT_R32_UINT;
            desc.Triangles.IndexCount                   = mesh.IndexCount;
            desc.Triangles.IndexBuffer                  = geometryHandle.AddressIB;
            desc.Triangles.VertexFormat                 = DXGI_FORMAT_R32G32B32_FLOAT;
            desc.Triangles.VertexBuffer.StartAddress    = geometryHandle.AddressVB;
            desc.Triangles.VertexBuffer.StrideInBytes   = UINT(sizeof(ResVertex));
            desc.Triangles.VertexCount                  = mesh.VertexCount;

            if (!m_BLAS[i].Init(pDevice, 1, &desc, buildFlag))
            {
                ELOGA("Error : Blas::Init() Failed. index = %llu", i);
                return false;
            }

            auto size = m_BLAS[i].GetScratchBufferSize();
            if (!m_ScratchBLAS[i].Init(pDevice, size))
            {
                ELOGA("Error : AsScratchBuffer::Init() Failed. index = %llu", i);
                return false;
            }
            RTC_DEBUG_CODE(m_ScratchBLAS[i].SetName(L"ScratchBLAS"));

            m_BLAS[i].Build(pCmdList, m_ScratchBLAS[i].GetGpuAddress());
        }
    }

    // TLAS構築.
    {
        auto count = resScene->InstanceCount();
        assert(count > 0);

        std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
        instanceDescs.resize(count);
        m_Instances.resize(count);

        auto resInstances = resScene->Instances();

        m_DrawCalls.resize(count);
        auto resMeshes = resScene->Meshes();
        assert(resMeshes != nullptr);

        auto resInstanceTags = resScene->InstanceTags();

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

            auto matId = srcInstance->MaterialIndex();
            assert(matId < resScene->MaterialCount());

            r3d::CpuInstance instance;
            instance.MeshId     = meshId;
            instance.MaterialId = matId;
            instance.Transform  = transform;

            auto instanceHandle = m_ModelMgr.AddInstance(instance);

            memcpy(dstDesc.Transform, transform.m, sizeof(float) * 12);
            dstDesc.InstanceID                          = instanceHandle.InstanceId;
            dstDesc.InstanceMask                        = 0xFF;
            dstDesc.InstanceContributionToHitGroupIndex = 0;
            dstDesc.Flags                               = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
            dstDesc.AccelerationStructure               = m_BLAS[meshId].GetResource()->GetGPUVirtualAddress();

            m_Instances[i].InstanceId = instanceHandle.InstanceId;
            m_Instances[i].MeshId     = meshId;

            auto mesh = resMeshes->Get(meshId);

            auto geometryHandle = m_ModelMgr.GetGeometryHandle(meshId);

            D3D12_VERTEX_BUFFER_VIEW vbv = {};
            vbv.BufferLocation  = geometryHandle.AddressVB;
            vbv.SizeInBytes     = sizeof(ResVertex) * mesh->VertexCount();
            vbv.StrideInBytes   = sizeof(ResVertex);

            D3D12_INDEX_BUFFER_VIEW ibv = {};
            ibv.BufferLocation  = geometryHandle.AddressIB;
            ibv.SizeInBytes     = sizeof(uint32_t) * mesh->IndexCount();
            ibv.Format          = DXGI_FORMAT_R32_UINT;

            m_DrawCalls[i].IndexCount = mesh->IndexCount();
            m_DrawCalls[i].VBV        = vbv;
            m_DrawCalls[i].IBV        = ibv;
            m_DrawCalls[i].IndexVB    = geometryHandle.IndexVB;
            m_DrawCalls[i].IndexIB    = geometryHandle.IndexIB;
            m_DrawCalls[i].MaterialId = matId;

            // データバインディング用辞書を作成しておく.
            auto hashTag = resInstanceTags->Get(i);
            auto itr = m_InstanceDict.find(hashTag);
            assert(itr == m_InstanceDict.end());
            m_InstanceDict[hashTag] = i;
        }

        if (!m_TLAS.Init(pDevice, resScene->InstanceCount(), instanceDescs.data(), buildFlag))
        {
            ELOGA("Error : Tlas::Init() Failed.");
            return false;
        }

        if (!m_ScratchTLAS.Init(pDevice, m_TLAS.GetScratchBufferSize()))
        {
            ELOGA("Error : AsScratchBuffer::Init() Failed.");
            return false;
        }
        RTC_DEBUG_CODE(m_ScratchTLAS.SetName(L"ScratchTLAS"));

        m_TLAS.Build(pCmdList, m_ScratchTLAS.GetGpuAddress());
    }

    // ライトバッファ構築.
    {
        auto count  = resScene->LightCount();
        auto stride = uint32_t(sizeof(ResLight));

        auto srcLights    = resScene->Lights();
        auto srcLightTags = resScene->LightTags();

        // ライトがあれば初期化.
        if (count > 0)
        {
            if (!m_LB.Init(pCmdList, count, stride, srcLights->data()))
            {
                ELOGA("Error : LB::Init() Failed.");
                return false;
            }
        }

        // データバインディング用辞書を作成しておく.
        for(auto i=0u; i<count; ++i)
        {
            auto hashTag = srcLightTags->Get(i);
            auto itr = m_LightDict.find(hashTag);
            assert(itr == m_LightDict.end());
            m_LightDict[hashTag] = i;
        }
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
    m_LB      .Term();

    m_DrawCalls.clear();
    m_Instances.clear();

    if (m_pBinary != nullptr)
    {
        free(m_pBinary);
        m_pBinary = nullptr;
    }

    m_LightDict   .clear();
    m_InstanceDict.clear();
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
//      ライトバッファのシェーダリソースビューを取得します.
//-----------------------------------------------------------------------------
asdx::IShaderResourceView* Scene::GetLB() const
{ return m_LB.GetView(); }

//-----------------------------------------------------------------------------
//      IBLのシェーダリソースビューを取得します.
//-----------------------------------------------------------------------------
asdx::IShaderResourceView* Scene::GetIBL() const
{ return m_IBL.GetView(); }

//-----------------------------------------------------------------------------
//      TLASを取得します.
//-----------------------------------------------------------------------------
ID3D12Resource* Scene::GetTLAS() const
{ return m_TLAS.GetResource(); }

//-----------------------------------------------------------------------------
//      描画処理を行います.
//-----------------------------------------------------------------------------
void Scene::Draw(ID3D12GraphicsCommandList4* pCmdList)
{
    auto count = m_Instances.size();
    for(auto i=0u; i<count; ++i)
    {
        auto& instance = m_Instances[i];
        pCmdList->SetGraphicsRoot32BitConstant(1, instance.InstanceId, 0);

        auto& dc = m_DrawCalls[i];
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

//-----------------------------------------------------------------------------
//      ライト数を取得します.
//-----------------------------------------------------------------------------
uint32_t Scene::GetLightCount() const
{
    if (m_pBinary == nullptr)
    { return 0; }

    // シーンリソース取得.
    auto resScene = GetResScene(m_pBinary);
    assert(resScene != nullptr);

    return resScene->LightCount();
}

//-----------------------------------------------------------------------------
//      ハッシュタグに対応するライトインデックスを検索します.
//-----------------------------------------------------------------------------
uint32_t Scene::FindLightIndex(uint32_t hashTag) const
{
    auto itr = m_LightDict.find(hashTag);
    if (itr == m_LightDict.end())
    { return UINT32_MAX; }

    return itr->second;
}

//-----------------------------------------------------------------------------
//      ハッシュタグに対応するインスタンスインデックスを検索します.
//-----------------------------------------------------------------------------
uint32_t Scene::FindInstanceIndex(uint32_t hashTag) const
{
    auto itr = m_InstanceDict.find(hashTag);
    if (itr == m_InstanceDict.end())
    { return UINT32_MAX; }

    return itr->second;
}


#if !CAMP_RELEASE
//-----------------------------------------------------------------------------
//      リロード処理を行います.
//-----------------------------------------------------------------------------
void Scene::Reload(const char* path)
{
    std::string findPath;
    if (asdx::SearchFilePathA(path, findPath))
    {
        m_RequestTerm = true;
        m_WaitCount   = 0;
        m_ReloadPath  = findPath;
    }
}

//-----------------------------------------------------------------------------
//      リロード中かどうか?
//-----------------------------------------------------------------------------
bool Scene::IsReloading() const
{ return m_RequestTerm; }

//-----------------------------------------------------------------------------
//      巡回処理.
//-----------------------------------------------------------------------------
void Scene::Polling(ID3D12GraphicsCommandList4* pCmdList)
{
    if (!m_RequestTerm) 
        return;

    if (m_WaitCount == 4)
    {
        Term();
    }
    else if (m_WaitCount == 8) 
    {
        Init(m_ReloadPath.c_str(), pCmdList);
        m_RequestTerm = false;
        m_WaitCount   = 0;
    }

    m_WaitCount++;
}
#endif


#if !CAMP_RELEASE

struct ImTextureSurfaceMemory
{
    std::vector<uint8_t>     Pixels;
};

struct ImTextureMemory
{
    asdx::ResTexture                                    SrcTexture;
    std::vector<flatbuffers::Offset<r3d::SubResource>>  SubResources;
    std::vector<ImTextureSurfaceMemory>                 Surfaces;

    void Dispose()
    {
        SrcTexture.Dispose();
        for(size_t i=0; i<Surfaces.size(); ++i)
        { Surfaces[i].Pixels.clear(); }
        Surfaces.clear();
    }
};

///////////////////////////////////////////////////////////////////////////////
// SceneExporter class
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      テキストファイルからシーンを読み込み，出力します.
//-----------------------------------------------------------------------------
bool SceneExporter::LoadFromTXT(const char* path, std::string& exportPath)
{
    std::string inputPath;
    if (!asdx::SearchFilePathA(path, inputPath))
    {
        ELOGA("Error : File Not Found. path = %s", path);
        return false;
    }

    std::ifstream stream;
    stream.open(inputPath.c_str(), std::ios::in);

    if (!stream.is_open())
    {
        ELOGA("Error : File Open Failed. path = %s", inputPath.c_str());
        return false;
    }

    const uint32_t BUFFER_SIZE =4096;
    char buf[BUFFER_SIZE] = {};

    std::vector<r3d::CpuInstance> instances;
    std::map<std::string, uint32_t>  meshDic;
    std::map<std::string, uint32_t>  materialDic;
    std::map<std::string, uint32_t>  textureDic;

    uint32_t meshIndex     = 0;
    uint32_t materialIndex = 0;
    uint32_t textureIndex  = 0;

    for(;;)
    {
        // バッファに格納.
        stream >> buf;

        if (!stream || stream.eof())
        { break; }

        if (0 == strcmp(buf, "#") || 0 == strcmp(buf, "//"))
        { /* DO_NOTHING */ }
        else if (0 == _stricmp(buf, "model"))
        {
            std::string tag;
            std::string path;

            for(;;)
            {
                stream >> buf;
                if (!stream || stream.eof())
                { break; }

                if (0 == strcmp(buf, "};"))
                { break; }
                else if (0 == strcmp(buf, "#") || 0 == strcmp(buf, "//"))
                { /* DO_NOTHING */ }
                else if (0 == _stricmp(buf, "-Tag:"))
                { stream >> tag; }
                else if (0 == _stricmp(buf, "-Path:"))
                { stream >> path; }

                stream.ignore(BUFFER_SIZE, '\n');
            }

            assert(tag.empty() == false);
            assert(path.empty() == false);
            {
                std::string findPath;
                if (asdx::SearchFilePathA(path.c_str(), findPath))
                {
                    std::vector<r3d::MeshInfo> meshInfos;
                    std::vector<r3d::Mesh>     meshes;

                    if (LoadMesh(findPath.c_str(), meshes, meshInfos))
                    {
                        for(size_t i=0; i<meshInfos.size(); ++i)
                        {
                            if (meshDic.find(meshInfos[i].MeshName) == meshDic.end())
                            {
                                meshDic[meshInfos[i].MeshName] = meshIndex;
                                meshIndex++;
                            }
                        }

                        AddMeshes(meshes);
                    }
                }
            }
        }
        else if (0 == _stricmp(buf, "material"))
        {
            std::string tag;
            asdx::Vector4 baseColor = asdx::Vector4(0.0f, 0.0f, 0.0f, 1.0f);
            float occlusion = 0.0f;
            float roughness = 1.0f;
            float metalness = 0.0f;
            asdx::Vector4 emissive = asdx::Vector4(0.0f, 0.0f, 0.0f, 0.0f);
            float ior = 0.0f;
            std::string texBaseColor;
            std::string texNormal;
            std::string texOrm;
            std::string texEmissive;

            for(;;)
            {
                stream >> buf;
                if (!stream || stream.eof())
                { break; }

                if (0 == strcmp(buf, "};"))
                { break; }
                else if (0 == strcmp(buf, "#") || 0 == strcmp(buf, "//"))
                { /* DO_NOTHING */ }
                else if (0 == _stricmp(buf, "-Tag:"))
                { stream >> tag; }
                else if (0 == _stricmp(buf, "-BaseColor:"))
                { stream >> baseColor.x >> baseColor.y >> baseColor.z >> baseColor.w; }
                else if (0 == _stricmp(buf, "-Occlusion:"))
                { stream >> occlusion; }
                else if (0 == _stricmp(buf, "-Roughness:"))
                { stream >> roughness; }
                else if (0 == _stricmp(buf, "-Metalness:"))
                { stream >> metalness; }
                else if (0 == _stricmp(buf, "-Ior:"))
                { stream >> ior; }
                else if (0 == _stricmp(buf, "-Emissive:"))
                { stream >> emissive.x >> emissive.y >> emissive.z >> emissive.w; }
                else if (0 == _stricmp(buf, "-BaseColorMap:"))
                { stream >> texBaseColor; }
                else if (0 == _stricmp(buf, "-NormalMap:"))
                { stream >> texNormal; }
                else if (0 == _stricmp(buf, "-OrmMap:"))
                { stream >> texOrm; }
                else if (0 == _stricmp(buf, "-EmissiveMap:"))
                { stream >> texEmissive; }

                stream.ignore(BUFFER_SIZE, '\n');
            }

            assert(tag.empty() == false);

            if (materialDic.find(tag) == materialDic.end())
            {
                Material material  = material.Default();
                material.BaseColor = baseColor;
                material.Occlusion = occlusion;
                material.Roughness = roughness;
                material.Metalness = metalness;
                material.Ior       = ior;
                material.Emissive  = emissive;

                uint32_t baseColorMapId = INVALID_MATERIAL_MAP;
                uint32_t normalMapId    = INVALID_MATERIAL_MAP;
                uint32_t ormMapId       = INVALID_MATERIAL_MAP;
                uint32_t emissiveMapId  = INVALID_MATERIAL_MAP;

                auto getOrRegisterTextureId = [&](std::string& path, uint32_t& retId)
                {
                    if (textureDic.find(path) != textureDic.end()) {
                        retId = textureDic[path];
                    }
                    else {
                        retId = textureIndex;
                        textureIndex++;
                        textureDic[path] = retId;
                    }
                };

                if (!texBaseColor.empty())
                { getOrRegisterTextureId(texBaseColor, baseColorMapId); }
                if (!texNormal.empty())
                { getOrRegisterTextureId(texNormal, normalMapId); }
                if (!texOrm.empty())
                { getOrRegisterTextureId(texOrm, ormMapId); }
                if (!texEmissive.empty())
                { getOrRegisterTextureId(texEmissive, emissiveMapId); }

                // テクスチャIDを設定.
                material.BaseColorMap = baseColorMapId;
                material.NormalMap    = normalMapId;
                material.OrmMap       = ormMapId;
                material.EmissiveMap  = emissiveMapId;

                AddMaterial(material);

                materialDic[tag] = materialIndex;
                materialIndex++;
            }
        }
        else if (0 == _stricmp(buf, "instance"))
        {
            std::string   instanceTag;
            std::string   meshTag;
            std::string   materialTag;
            asdx::Vector3 scale         = asdx::Vector3(1.0f, 1.0f, 1.0f);
            asdx::Vector3 rotate        = asdx::Vector3(0.0f, 0.0f, 0.0f);
            asdx::Vector3 translation   = asdx::Vector3(0.0f, 0.0f, 0.0f);

            for(;;)
            {
                stream >> buf;
                if (!stream || stream.eof())
                { break; }

                if (0 == strcmp(buf, "};"))
                { break; }
                else if (0 == strcmp(buf, "#") || 0 == strcmp(buf, "//"))
                { /* DO_NOTHING */ }
                else if (0 == _stricmp(buf, "-Tag:"))
                { stream >> instanceTag; }
                else if (0 == _stricmp(buf, "-Mesh:"))
                { stream >> meshTag; }
                else if (0 == _stricmp(buf, "-Material:"))
                { stream >> materialTag; }
                else if (0 == _stricmp(buf, "-Scale:"))
                { stream >> scale.x >> scale.y >> scale.z; }
                else if (0 == _stricmp(buf, "-Rotation:"))
                { stream >> rotate.x >> rotate.y >> rotate.z; }
                else if (0 == _stricmp(buf, "-Translation:"))
                { stream >> translation.x >> translation.y >> translation.z; }

                stream.ignore(BUFFER_SIZE, '\n');
            }

            if (instanceTag.empty() || instanceTag == "")
            {
                instanceTag = "r3d::Instance";
                instanceTag += std::to_string(m_Instances.size());
            }

            assert(meshTag.empty() == false);
            assert(materialTag.empty() == false);

            bool findMesh = meshDic.find(meshTag) != meshDic.end();
            bool findMat  = materialDic.find(materialTag) != materialDic.end();

            if (findMesh && findMat)
            {
                asdx::Matrix matrix = asdx::Matrix::CreateScale(scale)
                    * asdx::Matrix::CreateRotationY(asdx::ToRadian(rotate.y))
                    * asdx::Matrix::CreateRotationZ(asdx::ToRadian(rotate.z))
                    * asdx::Matrix::CreateRotationX(asdx::ToRadian(rotate.x))
                    * asdx::Matrix::CreateTranslation(translation);

                r3d::CpuInstance instance;
                instance.HashTag    = CalcHashTag(instanceTag);
                instance.MaterialId = materialDic[materialTag];
                instance.MeshId     = meshDic[meshTag];
                instance.Transform  = asdx::FromMatrix(matrix);

                AddInstance(instance);
            }
            else
            {
                ELOGA("Error : Instance(MeshTag = %s, MaterialTag = %s) is Not Registered. findMesh = %s, findMat = %s", meshTag.c_str(), materialTag.c_str(),
                    findMesh ? "true" : "false",
                    findMat ? "true" : "false"); 
                assert(false);
            }
        }
        else if (0 == _stricmp(buf, "ibl"))
        {
            std::string path;

            for(;;)
            {
                stream >> buf;
                if (!stream || stream.eof())
                { break; }

                if (0 == strcmp(buf, "};"))
                { break; }
                else if (0 == strcmp(buf, "#") || 0 == strcmp(buf, "//"))
                { /* DO_NOTHING */ }
                else if (0 == _stricmp(buf, "-Path:"))
                { stream >> path; }

                stream.ignore(BUFFER_SIZE, '\n');
            }

            assert(path.empty() == false);
            {
                std::string findPath;
                if (asdx::SearchFilePathA(path.c_str(), findPath))
                { SetIBL(path.c_str()); }
            }
        }
        else if (0 == _stricmp(buf, "directional_light"))
        {
            asdx::Vector3 direction = asdx::Vector3(0.0f, -1.0f, 0.0f);
            asdx::Vector3 intensity = asdx::Vector3(1.0f, 1.0f, 1.0f);
            std::string   tag;

            for(;;)
            {
                stream >> buf;
                if (!stream || stream.eof())
                { break; }

                if (0 == strcmp(buf, "};"))
                { break; }
                else if (0 == strcmp(buf, "#") || 0 == strcmp(buf, "//"))
                { /* DO_NOTHING */ }
                else if (0 == _stricmp(buf, "-Tag:"))
                { stream >> tag; }
                else if (0 == _stricmp(buf, "-Direction:"))
                { stream >> direction.x >> direction.y >> direction.z; }
                else if (0 == _stricmp(buf, "-Intensity:"))
                { stream >> intensity.x >> intensity.y >> intensity.z; }

                stream.ignore(BUFFER_SIZE, '\n');
            }

            if (tag.empty() || tag =="")
            {
                tag = "r3d::DirectionalLight";
                tag += std::to_string(m_Lights.size());
            }

            Light light;
            light.HashTag   = CalcHashTag(tag);
            light.Type      = LIGHT_TYPE_DIRECTIONAL;
            light.Position  = -direction;
            light.Intensity = intensity;
            light.Radius    = 1.0f;

            AddLight(light);
        }
        else if (0 == _stricmp(buf, "point_light"))
        {
            asdx::Vector3 position  = asdx::Vector3(0.0f, 0.0f, 0.0f);
            float         radius    = 1.0f;
            asdx::Vector3 intensity = asdx::Vector3(0.0f, 0.0f, 0.0f);
            std::string   tag;

            for(;;)
            {
                stream >> buf;
                if (!stream || stream.eof())
                { break; }

                if (0 == strcmp(buf, "};"))
                { break; }
                else if (0 == strcmp(buf, "#") || 0 == strcmp(buf, "//"))
                { /* DO_NOTHING */ }
                else if (0 == _stricmp(buf, "-Tag:"))
                { stream >> tag; }
                else if (0 == _stricmp(buf, "-Position:"))
                { stream >> position.x >> position.y >> position.z; }
                else if (0 == _stricmp(buf, "-Radius:"))
                { stream >> radius; }
                else if (0 == _stricmp(buf, "-Intensity:"))
                { stream >> intensity.x >> intensity.y >> intensity.z; }

                stream.ignore(BUFFER_SIZE, '\n');
            }

            if (tag.empty() || tag == "")
            {
                tag = "r3d::PointLight";
                tag += std::to_string(m_Lights.size());
            }

            Light light;
            light.HashTag   = CalcHashTag(tag);
            light.Type      = LIGHT_TYPE_POINT;
            light.Position  = position;
            light.Radius    = radius;
            light.Intensity = intensity;

            AddLight(light);
        }
        else if (0 == _stricmp(buf, "spot_light"))
        {
            assert(false); // Not Implementation Yet.
        }
        else if (0 == _stricmp(buf, "export"))
        {
            for(;;)
            {
                stream >> buf;
                if (!stream || stream.eof())
                { break; }

                if (0 == strcmp(buf, "};"))
                { break; }
                else if (0 == strcmp(buf, "#") || 0 == strcmp(buf, "//"))
                { /* DO_NOTHING */ }
                else if (0 == _stricmp(buf, "-Path:"))
                { stream >> exportPath; }

                stream.ignore(BUFFER_SIZE, '\n');
            }
        }

        stream.ignore(BUFFER_SIZE, '\n');
    }
    stream.close();

    // エクスポート名が無ければタイムスタンプを付ける
    if (exportPath.empty())
    {
        tm local_time = {};
        auto t   = time(nullptr);
        auto err = localtime_s( &local_time, &t );

        char timeStamp[256] = {};
        sprintf_s(timeStamp, "%04d%02d%02d_%02d%02d%02d",
            local_time.tm_year + 1900,
            local_time.tm_mon + 1,
            local_time.tm_mday,
            local_time.tm_hour,
            local_time.tm_min,
            local_time.tm_sec);

        exportPath = "../res/scene/scene_";
        exportPath += timeStamp;
        exportPath += ".scn";
    }

    if (!Export(exportPath.c_str()))
    {
        ELOG("Error : Scene Export Failed.");
        return false;
    }

    ILOGA("Info : Scene Exported!! path = %s", exportPath.c_str());
    return true;
}

//-----------------------------------------------------------------------------
//      ファイルに出力します.
//-----------------------------------------------------------------------------
bool SceneExporter::Export(const char* path)
{
    std::vector<flatbuffers::Offset<r3d::ResMesh>>      dstMeshes;
    std::vector<flatbuffers::Offset<r3d::ResTexture>>   dstTextures;
    std::vector<r3d::ResMaterial>                       dstMaterials;
    std::vector<r3d::ResLight>                          dstLights;
    std::vector<r3d::ResInstance>                       dstInstances;
    flatbuffers::Offset<r3d::ResTexture>                dstIBL;
    std::vector<uint32_t>                               instanceTags;
    std::vector<uint32_t>                               lightTags;

    ImTextureMemory srcIBL;
    std::vector<ImTextureMemory> srcTextures;

    flatbuffers::FlatBufferBuilder  builder(2048);

    // 破棄処理.
    auto dispose = [&]() {
        srcIBL.Dispose();
        for(size_t i=0; i<srcTextures.size(); ++i)
        { srcTextures[i].Dispose(); }
    };

    // IBLテクスチャ読み込み.
    {
        std::string texPath;
        if (!asdx::SearchFilePathA(m_IBL.c_str(), texPath))
        {
            ELOGA("Error : File Not Found. path = %s", m_IBL.c_str());
            return false;
        }

        if (!srcIBL.SrcTexture.LoadFromFileA(texPath.c_str()))
        {
            ELOGA("Error : IBL Load Failed. path = %s", texPath.c_str());
            return false;
        }

        auto count = srcIBL.SrcTexture.SurfaceCount * srcIBL.SrcTexture.MipMapCount;

        srcIBL.Surfaces.resize(count);

        for(auto i=0u; i<count; ++i)
        {
            srcIBL.Surfaces[i].Pixels.resize(srcIBL.SrcTexture.pResources[i].SlicePitch);

            // 順番補正.
            if (srcIBL.SrcTexture.Format == 2/*DXGI_FORMAT_R32G32B32A32_FLOAT*/)
            {
                auto count = srcIBL.SrcTexture.pResources[i].SlicePitch / sizeof(float);
                auto pFloatPixels = reinterpret_cast<float*>(srcIBL.SrcTexture.pResources[i].pPixels);

                for(auto px=0; px<count; px+=4)
                {
                    auto A = pFloatPixels[px + 0];
                    auto R = pFloatPixels[px + 1];
                    auto G = pFloatPixels[px + 2];
                    auto B = pFloatPixels[px + 3];

                    pFloatPixels[px + 0] = R;
                    pFloatPixels[px + 1] = G;
                    pFloatPixels[px + 2] = B;
                    pFloatPixels[px + 3] = A;
                }
            }

            memcpy(
                srcIBL.Surfaces[i].Pixels.data(),
                srcIBL.SrcTexture.pResources[i].pPixels,
                srcIBL.SrcTexture.pResources[i].SlicePitch);

            auto item = r3d::CreateSubResourceDirect(
                builder,
                srcIBL.SrcTexture.pResources[i].Width,
                srcIBL.SrcTexture.pResources[i].Height,
                srcIBL.SrcTexture.pResources[i].MipIndex,
                srcIBL.SrcTexture.pResources[i].Pitch,
                srcIBL.SrcTexture.pResources[i].SlicePitch,
                &srcIBL.Surfaces[i].Pixels);

            srcIBL.SubResources.push_back(item);
        }

        dstIBL = r3d::CreateResTextureDirect(
            builder,
            srcIBL.SrcTexture.Dimension,
            srcIBL.SrcTexture.Width,
            srcIBL.SrcTexture.Height,
            srcIBL.SrcTexture.Depth,
            srcIBL.SrcTexture.Format,
            srcIBL.SrcTexture.MipMapCount,
            srcIBL.SrcTexture.SurfaceCount,
            0,
            &srcIBL.SubResources);
    }

    // マテリアル用テクスチャ読み込み.
    {
        srcTextures.resize(m_Textures.size());

        for(size_t i=0; i<m_Textures.size(); ++i)
        {
            std::string texPath;
            if (!asdx::SearchFilePathA(m_Textures[i].c_str(), texPath))
            {
                ELOGA("Error : File Not Found. path = %s", m_Textures[i].c_str());
                dispose();
                return false;
            }

            if (!srcTextures[i].SrcTexture.LoadFromFileA(texPath.c_str()))
            {
                ELOGA("Error : Texture Load Failed. path = %s", texPath.c_str());
                dispose();
                return false;
            }

            auto count = srcTextures[i].SrcTexture.SurfaceCount * srcTextures[i].SrcTexture.MipMapCount;

            srcTextures[i].Surfaces.resize(count);

            for(auto j=0u; j<count; ++j)
            {
                srcTextures[i].Surfaces[j].Pixels.resize(srcTextures[i].SrcTexture.pResources[j].SlicePitch);
                memcpy(
                    srcTextures[i].Surfaces[j].Pixels.data(),
                    srcTextures[i].SrcTexture.pResources[j].pPixels,
                    srcTextures[i].SrcTexture.pResources[j].SlicePitch);

                auto item = r3d::CreateSubResourceDirect(
                    builder,
                    srcTextures[i].SrcTexture.pResources[j].Width,
                    srcTextures[i].SrcTexture.pResources[j].Height,
                    srcTextures[i].SrcTexture.pResources[j].MipIndex,
                    srcTextures[i].SrcTexture.pResources[j].Pitch,
                    srcTextures[i].SrcTexture.pResources[j].SlicePitch,
                    &srcTextures[i].Surfaces[j].Pixels);

                srcTextures[i].SubResources.push_back(item);
            }

            dstTextures.push_back(
                r3d::CreateResTextureDirect(
                    builder,
                    srcTextures[i].SrcTexture.Dimension,
                    srcTextures[i].SrcTexture.Width,
                    srcTextures[i].SrcTexture.Height,
                    srcTextures[i].SrcTexture.Depth,
                    srcTextures[i].SrcTexture.Format,
                    srcTextures[i].SrcTexture.MipMapCount,
                    srcTextures[i].SrcTexture.SurfaceCount,
                    0,
                    &srcTextures[i].SubResources));
        }
    }

    // メッシュ変換処理
    {
        for(size_t i=0; i<m_Meshes.size(); ++i)
        {
            auto& srcMesh = m_Meshes[i];

            std::vector<ResVertex> vertices(srcMesh.Vertices, srcMesh.Vertices + srcMesh.VertexCount);
            std::vector<uint32_t>  indices(srcMesh.Indices, srcMesh.Indices + srcMesh.IndexCount);

            dstMeshes.push_back(
                r3d::CreateResMeshDirect(
                    builder,
                    m_Meshes[i].VertexCount,
                    m_Meshes[i].IndexCount,
                    &vertices,
                    &indices));
        }
    }

    // マテリアル変換処理.
    {
        for(size_t i=0; i<m_Materials.size(); ++i)
        {
            r3d::ResMaterial item(
                m_Materials[i].BaseColorMap,
                m_Materials[i].NormalMap,
                m_Materials[i].OrmMap,
                m_Materials[i].EmissiveMap,

                ToBinaryFormat(m_Materials[i].BaseColor),
                m_Materials[i].Occlusion,
                m_Materials[i].Roughness,
                m_Materials[i].Metalness,
                m_Materials[i].Ior,
                ToBinaryFormat(m_Materials[i].Emissive)
            );

            dstMaterials.push_back(item);
        }
    }

    // ライト変換処理.
    {
        for(size_t i=0; i<m_Lights.size(); ++i)
        {
            r3d::ResLight item(
                m_Lights[i].Type,
                r3d::Vector3(m_Lights[i].Intensity.x, m_Lights[i].Intensity.y, m_Lights[i].Intensity.z),
                r3d::Vector3(m_Lights[i].Position .x, m_Lights[i].Position .y, m_Lights[i].Position .z),
                m_Lights[i].Radius);

            dstLights.push_back(item);

            auto hashTag = m_Lights[i].HashTag;
            lightTags.push_back(hashTag);
        }
    }

    // インスタンス変換処理.
    {
        for(size_t i=0; i<m_Instances.size(); ++i)
        {
            auto& srcMtx = m_Instances[i].Transform;
            r3d::Matrix3x4 dstMtx(
                r3d::Vector4(srcMtx.m[0][0], srcMtx.m[0][1], srcMtx.m[0][2], srcMtx.m[0][3]),
                r3d::Vector4(srcMtx.m[1][0], srcMtx.m[1][1], srcMtx.m[1][2], srcMtx.m[1][3]),
                r3d::Vector4(srcMtx.m[2][0], srcMtx.m[2][1], srcMtx.m[2][2], srcMtx.m[2][3]));

            r3d::ResInstance item(
                m_Instances[i].MeshId,
                m_Instances[i].MaterialId,
                dstMtx);

            dstInstances.push_back(item);

            auto hashTag = m_Instances[i].HashTag;
            instanceTags.push_back(hashTag);
        }
    }

    // 出力処理.
    {
        auto meshCount      = uint32_t(dstMeshes.size());
        auto instanceCount  = uint32_t(dstInstances.size());
        auto textureCount   = uint32_t(dstTextures.size());
        auto materialCount  = uint32_t(dstMaterials.size());
        auto lightCount     = uint32_t(dstLights.size());

        auto dstScene = r3d::CreateResSceneDirect(
            builder,
            meshCount,
            instanceCount,
            textureCount,
            materialCount,
            lightCount,
            dstIBL,
            &dstMeshes,
            &dstInstances,
            &dstTextures,
            &dstMaterials,
            &dstLights,
            &instanceTags,
            &lightTags);

        builder.Finish(dstScene);

        auto buffer = builder.GetBufferPointer();
        auto size   = builder.GetSize();

        // ファイルに出力.
        FILE* fp = nullptr;
        auto err = fopen_s(&fp, path, "wb");
        if (err != 0)
        {
            ELOGA("Error : File Open Failed. path = %s", path);
            dispose();
            return false;
        }

        fwrite(buffer, size, 1, fp);
        fclose(fp);

        ILOGA("Info : Scene File Exported!! path = %s", path);
    }

    dispose();

    // 正常終了.
    return true;
}

//-----------------------------------------------------------------------------
//      リセットします.
//-----------------------------------------------------------------------------
void SceneExporter::Reset()
{
    for(size_t i=0; i<m_Meshes.size(); ++i)
    {
        if (m_Meshes[i].Vertices != nullptr)
        { delete[] m_Meshes[i].Vertices; }

        if (m_Meshes[i].Indices != nullptr)
        { delete[] m_Meshes[i].Indices; }
    }

    m_Lights   .clear();
    m_Meshes   .clear();
    m_Materials.clear();
    m_Instances.clear();
    m_Textures .clear();
}

//-----------------------------------------------------------------------------
//      ライトを追加します.
//-----------------------------------------------------------------------------
void SceneExporter::AddLight(const Light& value)
{ m_Lights.emplace_back(value); }

//-----------------------------------------------------------------------------
//      メッシュを追加します.
//-----------------------------------------------------------------------------
void SceneExporter::AddMesh(const Mesh& value)
{ m_Meshes.emplace_back(value); }

//-----------------------------------------------------------------------------
//      メッシュを追加します.
//-----------------------------------------------------------------------------
void SceneExporter::AddMeshes(const std::vector<Mesh>& values)
{ m_Meshes.insert(m_Meshes.end(), values.begin(), values.end()); }

//-----------------------------------------------------------------------------
//      マテリアルを追加します.
//-----------------------------------------------------------------------------
void SceneExporter::AddMaterial(const Material& value)
{ m_Materials.emplace_back(value); }

//-----------------------------------------------------------------------------
//      インスタンスを追加します.
//-----------------------------------------------------------------------------
void SceneExporter::AddInstance(const CpuInstance& value)
{ m_Instances.emplace_back(value); }

//-----------------------------------------------------------------------------
//      インスタンスを追加します.
//-----------------------------------------------------------------------------
void SceneExporter::AddInstances(const std::vector<CpuInstance>& values)
{ m_Instances.insert(m_Instances.end(), values.begin(), values.end()); }

//-----------------------------------------------------------------------------
//      テクスチャを追加します.
//-----------------------------------------------------------------------------
void SceneExporter::AddTexture(const char* path)
{ m_Textures.push_back(path); }

//-----------------------------------------------------------------------------
//      IBLを設定します.
//-----------------------------------------------------------------------------
void SceneExporter::SetIBL(const char* path)
{ m_IBL = path; }

//-----------------------------------------------------------------------------
//      メッシュをロードします.
//-----------------------------------------------------------------------------
bool LoadMesh(const char* path, std::vector<Mesh>& result, std::vector<MeshInfo>& infos)
{
    std::string meshPath;
    if (!asdx::SearchFilePathA(path, meshPath))
    {
        ELOGA("Error : File Not Found. path = %s", path);
        return false;
    }

    ModelOBJ  model;
    OBJLoader loader;
    if (!loader.Load(meshPath.c_str(), model))
    {
        ELOGA("Error : Model Load Failed. path = %s", meshPath.c_str());
        return false;
    }

    result.resize(model.Meshes.size());
    infos .resize(model.Meshes.size());

    for(size_t i=0; i<model.Meshes.size(); ++i)
    {
        auto& srcMesh = model.Meshes[i];
        auto& dstMesh = result[i];

        infos[i].MeshName     = srcMesh.Name;
        infos[i].MaterialName = srcMesh.MaterialName;

        auto vertexCount = uint32_t(srcMesh.Vertices.size());
        auto indexCount  = uint32_t(srcMesh.Indices.size());

        dstMesh.VertexCount = vertexCount;
        dstMesh.IndexCount  = indexCount;

        dstMesh.Vertices = new ResVertex[vertexCount];
        dstMesh.Indices  = new uint32_t [indexCount];

        for(uint32_t j=0; j<vertexCount; ++j)
        {
            auto& srcVtx = srcMesh.Vertices[j];
            dstMesh.Vertices[j] = ResVertex(
                r3d::Vector3(srcVtx.Position.x, srcVtx.Position.y, srcVtx.Position.z),
                r3d::Vector3(srcVtx.Normal  .x, srcVtx.Normal  .y, srcVtx.Normal  .z),
                r3d::Vector3(srcVtx.Tangent .x, srcVtx.Tangent .y, srcVtx.Tangent .z),
                r3d::Vector2(srcVtx.TexCoord.x, srcVtx.TexCoord.y)
            );
        }

        for(uint32_t j=0; j<indexCount; ++j)
        { dstMesh.Indices[j] = srcMesh.Indices[j]; }
    }

    return true;
}

#endif

} // namespace r3d
