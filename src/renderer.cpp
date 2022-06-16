//-----------------------------------------------------------------------------
// File : renderer.cpp
// Desc : Renderer.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <renderer.h>
#include <fnd/asdxLogger.h>
#include <fnd/asdxStopWatch.h>
#include <fnd/asdxMisc.h>
#include <process.h>
#include <fpng.h>
#include <OBJLoader.h>

#if ASDX_ENABLE_IMGUI
#include "../external/asdx12/external/imgui/imgui.h"
#endif

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 602;}
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }

namespace {

//-----------------------------------------------------------------------------
// Constant Values.
//-----------------------------------------------------------------------------
#include "../res/shader/Compile/TonemapVS.inc"
#include "../res/shader/Compile/TonemapPS.inc"
#include "../res/shader/Compile/RtCamp.inc"
#include "../res/shader/Compile/ModelVS.inc"
#include "../res/shader/Compile/ModelPS.inc"

static const D3D12_INPUT_ELEMENT_DESC kModelElements[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL"  , 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TANGENT" , 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT   , 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};

static_assert(sizeof(r3d::Vertex) == sizeof(VertexOBJ), "Vertex size not matched!");


///////////////////////////////////////////////////////////////////////////////
// Payload structure
///////////////////////////////////////////////////////////////////////////////
struct Payload
{
    asdx::Vector3   Position;
    uint32_t        MaterialId;
    asdx::Vector3   Normal;
    asdx::Vector3   Tangent;
    asdx::Vector2   TexCoord;
};

///////////////////////////////////////////////////////////////////////////////
// SceneParam structure
///////////////////////////////////////////////////////////////////////////////
struct SceneParam
{
    asdx::Matrix    View;
    asdx::Matrix    Proj;
    asdx::Matrix    InvView;
    asdx::Matrix    InvProj;
    asdx::Matrix    InvViewProj;

    uint32_t        MaxBounce;
    uint32_t        FrameIndex;
    float           SkyIntensity;
    float           Exposure;
};

//-----------------------------------------------------------------------------
//      画像に出力します.
//-----------------------------------------------------------------------------
unsigned Export(void* args)
{
    auto data = reinterpret_cast<r3d::Renderer::ExportData*>(args);
    if (data == nullptr)
    { return -1; }

    //asdx::StopWatch timer;
    //timer.Start();

    char path[256] = {};
    sprintf_s(path, "output_%03ld.png", data->FrameIndex);

    // Releaseモードで 5-28[ms]程度の揺れ.
    if (fpng::fpng_encode_image_to_memory(
        data->Pixels.data(),
        data->Width,
        data->Height,
        4,
        data->Converted,
        data->Temporary))
    {
        FILE* pFile = nullptr;
        auto err = fopen_s(&pFile, path, "wb");
        if (pFile != nullptr)
        {
            fwrite(data->Converted.data(), 1, data->Converted.size(), pFile);
            fclose(pFile);
        }
    }

    //timer.End();
    //ILOGA("timer = %lf", timer.GetElapsedMsec());

    return 0;
}

//-----------------------------------------------------------------------------
//      マテリアルIDとジオメトリIDをパッキングします.
//-----------------------------------------------------------------------------
inline uint32_t PackInstanceId(uint32_t materialId, uint32_t geometryId)
{ return ((geometryId & 0x3FFF) << 10) | (materialId & 0x3FF); }

//-----------------------------------------------------------------------------
//      マテリアルIDとジオメトリIDのパッキングを解除します.
//-----------------------------------------------------------------------------
inline void UnpackInstanceId(uint32_t instanceId, uint32_t& materialId, uint32_t& geometryId)
{
    materialId = instanceId & 0x3FF;
    geometryId = (instanceId >> 10) & 0x3FFF;
}

} // namespace


