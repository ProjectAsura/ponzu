//-----------------------------------------------------------------------------
// File : target_wrapper.cpp
// Desc : Target Wrapper.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <target_wrapper.h>


///////////////////////////////////////////////////////////////////////////////
// ColorView class
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      初期化処理を行います.
//-----------------------------------------------------------------------------
bool ColorView::Init(const asdx::TargetDesc* desc)
{
    if (m_Target.Init(desc))
    {
        m_PrevState = desc->InitState;
        return true;
    }

    return false;
}

//-----------------------------------------------------------------------------
//      終了処理を行います.
//-----------------------------------------------------------------------------
void ColorView::Term()
{
    m_Target.Term();
    m_PrevState = D3D12_RESOURCE_STATE_COMMON;
}

//-----------------------------------------------------------------------------
//      リサイズ処理を行います.
//-----------------------------------------------------------------------------
bool ColorView::Resize(uint32_t width, uint32_t height)
{ return m_Target.Resize(width, height); }

//-----------------------------------------------------------------------------
//      ステート遷移を行います.
//-----------------------------------------------------------------------------
void ColorView::Transition(ID3D12GraphicsCommandList* pCmdList, D3D12_RESOURCE_STATES state)
{
    if (state == m_PrevState)
    { return; }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                    = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource    = m_Target.GetResource();
    barrier.Transition.Subresource  = 0;
    barrier.Transition.StateBefore  = m_PrevState;
    barrier.Transition.StateAfter   = state;

    pCmdList->ResourceBarrier(1, &barrier);

    m_PrevState = state;
}

//-----------------------------------------------------------------------------
//      ステート遷移を行います.
//-----------------------------------------------------------------------------
void ColorView::Transition(asdx::CommandList& cmdList, D3D12_RESOURCE_STATES state)
{ Transition(cmdList.GetCommandList(), state); }

//-----------------------------------------------------------------------------
//      リソースを取得します.
//-----------------------------------------------------------------------------
ID3D12Resource* ColorView::GetResource() const
{ return m_Target.GetResource(); }

//-----------------------------------------------------------------------------
//      レンダーターゲットビューを取得します.
//-----------------------------------------------------------------------------
const asdx::IRenderTargetView* ColorView::GetRTV() const
{ return m_Target.GetRTV(); }

//-----------------------------------------------------------------------------
//      シェーダリソースビューを取得します.
//-----------------------------------------------------------------------------
const asdx::IShaderResourceView* ColorView::GetSRV() const
{ return m_Target.GetSRV(); }

//-----------------------------------------------------------------------------
//      構成設定を取得します.
//-----------------------------------------------------------------------------
asdx::TargetDesc ColorView::GetDesc() const
{ return m_Target.GetDesc(); }

//-----------------------------------------------------------------------------
//      デバッグ名を設定します.
//-----------------------------------------------------------------------------
void ColorView::SetName(LPCWSTR name)
{ m_Target.SetName(name); }


///////////////////////////////////////////////////////////////////////////////
// DepthView class
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      初期化処理を行います.
//-----------------------------------------------------------------------------
bool DepthView::Init(const asdx::TargetDesc* desc)
{
    if (m_Target.Init(desc))
    {
        m_PrevState = desc->InitState;
        return true;
    }

    return false;
}

//-----------------------------------------------------------------------------
//      終了処理を行います.
//-----------------------------------------------------------------------------
void DepthView::Term()
{
    m_Target.Term();
    m_PrevState = D3D12_RESOURCE_STATE_COMMON;
}

//-----------------------------------------------------------------------------
//      リサイズ処理を行います.
//-----------------------------------------------------------------------------
bool DepthView::Resize(uint32_t width, uint32_t height)
{ return m_Target.Resize(width, height); }

//-----------------------------------------------------------------------------
//      ステート遷移を行います
//-----------------------------------------------------------------------------
void DepthView::Transition(ID3D12GraphicsCommandList* pCmdList, D3D12_RESOURCE_STATES state)
{
    if (state == m_PrevState)
    { return; }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                    = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource    = m_Target.GetResource();
    barrier.Transition.Subresource  = 0;
    barrier.Transition.StateBefore  = m_PrevState;
    barrier.Transition.StateAfter   = state;

    pCmdList->ResourceBarrier(1, &barrier);

    m_PrevState = state;
}

