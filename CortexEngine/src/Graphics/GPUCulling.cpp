#include "GPUCulling.h"
#include "RHI/DX12Device.h"
#include "RHI/DX12CommandQueue.h"
#include "RHI/DX12Pipeline.h"
#include <algorithm>
#include <spdlog/spdlog.h>
#include <cstring>

namespace Cortex::Graphics {

// GPU Culling constants (must match shader)
struct CullConstants {
    glm::mat4 viewProj;
    glm::vec4 frustumPlanes[6];
    glm::vec3 cameraPos;
    uint32_t instanceCount;
    uint32_t forceVisible;
    uint32_t padding[3];
};

static void LogIndirectCommand(const char* label, uint32_t index, const IndirectCommand& cmd) {
    spdlog::info(
        "{}[{}]: objectCBV=0x{:016X} materialCBV=0x{:016X} "
        "VBV(addr=0x{:016X} size={} stride={}) "
        "IBV(addr=0x{:016X} size={} fmt={}) "
        "draw(indexCount={} instanceCount={} startIndex={} baseVertex={} startInstance={})",
        label,
        index,
        static_cast<uint64_t>(cmd.objectCBV),
        static_cast<uint64_t>(cmd.materialCBV),
        static_cast<uint64_t>(cmd.vertexBuffer.BufferLocation),
        cmd.vertexBuffer.SizeInBytes,
        cmd.vertexBuffer.StrideInBytes,
        static_cast<uint64_t>(cmd.indexBuffer.BufferLocation),
        cmd.indexBuffer.SizeInBytes,
        static_cast<unsigned int>(cmd.indexBuffer.Format),
        cmd.draw.indexCountPerInstance,
        cmd.draw.instanceCount,
        cmd.draw.startIndexLocation,
        cmd.draw.baseVertexLocation,
        cmd.draw.startInstanceLocation);
}

Result<void> GPUCullingPipeline::Initialize(
    DX12Device* device,
    DescriptorHeapManager* descriptorManager,
    DX12CommandQueue* commandQueue,
    uint32_t maxInstances)
{
    if (!device || !descriptorManager || !commandQueue) {
        return Result<void>::Err("Invalid parameters for GPU culling initialization");
    }

    m_device = device;
    m_descriptorManager = descriptorManager;
    m_commandQueue = commandQueue;
    m_maxInstances = maxInstances;

    auto rootResult = CreateRootSignature();
    if (rootResult.IsErr()) {
        return rootResult;
    }

    auto pipelineResult = CreateComputePipeline();
    if (pipelineResult.IsErr()) {
        return pipelineResult;
    }

    auto bufferResult = CreateBuffers();
    if (bufferResult.IsErr()) {
        return bufferResult;
    }

    spdlog::info("GPU Culling Pipeline initialized (max {} instances)", m_maxInstances);
    return Result<void>::Ok();
}

void GPUCullingPipeline::Shutdown() {
    if (m_flushCallback) {
        m_flushCallback();
    }

    m_cullPipeline.Reset();
    m_rootSignature.Reset();
    m_commandSignature.Reset();
    m_instanceBuffer.Reset();
    m_instanceUploadBuffer.Reset();
    m_allCommandBuffer.Reset();
    m_allCommandUploadBuffer.Reset();
    m_visibleCommandBuffer.Reset();
    m_commandCountBuffer.Reset();
    m_commandCountReadback.Reset();
    m_visibleCommandReadback.Reset();
    m_cullConstantBuffer.Reset();

    spdlog::info("GPU Culling Pipeline shutdown");
}

Result<void> GPUCullingPipeline::CreateRootSignature() {
    // Root signature for compute culling:
    // 0: CBV - Cull constants (view-proj, frustum planes, camera pos)
    // 1: SRV - Instance buffer (input, all instances)
    // 2: SRV - All indirect commands (input)
    // 3: UAV - Visible command buffer (output)
    // 4: UAV - Command count buffer (atomic append)

    D3D12_ROOT_PARAMETER1 rootParams[5] = {};

    // CBV for constants
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].Descriptor.RegisterSpace = 0;
    rootParams[0].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // SRV for instance buffer
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParams[1].Descriptor.ShaderRegister = 0;
    rootParams[1].Descriptor.RegisterSpace = 0;
    rootParams[1].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // SRV for all command buffer
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParams[2].Descriptor.ShaderRegister = 1;
    rootParams[2].Descriptor.RegisterSpace = 0;
    rootParams[2].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // UAV for visible command buffer
    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rootParams[3].Descriptor.ShaderRegister = 0;
    rootParams[3].Descriptor.RegisterSpace = 0;
    rootParams[3].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
    rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // UAV for command count buffer
    rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rootParams[4].Descriptor.ShaderRegister = 1;
    rootParams[4].Descriptor.RegisterSpace = 0;
    rootParams[4].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
    rootParams[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootSigDesc.Desc_1_1.NumParameters = 5;
    rootSigDesc.Desc_1_1.pParameters = rootParams;
    rootSigDesc.Desc_1_1.NumStaticSamplers = 0;
    rootSigDesc.Desc_1_1.pStaticSamplers = nullptr;
    rootSigDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3D12SerializeVersionedRootSignature(&rootSigDesc, &serializedRootSig, &errorBlob);
    if (FAILED(hr)) {
        std::string errMsg = "Failed to serialize GPU culling root signature";
        if (errorBlob) {
            errMsg += ": ";
            errMsg += static_cast<const char*>(errorBlob->GetBufferPointer());
        }
        return Result<void>::Err(errMsg);
    }

    hr = m_device->GetDevice()->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(&m_rootSignature)
    );

    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create GPU culling root signature");
    }

    return Result<void>::Ok();
}

