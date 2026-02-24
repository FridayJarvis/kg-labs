#include "../include/D3D12Context.h"
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <array>
#include <cmath>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <wincodec.h>
#include <objbase.h>
#include <DirectXMath.h>
using namespace DirectX;

using Microsoft::WRL::ComPtr;

// ─────────────────────────────────────────────────────────────────────────────
//  Утилиты путей
// ─────────────────────────────────────────────────────────────────────────────
static std::wstring ToWStringAscii(const std::string& s)
{
    return std::wstring(s.begin(), s.end());
}

static std::string Dirname(const std::string& path)
{
    size_t p = path.find_last_of("/\\");
    return (p == std::string::npos) ? std::string() : path.substr(0, p + 1);
}

static std::string JoinPath(const std::string& a, const std::string& b)
{
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == '/' || a.back() == '\\') return a + b;
    return a + "/" + b;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Загрузка изображений через WIC
// ─────────────────────────────────────────────────────────────────────────────
struct WicImage
{
    uint32_t            width = 0;
    uint32_t            height = 0;
    std::vector<uint8_t> bgra;
};

static bool LoadImageWIC(const std::wstring& filePath, WicImage& out)
{
    static bool s_comInitTried = false;
    if (!s_comInitTried)
    {
        s_comInitTried = true;
        HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        (void)hrCo;
    }

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(hr))
    {
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory));
        if (FAILED(hr)) return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(filePath.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) return false;

    UINT w = 0, h = 0;
    frame->GetSize(&w, &h);
    if (w == 0 || h == 0) return false;

    ComPtr<IWICFormatConverter> conv;
    hr = factory->CreateFormatConverter(&conv);
    if (FAILED(hr)) return false;

    hr = conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return false;

    out.width = (uint32_t)w;
    out.height = (uint32_t)h;
    out.bgra.resize((size_t)w * (size_t)h * 4);

    const UINT stride = w * 4;
    hr = conv->CopyPixels(nullptr, stride, (UINT)out.bgra.size(), out.bgra.data());
    return SUCCEEDED(hr);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Хелперы для создания D3D12-ресурсов
// ─────────────────────────────────────────────────────────────────────────────
static D3D12_RESOURCE_DESC Tex2DDesc(uint32_t w, uint32_t h, DXGI_FORMAT fmt)
{
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w;
    d.Height = h;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.Format = fmt;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    d.Flags = D3D12_RESOURCE_FLAG_NONE;
    return d;
}

