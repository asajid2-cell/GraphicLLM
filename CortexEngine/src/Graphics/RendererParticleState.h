#pragma once

#include <cstdint>

#include <glm/glm.hpp>
#include "Graphics/Renderer_ConstantBuffer.h"

namespace Cortex::Graphics {

struct ParticleInstance {
    glm::vec3 position;
    float size;
    glm::vec4 color;
};

struct ParticleRenderState {
    bool instanceMapFailed = false;
    bool enabledForScene = true;
    ComPtr<ID3D12Resource> instanceBuffer;
    UINT instanceCapacity = 0;
    ComPtr<ID3D12Resource> quadVertexBuffer;

    uint32_t frameEmitterCount = 0;
    uint32_t frameLiveParticles = 0;
    uint32_t frameSubmittedInstances = 0;
    uint32_t frameFrustumCulled = 0;
    uint32_t frameMaxInstances = 4096;
    bool frameCapped = false;
    bool frameExecuted = false;

    [[nodiscard]] uint64_t InstanceBufferBytes() const {
        return static_cast<uint64_t>(instanceCapacity) * sizeof(ParticleInstance);
    }

    void ResetFrameStats() {
        frameEmitterCount = 0;
        frameLiveParticles = 0;
        frameSubmittedInstances = 0;
        frameFrustumCulled = 0;
        frameMaxInstances = 4096;
        frameCapped = false;
        frameExecuted = false;
    }

    void ResetResources() {
        instanceMapFailed = false;
        instanceBuffer.Reset();
        instanceCapacity = 0;
        quadVertexBuffer.Reset();
        ResetFrameStats();
    }
};

} // namespace Cortex::Graphics
