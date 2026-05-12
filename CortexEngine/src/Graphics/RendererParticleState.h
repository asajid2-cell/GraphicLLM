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

struct ParticleGpuSource {
    glm::vec4 positionSize;
    glm::vec4 velocityAge;
    glm::vec4 color;
    glm::vec4 params;
};

struct ParticleGpuEmitter {
    glm::vec4 positionRate;
    glm::vec4 initialVelocityLifetime;
    glm::vec4 velocityRandomGravity;
    glm::vec4 sizeLocalType;
    glm::vec4 colorStart;
    glm::vec4 colorEnd;
    glm::vec4 offsetCountSeed;
};

struct alignas(16) ParticleGpuPrepareConstants {
    uint32_t count = 0;
    float deltaTime = 0.0f;
    float bloomContribution = 1.0f;
    float softDepthFade = 0.5f;
    float windInfluence = 0.0f;
    glm::vec3 padding0{0.0f};
    glm::vec4 cameraPosition{0.0f};
};

struct alignas(16) ParticleGpuLifecycleConstants {
    uint32_t emitterCount = 0;
    uint32_t particleCount = 0;
    float time = 0.0f;
    float bloomContribution = 1.0f;
    float softDepthFade = 0.5f;
    float windInfluence = 0.0f;
    glm::vec2 padding0{0.0f};
    glm::vec4 cameraPosition{0.0f};
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
    ComPtr<ID3D12Resource> gpuEmitterBuffer;
    UINT gpuEmitterCapacity = 0;
    ComPtr<ID3D12Resource> gpuSourceBuffer;
    UINT gpuSourceCapacity = 0;
    ComPtr<ID3D12Resource> gpuInstanceBuffer;
    D3D12_RESOURCE_STATES gpuInstanceState = D3D12_RESOURCE_STATE_COMMON;
    ConstantBuffer<ParticleGpuPrepareConstants> gpuPrepareConstants;
    ConstantBuffer<ParticleGpuLifecycleConstants> gpuLifecycleConstants;
    bool gpuPrepareConstantsInitialized = false;
    bool gpuLifecycleConstantsInitialized = false;
    bool gpuPreparedThisFrame = false;
    ComPtr<ID3D12Resource> quadVertexBuffer;

    [[nodiscard]] bool NeedsInstanceCapacity(UINT requiredCapacity) const {
        return !instanceBuffer || !gpuSourceBuffer || !gpuInstanceBuffer || instanceCapacity < requiredCapacity || gpuSourceCapacity < requiredCapacity;
    }

    [[nodiscard]] HRESULT EnsureInstanceBuffer(ID3D12Device* device,
                                               UINT requiredCapacity,
                                               UINT minCapacity) {
        if (!device || requiredCapacity == 0) {
            return E_INVALIDARG;
        }
        if (!NeedsInstanceCapacity(requiredCapacity)) {
            if (!gpuPrepareConstantsInitialized) {
                auto cbResult = gpuPrepareConstants.Initialize(device, 16);
                if (cbResult.IsErr()) {
                    return E_FAIL;
                }
                gpuPrepareConstantsInitialized = true;
            }
            if (!gpuLifecycleConstantsInitialized) {
                auto cbResult = gpuLifecycleConstants.Initialize(device, 16);
                if (cbResult.IsErr()) {
                    return E_FAIL;
                }
                gpuLifecycleConstantsInitialized = true;
            }
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

        ComPtr<ID3D12Resource> uploadInstances;
        HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&uploadInstances));
        if (FAILED(hr)) {
            return hr;
        }