static void CheckHR(HRESULT hr, const char* msg)
{
    if (FAILED(hr))
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s (hr=0x%08X)", msg, (unsigned)hr);
        throw std::runtime_error(buf);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Парсер OBJ
// ─────────────────────────────────────────────────────────────────────────────
struct ObjKey
{
    int p = -1, t = -1, n = -1;
    bool operator==(const ObjKey& o) const { return p == o.p && t == o.t && n == o.n; }
};
struct ObjKeyHash
{
    size_t operator()(const ObjKey& k) const noexcept
    {
        return (size_t)k.p * 73856093u ^ (size_t)k.t * 19349663u ^ (size_t)k.n * 83492791u;
    }
};

static int FixObjIndex(int idx, int size)
{
    if (idx > 0) return idx - 1;
    if (idx < 0) return size + idx;
    return -1;
}

static void ParseFaceToken(const std::string& tok, int& p, int& t, int& n)
{
    p = t = n = 0;
    size_t s1 = tok.find('/');
    if (s1 == std::string::npos) { p = std::stoi(tok); return; }

    if (s1 > 0) p = std::stoi(tok.substr(0, s1));

    size_t s2 = tok.find('/', s1 + 1);
    if (s2 == std::string::npos)
    {
        if (s1 + 1 < tok.size()) t = std::stoi(tok.substr(s1 + 1));
        return;
    }

    if (s2 > s1 + 1) t = std::stoi(tok.substr(s1 + 1, s2 - (s1 + 1)));
    if (s2 + 1 < tok.size()) n = std::stoi(tok.substr(s2 + 1));
}

static std::unordered_map<std::string, std::string> LoadMtlMapKd(const std::string& mtlPath)
{
    std::unordered_map<std::string, std::string> out;
    std::ifstream f(mtlPath);
    if (!f.is_open()) return out;

    std::string baseDir = Dirname(mtlPath);
    std::string line, cur;

    while (std::getline(f, line))
    {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;
        if (cmd == "newmtl")
        {
            ss >> cur;
        }
        else if (cmd == "map_Kd" && !cur.empty())
        {
            std::string tok, last;
            while (ss >> tok) last = tok;
            if (!last.empty())
                out[cur] = JoinPath(baseDir, last);
        }
    }
    return out;
}

struct ObjLoaded
{
    std::vector<GraphicsEngine::MeshVertex> vertices;
    std::vector<uint32_t>                  indices;

    struct Group { uint32_t start = 0; uint32_t count = 0; std::string mtl; };
    std::vector<Group> groups;

    std::unordered_map<std::string, std::string> mtlToDiffuse;
};

static bool LoadObjWithGroups(const std::string& objPath, ObjLoaded& out)
{
    std::ifstream file(objPath);
    if (!file.is_open()) return false;

    std::string baseDir = Dirname(objPath);

    std::vector<XMFLOAT3> positions, normals;
    std::vector<XMFLOAT2> texcoords;
    positions.reserve(200000);
    normals.reserve(200000);
    texcoords.reserve(200000);

    std::unordered_map<ObjKey, uint32_t, ObjKeyHash> uniqueMap;

    std::string mtlLib, curMtl;

    auto switchMaterial = [&](const std::string& newMtl)
        {
            if (out.groups.empty())
            {
                curMtl = newMtl;
                ObjLoaded::Group g{};
                g.start = (uint32_t)out.indices.size();
                g.mtl = curMtl;
                out.groups.push_back(g);
                return;
            }
            if (curMtl == newMtl) return;

            out.groups.back().count = (uint32_t)out.indices.size() - out.groups.back().start;

            curMtl = newMtl;
            ObjLoaded::Group g{};
            g.start = (uint32_t)out.indices.size();
            g.mtl = curMtl;
            out.groups.push_back(g);
        };

    auto beginGroupIfNeeded = [&]()
        {
            if (out.groups.empty())
            {
                ObjLoaded::Group g{};
                g.start = (uint32_t)out.indices.size();
                g.mtl = curMtl;
                out.groups.push_back(g);
            }
        };

    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#') continue;

        if (line.rfind("mtllib ", 0) == 0)
        {
            std::istringstream ss(line);
            std::string cmd, name;
            ss >> cmd >> name;
            mtlLib = JoinPath(baseDir, name);
            continue;
        }
        if (line.rfind("usemtl ", 0) == 0)
        {
            std::istringstream ss(line);
            std::string cmd, name;
            ss >> cmd >> name;
            switchMaterial(name);
            continue;
        }
        if (line.size() > 2 && line[0] == 'v' && line[1] == ' ')
        {
            std::istringstream ss(line);
            char v; float x, y, z;
            ss >> v >> x >> y >> z;
            positions.push_back({ x, y, z });
            continue;
        }
        if (line.size() > 3 && line[0] == 'v' && line[1] == 'n' && line[2] == ' ')
        {
            std::istringstream ss(line);
            std::string vn; float x, y, z;
            ss >> vn >> x >> y >> z;
            normals.push_back({ x, y, z });
            continue;
        }
        if (line.size() > 3 && line[0] == 'v' && line[1] == 't' && line[2] == ' ')
        {
            std::istringstream ss(line);
            std::string vt; float u, v;
            ss >> vt >> u >> v;
            texcoords.push_back({ u, 1.0f - v });
            continue;
        }
        if (line.size() > 2 && line[0] == 'f' && line[1] == ' ')
        {
            beginGroupIfNeeded();

            std::istringstream ss(line);
            char fch; ss >> fch;

            std::vector<uint32_t> face;
            face.reserve(8);

            std::string tok;
            while (ss >> tok)
            {
                int pRaw = 0, tRaw = 0, nRaw = 0;
                ParseFaceToken(tok, pRaw, tRaw, nRaw);

                int p = FixObjIndex(pRaw, (int)positions.size());
                int t = FixObjIndex(tRaw, (int)texcoords.size());
                int n = FixObjIndex(nRaw, (int)normals.size());

                if (p < 0) continue;

                ObjKey key{ p, t, n };
                auto it = uniqueMap.find(key);
                if (it == uniqueMap.end())
                {
                    GraphicsEngine::MeshVertex mv{};
                    mv.Position = positions[p];
                    mv.Normal = (n >= 0) ? normals[n] : XMFLOAT3(0, 1, 0);
                    mv.TexC = (t >= 0) ? texcoords[t] : XMFLOAT2(0, 0);

                    uint32_t idx = (uint32_t)out.vertices.size();
                    out.vertices.push_back(mv);
                    uniqueMap.emplace(key, idx);
                    face.push_back(idx);
                }
                else face.push_back(it->second);
            }

            if (face.size() >= 3)
            {
                for (size_t i = 1; i + 1 < face.size(); ++i)
                {
                    out.indices.push_back(face[0]);
                    out.indices.push_back(face[i]);
                    out.indices.push_back(face[i + 1]);
                }
            }
        }
    }

    if (!out.groups.empty())
        out.groups.back().count = (uint32_t)out.indices.size() - out.groups.back().start;

    if (!mtlLib.empty())
        out.mtlToDiffuse = LoadMtlMapKd(mtlLib);

    return !out.vertices.empty() && !out.indices.empty();
}

