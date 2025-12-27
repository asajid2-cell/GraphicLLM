#include "GPUCulling.h"
#include "RHI/DX12Device.h"
#include "RHI/DX12CommandQueue.h"
#include "RHI/DX12Pipeline.h"
#include <algorithm>
#include <spdlog/spdlog.h>
#include <cstring>
#include <glm/gtc/matrix_inverse.hpp>

namespace Cortex::Graphics {

// GPU Culling constants (must match shader). Layout is 16-byte aligned and
// avoids relying on glm::vec3 packing rules by using explicit arrays.
struct alignas(16) CullConstants {
    glm::mat4 viewProj;
    glm::vec4 frustumPlanes[6];
    float cameraPos[3];
    uint32_t instanceCount;
    glm::uvec4 occlusionParams0;  // x=forceVisible, y=hzbEnabled, z=hzbMipCount, w=streakThreshold
    glm::uvec4 occlusionParams1;  // x=hzbWidth, y=hzbHeight, z=historySize, w=debugEnabled
    glm::vec4 occlusionParams2;   // x=invW, y=invH, z=proj00, w=proj11
    glm::vec4 occlusionParams3;   // x=near, y=far, z=epsilon, w=cameraMotionWS
    glm::mat4 hzbViewMatrix;
    glm::mat4 hzbViewProjMatrix;
    glm::vec4 hzbCameraPos;       // xyz=cameraPosWS
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
    m_occlusionHistoryA.Reset();
    m_occlusionHistoryB.Reset();
    m_visibilityMaskBuffer.Reset();
    m_dummyHzbTexture.Reset();

