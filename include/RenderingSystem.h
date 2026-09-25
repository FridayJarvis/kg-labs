#pragma once

#include "GBuffer.h"
#include "ParticleSystem.h"

#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <array>
#include <cstdint>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <memory>
#include <string>
#include <vector>
#include <windows.h>
#include <wrl.h>

class RenderingSystem
{
public:
    ~RenderingSystem();

    bool Setup(HWND window, uint32_t width, uint32_t height);
    void Cleanup();
    void HandleResize(uint32_t width, uint32_t height);
    void RenderFrame();
    void UpdateCamera(const DirectX::XMFLOAT3& position, float yaw, float pitch);
    void UpdateAnimation(float deltaTime);
    bool ProcessGuiMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    struct MeshVertex
    {
        DirectX::XMFLOAT3 Position;
        DirectX::XMFLOAT3 Normal;
        DirectX::XMFLOAT2 TexC;
    };

    struct DrawItem
    {
        uint32_t IndexCount = 0;
        uint32_t StartIndexLocation = 0;
        uint32_t TextureSrvIndex = 1;
        DirectX::XMFLOAT4 DiffuseColor{ 1.f, 1.f, 1.f, 1.f };
        float Shininess = 32.f;
        uint32_t MaterialMode = 0;
    };

    struct InstanceVertex
    {
        DirectX::XMFLOAT4X4 World;
    };

    struct SceneInstance
    {
        InstanceVertex GpuData;
        DirectX::BoundingBox Bounds;
    };

    struct SpatialNode
    {
        DirectX::BoundingBox Bounds;
        std::vector<uint32_t> Objects;
        std::array<std::unique_ptr<SpatialNode>, 8> Children;
    };

    enum class LocalLightType : int32_t { Point = 0, Spot = 1 };

    // One binary layout for every local light.  Type is consumed by the
    // volume shader, so a single instanced draw handles both point and spots.
    struct alignas(16) LocalLight
    {
        DirectX::XMFLOAT3 Position{};
        float Radius = 1.f;
        DirectX::XMFLOAT3 Direction{ 0.f, -1.f, 0.f };
        float ConeCosine = 0.8f;
        DirectX::XMFLOAT3 Color{ 1.f, 1.f, 1.f };
        float Intensity = 1.f;
        int32_t Type = static_cast<int32_t>(LocalLightType::Point);
        float Padding[3]{};
    };
    static_assert(sizeof(LocalLight) == 64, "LocalLight must match Deferred.hlsl");

private:
    static constexpr uint32_t kNumFrameBuffers = 2;
    static constexpr uint32_t kShadowCascadeCount = 3;
    static constexpr uint32_t kShadowMapResolution = 2048;
    static constexpr float kCameraNear = 0.5f;
    static constexpr float kCameraFar = 180.0f;
    static constexpr DXGI_FORMAT kSceneColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    static constexpr DXGI_FORMAT kExposureFormat = DXGI_FORMAT_R16_FLOAT;

    struct alignas(16) FrameConstants
    {
        DirectX::XMFLOAT4X4 Model;
        DirectX::XMFLOAT4X4 View;
        DirectX::XMFLOAT4X4 ViewProjection;
        DirectX::XMFLOAT4X4 InverseViewProjection;
        DirectX::XMFLOAT4X4 CascadeViewProjection[kShadowCascadeCount];
        DirectX::XMFLOAT4 CascadeSplits{};
        DirectX::XMFLOAT3 CameraPosition; float Ambient = 0.025f;
        DirectX::XMFLOAT3 DirectionalDirection; float DirectionalIntensity = 1.f;
        DirectX::XMFLOAT3 DirectionalColor{ 1.f, 0.96f, 0.88f }; float Padding0 = 0.f;
        DirectX::XMFLOAT2 TextureTiling{ 1.f, 1.f };
        DirectX::XMFLOAT2 TextureOffset{};
    };

