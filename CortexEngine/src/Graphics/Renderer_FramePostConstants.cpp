#include "Renderer.h"

#include "Graphics/RenderableClassification.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

namespace {

float ReflectionV3SourceOverrideFromEnv() {
    static bool s_checked = false;
    static float s_override = 0.0f;
    if (s_checked) {
        return s_override;
    }
    s_checked = true;

    const char* value = std::getenv("CORTEX_V3_REFLECTION_SOURCE_OVERRIDE");
    if (!value || value[0] == '\0') {
        return s_override;
    }

    if (std::strcmp(value, "auto") == 0 || std::strcmp(value, "AUTO") == 0) {
        s_override = 0.0f;
    } else if (std::strcmp(value, "local") == 0 || std::strcmp(value, "scene_local") == 0) {
        s_override = 1.0f;
    } else if (std::strcmp(value, "ssr") == 0 || std::strcmp(value, "screen_space") == 0) {
        s_override = 2.0f;
    } else if (std::strcmp(value, "rt") == 0 ||
               std::strcmp(value, "ray_query") == 0 ||
               std::strcmp(value, "raytraced") == 0 ||
               std::strcmp(value, "ray_traced") == 0) {
        s_override = 3.0f;
    } else if (std::strcmp(value, "environment") == 0 || std::strcmp(value, "env") == 0) {
        s_override = 4.0f;
    } else if (std::strcmp(value, "none") == 0 || std::strcmp(value, "off") == 0) {
        s_override = 255.0f;
    } else {
        const int numeric = std::atoi(value);
        if (numeric == 1 || numeric == 2 || numeric == 3 || numeric == 4 || numeric == 255) {
            s_override = static_cast<float>(numeric);
        } else {
            spdlog::warn("Renderer: ignoring unsupported CORTEX_V3_REFLECTION_SOURCE_OVERRIDE='{}'", value);
            s_override = 0.0f;
        }
    }

    spdlog::info("Renderer: ReflectionV3 source override {}", s_override);
    return s_override;
}

std::string SceneLocalPayloadTextureSetIdForFamily(const std::string& family) {
    std::string id;
    id.reserve(family.size());
    for (char c : family) {
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9')) {
            id.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else if (!id.empty() && id.back() != '_') {
            id.push_back('_');
        }
    }
    while (!id.empty() && id.back() == '_') {
        id.pop_back();
    }
    return id.empty() ? "none" : id;
}

struct SceneLocalPayloadScan {
    uint32_t textureCount = 0;
    uint32_t albedoCount = 0;
    uint32_t normalCount = 0;
    bool present = false;
};

struct SceneLocalPayloadTextureCandidates {
    std::string textureSetId = "none";
    std::string albedoPath;
    std::string normalPath;
    bool present = false;
};

std::string ToLowerAscii(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

bool IsDdsPath(const std::filesystem::path& path) {
    const std::string extension = path.extension().string();
    return extension == ".dds" || extension == ".DDS";
}

int PayloadTexturePriority(const std::filesystem::path& path) {
    const std::string filename = ToLowerAscii(path.filename().string());
    if (filename.find("floor") != std::string::npos) {
        return 0;
    }
    if (filename.find("wall") != std::string::npos) {
        return 1;
    }
    if (filename.find("cube") != std::string::npos) {
        return 2;
    }
    return 3;
}

SceneLocalPayloadTextureCandidates FindSceneLocalPayloadTextureCandidates(const std::string& family) {
    SceneLocalPayloadTextureCandidates result{};
    result.textureSetId = SceneLocalPayloadTextureSetIdForFamily(family);
    if (result.textureSetId == "none") {
        return result;
    }

    std::vector<std::filesystem::path> candidateTextureSetPaths = {
        std::filesystem::path("assets/textures/scene_local") / result.textureSetId,
        std::filesystem::path("../../assets/textures/scene_local") / result.textureSetId,
    };
    if (result.textureSetId == "rt_showcase_gallery") {
        candidateTextureSetPaths.emplace_back("assets/textures/rtshowcase");
        candidateTextureSetPaths.emplace_back("../../assets/textures/rtshowcase");
    }

    std::error_code ec;
    std::filesystem::path textureSetPath;
    for (const auto& candidate : candidateTextureSetPaths) {
        ec.clear();
        if (std::filesystem::exists(candidate, ec) && std::filesystem::is_directory(candidate, ec)) {
            textureSetPath = candidate;
            break;
        }
    }
    if (textureSetPath.empty()) {
        return result;
    }

    std::vector<std::filesystem::path> albedo;
    std::vector<std::filesystem::path> normal;
    for (const auto& entry : std::filesystem::directory_iterator(textureSetPath, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec) || !IsDdsPath(entry.path())) {
            continue;
        }
        const std::string filename = ToLowerAscii(entry.path().filename().string());
        if (filename.find("albedo") != std::string::npos) {
            albedo.push_back(entry.path());
        } else if (filename.find("normal") != std::string::npos) {
            normal.push_back(entry.path());
        }
    }

    auto textureOrder = [](const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
        const int lhsPriority = PayloadTexturePriority(lhs);
        const int rhsPriority = PayloadTexturePriority(rhs);
        if (lhsPriority != rhsPriority) {
            return lhsPriority < rhsPriority;
        }
        return lhs.generic_string() < rhs.generic_string();
    };
    std::sort(albedo.begin(), albedo.end(), textureOrder);
    std::sort(normal.begin(), normal.end(), textureOrder);

    result.present = !albedo.empty() || !normal.empty();
    if (!albedo.empty()) {
        result.albedoPath = albedo.front().generic_string();
    }
    if (!normal.empty()) {
        result.normalPath = normal.front().generic_string();
    }
    return result;
}

SceneLocalPayloadScan ScanSceneLocalPayload(const std::string& family) {
    SceneLocalPayloadScan scan{};
    const std::string setId = SceneLocalPayloadTextureSetIdForFamily(family);
    if (setId == "none") {
        return scan;
    }

    std::vector<std::filesystem::path> candidateTextureSetPaths = {
        std::filesystem::path("assets/textures/scene_local") / setId,
        std::filesystem::path("../../assets/textures/scene_local") / setId,
    };
    if (setId == "rt_showcase_gallery") {
        candidateTextureSetPaths.emplace_back("assets/textures/rtshowcase");
        candidateTextureSetPaths.emplace_back("../../assets/textures/rtshowcase");
    }
    std::error_code ec;
    std::filesystem::path textureSetPath;
    for (const auto& candidate : candidateTextureSetPaths) {
        ec.clear();
        if (std::filesystem::exists(candidate, ec) && std::filesystem::is_directory(candidate, ec)) {
            textureSetPath = candidate;
            break;
        }
    }
    if (textureSetPath.empty()) {
        return scan;
    }

    scan.present = true;
    for (const auto& entry : std::filesystem::directory_iterator(textureSetPath, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const auto extension = entry.path().extension().string();
        if (extension != ".dds" && extension != ".DDS") {
            continue;
        }
        ++scan.textureCount;
        const std::string filename = entry.path().filename().string();
        if (filename.find("albedo") != std::string::npos) {
            ++scan.albedoCount;
        }
        if (filename.find("normal") != std::string::npos) {
            ++scan.normalCount;
        }
    }
    return scan;
}

} // namespace

glm::vec4 Renderer::BuildCinematicStabilityParams() const {
    const bool active = m_sceneVisualContract.active &&
        m_sceneVisualContract.postQualitySetId == "scene_local_cinematic_post_quality_v1";
    if (!active) {
        return glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
    }

    const bool enclosed = m_sceneVisualContract.enclosedScene;
    const bool stableShadowPolicy =
        m_sceneVisualContract.shadowPolicyId == "scene_local_soft_stable_shadows_v1";
    const bool manualExposurePolicy =
        m_sceneVisualContract.exposurePolicyId == "scene_local_manual_exposure_v1";

    const float motionSpecularDamping = enclosed ? 0.24f : 0.14f;
    const float reflectionDebugStability = enclosed ? 0.30f : 0.18f;
    const float shadowSoftnessScale = stableShadowPolicy ? (enclosed ? 1.18f : 1.10f) : 1.0f;
    const float highlightProtection = manualExposurePolicy ? (enclosed ? 0.24f : 0.14f) : 0.0f;

    return glm::vec4(motionSpecularDamping,
                     reflectionDebugStability,
                     shadowSoftnessScale,
                     highlightProtection);
}

glm::vec4 Renderer::BuildCinematicLookParams() const {
    const bool active = m_sceneVisualContract.active &&
        m_sceneVisualContract.postQualitySetId == "scene_local_cinematic_post_quality_v1";
    if (!active) {
        return glm::vec4(0.0f);
    }

    float toeLift = m_sceneVisualContract.enclosedScene ? 0.050f : 0.035f;
    float highlightRolloff = 0.22f;
    float colorSeparation = 0.18f;
    float halationStrength = 0.16f;

    const std::string& toneMapper = m_sceneVisualContract.toneMapperPreset;
    if (toneMapper == "punchy") {
        toeLift = 0.035f;
        highlightRolloff = 0.30f;
        colorSeparation = 0.32f;
        halationStrength = 0.34f;
    } else if (toneMapper == "filmic_soft") {
        toeLift = 0.060f;
        highlightRolloff = 0.24f;
        colorSeparation = 0.20f;
        halationStrength = 0.18f;
    } else if (toneMapper == "reinhard") {
        toeLift = 0.040f;
        highlightRolloff = 0.16f;
        colorSeparation = 0.10f;
        halationStrength = 0.10f;
    }

    const std::string& palette = m_sceneVisualContract.materialPaletteId;
    if (palette.find("kitchen") != std::string::npos) {
        colorSeparation += 0.06f;
        halationStrength += 0.05f;
    } else if (palette.find("office") != std::string::npos) {
        toeLift += 0.02f;
        colorSeparation += 0.04f;
    } else if (palette.find("gym") != std::string::npos ||
               palette.find("classroom") != std::string::npos) {
        highlightRolloff += 0.04f;
        halationStrength -= 0.04f;
    } else if (palette.find("concert") != std::string::npos ||
               palette.find("red_light") != std::string::npos) {
        colorSeparation += 0.10f;
        halationStrength += 0.14f;
    } else if (palette.find("stadium") != std::string::npos) {
        highlightRolloff += 0.08f;
        colorSeparation += 0.08f;
    }

    return glm::vec4(glm::clamp(toeLift, 0.0f, 0.18f),
                     glm::clamp(highlightRolloff, 0.0f, 0.55f),
                     glm::clamp(colorSeparation, 0.0f, 0.55f),
                     glm::clamp(halationStrength, 0.0f, 0.65f));
}

glm::vec4 Renderer::BuildCinematicExposureParams() const {
    const bool active = m_sceneVisualContract.active &&
        m_sceneVisualContract.postQualitySetId == "scene_local_cinematic_post_quality_v1";
    if (!active) {
        return glm::vec4(1.0f, 24.0f, 0.0f, 0.0f);
    }

    float exposureTrim = m_sceneVisualContract.enclosedScene ? 0.90f : 0.96f;
    float hdrShoulderStart = 5.8f;
    float hdrShoulderStrength = 0.22f;
    float postWhiteCompression = 0.16f;

    const std::string& toneMapper = m_sceneVisualContract.toneMapperPreset;
    if (toneMapper == "punchy") {
        exposureTrim = 0.95f;
        hdrShoulderStart = 7.0f;
        hdrShoulderStrength = 0.14f;
        postWhiteCompression = 0.10f;
    } else if (toneMapper == "filmic_soft") {
        exposureTrim = 0.88f;
        hdrShoulderStart = 5.2f;
        hdrShoulderStrength = 0.26f;
        postWhiteCompression = 0.18f;
    } else if (toneMapper == "reinhard") {
        exposureTrim = 0.92f;
        hdrShoulderStart = 6.5f;
        hdrShoulderStrength = 0.12f;
        postWhiteCompression = 0.10f;
    }

    const std::string& profile = m_sceneVisualContract.profileId;
    const std::string& palette = m_sceneVisualContract.materialPaletteId;
    if (profile.find("gallery") != std::string::npos) {
        exposureTrim = std::min(exposureTrim, 0.76f);
        hdrShoulderStart = std::min(hdrShoulderStart, 3.8f);
        hdrShoulderStrength = std::max(hdrShoulderStrength, 0.42f);
        postWhiteCompression = std::max(postWhiteCompression, 0.36f);
    }
    if (palette.find("kitchen") != std::string::npos) {
        exposureTrim = std::min(exposureTrim, 0.72f);
        hdrShoulderStart = std::min(hdrShoulderStart, 3.8f);
        hdrShoulderStrength = std::max(hdrShoulderStrength, 0.42f);
        postWhiteCompression = std::max(postWhiteCompression, 0.36f);
    } else if (palette.find("office") != std::string::npos) {
        exposureTrim = std::min(exposureTrim, 0.72f);
        hdrShoulderStart = std::min(hdrShoulderStart, 3.6f);
        hdrShoulderStrength = std::max(hdrShoulderStrength, 0.44f);
        postWhiteCompression = std::max(postWhiteCompression, 0.38f);
    } else if (palette.find("gym") != std::string::npos ||
               palette.find("classroom") != std::string::npos) {
        exposureTrim = std::min(exposureTrim, 0.46f);
        hdrShoulderStart = std::min(hdrShoulderStart, 2.2f);
        hdrShoulderStrength = std::max(hdrShoulderStrength, 0.74f);
        postWhiteCompression = std::max(postWhiteCompression, 0.68f);
    } else if (palette.find("concert") != std::string::npos ||
               palette.find("red_light") != std::string::npos) {
        exposureTrim = std::max(exposureTrim, 0.94f);
        hdrShoulderStart = std::max(hdrShoulderStart, 6.8f);
        hdrShoulderStrength = std::min(hdrShoulderStrength, 0.20f);
        postWhiteCompression = std::min(postWhiteCompression, 0.16f);
    } else if (palette.find("stadium") != std::string::npos) {
        exposureTrim = std::min(exposureTrim, 0.62f);
        hdrShoulderStart = std::min(hdrShoulderStart, 3.0f);
        hdrShoulderStrength = std::max(hdrShoulderStrength, 0.56f);
        postWhiteCompression = std::max(postWhiteCompression, 0.48f);
    }

    return glm::vec4(glm::clamp(exposureTrim, 0.42f, 1.10f),
                     glm::clamp(hdrShoulderStart, 1.0f, 24.0f),
                     glm::clamp(hdrShoulderStrength, 0.0f, 0.80f),
                     glm::clamp(postWhiteCompression, 0.0f, 0.70f));
}

glm::vec4 Renderer::BuildSceneLocalEnvironmentV3ProfileParams() const {
    if (!m_sceneVisualContract.active) {
        return glm::vec4(0.0f);
    }

    const std::string& family = m_sceneVisualContract.family;
    const std::string& profile = m_sceneVisualContract.profileId;
    const std::string& rig = m_sceneVisualContract.lightRigId;

    float profileMode = 0.0f; // neutral / lab
    float localBackgroundStrength = 0.20f;

    if (family.find("gallery") != std::string::npos ||
        profile.find("gallery") != std::string::npos) {
        profileMode = 1.0f;
        localBackgroundStrength = m_sceneVisualContract.visibleExternalHDRIAllowed ? 0.35f : 0.75f;
    } else if (family.find("concert") != std::string::npos ||
               family.find("stage") != std::string::npos ||
               family.find("red_room") != std::string::npos ||
               profile.find("concert") != std::string::npos ||
               profile.find("red_room") != std::string::npos ||
               rig.find("stage") != std::string::npos ||
               rig.find("concert") != std::string::npos) {
        profileMode = 3.0f;
        localBackgroundStrength = 1.0f;
    } else if (m_sceneVisualContract.enclosedScene) {
        profileMode = 2.0f;
        localBackgroundStrength = m_sceneVisualContract.visibleExternalHDRIAllowed ? 0.55f : 1.0f;
    } else {
        profileMode = 4.0f;
        localBackgroundStrength = 0.05f;
    }

    return glm::vec4(profileMode,
                     glm::clamp(localBackgroundStrength, 0.0f, 1.0f),
                     0.0f,
                     0.0f);
}

glm::vec4 Renderer::BuildSceneLocalEnvironmentV3PayloadParams() const {
    if (!m_sceneVisualContract.active) {
        return glm::vec4(0.0f);
    }

    const SceneLocalPayloadScan scan = ScanSceneLocalPayload(m_sceneVisualContract.family);
    const bool irradianceProxyReady =
        scan.albedoCount > 0 &&
        m_environmentState.localProbeDiffuseIntensity > 0.0f;
    const bool specularProxyReady =
        scan.normalCount > 0 &&
        m_environmentState.localProbeSpecularIntensity > 0.0f;
    const bool visibleBackgroundProxyReady =
        m_sceneVisualContract.enclosedScene &&
        !m_sceneVisualContract.externalHDRIVisible &&
        scan.albedoCount > 0;
    const uint32_t proxyCount =
        (irradianceProxyReady ? 1u : 0u) +
        (specularProxyReady ? 1u : 0u) +
        (visibleBackgroundProxyReady ? 1u : 0u);
    const bool payloadReady =
        scan.present &&
        scan.textureCount >= 2 &&
        scan.albedoCount > 0 &&
        scan.normalCount > 0 &&
        proxyCount > 0u;
    const float textureRichness = glm::clamp(static_cast<float>(scan.textureCount) / 10.0f, 0.0f, 1.0f);
    const float proxyScore = glm::clamp(static_cast<float>(proxyCount) / 3.0f, 0.0f, 1.0f);
    const float shaderInfluence =
        payloadReady
            ? glm::clamp(0.35f + 0.35f * textureRichness + 0.25f * proxyScore, 0.0f, 1.0f)
            : 0.0f;

    return glm::vec4(payloadReady ? 1.0f : 0.0f,
                     textureRichness,
                     proxyScore,
                     shaderInfluence);
}

Renderer::SceneLocalEnvironmentV3PayloadBindingInfo
Renderer::BuildSceneLocalEnvironmentV3PayloadBindingInfo(bool queueMissingUploads) {
    SceneLocalEnvironmentV3PayloadBindingInfo info{};
    if (!m_sceneVisualContract.active) {
        info.fallbackReason = "scene_profile_inactive";
        return info;
    }

    const SceneLocalPayloadTextureCandidates candidates =
        FindSceneLocalPayloadTextureCandidates(m_sceneVisualContract.family);
    info.textureSetId = candidates.textureSetId;
    info.albedoPath = candidates.albedoPath;
    info.normalPath = candidates.normalPath;
    info.resourceTableRequired = candidates.present;
    if (!candidates.present) {
        info.fallbackReason = "scene_local_payload_set_missing";
        return info;
    }
    if (candidates.albedoPath.empty() || candidates.normalPath.empty()) {
        info.fallbackReason = "scene_local_payload_pair_incomplete";
        return info;
    }

    if (TryGetCachedTexture(candidates.albedoPath, true, AssetRegistry::TextureKind::Generic, info.albedo) &&
        info.albedo && info.albedo->GetResource()) {
        ++info.boundResourceCount;
    } else if (queueMissingUploads) {
        (void)QueueTextureUploadFromFile(candidates.albedoPath, true, AssetRegistry::TextureKind::Generic);
    }

    if (TryGetCachedTexture(candidates.normalPath, false, AssetRegistry::TextureKind::Generic, info.normal) &&
        info.normal && info.normal->GetResource()) {
        ++info.boundResourceCount;
    } else if (queueMissingUploads) {
        (void)QueueTextureUploadFromFile(candidates.normalPath, false, AssetRegistry::TextureKind::Generic);
    }

    info.resourceTableBindable = info.boundResourceCount > 0;
    if (info.boundResourceCount >= 2u) {
        info.bindingSource = "cached_scene_local_payload_pair";
        info.fallbackReason = "none";
    } else if (info.boundResourceCount == 1u) {
        info.bindingSource = "partial_cached_scene_local_payload";
        info.fallbackReason = "one_payload_texture_not_resident";
    } else {
        info.bindingSource = "null_payload_descriptors";
        info.fallbackReason = "payload_textures_not_resident";
    }
    return info;
}

void Renderer::PopulateFrameDebugAndPostConstants(FrameConstants& frameData,
                                                 Scene::ECS_Registry* registry,
                                                 const FrameConstantCameraState& cameraState) {
    const float invWidth = cameraState.invWidth;
    const float invHeight = cameraState.invHeight;
    float overlayFlag = m_debugOverlayState.visible ? 1.0f : 0.0f;
    float selectedNorm = 0.0f;
    if (m_debugOverlayState.visible) {
        // Normalize selected row (0..14) into 0..1 for the shader.
        selectedNorm = glm::clamp(static_cast<float>(m_debugOverlayState.selectedRow) / 14.0f, 0.0f, 1.0f);
    }
    float debugParamZ = selectedNorm;
    if (m_debugViewState.mode == 32u) {
        // HZB debug view: repurpose debugMode.z as a normalized mip selector.
        if (m_hzbResources.resources.mipCount > 1) {
            debugParamZ = glm::clamp(static_cast<float>(m_hzbResources.debug.debugMip) / static_cast<float>(m_hzbResources.resources.mipCount - 1u), 0.0f, 1.0f);
        } else {
            debugParamZ = 0.0f;
        }
    }
    // debugMode.w is used as a coarse "RT history valid" flag across the
    // shading and post-process passes. Treat history as valid once any of
    // the RT pipelines (shadows, GI, reflections) has produced at least one
    // frame of data so temporal filtering can stabilize without requiring
    // every RT feature to be active at the same time.
    const bool anyRTTemporalHistory =
        m_temporalHistory.manager.IsValid(TemporalHistoryId::RTShadow) ||
        m_temporalHistory.manager.IsValid(TemporalHistoryId::RTGI) ||
        m_temporalHistory.manager.IsValid(TemporalHistoryId::RTReflection);
    float rtHistoryValid = anyRTTemporalHistory ? 1.0f : 0.0f;
    frameData.debugMode = glm::vec4(
        static_cast<float>(m_debugViewState.mode),
        overlayFlag,
        debugParamZ,
        rtHistoryValid);

    // Post-process parameters: reciprocal resolution, FXAA flag, and an extra
    // channel used as a simple runtime toggle for ray-traced sun shadows in
    // the shading path (when DXR is available and the RT pipeline is valid).
    float fxaaFlag = (m_temporalAAState.enabled ? 0.0f : (m_postProcessState.fxaaEnabled ? 1.0f : 0.0f));
    bool rtPipelineReady =
        m_rtRuntimeState.supported &&
        m_rtRuntimeState.enabled &&
        m_services.rayTracingContext &&
        m_services.rayTracingContext->HasPipeline();
    bool rtReflPipelineReady =
        rtPipelineReady &&
        m_services.rayTracingContext &&
        m_services.rayTracingContext->HasReflectionPipeline();
    // postParams.w represents "RT sun shadows enabled" per ShaderTypes.h line 102.
    // This flag gates the RT shadow mask sampling in Basic.hlsl (line 878).
    // RT shadows are always active when the RT pipeline is ready, unlike
    // reflections/GI which have separate feature toggles.
    float rtShadowsToggle = rtPipelineReady ? 1.0f : 0.0f;
    frameData.postParams = glm::vec4(invWidth, invHeight, fxaaFlag, rtShadowsToggle);

    // Image-based lighting parameters
    float iblEnabled = m_environmentState.enabled ? 1.0f : 0.0f;
    frameData.envParams = glm::vec4(
        m_environmentState.diffuseIntensity,
        m_environmentState.specularIntensity,
        iblEnabled,
        m_environmentState.backgroundExposure);

    // Color grading parameters (warm/cool) for post-process. We repurpose
    // colorGrade.z for volumetric sun shafts and colorGrade.w for vignette
    // so Phase 3 cinematic controls stay in the existing frame constants.
    frameData.colorGrade = glm::vec4(
        m_postProcessState.warm,
        m_postProcessState.cool,
        m_postProcessState.godRayIntensity,
        m_postProcessState.EffectiveVignette());

    // Exponential height fog parameters
    frameData.fogParams = glm::vec4(
        m_fogState.density,
        m_fogState.height,
        m_fogState.falloff,
        m_fogState.enabled ? 1.0f : 0.0f);
    const glm::vec4 sceneLocalPayload = BuildSceneLocalEnvironmentV3PayloadParams();
    frameData.fogExtraParams = glm::vec4(
        m_fogState.startDistance,
        sceneLocalPayload.x,
        sceneLocalPayload.y,
        sceneLocalPayload.w);

    // SSAO parameters packed into aoParams. Disable sampling if the SSAO
    // resources are unavailable so post-process does not read null SRVs.
    const bool ssaoResourcesReady = (m_ssaoResources.resources.texture && m_ssaoResources.resources.srv.IsValid());
    frameData.aoParams = glm::vec4(
        (m_ssaoResources.controls.enabled && ssaoResourcesReady) ? 1.0f : 0.0f,
        m_ssaoResources.controls.radius,
        m_ssaoResources.controls.bias,
        m_ssaoResources.controls.intensity);

    // Bloom shaping parameters. The w component is used as a small bitmask for
    // post-process feature toggles so the shader can safely gate optional
    // sampling without relying on other unrelated flags:
    //   bit0: SSR enabled
    //   bit1: RT reflections enabled
    //   bit2: RT reflection history valid
    //   bit3: disable RT reflection temporal (debug)
    //   bit4: visibility-buffer path active this frame (HUD / debug)
    //   bits 5-7: RT reflection composition strength quantized to 0..7
    //   bits 16-23: RT reflection denoise alpha quantized to 0..255
    //   bit24: V2 reflection resolver candidate drives beauty (debug/review)
    m_visibilityBufferState.plannedThisFrame = false;
    if (m_visibilityBufferState.enabled && m_services.visibilityBuffer && registry) {
        auto renderableView = registry->View<Scene::RenderableComponent>();
        for (auto entity : renderableView) {
            const auto& renderable = renderableView.get<Scene::RenderableComponent>(entity);
            if (!renderable.visible || !renderable.mesh) {
                continue;
            }
            if (IsTransparentRenderable(renderable)) {
                continue;
            }
            m_visibilityBufferState.plannedThisFrame = true;
            break;
        }
    }
    uint32_t postFxFlags = 0u;
    static bool s_checkedRtReflPostFxEnv = false;
    static bool s_disableRtReflTemporal = false;
    static bool s_checkedV2ReflectionCandidateEnv = false;
    if (!s_checkedRtReflPostFxEnv) {
        s_checkedRtReflPostFxEnv = true;
        if (std::getenv("CORTEX_RTREFL_DISABLE_TEMPORAL")) {
            s_disableRtReflTemporal = true;
            spdlog::warn("Renderer: CORTEX_RTREFL_DISABLE_TEMPORAL set; disabling RT reflection temporal accumulation (debug)");
        }
    }
    if (!s_checkedV2ReflectionCandidateEnv) {
        s_checkedV2ReflectionCandidateEnv = true;
        if (std::getenv("CORTEX_V2_REFLECTION_CANDIDATE_BEAUTY")) {
            m_postProcessState.v2ReflectionCandidateEnabled = true;
            spdlog::warn("Renderer: CORTEX_V2_REFLECTION_CANDIDATE_BEAUTY set; V2 reflection candidate drives beauty (review)");
        }
    }
    if (m_ssrResources.frame.activeThisFrame) {
        postFxFlags |= 1u;
    }
    if (rtReflPipelineReady && m_rtRuntimeState.reflectionsEnabled) {
        postFxFlags |= 2u;
    }
    if (rtReflPipelineReady && m_temporalHistory.manager.CanReproject(TemporalHistoryId::RTReflection)) {
        postFxFlags |= 4u;
    }
    if (s_disableRtReflTemporal) {
        postFxFlags |= 8u;
    }
    if (m_visibilityBufferState.plannedThisFrame) {
        postFxFlags |= 16u;
    }
    const uint32_t rtReflectionComposition =
        static_cast<uint32_t>(glm::clamp(m_rtDenoiseState.reflectionCompositionStrength, 0.0f, 1.0f) * 7.0f + 0.5f);
    const uint32_t rtReflectionAlpha =
        static_cast<uint32_t>(glm::clamp(m_rtDenoiseState.reflectionHistoryAlpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    postFxFlags |= (rtReflectionComposition & 0x7u) << 5u;
    postFxFlags |= m_postProcessState.EncodedLensDirtByte() << 8u;
    postFxFlags |= (rtReflectionAlpha & 0xFFu) << 16u;
    if (m_postProcessState.v2ReflectionCandidateEnabled) {
        postFxFlags |= 1u << 24u;
    }
    frameData.bloomParams = glm::vec4(
        m_bloomResources.controls.threshold,
        m_bloomResources.controls.softKnee,
        m_bloomResources.controls.maxContribution,
        static_cast<float>(postFxFlags));

    // TAA parameters: history UV offset from jitter delta and blend factor / enable flag.
    // Only enable TAA in the shader once we have a valid history buffer;
    // this avoids sampling uninitialized history and causing color flashes
    // on the first frame after startup or resize. When the camera is nearly
    // stationary we reduce jitter and blend strength to keep edges crisp and
    // minimize residual ghosting.
    glm::vec2 jitterDeltaPixels = m_temporalAAState.jitterPrevPixels - m_temporalAAState.jitterCurrPixels;
    glm::vec2 jitterDeltaUV = glm::vec2(jitterDeltaPixels.x * invWidth, jitterDeltaPixels.y * invHeight);
    const bool taaActiveThisFrame = m_temporalAAState.enabled && m_temporalHistory.manager.CanReproject(TemporalHistoryId::TAAColor);
    float blendForThisFrame = m_temporalAAState.blendFactor;
    if (!m_temporalAAState.cameraIsMoving) {
        // When the camera is effectively stationary, reduce blend strength
        // so history converges but does not dominate the image.
        blendForThisFrame *= 0.5f;
    }
    frameData.taaParams = glm::vec4(
        jitterDeltaUV.x,
        jitterDeltaUV.y,
        blendForThisFrame,
        taaActiveThisFrame ? 1.0f : 0.0f);

    // Water parameters shared with shaders (see ShaderTypes.h / Basic.hlsl).
    frameData.waterParams0 = glm::vec4(
        m_waterState.waveAmplitude,
        m_waterState.waveLength,
        m_waterState.waveSpeed,
        m_waterState.levelY);
    frameData.waterParams1 = glm::vec4(
        m_waterState.primaryDirection.x,
        m_waterState.primaryDirection.y,
        m_waterState.secondaryAmplitude,
        m_waterState.steepness);

    frameData.ssrParams = glm::vec4(
        m_ssrResources.controls.maxDistance,
        m_ssrResources.controls.thickness,
        m_ssrResources.controls.strength,
        0.0f);
    frameData.postGradeParams = glm::vec4(
        m_postProcessState.contrast,
        m_postProcessState.saturation,
        m_postProcessState.EffectiveMotionBlur(),
        m_postProcessState.EffectiveDepthOfField());
    frameData.rtReflectionParams = glm::vec4(
        m_rtDenoiseState.reflectionRoughnessThreshold,
        m_rtDenoiseState.reflectionHistoryMaxBlend,
        m_rtDenoiseState.reflectionFireflyClampLuma,
        m_rtDenoiseState.reflectionSignalScale);
    frameData.cinematicParams = glm::vec4(
        static_cast<float>(m_postProcessState.ToneMapperMode()),
        glm::radians(m_environmentState.rotationDegrees),
        m_rtDenoiseState.giStrength,
        m_rtDenoiseState.giRayDistance);
    const glm::vec4 sceneLocalEnvironmentProfile = BuildSceneLocalEnvironmentV3ProfileParams();
    frameData.cinematicDofParams = glm::vec4(
        m_postProcessState.dofFocusDistance,
        m_postProcessState.dofAperture,
        sceneLocalEnvironmentProfile.x,
        sceneLocalEnvironmentProfile.y);
    frameData.cinematicStabilityParams = BuildCinematicStabilityParams();
    frameData.cinematicLookParams = BuildCinematicLookParams();
    frameData.cinematicExposureParams = BuildCinematicExposureParams();
    frameData.localProbeParams = glm::vec4(
        m_environmentState.localProbeDiffuseIntensity,
        m_environmentState.localProbeSpecularIntensity,
        m_environmentState.localProbeRadianceEnabled ? 1.0f : 0.0f,
        ReflectionV3SourceOverrideFromEnv());

    // Default clustered-light parameters for forward+ transparency. These are
    // overridden by the VB path once the per-frame local light buffer and
    // clustered lists are built.
    frameData.screenAndCluster = glm::uvec4(
        static_cast<uint32_t>(m_services.window ? m_services.window->GetWidth() : 0),
        static_cast<uint32_t>(m_services.window ? m_services.window->GetHeight() : 0),
        16u,
        9u
    );
    frameData.clusterParams = glm::uvec4(24u, 128u, 0u, 0u);
    frameData.clusterSRVIndices = glm::uvec4(kInvalidBindlessIndex, kInvalidBindlessIndex, kInvalidBindlessIndex, 0u);
    frameData.projectionParams = glm::vec4(
        frameData.projectionMatrix[0][0],
        frameData.projectionMatrix[1][1],
        m_cameraState.nearPlane,
        m_cameraState.farPlane
    );

}

} // namespace Cortex::Graphics
