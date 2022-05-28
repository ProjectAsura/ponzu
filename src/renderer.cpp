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

#if ASDX_ENABLE_IMGUI
#include "../external/asdx12/external/imgui/imgui.h"
#endif


namespace {

//-----------------------------------------------------------------------------
// Constant Values.
//-----------------------------------------------------------------------------
#include "../res/shader/Compile/TonemapVS.inc"
#include "../res/shader/Compile/TonemapPS.inc"

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
    }

    // レイトレ用パイプラインステート生成.
    {
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

    m_TonemapPSO    .Term();
    m_TonemapRootSig.Term();
    m_RayTracingPSO .Term();

    for(size_t i=0; i<m_BLAS.size(); ++i)
    { m_BLAS[i].Term(); }
    m_BLAS.clear();

    m_TLAS  .Term();
    m_Canvas.Term();
}

//-----------------------------------------------------------------------------
//      フレーム遷移時の処理です.
//-----------------------------------------------------------------------------
void Renderer::OnFrameMove(asdx::FrameEventArgs& args)
{
    // 制限時間を超えた
    if (args.Time >= m_RenderTimeSec)
    {
        // TODO : 終了処理.
        return;
    }
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

        asdx::ScopedBarrier barrier1(
            m_GfxCmdList,
            m_ColorTarget[idx].GetResource(),
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
