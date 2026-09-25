#include "../include/GBuffer.h"

#include <stdexcept>

using Microsoft::WRL::ComPtr;

namespace
{
    void Require(HRESULT result, const char* operation)
    {
        if (FAILED(result)) throw std::runtime_error(operation);
    }

    D3D12_HEAP_PROPERTIES DefaultHeap()
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        return heap;
    }
}

GBuffer::GBuffer(ID3D12Device* device, uint32_t width, uint32_t height)
{
    Initialize(device, width, height);
}

void GBuffer::Initialize(ID3D12Device* device, uint32_t width, uint32_t height)
{
    m_device = device;
    m_width = width;
    m_height = height;
    m_rtvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_srvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    CreateHeaps();
    CreateAttachments();
}

void GBuffer::Resize(uint32_t width, uint32_t height)
{
    if (!m_device || width == 0 || height == 0) return;
    m_width = width;
    m_height = height;
    for (auto& attachment : m_attachments)
    {
        attachment.resource.Reset();
        attachment.state = D3D12_RESOURCE_STATE_COMMON;
    }
    CreateAttachments();
}

void GBuffer::CreateHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtv{};
    rtv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv.NumDescriptors = kColorTargetCount;
    Require(m_device->CreateDescriptorHeap(&rtv, IID_PPV_ARGS(&m_rtvHeap)), "Create G-buffer RTV heap");

    D3D12_DESCRIPTOR_HEAP_DESC dsv{};
    dsv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsv.NumDescriptors = 1;
    Require(m_device->CreateDescriptorHeap(&dsv, IID_PPV_ARGS(&m_dsvHeap)), "Create G-buffer DSV heap");

    D3D12_DESCRIPTOR_HEAP_DESC srv{};
    srv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    // The fourth slot is reserved for the cascaded shadow-map array.  Keeping
    // it in this heap lets the lighting pass bind all sampled textures through
    // the single shader-visible CBV/SRV/UAV heap allowed by D3D12.
    srv.NumDescriptors = kShaderTargetCount + 1;
    srv.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    Require(m_device->CreateDescriptorHeap(&srv, IID_PPV_ARGS(&m_srvHeap)), "Create G-buffer SRV heap");
}

void GBuffer::CreateAttachments()
{
    const DXGI_FORMAT resourceFormats[kShaderTargetCount] = {
        AlbedoFormat(), NormalFormat(), DepthResourceFormat()
    };
    const auto heap = DefaultHeap();

    auto rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    auto srv = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    const auto dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

    for (uint32_t i = 0; i < kShaderTargetCount; ++i)
    {
        const bool depth = i == static_cast<uint32_t>(Target::Depth);
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = m_width;
        desc.Height = m_height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = resourceFormats[i];
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
                           : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clear{};
        clear.Format = depth ? DepthViewFormat() : resourceFormats[i];
        if (depth) clear.DepthStencil.Depth = 1.0f;

        Require(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COMMON, &clear,
            IID_PPV_ARGS(&m_attachments[i].resource)), "Create G-buffer attachment");

        if (depth)
        {
            D3D12_DEPTH_STENCIL_VIEW_DESC view{};
            view.Format = DepthViewFormat();
            view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            m_device->CreateDepthStencilView(m_attachments[i].resource.Get(), &view, dsv);
        }
        else
        {
            D3D12_RENDER_TARGET_VIEW_DESC view{};
            view.Format = resourceFormats[i];
            view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            m_device->CreateRenderTargetView(m_attachments[i].resource.Get(), &view, rtv);
            rtv.ptr += m_rtvStride;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Format = depth ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : resourceFormats[i];
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Texture2D.MipLevels = 1;
        m_device->CreateShaderResourceView(m_attachments[i].resource.Get(), &view, srv);
        srv.ptr += m_srvStride;
    }
}

void GBuffer::Transition(ID3D12GraphicsCommandList* commands, Target target,
    D3D12_RESOURCE_STATES nextState)
{
    auto& attachment = m_attachments[static_cast<uint32_t>(target)];
    if (attachment.state == nextState) return;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = attachment.resource.Get();
    barrier.Transition.StateBefore = attachment.state;
    barrier.Transition.StateAfter = nextState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commands->ResourceBarrier(1, &barrier);
    attachment.state = nextState;
}

void GBuffer::BeginGeometry(ID3D12GraphicsCommandList* commands)
{
    Transition(commands, Target::Albedo, D3D12_RESOURCE_STATE_RENDER_TARGET);
    Transition(commands, Target::Normal, D3D12_RESOURCE_STATE_RENDER_TARGET);
    Transition(commands, Target::Depth, D3D12_RESOURCE_STATE_DEPTH_WRITE);
}

void GBuffer::BeginLighting(ID3D12GraphicsCommandList* commands)
{
    Transition(commands, Target::Albedo, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Transition(commands, Target::Normal, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Transition(commands, Target::Depth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void GBuffer::Clear(ID3D12GraphicsCommandList* commands, const float clearColor[4])
{
    const auto rtvs = ColorRtvs();
    for (auto rtv : rtvs) commands->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    commands->ClearDepthStencilView(DepthDsv(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

std::array<D3D12_CPU_DESCRIPTOR_HANDLE, GBuffer::kColorTargetCount> GBuffer::ColorRtvs() const
{
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kColorTargetCount> handles{};
    handles[0] = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    handles[1] = handles[0];
    handles[1].ptr += m_rtvStride;
    return handles;
}

D3D12_CPU_DESCRIPTOR_HANDLE GBuffer::DepthDsv() const
{
    return m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
}

D3D12_GPU_DESCRIPTOR_HANDLE GBuffer::SrvTable() const
{
    return m_srvHeap->GetGPUDescriptorHandleForHeapStart();
}

D3D12_GPU_DESCRIPTOR_HANDLE GBuffer::ShadowSrv() const
{
    auto handle = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(kShaderTargetCount) * m_srvStride;
    return handle;
}

void GBuffer::SetShadowMap(ID3D12Resource* shadowMap, uint32_t cascadeCount)
{
    if (!m_device || !m_srvHeap || !shadowMap || cascadeCount == 0) return;

    auto handle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(kShaderTargetCount) * m_srvStride;

    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Format = DXGI_FORMAT_R32_FLOAT;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    view.Texture2DArray.MostDetailedMip = 0;
    view.Texture2DArray.MipLevels = 1;
    view.Texture2DArray.FirstArraySlice = 0;
    view.Texture2DArray.ArraySize = cascadeCount;
    m_device->CreateShaderResourceView(shadowMap, &view, handle);
}
