#include "GPUCullingInternal.h"
#include "RHI/DX12Device.h"
#include "RHI/DescriptorHeap.h"

namespace Cortex::Graphics {
Result<void> GPUCullingPipeline::CreateBuffers() {
    ID3D12Device* device = m_device->GetDevice();
    HRESULT hr = S_OK;

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

    // Instance buffers (triple-buffered: default heap + upload staging)
    for (uint32_t i = 0; i < kGPUCullingFrameCount; ++i) {
        hr = device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_instanceBuffer[i])
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create instance buffer");
        }
        std::wstring instName = L"GPUCulling_InstanceBuffer_" + std::to_wstring(i);
        m_instanceBuffer[i]->SetName(instName.c_str());
        m_instanceState[i] = D3D12_RESOURCE_STATE_COPY_DEST;

        hr = device->CreateCommittedResource(
            &uploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_instanceUploadBuffer[i])
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create instance upload buffer");
        }
        std::wstring uploadName = L"GPUCulling_InstanceUpload_" + std::to_wstring(i);
        m_instanceUploadBuffer[i]->SetName(uploadName.c_str());
    }

    // All-commands buffers (triple-buffered: default heap + upload staging)
    bufferDesc.Width = commandBufferSize;
    for (uint32_t i = 0; i < kGPUCullingFrameCount; ++i) {
        hr = device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_allCommandBuffer[i])
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create all-commands buffer");
        }
        std::wstring cmdName = L"GPUCulling_AllCommandBuffer_" + std::to_wstring(i);
        m_allCommandBuffer[i]->SetName(cmdName.c_str());
        m_allCommandState[i] = D3D12_RESOURCE_STATE_COPY_DEST;

        hr = device->CreateCommittedResource(
            &uploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_allCommandUploadBuffer[i])
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create all-commands upload buffer");
        }
        std::wstring cmdUploadName = L"GPUCulling_AllCommandUpload_" + std::to_wstring(i);
        m_allCommandUploadBuffer[i]->SetName(cmdUploadName.c_str());
    }

    // Visible command buffer (default heap, UAV) - triple-buffered
    bufferDesc.Width = commandBufferSize;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    for (uint32_t i = 0; i < kGPUCullingFrameCount; ++i) {
        hr = device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&m_visibleCommandBuffer[i])
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create visible command buffer " + std::to_string(i));
        }
        std::wstring visibleName = L"GPUCulling_VisibleCommand_" + std::to_wstring(i);
        m_visibleCommandBuffer[i]->SetName(visibleName.c_str());
        m_visibleCommandState[i] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    // Command count buffer (4 bytes for atomic counter) - triple-buffered
    bufferDesc.Width = sizeof(uint32_t);
    for (uint32_t i = 0; i < kGPUCullingFrameCount; ++i) {
        hr = device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&m_commandCountBuffer[i])
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create command count buffer " + std::to_string(i));
        }
        std::wstring countName = L"GPUCulling_CommandCount_" + std::to_wstring(i);
        m_commandCountBuffer[i]->SetName(countName.c_str());
        m_commandCountState[i] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
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
    // Triple-buffered to prevent race conditions between GPU culling write and visibility pass read.
    {
        D3D12_RESOURCE_DESC maskDesc = bufferDesc;
        maskDesc.Width = static_cast<UINT64>(m_maxInstances) * sizeof(uint32_t);
        maskDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        for (uint32_t i = 0; i < kGPUCullingFrameCount; ++i) {
            hr = device->CreateCommittedResource(
                &defaultHeap,
                D3D12_HEAP_FLAG_NONE,
                &maskDesc,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                nullptr,
                IID_PPV_ARGS(&m_visibilityMaskBuffer[i])
            );
            if (FAILED(hr)) {
                return Result<void>::Err("Failed to create visibility mask buffer " + std::to_string(i));
            }
            std::wstring maskName = L"GPUCullingVisibilityMask_" + std::to_wstring(i);
            m_visibilityMaskBuffer[i]->SetName(maskName.c_str());
            m_visibilityMaskState[i] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
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

    // Allocate descriptors for counter buffer UAV (needed for ClearUnorderedAccessViewUint) - triple-buffered
    // Create UAV for command count buffer (raw buffer, 1 uint32)
    D3D12_UNORDERED_ACCESS_VIEW_DESC counterUAVDesc = {};
    counterUAVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    counterUAVDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    counterUAVDesc.Buffer.FirstElement = 0;
    counterUAVDesc.Buffer.NumElements = 1;
    counterUAVDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;

    for (uint32_t i = 0; i < kGPUCullingFrameCount; ++i) {
        auto counterUAVResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (counterUAVResult.IsErr()) {
            return Result<void>::Err("Failed to allocate command count UAV descriptor " + std::to_string(i));
        }
        m_counterUAV[i] = counterUAVResult.Value();

        auto counterStagingResult = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (counterStagingResult.IsErr()) {
            return Result<void>::Err("Failed to allocate command count UAV staging descriptor " + std::to_string(i));
        }
        m_counterUAVStaging[i] = counterStagingResult.Value();

        device->CreateUnorderedAccessView(m_commandCountBuffer[i].Get(), nullptr, &counterUAVDesc, m_counterUAV[i].cpu);
        device->CreateUnorderedAccessView(m_commandCountBuffer[i].Get(), nullptr, &counterUAVDesc, m_counterUAVStaging[i].cpu);
    }

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

        for (uint32_t i = 0; i < kGPUCullingFrameCount; ++i) {
            auto hzbDispatchSrvRes = m_descriptorManager->AllocateCBV_SRV_UAV();
            if (hzbDispatchSrvRes.IsErr()) {
                return Result<void>::Err("Failed to allocate HZB dispatch SRV descriptor " + std::to_string(i));
            }
            m_hzbSrvDispatch[i] = hzbDispatchSrvRes.Value();
        }

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
        for (uint32_t i = 0; i < kGPUCullingFrameCount; ++i) {
            device->CreateShaderResourceView(m_dummyHzbTexture.Get(), &hzbSrvDesc, m_hzbSrvDispatch[i].cpu);
        }
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

    // Note: m_instanceState and m_allCommandState are already initialized per-frame in the buffer creation loops
    return Result<void>::Ok();
}

} // namespace Cortex::Graphics

