#include "../include/ParticleSystem.h"
#include "../include/GBuffer.h"

#include <d3dcompiler.h>
#include <algorithm>
#include <stdexcept>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace
{
    void CheckResult(HRESULT result, const char* operation)
    {
        if (FAILED(result))
            throw std::runtime_error(operation);
    }

    D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE type)
    {
        D3D12_HEAP_PROPERTIES properties{};
        properties.Type = type;
        properties.CreationNodeMask = 1;
        properties.VisibleNodeMask = 1;
        return properties;
    }

    D3D12_RESOURCE_DESC BufferDescription(UINT64 size,
        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
    {
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        description.Width = size;
        description.Height = 1;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.SampleDesc.Count = 1;
        description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        description.Flags = flags;
        return description;
    }

    D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        return barrier;
    }

    D3D12_RESOURCE_BARRIER UavBarrier(ID3D12Resource* resource)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = resource;
        return barrier;
    }

    void CreateRootSignature(ID3D12Device* device,
        const D3D12_ROOT_SIGNATURE_DESC& description,
        ID3D12RootSignature** rootSignature)
    {
        ComPtr<ID3DBlob> binary;
        ComPtr<ID3DBlob> errors;
        const HRESULT result = D3D12SerializeRootSignature(&description,
            D3D_ROOT_SIGNATURE_VERSION_1, &binary, &errors);
        if (FAILED(result))
        {
            const char* message = errors
                ? static_cast<const char*>(errors->GetBufferPointer())
                : "Failed to serialize particle root signature";
            throw std::runtime_error(message);
        }
        CheckResult(device->CreateRootSignature(0, binary->GetBufferPointer(),
            binary->GetBufferSize(), IID_PPV_ARGS(rootSignature)),
            "Create particle root signature");
    }

    ComPtr<ID3DBlob> Compile(const char* entryPoint, const char* target)
    {
        UINT flags = 0;
#if defined(_DEBUG)
        flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        ComPtr<ID3DBlob> shader;
        ComPtr<ID3DBlob> errors;
        const HRESULT result = D3DCompileFromFile(L"Particle.hlsl", nullptr,
            D3D_COMPILE_STANDARD_FILE_INCLUDE, entryPoint, target, flags, 0,
            &shader, &errors);
        if (FAILED(result))
        {
            const char* message = errors
                ? static_cast<const char*>(errors->GetBufferPointer())
                : "Failed to compile particle shader";
            throw std::runtime_error(message);
        }
        return shader;
    }
}

ParticleSystem::~ParticleSystem()
{
    if (m_constantBuffer && m_mappedConstants)
    {
        m_constantBuffer->Unmap(0, nullptr);
        m_mappedConstants = nullptr;
    }
}

void ParticleSystem::Initialize(ID3D12Device* device)
{
    if (!device)
        throw std::runtime_error("ParticleSystem requires a D3D12 device");

    m_device = device;
    CreateRootSignatures();
    CreatePipelineStates();
    CreateBuffers();
}

