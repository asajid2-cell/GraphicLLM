#pragma once

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"

namespace Cortex::Graphics {

struct TemporalAAState {
    bool enabled = true; // Re-enabled now that memory issues are fixed
    float blendFactor = 0.06f;
    bool cameraIsMoving = false;
    glm::vec2 jitterPrevPixels{0.0f, 0.0f};
    glm::vec2 jitterCurrPixels{0.0f, 0.0f};
    uint32_t sampleIndex = 0;
    glm::mat4 prevViewProjMatrix{1.0f};
    bool hasPrevViewProj = false;

    void ResetAccumulation() {
        sampleIndex = 0;
        jitterPrevPixels = glm::vec2(0.0f);
        jitterCurrPixels = glm::vec2(0.0f);
        hasPrevViewProj = false;
    }
};

struct TemporalScreenPassState {
    ComPtr<ID3D12Resource> velocityBuffer;
    DescriptorHandle velocityRTV;
    DescriptorHandle velocitySRV;
    D3D12_RESOURCE_STATES velocityState = D3D12_RESOURCE_STATE_COMMON;

    ComPtr<ID3D12Resource> historyColor;
    DescriptorHandle historySRV;
    D3D12_RESOURCE_STATES historyState = D3D12_RESOURCE_STATE_COMMON;

    ComPtr<ID3D12Resource> taaIntermediate;
    DescriptorHandle taaIntermediateRTV;
    D3D12_RESOURCE_STATES taaIntermediateState = D3D12_RESOURCE_STATE_COMMON;

    std::array<std::array<DescriptorHandle, 13>, kFrameCount> taaResolveSrvTables{};
    bool taaResolveSrvTableValid = false;
    std::array<std::array<DescriptorHandle, 10>, kFrameCount> motionVectorSrvTables{};
    bool motionVectorSrvTableValid = false;
    std::array<std::array<DescriptorHandle, 13>, kFrameCount> postProcessSrvTables{};
    bool postProcessSrvTableValid = false;

    void ResetResources() {
        velocityBuffer.Reset();
        velocityState = D3D12_RESOURCE_STATE_COMMON;
        historyColor.Reset();
        historyState = D3D12_RESOURCE_STATE_COMMON;
        taaIntermediate.Reset();
        taaIntermediateState = D3D12_RESOURCE_STATE_COMMON;
        taaResolveSrvTableValid = false;
        motionVectorSrvTableValid = false;
        postProcessSrvTableValid = false;
    }
};

} // namespace Cortex::Graphics