Result<void> GPUCullingPipeline::CreateComputePipeline() {
    auto csResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/GPUCulling.hlsl",
        "CSMain",
        "cs_5_1"
    );
    if (csResult.IsErr()) {
        return Result<void>::Err("Failed to compile GPU culling shader: " + csResult.Error());
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSignature.Get();
    auto csBytecode = csResult.Value().GetBytecode();
    psoDesc.CS = csBytecode;

    HRESULT hr = m_device->GetDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_cullPipeline));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create GPU culling pipeline state");
    }

    return Result<void>::Ok();
}

Result<void> GPUCullingPipeline::CreateBuffers() {
    ID3D12Device* device = m_device->GetDevice();

    const size_t instanceBufferSize = m_maxInstances * sizeof(GPUInstanceData);
    const size_t commandBufferSize = m_maxInstances * sizeof(IndirectCommand);

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = instanceBufferSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    // Instance buffer (default heap) + upload staging
    HRESULT hr = device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_instanceBuffer)
    );
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create instance buffer");
    }
    hr = device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_instanceUploadBuffer)
    );
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create instance upload buffer");
    }

    // All-commands buffer (default heap) + upload staging
    bufferDesc.Width = commandBufferSize;
    hr = device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_allCommandBuffer)
    );
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create all-commands buffer");
    }
    hr = device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_allCommandUploadBuffer)
    );
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create all-commands upload buffer");
    }

    // Visible command buffer (default heap, UAV)
    bufferDesc.Width = commandBufferSize;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    hr = device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_visibleCommandBuffer)
    );
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create visible command buffer");
    }

    // Command count buffer (4 bytes for atomic counter)
    bufferDesc.Width = sizeof(uint32_t);
    hr = device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_commandCountBuffer)
    );
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create command count buffer");
    }

    // Command count readback buffer
    D3D12_HEAP_PROPERTIES readbackHeap = {};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    hr = device->CreateCommittedResource(
        &readbackHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_commandCountReadback)
    );
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create command count readback buffer");
    }

    // Allocate descriptors for counter buffer UAV (needed for ClearUnorderedAccessViewUint)
    auto counterUAVResult = m_descriptorManager->AllocateCBV_SRV_UAV();
    if (counterUAVResult.IsErr()) {
        return Result<void>::Err("Failed to allocate command count UAV descriptor");
    }
    m_counterUAV = counterUAVResult.Value();

    auto counterStagingResult = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
    if (counterStagingResult.IsErr()) {
        return Result<void>::Err("Failed to allocate command count UAV staging descriptor");
    }
    m_counterUAVStaging = counterStagingResult.Value();

    // Create UAV for command count buffer (raw buffer, 1 uint32)
    D3D12_UNORDERED_ACCESS_VIEW_DESC counterUAVDesc = {};
    counterUAVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    counterUAVDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    counterUAVDesc.Buffer.FirstElement = 0;
    counterUAVDesc.Buffer.NumElements = 1;
    counterUAVDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;

    device->CreateUnorderedAccessView(m_commandCountBuffer.Get(), nullptr, &counterUAVDesc, m_counterUAV.cpu);
    device->CreateUnorderedAccessView(m_commandCountBuffer.Get(), nullptr, &counterUAVDesc, m_counterUAVStaging.cpu);

    m_visibleCommandState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    m_commandCountState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    // Constant buffer
    size_t cbSize = (sizeof(CullConstants) + 255) & ~255;  // 256-byte aligned

    D3D12_RESOURCE_DESC cbDesc = {};
    cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    cbDesc.Width = cbSize;
    cbDesc.Height = 1;
    cbDesc.DepthOrArraySize = 1;
    cbDesc.MipLevels = 1;
    cbDesc.Format = DXGI_FORMAT_UNKNOWN;
    cbDesc.SampleDesc.Count = 1;
    cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &cbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_cullConstantBuffer)
    );
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create culling constant buffer");
    }

    m_instanceState = D3D12_RESOURCE_STATE_COPY_DEST;
    m_allCommandState = D3D12_RESOURCE_STATE_COPY_DEST;
    return Result<void>::Ok();
}

