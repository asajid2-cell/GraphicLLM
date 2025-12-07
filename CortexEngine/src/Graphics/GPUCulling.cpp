#include "GPUCulling.h"
#include "RHI/DX12Device.h"
#include "RHI/DX12CommandQueue.h"
#include "RHI/DX12Pipeline.h"
#include <spdlog/spdlog.h>
#include <d3dcompiler.h>
#include <cstring>

namespace Cortex::Graphics {

// GPU Culling constants (must match shader)
struct CullConstants {
    glm::mat4 viewProj;
    glm::vec4 frustumPlanes[6];
    glm::vec3 cameraPos;
    uint32_t instanceCount;
};

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

    auto cmdSigResult = CreateCommandSignature();
    if (cmdSigResult.IsErr()) {
        return cmdSigResult;
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
    m_visibleInstanceBuffer.Reset();
    m_indirectArgBuffer.Reset();
    m_counterBuffer.Reset();
    m_counterReadback.Reset();
    m_cullConstantBuffer.Reset();

    spdlog::info("GPU Culling Pipeline shutdown");
}

Result<void> GPUCullingPipeline::CreateRootSignature() {
    // Root signature for compute culling:
    // 0: CBV - Cull constants (view-proj, frustum planes, camera pos)
    // 1: SRV - Instance buffer (input, all instances)
    // 2: UAV - Visible instance buffer (output, compacted)
    // 3: UAV - Counter buffer (atomic append)
    // 4: UAV - Indirect argument buffer (output)

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

    // UAV for visible instance buffer
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rootParams[2].Descriptor.ShaderRegister = 0;
    rootParams[2].Descriptor.RegisterSpace = 0;
    rootParams[2].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // UAV for counter buffer
    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rootParams[3].Descriptor.ShaderRegister = 1;
    rootParams[3].Descriptor.RegisterSpace = 0;
    rootParams[3].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
    rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // UAV for indirect argument buffer
    rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rootParams[4].Descriptor.ShaderRegister = 2;
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
    // Compile the culling compute shader
    const char* shaderSource = R"(
// GPU Frustum Culling Compute Shader

struct InstanceData {
    float4x4 modelMatrix;
    float4 boundingSphere;  // xyz = center (object space), w = radius
    uint meshIndex;
    uint materialIndex;
    uint flags;
    uint _pad;
};

cbuffer CullConstants : register(b0) {
    float4x4 g_ViewProj;
    float4 g_FrustumPlanes[6];
    float3 g_CameraPos;
    uint g_InstanceCount;
};

StructuredBuffer<InstanceData> g_Instances : register(t0);
RWStructuredBuffer<InstanceData> g_VisibleInstances : register(u0);
RWByteAddressBuffer g_Counter : register(u1);
RWStructuredBuffer<uint4> g_IndirectArgs : register(u2);  // DrawIndexedArguments

[numthreads(64, 1, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    uint instanceIdx = DTid.x;
    if (instanceIdx >= g_InstanceCount) {
        return;
    }

    InstanceData instance = g_Instances[instanceIdx];

    // Transform bounding sphere center to world space
    float3 centerOS = instance.boundingSphere.xyz;
    float radius = instance.boundingSphere.w;

    // Get world-space center from model matrix
    float3 centerWS = mul(instance.modelMatrix, float4(centerOS, 1.0)).xyz;

    // Simple uniform scale extraction (take max of column lengths for safety)
    float3 scaleX = float3(instance.modelMatrix[0][0], instance.modelMatrix[1][0], instance.modelMatrix[2][0]);
    float3 scaleY = float3(instance.modelMatrix[0][1], instance.modelMatrix[1][1], instance.modelMatrix[2][1]);
    float3 scaleZ = float3(instance.modelMatrix[0][2], instance.modelMatrix[1][2], instance.modelMatrix[2][2]);
    float maxScale = max(max(length(scaleX), length(scaleY)), length(scaleZ));
    float worldRadius = radius * maxScale;

    // Frustum culling against 6 planes
    bool visible = true;
    [unroll]
    for (int i = 0; i < 6; i++) {
        float dist = dot(g_FrustumPlanes[i].xyz, centerWS) + g_FrustumPlanes[i].w;
        if (dist < -worldRadius) {
            visible = false;
            break;
        }
    }

    if (visible) {
        // Atomic increment and append to visible list
        uint visibleIdx;
        g_Counter.InterlockedAdd(0, 1, visibleIdx);

        g_VisibleInstances[visibleIdx] = instance;
    }
}
)";

    ComPtr<ID3DBlob> csBlob;
    ComPtr<ID3DBlob> errorBlob;

    UINT compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = D3DCompile(
        shaderSource,
        strlen(shaderSource),
        "GPUCulling.hlsl",
        nullptr,
        nullptr,
        "CSMain",
        "cs_5_1",
        compileFlags,
        0,
        &csBlob,
        &errorBlob
    );

    if (FAILED(hr)) {
        std::string errMsg = "Failed to compile GPU culling shader";
        if (errorBlob) {
            errMsg += ": ";
            errMsg += static_cast<const char*>(errorBlob->GetBufferPointer());
        }
        return Result<void>::Err(errMsg);
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.CS.pShaderBytecode = csBlob->GetBufferPointer();
    psoDesc.CS.BytecodeLength = csBlob->GetBufferSize();

    hr = m_device->GetDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_cullPipeline));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create GPU culling pipeline state");
    }

    return Result<void>::Ok();
}

