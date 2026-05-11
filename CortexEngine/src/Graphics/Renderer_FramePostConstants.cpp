#include "Renderer.h"

#include "Graphics/RenderableClassification.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <algorithm>
#include <cstdlib>

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

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
        if (m_hzbResources.mipCount > 1) {
            debugParamZ = glm::clamp(static_cast<float>(m_hzbResources.debugMip) / static_cast<float>(m_hzbResources.mipCount - 1u), 0.0f, 1.0f);
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

    // SSAO parameters packed into aoParams. Disable sampling if the SSAO
    // resources are unavailable so post-process does not read null SRVs.
    const bool ssaoResourcesReady = (m_ssaoResources.texture && m_ssaoResources.srv.IsValid());
    frameData.aoParams = glm::vec4(
        (m_ssaoResources.enabled && ssaoResourcesReady) ? 1.0f : 0.0f,
        m_ssaoResources.radius,
        m_ssaoResources.bias,
        m_ssaoResources.intensity);

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
    if (m_ssrResources.activeThisFrame) {
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
        m_bloomResources.threshold,
        m_bloomResources.softKnee,
        m_bloomResources.maxContribution,
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
        m_ssrResources.maxDistance,
        m_ssrResources.thickness,
        m_ssrResources.strength,
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
