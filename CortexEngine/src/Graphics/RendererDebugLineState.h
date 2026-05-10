#pragma once

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include "Graphics/Renderer_ConstantBuffer.h"

namespace Cortex::Graphics {

struct DebugLineVertex {
    glm::vec3 position;
    glm::vec4 color;
};

struct DebugLineRenderState {
    std::vector<DebugLineVertex> lines;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
    uint32_t vertexCapacity = 0;
    bool disabled = false;

    void ResetFrame() {
        lines.clear();
    }

    void ResetResources() {
        lines.clear();
        vertexBuffer.Reset();
        vertexCapacity = 0;
        disabled = false;
    }
};

struct RendererDebugViewState {
    uint32_t mode = 0;
};

} // namespace Cortex::Graphics