Result<void> GPUCullingPipeline::CreateBuffers() {
    ID3D12Device* device = m_device->GetDevice();

    // Instance buffer (upload heap, written by CPU each frame)
    size_t instanceBufferSize = m_maxInstances * sizeof(GPUInstanceData);

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = instanceBufferSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_instanceBuffer)
    );
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create instance buffer");
    }

    // Visible instance buffer (default heap, UAV)
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    hr = device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_visibleInstanceBuffer)
    );
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create visible instance buffer");
    }

    // Counter buffer (4 bytes for atomic counter)
    bufferDesc.Width = sizeof(uint32_t);
    hr = device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_counterBuffer)
    );
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create counter buffer");
    }

    // Counter readback buffer
    D3D12_HEAP_PROPERTIES readbackHeap = {};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    hr = device->CreateCommittedResource(
        &readbackHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_counterReadback)
    );
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create counter readback buffer");
    }

    // Allocate descriptors for counter buffer UAV (needed for ClearUnorderedAccessViewUint)
    auto counterUAVResult = m_descriptorManager->AllocateCBV_SRV_UAV();
    if (counterUAVResult.IsErr()) {
        return Result<void>::Err("Failed to allocate counter UAV descriptor");
    }
    m_counterUAV = counterUAVResult.Value();

    auto counterStagingResult = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
    if (counterStagingResult.IsErr()) {
        return Result<void>::Err("Failed to allocate counter UAV staging descriptor");
    }
    m_counterUAVStaging = counterStagingResult.Value();

    // Create UAV for counter buffer (raw buffer, 1 uint32)
    D3D12_UNORDERED_ACCESS_VIEW_DESC counterUAVDesc = {};
    counterUAVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    counterUAVDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    counterUAVDesc.Buffer.FirstElement = 0;
    counterUAVDesc.Buffer.NumElements = 1;
    counterUAVDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;

    // Create UAV in shader-visible heap
    device->CreateUnorderedAccessView(m_counterBuffer.Get(), nullptr, &counterUAVDesc, m_counterUAV.cpu);
    // Create UAV in staging heap (CPU-only, for ClearUnorderedAccessViewUint)
    device->CreateUnorderedAccessView(m_counterBuffer.Get(), nullptr, &counterUAVDesc, m_counterUAVStaging.cpu);

    // Indirect argument buffer (for ExecuteIndirect)
    // Size for one DrawIndexedArguments per mesh (simplified: one draw per visible instance group)
    bufferDesc.Width = m_maxInstances * sizeof(DrawIndexedArguments);
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    hr = device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_indirectArgBuffer)
    );
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create indirect argument buffer");
    }

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

    return Result<void>::Ok();
}