    bool InitDevice();
    bool InitCommandQueue();
    bool InitSwapChain();
    bool InitHeaps();
    bool InitBackBufferViews();
    bool CompileShaders();
    bool CreateMesh();
    bool CreateFrameResources();
    bool CreateShadowResources();
    bool CreatePostProcessResources();
    bool CreateRootSignatures();
    bool CreatePipelines();
    bool CreateLightVolume();
    void CreateInstanceField(const DirectX::BoundingBox& prototypeBounds);
    std::unique_ptr<SpatialNode> BuildSpatialNode(
        const DirectX::BoundingBox& bounds,
        const std::vector<uint32_t>& objects, uint32_t depth);
    void UpdateInstanceVisibility();
    void QuerySpatialNode(const SpatialNode& node,
        const DirectX::BoundingFrustum& frustum,
        DirectX::ContainmentType inherited);
    void AppendSpatialNode(const SpatialNode& node);
    bool InitImGui();
    void DrawImGui();
    void UploadConstants();
    void UploadLights();
    void UpdateCascades();
    void RecordShadowPass();
    void RecordGeometryPass();
    void RecordDirectionalPass(D3D12_CPU_DESCRIPTOR_HANDLE target);
    void RecordLocalLightPass(D3D12_CPU_DESCRIPTOR_HANDLE target);
    void RecordEyeAdaptationPass();
    void RecordPostProcessPass(D3D12_CPU_DESCRIPTOR_HANDLE target);
    void WaitForGpu();

    D3D12_CPU_DESCRIPTOR_HANDLE GetActiveRTV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetSceneColorRTV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetExposureRTV(uint32_t index) const;
    ID3D12Resource* GetActiveBackBuffer() const;

    bool m_ready = false;
    HWND m_windowHandle = nullptr;
    uint32_t m_backbufferWidth = 0;
    uint32_t m_backbufferHeight = 0;

    Microsoft::WRL::ComPtr<IDXGIFactory4> m_dxgiFactory;
    Microsoft::WRL::ComPtr<ID3D12Device> m_d3dDevice;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_gpuFence;
    uint64_t m_gpuFenceCounter = 0;
    HANDLE m_gpuFenceEvent = nullptr;

    Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapchain;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_backBuffers[kNumFrameBuffers];
    uint32_t m_activeBuffer = 0;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_renderTargetHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_materialHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_imguiHeap;
    uint32_t m_rtvHandleSize = 0;
    uint32_t m_cbvSrvUavHandleSize = 0;
    D3D12_VIEWPORT m_vp{};
    D3D12_RECT m_scissor{};

    std::unique_ptr<GBuffer> m_gbuffer;
    std::unique_ptr<ParticleSystem> m_particleSystem;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_geometryRootSig;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_tessellationRootSig;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_lightingRootSig;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_shadowRootSig;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_postProcessRootSig;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_geometryPso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_geometryWireframePso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_instancedGeometryPso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_tessellationPso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_tessellationWireframePso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_directionalPso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_localLightPso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_shadowPso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_eyeAdaptationPso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_postProcessPso;
    Microsoft::WRL::ComPtr<ID3DBlob> m_geometryVs;
    Microsoft::WRL::ComPtr<ID3DBlob> m_instancedGeometryVs;
    Microsoft::WRL::ComPtr<ID3DBlob> m_geometryPs;
    Microsoft::WRL::ComPtr<ID3DBlob> m_tessellationVs;
    Microsoft::WRL::ComPtr<ID3DBlob> m_tessellationHs;
    Microsoft::WRL::ComPtr<ID3DBlob> m_tessellationDs;
    Microsoft::WRL::ComPtr<ID3DBlob> m_tessellationPs;
    Microsoft::WRL::ComPtr<ID3DBlob> m_fullscreenVs;
    Microsoft::WRL::ComPtr<ID3DBlob> m_directionalPs;
    Microsoft::WRL::ComPtr<ID3DBlob> m_localLightVs;
    Microsoft::WRL::ComPtr<ID3DBlob> m_localLightPs;
    Microsoft::WRL::ComPtr<ID3DBlob> m_shadowVs;
    Microsoft::WRL::ComPtr<ID3DBlob> m_eyeAdaptationPs;
    Microsoft::WRL::ComPtr<ID3DBlob> m_postProcessPs;
    D3D12_INPUT_ELEMENT_DESC m_vertexLayout[3]{};
    D3D12_INPUT_ELEMENT_DESC m_instancedVertexLayout[7]{};

