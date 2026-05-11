#pragma once

#include <array>
#include <cstdint>
#include <string>
#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"
#include "Utils/Result.h"

namespace Cortex::Graphics {

inline D3D12_HEAP_PROPERTIES RTDefaultHeapProperties() {
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;
    return heapProps;
}

inline D3D12_HEAP_PROPERTIES RTReadbackHeapProperties() {
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;
    return heapProps;
}

struct RTRuntimeState {
    bool supported = false;
    bool requested = false;
    bool enabled = false;
    bool reflectionsEnabled = false;
    bool giEnabled = false;
};

struct RTDenoisePassState {
    std::array<std::array<DescriptorHandle, 11>, kFrameCount> srvTables{};
    std::array<std::array<DescriptorHandle, 4>, kFrameCount> uavTables{};
    bool descriptorTablesValid = false;

    bool shadowDenoisedThisFrame = false;
    bool reflectionDenoisedThisFrame = false;
    bool giDenoisedThisFrame = false;
    bool executedThisFrame = false;
    uint32_t passCountThisFrame = 0;
    bool usedDepthNormalRejectionThisFrame = false;
    bool usedVelocityThisFrame = false;
    bool usedDisocclusionRejectionThisFrame = false;
    float shadowAlpha = 1.0f;
    float reflectionAlpha = 1.0f;
    float giAlpha = 1.0f;
    float reflectionHistoryAlpha = 0.28f;
    float reflectionCompositionStrength = 1.0f;
    float reflectionRoughnessThreshold = 0.50f;
    float reflectionHistoryMaxBlend = 0.25f;
    float reflectionFireflyClampLuma = 16.0f;
    float reflectionSignalScale = 1.0f;
    float giStrength = 0.10f;
    float giRayDistance = 5.0f;

    void ResetFrame() {
        shadowDenoisedThisFrame = false;
        reflectionDenoisedThisFrame = false;
        giDenoisedThisFrame = false;
        executedThisFrame = false;
        passCountThisFrame = 0;
        usedDepthNormalRejectionThisFrame = false;
        usedVelocityThisFrame = false;
        usedDisocclusionRejectionThisFrame = false;
        shadowAlpha = 1.0f;
        reflectionAlpha = reflectionHistoryAlpha;
        giAlpha = 1.0f;
    }
};

struct RTReflectionSignalStatsState {
    struct Metrics {
        bool valid = false;
        uint64_t sampleFrame = 0;
        uint32_t pixelCount = 0;
        float avgLuma = 0.0f;
        float maxLuma = 0.0f;
        float nonZeroRatio = 0.0f;
        float brightRatio = 0.0f;
        float outlierRatio = 0.0f;
        float avgLumaDelta = 0.0f;
        uint32_t readbackLatencyFrames = 0;

        void Reset() {
            valid = false;
            sampleFrame = 0;
            pixelCount = 0;
            avgLuma = 0.0f;
            maxLuma = 0.0f;
            nonZeroRatio = 0.0f;
            brightRatio = 0.0f;
            outlierRatio = 0.0f;
            avgLumaDelta = 0.0f;
            readbackLatencyFrames = 0;
        }
    };

    struct TargetResources {
        ComPtr<ID3D12Resource> statsBuffer;
        std::array<ComPtr<ID3D12Resource>, kFrameCount> readback{};
        DescriptorHandle statsUAV;
        D3D12_RESOURCE_STATES statsResourceState = D3D12_RESOURCE_STATE_COMMON;
        std::array<bool, kFrameCount> readbackPending{};
        std::array<uint64_t, kFrameCount> sampleFrame{};

        [[nodiscard]] Result<void> CreateStatsResources(ID3D12Device* device,
                                                        DescriptorHeapManager* descriptorManager,
                                                        UINT64 statsBytes,
                                                        UINT statsWords,
                                                        const char* label) {
            if (!device || !descriptorManager || statsBytes == 0 || statsWords == 0) {
                return Result<void>::Err("Renderer not initialized for RT reflection signal stats resources");
            }

            ResetResources();

            D3D12_RESOURCE_DESC statsDesc{};
            statsDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            statsDesc.Width = statsBytes;
            statsDesc.Height = 1;
            statsDesc.DepthOrArraySize = 1;
            statsDesc.MipLevels = 1;
            statsDesc.Format = DXGI_FORMAT_UNKNOWN;
            statsDesc.SampleDesc.Count = 1;
            statsDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            statsDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            const auto heapProps = RTDefaultHeapProperties();
            HRESULT hr = device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &statsDesc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS(&statsBuffer));
            if (FAILED(hr)) {
                ResetResources();
                return Result<void>::Err(std::string("Failed to create ") + label + " stats buffer");
            }

