#pragma once

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

    void ResetResources() {
        instanceMapFailed = false;
        instanceBuffer.Reset();
        instanceCapacity = 0;
        quadVertexBuffer.Reset();
    }
};

} // namespace Cortex::Graphics
