#include "Graphics/Passes/DepthPrepassTargetPass.h"

#include <cstdio>
#include <spdlog/spdlog.h>
#include <string>

namespace Cortex::Graphics::DepthPrepassTargetPass {

Result<void> CreateResources(const ResourceCreateContext& context) {
    if (!context.device || !context.descriptorManager || !context.depthState ||
        context.width == 0 || context.height == 0) {
        return Result<void>::Err("CreateDepthBuffer: renderer not initialized or invalid depth dimensions");
    }

    DepthTargetState& depthState = *context.depthState;

    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = context.width;
    depthDesc.Height = context.height;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = context.device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearValue,
        IID_PPV_ARGS(&depthState.resources.buffer));

    if (FAILED(hr)) {
        depthState.resources.buffer.Reset();
        depthState.descriptors.dsv = {};
        depthState.descriptors.readOnlyDsv = {};
        depthState.descriptors.srv = {};

        if (context.reportDeviceRemoved) {
            context.reportDeviceRemoved(hr);
        }

        char buf[64];
        sprintf_s(buf, "0x%08X", static_cast<unsigned int>(hr));
        char dim[64];
        sprintf_s(dim, "%llux%u", static_cast<unsigned long long>(depthDesc.Width), depthDesc.Height);
        return Result<void>::Err(std::string("Failed to create depth buffer (")
                                 + dim + ", scale=" + std::to_string(context.renderScale)
                                 + ", hr=" + buf + ")");
    }

    depthState.resources.resourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

    if (!depthState.descriptors.dsv.IsValid()) {
        auto dsvResult = context.descriptorManager->AllocateDSV();
        if (dsvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate DSV: " + dsvResult.Error());
        }
        depthState.descriptors.dsv = dsvResult.Value();
    }

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

    context.device->CreateDepthStencilView(
        depthState.resources.buffer.Get(),
        &dsvDesc,
        depthState.descriptors.dsv.cpu);

    if (!depthState.descriptors.readOnlyDsv.IsValid()) {
        auto roDsvResult = context.descriptorManager->AllocateDSV();
        if (roDsvResult.IsErr()) {
            spdlog::warn("Failed to allocate read-only DSV (continuing without): {}", roDsvResult.Error());
        } else {
            depthState.descriptors.readOnlyDsv = roDsvResult.Value();
        }
    }
    if (depthState.descriptors.readOnlyDsv.IsValid()) {
        D3D12_DEPTH_STENCIL_VIEW_DESC roDesc = dsvDesc;
        roDesc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
        context.device->CreateDepthStencilView(
            depthState.resources.buffer.Get(),
            &roDesc,
            depthState.descriptors.readOnlyDsv.cpu);
    }

    if (!depthState.descriptors.srv.IsValid()) {
        auto srvResult = context.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (srvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate staging SRV for depth buffer: " + srvResult.Error());
        }
        depthState.descriptors.srv = srvResult.Value();
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
    depthSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrvDesc.Texture2D.MipLevels = 1;

    context.device->CreateShaderResourceView(
        depthState.resources.buffer.Get(),
        &depthSrvDesc,
        depthState.descriptors.srv.cpu);

    spdlog::info("Depth buffer created");
    return Result<void>::Ok();
}

bool BindAndClear(const BindContext& context) {
    if (!context.commandList ||
        !context.depthBuffer ||
        !context.depthState ||
        !context.depthDsv.IsValid()) {
        return false;
    }

    if (!context.skipTransitions && *context.depthState != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        D3D12_RESOURCE_BARRIER depthBarrier{};
        depthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        depthBarrier.Transition.pResource = context.depthBuffer;
        depthBarrier.Transition.StateBefore = *context.depthState;
        depthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        depthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        context.commandList->ResourceBarrier(1, &depthBarrier);
        *context.depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE dsv = context.depthDsv.cpu;
    context.commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);

    if (context.clearDepth) {
        context.commandList->ClearDepthStencilView(
            dsv,
            D3D12_CLEAR_FLAG_DEPTH,
            1.0f,
            0,
            0,
            nullptr);
    }

    const D3D12_RESOURCE_DESC depthDesc = context.depthBuffer->GetDesc();
    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(depthDesc.Width);
    viewport.Height = static_cast<float>(depthDesc.Height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissorRect{};
    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = static_cast<LONG>(depthDesc.Width);
    scissorRect.bottom = static_cast<LONG>(depthDesc.Height);

    context.commandList->RSSetViewports(1, &viewport);
    context.commandList->RSSetScissorRects(1, &scissorRect);
    return true;
}

} // namespace Cortex::Graphics::DepthPrepassTargetPass