Result<void> GPUCullingPipeline::CreateCommandSignature(ID3D12RootSignature* rootSignature) {
    if (!rootSignature) {
        return Result<void>::Err("CreateCommandSignature requires a valid graphics root signature");
    }

    D3D12_INDIRECT_ARGUMENT_DESC args[5] = {};
    args[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
    args[0].ConstantBufferView.RootParameterIndex = 0;  // ObjectConstants (b0)

    args[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
    args[1].ConstantBufferView.RootParameterIndex = 2;  // MaterialConstants (b2)

    args[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
    args[2].VertexBuffer.Slot = 0;

    args[3].Type = D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW;

    args[4].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

    D3D12_COMMAND_SIGNATURE_DESC cmdSigDesc = {};
    cmdSigDesc.ByteStride = sizeof(IndirectCommand);
    cmdSigDesc.NumArgumentDescs = static_cast<UINT>(sizeof(args) / sizeof(args[0]));
    cmdSigDesc.pArgumentDescs = args;
    cmdSigDesc.NodeMask = 0;

    HRESULT hr = m_device->GetDevice()->CreateCommandSignature(
        &cmdSigDesc,
        rootSignature,
        IID_PPV_ARGS(&m_commandSignature)
    );

    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create command signature for ExecuteIndirect");
    }

    return Result<void>::Ok();
}

Result<void> GPUCullingPipeline::SetGraphicsRootSignature(ID3D12RootSignature* rootSignature) {
    if (m_commandSignature) {
        return Result<void>::Ok();
    }
    return CreateCommandSignature(rootSignature);
}

Result<void> GPUCullingPipeline::PrepareAllCommandsForExecuteIndirect(ID3D12GraphicsCommandList* cmdList) {
    if (!cmdList) {
        return Result<void>::Err("PrepareAllCommandsForExecuteIndirect requires a valid command list");
    }
    if (!m_allCommandBuffer) {
        return Result<void>::Err("All-commands buffer not initialized");
    }

    if (m_allCommandState != D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_allCommandBuffer.Get();
        barrier.Transition.StateBefore = m_allCommandState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_allCommandState = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    }

    return Result<void>::Ok();
}

void GPUCullingPipeline::RequestCommandReadback(uint32_t commandCount) {
    if (commandCount == 0) {
        return;
    }
    m_commandReadbackRequested = true;
    m_commandReadbackCount = commandCount;
}

void GPUCullingPipeline::UpdateVisibleCountFromReadback() {
    if (!m_commandCountReadback) {
        return;
    }

    uint32_t* mapped = nullptr;
    D3D12_RANGE readRange = {0, sizeof(uint32_t)};
    HRESULT hr = m_commandCountReadback->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
    if (SUCCEEDED(hr) && mapped) {
        m_visibleCount = *mapped;
        m_commandCountReadback->Unmap(0, nullptr);
    }

    if (m_commandReadbackPending && m_visibleCommandReadback && m_commandReadbackCount > 0) {
        const size_t readbackBytes = static_cast<size_t>(m_commandReadbackCount) * sizeof(IndirectCommand);
        D3D12_RANGE readRange = { 0, readbackBytes };
        void* commandData = nullptr;
        HRESULT cmdHr = m_visibleCommandReadback->Map(0, &readRange, &commandData);
        if (SUCCEEDED(cmdHr) && commandData) {
            const auto* commands = static_cast<const IndirectCommand*>(commandData);
            const uint32_t maxLog = std::min(m_commandReadbackCount, 2u);
            for (uint32_t i = 0; i < maxLog; ++i) {
                LogIndirectCommand("GPU VisibleCmd", i, commands[i]);
            }
            m_visibleCommandReadback->Unmap(0, nullptr);
        }
        m_commandReadbackPending = false;
    }
}

Result<void> GPUCullingPipeline::UpdateInstances(
    ID3D12GraphicsCommandList* cmdList,
    const std::vector<GPUInstanceData>& instances)
{
    if (instances.empty()) {
        m_totalInstances = 0;
        return Result<void>::Ok();
    }
    if (!cmdList) {
        return Result<void>::Err("UpdateInstances requires a valid command list");
    }
    if (!m_instanceBuffer || !m_instanceUploadBuffer) {
        return Result<void>::Err("Instance buffer not initialized");
    }

    if (instances.size() > m_maxInstances) {
        spdlog::warn("GPU Culling: Instance count {} exceeds max {}, truncating",
                     instances.size(), m_maxInstances);
    }

    m_totalInstances = static_cast<uint32_t>(std::min(instances.size(), static_cast<size_t>(m_maxInstances)));

    // Map and upload instance data
    void* mappedData = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    HRESULT hr = m_instanceUploadBuffer->Map(0, &readRange, &mappedData);
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to map instance upload buffer");
    }

    memcpy(mappedData, instances.data(), m_totalInstances * sizeof(GPUInstanceData));
    m_instanceUploadBuffer->Unmap(0, nullptr);

    const UINT64 copyBytes = m_totalInstances * sizeof(GPUInstanceData);
    if (copyBytes == 0) {
        return Result<void>::Ok();
    }

    if (m_instanceState != D3D12_RESOURCE_STATE_COPY_DEST) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_instanceBuffer.Get();
        barrier.Transition.StateBefore = m_instanceState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_instanceState = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    cmdList->CopyBufferRegion(m_instanceBuffer.Get(), 0, m_instanceUploadBuffer.Get(), 0, copyBytes);

    if (m_instanceState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_instanceBuffer.Get();
        barrier.Transition.StateBefore = m_instanceState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_instanceState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }

    return Result<void>::Ok();
}

Result<void> GPUCullingPipeline::UpdateIndirectCommands(
    ID3D12GraphicsCommandList* cmdList,
    const std::vector<IndirectCommand>& commands)
{
    if (!cmdList) {
        return Result<void>::Err("UpdateIndirectCommands requires a valid command list");
    }
    if (!m_allCommandBuffer || !m_allCommandUploadBuffer) {
        return Result<void>::Err("Indirect command buffer not initialized");
    }
    if (commands.empty()) {
        return Result<void>::Ok();
    }

    size_t commandCount = commands.size();
    if (commandCount > m_maxInstances) {
        spdlog::warn("GPU Culling: Command count {} exceeds max {}, truncating",
                     commandCount, m_maxInstances);
        commandCount = m_maxInstances;
    }

    void* mappedData = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    HRESULT hr = m_allCommandUploadBuffer->Map(0, &readRange, &mappedData);
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to map indirect command upload buffer");
    }

    memcpy(mappedData, commands.data(), commandCount * sizeof(IndirectCommand));
    m_allCommandUploadBuffer->Unmap(0, nullptr);

    const UINT64 copyBytes = commandCount * sizeof(IndirectCommand);
    if (copyBytes > 0) {
        if (m_allCommandState != D3D12_RESOURCE_STATE_COPY_DEST) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = m_allCommandBuffer.Get();
            barrier.Transition.StateBefore = m_allCommandState;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmdList->ResourceBarrier(1, &barrier);
            m_allCommandState = D3D12_RESOURCE_STATE_COPY_DEST;
        }

        cmdList->CopyBufferRegion(m_allCommandBuffer.Get(), 0, m_allCommandUploadBuffer.Get(), 0, copyBytes);

        if (m_allCommandState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = m_allCommandBuffer.Get();
            barrier.Transition.StateBefore = m_allCommandState;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmdList->ResourceBarrier(1, &barrier);
            m_allCommandState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        }
    }

    const uint32_t cmdCount = static_cast<uint32_t>(commandCount);
    if (m_totalInstances != cmdCount) {
        m_totalInstances = std::min(m_totalInstances, cmdCount);
    }

    return Result<void>::Ok();
}