            if (!statsUAV.IsValid()) {
                auto statsUavResult = descriptorManager->AllocateStagingCBV_SRV_UAV();
                if (statsUavResult.IsErr()) {
                    ResetResources();
                    return Result<void>::Err(std::string("Failed to allocate ") + label +
                                             " stats UAV: " + statsUavResult.Error());
                }
                statsUAV = statsUavResult.Value();
            }

            D3D12_UNORDERED_ACCESS_VIEW_DESC statsUavDesc{};
            statsUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            statsUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            statsUavDesc.Buffer.NumElements = statsWords;
            statsUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
            device->CreateUnorderedAccessView(statsBuffer.Get(), nullptr, &statsUavDesc, statsUAV.cpu);

            const auto readbackHeap = RTReadbackHeapProperties();
            D3D12_RESOURCE_DESC readbackDesc = statsDesc;
            readbackDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
            for (auto& readbackBuffer : readback) {
                hr = device->CreateCommittedResource(
                    &readbackHeap,
                    D3D12_HEAP_FLAG_NONE,
                    &readbackDesc,
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    nullptr,
                    IID_PPV_ARGS(&readbackBuffer));
                if (FAILED(hr)) {
                    ResetResources();
                    return Result<void>::Err(std::string("Failed to create ") + label +
                                             " stats readback buffer");
                }
            }

            return Result<void>::Ok();
        }

        void ResetResources() {
            statsBuffer.Reset();
            for (auto& readbackBuffer : readback) {
                readbackBuffer.Reset();
            }
            statsResourceState = D3D12_RESOURCE_STATE_COMMON;
            readbackPending.fill(false);
            sampleFrame.fill(0);
        }
    };

    struct DescriptorTableBundle {
        std::array<std::array<DescriptorHandle, 11>, kFrameCount> srvTables{};
        std::array<std::array<DescriptorHandle, 4>, kFrameCount> uavTables{};
        bool valid = false;

        void ResetHandles() {
            valid = false;
            for (auto& table : srvTables) {
                for (auto& handle : table) {
                    handle = {};
                }
            }
            for (auto& table : uavTables) {
                for (auto& handle : table) {
                    handle = {};
                }
            }
        }
    };

    TargetResources rawResources;
    TargetResources historyResources;
    DescriptorTableBundle descriptors;
    bool rawCapturedThisFrame = false;
    bool historyCapturedThisFrame = false;
    Metrics raw;
    Metrics history;

    void ResetFrame() {
        rawCapturedThisFrame = false;
        historyCapturedThisFrame = false;
    }

    void ResetMetrics() {
        raw.Reset();
        history.Reset();
    }

    void ResetResources() {
        rawResources.ResetResources();
        historyResources.ResetResources();
        ResetFrame();
        ResetMetrics();
    }

    [[nodiscard]] Result<void> CreateStatsResources(ID3D12Device* device,
                                                    DescriptorHeapManager* descriptorManager,
                                                    UINT64 statsBytes,
                                                    UINT statsWords) {
        ResetResources();
        auto rawResult = rawResources.CreateStatsResources(
            device,
            descriptorManager,
            statsBytes,
            statsWords,
            "RT reflection signal");
        if (rawResult.IsErr()) {
            ResetResources();
            return rawResult;
        }

        auto historyResult = historyResources.CreateStatsResources(
            device,
            descriptorManager,
            statsBytes,
            statsWords,
            "RT reflection history signal");
        if (historyResult.IsErr()) {
            ResetResources();
            return historyResult;
        }

        return Result<void>::Ok();
    }
};

struct RTShadowTargetState {
    ComPtr<ID3D12Resource> mask;
    DescriptorHandle maskSRV;
    DescriptorHandle maskUAV;
    D3D12_RESOURCE_STATES maskState = D3D12_RESOURCE_STATE_COMMON;

    ComPtr<ID3D12Resource> history;
    DescriptorHandle historySRV;
    DescriptorHandle historyUAV;
    D3D12_RESOURCE_STATES historyState = D3D12_RESOURCE_STATE_COMMON;

    void ResetResources() {
        mask.Reset();
        history.Reset();
        maskState = D3D12_RESOURCE_STATE_COMMON;
        historyState = D3D12_RESOURCE_STATE_COMMON;
    }