namespace r3d {

///////////////////////////////////////////////////////////////////////////////
// Renderer class
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      コンストラクタです.
//-----------------------------------------------------------------------------
Renderer::Renderer(const SceneDesc& desc)
: asdx::Application(L"r3d alpha 0.0", desc.Width, desc.Height, nullptr, nullptr, nullptr)
, m_SceneDesc(desc)
{
    m_SwapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
#if !CAMP_RELEASE
    //m_DeviceDesc.EnableCapture = true;
#endif
}

//-----------------------------------------------------------------------------
//      デストラクタです.
//-----------------------------------------------------------------------------
Renderer::~Renderer()
{
}

//-----------------------------------------------------------------------------
//      初期化処理を行います.
//-----------------------------------------------------------------------------
bool Renderer::OnInit()
{
    auto pDevice = asdx::GetD3D12Device();

    m_GfxCmdList.Reset();

    // DXRが使用可能かどうかチェック.
    if (!asdx::IsSupportDXR(pDevice))
    {
        ELOGA("Error : DirectX Ray Tracing is not supported.");
        return false;
    }

    // PNGライブラリ初期化.
    fpng::fpng_init();

    // モデルマネージャ初期化.
    if (!m_ModelMgr.Init(UINT16_MAX, UINT16_MAX))
    {
        ELOGA("Error : ModelMgr::Init() Failed.");
        return false;
    }

    // リードバックテクスチャ生成.
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Alignment          = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        desc.Width              = m_SceneDesc.Width * m_SceneDesc.Height * 4;
        desc.Height             = 1;
        desc.DepthOrArraySize   = 1;
        desc.MipLevels          = 1;
        desc.Format             = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags              = D3D12_RESOURCE_FLAG_NONE;

        D3D12_HEAP_PROPERTIES props = {};
        props.Type = D3D12_HEAP_TYPE_READBACK;

        for(auto i=0; i<3; ++i)
        {
            auto hr = pDevice->CreateCommittedResource(
                &props,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(m_ReadBackTexture[i].GetAddress()));

            if (FAILED(hr))
            {
                ELOGA("Error : ID3D12Device::CreateCommittedResource() Failed. errcode = 0x%x", hr);
                return false;
            }

            m_ExportData[i].Pixels.resize(m_SceneDesc.Width * m_SceneDesc.Height * 4);
            m_ExportData[i].FrameIndex  = 0;
            m_ExportData[i].Width       = m_SceneDesc.Width;
            m_ExportData[i].Height      = m_SceneDesc.Height;
        }

        UINT   rowCount     = 0;
        UINT64 pitchSize    = 0;
        UINT64 resSize      = 0;

        D3D12_RESOURCE_DESC dstDesc = {};
        dstDesc.Dimension           = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        dstDesc.Alignment           = 0;
        dstDesc.Width               = m_SceneDesc.Width;
        dstDesc.Height              = m_SceneDesc.Height;
        dstDesc.DepthOrArraySize    = 1;
        dstDesc.MipLevels           = 1;
        dstDesc.Format              = DXGI_FORMAT_R8G8B8A8_UNORM;
        dstDesc.SampleDesc.Count    = 1;
        dstDesc.SampleDesc.Quality  = 0;
        dstDesc.Layout              = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        dstDesc.Flags               = D3D12_RESOURCE_FLAG_NONE;

        pDevice->GetCopyableFootprints(&dstDesc,
            0,
            1,
            0,
            nullptr,
            &rowCount,
            &pitchSize,
            &resSize);

        m_ReadBackPitch = static_cast<uint32_t>((pitchSize + 255) & ~0xFFu);

        m_ReadBackIndex = 0;
    }

    // フル矩形用頂点バッファの初期化.
    if (!asdx::Quad::Instance().Init(pDevice))
    {
        ELOGA("Error : Quad::Init() Failed.");
        return false;
    }

#if ASDX_ENABLE_IMGUI
    // GUI初期化.
    {
        const auto path = "../res/font/07やさしさゴシック.ttf";
        if (!asdx::GuiMgr::Instance().Init(m_GfxCmdList, m_hWnd, m_Width, m_Height, m_SwapChainFormat, path))
        {
            ELOGA("Error : GuiMgr::Init() Failed.");
            return false;
        }
    }
#endif

