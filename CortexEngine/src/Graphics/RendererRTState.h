#pragma once

#include <array>
#include <cstdint>
#include <string>
#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"

namespace Cortex::Graphics {

struct RTRuntimeState {
    bool supported = false;
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
        reflectionAlpha = 1.0f;
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

    ComPtr<ID3D12Resource> rawStatsBuffer;
    ComPtr<ID3D12Resource> historyStatsBuffer;
    std::array<ComPtr<ID3D12Resource>, kFrameCount> rawReadback{};
    std::array<ComPtr<ID3D12Resource>, kFrameCount> historyReadback{};
    DescriptorHandle rawStatsUAV;
    DescriptorHandle historyStatsUAV;
    D3D12_RESOURCE_STATES rawStatsResourceState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES historyStatsResourceState = D3D12_RESOURCE_STATE_COMMON;
    std::array<bool, kFrameCount> rawReadbackPending{};
    std::array<bool, kFrameCount> historyReadbackPending{};
    std::array<uint64_t, kFrameCount> rawSampleFrame{};
    std::array<uint64_t, kFrameCount> historySampleFrame{};
    std::array<std::array<DescriptorHandle, 11>, kFrameCount> descriptorSrvTables{};
    std::array<std::array<DescriptorHandle, 4>, kFrameCount> descriptorUavTables{};
    bool descriptorTablesValid = false;
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
        rawStatsBuffer.Reset();
        historyStatsBuffer.Reset();
        for (auto& readback : rawReadback) {
            readback.Reset();
        }
        for (auto& readback : historyReadback) {
            readback.Reset();
        }
        rawStatsResourceState = D3D12_RESOURCE_STATE_COMMON;
        historyStatsResourceState = D3D12_RESOURCE_STATE_COMMON;
        rawReadbackPending.fill(false);
        historyReadbackPending.fill(false);
        rawSampleFrame.fill(0);
        historySampleFrame.fill(0);
        ResetFrame();
        ResetMetrics();
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