        desc.Width = static_cast<UINT64>(newCapacity) * sizeof(ParticleGpuSource);
        ComPtr<ID3D12Resource> sourceBuffer;
        hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&sourceBuffer));
        if (FAILED(hr)) {
            return hr;
        }

        D3D12_HEAP_PROPERTIES defaultHeapProps = {};
        defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        defaultHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        defaultHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        defaultHeapProps.CreationNodeMask = 1;
        defaultHeapProps.VisibleNodeMask = 1;

        desc.Width = static_cast<UINT64>(newCapacity) * sizeof(ParticleInstance);
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        ComPtr<ID3D12Resource> gpuInstances;
        hr = device->CreateCommittedResource(
            &defaultHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&gpuInstances));
        if (FAILED(hr)) {
            return hr;
        }

        if (!gpuPrepareConstantsInitialized) {
            auto cbResult = gpuPrepareConstants.Initialize(device, 16);
            if (cbResult.IsErr()) {
                return E_FAIL;
            }
            gpuPrepareConstantsInitialized = true;
        }
        if (!gpuLifecycleConstantsInitialized) {
            auto cbResult = gpuLifecycleConstants.Initialize(device, 16);
            if (cbResult.IsErr()) {
                return E_FAIL;
            }
            gpuLifecycleConstantsInitialized = true;
        }

        instanceBuffer = uploadInstances;
        gpuSourceBuffer = sourceBuffer;
        gpuInstanceBuffer = gpuInstances;
        gpuInstanceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        instanceCapacity = newCapacity;
        gpuSourceCapacity = newCapacity;
        gpuPreparedThisFrame = false;
        return S_OK;
    }

    [[nodiscard]] HRESULT EnsureGpuEmitterBuffer(ID3D12Device* device,
                                                 UINT requiredCapacity,
                                                 UINT minCapacity) {
        if (!device || requiredCapacity == 0) {
            return E_INVALIDARG;
        }
        if (gpuEmitterBuffer && gpuEmitterCapacity >= requiredCapacity) {
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
        desc.Width = static_cast<UINT64>(newCapacity) * sizeof(ParticleGpuEmitter);
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ComPtr<ID3D12Resource> emitterBuffer;
        const HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&emitterBuffer));
        if (FAILED(hr)) {
            return hr;
        }

        gpuEmitterBuffer = emitterBuffer;
        gpuEmitterCapacity = newCapacity;
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

    [[nodiscard]] HRESULT UploadGpuSources(const ParticleGpuSource* sources,
                                           UINT sourceCount,
                                           UINT& bytesWritten) {
        bytesWritten = 0;
        if (!gpuSourceBuffer || !sources || sourceCount == 0) {
            return E_INVALIDARG;
        }

        void* mapped = nullptr;
        D3D12_RANGE readRange{0, 0};
        bytesWritten = sourceCount * sizeof(ParticleGpuSource);
        const HRESULT hr = gpuSourceBuffer->Map(0, &readRange, &mapped);
        if (FAILED(hr)) {
            bytesWritten = 0;
            return hr;
        }
        std::memcpy(mapped, sources, bytesWritten);
        gpuSourceBuffer->Unmap(0, nullptr);
        return S_OK;
    }

    [[nodiscard]] HRESULT UploadGpuEmitters(const ParticleGpuEmitter* emitters,
                                            UINT emitterCount,
                                            UINT& bytesWritten) {
        bytesWritten = 0;
        if (!gpuEmitterBuffer || !emitters || emitterCount == 0) {
            return E_INVALIDARG;
        }

        void* mapped = nullptr;
        D3D12_RANGE readRange{0, 0};
        bytesWritten = emitterCount * sizeof(ParticleGpuEmitter);
        const HRESULT hr = gpuEmitterBuffer->Map(0, &readRange, &mapped);
        if (FAILED(hr)) {
            bytesWritten = 0;
            return hr;
        }
        std::memcpy(mapped, emitters, bytesWritten);
        gpuEmitterBuffer->Unmap(0, nullptr);
        return S_OK;
    }

    [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS WriteGpuPrepareConstants(const ParticleGpuPrepareConstants& constants) {
        if (!gpuPrepareConstantsInitialized) {
            return 0;
        }
        return gpuPrepareConstants.AllocateAndWrite(constants);
    }

    [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS WriteGpuLifecycleConstants(const ParticleGpuLifecycleConstants& constants) {
        if (!gpuLifecycleConstantsInitialized) {
            return 0;
        }
        return gpuLifecycleConstants.AllocateAndWrite(constants);
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
        ID3D12Resource* buffer = gpuPreparedThisFrame ? gpuInstanceBuffer.Get() : instanceBuffer.Get();
        if (!buffer || instanceCount == 0 || instanceBytes == 0) {
            return view;
        }
        view.BufferLocation = buffer->GetGPUVirtualAddress();
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
        return static_cast<uint64_t>(instanceCapacity) * sizeof(ParticleInstance) +
               static_cast<uint64_t>(gpuEmitterCapacity) * sizeof(ParticleGpuEmitter) +
               static_cast<uint64_t>(gpuSourceCapacity) * sizeof(ParticleGpuSource);
    }

    void Reset() {
        instanceBuffer.Reset();
        instanceCapacity = 0;
        gpuEmitterBuffer.Reset();
        gpuEmitterCapacity = 0;
        gpuSourceBuffer.Reset();
        gpuSourceCapacity = 0;
        gpuInstanceBuffer.Reset();
        gpuInstanceState = D3D12_RESOURCE_STATE_COMMON;
        if (gpuPrepareConstants.buffer && gpuPrepareConstants.mappedBytes) {
            gpuPrepareConstants.buffer->Unmap(0, nullptr);
            gpuPrepareConstants.mappedBytes = nullptr;
        }
        gpuPrepareConstants.buffer.Reset();
        gpuPrepareConstants.gpuAddress = 0;
        gpuPrepareConstants.bufferSize = 0;
        gpuPrepareConstants.alignedSize = 0;
        gpuPrepareConstants.offset = 0;
        gpuPrepareConstants.perFrameSize = 0;
        gpuPrepareConstants.frameRegionStart = 0;
        gpuPrepareConstants.frameRegionEnd = 0;
        gpuPrepareConstantsInitialized = false;
        if (gpuLifecycleConstants.buffer && gpuLifecycleConstants.mappedBytes) {
            gpuLifecycleConstants.buffer->Unmap(0, nullptr);
            gpuLifecycleConstants.mappedBytes = nullptr;
        }
        gpuLifecycleConstants.buffer.Reset();
        gpuLifecycleConstants.gpuAddress = 0;
        gpuLifecycleConstants.bufferSize = 0;
        gpuLifecycleConstants.alignedSize = 0;
        gpuLifecycleConstants.offset = 0;
        gpuLifecycleConstants.perFrameSize = 0;
        gpuLifecycleConstants.frameRegionStart = 0;
        gpuLifecycleConstants.frameRegionEnd = 0;
        gpuLifecycleConstantsInitialized = false;
        gpuPreparedThisFrame = false;
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
    bool frameGpuPrepared = false;
    bool frameGpuLifecycleDispatched = false;
    bool frameGpuSimulationDispatched = false;
    bool frameGpuSortDispatched = false;
    uint32_t frameGpuDispatchGroups = 0;
    uint64_t frameUploadBytes = 0;

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
        frameGpuPrepared = false;
        frameGpuLifecycleDispatched = false;
        frameGpuSimulationDispatched = false;
        frameGpuSortDispatched = false;
        frameGpuDispatchGroups = 0;
        frameUploadBytes = 0;
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
