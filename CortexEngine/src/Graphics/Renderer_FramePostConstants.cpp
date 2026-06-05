#include "Renderer.h"

#include "Graphics/RenderableClassification.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <algorithm>
#include <cstdlib>

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

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
    frameData.fogExtraParams = glm::vec4(m_fogState.startDistance, 0.0f, 0.0f, 0.0f);

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
    if (!s_checkedRtReflPostFxEnv) {
        s_checkedRtReflPostFxEnv = true;
        if (std::getenv("CORTEX_RTREFL_DISABLE_TEMPORAL")) {
            s_disableRtReflTemporal = true;
            spdlog::warn("Renderer: CORTEX_RTREFL_DISABLE_TEMPORAL set; disabling RT reflection temporal accumulation (debug)");
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
    frameData.cinematicDofParams = glm::vec4(
        m_postProcessState.dofFocusDistance,
        m_postProcessState.dofAperture,
        0.0f,
        0.0f);
    frameData.cinematicStabilityParams = BuildCinematicStabilityParams();
    frameData.cinematicLookParams = BuildCinematicLookParams();
    frameData.cinematicExposureParams = BuildCinematicExposureParams();

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
