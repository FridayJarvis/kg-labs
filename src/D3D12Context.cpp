#include "../include/D3D12Context.h"
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <array>
#include <cmath>
#include <DirectXMath.h>
using namespace DirectX;

using Microsoft::WRL::ComPtr;

static void CheckHR(HRESULT hr, const char* msg)
{
    if (FAILED(hr))
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s (hr=0x%08X)", msg, (unsigned)hr);
        throw std::runtime_error(buf);
    }
}

GraphicsEngine::~GraphicsEngine()
{
    Cleanup();
}

bool GraphicsEngine::Setup(HWND hWnd, uint32_t w, uint32_t h)
{
    m_ready = false;

    m_windowHandle     = hWnd;
    m_backbufferWidth  = w;
    m_backbufferHeight = h;

#if defined(_DEBUG)
    {
        ComPtr<ID3D12Debug> dbg;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg))))
            dbg->EnableDebugLayer();
    }
#endif

    CheckHR(CreateDXGIFactory1(IID_PPV_ARGS(&m_dxgiFactory)), "CreateDXGIFactory1");

    InitDevice();
    InitCommandQueue();

    CheckHR(m_d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_gpuFence)), "CreateFence");
    m_gpuFenceCounter = 0;
    m_gpuFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_gpuFenceEvent)
        throw std::runtime_error("CreateEvent for fence failed");

    InitSwapChain();

    m_rtvHandleSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_dsvHandleSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    InitHeaps();
    InitBackBufferViews();
    InitDepthBuffer();

    m_vp      = { 0.0f, 0.0f, (float)m_backbufferWidth, (float)m_backbufferHeight, 0.0f, 1.0f };
    m_scissor = { 0, 0, (LONG)m_backbufferWidth, (LONG)m_backbufferHeight };

    XMStoreFloat4x4(&m_worldMatrix, DirectX::XMMatrixIdentity());

    XMVECTOR eye    = XMVectorSet(m_cameraPos.x, m_cameraPos.y, m_cameraPos.z, 1.0f);
    XMVECTOR target = XMVectorZero();
    XMVECTOR up     = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMStoreFloat4x4(&m_viewMatrix, XMMatrixLookAtLH(eye, target, up));

    float aspect = (m_backbufferHeight > 0) ? ((float)m_backbufferWidth / (float)m_backbufferHeight) : 1.0f;
    XMStoreFloat4x4(&m_projMatrix, XMMatrixPerspectiveFovLH(0.25f * XM_PI, aspect, 1.0f, 1000.0f));

    CompileShaders();
    CreateMesh();
    CreateConstantBuffer();
    CreateRootSignature();
    CreatePipeline();

    m_ready = true;
    return true;
}

void GraphicsEngine::Cleanup()
{
    if (m_commandQueue) WaitForGpu();

    if (m_constBuffer && m_mappedCBData)
    {
        m_constBuffer->Unmap(0, nullptr);
        m_mappedCBData = nullptr;
    }

    if (m_gpuFenceEvent)
    {
        CloseHandle(m_gpuFenceEvent);
        m_gpuFenceEvent = nullptr;
    }
}

bool GraphicsEngine::InitDevice()
{
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_d3dDevice));
    if (FAILED(hr))
    {
        ComPtr<IDXGIAdapter> warp;
        CheckHR(m_dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "EnumWarpAdapter");
        CheckHR(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_d3dDevice)), "D3D12CreateDevice (WARP)");
    }
    return true;
}

bool GraphicsEngine::InitCommandQueue()
{
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    CheckHR(m_d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)), "CreateCommandQueue");

    CheckHR(m_d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)), "CreateCommandAllocator");
    CheckHR(m_d3dDevice->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(),
        nullptr, IID_PPV_ARGS(&m_commandList)), "CreateCommandList");

    CheckHR(m_commandList->Close(), "CommandList Close (initial)");
    return true;
}

