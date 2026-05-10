#pragma once

namespace Cortex::Graphics {

struct RendererPostProcessState {
    bool fxaaEnabled = true;
    float warm = 0.0f;
    float cool = 0.0f;
    float godRayIntensity = 1.0f;
    float bloomThreshold = 1.0f;
    float bloomSoftKnee = 0.5f;
    float bloomMaxContribution = 4.0f;
    float vignette = 0.0f;
    float lensDirt = 0.0f;
};

} // namespace Cortex::Graphics
