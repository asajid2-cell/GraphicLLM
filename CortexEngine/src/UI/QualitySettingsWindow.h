#pragma once

#include <windows.h>

namespace Cortex::UI {

// Lightweight Win32 window for performance / quality controls.
// Exposes render scale, RTX feature toggles, and shows an
// approximate FPS + VRAM usage readout so users can tune the
// engine for their GPU without rebuilding.
class QualitySettingsWindow {
public:
    static void Initialize(HWND parent);
    static void Shutdown();

    static void SetVisible(bool visible);
    static void Toggle();
    static bool IsVisible();
};

} // namespace Cortex::UI