bool GraphicsEngine::InitSwapChain()
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferDesc.Width  = m_backbufferWidth;
    sd.BufferDesc.Height = m_backbufferHeight;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count   = 1;
    sd.SampleDesc.Quality = 0;
    sd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount  = kNumFrameBuffers;
    sd.OutputWindow = m_windowHandle;
    sd.Windowed     = TRUE;
    sd.SwapEffect   = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags        = 0;

    m_swapchain.Reset();

    CheckHR(m_dxgiFactory->CreateSwapChain(m_commandQueue.Get(), &sd,
        m_swapchain.GetAddressOf()), "CreateSwapChain");

    m_activeBuffer = 0;
    return true;
}

bool GraphicsEngine::InitHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.NumDescriptors = kNumFrameBuffers;
    rtvDesc.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    CheckHR(m_d3dDevice->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_renderTargetHeap)), "Create RTV Heap");

    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
    dsvDesc.NumDescriptors = 1;
    dsvDesc.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    CheckHR(m_d3dDevice->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&m_depthHeap)), "Create DSV Heap");

    return true;
}

bool GraphicsEngine::InitBackBufferViews()
{
    auto handle = m_renderTargetHeap->GetCPUDescriptorHandleForHeapStart();

    for (uint32_t i = 0; i < kNumFrameBuffers; ++i)
    {
        CheckHR(m_swapchain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i])), "SwapChain GetBuffer");
        m_d3dDevice->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, handle);
        handle.ptr += m_rtvHandleSize;
    }
    return true;
}

bool GraphicsEngine::InitDepthBuffer()
{
    m_depthBuffer.Reset();

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width            = m_backbufferWidth;
    desc.Height           = m_backbufferHeight;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.Format           = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.SampleDesc.Count = 1;
    desc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearVal{};
    clearVal.Format               = DXGI_FORMAT_D24_UNORM_S8_UINT;
    clearVal.DepthStencil.Depth   = 1.0f;
    clearVal.DepthStencil.Stencil = 0;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    CheckHR(m_d3dDevice->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COMMON,
        &clearVal,
        IID_PPV_ARGS(&m_depthBuffer)
    ), "CreateCommittedResource (DepthStencil)");

    CheckHR(m_commandAllocator->Reset(), "CmdAlloc Reset (DepthStencil)");
    CheckHR(m_commandList->Reset(m_commandAllocator.Get(), nullptr), "CmdList Reset (DepthStencil)");

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = m_depthBuffer.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &barrier);

    CheckHR(m_commandList->Close(), "CmdList Close (DepthStencil)");
    ID3D12CommandList* cmds[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, cmds);
    WaitForGpu();

    m_d3dDevice->CreateDepthStencilView(
        m_depthBuffer.Get(), nullptr,
        m_depthHeap->GetCPUDescriptorHandleForHeapStart()
    );

    return true;
}

void GraphicsEngine::HandleResize(uint32_t w, uint32_t h)
{
    if (!m_ready) return;
    if (w == 0 || h == 0) return;

    m_backbufferWidth  = w;
    m_backbufferHeight = h;

    WaitForGpu();

    for (auto& buf : m_backBuffers) buf.Reset();
    m_depthBuffer.Reset();

    CheckHR(m_swapchain->ResizeBuffers(
        kNumFrameBuffers, m_backbufferWidth, m_backbufferHeight,
        DXGI_FORMAT_R8G8B8A8_UNORM, 0), "SwapChain ResizeBuffers");

    m_activeBuffer = 0;

    InitBackBufferViews();
    InitDepthBuffer();

    m_vp      = { 0.0f, 0.0f, (float)m_backbufferWidth, (float)m_backbufferHeight, 0.0f, 1.0f };
    m_scissor = { 0, 0, (LONG)m_backbufferWidth, (LONG)m_backbufferHeight };

    float aspect = (m_backbufferHeight > 0) ? ((float)m_backbufferWidth / (float)m_backbufferHeight) : 1.0f;
    XMStoreFloat4x4(&m_projMatrix, XMMatrixPerspectiveFovLH(0.25f * XM_PI, aspect, 1.0f, 1000.0f));
}