// ─────────────────────────────────────────────────────────────────────────────
//  GraphicsEngine
// ─────────────────────────────────────────────────────────────────────────────
GraphicsEngine::~GraphicsEngine()
{
    Cleanup();
}

bool GraphicsEngine::Setup(HWND hWnd, uint32_t w, uint32_t h)
{
    m_ready = false;

    m_windowHandle = hWnd;
    m_backbufferWidth = w;
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
    m_cbvSrvUavHandleSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    InitHeaps();
    InitBackBufferViews();
    InitDepthBuffer();

    m_vp = { 0.0f, 0.0f, (float)m_backbufferWidth, (float)m_backbufferHeight, 0.0f, 1.0f };
    m_scissor = { 0, 0, (LONG)m_backbufferWidth, (LONG)m_backbufferHeight };

    // Sponza очень большой в оригинале — масштабируем в 0.01
    XMStoreFloat4x4(&m_worldMatrix, XMMatrixScaling(0.01f, 0.01f, 0.01f));

    XMVECTOR eye = XMVectorSet(m_cameraPos.x, m_cameraPos.y, m_cameraPos.z, 1.0f);
    XMVECTOR target = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMStoreFloat4x4(&m_viewMatrix, XMMatrixLookAtLH(eye, target, up));

    float aspect = (m_backbufferHeight > 0)
        ? ((float)m_backbufferWidth / (float)m_backbufferHeight) : 1.0f;
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
        CheckHR(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_d3dDevice)),
            "D3D12CreateDevice (WARP)");
    }
    return true;
}

bool GraphicsEngine::InitCommandQueue()
{
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    CheckHR(m_d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)), "CreateCommandQueue");

    CheckHR(m_d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&m_commandAllocator)), "CreateCommandAllocator");
    CheckHR(m_d3dDevice->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(),
        nullptr, IID_PPV_ARGS(&m_commandList)), "CreateCommandList");
    CheckHR(m_commandList->Close(), "CommandList Close (initial)");
    return true;
}

bool GraphicsEngine::InitSwapChain()
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferDesc.Width = m_backbufferWidth;
    sd.BufferDesc.Height = m_backbufferHeight;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = kNumFrameBuffers;
    sd.OutputWindow = m_windowHandle;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = 0;

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
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    CheckHR(m_d3dDevice->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_renderTargetHeap)),
        "Create RTV Heap");

    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
    dsvDesc.NumDescriptors = 1;
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    CheckHR(m_d3dDevice->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&m_depthHeap)),
        "Create DSV Heap");

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
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = m_backbufferWidth;
    desc.Height = m_backbufferHeight;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearVal{};
    clearVal.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    clearVal.DepthStencil.Depth = 1.0f;
    clearVal.DepthStencil.Stencil = 0;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    CheckHR(m_d3dDevice->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COMMON, &clearVal,
        IID_PPV_ARGS(&m_depthBuffer)), "CreateCommittedResource (DepthStencil)");

    CheckHR(m_commandAllocator->Reset(), "CmdAlloc Reset (DepthStencil)");
    CheckHR(m_commandList->Reset(m_commandAllocator.Get(), nullptr), "CmdList Reset (DepthStencil)");

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_depthBuffer.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &barrier);

    CheckHR(m_commandList->Close(), "CmdList Close (DepthStencil)");
    ID3D12CommandList* cmds[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, cmds);
    WaitForGpu();

    m_d3dDevice->CreateDepthStencilView(
        m_depthBuffer.Get(), nullptr,
        m_depthHeap->GetCPUDescriptorHandleForHeapStart());

    return true;
}

