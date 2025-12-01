#pragma once

#include <windows.h>

namespace Cortex::UI {

// Modeless Win32 window that exposes a simple scene editor:
// - Spawn primitives (cube, sphere, plane, etc.)
// - Spawn glTF sample models from the built-in library
// - Apply material presets used by the renderer
// Entities are placed near the active camera and can be adjusted further
// using the in-engine translate/rotate/scale gizmos.
class SceneEditorWindow {
public:
    static void Initialize(HWND parent);
    static void Shutdown();

    static void Toggle();
    static void SetVisible(bool visible);
    static bool IsVisible();
};

} // namespace Cortex::UI

