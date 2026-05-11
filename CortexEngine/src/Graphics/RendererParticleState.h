#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

#include <glm/glm.hpp>
#include "Graphics/Renderer_ConstantBuffer.h"

namespace Cortex::Graphics {

struct ParticleInstance {
    glm::vec3 position;
    float size;
    glm::vec4 color;
};

struct ParticleRenderControls {
    bool enabledForScene = true;
    float densityScale = 1.0f;
    float qualityScale = 1.0f;
    float bloomContribution = 1.0f;
    float softDepthFade = 0.5f;
    float windInfluence = 0.0f;
    std::string effectPreset = "gallery_mix";

    void SetDensityScale(float scale) {
        densityScale = std::clamp(scale, 0.0f, 2.0f);
    }

    void SetTuning(float quality, float bloom, float softDepth, float wind) {
        qualityScale = std::clamp(quality, 0.25f, 2.0f);
        bloomContribution = std::clamp(bloom, 0.0f, 2.0f);
        softDepthFade = std::clamp(softDepth, 0.0f, 1.0f);
        windInfluence = std::clamp(wind, 0.0f, 2.0f);
    }

    void SetEffectPreset(const std::string& presetId) {
        effectPreset = presetId.empty() ? "gallery_mix" : presetId;
    }
};

struct ParticleRenderResources {
    ComPtr<ID3D12Resource> instanceBuffer;
    UINT instanceCapacity = 0;
    ComPtr<ID3D12Resource> quadVertexBuffer;

    [[nodiscard]] uint64_t InstanceBufferBytes() const {
        return static_cast<uint64_t>(instanceCapacity) * sizeof(ParticleInstance);
    }

    void Reset() {
        instanceBuffer.Reset();
        instanceCapacity = 0;
        quadVertexBuffer.Reset();
    }
};

struct ParticleFrameStats {
    uint32_t frameEmitterCount = 0;
    uint32_t frameLiveParticles = 0;
    uint32_t frameSubmittedInstances = 0;
    uint32_t frameFrustumCulled = 0;
    uint32_t frameMaxInstances = 4096;
    float frameDensityScale = 1.0f;
    float frameQualityScale = 1.0f;
    float frameBloomContribution = 1.0f;
    float frameSoftDepthFade = 0.5f;
    float frameWindInfluence = 0.0f;
    uint32_t framePresetMatchedEmitters = 0;
    uint32_t framePresetMismatchedEmitters = 0;
    bool frameCapped = false;
    bool frameExecuted = false;

    void Reset(const ParticleRenderControls& controls) {
        frameEmitterCount = 0;
        frameLiveParticles = 0;
        frameSubmittedInstances = 0;
        frameFrustumCulled = 0;
        frameMaxInstances = 4096;
        frameDensityScale = controls.densityScale;
        frameQualityScale = controls.qualityScale;
        frameBloomContribution = controls.bloomContribution;
        frameSoftDepthFade = controls.softDepthFade;
        frameWindInfluence = controls.windInfluence;
        framePresetMatchedEmitters = 0;
        framePresetMismatchedEmitters = 0;
        frameCapped = false;
        frameExecuted = false;
    }
};

struct ParticleRenderState {
    ParticleRenderControls controls;
    ParticleRenderResources resources;
    ParticleFrameStats frame;
    bool instanceMapFailed = false;

    void ResetFrameStats() {
        frame.Reset(controls);
    }

    void ResetResources() {
        instanceMapFailed = false;
        resources.Reset();
        ResetFrameStats();
    }
};

} // namespace Cortex::Graphics
