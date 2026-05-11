#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace Cortex::Graphics {

class Renderer;

struct RendererQualityTuning {
    std::string preset = "runtime";
    bool dirtyFromUI = false;
    float renderScale = 1.0f;
    bool taaEnabled = true;
    bool fxaaEnabled = false;
    bool gpuCullingEnabled = true;
    bool safeLightingRigOnLowVRAM = true;
};

struct RendererLightingTuning {
    float exposure = 1.0f;
    float bloomIntensity = 0.08f;
    float warm = 0.0f;
    float cool = 0.0f;
    float sunIntensity = 5.0f;
    float godRayIntensity = 0.0f;
    float areaLightSizeScale = 1.0f;
    float shadowBias = 0.001f;
    float shadowPCFRadius = 2.0f;
    float cascadeSplitLambda = 0.65f;
};

struct RendererEnvironmentTuning {
    std::string environmentId = "studio";
    bool iblEnabled = true;
    bool iblLimitEnabled = true;
    float diffuseIntensity = 1.0f;
    float specularIntensity = 1.0f;
    bool backgroundVisible = true;
    float backgroundExposure = 1.0f;
    float backgroundBlur = 0.0f;
};

struct RendererRayTracingTuning {
    bool enabled = true;
    bool reflectionsEnabled = true;
    bool giEnabled = false;
    float reflectionDenoiseAlpha = 0.28f;
    float reflectionCompositionStrength = 1.0f;
    float reflectionRoughnessThreshold = 0.50f;
    float reflectionHistoryMaxBlend = 0.25f;
    float reflectionFireflyClampLuma = 16.0f;
    float reflectionSignalScale = 1.0f;
};

struct RendererScreenSpaceTuning {
    bool ssaoEnabled = true;
    float ssaoRadius = 1.5f;
    float ssaoBias = 0.02f;
    float ssaoIntensity = 1.0f;
    bool ssrEnabled = true;
    float ssrMaxDistance = 30.0f;
    float ssrThickness = 0.20f;
    float ssrStrength = 1.0f;
    bool pcssEnabled = true;
};

struct RendererAtmosphereTuning {
    bool fogEnabled = true;
    float fogDensity = 0.015f;
    float fogHeight = 0.0f;
    float fogFalloff = 0.5f;
};

struct RendererWaterTuning {
    float levelY = 0.0f;
    float waveAmplitude = 0.2f;
    float waveLength = 8.0f;
    float waveSpeed = 1.0f;
    float secondaryAmplitude = 0.1f;
    float roughness = 0.03f;
    float fresnelStrength = 1.0f;
};

struct RendererParticleTuning {
    bool enabled = true;
    float densityScale = 1.0f;
};

struct RendererCinematicPostTuning {
    bool enabled = false;
    float bloomThreshold = 1.0f;
    float bloomSoftKnee = 0.5f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    float vignette = 0.0f;
    float lensDirt = 0.0f;
};

struct RendererTuningState {
    RendererQualityTuning quality;
    RendererLightingTuning lighting;
    RendererEnvironmentTuning environment;
    RendererRayTracingTuning rayTracing;
    RendererScreenSpaceTuning screenSpace;
    RendererAtmosphereTuning atmosphere;
    RendererWaterTuning water;
    RendererParticleTuning particles;
    RendererCinematicPostTuning cinematicPost;
};

[[nodiscard]] RendererTuningState CaptureRendererTuningState(const Renderer& renderer);
[[nodiscard]] RendererTuningState ClampRendererTuningState(RendererTuningState state);
void ApplyRendererTuningState(Renderer& renderer, const RendererTuningState& state);
[[nodiscard]] std::filesystem::path GetDefaultRendererTuningStatePath();
[[nodiscard]] std::filesystem::path GetDefaultRendererGraphicsPresetCollectionPath();
[[nodiscard]] std::optional<RendererTuningState> LoadRendererTuningStateFile(const std::filesystem::path& path,
                                                                              std::string* error = nullptr);
[[nodiscard]] std::optional<RendererTuningState> LoadRendererGraphicsPresetFile(const std::filesystem::path& path,
                                                                                const std::string& presetId,
                                                                                std::string* resolvedPresetId = nullptr,
                                                                                std::string* error = nullptr);
bool SaveRendererTuningStateFile(const std::filesystem::path& path,
                                 const RendererTuningState& state,
                                 std::string* error = nullptr);

} // namespace Cortex::Graphics