void GraphicsEngine::HandleResize(uint32_t w, uint32_t h)
{
    if (!m_ready) return;
    if (w == 0 || h == 0) return;

    m_backbufferWidth = w;
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

    m_vp = { 0.0f, 0.0f, (float)m_backbufferWidth, (float)m_backbufferHeight, 0.0f, 1.0f };
    m_scissor = { 0, 0, (LONG)m_backbufferWidth, (LONG)m_backbufferHeight };

    float aspect = (m_backbufferHeight > 0)
        ? ((float)m_backbufferWidth / (float)m_backbufferHeight) : 1.0f;
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
    toRT.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRT.Transition.pResource = GetActiveBackBuffer();
    toRT.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    toRT.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toRT.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &toRT);

    auto rtv = GetActiveRTV();
    auto dsv = m_depthHeap->GetCPUDescriptorHandleForHeapStart();
    m_commandList->OMSetRenderTargets(1, &rtv, TRUE, &dsv);

    const float bgColor[4] = { 0.08f, 0.12f, 0.30f, 1.0f };
    m_commandList->ClearRenderTargetView(rtv, bgColor, 0, nullptr);
    m_commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    ID3D12DescriptorHeap* heaps[] = { m_constantHeap.Get() };
    m_commandList->SetDescriptorHeaps(1, heaps);

    auto base = m_constantHeap->GetGPUDescriptorHandleForHeapStart();

    // slot 0 = CBV
    m_commandList->SetGraphicsRootDescriptorTable(0, base);

    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexView);
    m_commandList->IASetIndexBuffer(&m_indexView);

    // Per-материальный рендер с переключением текстуры
    for (const auto& di : m_drawItems)
    {
        D3D12_GPU_DESCRIPTOR_HANDLE srv = base;
        srv.ptr += (UINT64)di.TextureSrvIndex * (UINT64)m_cbvSrvUavHandleSize;
        m_commandList->SetGraphicsRootDescriptorTable(1, srv);

        m_commandList->DrawIndexedInstanced(di.IndexCount, 1, di.StartIndexLocation, 0, 0);
    }

    D3D12_RESOURCE_BARRIER toPresent = toRT;
    toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
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
    m_cameraPos = pos;

    const float cy = cosf(yaw), sy = sinf(yaw);
    const float cp = cosf(pitch), sp = sinf(pitch);

    XMVECTOR forward = XMVector3Normalize(XMVectorSet(sy * cp, sp, cy * cp, 0.0f));
    XMVECTOR eye = XMVectorSet(pos.x, pos.y, pos.z, 1.0f);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMStoreFloat4x4(&m_viewMatrix, XMMatrixLookToLH(eye, forward, up));
}

