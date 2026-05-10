#include "Renderer.h"

#include "Passes/HZBPass.h"
#include "RenderGraph.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <span>
#include <utility>

namespace Cortex::Graphics {

namespace {

constexpr D3D12_RESOURCE_STATES kDepthSampleState =
    D3D12_RESOURCE_STATE_DEPTH_READ |
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

uint32_t CalcHZBMipCount(uint32_t width, uint32_t height) {
    width = std::max(1u, width);
    height = std::max(1u, height);

    // D3D12 mip-chain sizing uses floor division each level:
    //   next = max(1, current / 2).
    // The maximum mip count is therefore based on the largest dimension.
    uint32_t maxDim = std::max(width, height);
    uint32_t mipCount = 1;
    while (maxDim > 1u) {
        maxDim >>= 1u;
        ++mipCount;
    }
    return mipCount;
}

} // namespace

Result<void> Renderer::CreateHZBResources() {
    if (!m_services.device || !m_services.descriptorManager || !m_depthResources.buffer) {
        return Result<void>::Err("CreateHZBResources: renderer not initialized or depth buffer missing");
    }

    const D3D12_RESOURCE_DESC depthDesc = m_depthResources.buffer->GetDesc();
    const uint32_t width = std::max<uint32_t>(1u, static_cast<uint32_t>(depthDesc.Width));
    const uint32_t height = std::max<uint32_t>(1u, depthDesc.Height);
    const uint32_t mipCount = CalcHZBMipCount(width, height);

    if (m_hzbResources.texture && m_hzbResources.width == width && m_hzbResources.height == height && m_hzbResources.mipCount == mipCount &&
        m_hzbResources.dispatchTablesValid) {
        return Result<void>::Ok();
    }

    // Defer deletion of old HZB texture; it may still be referenced by in-flight command lists.
    if (m_hzbResources.texture) {
        DeferredGPUDeletionQueue::Instance().QueueResource(std::move(m_hzbResources.texture));
    }
    m_hzbResources.fullSRV = {};
    m_hzbResources.mipSRVStaging.clear();
    m_hzbResources.mipUAVStaging.clear();
    m_hzbResources.dispatchTablesValid = false;
    for (auto& table : m_hzbResources.dispatchSrvTables) {
        table.clear();
    }
    for (auto& table : m_hzbResources.dispatchUavTables) {
        table.clear();
    }
    m_hzbResources.width = width;
    m_hzbResources.height = height;
    m_hzbResources.mipCount = mipCount;
    m_hzbResources.debugMip = 0;
    m_hzbResources.resourceState = D3D12_RESOURCE_STATE_COMMON;
    m_hzbResources.valid = false;
    m_hzbResources.captureValid = false;
    m_hzbResources.captureFrameCounter = 0;

    D3D12_RESOURCE_DESC hzbDesc{};
    hzbDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    hzbDesc.Alignment = 0;
    hzbDesc.Width = width;
    hzbDesc.Height = height;
    hzbDesc.DepthOrArraySize = 1;
    hzbDesc.MipLevels = static_cast<UINT16>(mipCount);
    hzbDesc.Format = DXGI_FORMAT_R32_FLOAT;
    hzbDesc.SampleDesc.Count = 1;
    hzbDesc.SampleDesc.Quality = 0;
    hzbDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    hzbDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = m_services.device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &hzbDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&m_hzbResources.texture)
    );

    if (FAILED(hr)) {
        m_hzbResources.texture.Reset();
        return Result<void>::Err("CreateHZBResources: failed to create HZB texture");
    }

    m_hzbResources.texture->SetName(L"HZBTexture");

    m_hzbResources.mipSRVStaging.reserve(mipCount);
    m_hzbResources.mipUAVStaging.reserve(mipCount);

    for (uint32_t mip = 0; mip < mipCount; ++mip) {
        auto srvResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (srvResult.IsErr()) {
            return Result<void>::Err("CreateHZBResources: failed to allocate staging SRV: " + srvResult.Error());
        }
        auto uavResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (uavResult.IsErr()) {
            return Result<void>::Err("CreateHZBResources: failed to allocate staging UAV: " + uavResult.Error());
        }

        DescriptorHandle srvHandle = srvResult.Value();
        DescriptorHandle uavHandle = uavResult.Value();

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MostDetailedMip = mip;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.PlaneSlice = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        m_services.device->GetDevice()->CreateShaderResourceView(
            m_hzbResources.texture.Get(),
            &srvDesc,
            srvHandle.cpu
        );

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = mip;
        uavDesc.Texture2D.PlaneSlice = 0;

        m_services.device->GetDevice()->CreateUnorderedAccessView(
            m_hzbResources.texture.Get(),
            nullptr,
            &uavDesc,
            uavHandle.cpu
        );

        m_hzbResources.mipSRVStaging.push_back(srvHandle);
        m_hzbResources.mipUAVStaging.push_back(uavHandle);
    }

    // Create a full-mip SRV for debug visualizations and shaders that choose a mip level.
    {
        auto fullSrvResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (fullSrvResult.IsErr()) {
            return Result<void>::Err("CreateHZBResources: failed to allocate full-mip SRV: " + fullSrvResult.Error());
        }
        m_hzbResources.fullSRV = fullSrvResult.Value();

        D3D12_SHADER_RESOURCE_VIEW_DESC fullDesc{};
        fullDesc.Format = DXGI_FORMAT_R32_FLOAT;
        fullDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        fullDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        fullDesc.Texture2D.MostDetailedMip = 0;
        fullDesc.Texture2D.MipLevels = static_cast<UINT>(mipCount);
        fullDesc.Texture2D.PlaneSlice = 0;
        fullDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        m_services.device->GetDevice()->CreateShaderResourceView(
            m_hzbResources.texture.Get(),
            &fullDesc,
            m_hzbResources.fullSRV.cpu
        );
    }

    for (uint32_t frame = 0; frame < kFrameCount; ++frame) {
        m_hzbResources.dispatchSrvTables[frame].resize(mipCount);
        m_hzbResources.dispatchUavTables[frame].resize(mipCount);
        for (uint32_t mip = 0; mip < mipCount; ++mip) {
            auto srvResult = m_services.descriptorManager->AllocateCBV_SRV_UAV();
            if (srvResult.IsErr()) {
                return Result<void>::Err("CreateHZBResources: failed to allocate shader-visible HZB SRV: " +
                                         srvResult.Error());
            }
            auto uavResult = m_services.descriptorManager->AllocateCBV_SRV_UAV();
            if (uavResult.IsErr()) {
                return Result<void>::Err("CreateHZBResources: failed to allocate shader-visible HZB UAV: " +
                                         uavResult.Error());
            }

            m_hzbResources.dispatchSrvTables[frame][mip] = srvResult.Value();
            m_hzbResources.dispatchUavTables[frame][mip] = uavResult.Value();

            const DescriptorHandle sourceSrv = (mip == 0) ? m_depthResources.srv : m_hzbResources.mipSRVStaging[mip - 1];
            const DescriptorHandle sourceUav = m_hzbResources.mipUAVStaging[mip];
            if (!sourceSrv.IsValid() || !sourceUav.IsValid()) {
                return Result<void>::Err("CreateHZBResources: invalid source descriptor while building HZB dispatch tables");
            }

            m_services.device->GetDevice()->CopyDescriptorsSimple(
                1,
                m_hzbResources.dispatchSrvTables[frame][mip].cpu,
                sourceSrv.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            m_services.device->GetDevice()->CopyDescriptorsSimple(
                1,
                m_hzbResources.dispatchUavTables[frame][mip].cpu,
                sourceUav.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }
    m_hzbResources.dispatchTablesValid = true;

    spdlog::info("HZB resources created: {}x{}, mips={}", width, height, mipCount);
    return Result<void>::Ok();
}

void Renderer::BuildHZBFromDepth() {
    if (!m_services.device || !m_commandResources.graphicsList || !m_services.descriptorManager) {
        return;
    }
    if (!m_pipelineState.computeRootSignature || !m_pipelineState.hzbInit || !m_pipelineState.hzbDownsample) {
        return;
    }
    if (!m_depthResources.buffer || !m_depthResources.srv.IsValid()) {
        return;
    }

    auto resResult = CreateHZBResources();
    if (resResult.IsErr()) {
        spdlog::warn("BuildHZBFromDepth: {}", resResult.Error());
        return;
    }
    if (!m_hzbResources.texture || m_hzbResources.mipCount == 0 || m_hzbResources.mipSRVStaging.size() != m_hzbResources.mipCount ||
        m_hzbResources.mipUAVStaging.size() != m_hzbResources.mipCount) {
        return;
    }

    if (m_depthResources.resourceState != kDepthSampleState) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_depthResources.buffer.Get();
        barrier.Transition.StateBefore = m_depthResources.resourceState;
        barrier.Transition.StateAfter = kDepthSampleState;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandResources.graphicsList->ResourceBarrier(1, &barrier);
        m_depthResources.resourceState = kDepthSampleState;
    }

    if (m_hzbResources.resourceState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_hzbResources.texture.Get();
        barrier.Transition.StateBefore = m_hzbResources.resourceState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandResources.graphicsList->ResourceBarrier(1, &barrier);
        m_hzbResources.resourceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    const bool compactHzbRoot = m_pipelineState.singleSrvUavComputeRootSignature != nullptr;
    ID3D12RootSignature* hzbRootSignature =
        compactHzbRoot ? m_pipelineState.singleSrvUavComputeRootSignature.Get() : m_pipelineState.computeRootSignature->GetRootSignature();
    const UINT hzbFrameConstantsRoot = compactHzbRoot ? 0u : 1u;
    const UINT hzbSrvTableRoot = compactHzbRoot ? 1u : 3u;
    const UINT hzbUavTableRoot = compactHzbRoot ? 2u : 6u;

    m_commandResources.graphicsList->SetComputeRootSignature(hzbRootSignature);
    ID3D12DescriptorHeap* heaps[] = { m_services.descriptorManager->GetCBV_SRV_UAV_Heap() };
    m_commandResources.graphicsList->SetDescriptorHeaps(1, heaps);
    m_commandResources.graphicsList->SetComputeRootConstantBufferView(hzbFrameConstantsRoot, m_constantBuffers.currentFrameGPU);

    auto bindSrvTableT0 = [&](DescriptorHandle src, uint32_t dispatchIndex) -> bool {
        if (!compactHzbRoot || !m_hzbResources.dispatchTablesValid) {
            spdlog::error("BuildHZBFromDepth: compact persistent HZB descriptors are unavailable");
            return false;
        }
        if (!src.IsValid()) {
            spdlog::error("BuildHZBFromDepth: invalid SRV staging descriptor");
            return false;
        }
        const auto& table = m_hzbResources.dispatchSrvTables[m_frameRuntime.frameIndex % kFrameCount];
        if (dispatchIndex >= table.size() || !table[dispatchIndex].IsValid()) {
            spdlog::error("BuildHZBFromDepth: missing persistent SRV descriptor for dispatch {}", dispatchIndex);
            return false;
        }
        m_commandResources.graphicsList->SetComputeRootDescriptorTable(hzbSrvTableRoot, table[dispatchIndex].gpu);
        return true;
    };

    auto bindUavTableU0 = [&](DescriptorHandle src, uint32_t dispatchIndex) -> bool {
        if (!compactHzbRoot || !m_hzbResources.dispatchTablesValid) {
            spdlog::error("BuildHZBFromDepth: compact persistent HZB descriptors are unavailable");
            return false;
        }
        if (!src.IsValid()) {
            spdlog::error("BuildHZBFromDepth: invalid UAV staging descriptor");
            return false;
        }
        const auto& table = m_hzbResources.dispatchUavTables[m_frameRuntime.frameIndex % kFrameCount];
        if (dispatchIndex >= table.size() || !table[dispatchIndex].IsValid()) {
            spdlog::error("BuildHZBFromDepth: missing persistent UAV descriptor for dispatch {}", dispatchIndex);
            return false;
        }
        m_commandResources.graphicsList->SetComputeRootDescriptorTable(hzbUavTableRoot, table[dispatchIndex].gpu);
        return true;
    };

    auto dispatchForDims = [&](uint32_t w, uint32_t h) {
        const UINT groupX = (w + 7) / 8;
        const UINT groupY = (h + 7) / 8;
        m_commandResources.graphicsList->Dispatch(groupX, groupY, 1);
    };

    m_commandResources.graphicsList->SetPipelineState(m_pipelineState.hzbInit->GetPipelineState());
    if (!bindSrvTableT0(m_depthResources.srv, 0)) {
        return;
    }
    if (!bindUavTableU0(m_hzbResources.mipUAVStaging[0], 0)) {
        return;
    }
    dispatchForDims(m_hzbResources.width, m_hzbResources.height);

    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_hzbResources.texture.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = 0;
        m_commandResources.graphicsList->ResourceBarrier(1, &barrier);
    }

    uint32_t mipW = m_hzbResources.width;
    uint32_t mipH = m_hzbResources.height;

    for (uint32_t mip = 1; mip < m_hzbResources.mipCount; ++mip) {
        mipW = (mipW + 1u) / 2u;
        mipH = (mipH + 1u) / 2u;

        m_commandResources.graphicsList->SetPipelineState(m_pipelineState.hzbDownsample->GetPipelineState());
        if (!bindSrvTableT0(m_hzbResources.mipSRVStaging[mip - 1], mip)) {
            return;
        }
        if (!bindUavTableU0(m_hzbResources.mipUAVStaging[mip], mip)) {
            return;
        }

        dispatchForDims(mipW, mipH);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_hzbResources.texture.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = mip;
        m_commandResources.graphicsList->ResourceBarrier(1, &barrier);
    }

    m_hzbResources.resourceState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    m_hzbResources.valid = true;

    m_hzbResources.captureViewMatrix = m_constantBuffers.frameCPU.viewMatrix;
    m_hzbResources.captureViewProjMatrix = m_constantBuffers.frameCPU.viewProjectionMatrix;
    m_hzbResources.captureCameraPosWS = m_cameraState.positionWS;
    m_hzbResources.captureCameraForwardWS = glm::normalize(m_cameraState.forwardWS);
    m_hzbResources.captureNearPlane = m_cameraState.nearPlane;
    m_hzbResources.captureFarPlane = m_cameraState.farPlane;
    m_hzbResources.captureFrameCounter = m_frameLifecycle.renderFrameCounter;
    m_hzbResources.captureValid = true;
}

void Renderer::AddHZBFromDepthPasses_RG(RenderGraph& graph, RGResourceHandle depthHandle, RGResourceHandle hzbHandle) {
    if (!m_services.device || !m_services.descriptorManager || !m_pipelineState.computeRootSignature ||
        !m_pipelineState.hzbInit || !m_pipelineState.hzbDownsample) {
        return;
    }

    const auto& srvTable = m_hzbResources.dispatchSrvTables[m_frameRuntime.frameIndex % kFrameCount];
    const auto& uavTable = m_hzbResources.dispatchUavTables[m_frameRuntime.frameIndex % kFrameCount];
    HZBPass::AddFromDepth(
        graph,
        depthHandle,
        hzbHandle,
        {
            m_services.descriptorManager.get(),
            m_pipelineState.singleSrvUavComputeRootSignature.Get(),
            m_pipelineState.computeRootSignature.get(),
            m_pipelineState.hzbInit.get(),
            m_pipelineState.hzbDownsample.get(),
            m_constantBuffers.currentFrameGPU,
            std::span<const DescriptorHandle>(srvTable.data(), srvTable.size()),
            std::span<const DescriptorHandle>(uavTable.data(), uavTable.size()),
            m_hzbResources.width,
            m_hzbResources.height,
            m_hzbResources.mipCount,
        });
}

} // namespace Cortex::Graphics
