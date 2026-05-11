#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

namespace Cortex::Graphics {

struct RendererPostProcessState {
    bool fxaaEnabled = true;
    bool cinematicEnabled = false;
    float warm = 0.0f;
    float cool = 0.0f;
    float godRayIntensity = 1.0f;
    float bloomThreshold = 1.0f;
    float bloomSoftKnee = 0.5f;
    float bloomMaxContribution = 4.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    float vignette = 0.0f;
    float lensDirt = 0.0f;
    float motionBlur = 0.0f;
    float depthOfField = 0.0f;
    std::string colorGradePreset = "neutral";

    [[nodiscard]] float EffectiveVignette() const {
        return cinematicEnabled ? std::clamp(vignette, 0.0f, 1.0f) : 0.0f;
    }

    [[nodiscard]] float EffectiveLensDirt() const {
        return cinematicEnabled ? std::clamp(lensDirt, 0.0f, 1.0f) : 0.0f;
    }

    [[nodiscard]] float EffectiveMotionBlur() const {
        return cinematicEnabled ? std::clamp(motionBlur, 0.0f, 1.0f) : 0.0f;
    }

    [[nodiscard]] float EffectiveDepthOfField() const {
        return cinematicEnabled ? std::clamp(depthOfField, 0.0f, 1.0f) : 0.0f;
    }

    [[nodiscard]] uint32_t EncodedLensDirtByte() const {
        return static_cast<uint32_t>(EffectiveLensDirt() * 255.0f + 0.5f) & 0xFFu;
    }
};

} // namespace Cortex::Graphics