D3D12_CPU_DESCRIPTOR_HANDLE GraphicsEngine::GetActiveRTV() const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_renderTargetHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (size_t)m_activeBuffer * m_rtvHandleSize;
    return h;
}

ID3D12Resource* GraphicsEngine::GetActiveBackBuffer() const
{
    return m_backBuffers[m_activeBuffer].Get();
}

void GraphicsEngine::RenderFrame()
{
    if (!m_ready) return;

    UploadConstants();

    CheckHR(m_commandAllocator->Reset(), "CmdAlloc Reset");
    CheckHR(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()), "CmdList Reset");

    m_commandList->RSSetViewports(1, &m_vp);
    m_commandList->RSSetScissorRects(1, &m_scissor);

    m_commandList->SetGraphicsRootSignature(m_rootSig.Get());

    D3D12_RESOURCE_BARRIER toRT{};
    toRT.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRT.Transition.pResource   = GetActiveBackBuffer();
    toRT.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    toRT.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toRT.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &toRT);

    auto rtv = GetActiveRTV();
    auto dsv = m_depthHeap->GetCPUDescriptorHandleForHeapStart();
    m_commandList->OMSetRenderTargets(1, &rtv, TRUE, &dsv);

    const float bgColor[4] = { 0.08f, 0.12f, 0.30f, 1.0f };
    m_commandList->ClearRenderTargetView(rtv, bgColor, 0, nullptr);
    m_commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // кубек
    ID3D12DescriptorHeap* heaps[] = { m_constantHeap.Get() };
    m_commandList->SetDescriptorHeaps(1, heaps);
    m_commandList->SetGraphicsRootDescriptorTable(0, m_constantHeap->GetGPUDescriptorHandleForHeapStart());

    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexView);
    m_commandList->IASetIndexBuffer(&m_indexView);
    m_commandList->DrawIndexedInstanced(m_numIndices, 1, 0, 0, 0);

    D3D12_RESOURCE_BARRIER toPresent = toRT;
    toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toPresent.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
    m_commandList->ResourceBarrier(1, &toPresent);

    CheckHR(m_commandList->Close(), "CmdList Close");

    ID3D12CommandList* cmds[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, cmds);

    CheckHR(m_swapchain->Present(1, 0), "SwapChain Present");
    m_activeBuffer = (m_activeBuffer + 1) % kNumFrameBuffers;

    WaitForGpu();
}

void GraphicsEngine::UpdateCamera(const DirectX::XMFLOAT3& pos, float yaw, float pitch)
{
    using namespace DirectX;

    m_cameraPos = pos;

    const float cy = cosf(yaw);
    const float sy = sinf(yaw);
    const float cp = cosf(pitch);
    const float sp = sinf(pitch);

    XMVECTOR forward = XMVector3Normalize(XMVectorSet(sy * cp, sp, cy * cp, 0.0f));
    XMVECTOR eye     = XMVectorSet(pos.x, pos.y, pos.z, 1.0f);
    XMVECTOR up      = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMStoreFloat4x4(&m_viewMatrix, XMMatrixLookToLH(eye, forward, up));
}

void GraphicsEngine::WaitForGpu()
{
    const uint64_t targetValue = ++m_gpuFenceCounter;
    CheckHR(m_commandQueue->Signal(m_gpuFence.Get(), targetValue), "Fence Signal");

    if (m_gpuFence->GetCompletedValue() < targetValue)
    {
        CheckHR(m_gpuFence->SetEventOnCompletion(targetValue, m_gpuFenceEvent), "Fence SetEventOnCompletion");
        WaitForSingleObject(m_gpuFenceEvent, INFINITE);
    }
}

static uint32_t AlignCBSize(uint32_t size)
{
    return (size + 255u) & ~255u;
}

static D3D12_HEAP_PROPERTIES MakeHeapProps(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES p{};
    p.Type                 = type;
    p.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    p.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    p.CreationNodeMask     = 1;
    p.VisibleNodeMask      = 1;
    return p;
}

