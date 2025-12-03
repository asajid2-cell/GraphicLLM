#pragma once

// Forward declaration of HWND to avoid requiring windows.h for every
// translation unit that includes this header. The implementation file
// includes <windows.h> directly.
struct HWND__;
using HWND = HWND__*;

namespace Cortex::UI {

// Scene hierarchy / graph visualizer. Presents the current ECS entities in a
// tree view and lets the user select an entity to drive editor focus and
// selection.
class HierarchyWindow {
public:
    static void Initialize(HWND parent);
    static void Shutdown();

    static void SetVisible(bool visible);
    static void Toggle();
    static bool IsVisible();

    // Rebuild the hierarchy tree from the current ECS registry.
    static void Refresh();

    // Keep tree selection in sync when the engine selection changes elsewhere
    // (e.g., picking or scene editor).
    static void OnSelectionChanged();
};

} // namespace Cortex::UI