//-----------------------------------------------------------------------------
//      ステート遷移を行います.
//-----------------------------------------------------------------------------
void DepthView::Transition(asdx::CommandList& cmdList, D3D12_RESOURCE_STATES state)
{ Transition(cmdList.GetCommandList(), state); }

//-----------------------------------------------------------------------------
//      リソースを取得します.
//-----------------------------------------------------------------------------
ID3D12Resource* DepthView::GetResource() const
{ return m_Target.GetResource(); }

//-----------------------------------------------------------------------------
//      深度ステンシルビューを取得します.
//-----------------------------------------------------------------------------
const asdx::IDepthStencilView* DepthView::GetDSV() const
{ return m_Target.GetDSV(); }

//-----------------------------------------------------------------------------
//      シェーダリソースビューを取得します.
//-----------------------------------------------------------------------------
const asdx::IShaderResourceView* DepthView::GetSRV() const
{ return m_Target.GetSRV(); }

//-----------------------------------------------------------------------------
//      構成設定を取得します.
//-----------------------------------------------------------------------------
asdx::TargetDesc DepthView::GetDesc() const
{ return m_Target.GetDesc(); }

//-----------------------------------------------------------------------------
//      デバッグ名を設定します.
//-----------------------------------------------------------------------------
void DepthView::SetName(LPCWSTR name)
{ m_Target.SetName(name); }


///////////////////////////////////////////////////////////////////////////////
// ComputeView class
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      初期化処理を行います.
//-----------------------------------------------------------------------------
bool ComputeView::Init(const asdx::TargetDesc* desc)
{
    if (m_Target.Init(desc))
    {
        m_PrevState = desc->InitState;
        return true;
    }

    return false;
}

//-----------------------------------------------------------------------------
//      終了処理を行います.
//-----------------------------------------------------------------------------
void ComputeView::Term()
{
    m_Target.Term();
    m_PrevState = D3D12_RESOURCE_STATE_COMMON;
}

//-----------------------------------------------------------------------------
//      ステート遷移を行います.
//-----------------------------------------------------------------------------
void ComputeView::Transition(ID3D12GraphicsCommandList* pCmdList, D3D12_RESOURCE_STATES state)
{
    if (state == m_PrevState)
    { return; }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                    = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource    = m_Target.GetResource();
    barrier.Transition.Subresource  = 0;
    barrier.Transition.StateBefore  = m_PrevState;
    barrier.Transition.StateAfter   = state;

    pCmdList->ResourceBarrier(1, &barrier);

    m_PrevState = state;
}

//-----------------------------------------------------------------------------
//      ステート遷移を行います.
//-----------------------------------------------------------------------------
void ComputeView::Transition(asdx::CommandList& cmdList, D3D12_RESOURCE_STATES state)
{ Transition(cmdList.GetCommandList(), state); }

//-----------------------------------------------------------------------------
//      リソースを取得します.
//-----------------------------------------------------------------------------
ID3D12Resource* ComputeView::GetResource() const
{ return m_Target.GetResource(); }

//-----------------------------------------------------------------------------
//      アンオーダードアクセスビューを取得します.
//-----------------------------------------------------------------------------
const asdx::IUnorderedAccessView* ComputeView::GetUAV() const
{ return m_Target.GetUAV(); }

//-----------------------------------------------------------------------------
//      シェーダリソースビューを取得します.
//-----------------------------------------------------------------------------
const asdx::IShaderResourceView* ComputeView::GetSRV() const
{ return m_Target.GetSRV(); }

//-----------------------------------------------------------------------------
//      構成設定を取得します.
//-----------------------------------------------------------------------------
asdx::TargetDesc ComputeView::GetDesc() const
{ return m_Target.GetDesc(); }

//-----------------------------------------------------------------------------
//      デバッグ名を設定します.
//-----------------------------------------------------------------------------
void ComputeView::SetName(LPCWSTR name)
{ m_Target.SetName(name); }
