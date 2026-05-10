#pragma once

#include <string>

namespace Cortex::Graphics {

struct RendererQualityRuntimeState {
    std::string activeGraphicsPresetId = "runtime";
    bool graphicsPresetDirtyFromUI = false;
    float exposure = 1.0f;

    // Internal rendering resolution scale for simple supersampling. Default
    // to 1.0 so that HDR and depth targets match the window resolution; this
    // keeps VRAM usage predictable on 8 GB GPUs. For heavier scenes this can
    // be reduced (e.g. 0.75) to trade some sharpness for lower memory use and
    // shading cost.
    float renderScale = 1.0f;
};

} // namespace Cortex::Graphics
