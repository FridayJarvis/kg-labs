#pragma once

#include <array>
#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

// Owns the material data produced by the geometry pass.  Resource-state
// tracking lives here so callers cannot accidentally sample an attachment
// while it is still bound for output.
class GBuffer
{
public:
    enum class Target : uint32_t { Albedo = 0, Normal = 1, Depth = 2, Count = 3 };
    static constexpr uint32_t kColorTargetCount = 2;
    static constexpr uint32_t kShaderTargetCount = 3;

    GBuffer() = default;
    GBuffer(ID3D12Device* device, uint32_t width, uint32_t height);

    void Initialize(ID3D12Device* device, uint32_t width, uint32_t height);
    void Resize(uint32_t width, uint32_t height);

    void BeginGeometry(ID3D12GraphicsCommandList* commands);
    void BeginLighting(ID3D12GraphicsCommandList* commands);
    void Clear(ID3D12GraphicsCommandList* commands, const float clearColor[4]);

    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kColorTargetCount> ColorRtvs() const;
    D3D12_CPU_DESCRIPTOR_HANDLE DepthDsv() const;
    ID3D12DescriptorHeap* SrvHeap() const { return m_srvHeap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE SrvTable() const;

    static constexpr DXGI_FORMAT AlbedoFormat() { return DXGI_FORMAT_R8G8B8A8_UNORM; }
    static constexpr DXGI_FORMAT NormalFormat() { return DXGI_FORMAT_R16G16B16A16_FLOAT; }
    static constexpr DXGI_FORMAT DepthResourceFormat() { return DXGI_FORMAT_R24G8_TYPELESS; }
    static constexpr DXGI_FORMAT DepthViewFormat() { return DXGI_FORMAT_D24_UNORM_S8_UINT; }

private:
    struct Attachment
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    };

    void CreateHeaps();
    void CreateAttachments();
    void Transition(ID3D12GraphicsCommandList* commands, Target target,
        D3D12_RESOURCE_STATES nextState);

    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    std::array<Attachment, kShaderTargetCount> m_attachments{};
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_rtvStride = 0;
    uint32_t m_srvStride = 0;
};
