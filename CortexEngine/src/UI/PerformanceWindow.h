#pragma once

#include <windows.h>

namespace Cortex::UI {

// Detailed performance and memory diagnostics window. Exposes per-pass
// timings, GPU memory breakdown, job queue status, asset-level usage, and
// fine-grained quality controls so heavy scenes can be tuned without
// changing code.
class PerformanceWindow {
public:
    static void Initialize(HWND parent);
    static void Shutdown();

    static void Toggle();
    static void SetVisible(bool visible);
    static bool IsVisible();
};

} // namespace Cortex::UI

