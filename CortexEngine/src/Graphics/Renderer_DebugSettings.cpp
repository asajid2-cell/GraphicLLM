#include "Renderer.h"

#include <algorithm>
#include <cmath>

#include <glm/gtx/norm.hpp>

namespace Cortex::Graphics {

namespace {
constexpr uint32_t kMaxDebugViewMode = 41u;
}

int Renderer::GetDebugViewMode() const {
    return GetQualityState().debugViewMode;
}

uint32_t Renderer::GetHZBDebugMip() const {
    return GetQualityState().hzbDebugMip;
}

void Renderer::CycleDebugViewMode() {
    // 0  = shaded, 1 = normals, 2 = roughness, 3 = metallic, 4 = albedo,
    // 5  = cascades, 6  = debug screen (post-process / HUD focus),
    // 7  = fractal height,
    // 8  = IBL diffuse only, 9  = IBL specular only, 10 = env direction/UV,
    // 11 = Fresnel (Fibl), 12 = specular mip debug,
    // 13 = SSAO only, 14 = SSAO overlay, 15 = SSR only, 16 = SSR overlay,
    // 17 = forward light debug (heatmap / count),
    // 18 = RT shadow mask debug, 19 = RT shadow history debug,
    // 20 = RT reflection buffer debug (post-process),
    // 21 = RT GI buffer debug,
    // 22 = shaded with RT GI disabled,
    // 23 = shaded with RT reflections disabled (SSR only),
    // 24 = SDF debug / RT reflection ray direction,
    // 25 = TAA history weight debug,
    // 26 = material layer debug (coat / sheen / SSS),
    // 27 = anisotropy debug,
    // 28 = fog factor debug,
    // 29 = water debug.
    // 30 = RT reflection history (post-process),
    // 31 = RT reflection delta (current vs history).
    // 32 = HZB mip debug (depth pyramid).
    // 33 = VB visibility (instance ID)
    // 34 = VB depth (hardware depth buffer)
    // 35 = VB G-buffer albedo
    // 36 = VB G-buffer normal/roughness
    // 37 = VB G-buffer emissive/metallic
    // 38 = VB G-buffer material ext0
    // 39 = VB G-buffer material ext1
    // 40 = VB G-buffer material ext2 / surface class
    // 41 = post-process material class overlay
    m_debugViewState.mode = (m_debugViewState.mode + 1) % (kMaxDebugViewMode + 1u);
    const char* label = nullptr;
    switch (m_debugViewState.mode) {
        case 0: label = "Shaded"; break;
        case 1: label = "Normals"; break;
        case 2: label = "Roughness"; break;
        case 3: label = "Metallic"; break;
        case 4: label = "Albedo"; break;
        case 5: label = "Cascades"; break;
        case 6: label = "DebugScreen"; break;
        case 7: label = "FractalHeight"; break;
        case 8: label = "IBL_Diffuse"; break;
        case 9: label = "IBL_Specular"; break;
        case 10: label = "EnvDirection"; break;
        case 11: label = "Fresnel"; break;
        case 12: label = "SpecularMip"; break;
        case 13: label = "SSAO_Only"; break;
        case 14: label = "SSAO_Overlay"; break;
        case 15: label = "SSR_Only"; break;
        case 16: label = "SSR_Overlay"; break;
        case 17: label = "Light_Debug"; break;
        case 18: label = "RT_ShadowMask"; break;
        case 19: label = "RT_ShadowHistory"; break;
        case 20: label = "RT_ReflectionBuffer"; break;
        case 21: label = "RT_GI_Buffer"; break;
        case 22: label = "Shaded_NoRTGI"; break;
        case 23: label = "Shaded_NoRTRefl"; break;
        case 24: label = "SDF_Debug"; break;
        case 25: label = "TAA_HistoryWeight"; break;
        case 26: label = "MaterialLayers"; break;
        case 27: label = "Anisotropy_Debug"; break;
        case 28: label = "Fog_Factor"; break;
        case 29: label = "Water_Debug"; break;
        case 30: label = "RT_ReflectionHistory"; break;
        case 31: label = "RT_ReflectionDelta"; break;
        case 32: label = "HZB_Mip"; break;
        case 33: label = "VB_Visibility"; break;
        case 34: label = "VB_Depth"; break;
        case 35: label = "VB_GBuffer_Albedo"; break;
        case 36: label = "VB_GBuffer_NormalRoughness"; break;
        case 37: label = "VB_GBuffer_EmissiveMetallic"; break;
        case 38: label = "VB_GBuffer_MaterialExt0"; break;
        case 39: label = "VB_GBuffer_MaterialExt1"; break;
        case 40: label = "VB_GBuffer_SurfaceClass"; break;
        case 41: label = "SurfaceClass_Overlay"; break;
        default: label = "Unknown"; break;
    }
    spdlog::info("Debug view mode: {}", label);
    if (m_debugViewState.mode == 20u || m_debugViewState.mode == 30u || m_debugViewState.mode == 31u) {
        const bool rtSupported = m_rtRuntimeState.supported;
        const bool rtEnabled = m_rtRuntimeState.enabled;
        const bool reflEnabled = m_rtRuntimeState.reflectionsEnabled;
        const bool hasReflRes = (m_rtReflectionTargets.color != nullptr);
        const bool hasReflSrv = m_rtReflectionTargets.srv.IsValid();
        const bool hasReflHistSrv = m_rtReflectionTargets.historySRV.IsValid();
        spdlog::info("RTRefl debug: rtSupported={} rtEnabled={} reflEnabled={} reflRes={} reflSRV={} reflHistSRV={} postTable={}",
                     rtSupported, rtEnabled, reflEnabled, hasReflRes, hasReflSrv, hasReflHistSrv, m_temporalScreenState.postProcessSrvTableValid);
        if (m_services.rayTracingContext) {
            spdlog::info("RTRefl debug: hasReflPipeline={}", m_services.rayTracingContext->HasReflectionPipeline());
        }
        if (const char* mode = std::getenv("CORTEX_RTREFL_CLEAR")) {
            spdlog::info("RTRefl debug: CORTEX_RTREFL_CLEAR={}", mode);
        }
        if (std::getenv("CORTEX_RTREFL_SKIP_DXR")) {
            spdlog::info("RTRefl debug: CORTEX_RTREFL_SKIP_DXR=1");
        }
    }
}

void Renderer::AdjustHZBDebugMip(int delta) {
    if (delta == 0) {
        return;
    }

    if (m_hzbResources.mipCount <= 1) {
        m_hzbResources.debugMip = 0;
        return;
    }

    const int maxMip = static_cast<int>(m_hzbResources.mipCount) - 1;
    const int next = std::clamp(static_cast<int>(m_hzbResources.debugMip) + delta, 0, maxMip);
    if (static_cast<uint32_t>(next) == m_hzbResources.debugMip) {
        return;
    }
    m_hzbResources.debugMip = static_cast<uint32_t>(next);
    spdlog::info("HZB debug mip set to {}/{}", m_hzbResources.debugMip, maxMip);
}

void Renderer::SetDebugViewMode(int mode) {
    const int clamped = std::max(0, std::min(mode, static_cast<int>(kMaxDebugViewMode)));
    if (static_cast<uint32_t>(clamped) == m_debugViewState.mode) {
        return;
    }
    m_debugViewState.mode = static_cast<uint32_t>(clamped);
    spdlog::info("Renderer debug view mode set to {}", clamped);
}

} // namespace Cortex::Graphics
