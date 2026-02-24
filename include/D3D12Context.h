#pragma once

#include <windows.h>
#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <cstdint>
#include <vector>
#include <string>

class GraphicsEngine
{
public:
    ~GraphicsEngine();

    bool Setup(HWND hWnd, uint32_t w, uint32_t h);
    void Cleanup();

    void HandleResize(uint32_t w, uint32_t h);
    void RenderFrame();
    void UpdateCamera(const DirectX::XMFLOAT3& pos, float yaw, float pitch);

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
        uint32_t TextureSrvIndex = 1;  // index into cbvHeap (0 = CBV, 1+ = SRVs)
    };

private:
    bool InitDevice();
    bool InitCommandQueue();
    bool InitSwapChain();
    bool InitHeaps();
    bool InitBackBufferViews();
    bool InitDepthBuffer();

    bool CompileShaders();
    bool CreateMesh();
    bool CreateConstantBuffer();
    bool CreateRootSignature();
    bool CreatePipeline();
    void UploadConstants();

    void WaitForGpu();

    D3D12_CPU_DESCRIPTOR_HANDLE GetActiveRTV() const;
    ID3D12Resource* GetActiveBackBuffer() const;

private:
    static constexpr uint32_t kNumFrameBuffers = 2;

    struct alignas(16) FrameConstants
    {
        DirectX::XMFLOAT4X4 ModelMatrix;
        DirectX::XMFLOAT4X4 MVP;

        DirectX::XMFLOAT3 CameraPos; float _p0 = 0.0f;
        DirectX::XMFLOAT3 SunDir;    float _p1 = 0.0f;

        DirectX::XMFLOAT4 AmbientColor;
        DirectX::XMFLOAT4 DiffuseColor;
        DirectX::XMFLOAT4 SpecularColor;
        float Shininess = 32.0f;
        float _p2[3] = { 0, 0, 0 };
    };

    bool m_ready = false;

    HWND     m_windowHandle = nullptr;
    uint32_t m_backbufferWidth = 0;
    uint32_t m_backbufferHeight = 0;

    Microsoft::WRL::ComPtr<IDXGIFactory4>             m_dxgiFactory;
    Microsoft::WRL::ComPtr<ID3D12Device>              m_d3dDevice;

    Microsoft::WRL::ComPtr<ID3D12CommandQueue>        m_commandQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator>    m_commandAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;

    Microsoft::WRL::ComPtr<ID3D12Fence> m_gpuFence;
    uint64_t m_gpuFenceCounter = 0;
    HANDLE   m_gpuFenceEvent = nullptr;

    Microsoft::WRL::ComPtr<IDXGISwapChain>   m_swapchain;
    Microsoft::WRL::ComPtr<ID3D12Resource>   m_backBuffers[kNumFrameBuffers];
    uint32_t m_activeBuffer = 0;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_renderTargetHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_depthHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_constantHeap;   // CBV + SRVs

    uint32_t m_rtvHandleSize = 0;
    uint32_t m_dsvHandleSize = 0;
    uint32_t m_cbvSrvUavHandleSize = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_depthBuffer;

    D3D12_VIEWPORT m_vp{};
    D3D12_RECT     m_scissor{};

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSig;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;

    Microsoft::WRL::ComPtr<ID3DBlob> m_vertexShaderBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> m_pixelShaderBlob;

    D3D12_INPUT_ELEMENT_DESC m_vertexLayout[3]{};

    Microsoft::WRL::ComPtr<ID3D12Resource> m_vbGpu;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_ibGpu;
    D3D12_VERTEX_BUFFER_VIEW m_vertexView{};
    D3D12_INDEX_BUFFER_VIEW  m_indexView{};
    uint32_t m_numIndices = 0;

    // Textures (index 0 = 1x1 white fallback, 1+ = loaded from disk)
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_textures;

    // Per-material draw calls
    std::vector<DrawItem> m_drawItems;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_constBuffer;
    uint8_t* m_mappedCBData = nullptr;
    uint32_t m_cbAlignedSize = 0;

    DirectX::XMFLOAT4X4 m_worldMatrix{};
    DirectX::XMFLOAT4X4 m_viewMatrix{};
    DirectX::XMFLOAT4X4 m_projMatrix{};
    DirectX::XMFLOAT3   m_cameraPos{ 0.3f, 3.5f, -4.5f };
    DirectX::XMFLOAT3   m_sunDirection{ 0.7f, -0.6f, 0.3f };
};