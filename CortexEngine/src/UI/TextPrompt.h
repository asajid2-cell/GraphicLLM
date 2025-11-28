#pragma once

#include <string>
#include <windows.h>

namespace Cortex::UI {

// Simple blocking Windows edit box for collecting text input
class TextPrompt {
public:
    // Show the prompt; returns empty string on cancel/close
    static std::string Show(HWND parent, const std::string& title = "Architect Input", const std::string& prompt = "Describe what to add to the scene:");
};

} // namespace Cortex::UI