void GraphicsEngine::WaitForGpu()
{
    const uint64_t target = ++m_gpuFenceCounter;
    CheckHR(m_commandQueue->Signal(m_gpuFence.Get(), target), "Fence Signal");

    if (m_gpuFence->GetCompletedValue() < target)
    {
        CheckHR(m_gpuFence->SetEventOnCompletion(target, m_gpuFenceEvent),
            "Fence SetEventOnCompletion");
        WaitForSingleObject(m_gpuFenceEvent, INFINITE);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Вспомогательные функции
// ─────────────────────────────────────────────────────────────────────────────
static uint32_t AlignCBSize(uint32_t size) { return (size + 255u) & ~255u; }

static D3D12_HEAP_PROPERTIES MakeHeapProps(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES p{};
    p.Type = type;
    p.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    p.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    p.CreationNodeMask = 1;
    p.VisibleNodeMask = 1;
    return p;
}

static D3D12_RESOURCE_DESC MakeBufferDesc(UINT64 size)
{
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = size;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.Format = DXGI_FORMAT_UNKNOWN;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return d;
}

// ─────────────────────────────────────────────────────────────────────────────
//  CompileShaders
// ─────────────────────────────────────────────────────────────────────────────
bool GraphicsEngine::CompileShaders()
{
    UINT flags = 0;
#if defined(_DEBUG)
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> errors;

    HRESULT hr = D3DCompileFromFile(
        L"CubeShader.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VertexMain", "vs_5_0", flags, 0, &m_vertexShaderBlob, &errors);
    if (FAILED(hr))
    {
        if (errors) throw std::runtime_error((const char*)errors->GetBufferPointer());
        CheckHR(hr, "D3DCompileFromFile VS failed");
    }

    errors.Reset();
    hr = D3DCompileFromFile(
        L"CubeShader.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "PixelMain", "ps_5_0", flags, 0, &m_pixelShaderBlob, &errors);
    if (FAILED(hr))
    {
        if (errors) throw std::runtime_error((const char*)errors->GetBufferPointer());
        CheckHR(hr, "D3DCompileFromFile PS failed");
    }

    m_vertexLayout[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0,
                           D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    m_vertexLayout[1] = { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
                           D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    m_vertexLayout[2] = { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24,
                           D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  CreateMesh — загружаем Sponza из OBJ
// ─────────────────────────────────────────────────────────────────────────────
bool GraphicsEngine::CreateMesh()
{
    // Путь к OBJ: ../../ — два уровня вверх из директории сборки (build/Debug → корень проекта)
    const std::string objPath = "sponza.obj";

    ObjLoaded model{};
    if (!LoadObjWithGroups(objPath, model))
        throw std::runtime_error("Failed to load OBJ: " + objPath);

    m_numIndices = (uint32_t)model.indices.size();

    // ── собираем список уникальных путей к текстурам ──────────────────────────
    std::unordered_map<std::string, uint32_t> pathToKey;
    std::vector<std::string>                  uniquePaths;

    auto getOrAddKey = [&](const std::string& path) -> uint32_t
        {
            if (path.empty()) return 0;   // → белый fallback
            auto it = pathToKey.find(path);
            if (it != pathToKey.end()) return it->second;
            uint32_t key = 1u + (uint32_t)uniquePaths.size();
            pathToKey.emplace(path, key);
            uniquePaths.push_back(path);
            return key;
        };

    std::vector<uint32_t> groupKey;
    groupKey.reserve(model.groups.size());

    for (const auto& g : model.groups)
    {
        std::string diffuse;
        if (!g.mtl.empty())
        {
            auto it = model.mtlToDiffuse.find(g.mtl);
            if (it != model.mtlToDiffuse.end()) diffuse = it->second;
        }
        groupKey.push_back(getOrAddKey(diffuse));
    }

    // ── GPU-буферы для вершин и индексов ──────────────────────────────────────
    const UINT64 vbSize = (UINT64)model.vertices.size() * sizeof(MeshVertex);
    const UINT64 ibSize = (UINT64)model.indices.size() * sizeof(uint32_t);

    D3D12_HEAP_PROPERTIES defaultHP = MakeHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_HEAP_PROPERTIES uploadHP = MakeHeapProps(D3D12_HEAP_TYPE_UPLOAD);

    D3D12_RESOURCE_DESC vbDesc = MakeBufferDesc(vbSize);
    D3D12_RESOURCE_DESC ibDesc = MakeBufferDesc(ibSize);

    CheckHR(m_d3dDevice->CreateCommittedResource(
        &defaultHP, D3D12_HEAP_FLAG_NONE, &vbDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_vbGpu)),
        "Create VB (default)");

    CheckHR(m_d3dDevice->CreateCommittedResource(
        &defaultHP, D3D12_HEAP_FLAG_NONE, &ibDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_ibGpu)),
        "Create IB (default)");

    ComPtr<ID3D12Resource> vbUpload, ibUpload;

    CheckHR(m_d3dDevice->CreateCommittedResource(
        &uploadHP, D3D12_HEAP_FLAG_NONE, &vbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vbUpload)),
        "Create VB (upload)");

    CheckHR(m_d3dDevice->CreateCommittedResource(
        &uploadHP, D3D12_HEAP_FLAG_NONE, &ibDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&ibUpload)),
        "Create IB (upload)");

    {
        void* ptr; D3D12_RANGE nr{ 0,0 };
        CheckHR(vbUpload->Map(0, &nr, &ptr), "Map VB");
        std::memcpy(ptr, model.vertices.data(), (size_t)vbSize);
        vbUpload->Unmap(0, nullptr);
    }

    {
        void* ptr; D3D12_RANGE nr{ 0,0 };
        CheckHR(ibUpload->Map(0, &nr, &ptr), "Map IB");
        std::memcpy(ptr, model.indices.data(), (size_t)ibSize);
        ibUpload->Unmap(0, nullptr);
    }

    // ── создаём GPU-текстуры (ещё до записи в командный список) ──────────────
    m_textures.clear();
    m_textures.reserve(1 + uniquePaths.size());

    // индекс 0 — белый 1×1 пиксель
    {
        D3D12_RESOURCE_DESC td = Tex2DDesc(1, 1, DXGI_FORMAT_B8G8R8A8_UNORM);
        ComPtr<ID3D12Resource> tex;
        CheckHR(m_d3dDevice->CreateCommittedResource(
            &defaultHP, D3D12_HEAP_FLAG_NONE, &td,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex)),
            "Create white tex");
        m_textures.push_back(tex);
    }

    std::vector<bool> loadedOk;
    loadedOk.reserve(uniquePaths.size());

    for (const auto& p : uniquePaths)
    {
        WicImage img{};
        bool ok = LoadImageWIC(ToWStringAscii(p), img);
        loadedOk.push_back(ok);

        if (!ok)
        {
            m_textures.push_back(m_textures[0]);   // fallback
            continue;
        }

        D3D12_RESOURCE_DESC td = Tex2DDesc(img.width, img.height, DXGI_FORMAT_B8G8R8A8_UNORM);
        ComPtr<ID3D12Resource> tex;
        CheckHR(m_d3dDevice->CreateCommittedResource(
            &defaultHP, D3D12_HEAP_FLAG_NONE, &td,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex)),
            "Create texture default");
        m_textures.push_back(tex);
    }

    // ── записываем всё в командный список ────────────────────────────────────
    CheckHR(m_commandAllocator->Reset(), "CmdAlloc Reset (CreateMesh)");
    CheckHR(m_commandList->Reset(m_commandAllocator.Get(), nullptr), "CmdList Reset (CreateMesh)");

    m_commandList->CopyBufferRegion(m_vbGpu.Get(), 0, vbUpload.Get(), 0, vbSize);
    m_commandList->CopyBufferRegion(m_ibGpu.Get(), 0, ibUpload.Get(), 0, ibSize);

    {
        D3D12_RESOURCE_BARRIER b[2]{};
        b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b[0].Transition.pResource = m_vbGpu.Get();
        b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        b[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b[1].Transition.pResource = m_ibGpu.Get();
        b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;
        b[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        m_commandList->ResourceBarrier(2, b);
    }

    // Аплоад текстур (держим upload-буферы живыми до ExecuteCommandLists)
    std::vector<ComPtr<ID3D12Resource>> texUploads;
    texUploads.reserve(m_textures.size());

    // ── белый 1×1 ────────────────────────────────────────────────────────────
    {
        const uint8_t white[4] = { 255,255,255,255 };
        D3D12_RESOURCE_DESC td = Tex2DDesc(1, 1, DXGI_FORMAT_B8G8R8A8_UNORM);

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT nr = 0; UINT64 rb = 0, tb = 0;
        m_d3dDevice->GetCopyableFootprints(&td, 0, 1, 0, &fp, &nr, &rb, &tb);

        D3D12_RESOURCE_DESC upDescW = MakeBufferDesc(tb);
        ComPtr<ID3D12Resource> upload;
        CheckHR(m_d3dDevice->CreateCommittedResource(
            &uploadHP, D3D12_HEAP_FLAG_NONE, &upDescW,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)),
            "Create white upload");

        void* mapped; D3D12_RANGE rr{ 0,0 };
        CheckHR(upload->Map(0, &rr, &mapped), "Map white upload");
        std::memcpy(mapped, white, 4);
        upload->Unmap(0, nullptr);

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = m_textures[0].Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = upload.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = fp;

        m_commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        D3D12_RESOURCE_BARRIER tb2{};
        tb2.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        tb2.Transition.pResource = m_textures[0].Get();
        tb2.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        tb2.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        tb2.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &tb2);

        texUploads.push_back(upload);
    }

    // ── реальные текстуры ────────────────────────────────────────────────────
    for (size_t i = 0; i < uniquePaths.size(); ++i)
    {
        if (!loadedOk[i]) continue;

        WicImage img{};
        if (!LoadImageWIC(ToWStringAscii(uniquePaths[i]), img)) continue;

        D3D12_RESOURCE_DESC td = Tex2DDesc(img.width, img.height, DXGI_FORMAT_B8G8R8A8_UNORM);

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT nr = 0; UINT64 rb = 0, tb = 0;
        m_d3dDevice->GetCopyableFootprints(&td, 0, 1, 0, &fp, &nr, &rb, &tb);

        D3D12_RESOURCE_DESC upDescT = MakeBufferDesc(tb);
        ComPtr<ID3D12Resource> upload;
        CheckHR(m_d3dDevice->CreateCommittedResource(
            &uploadHP, D3D12_HEAP_FLAG_NONE, &upDescT,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)),
            "Create tex upload");

        void* mapped; D3D12_RANGE rr{ 0,0 };
        CheckHR(upload->Map(0, &rr, &mapped), "Map tex upload");

        const uint8_t* srcPx = img.bgra.data();
        uint8_t* dstPx = (uint8_t*)mapped;
        const uint32_t srcStride = img.width * 4;
        const uint32_t dstStride = fp.Footprint.RowPitch;

        for (uint32_t y = 0; y < img.height; ++y)
            std::memcpy(dstPx + (size_t)y * dstStride, srcPx + (size_t)y * srcStride, srcStride);

        upload->Unmap(0, nullptr);

        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource = m_textures[i + 1].Get();
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource = upload.Get();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint = fp;

        m_commandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

        D3D12_RESOURCE_BARRIER tb2{};
        tb2.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        tb2.Transition.pResource = m_textures[i + 1].Get();
        tb2.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        tb2.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        tb2.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &tb2);

        texUploads.push_back(upload);
    }

    CheckHR(m_commandList->Close(), "CmdList Close (CreateMesh)");
    ID3D12CommandList* cmds[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, cmds);
    WaitForGpu();

    // ── заполняем views ───────────────────────────────────────────────────────
    m_vertexView.BufferLocation = m_vbGpu->GetGPUVirtualAddress();
    m_vertexView.StrideInBytes = sizeof(MeshVertex);
    m_vertexView.SizeInBytes = (UINT)vbSize;

    m_indexView.BufferLocation = m_ibGpu->GetGPUVirtualAddress();
    m_indexView.SizeInBytes = (UINT)ibSize;
    m_indexView.Format = DXGI_FORMAT_R32_UINT;   // uint32!

    // ── DrawItem-ы ────────────────────────────────────────────────────────────
    m_drawItems.clear();
    m_drawItems.reserve(model.groups.size());

    for (size_t gi = 0; gi < model.groups.size(); ++gi)
    {
        const auto& g = model.groups[gi];
        if (g.count == 0) continue;

        DrawItem di{};
        di.StartIndexLocation = g.start;
        di.IndexCount = g.count;

        uint32_t key = groupKey[gi];
        uint32_t texResIdx = (key > 0 && key < (uint32_t)m_textures.size()) ? key : 0;
        di.TextureSrvIndex = 1 + texResIdx;   // 0=CBV, 1+=SRV

        m_drawItems.push_back(di);
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  CreateConstantBuffer — выделяем CBV + SRV для всех текстур в одной heap
// ─────────────────────────────────────────────────────────────────────────────
bool GraphicsEngine::CreateConstantBuffer()
{
    m_cbAlignedSize = AlignCBSize(sizeof(FrameConstants));

    D3D12_HEAP_PROPERTIES uploadHP = MakeHeapProps(D3D12_HEAP_TYPE_UPLOAD);

    D3D12_RESOURCE_DESC cbDesc = MakeBufferDesc((UINT64)m_cbAlignedSize);
    CheckHR(m_d3dDevice->CreateCommittedResource(
        &uploadHP, D3D12_HEAP_FLAG_NONE, &cbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_constBuffer)),
        "Create Constant Buffer");

    D3D12_RANGE noRead{ 0, 0 };
    CheckHR(m_constBuffer->Map(0, &noRead, reinterpret_cast<void**>(&m_mappedCBData)),
        "Map Constant Buffer");

    if (m_textures.empty())
        throw std::runtime_error("No textures (expected at least the white fallback).");

    // Heap: 1 CBV + N SRV
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.NumDescriptors = 1 + (UINT)m_textures.size();
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    CheckHR(m_d3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_constantHeap)),
        "Create CBV/SRV heap");

    // CBV (слот 0)
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
    cbvDesc.BufferLocation = m_constBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = m_cbAlignedSize;
    m_d3dDevice->CreateConstantBufferView(&cbvDesc,
        m_constantHeap->GetCPUDescriptorHandleForHeapStart());

    // SRV (слоты 1…N)
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_constantHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (UINT64)m_cbvSrvUavHandleSize;

    for (size_t i = 0; i < m_textures.size(); ++i)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        srv.Texture2D.MostDetailedMip = 0;
        srv.Texture2D.ResourceMinLODClamp = 0.0f;

        m_d3dDevice->CreateShaderResourceView(m_textures[i].Get(), &srv, h);
        h.ptr += (UINT64)m_cbvSrvUavHandleSize;
    }

    UploadConstants();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  CreateRootSignature — slot0: CBV table, slot1: SRV table + static sampler