    // コンピュートターゲット生成.
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_SceneDesc.Width;
        desc.Height             = m_SceneDesc.Height;
        desc.DepthOrArraySize   = 1;
        desc.Format             = DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.MipLevels          = 1;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        if (!m_Canvas.Init(&desc))
        {
            ELOGA("Error : ComputeTarget::Init() Failed.");
            return false;
        }
    }

    // レイトレ用ルートシグニチャ生成.
    {
        asdx::DescriptorSetLayout<6, 1> layout;
        layout.SetTableUAV(0, asdx::SV_ALL, 0);
        layout.SetSRV(1, asdx::SV_ALL, 0);
        layout.SetSRV(2, asdx::SV_ALL, 1);
        layout.SetSRV(3, asdx::SV_ALL, 2);
        layout.SetTableSRV(4, asdx::SV_ALL, 3);
        layout.SetCBV(5, asdx::SV_ALL, 0);
        layout.SetStaticSampler(0, asdx::SV_ALL, asdx::STATIC_SAMPLER_LINEAR_WRAP, 0);
        layout.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);

        if (!m_RayTracingRootSig.Init(pDevice, layout.GetDesc()))
        {
            ELOGA("Error : RayTracing RootSignature Failed.");
            return false;
        }
    }

    // レイトレ用パイプラインステート生成.
    {
        D3D12_EXPORT_DESC exports[] = {
            { L"OnGenerateRay"      , nullptr, D3D12_EXPORT_FLAG_NONE },
            { L"OnClosestHit"       , nullptr, D3D12_EXPORT_FLAG_NONE },
            { L"OnShadowClosestHit" , nullptr, D3D12_EXPORT_FLAG_NONE },
            { L"OnMiss"             , nullptr, D3D12_EXPORT_FLAG_NONE },
            { L"OnShadowMiss"       , nullptr, D3D12_EXPORT_FLAG_NONE },
        };

        D3D12_HIT_GROUP_DESC groups[2] = {};
        groups[0].ClosestHitShaderImport    = L"OnClosestHit";
        groups[0].HitGroupExport            = L"StandardHit";
        groups[0].Type                      = D3D12_HIT_GROUP_TYPE_TRIANGLES;

        groups[1].ClosestHitShaderImport    = L"OnShadowClosestHit";
        groups[1].HitGroupExport            = L"ShadowHit";
        groups[1].Type                      = D3D12_HIT_GROUP_TYPE_TRIANGLES;

        asdx::RayTracingPipelineStateDesc desc = {};
        desc.pGlobalRootSignature       = m_RayTracingRootSig.GetPtr();
        desc.DXILLibrary                = { RtCamp, sizeof(RtCamp) };
        desc.ExportCount                = _countof(exports);
        desc.pExports                   = exports;
        desc.HitGroupCount              = _countof(groups);
        desc.pHitGroups                 = groups;
        desc.MaxPayloadSize             = sizeof(Payload);
        desc.MaxAttributeSize           = sizeof(asdx::Vector2);
        desc.MaxTraceRecursionDepth     = 1;

        if (!m_RayTracingPSO.Init(pDevice, desc))
        {
            ELOGA("Error : RayTracing PSO Failed.");
            return false;
        }
    }

    // レイ生成テーブル.
    {
        asdx::ShaderRecord record = {};
        record.ShaderIdentifier = m_RayTracingPSO.GetShaderIdentifier(L"OnGenerateRay");

        asdx::ShaderTable::Desc desc = {};
        desc.RecordCount    = 1;
        desc.pRecords       = &record;

        if (!m_RayGenTable.Init(pDevice, &desc))
        {
            ELOGA("Error : RayGenTable Init Failed.");
            return false;
        }
    }

    // ミステーブル.
    {
        asdx::ShaderRecord record[2] = {};
        record[0].ShaderIdentifier = m_RayTracingPSO.GetShaderIdentifier(L"OnMiss");
        record[1].ShaderIdentifier = m_RayTracingPSO.GetShaderIdentifier(L"OnShadowMiss");

        asdx::ShaderTable::Desc desc = {};
        desc.RecordCount = 2;
        desc.pRecords    = record;

        if (!m_MissTable.Init(pDevice, &desc))
        {
            ELOGA("Error : MissTable Init Failed.");
            return false;
        }
    }

    // ヒットグループ.
    {
        asdx::ShaderRecord record[2];
        record[0].ShaderIdentifier = m_RayTracingPSO.GetShaderIdentifier(L"StandardHit");
        record[1].ShaderIdentifier = m_RayTracingPSO.GetShaderIdentifier(L"ShadowHit");

        asdx::ShaderTable::Desc desc = {};
        desc.RecordCount = 2;
        desc.pRecords    = record;

        if (!m_HitGroupTable.Init(pDevice, &desc))
        {
            ELOGA("Error : HitGroupTable Init Failed.");
            return false;
        }
    }

    // トーンマップ用ルートシグニチャ生成.
    {
        asdx::DescriptorSetLayout<1, 1> layout;
        layout.SetTableSRV(0, asdx::SV_PS, 0);
        layout.SetStaticSampler(0, asdx::SV_PS, asdx::STATIC_SAMPLER_LINEAR_CLAMP, 0);
        layout.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        if (!m_TonemapRootSig.Init(pDevice, layout.GetDesc()))
        {
            ELOGA("Error : Tonemap RootSignature Failed.");
            return false;
        }
    }

    // トーンマップ用パイプラインステート生成.
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = m_TonemapRootSig.GetPtr();
        desc.VS                     = { TonemapVS, sizeof(TonemapVS) };
        desc.PS                     = { TonemapPS, sizeof(TonemapPS) };
        desc.BlendState             = asdx::BLEND_DESC(asdx::BLEND_STATE_OPAQUE);
        desc.DepthStencilState      = asdx::DEPTH_STENCIL_DESC(asdx::DEPTH_STATE_NONE);
        desc.RasterizerState        = asdx::RASTERIZER_DESC(asdx::RASTERIZER_STATE_CULL_NONE);
        desc.SampleMask             = D3D12_DEFAULT_SAMPLE_MASK;
        desc.PrimitiveTopologyType  = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets       = 1;
        desc.RTVFormats[0]          = m_SwapChainFormat;
        desc.DSVFormat              = DXGI_FORMAT_UNKNOWN;
        desc.InputLayout            = asdx::Quad::kInputLayout;
        desc.SampleDesc.Count       = 1;
        desc.SampleDesc.Quality     = 0;

        if (!m_TonemapPSO.Init(pDevice, &desc))
        {
            ELOGA("Error : Tonemap PipelineState Failed.");
            return false;
        }
    }

    // カメラ初期化.
    {
        auto pos = asdx::Vector3(0.0f, 0.0f, 10.0f);
        auto target = asdx::Vector3(0.0f, 0.0f, 0.0f);
        auto upward = asdx::Vector3(0.0f, 1.0f, 0.0f);
        m_CameraController.Init(pos, target, upward, 1.0f, 1000.0f);
    }

    // 定数バッファ初期化.
    {
        auto size = asdx::RoundUp(sizeof(SceneParam), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        if (!m_SceneParam.Init(size))
        {
            ELOGA("Error : SceneParam Init Failed.");
            return false;
        }
    }

    // IBL読み込み.
    {
        std::string path;
        if (!asdx::SearchFilePathA("../res/ibl/studio_garden_2k.dds", path))
        {
            ELOGA("Error : IBL File Not Found.");
            return false;
        }

        asdx::ResTexture res;
        if (!res.LoadFromFileA(path.c_str()))
        {
            ELOGA("Error : IBL Load Failed.");
            return false;
        }

        if (!m_IBL.Init(m_GfxCmdList, res))
        {
            ELOGA("Error : IBL Init Failed.");
            return false;
        }
    }

    // アルベドバッファ.
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_SceneDesc.Width;
        desc.Height             = m_SceneDesc.Height;
        desc.DepthOrArraySize   = 1;
        desc.MipLevels          = 1;
        desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_COMMON;
        desc.ClearColor[0]      = 0.0f;
        desc.ClearColor[1]      = 0.0f;
        desc.ClearColor[2]      = 0.0f;
        desc.ClearColor[3]      = 1.0f;

        if (!m_AlbedoTarget.Init(&desc, true))
        {
            ELOGA("Error : DebugColorTarget Init Failed.");
            return false;
        }

        m_GfxCmdList.BarrierTransition(
            m_AlbedoTarget.GetResource(), 0,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    // 法線バッファ.
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_SceneDesc.Width;
        desc.Height             = m_SceneDesc.Height;
        desc.DepthOrArraySize   = 1;
        desc.MipLevels          = 1;
        desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_COMMON;
        desc.ClearColor[0]      = 0.5f;
        desc.ClearColor[1]      = 0.5f;
        desc.ClearColor[2]      = 1.0f;
        desc.ClearColor[3]      = 1.0f;

        if (!m_NormalTarget.Init(&desc, false))
        {
            ELOGA("Error : NormalTarget Init Failed.");
            return false;
        }

        m_GfxCmdList.BarrierTransition(
            m_NormalTarget.GetResource(), 0,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    // 深度ターゲット生成.
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_SceneDesc.Width;
        desc.Height             = m_SceneDesc.Height;
        desc.DepthOrArraySize   = 1;
        desc.MipLevels          = 1;
        desc.Format             = DXGI_FORMAT_D32_FLOAT;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        desc.ClearDepth         = 1.0f;
        desc.ClearStencil       = 0;

        if (!m_ModelDepthTarget.Init(&desc))
        {
            ELOGA("Error : DebugDepthTarget Init Failed.");
            return false;
        }
    }

    // G-Bufferルートシグニチャ生成.
    {
        auto flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

        asdx::DescriptorSetLayout<4, 1> layout;
        layout.SetCBV(0, asdx::SV_VS, 0);
        layout.SetContants(1, asdx::SV_ALL, 1, 1);
        layout.SetSRV(2, asdx::SV_VS, 0);
        layout.SetSRV(3, asdx::SV_PS, 1);
        layout.SetStaticSampler(0, asdx::SV_ALL, asdx::STATIC_SAMPLER_LINEAR_WRAP, 0);
        layout.SetFlags(flags);

        if (!m_ModelRootSig.Init(pDevice, layout.GetDesc()))
        {
            ELOGA("Error : RayTracing RootSignature Failed.");
            return false;
        }
    }

    // G-Bufferパイプラインステート生成.
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature                 = m_ModelRootSig.GetPtr();
        desc.VS                             = { ModelVS, sizeof(ModelVS) };
        desc.PS                             = { ModelPS, sizeof(ModelPS) };
        desc.BlendState                     = asdx::BLEND_DESC(asdx::BLEND_STATE_OPAQUE);
        desc.DepthStencilState              = asdx::DEPTH_STENCIL_DESC(asdx::DEPTH_STATE_DEFAULT);
        desc.RasterizerState                = asdx::RASTERIZER_DESC(asdx::RASTERIZER_STATE_CULL_NONE);
        desc.SampleMask                     = D3D12_DEFAULT_SAMPLE_MASK;
        desc.PrimitiveTopologyType          = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets               = 2;
        desc.RTVFormats[0]                  = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        desc.RTVFormats[1]                  = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.DSVFormat                      = DXGI_FORMAT_D32_FLOAT;
        desc.InputLayout.NumElements        = _countof(kModelElements);
        desc.InputLayout.pInputElementDescs = kModelElements;
        desc.SampleDesc.Count               = 1;
        desc.SampleDesc.Quality             = 0;

        if (!m_ModelPSO.Init(pDevice, &desc))
        {
            ELOGA("Error : PipelineState Failed.");
            return false;
        }
    }

    // Test
    {
        ModelOBJ model;
        OBJLoader loader;
        if (!loader.Load("../res/model/dosei_quad.obj", model))
        {
            ELOGA("Error : Model Load Failed.");
            return false;
        }

        Material dummy;
        dummy.Normal = 0;
        dummy.BaseColor = 0;
        dummy.ORM = 0;
        m_ModelMgr.AddMaterials(&dummy, 1);

        auto meshCount = model.Meshes.size();

        m_BLAS.resize(meshCount);

        std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
        instanceDescs.resize(meshCount);

        m_MeshDrawCalls.resize(meshCount);
 
        for(size_t i=0; i<meshCount; ++i)
        {
            r3d::Mesh mesh;
            mesh.VertexCount = uint32_t(model.Meshes[i].Vertices.size());
            mesh.Vertices    = reinterpret_cast<Vertex*>(model.Meshes[i].Vertices.data());
            mesh.IndexCount  = uint32_t(model.Meshes[i].Indices.size());
            mesh.Indices     = model.Meshes[i].Indices.data();

            auto geometryHandle = m_ModelMgr.AddMesh(mesh);

            r3d::Instance instance;
            instance.VertexBufferId = geometryHandle.IndexVB;
            instance.IndexBufferId  = geometryHandle.IndexIB;
            instance.MaterialId     = 0;

            // 単位行列.
            asdx::Transform3x4 transform;
            auto instanceHandle = m_ModelMgr.AddInstance(instance, transform);

            D3D12_RAYTRACING_GEOMETRY_DESC desc = {};
            desc.Type                                   = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            desc.Flags                                  = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
            desc.Triangles.Transform3x4                 = instanceHandle.AddressTB;
            desc.Triangles.IndexFormat                  = DXGI_FORMAT_R32_UINT;
            desc.Triangles.IndexCount                   = mesh.IndexCount;
            desc.Triangles.IndexBuffer                  = geometryHandle.AddressIB;
            desc.Triangles.VertexFormat                 = DXGI_FORMAT_R32G32B32_FLOAT;
            desc.Triangles.VertexBuffer.StartAddress    = geometryHandle.AddressVB;
            desc.Triangles.VertexBuffer.StrideInBytes   = sizeof(Vertex);
            desc.Triangles.VertexCount                  = mesh.VertexCount;

            if (!m_BLAS[i].Init(
                pDevice,
                1,
                &desc, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE))
            {
                ELOGA("Error : Blas::Init() Failed.");
                return false;
            }

            // ビルドコマンドを積んでおく.
            m_BLAS[i].Build(m_GfxCmdList.GetCommandList());

            memcpy(instanceDescs[i].Transform, transform.m, sizeof(float) * 12);
            instanceDescs[i].InstanceID                             = instanceHandle.InstanceId;
            instanceDescs[i].InstanceMask                           = 0xFF;
            instanceDescs[i].InstanceContributionToHitGroupIndex    = 0;
            instanceDescs[i].Flags                                  = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
            instanceDescs[i].AccelerationStructure                  = m_BLAS[i].GetResource()->GetGPUVirtualAddress();

            D3D12_VERTEX_BUFFER_VIEW vbv = {};
            vbv.BufferLocation = geometryHandle.AddressVB;
            vbv.SizeInBytes    = sizeof(Vertex) * mesh.VertexCount;
            vbv.StrideInBytes  = sizeof(Vertex);

            D3D12_INDEX_BUFFER_VIEW ibv = {};
            ibv.BufferLocation = geometryHandle.AddressIB;
            ibv.SizeInBytes    = sizeof(uint32_t) * mesh.IndexCount;
            ibv.Format         = DXGI_FORMAT_R32_UINT;

            m_MeshDrawCalls[i].StartIndex = 0;
            m_MeshDrawCalls[i].IndexCount = mesh.IndexCount;
            m_MeshDrawCalls[i].BaseVertex = 0;
            m_MeshDrawCalls[i].InstanceId = instanceHandle.InstanceId;
            m_MeshDrawCalls[i].VBV        = vbv;
            m_MeshDrawCalls[i].IBV        = ibv;
        }

        auto instanceCount = uint32_t(instanceDescs.size());
        if (!m_TLAS.Init(
            pDevice,
            instanceCount,
            instanceDescs.data(),
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE))
        {
            ELOGA("Error : Tlas::Init() Failed.");
            return false;
        }

        // ビルドコマンドを積んでおく.
        m_TLAS.Build(m_GfxCmdList.GetCommandList());
    }

    // 高速化機構生成.
    {
    }

    // セットアップコマンド実行.
    {
        // コマンド記録終了.
        m_GfxCmdList.Close();

        ID3D12CommandList* pCmds[] = {
            m_GfxCmdList.GetCommandList()
        };

        auto pGraphicsQueue = asdx::GetGraphicsQueue();

        // コマンドを実行.
        pGraphicsQueue->Execute(_countof(pCmds), pCmds);

        // 待機点を発行.
        m_FrameWaitPoint = pGraphicsQueue->Signal();

        // 完了を待機.
        pGraphicsQueue->Sync(m_FrameWaitPoint);
    }
 
    return true;
}

