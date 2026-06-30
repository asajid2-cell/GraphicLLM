#pragma once

#include <string>

namespace Cortex::Graphics {

struct RendererQualityRuntimeState {
    std::string activeGraphicsPresetId = "runtime";
    bool graphicsPresetDirtyFromUI = false;
    float exposure = 1.0f;

    // When false, the auto-exposure metering is bypassed and `exposure` is used
    // as a FIXED scene exposure. Needed for deliberately dark/cinematic looks
    // (e.g. a night interior) where auto-exposure would meter the dark scene
    // back up toward mid-grey and wash out the moody lighting.
    bool autoExposureEnabled = true;

    // Internal rendering resolution scale for simple supersampling. Default
    // to 1.0 so that HDR and depth targets match the window resolution; this
    // keeps VRAM usage predictable on 8 GB GPUs. For heavier scenes this can
    // be reduced (e.g. 0.75) to trade some sharpness for lower memory use and
    // shading cost.
    float renderScale = 1.0f;
};

} // namespace Cortex::Graphics