// ─────────────────────────────────────────────────────────────────────────────
bool GraphicsEngine::CreateRootSignature()
{
    D3D12_DESCRIPTOR_RANGE cbvRange{};
    cbvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    cbvRange.NumDescriptors = 1;
    cbvRange.BaseShaderRegister = 0;
    cbvRange.RegisterSpace = 0;
    cbvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[2]{};

    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &cbvRange;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &srvRange;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samp{};
    samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.MaxAnisotropy = 1;
    samp.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    samp.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    samp.MaxLOD = D3D12_FLOAT32_MAX;
    samp.ShaderRegister = 0;
    samp.RegisterSpace = 0;
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = 2;
    rsDesc.pParameters = params;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers = &samp;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serialized, errors;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        &serialized, &errors);
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

// ─────────────────────────────────────────────────────────────────────────────
//  CreatePipeline
// ─────────────────────────────────────────────────────────────────────────────
bool GraphicsEngine::CreatePipeline()
{
    D3D12_RASTERIZER_DESC rast{};
    rast.FillMode = D3D12_FILL_MODE_SOLID;
    rast.CullMode = D3D12_CULL_MODE_NONE;
    rast.FrontCounterClockwise = TRUE;
    rast.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rast.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rast.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rast.DepthClipEnable = TRUE;

    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    ds.StencilEnable = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = m_rootSig.Get();
    psoDesc.VS = { m_vertexShaderBlob->GetBufferPointer(), m_vertexShaderBlob->GetBufferSize() };
    psoDesc.PS = { m_pixelShaderBlob->GetBufferPointer(),  m_pixelShaderBlob->GetBufferSize() };
    psoDesc.BlendState = blend;
    psoDesc.RasterizerState = rast;
    psoDesc.DepthStencilState = ds;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.InputLayout = { m_vertexLayout, 3 };   // 3 элемента
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;

    CheckHR(m_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)),
        "CreateGraphicsPipelineState");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  UploadConstants
