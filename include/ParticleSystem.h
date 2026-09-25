#pragma once

#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

class ParticleSystem
{
public:
    static constexpr uint32_t ParticleCount = 2048;

    ParticleSystem() = default;
    ~ParticleSystem();

    ParticleSystem(const ParticleSystem&) = delete;
    ParticleSystem& operator=(const ParticleSystem&) = delete;

    void Initialize(ID3D12Device* device);
    void SetFrameData(const DirectX::XMFLOAT4X4& view,
        const DirectX::XMFLOAT4X4& projection,
        const DirectX::XMFLOAT3& cameraPosition,
        const DirectX::XMFLOAT3& emitterPosition,
        float deltaTime, float totalTime);
    void Simulate(ID3D12GraphicsCommandList* commands);
    void Draw(ID3D12GraphicsCommandList* commands) const;

private:
    static constexpr uint32_t ParticleStride = 64;

    struct alignas(16) Constants
    {
        DirectX::XMFLOAT4X4 ViewProjection{};
        DirectX::XMFLOAT3 CameraPosition{};
        float DeltaTime = 0.0f;
        float TotalTime = 0.0f;
        DirectX::XMFLOAT3 EmitterPosition{};
    };

    void CreateRootSignatures();
    void CreatePipelineStates();
    void CreateBuffers();
    void ResetCounter(ID3D12GraphicsCommandList* commands, uint32_t index) const;
    void InitializeParticles(ID3D12GraphicsCommandList* commands);

    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_counterRootSignature;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_initializeRootSignature;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_simulateRootSignature;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_drawRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_counterPipeline;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_initializePipeline;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_simulatePipeline;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_drawPipeline;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_uavHeap;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> m_particleBuffers;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> m_counterBuffers;
    std::array<D3D12_RESOURCE_STATES, 2> m_particleStates{
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> m_constantBuffer;
    Constants* m_mappedConstants = nullptr;
    uint32_t m_descriptorSize = 0;
    uint32_t m_consumeIndex = 0;
    uint32_t m_appendIndex = 1;
    bool m_initializedOnGpu = false;
};