void ParticleSystem::CreateRootSignatures()
{
    D3D12_ROOT_PARAMETER counterParameter{};
    counterParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    counterParameter.Descriptor.ShaderRegister = 3;
    counterParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC counterDescription{};
    counterDescription.NumParameters = 1;
    counterDescription.pParameters = &counterParameter;
    CreateRootSignature(m_device.Get(), counterDescription,
        m_counterRootSignature.GetAddressOf());

    D3D12_DESCRIPTOR_RANGE initializeRange{};
    initializeRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    initializeRange.NumDescriptors = 1;
    initializeRange.BaseShaderRegister = 2;
    D3D12_ROOT_PARAMETER initializeParameters[2]{};
    initializeParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    initializeParameters[0].DescriptorTable = { 1, &initializeRange };
    initializeParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    initializeParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    initializeParameters[1].Descriptor.ShaderRegister = 0;
    initializeParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC initializeDescription{};
    initializeDescription.NumParameters = 2;
    initializeDescription.pParameters = initializeParameters;
    CreateRootSignature(m_device.Get(), initializeDescription,
        m_initializeRootSignature.GetAddressOf());

    D3D12_DESCRIPTOR_RANGE simulationRange{};
    simulationRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    simulationRange.NumDescriptors = 2;
    simulationRange.BaseShaderRegister = 0;
    D3D12_ROOT_PARAMETER simulationParameters[2]{};
    simulationParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    simulationParameters[0].DescriptorTable = { 1, &simulationRange };
    simulationParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    simulationParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    simulationParameters[1].Descriptor.ShaderRegister = 0;
    simulationParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC simulationDescription{};
    simulationDescription.NumParameters = 2;
    simulationDescription.pParameters = simulationParameters;
    CreateRootSignature(m_device.Get(), simulationDescription,
        m_simulateRootSignature.GetAddressOf());

    D3D12_ROOT_PARAMETER drawParameters[2]{};
    drawParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    drawParameters[0].Descriptor.ShaderRegister = 0;
    drawParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    drawParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    drawParameters[1].Descriptor.ShaderRegister = 0;
    drawParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC drawDescription{};
    drawDescription.NumParameters = 2;
    drawDescription.pParameters = drawParameters;
    drawDescription.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    CreateRootSignature(m_device.Get(), drawDescription,
        m_drawRootSignature.GetAddressOf());
}

void ParticleSystem::CreatePipelineStates()
{
    const ComPtr<ID3DBlob> counterShader = Compile("ResetCounterCS", "cs_5_0");
    const ComPtr<ID3DBlob> initializeShader = Compile("InitializeCS", "cs_5_0");
    const ComPtr<ID3DBlob> simulateShader = Compile("SimulateCS", "cs_5_0");
    const ComPtr<ID3DBlob> vertexShader = Compile("ParticleVS", "vs_5_0");
    const ComPtr<ID3DBlob> geometryShader = Compile("BillboardGS", "gs_5_0");
    const ComPtr<ID3DBlob> pixelShader = Compile("ParticlePS", "ps_5_0");

    auto createComputePipeline = [&](ID3D12RootSignature* rootSignature,
        ID3DBlob* shader, ComPtr<ID3D12PipelineState>& pipeline,
        const char* operation)
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC description{};
        description.pRootSignature = rootSignature;
        description.CS = { shader->GetBufferPointer(), shader->GetBufferSize() };
        CheckResult(m_device->CreateComputePipelineState(&description,
            IID_PPV_ARGS(&pipeline)), operation);
    };
    createComputePipeline(m_counterRootSignature.Get(), counterShader.Get(),
        m_counterPipeline, "Create particle counter pipeline");
    createComputePipeline(m_initializeRootSignature.Get(), initializeShader.Get(),
        m_initializePipeline, "Create particle initialization pipeline");
    createComputePipeline(m_simulateRootSignature.Get(), simulateShader.Get(),
        m_simulatePipeline, "Create particle simulation pipeline");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
    description.pRootSignature = m_drawRootSignature.Get();
    description.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    description.GS = { geometryShader->GetBufferPointer(), geometryShader->GetBufferSize() };
    description.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    description.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    description.RasterizerState.DepthClipEnable = TRUE;
    description.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    description.BlendState.RenderTarget[1].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    description.DepthStencilState.DepthEnable = TRUE;
    description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    description.SampleMask = UINT_MAX;
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    description.NumRenderTargets = 2;
    description.RTVFormats[0] = GBuffer::AlbedoFormat();
    description.RTVFormats[1] = GBuffer::NormalFormat();
    description.DSVFormat = GBuffer::DepthViewFormat();
    description.SampleDesc.Count = 1;
    CheckResult(m_device->CreateGraphicsPipelineState(&description,
        IID_PPV_ARGS(&m_drawPipeline)), "Create particle draw pipeline");
}