    spdlog::info("GPU Culling Pipeline shutdown");
}

void GPUCullingPipeline::SetHZBForOcclusion(
    ID3D12Resource* hzbTexture,
    uint32_t hzbWidth,
    uint32_t hzbHeight,
    uint32_t hzbMipCount,
    const glm::mat4& hzbViewMatrix,
    const glm::mat4& hzbViewProjMatrix,
    const glm::vec3& hzbCameraPosWS,
    float cameraNearPlane,
    float cameraFarPlane,
    bool enabled)
{
    m_hzbEnabled = enabled && (hzbTexture != nullptr) && (hzbMipCount > 0) && (hzbWidth > 0) && (hzbHeight > 0);
    m_hzbTexture = hzbTexture;
    m_hzbWidth = hzbWidth;
    m_hzbHeight = hzbHeight;
    m_hzbMipCount = hzbMipCount;
    m_hzbViewMatrix = hzbViewMatrix;
    m_hzbViewProjMatrix = hzbViewProjMatrix;
    m_hzbCameraPosWS = hzbCameraPosWS;
    m_hzbNearPlane = cameraNearPlane;
    m_hzbFarPlane = cameraFarPlane;

    if (!m_descriptorManager || !m_device) {
        return;
    }

    ID3D12Device* device = m_device->GetDevice();
    if (!device) {
        return;
    }

    // Update a CPU-only staging SRV; we copy it into a per-frame transient slot
    // during DispatchCulling() to avoid rewriting in-flight shader-visible descriptors.
    if (!m_hzbSrvStaging.IsValid()) {
        auto srvRes = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (srvRes.IsOk()) {
            m_hzbSrvStaging = srvRes.Value();
        }
    }
    if (!m_hzbSrvStaging.IsValid()) {
        return;
    }

    ID3D12Resource* srvResource = m_hzbEnabled ? m_hzbTexture : m_dummyHzbTexture.Get();
    if (!srvResource) {
        return;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = (m_hzbEnabled ? static_cast<UINT>(m_hzbMipCount) : 1u);
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    device->CreateShaderResourceView(srvResource, &srvDesc, m_hzbSrvStaging.cpu);
}

Result<void> GPUCullingPipeline::CreateRootSignature() {
    // Root signature for compute culling:
    // 0: CBV - Cull constants (view-proj, frustum planes, camera pos)
    // 1: SRV - Instance buffer (input, all instances)
    // 2: SRV - All indirect commands (input)
    // 3: SRV - Occlusion history (input)
    // 4: UAV - Visible command buffer (output)
    // 5: UAV - Command count buffer (atomic append)
    // 6: UAV - Occlusion history (output)
    // 7: UAV - Debug counters/sample (u3)
    // 8: UAV - Visibility mask (u4, one uint32 per instance)
    // 9: SRV table - HZB texture (t2)

    D3D12_ROOT_PARAMETER1 rootParams[10] = {};
    D3D12_DESCRIPTOR_RANGE1 hzbRange{};
    hzbRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    hzbRange.NumDescriptors = 1;
    hzbRange.BaseShaderRegister = 2;
    hzbRange.RegisterSpace = 0;
    // HZB is rebuilt later in the same command list (for next-frame occlusion),
    // so the underlying resource data is not static over the lifetime of the
    // command list.
    hzbRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
    hzbRange.OffsetInDescriptorsFromTableStart = 0;

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

    // SRV for occlusion history input
    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParams[3].Descriptor.ShaderRegister = 3;
    rootParams[3].Descriptor.RegisterSpace = 0;
    rootParams[3].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
    rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // UAV for visible command buffer
    rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rootParams[4].Descriptor.ShaderRegister = 0;
    rootParams[4].Descriptor.RegisterSpace = 0;
    rootParams[4].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
    rootParams[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // UAV for command count buffer
    rootParams[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rootParams[5].Descriptor.ShaderRegister = 1;
    rootParams[5].Descriptor.RegisterSpace = 0;
    rootParams[5].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
    rootParams[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // UAV for occlusion history output
    rootParams[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rootParams[6].Descriptor.ShaderRegister = 2;
    rootParams[6].Descriptor.RegisterSpace = 0;
    rootParams[6].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
    rootParams[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // UAV for debug counters/sample (raw buffer)
    rootParams[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rootParams[7].Descriptor.ShaderRegister = 3;
    rootParams[7].Descriptor.RegisterSpace = 0;
    rootParams[7].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
    rootParams[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // UAV for per-instance visibility mask (raw buffer)
    rootParams[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rootParams[8].Descriptor.ShaderRegister = 4;
    rootParams[8].Descriptor.RegisterSpace = 0;
    rootParams[8].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
    rootParams[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // SRV descriptor table for HZB texture (t2)
    rootParams[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[9].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[9].DescriptorTable.pDescriptorRanges = &hzbRange;
    rootParams[9].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootSigDesc.Desc_1_1.NumParameters = static_cast<UINT>(_countof(rootParams));
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

    // Visibility mask buffer (one uint32 per instance). Consumers can bind this
    // as a ByteAddressBuffer SRV to skip drawing occluded instances.
    {
        D3D12_RESOURCE_DESC maskDesc = bufferDesc;
        maskDesc.Width = static_cast<UINT64>(m_maxInstances) * sizeof(uint32_t);
        maskDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        hr = device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &maskDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&m_visibilityMaskBuffer)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create visibility mask buffer");
        }
        m_visibilityMaskBuffer->SetName(L"GPUCullingVisibilityMask");
        m_visibilityMaskState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    // Debug buffer (counters + sample). Writes are gated by constants, but the
    // resource is always available so the root UAV is always valid.
    {
        D3D12_RESOURCE_DESC debugDesc = bufferDesc;
        debugDesc.Width = 64;
        debugDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        hr = device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &debugDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&m_debugBuffer)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create GPU culling debug buffer");
        }
        m_debugBuffer->SetName(L"GPUCullingDebugBuffer");

        D3D12_HEAP_PROPERTIES readbackHeapDbg = {};
        readbackHeapDbg.Type = D3D12_HEAP_TYPE_READBACK;
        debugDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        hr = device->CreateCommittedResource(
            &readbackHeapDbg,
            D3D12_HEAP_FLAG_NONE,
            &debugDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_debugReadback)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create GPU culling debug readback buffer");
        }
        m_debugReadback->SetName(L"GPUCullingDebugReadback");

        auto dbgUavRes = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (dbgUavRes.IsErr()) {
            return Result<void>::Err("Failed to allocate debug UAV descriptor");
        }
        m_debugUAV = dbgUavRes.Value();

        auto dbgUavStagingRes = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (dbgUavStagingRes.IsErr()) {
            return Result<void>::Err("Failed to allocate debug UAV staging descriptor");
        }
        m_debugUAVStaging = dbgUavStagingRes.Value();

        D3D12_UNORDERED_ACCESS_VIEW_DESC debugUavDesc = {};
        debugUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        debugUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        debugUavDesc.Buffer.FirstElement = 0;
        debugUavDesc.Buffer.NumElements = static_cast<UINT>(debugDesc.Width / 4);
        debugUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;

        device->CreateUnorderedAccessView(m_debugBuffer.Get(), nullptr, &debugUavDesc, m_debugUAV.cpu);
        device->CreateUnorderedAccessView(m_debugBuffer.Get(), nullptr, &debugUavDesc, m_debugUAVStaging.cpu);

        m_debugState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
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

    // Occlusion history buffers (ping-pong), one uint32 per instance.
    bufferDesc.Width = static_cast<UINT64>(m_maxInstances) * sizeof(uint32_t);
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    hr = device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_occlusionHistoryA)
    );
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create occlusion history buffer A");
    }

    hr = device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_occlusionHistoryB)
    );
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create occlusion history buffer B");
    }

    // Allocate descriptors for history UAV clears.
    auto histAUAV = m_descriptorManager->AllocateCBV_SRV_UAV();
    if (histAUAV.IsErr()) {
        return Result<void>::Err("Failed to allocate history A UAV descriptor");
    }
    m_historyAUAV = histAUAV.Value();

    auto histAStaging = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
    if (histAStaging.IsErr()) {
        return Result<void>::Err("Failed to allocate history A UAV staging descriptor");
    }
    m_historyAUAVStaging = histAStaging.Value();

    auto histBUAV = m_descriptorManager->AllocateCBV_SRV_UAV();
    if (histBUAV.IsErr()) {
        return Result<void>::Err("Failed to allocate history B UAV descriptor");
    }
    m_historyBUAV = histBUAV.Value();

    auto histBStaging = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
    if (histBStaging.IsErr()) {
        return Result<void>::Err("Failed to allocate history B UAV staging descriptor");
    }
    m_historyBUAVStaging = histBStaging.Value();

    D3D12_UNORDERED_ACCESS_VIEW_DESC histUavDesc{};
    histUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    histUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    histUavDesc.Buffer.FirstElement = 0;
    histUavDesc.Buffer.NumElements = m_maxInstances;
    histUavDesc.Buffer.StructureByteStride = 0;
    histUavDesc.Buffer.CounterOffsetInBytes = 0;
    histUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;

    device->CreateUnorderedAccessView(m_occlusionHistoryA.Get(), nullptr, &histUavDesc, m_historyAUAV.cpu);
    device->CreateUnorderedAccessView(m_occlusionHistoryA.Get(), nullptr, &histUavDesc, m_historyAUAVStaging.cpu);
    device->CreateUnorderedAccessView(m_occlusionHistoryB.Get(), nullptr, &histUavDesc, m_historyBUAV.cpu);
    device->CreateUnorderedAccessView(m_occlusionHistoryB.Get(), nullptr, &histUavDesc, m_historyBUAVStaging.cpu);

    m_historyAState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    m_historyBState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    m_historyPingPong = false;
    m_historyInitialized = false;

    // Dummy HZB texture used to keep the HZB SRV root parameter valid even when
    // occlusion culling is disabled or the renderer hasn't built an HZB yet.
    {
        D3D12_RESOURCE_DESC texDesc{};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Alignment = 0;
        texDesc.Width = 1;
        texDesc.Height = 1;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_R32_FLOAT;
        texDesc.SampleDesc.Count = 1;
        texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        hr = device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            nullptr,
            IID_PPV_ARGS(&m_dummyHzbTexture)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create dummy HZB texture");
        }
        m_dummyHzbTexture->SetName(L"DummyHZBTexture");

        auto hzbSrvRes = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (hzbSrvRes.IsErr()) {
            return Result<void>::Err("Failed to allocate HZB SRV descriptor");
        }
        m_hzbSrv = hzbSrvRes.Value();

        auto hzbSrvStagingRes = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (hzbSrvStagingRes.IsErr()) {
            return Result<void>::Err("Failed to allocate HZB staging SRV descriptor");
        }
        m_hzbSrvStaging = hzbSrvStagingRes.Value();

        D3D12_SHADER_RESOURCE_VIEW_DESC hzbSrvDesc{};
        hzbSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        hzbSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        hzbSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        hzbSrvDesc.Texture2D.MostDetailedMip = 0;
        hzbSrvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(m_dummyHzbTexture.Get(), &hzbSrvDesc, m_hzbSrv.cpu);
        device->CreateShaderResourceView(m_dummyHzbTexture.Get(), &hzbSrvDesc, m_hzbSrvStaging.cpu);
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

    m_debugStats.enabled = m_debugEnabled;

    uint32_t* mapped = nullptr;
    D3D12_RANGE readRange = {0, sizeof(uint32_t)};
    HRESULT hr = m_commandCountReadback->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
    if (SUCCEEDED(hr) && mapped) {
        m_visibleCount = *mapped;
        m_commandCountReadback->Unmap(0, nullptr);
    }

    if (m_commandReadbackPending && m_visibleCommandReadback && m_commandReadbackCount > 0) {
        const size_t readbackBytes = static_cast<size_t>(m_commandReadbackCount) * sizeof(IndirectCommand);
        D3D12_RANGE cmdReadRange = { 0, readbackBytes };
        void* commandData = nullptr;
        HRESULT cmdHr = m_visibleCommandReadback->Map(0, &cmdReadRange, &commandData);
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

    if (m_debugReadbackPending && m_debugReadback) {
        void* mappedData = nullptr;
        D3D12_RANGE dbgRange = {0, 64};
        HRESULT dbgHr = m_debugReadback->Map(0, &dbgRange, &mappedData);
        if (SUCCEEDED(dbgHr) && mappedData) {
            const auto* u32 = static_cast<const uint32_t*>(mappedData);
            m_debugStats.valid = true;
            m_debugStats.tested = u32[0];
            m_debugStats.frustumCulled = u32[1];
            m_debugStats.occluded = u32[2];
            m_debugStats.visible = u32[3];

            float nearDepth = 0.0f;
            float hzbDepth = 0.0f;
            std::memcpy(&nearDepth, &u32[4], sizeof(float));
            std::memcpy(&hzbDepth, &u32[5], sizeof(float));
            m_debugStats.sampleNearDepth = nearDepth;
            m_debugStats.sampleHzbDepth = hzbDepth;
            m_debugStats.sampleMip = u32[6];
            m_debugStats.sampleFlags = u32[7];

            m_debugReadback->Unmap(0, nullptr);
        } else {
            m_debugStats.valid = false;
        }
        m_debugReadbackPending = false;
    } else {
        m_debugStats.valid = false;
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
    constants.cameraPos[0] = cameraPos.x;
    constants.cameraPos[1] = cameraPos.y;
    constants.cameraPos[2] = cameraPos.z;
    constants.instanceCount = m_totalInstances;

    // HZB occlusion culling using simplified depth comparison.
    // Changed from complex world-space near-point to simpler view-space centerZ - radius.
    const uint32_t hzbEnabled =
        (m_hzbEnabled && m_hzbTexture && (m_hzbMipCount > 0) && (m_hzbWidth > 0) && (m_hzbHeight > 0)) ? 1u : 0u;

    // Streak threshold: require N consecutive occluded frames before culling.
    // Higher values reduce popping/flickering but delay culling slightly.
    // At 60fps, 8 frames = 133ms delay before occlusion kicks in.
    constexpr uint32_t kOcclusionStreakThreshold = 8u;
    constants.occlusionParams0 = glm::uvec4(
        m_forceVisible ? 1u : 0u,
        hzbEnabled,
        m_hzbMipCount,
        kOcclusionStreakThreshold);

    constants.occlusionParams1 =
        glm::uvec4(m_hzbWidth, m_hzbHeight, m_maxInstances, m_debugEnabled ? 1u : 0u);

    const float invW = (m_hzbWidth > 0) ? (1.0f / static_cast<float>(m_hzbWidth)) : 0.0f;
    const float invH = (m_hzbHeight > 0) ? (1.0f / static_cast<float>(m_hzbHeight)) : 0.0f;

    // Projection scale terms (P00, P11) used for screen-radius estimation.
    // Derive the projection matrix from the captured view + view-projection.
    const glm::mat4 proj = m_hzbViewProjMatrix * glm::inverse(m_hzbViewMatrix);
    constants.occlusionParams2 = glm::vec4(invW, invH, proj[0][0], proj[1][1]);

    // View-space depth epsilon (meters/units). This is intentionally larger than
    // the old NDC-depth epsilon because HZB now stores view-space Z.
    // Increased to 5cm to be more conservative and reduce false occlusion.
    constexpr float kHzbEpsilon = 0.05f;
    const float cameraMotionWS =
        glm::length(glm::vec3(constants.cameraPos[0], constants.cameraPos[1], constants.cameraPos[2]) - m_hzbCameraPosWS);
    constants.occlusionParams3 = glm::vec4(m_hzbNearPlane, m_hzbFarPlane, kHzbEpsilon, cameraMotionWS);
    constants.hzbViewMatrix = m_hzbViewMatrix;
    constants.hzbViewProjMatrix = m_hzbViewProjMatrix;
    constants.hzbCameraPos = glm::vec4(m_hzbCameraPosWS, 0.0f);

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

    if (m_visibilityMaskBuffer && m_visibilityMaskState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_visibilityMaskBuffer.Get();
        barrier.Transition.StateBefore = m_visibilityMaskState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_visibilityMaskState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    // Keep the debug UAV in a valid state for dispatch even when debug writes are disabled.
    if (m_debugBuffer && m_debugState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_debugBuffer.Get();
        barrier.Transition.StateBefore = m_debugState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_debugState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
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

    // Ensure descriptor heap is bound (ClearUAV + HZB SRV table use it).
    if (m_descriptorManager) {
        ID3D12DescriptorHeap* heaps[] = { m_descriptorManager->GetCBV_SRV_UAV_Heap() };
        cmdList->SetDescriptorHeaps(1, heaps);
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

    // One-time init: clear both occlusion history buffers so streaks start at 0.
    if (!m_historyInitialized && m_occlusionHistoryA && m_occlusionHistoryB &&
        m_historyAUAV.IsValid() && m_historyBUAV.IsValid()) {
        cmdList->ClearUnorderedAccessViewUint(
            m_historyAUAV.gpu, m_historyAUAVStaging.cpu, m_occlusionHistoryA.Get(),
            clearValues, 0, nullptr);
        cmdList->ClearUnorderedAccessViewUint(
            m_historyBUAV.gpu, m_historyBUAVStaging.cpu, m_occlusionHistoryB.Get(),
            clearValues, 0, nullptr);
        m_historyInitialized = true;
    }

    // Clear debug counters/sample (optional).
    if (m_debugEnabled && m_debugBuffer && m_debugUAV.IsValid()) {
        cmdList->ClearUnorderedAccessViewUint(
            m_debugUAV.gpu,
            m_debugUAVStaging.cpu,
            m_debugBuffer.Get(),
            clearValues,
            0,
            nullptr);
    }

    // UAV barriers to ensure clears complete before compute dispatch.
    D3D12_RESOURCE_BARRIER clearBarriers[4] = {};
    uint32_t barrierCount = 0;
    clearBarriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    clearBarriers[barrierCount].UAV.pResource = m_commandCountBuffer.Get();
    ++barrierCount;
    if (m_historyInitialized) {
        clearBarriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        clearBarriers[barrierCount].UAV.pResource = m_occlusionHistoryA.Get();
        ++barrierCount;
        clearBarriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        clearBarriers[barrierCount].UAV.pResource = m_occlusionHistoryB.Get();
        ++barrierCount;
    }
    if (m_debugEnabled && m_debugBuffer) {
        clearBarriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        clearBarriers[barrierCount].UAV.pResource = m_debugBuffer.Get();
        ++barrierCount;
    }
    cmdList->ResourceBarrier(barrierCount, clearBarriers);

    // Select occlusion history buffers for this dispatch.
    ID3D12Resource* historyIn = m_historyPingPong ? m_occlusionHistoryB.Get() : m_occlusionHistoryA.Get();
    ID3D12Resource* historyOut = m_historyPingPong ? m_occlusionHistoryA.Get() : m_occlusionHistoryB.Get();
    D3D12_RESOURCE_STATES* historyInState = m_historyPingPong ? &m_historyBState : &m_historyAState;
    D3D12_RESOURCE_STATES* historyOutState = m_historyPingPong ? &m_historyAState : &m_historyBState;

    // Transition history buffers to the required states.
    if (historyIn && *historyInState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = historyIn;
        barrier.Transition.StateBefore = *historyInState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        *historyInState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }

    if (historyOut && *historyOutState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = historyOut;
        barrier.Transition.StateBefore = *historyOutState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        *historyOutState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    // Set pipeline and root signature
    cmdList->SetComputeRootSignature(m_rootSignature.Get());
    cmdList->SetPipelineState(m_cullPipeline.Get());

    // Bind resources
    cmdList->SetComputeRootConstantBufferView(0, m_cullConstantBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootShaderResourceView(1, m_instanceBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootShaderResourceView(2, m_allCommandBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootShaderResourceView(3, historyIn ? historyIn->GetGPUVirtualAddress() : 0);
    cmdList->SetComputeRootUnorderedAccessView(4, m_visibleCommandBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootUnorderedAccessView(5, m_commandCountBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootUnorderedAccessView(6, historyOut ? historyOut->GetGPUVirtualAddress() : 0);
    cmdList->SetComputeRootUnorderedAccessView(7, m_debugBuffer ? m_debugBuffer->GetGPUVirtualAddress() : 0);
    cmdList->SetComputeRootUnorderedAccessView(8, m_visibilityMaskBuffer ? m_visibilityMaskBuffer->GetGPUVirtualAddress() : 0);

    // Bind HZB SRV via a per-frame transient slot to avoid rewriting a shader-visible
    // descriptor that may still be referenced by an in-flight command list.
    DescriptorHandle hzbSrvForDispatch = m_hzbSrv; // fallback dummy (always valid)
    if (m_descriptorManager && m_hzbSrvStaging.IsValid()) {
        auto transientRes = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
        if (transientRes.IsOk()) {
            hzbSrvForDispatch = transientRes.Value();
            m_device->GetDevice()->CopyDescriptorsSimple(
                1,
                hzbSrvForDispatch.cpu,
                m_hzbSrvStaging.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }
    cmdList->SetComputeRootDescriptorTable(9, hzbSrvForDispatch.gpu);

    // Dispatch compute shader (64 threads per group)
    uint32_t numGroups = (m_totalInstances + 63) / 64;
    cmdList->Dispatch(numGroups, 1, 1);

    // Barrier to ensure compute writes are visible
    D3D12_RESOURCE_BARRIER uavBarriers[5] = {};
    uavBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarriers[0].UAV.pResource = m_visibleCommandBuffer.Get();
    uavBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarriers[1].UAV.pResource = m_commandCountBuffer.Get();
    uavBarriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarriers[2].UAV.pResource = historyOut;
    uint32_t uavBarrierCount = historyOut ? 3u : 2u;
    if (m_visibilityMaskBuffer) {
        uavBarriers[uavBarrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarriers[uavBarrierCount].UAV.pResource = m_visibilityMaskBuffer.Get();
        ++uavBarrierCount;
    }
    if (m_debugEnabled && m_debugBuffer) {
        uavBarriers[uavBarrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarriers[uavBarrierCount].UAV.pResource = m_debugBuffer.Get();
        ++uavBarrierCount;
    }
    cmdList->ResourceBarrier(uavBarrierCount, uavBarriers);

    // Swap occlusion history buffers for next frame.
    m_historyPingPong = !m_historyPingPong;

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

    // Copy debug counters/sample to readback (optional).
    if (m_debugEnabled && m_debugReadback && m_debugBuffer) {
        D3D12_RESOURCE_BARRIER toCopy = {};
        toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toCopy.Transition.pResource = m_debugBuffer.Get();
        toCopy.Transition.StateBefore = m_debugState;
        toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &toCopy);
        m_debugState = D3D12_RESOURCE_STATE_COPY_SOURCE;

        cmdList->CopyResource(m_debugReadback.Get(), m_debugBuffer.Get());
        m_debugReadbackPending = true;
    }

    // Transition buffers for ExecuteIndirect.
    D3D12_RESOURCE_BARRIER postBarriers[3] = {};
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

    constexpr D3D12_RESOURCE_STATES kVisibilityMaskSrvState =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    if (m_visibilityMaskBuffer && m_visibilityMaskState != kVisibilityMaskSrvState) {
        postBarriers[postCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        postBarriers[postCount].Transition.pResource = m_visibilityMaskBuffer.Get();
        postBarriers[postCount].Transition.StateBefore = m_visibilityMaskState;
        postBarriers[postCount].Transition.StateAfter = kVisibilityMaskSrvState;
        postBarriers[postCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++postCount;
        m_visibilityMaskState = kVisibilityMaskSrvState;
    }

    if (postCount > 0) {
        cmdList->ResourceBarrier(postCount, postBarriers);
    }

    return Result<void>::Ok();
}

} // namespace Cortex::Graphics