//-----------------------------------------------------------------------------
//      終了処理を行います.
//-----------------------------------------------------------------------------
void Renderer::OnTerm()
{
#if ASDX_ENABLE_IMGUI
    asdx::GuiMgr::Instance().Term();
#endif

    asdx::Quad::Instance().Term();

    m_TonemapPSO        .Term();
    m_TonemapRootSig    .Term();
    m_RayTracingPSO     .Term();
    m_RayTracingRootSig .Term();

    for(size_t i=0; i<m_BLAS.size(); ++i)
    { m_BLAS[i].Term(); }
    m_BLAS.clear();

    m_TLAS  .Term();
    m_Canvas.Term();

    m_SceneParam.Term();
    m_IBL       .Term();

    m_RayGenTable   .Term();
    m_MissTable     .Term();
    m_HitGroupTable .Term();

    for(auto i=0; i<3; ++i)
    {
        m_ReadBackTexture[i].Reset();
        m_ExportData[i].Pixels.clear();
    }

    m_AlbedoTarget    .Term();
    m_NormalTarget    .Term();
    m_ModelDepthTarget.Term();
    m_ModelRootSig    .Term();
    m_ModelPSO        .Term();
    //m_MeshDrawCalls.clear();

    m_ModelMgr.Term();
}

//-----------------------------------------------------------------------------
//      フレーム遷移時の処理です.
//-----------------------------------------------------------------------------
void Renderer::OnFrameMove(asdx::FrameEventArgs& args)
{
#if (CAMP_RELEASE)
    // 制限時間を超えた
    if (args.Time >= m_SceneDesc.TimeSec)
    {
        PostQuitMessage(0);
        return;
    }

    // CPUで読み取り.
    if (GetFrameCount() > 2)
    {
        uint8_t* ptr = nullptr;
        auto idx = m_MapIndex;
        auto hr  = m_ReadBackTexture[idx]->Map(0, nullptr, reinterpret_cast<void**>(&ptr));
        if (SUCCEEDED(hr))
        {
            memcpy(m_ExportData[idx].Pixels.data(), ptr, m_ExportData[idx].Pixels.size());
            m_ExportData[idx].FrameIndex = m_CaptureIndex;
        }
        m_ReadBackTexture[idx]->Unmap(0, nullptr);
        m_CaptureIndex++;

        // 画像に出力.
        _beginthreadex(nullptr, 0, Export, &m_ExportData[idx], 0, nullptr);
    }
#endif
}