    [[nodiscard]] Result<void> CreateResources(ID3D12Device* device,
                                               DescriptorHeapManager* descriptorManager,
                                               UINT width,
                                               UINT height) {
        if (!device || !descriptorManager || width == 0 || height == 0) {
            return Result<void>::Err("Renderer not initialized for RT shadow mask creation");
        }

        ResetResources();

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        auto createTarget = [&](ComPtr<ID3D12Resource>& target,
                                DescriptorHandle& srv,
                                DescriptorHandle& uav,
                                D3D12_RESOURCE_STATES& state,
                                D3D12_RESOURCE_STATES initialState,
                                const char* label) -> Result<void> {
            const auto heapProps = RTDefaultHeapProperties();
            const HRESULT hr = device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                initialState,
                nullptr,
                IID_PPV_ARGS(&target));
            if (FAILED(hr)) {
                target.Reset();
                return Result<void>::Err(std::string("Failed to create ") + label + " texture");
            }
            state = initialState;

            if (!srv.IsValid()) {
                auto srvResult = descriptorManager->AllocateStagingCBV_SRV_UAV();
                if (srvResult.IsErr()) {
                    target.Reset();
                    return Result<void>::Err(std::string("Failed to allocate staging SRV for ") +
                                             label + ": " + srvResult.Error());
                }
                srv = srvResult.Value();
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = desc.Format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(target.Get(), &srvDesc, srv.cpu);

            if (!uav.IsValid()) {
                auto uavResult = descriptorManager->AllocateStagingCBV_SRV_UAV();
                if (uavResult.IsErr()) {
                    target.Reset();
                    return Result<void>::Err(std::string("Failed to allocate staging UAV for ") +
                                             label + ": " + uavResult.Error());
                }
                uav = uavResult.Value();
            }

            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = desc.Format;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            device->CreateUnorderedAccessView(target.Get(), nullptr, &uavDesc, uav.cpu);
            return Result<void>::Ok();
        };

        auto maskResult = createTarget(
            mask,
            maskSRV,
            maskUAV,
            maskState,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            "RT shadow mask");
        if (maskResult.IsErr()) {
            ResetResources();
            return maskResult;
        }

        auto historyResult = createTarget(
            history,
            historySRV,
            historyUAV,
            historyState,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            "RT shadow mask history");
        if (historyResult.IsErr()) {
            ResetResources();
            return historyResult;
        }

        return Result<void>::Ok();
    }
};

struct RTReflectionTargetState {
    ComPtr<ID3D12Resource> color;
    DescriptorHandle srv;
    DescriptorHandle uav;
    std::array<DescriptorHandle, kFrameCount> dispatchClearUAVs{};
    std::array<DescriptorHandle, kFrameCount> postClearUAVs{};
    D3D12_RESOURCE_STATES colorState = D3D12_RESOURCE_STATE_COMMON;

    ComPtr<ID3D12Resource> history;
    DescriptorHandle historySRV;
    DescriptorHandle historyUAV;
    D3D12_RESOURCE_STATES historyState = D3D12_RESOURCE_STATE_COMMON;

    void ResetResources() {
        color.Reset();
        history.Reset();
        colorState = D3D12_RESOURCE_STATE_COMMON;
        historyState = D3D12_RESOURCE_STATE_COMMON;
    }

    [[nodiscard]] Result<void> CreateResources(ID3D12Device* device,
                                               DescriptorHeapManager* descriptorManager,
                                               UINT width,
                                               UINT height) {
        if (!device || !descriptorManager || width == 0 || height == 0) {
            return Result<void>::Err("Renderer not initialized for RT reflection creation");
        }

        ResetResources();

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        auto createTarget = [&](ComPtr<ID3D12Resource>& target,
                                DescriptorHandle& targetSrv,
                                DescriptorHandle& targetUav,
                                D3D12_RESOURCE_STATES& targetState,
                                D3D12_RESOURCE_STATES initialState,
                                const char* label) -> Result<void> {
            const auto heapProps = RTDefaultHeapProperties();
            const HRESULT hr = device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                initialState,
                nullptr,
                IID_PPV_ARGS(&target));
            if (FAILED(hr)) {
                target.Reset();
                return Result<void>::Err(std::string("Failed to create ") + label + " buffer");
            }
            targetState = initialState;

            if (!targetSrv.IsValid()) {
                auto srvResult = descriptorManager->AllocateStagingCBV_SRV_UAV();
                if (srvResult.IsErr()) {
                    target.Reset();
                    return Result<void>::Err(std::string("Failed to allocate staging SRV for ") +
                                             label + ": " + srvResult.Error());
                }
                targetSrv = srvResult.Value();
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = desc.Format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(target.Get(), &srvDesc, targetSrv.cpu);

            if (!targetUav.IsValid()) {
                auto uavResult = descriptorManager->AllocateStagingCBV_SRV_UAV();
                if (uavResult.IsErr()) {
                    target.Reset();
                    return Result<void>::Err(std::string("Failed to allocate staging UAV for ") +
                                             label + ": " + uavResult.Error());
                }
                targetUav = uavResult.Value();
            }

            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = desc.Format;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            device->CreateUnorderedAccessView(target.Get(), nullptr, &uavDesc, targetUav.cpu);
            return Result<void>::Ok();
        };

        auto colorResult = createTarget(
            color,
            srv,
            uav,
            colorState,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            "RT reflection color");
        if (colorResult.IsErr()) {
            ResetResources();
            return colorResult;
        }

        auto historyResult = createTarget(
            history,
            historySRV,
            historyUAV,
            historyState,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            "RT reflection history");
        if (historyResult.IsErr()) {
            ResetResources();
            return historyResult;
        }

        return Result<void>::Ok();
    }
};

