//-----------------------------------------------------------------------------
// File : target_wrapper.h
// Desc : Target Wrapper.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------
#pragma once

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <gfx/asdxTarget.h>
#include <gfx/asdxCommandList.h>

/* Single Thread 限定なので注意!! */

///////////////////////////////////////////////////////////////////////////////
// ColorView class
///////////////////////////////////////////////////////////////////////////////
class ColorView
{
public:
    bool Init(const asdx::TargetDesc* desc);
    void Term();
    void Transition(ID3D12GraphicsCommandList* pCmdList, D3D12_RESOURCE_STATES state);
    void Transition(asdx::CommandList& cmdList, D3D12_RESOURCE_STATES state);
    bool Resize(uint32_t width, uint32_t height);

    ID3D12Resource* GetResource() const;
    const asdx::IRenderTargetView* GetRTV() const;
    const asdx::IShaderResourceView* GetSRV() const;
    asdx::TargetDesc GetDesc() const;
    void SetName(LPCWSTR name);

private:
    asdx::ColorTarget       m_Target;
    D3D12_RESOURCE_STATES   m_PrevState;
};

///////////////////////////////////////////////////////////////////////////////
// DepthView class
///////////////////////////////////////////////////////////////////////////////
class DepthView
{
public:
    bool Init(const asdx::TargetDesc* desc);
    void Term();
    void Transition(ID3D12GraphicsCommandList* pCmdList, D3D12_RESOURCE_STATES state);
    void Transition(asdx::CommandList& cmdList, D3D12_RESOURCE_STATES state);
    bool Resize(uint32_t width, uint32_t height);

    ID3D12Resource* GetResource() const;
    const asdx::IDepthStencilView* GetDSV() const;
    const asdx::IShaderResourceView* GetSRV() const;
    asdx::TargetDesc GetDesc() const;
    void SetName(LPCWSTR name);

private:
    asdx::DepthTarget       m_Target;
    D3D12_RESOURCE_STATES   m_PrevState;
};

///////////////////////////////////////////////////////////////////////////////
// ComputeView class
///////////////////////////////////////////////////////////////////////////////
class ComputeView
{
public:
    bool Init(const asdx::TargetDesc* desc);
    void Term();
    void Transition(ID3D12GraphicsCommandList* pCmdList, D3D12_RESOURCE_STATES state);
    void Transition(asdx::CommandList& cmdList, D3D12_RESOURCE_STATES state);

    ID3D12Resource* GetResource() const;
    const asdx::IUnorderedAccessView* GetUAV() const;
    const asdx::IShaderResourceView* GetSRV() const;
    asdx::TargetDesc GetDesc() const;
    void SetName(LPCWSTR name);

private:
    asdx::ComputeTarget     m_Target;
    D3D12_RESOURCE_STATES   m_PrevState;
};