void GPUCullingPipeline::ExtractFrustumPlanes(const glm::mat4& viewProj, FrustumPlanes& planes) {
    // Extract frustum planes from view-projection matrix
    // Left plane
    planes.planes[0] = glm::vec4(
        viewProj[0][3] + viewProj[0][0],
        viewProj[1][3] + viewProj[1][0],
        viewProj[2][3] + viewProj[2][0],
        viewProj[3][3] + viewProj[3][0]
    );

    // Right plane
    planes.planes[1] = glm::vec4(
        viewProj[0][3] - viewProj[0][0],
        viewProj[1][3] - viewProj[1][0],
        viewProj[2][3] - viewProj[2][0],
        viewProj[3][3] - viewProj[3][0]
    );

    // Bottom plane
    planes.planes[2] = glm::vec4(
        viewProj[0][3] + viewProj[0][1],
        viewProj[1][3] + viewProj[1][1],
        viewProj[2][3] + viewProj[2][1],
        viewProj[3][3] + viewProj[3][1]
    );

    // Top plane
    planes.planes[3] = glm::vec4(
        viewProj[0][3] - viewProj[0][1],
        viewProj[1][3] - viewProj[1][1],
        viewProj[2][3] - viewProj[2][1],
        viewProj[3][3] - viewProj[3][1]
    );

    // Near plane
    planes.planes[4] = glm::vec4(
        viewProj[0][2],
        viewProj[1][2],
        viewProj[2][2],
        viewProj[3][2]
    );

    // Far plane
    planes.planes[5] = glm::vec4(
        viewProj[0][3] - viewProj[0][2],
        viewProj[1][3] - viewProj[1][2],
        viewProj[2][3] - viewProj[2][2],
        viewProj[3][3] - viewProj[3][2]
    );

    // Normalize planes
    for (int i = 0; i < 6; i++) {
        float len = glm::length(glm::vec3(planes.planes[i]));
        if (len > 0.0001f) {
            planes.planes[i] /= len;
        }
    }
}

