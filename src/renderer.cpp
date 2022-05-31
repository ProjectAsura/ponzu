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
#include <process.h>

#include <fpng.h>

#if ASDX_ENABLE_IMGUI
#include "../external/asdx12/external/imgui/imgui.h"
#endif

#include <fnd/asdxStopWatch.h>


extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 602;}
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }

namespace {

//-----------------------------------------------------------------------------
// Constant Values.
//-----------------------------------------------------------------------------
#include "../res/shader/Compile/TonemapVS.inc"
#include "../res/shader/Compile/TonemapPS.inc"
#include "../res/shader/Compile/RtCamp.inc"

struct Payload
{
    asdx::Vector3   Position;
    uint32_t        MaterialId;
    asdx::Vector3   Normal;
    asdx::Vector3   Tangent;
    asdx::Vector2   TexCoord;
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
    //m_ModelMgr.Init();

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
        asdx::DescriptorSetLayout<7, 1> layout;
        layout.SetTableUAV(0, asdx::SV_ALL, 0);
        layout.SetSRV(1, asdx::SV_ALL, 0);
        layout.SetSRV(2, asdx::SV_ALL, 1);
        layout.SetSRV(3, asdx::SV_ALL, 2);
        layout.SetSRV(4, asdx::SV_ALL, 3);
        layout.SetTableSRV(5, asdx::SV_ALL, 4);
        layout.SetCBV(6, asdx::SV_ALL, 0);
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

    for(auto i=0; i<3; ++i)
    {
        m_ReadBackTexture[i].Reset();
        m_ExportData[i].Pixels.clear();
    }

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

    // レイトレ実行.
    {
    }

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
}

//-----------------------------------------------------------------------------
//      マウス処理です.
//-----------------------------------------------------------------------------
void Renderer::OnMouse(const asdx::MouseEventArgs& args)
{
#if ASDX_ENABLE_IMGUI
    asdx::GuiMgr::Instance().OnMouse(
        args.X, args.Y, args.WheelDelta,
        args.IsLeftButtonDown,
        args.IsMiddleButtonDown,
        args.IsRightButtonDown);
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
        ImGui::Text(u8"あああああああああああ");
    }
    ImGui::End();


    asdx::GuiMgr::Instance().Draw(m_GfxCmdList.GetCommandList());
#endif
}

} // namespace r3d