static D3D12_RESOURCE_DESC MakeBufferDesc(UINT64 size)
{
    D3D12_RESOURCE_DESC d{};
    d.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width            = size;
    d.Height           = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels        = 1;
    d.Format           = DXGI_FORMAT_UNKNOWN;
    d.SampleDesc.Count = 1;
    d.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return d;
}

bool GraphicsEngine::CompileShaders()
{
    UINT flags = 0;
#if defined(_DEBUG)
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> errors;

    HRESULT hr = D3DCompileFromFile(
        L"CubeShader.hlsl",
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VertexMain",
        "vs_5_0",
        flags,
        0,
        &m_vertexShaderBlob,
        &errors
    );
    if (FAILED(hr))
    {
        if (errors) throw std::runtime_error((const char*)errors->GetBufferPointer());
        CheckHR(hr, "D3DCompileFromFile VS failed");
    }

    errors.Reset();
    hr = D3DCompileFromFile(
        L"CubeShader.hlsl",
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "PixelMain",
        "ps_5_0",
        flags,
        0,
        &m_pixelShaderBlob,
        &errors
    );
    if (FAILED(hr))
    {
        if (errors) throw std::runtime_error((const char*)errors->GetBufferPointer());
        CheckHR(hr, "D3DCompileFromFile PS failed");
    }

    m_vertexLayout[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
                           D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    m_vertexLayout[1] = { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
                           D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };

    return true;
}

bool GraphicsEngine::CreateMesh()
{
    const float hs = 0.8f;

    const std::array<MeshVertex, 24> verts =
    {
        MeshVertex{{+hs,+hs,-hs},{+1,0,0}}, MeshVertex{{+hs,+hs,+hs},{+1,0,0}}, MeshVertex{{+hs,-hs,+hs},{+1,0,0}}, MeshVertex{{+hs,-hs,-hs},{+1,0,0}},
        MeshVertex{{-hs,+hs,+hs},{-1,0,0}}, MeshVertex{{-hs,+hs,-hs},{-1,0,0}}, MeshVertex{{-hs,-hs,-hs},{-1,0,0}}, MeshVertex{{-hs,-hs,+hs},{-1,0,0}},
        MeshVertex{{-hs,+hs,-hs},{0,+1,0}}, MeshVertex{{+hs,+hs,-hs},{0,+1,0}}, MeshVertex{{+hs,+hs,+hs},{0,+1,0}}, MeshVertex{{-hs,+hs,+hs},{0,+1,0}},
        MeshVertex{{-hs,-hs,+hs},{0,-1,0}}, MeshVertex{{+hs,-hs,+hs},{0,-1,0}}, MeshVertex{{+hs,-hs,-hs},{0,-1,0}}, MeshVertex{{-hs,-hs,-hs},{0,-1,0}},
        MeshVertex{{+hs,+hs,+hs},{0,0,+1}}, MeshVertex{{-hs,+hs,+hs},{0,0,+1}}, MeshVertex{{-hs,-hs,+hs},{0,0,+1}}, MeshVertex{{+hs,-hs,+hs},{0,0,+1}},
        MeshVertex{{-hs,+hs,-hs},{0,0,-1}}, MeshVertex{{+hs,+hs,-hs},{0,0,-1}}, MeshVertex{{+hs,-hs,-hs},{0,0,-1}}, MeshVertex{{-hs,-hs,-hs},{0,0,-1}},
    };

    const std::array<uint16_t, 36> idx =
    {
        0,1,2, 0,2,3,
        4,5,6, 4,6,7,
        8,9,10, 8,10,11,
        12,13,14, 12,14,15,
        16,17,18, 16,18,19,
        20,21,22, 20,22,23
    };

    m_numIndices = (uint32_t)idx.size();

    const UINT64 vbSize = (UINT64)verts.size() * sizeof(MeshVertex);
    const UINT64 ibSize = (UINT64)idx.size() * sizeof(uint16_t);

    D3D12_HEAP_PROPERTIES defaultHP = MakeHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_HEAP_PROPERTIES uploadHP  = MakeHeapProps(D3D12_HEAP_TYPE_UPLOAD);

    D3D12_RESOURCE_DESC vbDesc = MakeBufferDesc(vbSize);
    D3D12_RESOURCE_DESC ibDesc = MakeBufferDesc(ibSize);

    CheckHR(m_d3dDevice->CreateCommittedResource(
        &defaultHP, D3D12_HEAP_FLAG_NONE,
        &vbDesc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(&m_vbGpu)
    ), "Create vertex buffer (default)");

    CheckHR(m_d3dDevice->CreateCommittedResource(
        &defaultHP, D3D12_HEAP_FLAG_NONE,
        &ibDesc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(&m_ibGpu)
    ), "Create index buffer (default)");

    ComPtr<ID3D12Resource> vbUpload;
    ComPtr<ID3D12Resource> ibUpload;

    CheckHR(m_d3dDevice->CreateCommittedResource(
        &uploadHP, D3D12_HEAP_FLAG_NONE,
        &vbDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&vbUpload)
    ), "Create vertex buffer (upload)");

    CheckHR(m_d3dDevice->CreateCommittedResource(
        &uploadHP, D3D12_HEAP_FLAG_NONE,
        &ibDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&ibUpload)
    ), "Create index buffer (upload)");

    {
        void* ptr = nullptr;
        D3D12_RANGE noRead{ 0, 0 };
        CheckHR(vbUpload->Map(0, &noRead, &ptr), "Map vertex upload");
        std::memcpy(ptr, verts.data(), (size_t)vbSize);
        vbUpload->Unmap(0, nullptr);
    }
    {
        void* ptr = nullptr;
        D3D12_RANGE noRead{ 0, 0 };
        CheckHR(ibUpload->Map(0, &noRead, &ptr), "Map index upload");
        std::memcpy(ptr, idx.data(), (size_t)ibSize);
        ibUpload->Unmap(0, nullptr);
    }

    CheckHR(m_commandAllocator->Reset(), "CmdAlloc Reset (Geometry)");
    CheckHR(m_commandList->Reset(m_commandAllocator.Get(), nullptr), "CmdList Reset (Geometry)");

    m_commandList->CopyBufferRegion(m_vbGpu.Get(), 0, vbUpload.Get(), 0, vbSize);
    m_commandList->CopyBufferRegion(m_ibGpu.Get(), 0, ibUpload.Get(), 0, ibSize);

    D3D12_RESOURCE_BARRIER barriers[2]{};

    barriers[0].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource   = m_vbGpu.Get();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    barriers[1].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource   = m_ibGpu.Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_INDEX_BUFFER;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    m_commandList->ResourceBarrier(2, barriers);

    CheckHR(m_commandList->Close(), "CmdList Close (Geometry upload)");
    ID3D12CommandList* cmds[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, cmds);
    WaitForGpu();

    m_vertexView.BufferLocation = m_vbGpu->GetGPUVirtualAddress();
    m_vertexView.StrideInBytes  = sizeof(MeshVertex);
    m_vertexView.SizeInBytes    = (UINT)vbSize;

    m_indexView.BufferLocation = m_ibGpu->GetGPUVirtualAddress();
    m_indexView.SizeInBytes    = (UINT)ibSize;
    m_indexView.Format         = DXGI_FORMAT_R16_UINT;

    return true;
}

