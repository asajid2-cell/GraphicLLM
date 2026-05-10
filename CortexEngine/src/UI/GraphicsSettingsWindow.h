#pragma once

#include <windows.h>

namespace Cortex::UI {

// Unified Phase 3 graphics control surface. The window mirrors
// Graphics::RendererTuningState and routes edits through the renderer
// control applier layer so tuning stays contract-driven.
class GraphicsSettingsWindow {
public:
    static void Initialize(HWND parent);
    static void Shutdown();

    static void SetVisible(bool visible);
    static void Toggle();
    static bool IsVisible();
};

} // namespace Cortex::UI