Result<void> GPUCullingPipeline::CreateCommandSignature() {
    // Command signature for ExecuteIndirect with DrawIndexedInstanced
    D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
    argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

    D3D12_COMMAND_SIGNATURE_DESC cmdSigDesc = {};
    cmdSigDesc.ByteStride = sizeof(DrawIndexedArguments);
    cmdSigDesc.NumArgumentDescs = 1;
    cmdSigDesc.pArgumentDescs = &argDesc;
    cmdSigDesc.NodeMask = 0;

    HRESULT hr = m_device->GetDevice()->CreateCommandSignature(
        &cmdSigDesc,
        nullptr,  // No root signature needed for draw-only commands
        IID_PPV_ARGS(&m_commandSignature)
    );

    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create command signature for ExecuteIndirect");
    }

    return Result<void>::Ok();
}

Result<void> GPUCullingPipeline::UpdateInstances(
    ID3D12GraphicsCommandList* cmdList,
    const std::vector<GPUInstanceData>& instances)
{
    if (instances.empty()) {
        m_totalInstances = 0;
        return Result<void>::Ok();
    }

    if (instances.size() > m_maxInstances) {
        spdlog::warn("GPU Culling: Instance count {} exceeds max {}, truncating",
                     instances.size(), m_maxInstances);
    }

    m_totalInstances = static_cast<uint32_t>(std::min(instances.size(), static_cast<size_t>(m_maxInstances)));

    // Map and upload instance data
    void* mappedData = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    HRESULT hr = m_instanceBuffer->Map(0, &readRange, &mappedData);
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to map instance buffer");
    }

    memcpy(mappedData, instances.data(), m_totalInstances * sizeof(GPUInstanceData));
    m_instanceBuffer->Unmap(0, nullptr);

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

    // Update constants
    CullConstants constants;
    constants.viewProj = viewProj;
    constants.cameraPos = cameraPos;
    constants.instanceCount = m_totalInstances;

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

    // Clear the counter buffer to 0 using ClearUnorderedAccessViewUint
    // This requires both a GPU descriptor (shader-visible) and CPU descriptor
    const UINT clearValues[4] = { 0, 0, 0, 0 };
    cmdList->ClearUnorderedAccessViewUint(
        m_counterUAV.gpu,           // GPU descriptor handle (shader-visible heap)
        m_counterUAVStaging.cpu,    // CPU descriptor handle (staging heap)
        m_counterBuffer.Get(),
        clearValues,
        0,                          // No rects
        nullptr
    );

    // UAV barrier to ensure clear completes before compute dispatch
    D3D12_RESOURCE_BARRIER clearBarrier = {};
    clearBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    clearBarrier.UAV.pResource = m_counterBuffer.Get();
    cmdList->ResourceBarrier(1, &clearBarrier);

    // Set pipeline and root signature
    cmdList->SetComputeRootSignature(m_rootSignature.Get());
    cmdList->SetPipelineState(m_cullPipeline.Get());

    // Bind resources
    cmdList->SetComputeRootConstantBufferView(0, m_cullConstantBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootShaderResourceView(1, m_instanceBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootUnorderedAccessView(2, m_visibleInstanceBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootUnorderedAccessView(3, m_counterBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootUnorderedAccessView(4, m_indirectArgBuffer->GetGPUVirtualAddress());

    // Dispatch compute shader (64 threads per group)
    uint32_t numGroups = (m_totalInstances + 63) / 64;
    cmdList->Dispatch(numGroups, 1, 1);

    // Barrier to ensure compute is complete before reading results
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = m_visibleInstanceBuffer.Get();
    cmdList->ResourceBarrier(1, &barrier);

    return Result<void>::Ok();
}

} // namespace Cortex::Graphics