bool GraphicsEngine::CreateConstantBuffer()
{
    m_cbAlignedSize = AlignCBSize(sizeof(FrameConstants));

    D3D12_HEAP_PROPERTIES uploadHP = MakeHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC cbDesc = MakeBufferDesc((UINT64)m_cbAlignedSize);

    CheckHR(m_d3dDevice->CreateCommittedResource(
        &uploadHP, D3D12_HEAP_FLAG_NONE,
        &cbDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&m_constBuffer)), "Create Constant Buffer");

    D3D12_RANGE noRead{ 0, 0 };
    CheckHR(m_constBuffer->Map(0, &noRead, reinterpret_cast<void**>(&m_mappedCBData)),
                  "Map Constant Buffer");

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.NumDescriptors = 1;
    heapDesc.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    CheckHR(m_d3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_constantHeap)), "Create CBV heap");

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
    cbvDesc.BufferLocation = m_constBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes    = m_cbAlignedSize;
    m_d3dDevice->CreateConstantBufferView(&cbvDesc, m_constantHeap->GetCPUDescriptorHandleForHeapStart());

    UploadConstants();
    return true;
}

bool GraphicsEngine::CreateRootSignature()
{
    D3D12_DESCRIPTOR_RANGE cbvRange{};
    cbvRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    cbvRange.NumDescriptors     = 1;
    cbvRange.BaseShaderRegister = 0;
    cbvRange.RegisterSpace      = 0;
    cbvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER param{};
    param.ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param.DescriptorTable.NumDescriptorRanges = 1;
    param.DescriptorTable.pDescriptorRanges   = &cbvRange;
    param.ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = 1;
    rsDesc.pParameters   = &param;
    rsDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
    if (FAILED(hr))
    {
        if (errors) throw std::runtime_error((const char*)errors->GetBufferPointer());
        CheckHR(hr, "D3D12SerializeRootSignature failed");
    }

    CheckHR(m_d3dDevice->CreateRootSignature(0,
        serialized->GetBufferPointer(), serialized->GetBufferSize(),
        IID_PPV_ARGS(&m_rootSig)), "CreateRootSignature");

    return true;
}