    Microsoft::WRL::ComPtr<ID3D12Resource> m_vbGpu;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_ibGpu;
    D3D12_VERTEX_BUFFER_VIEW m_vertexView{};
    D3D12_INDEX_BUFFER_VIEW m_indexView{};
    uint32_t m_numIndices = 0;
    uint32_t m_tessellationStartIndex = 0;
    uint32_t m_tessellationIndexCount = 0;
    uint32_t m_tessellationTextureSrvIndex = 0;
    uint32_t m_groundStartIndex = 0;
    uint32_t m_groundIndexCount = 0;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_textures;
    std::vector<DrawItem> m_drawItems;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_volumeVb;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_volumeIb;
    D3D12_VERTEX_BUFFER_VIEW m_volumeVertexView{};
    D3D12_INDEX_BUFFER_VIEW m_volumeIndexView{};
    uint32_t m_volumeIndexCount = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_constBuffer;
    uint8_t* m_mappedCBData = nullptr;
    uint32_t m_cbAlignedSize = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_lightBuffer;
    uint8_t* m_mappedLightData = nullptr;
    std::vector<LocalLight> m_localLights;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_instanceBuffer;
    uint8_t* m_mappedInstanceData = nullptr;
    D3D12_VERTEX_BUFFER_VIEW m_instanceView{};
    Microsoft::WRL::ComPtr<ID3D12Resource> m_shadowInstanceBuffer;
    uint8_t* m_mappedShadowInstanceData = nullptr;
    std::vector<SceneInstance> m_sceneInstances;
    std::vector<uint32_t> m_visibleInstanceIndices;
    std::unique_ptr<SpatialNode> m_spatialRoot;
    uint32_t m_visibleInstanceCount = 0;
    uint32_t m_culledInstanceCount = 0;
    uint32_t m_nodesVisited = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_cascadeShadowMap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_shadowDsvHeap;
    D3D12_RESOURCE_STATES m_shadowMapState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    uint32_t m_dsvHandleSize = 0;
    D3D12_VIEWPORT m_shadowViewport{};
    D3D12_RECT m_shadowScissor{};
    std::array<DirectX::XMFLOAT4X4, kShadowCascadeCount> m_cascadeViewProjections{};
    DirectX::XMFLOAT4 m_cascadeSplits{};
    DirectX::BoundingBox m_sceneBounds{};

    Microsoft::WRL::ComPtr<ID3D12Resource> m_sceneColor;
    D3D12_RESOURCE_STATES m_sceneColorState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, GBuffer::kExposureCount> m_exposureMaps;
    std::array<D3D12_RESOURCE_STATES, GBuffer::kExposureCount> m_exposureStates{
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    };
    uint32_t m_exposureWriteIndex = 0;
    bool m_resetExposure = true;

    DirectX::XMFLOAT4X4 m_worldMatrix{};
    DirectX::XMFLOAT4X4 m_viewMatrix{};
    DirectX::XMFLOAT4X4 m_projMatrix{};
    DirectX::XMFLOAT3 m_cameraPos{ -4.f, 1.5f, -4.f };
    DirectX::XMFLOAT3 m_sunDirection{ 0.45f, -0.82f, 0.35f };
    float m_textureTime = 0.f;
    float m_frameDeltaTime = 0.f;
    float m_particleTime = 0.f;
    float m_particleDeltaTime = 0.f;
    DirectX::XMFLOAT3 m_particleEmitter{ 0.f, 0.4f, 0.f };
    bool m_particlesEnabled = true;
    bool m_particlesPaused = false;
    bool m_textureAnimationEnabled = false;
    bool m_showSponza = true;
    bool m_showGround = true;
    bool m_showDisplacementModel = true;
    bool m_showInstanceField = true;
    bool m_frustumCulling = true;
    bool m_octreeCulling = false;
    bool m_wireframe = false;
    bool m_useNormalMap = true;
    float m_displacementScale = 0.18f;
    float m_minTessellation = 1.0f;
    float m_maxTessellation = 6.0f;
    float m_tessellationNearDistance = 2.0f;
    float m_tessellationFarDistance = 18.0f;
    bool m_eyeAdaptationEnabled = true;
    bool m_vignetteEnabled = true;
    float m_exposureKey = 0.10f;
    float m_vignetteStrength = 0.65f;
    float m_vignetteInnerRadius = 0.35f;
    float m_vignetteOuterRadius = 1.15f;
    bool m_imguiReady = false;
};
