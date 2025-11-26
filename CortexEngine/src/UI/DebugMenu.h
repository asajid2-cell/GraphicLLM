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

    // Update slider positions from the given state (e.g., if values change externally)
    static void SyncFromState(const DebugMenuState& state);

    // Read current slider values
    static DebugMenuState GetState();
};

} // namespace Cortex::UI
