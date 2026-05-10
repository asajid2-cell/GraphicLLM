#pragma once

#include <vector>

#include "Graphics/VisibilityBuffer.h"

namespace Cortex::Graphics {

struct RendererVisibilityBufferState {
    std::vector<VBInstanceData> instances;
    std::vector<VisibilityBufferRenderer::VBMeshDrawInfo> meshDraws;
    bool enabled = false;
    bool plannedThisFrame = false;
    bool renderedThisFrame = false;
    bool hzbOcclusionUsedThisFrame = false;
    bool debugOverrideThisFrame = false;

    void ResetFrameFlags() {
        renderedThisFrame = false;
        hzbOcclusionUsedThisFrame = false;
        debugOverrideThisFrame = false;
    }

    void ClearDrawInputs() {
        instances.clear();
        meshDraws.clear();
    }
};

} // namespace Cortex::Graphics
