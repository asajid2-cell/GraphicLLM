#include "DebugMenu.h"

#include "Core/ServiceLocator.h"
#include "Graphics/Renderer.h"

namespace Cortex::UI {

namespace {

// Lightweight, windowless debug menu implementation.
// This keeps the same public API but avoids complex Win32 UI so that
// graphics debugging and rendering can proceed even if the old UI
// implementation was failing to compile or causing instability.

struct DebugMenuInternalState {
    DebugMenuState current{};
    DebugMenuState defaults{};
    bool initialized = false;
    bool visible = false;
    HWND parent = nullptr;
};

DebugMenuInternalState g_state;

void ApplyStateToRenderer(const DebugMenuState& state) {
    auto* renderer = Cortex::ServiceLocator::GetRenderer();
    if (!renderer) {
        return;
    }

    renderer->SetExposure(state.exposure);
    renderer->SetShadowBias(state.shadowBias);
    renderer->SetShadowPCFRadius(state.shadowPCFRadius);
    renderer->SetCascadeSplitLambda(state.cascadeLambda);

    float currentScale = renderer->GetCascadeResolutionScale(0);
    float targetScale = state.cascade0ResolutionScale;
    renderer->AdjustCascadeResolutionScale(0, targetScale - currentScale);

    renderer->SetBloomIntensity(state.bloomIntensity);

    renderer->SetFractalParams(
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

    // Feature toggles
    renderer->SetShadowsEnabled(state.shadowsEnabled);
    renderer->SetPCSS(state.pcssEnabled);
    renderer->SetFXAAEnabled(state.fxaaEnabled);
    renderer->SetTAAEnabled(state.taaEnabled);
    renderer->SetSSREnabled(state.ssrEnabled);
    renderer->SetSSAOEnabled(state.ssaoEnabled);
    renderer->SetIBLEnabled(state.iblEnabled);
    renderer->SetFogEnabled(state.fogEnabled);

    if (renderer->IsRayTracingSupported()) {
        renderer->SetRayTracingEnabled(state.rayTracingEnabled);
    }
}

} // namespace

void DebugMenu::Initialize(HWND parent, const DebugMenuState& initialState) {
    g_state.parent = parent;
    g_state.current = initialState;
    g_state.defaults = initialState;
    g_state.initialized = true;
    g_state.visible = false;

    ApplyStateToRenderer(g_state.current);
}

void DebugMenu::Shutdown() {
    g_state = DebugMenuInternalState{};
}

void DebugMenu::Toggle() {
    if (!g_state.initialized) {
        return;
    }
    g_state.visible = !g_state.visible;
}

void DebugMenu::SetVisible(bool visible) {
    if (!g_state.initialized) {
        return;
    }
    g_state.visible = visible;
}

bool DebugMenu::IsVisible() {
    return g_state.visible;
}

void DebugMenu::SyncFromState(const DebugMenuState& state) {
    if (!g_state.initialized) {
        return;
    }
    g_state.current = state;
    ApplyStateToRenderer(g_state.current);
}

DebugMenuState DebugMenu::GetState() {
    return g_state.current;
}

void DebugMenu::ResetToDefaults() {
    if (!g_state.initialized) {
        return;
    }

    if (auto* renderer = Cortex::ServiceLocator::GetRenderer()) {
        renderer->SetShadowsEnabled(true);
        renderer->SetDebugViewMode(0);
        renderer->SetPCSS(false);
        renderer->SetFXAAEnabled(true);
        renderer->SetTAAEnabled(true);
        renderer->SetSSREnabled(true);
        renderer->SetSSAOEnabled(true);
        renderer->SetIBLEnabled(true);
        renderer->SetFogEnabled(false);
    }

    g_state.current = g_state.defaults;
    g_state.current.shadowsEnabled = true;
    g_state.current.pcssEnabled = false;
    g_state.current.fxaaEnabled = true;
    g_state.current.taaEnabled = true;
    g_state.current.ssrEnabled = true;
    g_state.current.ssaoEnabled = true;
    g_state.current.iblEnabled = true;
    g_state.current.fogEnabled = false;

    ApplyStateToRenderer(g_state.current);
}

} // namespace Cortex::UI