//-----------------------------------------------------------------------------
//      フレーム描画時の処理です.
//-----------------------------------------------------------------------------
void Renderer::OnFrameRender(asdx::FrameEventArgs& args)
{
    auto idx = GetCurrentBackBufferIndex();

    // コマンド記録開始.
    m_GfxCmdList.Reset();

    // 定数バッファ更新.
    {
        auto fovY   = asdx::ToRadian(37.5f);
        auto aspect = float(m_SceneDesc.Width) / float(m_SceneDesc.Height);

        auto view = m_CameraController.GetView();

        SceneParam param = {};
        param.View = view;
        param.Proj = asdx::Matrix::CreatePerspectiveFieldOfView(
            fovY,
            aspect,
            m_CameraController.GetNearClip(),
            m_CameraController.GetFarClip());
        param.InvView = asdx::Matrix::Invert(param.View);
        param.InvProj = asdx::Matrix::Invert(param.Proj);
        param.InvViewProj = param.InvProj * param.InvView;

        param.MaxBounce     = 8;
        param.FrameIndex    = GetFrameCount();
        param.SkyIntensity  = 1.0f;
        param.Exposure      = 1.0f;

        m_SceneParam.SwapBuffer();
        m_SceneParam.Update(&param, sizeof(param));
    }

    // デノイズ用 G-Buffer.
    {
        asdx::ScopedBarrier barrier0(m_GfxCmdList, m_AlbedoTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        asdx::ScopedBarrier barrier1(m_GfxCmdList, m_NormalTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

        auto pRTV0 = m_AlbedoTarget.GetRTV();
        auto pRTV1 = m_NormalTarget.GetRTV();
        auto pDSV  = m_ModelDepthTarget.GetDSV();

        float clearColor [4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        float clearNormal[4] = { 0.5f, 0.5f, 1.0f, 1.0f };

        m_GfxCmdList.ClearRTV(pRTV0, clearColor);
        m_GfxCmdList.ClearRTV(pRTV1, clearNormal);
        m_GfxCmdList.ClearDSV(pDSV, 1.0f);

        D3D12_CPU_DESCRIPTOR_HANDLE handleRTVs[] = {
            pRTV0->GetHandleCPU(),
            pRTV1->GetHandleCPU(),
        };

        auto handleDSV = pDSV->GetHandleCPU();

        m_GfxCmdList.GetCommandList()->OMSetRenderTargets(
            2, 
            handleRTVs,
            FALSE,
            &handleDSV);

        m_GfxCmdList.SetViewport(m_AlbedoTarget.GetResource());
        m_GfxCmdList.SetRootSignature(m_ModelRootSig.GetPtr(), false);
        m_GfxCmdList.SetPipelineState(m_ModelPSO.GetPtr());

        m_GfxCmdList.SetCBV(0, m_SceneParam.GetResource());
        m_GfxCmdList.SetSRV(2, m_ModelMgr.GetTB());
        m_GfxCmdList.SetSRV(3, m_ModelMgr.GetMB());
        m_GfxCmdList.SetPrimitiveToplogy(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        auto count = m_MeshDrawCalls.size();
        for(size_t i=0; i<count; ++i)
        {
            auto& info = m_MeshDrawCalls[i];

            m_GfxCmdList.SetVertexBuffers(0, 1, &info.VBV);
            m_GfxCmdList.SetIndexBuffer(&info.IBV);

            m_GfxCmdList.SetConstants(1, 1, &info.InstanceId, 0);
            m_GfxCmdList.DrawIndexedInstanced(
                info.IndexCount,
                1,
                info.StartIndex,
                info.BaseVertex,
                0);
        }
    }

    // レイトレ実行.
    {
        m_GfxCmdList.SetStateObject(m_RayTracingPSO.GetStateObject());
        m_GfxCmdList.SetRootSignature(m_RayTracingRootSig.GetPtr(), true);
        m_GfxCmdList.SetTable(0, m_Canvas.GetUAV(), true);
        m_GfxCmdList.SetSRV(1, m_TLAS.GetResource(), true);
        m_GfxCmdList.SetSRV(2, m_ModelMgr.GetIB(), true);
        m_GfxCmdList.SetSRV(3, m_ModelMgr.GetMB(), true);
        m_GfxCmdList.SetTable(4, m_IBL.GetView(), true);
        m_GfxCmdList.SetCBV(5, m_SceneParam.GetResource(), true);

        D3D12_DISPATCH_RAYS_DESC desc = {};
        desc.RayGenerationShaderRecord  = m_RayGenTable.GetRecordView();
        desc.MissShaderTable            = m_MissTable.GetTableView();
        desc.HitGroupTable              = m_HitGroupTable.GetTableView();
        desc.Width                      = m_SceneDesc.Width;
        desc.Height                     = m_SceneDesc.Height;
        desc.Depth                      = 1;

        m_GfxCmdList.DispatchRays(&desc);

        // バリアを張っておく.
        m_GfxCmdList.BarrierUAV(m_Canvas.GetResource());
    }

    // スワップチェインに描画.
    {
        asdx::ScopedBarrier barrier0(
            m_GfxCmdList,
            m_Canvas.GetResource(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        m_GfxCmdList.BarrierTransition(
            m_ColorTarget[idx].GetResource(), 0,
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET);

        // トーンマップ実行.
        m_GfxCmdList.SetTarget(m_ColorTarget[idx].GetRTV(), nullptr);
        m_GfxCmdList.SetViewport(m_ColorTarget[idx].GetResource());
        m_GfxCmdList.SetRootSignature(m_TonemapRootSig.GetPtr(), false);
        m_GfxCmdList.SetPipelineState(m_TonemapPSO.GetPtr());
        m_GfxCmdList.SetTable(0, m_Canvas.GetSRV());
        asdx::Quad::Instance().Draw(m_GfxCmdList.GetCommandList());

        // 2D描画.
        Draw2D();
    }

    // リードバック実行.
    {
        m_GfxCmdList.BarrierTransition(
            m_ColorTarget[idx].GetResource(), 0,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_COPY_SOURCE);

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.Type                                = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.pResource                           = m_ReadBackTexture[m_ReadBackIndex].GetPtr();
        dst.PlacedFootprint.Footprint.Width     = static_cast<UINT>(m_SceneDesc.Width);
        dst.PlacedFootprint.Footprint.Height    = m_SceneDesc.Height;
        dst.PlacedFootprint.Footprint.Depth     = 1;
        dst.PlacedFootprint.Footprint.RowPitch  = m_ReadBackPitch;
        dst.PlacedFootprint.Footprint.Format    = DXGI_FORMAT_R8G8B8A8_UNORM;

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.Type                = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource           = m_ColorTarget[idx].GetResource();
        src.SubresourceIndex    = 0;

        D3D12_BOX box = {};
        box.left    = 0;
        box.right   = m_SceneDesc.Width;
        box.top     = 0;
        box.bottom  = m_SceneDesc.Height;
        box.front   = 0;
        box.back    = 1;

        m_GfxCmdList.CopyTextureRegion(&dst, 0, 0, 0, &src, &box);

        m_ReadBackIndex = (m_ReadBackIndex + 1) % 3;

        m_GfxCmdList.BarrierTransition(
            m_ColorTarget[idx].GetResource(), 0,
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_PRESENT);
    }

    // コマンド記録終了.
    m_GfxCmdList.Close();

    ID3D12CommandList* pCmds[] = {
        m_GfxCmdList.GetCommandList()
    };

    auto pGraphicsQueue = asdx::GetGraphicsQueue();

    // 前フレームの描画の完了を待機.
    if (m_FrameWaitPoint.IsValid())
    { pGraphicsQueue->Sync(m_FrameWaitPoint); }

    // コマンドを実行.
    pGraphicsQueue->Execute(_countof(pCmds), pCmds);

    // 待機点を発行.
    m_FrameWaitPoint = pGraphicsQueue->Signal();

    // 画面に表示.
    Present(1);

    // フレーム同期.
    asdx::FrameSync();
}

//-----------------------------------------------------------------------------
//      リサイズ処理です.
//-----------------------------------------------------------------------------
void Renderer::OnResize(const asdx::ResizeEventArgs& args)
{
}

//-----------------------------------------------------------------------------
//      キー処理です.
//-----------------------------------------------------------------------------
void Renderer::OnKey(const asdx::KeyEventArgs& args)
{
#if ASDX_ENABLE_IMGUI
    asdx::GuiMgr::Instance().OnKey(
        args.IsKeyDown, args.IsAltDown, args.KeyCode);
#endif

#if (!CAMP_RELEASE)
    m_CameraController.OnKey(args.KeyCode, args.IsKeyDown, args.IsAltDown);
#endif
}

//-----------------------------------------------------------------------------
//      マウス処理です.
//-----------------------------------------------------------------------------
void Renderer::OnMouse(const asdx::MouseEventArgs& args)
{
    auto isAltDown = !!(GetKeyState(VK_MENU) & 0x8000);

#if ASDX_ENABLE_IMGUI
    if (!isAltDown)
    {
        asdx::GuiMgr::Instance().OnMouse(
            args.X, args.Y, args.WheelDelta,
            args.IsLeftButtonDown,
            args.IsMiddleButtonDown,
            args.IsRightButtonDown);
    }
#endif

#if (!CAMP_RELEASE)

    if (isAltDown)
    {
        m_CameraController.OnMouse(
            args.X,
            args.Y,
            args.WheelDelta,
            args.IsLeftButtonDown,
            args.IsRightButtonDown,
            args.IsMiddleButtonDown,
            args.IsSideButton1Down,
            args.IsSideButton2Down);
    }
#endif
}

//-----------------------------------------------------------------------------
//      タイピング処理です.
//-----------------------------------------------------------------------------
void Renderer::OnTyping(uint32_t keyCode)
{
#if ASDX_ENABLE_IMGUI
    asdx::GuiMgr::Instance().OnTyping(keyCode);
#endif
}

//-----------------------------------------------------------------------------
//      2D描画を行います.
//-----------------------------------------------------------------------------
void Renderer::Draw2D()
{
#if ASDX_ENABLE_IMGUI
    asdx::GuiMgr::Instance().Update(m_Width, m_Height);

    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowSize(ImVec2(140, 50));
    if (ImGui::Begin(u8"フレーム情報", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar))
    {
        ImGui::Text(u8"FPS   : %.3lf", GetFPS());
        ImGui::Text(u8"Frame : %ld", GetFrameCount());
    }
    ImGui::End();

    if (ImGui::Begin(u8"デバッグ設定", &m_DebugSetting))
    {
        auto albedo = const_cast<asdx::IShaderResourceView*>(m_AlbedoTarget.GetSRV());
        auto normal = const_cast<asdx::IShaderResourceView*>(m_NormalTarget.GetSRV());

        ImVec2 texSize(320, 180);

        ImGui::Image(static_cast<ImTextureID>(albedo), texSize);
        ImGui::Image(static_cast<ImTextureID>(normal), texSize);
    }
    ImGui::End();


    asdx::GuiMgr::Instance().Draw(m_GfxCmdList.GetCommandList());
#endif
}

} // namespace r3d