bool GraphicsEngine::CreatePipeline()
{
    D3D12_RASTERIZER_DESC rast{};
    rast.FillMode              = D3D12_FILL_MODE_SOLID;
    rast.CullMode              = D3D12_CULL_MODE_NONE;
    rast.FrontCounterClockwise = TRUE;
    rast.DepthBias             = D3D12_DEFAULT_DEPTH_BIAS;
    rast.DepthBiasClamp        = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rast.SlopeScaledDepthBias  = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rast.DepthClipEnable       = TRUE;

    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable    = TRUE;
    ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;
    ds.StencilEnable  = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature    = m_rootSig.Get();
    psoDesc.VS                = { m_vertexShaderBlob->GetBufferPointer(), m_vertexShaderBlob->GetBufferSize() };
    psoDesc.PS                = { m_pixelShaderBlob->GetBufferPointer(), m_pixelShaderBlob->GetBufferSize() };
    psoDesc.BlendState        = blend;
    psoDesc.RasterizerState   = rast;
    psoDesc.DepthStencilState = ds;
    psoDesc.SampleMask        = UINT_MAX;

    psoDesc.InputLayout           = { m_vertexLayout, 2 };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0]    = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat        = DXGI_FORMAT_D24_UNORM_S8_UINT;

    psoDesc.SampleDesc.Count   = 1;
    psoDesc.SampleDesc.Quality = 0;

    CheckHR(m_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)), "CreateGraphicsPipelineState");
    return true;
}

void GraphicsEngine::UploadConstants()
{
    if (!m_mappedCBData) return;

    FrameConstants fc{};

    XMMATRIX world = XMLoadFloat4x4(&m_worldMatrix);
    XMMATRIX view  = XMLoadFloat4x4(&m_viewMatrix);
    XMMATRIX proj  = XMLoadFloat4x4(&m_projMatrix);
    XMMATRIX mvp   = world * view * proj;

    XMStoreFloat4x4(&fc.ModelMatrix, XMMatrixTranspose(world));
    XMStoreFloat4x4(&fc.MVP,         XMMatrixTranspose(mvp));

    fc.CameraPos = m_cameraPos;
    fc.SunDir    = m_sunDirection;
    DirectX::XMVECTOR L = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&m_sunDirection));
    DirectX::XMStoreFloat3(&fc.SunDir, L);

    fc.AmbientColor  = DirectX::XMFLOAT4(0.10f, 0.10f, 0.10f, 1.0f);
    fc.DiffuseColor  = DirectX::XMFLOAT4(0.85f, 0.85f, 0.85f, 1.0f);
    fc.SpecularColor = DirectX::XMFLOAT4(0.85f, 0.85f, 0.85f, 1.0f);
    fc.Shininess     = 48.0f;

    std::memcpy(m_mappedCBData, &fc, sizeof(fc));
}