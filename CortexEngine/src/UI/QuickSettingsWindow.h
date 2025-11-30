#pragma once

#include <windows.h>

namespace Cortex::UI {

// Lightweight renderer settings window toggled with the O key.
// This window presents a small subset of DebugMenuState controls
// (exposure, bloom, camera speed, and the main feature toggles) in
// a modeless Win32 tool window.
class QuickSettingsWindow {
public:
    static void Initialize(HWND parent);
    static void Shutdown();

    static void Toggle();
    static void SetVisible(bool visible);
    static bool IsVisible();
};

} // namespace Cortex::UI

