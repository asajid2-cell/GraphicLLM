#include "RendererControlApplier.h"

#include "Renderer.h"

namespace Cortex::Graphics {

void ApplyDebugControlState(Renderer& renderer, const RendererDebugControlState& state) {
    renderer.SetExposure(state.exposure);
    renderer.SetShadowBias(state.shadowBias);
    renderer.SetShadowPCFRadius(state.shadowPCFRadius);
    renderer.SetCascadeSplitLambda(state.cascadeLambda);

    const float currentScale = renderer.GetCascadeResolutionScale(0);
    renderer.AdjustCascadeResolutionScale(0, state.cascade0ResolutionScale - currentScale);

    renderer.SetBloomIntensity(state.bloomIntensity);

    renderer.SetFractalParams(
        state.fractalAmplitude,
        state.fractalFrequency,
        state.fractalOctaves,
        state.fractalCoordMode,
        state.fractalScaleX,
        state.fractalScaleZ,
        state.fractalLacunarity,
        state.fractalGain,
        state.fractalWarpStrength,
        state.fractalNoiseType);

    renderer.SetShadowsEnabled(state.shadowsEnabled);
    renderer.SetPCSS(state.pcssEnabled);
    renderer.SetFXAAEnabled(state.fxaaEnabled);
    renderer.SetTAAEnabled(state.taaEnabled);
    renderer.SetSSREnabled(state.ssrEnabled);
    renderer.SetSSAOEnabled(state.ssaoEnabled);
    renderer.SetIBLEnabled(state.iblEnabled);
    renderer.SetFogEnabled(state.fogEnabled);

    renderer.SetRayTracingEnabled(state.rayTracingEnabled);
}

void ApplyDebugControlReset(Renderer& renderer) {
    renderer.SetShadowsEnabled(true);
    renderer.SetDebugViewMode(0);
    renderer.SetPCSS(false);
    renderer.SetFXAAEnabled(true);
    renderer.SetTAAEnabled(false);
    renderer.SetSSREnabled(true);
    renderer.SetSSAOEnabled(true);
    renderer.SetIBLEnabled(true);
    renderer.SetFogEnabled(false);
}

void ApplyShadowPCFRadiusDeltaControl(Renderer& renderer, float delta) {
    renderer.SetShadowPCFRadius(renderer.GetShadowPCFRadius() + delta);
}

void ApplyDebugViewModeControl(Renderer& renderer, int mode) {
    renderer.SetDebugViewMode(mode);
}

void ApplyDebugOverlayControl(Renderer& renderer, bool visible, int selectedSection) {
    renderer.SetDebugOverlayState(visible, selectedSection);
}

void ApplyHZBDebugMipDeltaControl(Renderer& renderer, int delta) {
    renderer.AdjustHZBDebugMip(delta);
}

void ApplyGPUCullingEnabledControl(Renderer& renderer, bool enabled) {
    renderer.SetGPUCullingEnabled(enabled);
}

bool ToggleGPUCullingFreezeControl(Renderer& renderer) {
    renderer.ToggleGPUCullingFreeze();
    return renderer.IsGPUCullingFreezeEnabled();
}

void ApplyVoxelBackendControl(Renderer& renderer, bool enabled) {
    renderer.SetVoxelBackendEnabled(enabled);
}

void ApplyShadowBiasDeltaControl(Renderer& renderer, float delta) {
    renderer.SetShadowBias(renderer.GetShadowBias() + delta);
}

void ApplyCascadeSplitLambdaDeltaControl(Renderer& renderer, float delta) {
    renderer.SetCascadeSplitLambda(renderer.GetCascadeSplitLambda() + delta);
}

void ApplyCascadeResolutionScaleDeltaControl(Renderer& renderer, uint32_t cascadeIndex, float delta) {
    renderer.AdjustCascadeResolutionScale(cascadeIndex, delta);
}

void CycleDebugViewControl(Renderer& renderer) {
    renderer.CycleDebugViewMode();
}

} // namespace Cortex::Graphics