// ─────────────────────────────────────────────────────────────────────────────
void GraphicsEngine::UploadConstants()
{
    if (!m_mappedCBData) return;

    FrameConstants fc{};

    XMMATRIX world = XMLoadFloat4x4(&m_worldMatrix);
    XMMATRIX view = XMLoadFloat4x4(&m_viewMatrix);
    XMMATRIX proj = XMLoadFloat4x4(&m_projMatrix);
    XMMATRIX mvp = world * view * proj;

    XMStoreFloat4x4(&fc.ModelMatrix, XMMatrixTranspose(world));
    XMStoreFloat4x4(&fc.MVP, XMMatrixTranspose(mvp));

    fc.CameraPos = m_cameraPos;

    XMVECTOR L = XMVector3Normalize(XMLoadFloat3(&m_sunDirection));
    XMStoreFloat3(&fc.SunDir, L);

    fc.AmbientColor = XMFLOAT4(0.08f, 0.08f, 0.08f, 1.0f);
    fc.DiffuseColor = XMFLOAT4(0.90f, 0.90f, 0.90f, 1.0f);
    fc.SpecularColor = XMFLOAT4(0.90f, 0.90f, 0.90f, 1.0f);
    fc.Shininess = 64.0f;

    std::memcpy(m_mappedCBData, &fc, sizeof(fc));
}