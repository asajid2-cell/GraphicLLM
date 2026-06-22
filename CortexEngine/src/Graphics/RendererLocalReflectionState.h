#pragma once

#include <array>
#include <memory>
#include <string>

#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"
#include "RHI/DX12Texture.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace Cortex::Graphics {

struct LocalReflectionProbeCubemapCaptureState {
    static constexpr uint32_t kFaceCount = 6;
    static constexpr uint32_t kDefaultFaceSize = 128;

    std::shared_ptr<DX12Texture> target;
    std::array<DescriptorHandle, kFaceCount> faceRTVs{};
    std::array<glm::mat4, kFaceCount> viewMatrices{};
    glm::mat4 projectionMatrix{1.0f};
    glm::vec3 captureCenter{0.0f};
    uint32_t faceSize = kDefaultFaceSize;
    uint32_t scheduledProbes = 0;
    uint32_t capturedFaces = 0;
    bool allocated = false;
    bool scheduledThisFrame = false;
    bool executedThisFrame = false;
    bool failedThisFrame = false;
    bool captureCompleted = false;
    std::string captureMode = "none";
    std::string failureReason;

    void ResetPerFrame() {
        scheduledThisFrame = false;
        executedThisFrame = false;
        failedThisFrame = false;
        failureReason.clear();
        scheduledProbes = 0;
        capturedFaces = 0;
    }

    void ResetResources() {
        target.reset();
        for (auto& rtv : faceRTVs) {
            rtv = {};
        }
        allocated = false;
        captureCompleted = false;
        captureMode = "none";
        ResetPerFrame();
    }
};

struct LocalReflectionRadianceDescriptorTables {
    std::array<std::array<DescriptorHandle, 7>, kFrameCount> srvTables{};
    std::array<std::array<DescriptorHandle, 1>, kFrameCount> uavTables{};
    bool valid = false;

    void Reset() {
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

struct LocalReflectionRadianceState {
    LocalReflectionRadianceDescriptorTables descriptors;
    LocalReflectionProbeCubemapCaptureState cubemapCapture;
};

} // namespace Cortex::Graphics
