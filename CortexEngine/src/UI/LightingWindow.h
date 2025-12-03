#pragma once

#include <windows.h>

namespace Cortex::UI {

// Dedicated lighting control panel:
// - Spawns common light primitives (directional / point / spot).
// - Applies high-level lighting rigs.
// - Adjusts a few global lighting parameters (sun / IBL / god-rays).
class LightingWindow {
public:
    static void Initialize(HWND parent);
    static void Shutdown();

    static void SetVisible(bool visible);
    static void Toggle();
    static bool IsVisible();
};

} // namespace Cortex::UI