struct RTGITargetState {
    ComPtr<ID3D12Resource> color;
    DescriptorHandle srv;
    DescriptorHandle uav;
    D3D12_RESOURCE_STATES colorState = D3D12_RESOURCE_STATE_COMMON;

    ComPtr<ID3D12Resource> history;
    DescriptorHandle historySRV;
    DescriptorHandle historyUAV;
    D3D12_RESOURCE_STATES historyState = D3D12_RESOURCE_STATE_COMMON;

    void ResetResources() {
        color.Reset();
        history.Reset();
        colorState = D3D12_RESOURCE_STATE_COMMON;
        historyState = D3D12_RESOURCE_STATE_COMMON;
    }

    [[nodiscard]] Result<void> CreateResources(ID3D12Device* device,
                                               DescriptorHeapManager* descriptorManager,
                                               UINT width,
                                               UINT height) {
        if (!device || !descriptorManager || width == 0 || height == 0) {
            return Result<void>::Err("Renderer not initialized for RT GI creation");
        }

        ResetResources();

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        auto createTarget = [&](ComPtr<ID3D12Resource>& target,
                                DescriptorHandle& targetSrv,
                                DescriptorHandle& targetUav,
                                D3D12_RESOURCE_STATES& targetState,
                                D3D12_RESOURCE_STATES initialState,
                                const char* label) -> Result<void> {
            const auto heapProps = RTDefaultHeapProperties();
            const HRESULT hr = device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                initialState,
                nullptr,
                IID_PPV_ARGS(&target));
            if (FAILED(hr)) {
                target.Reset();
                return Result<void>::Err(std::string("Failed to create ") + label + " buffer");
            }
            targetState = initialState;

            if (!targetSrv.IsValid()) {
                auto srvResult = descriptorManager->AllocateStagingCBV_SRV_UAV();
                if (srvResult.IsErr()) {
                    target.Reset();
                    return Result<void>::Err(std::string("Failed to allocate staging SRV for ") +
                                             label + ": " + srvResult.Error());
                }
                targetSrv = srvResult.Value();
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = desc.Format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(target.Get(), &srvDesc, targetSrv.cpu);

            if (!targetUav.IsValid()) {
                auto uavResult = descriptorManager->AllocateStagingCBV_SRV_UAV();
                if (uavResult.IsErr()) {
                    target.Reset();
                    return Result<void>::Err(std::string("Failed to allocate staging UAV for ") +
                                             label + ": " + uavResult.Error());
                }
                targetUav = uavResult.Value();
            }

            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = desc.Format;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            device->CreateUnorderedAccessView(target.Get(), nullptr, &uavDesc, targetUav.cpu);
            return Result<void>::Ok();
        };

        auto colorResult = createTarget(
            color,
            srv,
            uav,
            colorState,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            "RT GI");
        if (colorResult.IsErr()) {
            ResetResources();
            return colorResult;
        }

        auto historyResult = createTarget(
            history,
            historySRV,
            historyUAV,
            historyState,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            "RT GI history");
        if (historyResult.IsErr()) {
            ResetResources();
            return historyResult;
        }

        return Result<void>::Ok();
    }
};

struct RTReflectionReadinessState {
    bool ready = false;
    bool hasPipeline = false;
    bool hasTLAS = false;
    bool hasMaterialBuffer = false;
    bool hasOutput = false;
    bool hasDepth = false;
    bool hasNormalRoughness = false;
    bool hasMaterialExt2 = false;
    bool hasEnvironmentTable = false;
    bool hasFrameConstants = false;
    bool hasDispatchDescriptors = false;
    uint32_t dispatchWidth = 0;
    uint32_t dispatchHeight = 0;
    std::string reason;

    void ResetFrame() {
        ready = false;
        hasPipeline = false;
        hasTLAS = false;
        hasMaterialBuffer = false;
        hasOutput = false;
        hasDepth = false;
        hasNormalRoughness = false;
        hasMaterialExt2 = false;
        hasEnvironmentTable = false;
        hasFrameConstants = false;
        hasDispatchDescriptors = false;
        dispatchWidth = 0;
        dispatchHeight = 0;
        reason.clear();
    }
};

} // namespace Cortex::Graphics