void ParticleSystem::CreateBuffers()
{
    const auto defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    const auto particleDescription = BufferDescription(
        static_cast<UINT64>(ParticleCount) * ParticleStride,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    const auto counterDescription = BufferDescription(sizeof(uint32_t),
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    for (uint32_t index = 0; index < 2; ++index)
    {
        CheckResult(m_device->CreateCommittedResource(&defaultHeap,
            D3D12_HEAP_FLAG_NONE, &particleDescription,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            IID_PPV_ARGS(&m_particleBuffers[index])), "Create particle buffer");
        CheckResult(m_device->CreateCommittedResource(&defaultHeap,
            D3D12_HEAP_FLAG_NONE, &counterDescription,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            IID_PPV_ARGS(&m_counterBuffers[index])), "Create particle counter");
    }

    D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
    heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDescription.NumDescriptors = 4;
    heapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    CheckResult(m_device->CreateDescriptorHeap(&heapDescription,
        IID_PPV_ARGS(&m_uavHeap)), "Create particle UAV heap");
    m_descriptorSize = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Format = DXGI_FORMAT_UNKNOWN;
    uav.Buffer.NumElements = ParticleCount;
    uav.Buffer.StructureByteStride = ParticleStride;

    D3D12_CPU_DESCRIPTOR_HANDLE handle =
        m_uavHeap->GetCPUDescriptorHandleForHeapStart();
    const uint32_t descriptorOrder[4] = { 0, 1, 1, 0 };
    // The heap exposes two adjacent UAV pairs: [consume 0, append 1] and
    // [consume 1, append 0]. This keeps a ping-pong step to one table bind.
    for (uint32_t descriptor = 0; descriptor < 4; ++descriptor)
    {
        const uint32_t buffer = descriptorOrder[descriptor];
        m_device->CreateUnorderedAccessView(m_particleBuffers[buffer].Get(),
            m_counterBuffers[buffer].Get(), &uav, handle);
        handle.ptr += m_descriptorSize;
    }

    const auto uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    const auto constantDescription = BufferDescription(256);
    CheckResult(m_device->CreateCommittedResource(&uploadHeap,
        D3D12_HEAP_FLAG_NONE, &constantDescription,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&m_constantBuffer)), "Create particle constant buffer");
    D3D12_RANGE noRead{ 0, 0 };
    CheckResult(m_constantBuffer->Map(0, &noRead,
        reinterpret_cast<void**>(&m_mappedConstants)),
        "Map particle constant buffer");
}

void ParticleSystem::SetFrameData(const XMFLOAT4X4& view,
    const XMFLOAT4X4& projection, const XMFLOAT3& cameraPosition,
    const XMFLOAT3& emitterPosition, float deltaTime, float totalTime)
{
    if (!m_mappedConstants)
        return;

    const XMMATRIX viewProjection = XMLoadFloat4x4(&view) *
        XMLoadFloat4x4(&projection);
    XMStoreFloat4x4(&m_mappedConstants->ViewProjection,
        XMMatrixTranspose(viewProjection));
    m_mappedConstants->CameraPosition = cameraPosition;
    m_mappedConstants->DeltaTime = std::clamp(deltaTime, 0.0f, 0.05f);
    m_mappedConstants->TotalTime = totalTime;
    m_mappedConstants->EmitterPosition = emitterPosition;
}

void ParticleSystem::ResetCounter(ID3D12GraphicsCommandList* commands,
    uint32_t index) const
{
    commands->SetPipelineState(m_counterPipeline.Get());
    commands->SetComputeRootSignature(m_counterRootSignature.Get());
    commands->SetComputeRootUnorderedAccessView(0,
        m_counterBuffers[index]->GetGPUVirtualAddress());
    commands->Dispatch(1, 1, 1);
    const D3D12_RESOURCE_BARRIER barrier = UavBarrier(m_counterBuffers[index].Get());
    commands->ResourceBarrier(1, &barrier);
}

void ParticleSystem::InitializeParticles(ID3D12GraphicsCommandList* commands)
{
    ResetCounter(commands, 0);
    ResetCounter(commands, 1);

    commands->SetPipelineState(m_initializePipeline.Get());
    commands->SetComputeRootSignature(m_initializeRootSignature.Get());
    ID3D12DescriptorHeap* heaps[] = { m_uavHeap.Get() };
    commands->SetDescriptorHeaps(1, heaps);
    commands->SetComputeRootDescriptorTable(0,
        m_uavHeap->GetGPUDescriptorHandleForHeapStart());
    commands->SetComputeRootConstantBufferView(1,
        m_constantBuffer->GetGPUVirtualAddress());
    commands->Dispatch((ParticleCount + 63) / 64, 1, 1);

    const D3D12_RESOURCE_BARRIER uavBarriers[] = {
        UavBarrier(m_particleBuffers[0].Get()),
        UavBarrier(m_counterBuffers[0].Get())
    };
    commands->ResourceBarrier(static_cast<UINT>(std::size(uavBarriers)), uavBarriers);
    const D3D12_RESOURCE_BARRIER toShaderResource = Transition(
        m_particleBuffers[0].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commands->ResourceBarrier(1, &toShaderResource);
    m_particleStates[0] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    m_initializedOnGpu = true;
}

void ParticleSystem::Simulate(ID3D12GraphicsCommandList* commands)
{
    // Initialization is deliberately recorded on the first frame so it uses
    // the same direct command list as the rest of the GPU-only simulation.
    if (!m_initializedOnGpu)
    {
        InitializeParticles(commands);
        return;
    }

    if (m_particleStates[m_consumeIndex] != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    {
        const D3D12_RESOURCE_BARRIER toUav = Transition(
            m_particleBuffers[m_consumeIndex].Get(),
            m_particleStates[m_consumeIndex], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commands->ResourceBarrier(1, &toUav);
        m_particleStates[m_consumeIndex] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    ResetCounter(commands, m_appendIndex);
    commands->SetPipelineState(m_simulatePipeline.Get());
    commands->SetComputeRootSignature(m_simulateRootSignature.Get());
    ID3D12DescriptorHeap* heaps[] = { m_uavHeap.Get() };
    commands->SetDescriptorHeaps(1, heaps);
    D3D12_GPU_DESCRIPTOR_HANDLE table =
        m_uavHeap->GetGPUDescriptorHandleForHeapStart();
    if (m_consumeIndex == 1)
        table.ptr += static_cast<UINT64>(2) * m_descriptorSize;
    commands->SetComputeRootDescriptorTable(0, table);
    commands->SetComputeRootConstantBufferView(1,
        m_constantBuffer->GetGPUVirtualAddress());
    commands->Dispatch((ParticleCount + 63) / 64, 1, 1);

    const D3D12_RESOURCE_BARRIER uavBarriers[] = {
        UavBarrier(m_particleBuffers[m_consumeIndex].Get()),
        UavBarrier(m_particleBuffers[m_appendIndex].Get()),
        UavBarrier(m_counterBuffers[m_consumeIndex].Get()),
        UavBarrier(m_counterBuffers[m_appendIndex].Get())
    };
    commands->ResourceBarrier(static_cast<UINT>(std::size(uavBarriers)), uavBarriers);
    const D3D12_RESOURCE_BARRIER toShaderResource = Transition(
        m_particleBuffers[m_appendIndex].Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commands->ResourceBarrier(1, &toShaderResource);
    m_particleStates[m_appendIndex] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    std::swap(m_consumeIndex, m_appendIndex);
}

void ParticleSystem::Draw(ID3D12GraphicsCommandList* commands) const
{
    if (!m_initializedOnGpu)
        return;

    commands->SetPipelineState(m_drawPipeline.Get());
    commands->SetGraphicsRootSignature(m_drawRootSignature.Get());
    commands->SetGraphicsRootShaderResourceView(0,
        m_particleBuffers[m_consumeIndex]->GetGPUVirtualAddress());
    commands->SetGraphicsRootConstantBufferView(1,
        m_constantBuffer->GetGPUVirtualAddress());
    commands->IASetVertexBuffers(0, 0, nullptr);
    commands->IASetIndexBuffer(nullptr);
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    commands->DrawInstanced(ParticleCount, 1, 0, 0);
}
