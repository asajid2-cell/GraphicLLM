#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

#include <glm/glm.hpp>
#include "Graphics/Renderer_ConstantBuffer.h"

namespace Cortex::Graphics {

struct ParticleInstance {
    glm::vec3 position;
    float size;
    glm::vec4 color;
};

struct ParticleRenderControls {
    bool enabledForScene = true;
    float densityScale = 1.0f;
    float qualityScale = 1.0f;
    float bloomContribution = 1.0f;
    float softDepthFade = 0.5f;
    float windInfluence = 0.0f;
    std::string effectPreset = "gallery_mix";

    void SetDensityScale(float scale) {
        densityScale = std::clamp(scale, 0.0f, 2.0f);
    }

    void SetTuning(float quality, float bloom, float softDepth, float wind) {
        qualityScale = std::clamp(quality, 0.25f, 2.0f);
        bloomContribution = std::clamp(bloom, 0.0f, 2.0f);
        softDepthFade = std::clamp(softDepth, 0.0f, 1.0f);
        windInfluence = std::clamp(wind, 0.0f, 2.0f);
    }

    void SetEffectPreset(const std::string& presetId) {
        effectPreset = presetId.empty() ? "gallery_mix" : presetId;
    }
};

struct ParticleRenderResources {
    ComPtr<ID3D12Resource> instanceBuffer;
    UINT instanceCapacity = 0;
    ComPtr<ID3D12Resource> quadVertexBuffer;

    [[nodiscard]] bool NeedsInstanceCapacity(UINT requiredCapacity) const {
        return !instanceBuffer || instanceCapacity < requiredCapacity;
    }

    [[nodiscard]] HRESULT EnsureInstanceBuffer(ID3D12Device* device,
                                               UINT requiredCapacity,
                                               UINT minCapacity) {
        if (!device || requiredCapacity == 0) {
            return E_INVALIDARG;
        }
        if (!NeedsInstanceCapacity(requiredCapacity)) {
            return S_OK;
        }

        const UINT newCapacity = std::max(requiredCapacity, minCapacity);

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = static_cast<UINT64>(newCapacity) * sizeof(ParticleInstance);
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ComPtr<ID3D12Resource> buffer;
        const HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&buffer));
        if (FAILED(hr)) {
            return hr;
        }

        instanceBuffer = buffer;
        instanceCapacity = newCapacity;
        return S_OK;
    }

    [[nodiscard]] HRESULT UploadInstances(const ParticleInstance* instances,
                                          UINT instanceCount,
                                          UINT& bytesWritten) {
        bytesWritten = 0;
        if (!instanceBuffer || !instances || instanceCount == 0) {
            return E_INVALIDARG;
        }

        void* mapped = nullptr;
        D3D12_RANGE readRange{0, 0};
        bytesWritten = instanceCount * sizeof(ParticleInstance);
        const HRESULT hr = instanceBuffer->Map(0, &readRange, &mapped);
        if (FAILED(hr)) {
            bytesWritten = 0;
            return hr;
        }
        std::memcpy(mapped, instances, bytesWritten);
        instanceBuffer->Unmap(0, nullptr);
        return S_OK;
    }

    [[nodiscard]] HRESULT EnsureQuadVertexBuffer(ID3D12Device* device,
                                                 const void* vertices,
                                                 UINT vertexBytes) {
        if (!device || !vertices || vertexBytes == 0) {
            return E_INVALIDARG;
        }
        if (quadVertexBuffer) {
            return S_OK;
        }

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = vertexBytes;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ComPtr<ID3D12Resource> buffer;
        const HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&buffer));
        if (FAILED(hr)) {
            return hr;
        }

        void* mapped = nullptr;
        D3D12_RANGE readRange{0, 0};
        const HRESULT mapHr = buffer->Map(0, &readRange, &mapped);
        if (FAILED(mapHr)) {
            return mapHr;
        }
        std::memcpy(mapped, vertices, vertexBytes);
        buffer->Unmap(0, nullptr);

        quadVertexBuffer = buffer;
        return S_OK;
    }

    [[nodiscard]] D3D12_VERTEX_BUFFER_VIEW InstanceBufferView(UINT instanceCount,
                                                             UINT instanceBytes) const {
        D3D12_VERTEX_BUFFER_VIEW view{};
        if (!instanceBuffer || instanceCount == 0 || instanceBytes == 0) {
            return view;
        }
        view.BufferLocation = instanceBuffer->GetGPUVirtualAddress();
        view.StrideInBytes = sizeof(ParticleInstance);
        view.SizeInBytes = instanceBytes;
        return view;
    }

    [[nodiscard]] D3D12_VERTEX_BUFFER_VIEW QuadVertexBufferView(UINT strideBytes,
                                                               UINT vertexBytes) const {
        D3D12_VERTEX_BUFFER_VIEW view{};
        if (!quadVertexBuffer || strideBytes == 0 || vertexBytes == 0) {
            return view;
        }
        view.BufferLocation = quadVertexBuffer->GetGPUVirtualAddress();
        view.StrideInBytes = strideBytes;
        view.SizeInBytes = vertexBytes;
        return view;
    }

    [[nodiscard]] uint64_t InstanceBufferBytes() const {
        return static_cast<uint64_t>(instanceCapacity) * sizeof(ParticleInstance);
    }

    void Reset() {
        instanceBuffer.Reset();
        instanceCapacity = 0;
        quadVertexBuffer.Reset();
    }
};

struct ParticleFrameStats {
    uint32_t frameEmitterCount = 0;
    uint32_t frameLiveParticles = 0;
    uint32_t frameSubmittedInstances = 0;
    uint32_t frameFrustumCulled = 0;
    uint32_t frameMaxInstances = 4096;
    float frameDensityScale = 1.0f;
    float frameQualityScale = 1.0f;
    float frameBloomContribution = 1.0f;
    float frameSoftDepthFade = 0.5f;
    float frameWindInfluence = 0.0f;
    uint32_t framePresetMatchedEmitters = 0;
    uint32_t framePresetMismatchedEmitters = 0;
    bool frameCapped = false;
    bool frameExecuted = false;

    void Reset(const ParticleRenderControls& controls) {
        frameEmitterCount = 0;
        frameLiveParticles = 0;
        frameSubmittedInstances = 0;
        frameFrustumCulled = 0;
        frameMaxInstances = 4096;
        frameDensityScale = controls.densityScale;
        frameQualityScale = controls.qualityScale;
        frameBloomContribution = controls.bloomContribution;
        frameSoftDepthFade = controls.softDepthFade;
        frameWindInfluence = controls.windInfluence;
        framePresetMatchedEmitters = 0;
        framePresetMismatchedEmitters = 0;
        frameCapped = false;
        frameExecuted = false;
    }
};

struct ParticleRenderState {
    ParticleRenderControls controls;
    ParticleRenderResources resources;
    ParticleFrameStats frame;
    bool instanceMapFailed = false;

    void ResetFrameStats() {
        frame.Reset(controls);
    }

    void ResetResources() {
        instanceMapFailed = false;
        resources.Reset();
        ResetFrameStats();
    }
};

} // namespace Cortex::Graphics