Result<void> GPUCullingPipeline::DispatchCulling(
    ID3D12GraphicsCommandList* cmdList,
    const glm::mat4& viewProj,
    const glm::vec3& cameraPos)
{
    if (m_totalInstances == 0) {
        m_visibleCount = 0;
        return Result<void>::Ok();
    }
    if (!m_visibleCommandBuffer || !m_allCommandBuffer || !m_commandCountBuffer) {
        return Result<void>::Err("GPU culling buffers are not initialized");
    }

    // Update constants
    CullConstants constants;
    constants.viewProj = viewProj;
    constants.cameraPos = cameraPos;
    constants.instanceCount = m_totalInstances;
    constants.forceVisible = m_forceVisible ? 1u : 0u;
    constants.padding[0] = 0;
    constants.padding[1] = 0;
    constants.padding[2] = 0;

    FrustumPlanes frustum;
    ExtractFrustumPlanes(viewProj, frustum);
    for (int i = 0; i < 6; i++) {
        constants.frustumPlanes[i] = frustum.planes[i];
    }

    // Map and upload constants
    void* mappedCB = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    HRESULT hr = m_cullConstantBuffer->Map(0, &readRange, &mappedCB);
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to map culling constant buffer");
    }
    memcpy(mappedCB, &constants, sizeof(CullConstants));
    m_cullConstantBuffer->Unmap(0, nullptr);

    // Ensure UAV resources are in the correct state before clearing/dispatch.
    if (m_visibleCommandState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_visibleCommandBuffer.Get();
        barrier.Transition.StateBefore = m_visibleCommandState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_visibleCommandState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    if (m_commandCountState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_commandCountBuffer.Get();
        barrier.Transition.StateBefore = m_commandCountState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_commandCountState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    if (m_instanceState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_instanceBuffer.Get();
        barrier.Transition.StateBefore = m_instanceState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_instanceState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }

    if (m_allCommandState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_allCommandBuffer.Get();
        barrier.Transition.StateBefore = m_allCommandState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_allCommandState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }

    // Clear the command count buffer to 0 using ClearUnorderedAccessViewUint
    const UINT clearValues[4] = { 0, 0, 0, 0 };
    cmdList->ClearUnorderedAccessViewUint(
        m_counterUAV.gpu,
        m_counterUAVStaging.cpu,
        m_commandCountBuffer.Get(),
        clearValues,
        0,
        nullptr
    );

    // UAV barrier to ensure clear completes before compute dispatch
    D3D12_RESOURCE_BARRIER clearBarrier = {};
    clearBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    clearBarrier.UAV.pResource = m_commandCountBuffer.Get();
    cmdList->ResourceBarrier(1, &clearBarrier);

    // Set pipeline and root signature
    cmdList->SetComputeRootSignature(m_rootSignature.Get());
    cmdList->SetPipelineState(m_cullPipeline.Get());

    // Bind resources
    cmdList->SetComputeRootConstantBufferView(0, m_cullConstantBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootShaderResourceView(1, m_instanceBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootShaderResourceView(2, m_allCommandBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootUnorderedAccessView(3, m_visibleCommandBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootUnorderedAccessView(4, m_commandCountBuffer->GetGPUVirtualAddress());

    // Dispatch compute shader (64 threads per group)
    uint32_t numGroups = (m_totalInstances + 63) / 64;
    cmdList->Dispatch(numGroups, 1, 1);

    // Barrier to ensure compute writes are visible
    D3D12_RESOURCE_BARRIER uavBarriers[2] = {};
    uavBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarriers[0].UAV.pResource = m_visibleCommandBuffer.Get();
    uavBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarriers[1].UAV.pResource = m_commandCountBuffer.Get();
    cmdList->ResourceBarrier(2, uavBarriers);

    if (m_commandReadbackRequested && m_commandReadbackCount > 0) {
        const uint32_t readbackCount = std::min(m_commandReadbackCount, m_maxInstances);
        const size_t readbackBytes = static_cast<size_t>(readbackCount) * sizeof(IndirectCommand);
        if (!m_visibleCommandReadback ||
            m_visibleCommandReadback->GetDesc().Width < readbackBytes) {
            D3D12_HEAP_PROPERTIES readbackHeap = {};
            readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

            D3D12_RESOURCE_DESC readbackDesc = {};
            readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            readbackDesc.Width = readbackBytes;
            readbackDesc.Height = 1;
            readbackDesc.DepthOrArraySize = 1;
            readbackDesc.MipLevels = 1;
            readbackDesc.Format = DXGI_FORMAT_UNKNOWN;
            readbackDesc.SampleDesc.Count = 1;
            readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            m_visibleCommandReadback.Reset();
            HRESULT rbHr = m_device->GetDevice()->CreateCommittedResource(
                &readbackHeap,
                D3D12_HEAP_FLAG_NONE,
                &readbackDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&m_visibleCommandReadback)
            );
            if (FAILED(rbHr)) {
                spdlog::warn("GPU culling: failed to create command readback buffer");
            }
        }

        if (m_visibleCommandReadback) {
            if (m_visibleCommandState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
                D3D12_RESOURCE_BARRIER toCopy = {};
                toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                toCopy.Transition.pResource = m_visibleCommandBuffer.Get();
                toCopy.Transition.StateBefore = m_visibleCommandState;
                toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
                toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                cmdList->ResourceBarrier(1, &toCopy);
                m_visibleCommandState = D3D12_RESOURCE_STATE_COPY_SOURCE;
            }

            cmdList->CopyBufferRegion(
                m_visibleCommandReadback.Get(),
                0,
                m_visibleCommandBuffer.Get(),
                0,
                readbackBytes);

            m_commandReadbackPending = true;
            m_commandReadbackRequested = false;
            m_commandReadbackCount = readbackCount;
        } else {
            m_commandReadbackRequested = false;
        }
    }

    // Copy command count to readback for CPU stats.
    if (m_commandCountReadback) {
        D3D12_RESOURCE_BARRIER toCopy = {};
        toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toCopy.Transition.pResource = m_commandCountBuffer.Get();
        toCopy.Transition.StateBefore = m_commandCountState;
        toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &toCopy);
        m_commandCountState = D3D12_RESOURCE_STATE_COPY_SOURCE;

        cmdList->CopyResource(m_commandCountReadback.Get(), m_commandCountBuffer.Get());
    }

    // Transition buffers for ExecuteIndirect.
    D3D12_RESOURCE_BARRIER postBarriers[2] = {};
    UINT postCount = 0;

    if (m_commandCountState != D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT) {
        postBarriers[postCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        postBarriers[postCount].Transition.pResource = m_commandCountBuffer.Get();
        postBarriers[postCount].Transition.StateBefore = m_commandCountState;
        postBarriers[postCount].Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        postBarriers[postCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++postCount;
        m_commandCountState = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    }

    if (m_visibleCommandState != D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT) {
        postBarriers[postCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        postBarriers[postCount].Transition.pResource = m_visibleCommandBuffer.Get();
        postBarriers[postCount].Transition.StateBefore = m_visibleCommandState;
        postBarriers[postCount].Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        postBarriers[postCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++postCount;
        m_visibleCommandState = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    }

    if (postCount > 0) {
        cmdList->ResourceBarrier(postCount, postBarriers);
    }

    return Result<void>::Ok();
}

} // namespace Cortex::Graphics
