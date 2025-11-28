#pragma once

#include <windows.h>

namespace Cortex::UI {

struct DebugMenuState {
    float exposure = 1.0f;
    float shadowBias = 0.0005f;
    float shadowPCFRadius = 1.5f;
    float cascadeLambda = 0.5f;
    float cascade0ResolutionScale = 1.0f;
    float bloomIntensity = 0.25f;
    float cameraBaseSpeed = 5.0f;

    // Fractal surface debug parameters (normal-only bump)
    float fractalAmplitude = 0.0f;      // 0..0.5
    float fractalFrequency = 0.5f;      // 0.1..4.0
    float fractalOctaves = 4.0f;        // 1..6 (treated as int)
    float fractalCoordMode = 1.0f;      // 0 = UV, 1 = world XZ
    float fractalScaleX = 1.0f;         // 0.1..4.0
    float fractalScaleZ = 1.0f;         // 0.1..4.0
    float fractalLacunarity = 2.0f;     // 1.0..4.0
    float fractalGain = 0.5f;           // 0.1..0.9
    float fractalWarpStrength = 0.0f;   // 0.0..1.0
    float fractalNoiseType = 0.0f;      // 0 = fbm, 1 = ridged, 2 = turbulence

    // Lighting rig selection (0 = none/custom, 1 = studio three-point,
    // 2 = top-down warehouse, 3 = horror side-light).
    int lightingRig = 0;
};

// Simple Win32 debug menu window with sliders for renderer/camera parameters.
// The menu is modeless and can be toggled with F2 from the engine.
class DebugMenu {
public:
    static void Initialize(HWND parent, const DebugMenuState& initialState);
    static void Shutdown();

    static void Toggle();
    static void SetVisible(bool visible);
    static bool IsVisible();

    // Reset all renderer/camera debug controls (including debug view/toggles)
    // back to the defaults captured at initialization time.
    static void ResetToDefaults();

    // Update slider positions from the given state (e.g., if values change externally)
    static void SyncFromState(const DebugMenuState& state);

    // Read current slider values
    static DebugMenuState GetState();
};

} // namespace Cortex::UI
