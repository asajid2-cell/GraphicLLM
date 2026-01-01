// InputManager.cpp
// Unified input handling implementation.

#include "InputManager.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

// JSON for serialization
#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace Cortex::Input {

// ============================================================================
// InputSource Implementation
// ============================================================================

InputSource InputSource::Key(KeyCode k, bool shift, bool ctrl, bool alt) {
    InputSource source;
    source.type = Type::Key;
    source.key = k;
    source.requireShift = shift;
    source.requireCtrl = ctrl;
    source.requireAlt = alt;
    return source;
}

InputSource InputSource::Mouse(MouseButton b) {
    InputSource source;
    source.type = Type::MouseButton;
    source.mouseButton = b;
    return source;
}

InputSource InputSource::MouseAxis(uint8_t axis, float scale, bool invert) {
    InputSource source;
    source.type = Type::MouseAxis;
    source.mouseAxis = axis;
    source.axisScale = scale;
    source.axisInvert = invert;
    return source;
}

InputSource InputSource::Gamepad(GamepadButton b) {
    InputSource source;
    source.type = Type::GamepadButton;
    source.gamepadButton = b;
    return source;
}

InputSource InputSource::GamepadAxis(GamepadAxis axis, float scale, bool invert) {
    InputSource source;
    source.type = Type::GamepadAxis;
    source.gamepadAxis = axis;
    source.axisScale = scale;
    source.axisInvert = invert;
    return source;
}

bool InputSource::operator==(const InputSource& other) const {
    if (type != other.type) return false;

    switch (type) {
        case Type::Key:
            return key == other.key &&
                   requireShift == other.requireShift &&
                   requireCtrl == other.requireCtrl &&
                   requireAlt == other.requireAlt;
        case Type::MouseButton:
            return mouseButton == other.mouseButton;
        case Type::MouseAxis:
            return mouseAxis == other.mouseAxis;
        case Type::GamepadButton:
            return gamepadButton == other.gamepadButton;
        case Type::GamepadAxis:
            return gamepadAxis == other.gamepadAxis;
        default:
            return true;
    }
}

std::string InputSource::ToString() const {
    std::string result;

    if (requireCtrl) result += "Ctrl+";
    if (requireAlt) result += "Alt+";
    if (requireShift) result += "Shift+";

    switch (type) {
        case Type::Key:
            result += InputUtils::KeyCodeToString(key);
            break;
        case Type::MouseButton:
            result += InputUtils::MouseButtonToString(mouseButton);
            break;
        case Type::MouseAxis:
            result += "MouseAxis" + std::to_string(mouseAxis);
            break;
        case Type::GamepadButton:
            result += InputUtils::GamepadButtonToString(gamepadButton);
            break;
        case Type::GamepadAxis:
            result += InputUtils::GamepadAxisToString(gamepadAxis);
            break;
        default:
            result += "None";
            break;
    }

    return result;
}

// ============================================================================
// InputBinding Implementation
// ============================================================================

void InputBinding::AddSource(const InputSource& source) {
    sources.push_back(source);
}

void InputBinding::RemoveSource(size_t index) {
    if (index < sources.size()) {
        sources.erase(sources.begin() + index);
    }
}

void InputBinding::ClearSources() {
    sources.clear();
}

// ============================================================================
// InputContext Implementation
// ============================================================================

InputContext::InputContext(const std::string& name) : m_name(name) {
}

void InputContext::RegisterAction(const std::string& name, ActionType type) {
    InputAction action;
    action.name = name;
    action.type = type;
    m_actions[name] = action;
}

void InputContext::UnregisterAction(const std::string& name) {
    m_actions.erase(name);
}

InputAction* InputContext::GetAction(const std::string& name) {
    auto it = m_actions.find(name);
    return it != m_actions.end() ? &it->second : nullptr;
}

const InputAction* InputContext::GetAction(const std::string& name) const {
    auto it = m_actions.find(name);
    return it != m_actions.end() ? &it->second : nullptr;
}

void InputContext::SetBinding(const std::string& actionName, const InputBinding& binding) {
    auto* action = GetAction(actionName);
    if (action) {
        action->binding = binding;
    }
}

void InputContext::AddBinding(const std::string& actionName, const InputSource& source) {
    auto* action = GetAction(actionName);
    if (action) {
        action->binding.AddSource(source);
    }
}

InputBinding* InputContext::GetBinding(const std::string& actionName) {
    auto* action = GetAction(actionName);
    return action ? &action->binding : nullptr;
}

void InputContext::RegisterCompositeAxis2D(const std::string& name, const CompositeAxis2D& composite) {
    m_compositeAxes[name] = composite;
}

glm::vec2 InputContext::GetCompositeAxis2D(const std::string& name) const {
    auto it = m_compositeAxes.find(name);
    if (it == m_compositeAxes.end()) return glm::vec2(0.0f);

    const auto& composite = it->second;
    glm::vec2 result(0.0f);

    auto* posX = GetAction(composite.positiveX);
    auto* negX = GetAction(composite.negativeX);
    auto* posY = GetAction(composite.positiveY);
    auto* negY = GetAction(composite.negativeY);

    if (posX && posX->isPressed) result.x += 1.0f;
    if (negX && negX->isPressed) result.x -= 1.0f;
    if (posY && posY->isPressed) result.y += 1.0f;
    if (negY && negY->isPressed) result.y -= 1.0f;

    // Normalize if diagonal
    float length = glm::length(result);
    if (length > 1.0f) {
        result /= length;
    }

    return result;
}

void InputContext::Update(float /*deltaTime*/) {
    for (auto& [name, action] : m_actions) {
        action.wasPressed = action.isPressed;
    }
}

void InputContext::ProcessEvent(const InputEvent& /*event*/) {
    // Events are processed by InputManager
}

bool InputContext::IsActionPressed(const std::string& name) const {
    auto* action = GetAction(name);
    return action && action->isPressed;
}

bool InputContext::IsActionJustPressed(const std::string& name) const {
    auto* action = GetAction(name);
    return action && action->isPressed && !action->wasPressed;
}

bool InputContext::IsActionJustReleased(const std::string& name) const {
    auto* action = GetAction(name);
    return action && !action->isPressed && action->wasPressed;
}

float InputContext::GetActionValue(const std::string& name) const {
    auto* action = GetAction(name);
    return action ? action->value : 0.0f;
}

glm::vec2 InputContext::GetActionAxis2D(const std::string& name) const {
    auto* action = GetAction(name);
    return action ? action->axis2D : glm::vec2(0.0f);
}

// ============================================================================
// InputManager Implementation
// ============================================================================

InputManager::InputManager() {
    m_gamepads.resize(MAX_GAMEPADS);
    m_rumbleStates.resize(MAX_GAMEPADS);
}

InputManager::~InputManager() {
    Shutdown();
}

bool InputManager::Initialize() {
    // Reset all state
    std::memset(&m_keyboard, 0, sizeof(m_keyboard));
    std::memset(&m_mouse, 0, sizeof(m_mouse));

    for (auto& gamepad : m_gamepads) {
        gamepad = GamepadState{};
    }

    // Create default "gameplay" context
    CreateContext("gameplay");
    PushContext("gameplay");

    return true;
}

void InputManager::Shutdown() {
    m_contexts.clear();
    m_contextStack.clear();
}

void InputManager::Update(float deltaTime) {
    m_currentTime += deltaTime;

    // Store previous keyboard state
    std::memcpy(m_keyboard.prevKeys, m_keyboard.keys, sizeof(m_keyboard.prevKeys));

    // Store previous mouse state
    std::memcpy(m_mouse.prevButtons, m_mouse.buttons, sizeof(m_mouse.prevButtons));

    // Store previous gamepad state
    for (auto& gamepad : m_gamepads) {
        std::memcpy(gamepad.prevButtons, gamepad.buttons, sizeof(gamepad.prevButtons));
        std::memcpy(gamepad.prevAxes, gamepad.axes, sizeof(gamepad.prevAxes));
    }

    // Reset mouse delta
    m_mouse.delta = glm::vec2(0.0f);
    m_mouse.scrollDelta = 0.0f;

    // Update contexts
    for (auto& [name, context] : m_contexts) {
        if (context->IsEnabled()) {
            context->Update(deltaTime);
        }
    }

    // Update action states in active contexts
    for (auto it = m_contextStack.rbegin(); it != m_contextStack.rend(); ++it) {
        auto* context = GetContext(*it);
        if (context && context->IsEnabled()) {
            for (auto& [name, action] : const_cast<std::unordered_map<std::string, InputAction>&>(
                reinterpret_cast<const std::unordered_map<std::string, InputAction>&>(
                    *reinterpret_cast<const void*>(&context)))) {
                // This is a workaround - ideally InputContext would expose iteration
            }
        }
    }

    // Update rumble
    for (int i = 0; i < MAX_GAMEPADS; ++i) {
        auto& rumble = m_rumbleStates[i];
        if (rumble.duration > 0.0f) {
            rumble.elapsed += deltaTime;
            if (rumble.elapsed >= rumble.duration) {
                StopGamepadRumble(i);
            }
        }
    }

    // Update all action states
    for (auto& [contextName, context] : m_contexts) {
        if (!context->IsEnabled()) continue;

        // Get mutable access to actions through the context
        for (auto& [actionName, action] : const_cast<std::unordered_map<std::string, InputAction>&>(
            reinterpret_cast<const std::unordered_map<std::string, InputAction>&>(
                *reinterpret_cast<const void*>(&context)))) {
            UpdateActionState(action, deltaTime);
        }
    }
}

void InputManager::UpdateActionState(InputAction& action, float /*deltaTime*/) {
    action.wasPressed = action.isPressed;
    float newValue = EvaluateBinding(action.binding);

    // Apply deadzone and sensitivity
    if (action.type == ActionType::Button) {
        action.isPressed = newValue > 0.5f;
        action.value = action.isPressed ? 1.0f : 0.0f;
    } else {
        action.value = newValue * action.binding.sensitivity;
        action.isPressed = std::abs(action.value) > action.binding.deadzone;
    }

    // Trigger callbacks
    if (action.isPressed && !action.wasPressed) {
        action.pressedTime = m_currentTime;
        if (action.onPressed) action.onPressed();
    } else if (!action.isPressed && action.wasPressed) {
        action.releasedTime = m_currentTime;
        if (action.onReleased) action.onReleased();
    }

    if (action.onValueChanged && action.value != 0.0f) {
        action.onValueChanged(action.value);
    }
}

float InputManager::EvaluateBinding(const InputBinding& binding) const {
    float maxValue = 0.0f;

    for (const auto& source : binding.sources) {
        float value = EvaluateSource(source);
        if (std::abs(value) > std::abs(maxValue)) {
            maxValue = value;
        }
    }

    // Apply deadzone
    if (std::abs(maxValue) < binding.deadzone) {
        return 0.0f;
    }

    // Remap from deadzone to 1
    float sign = maxValue >= 0.0f ? 1.0f : -1.0f;
    maxValue = (std::abs(maxValue) - binding.deadzone) / (1.0f - binding.deadzone);

    if (binding.invertAxis) {
        maxValue = -maxValue;
    }

    return maxValue * sign * binding.scale;
}

float InputManager::EvaluateSource(const InputSource& source) const {
    if (!CheckModifiers(source)) {
        return 0.0f;
    }

    switch (source.type) {
        case InputSource::Type::Key: {
            int keyIndex = static_cast<int>(source.key);
            if (keyIndex >= 0 && keyIndex < 512) {
                return m_keyboard.keys[keyIndex] ? 1.0f : 0.0f;
            }
            break;
        }
        case InputSource::Type::MouseButton: {
            int buttonIndex = static_cast<int>(source.mouseButton);
            if (buttonIndex >= 0 && buttonIndex < 5) {
                return m_mouse.buttons[buttonIndex] ? 1.0f : 0.0f;
            }
            break;
        }
        case InputSource::Type::MouseAxis: {
            float value = 0.0f;
            switch (source.mouseAxis) {
                case 0: value = m_mouse.delta.x; break;
                case 1: value = m_mouse.delta.y; break;
                case 2: value = m_mouse.scrollDelta; break;
            }
            value *= source.axisScale * m_mouseSensitivity;
            if (source.axisInvert) value = -value;
            return value;
        }
        case InputSource::Type::GamepadButton: {
            int buttonIndex = static_cast<int>(source.gamepadButton);
            for (const auto& gamepad : m_gamepads) {
                if (gamepad.connected && buttonIndex < 16) {
                    if (gamepad.buttons[buttonIndex]) return 1.0f;
                }
            }
            break;
        }
        case InputSource::Type::GamepadAxis: {
            int axisIndex = static_cast<int>(source.gamepadAxis);
            for (const auto& gamepad : m_gamepads) {
                if (gamepad.connected && axisIndex < 6) {
                    float value = gamepad.axes[axisIndex] * source.axisScale;
                    if (source.axisInvert) value = -value;
                    return value;
                }
            }
            break;
        }
        default:
            break;
    }

    return 0.0f;
}

bool InputManager::CheckModifiers(const InputSource& source) const {
    if (source.requireShift && !IsShiftDown()) return false;
    if (source.requireCtrl && !IsCtrlDown()) return false;
    if (source.requireAlt && !IsAltDown()) return false;
    return true;
}

void InputManager::DispatchEvent(const InputEvent& event) {
    // Dispatch to contexts in reverse stack order (top first)
    for (auto it = m_contextStack.rbegin(); it != m_contextStack.rend(); ++it) {
        auto* context = GetContext(*it);
        if (context && context->IsEnabled()) {
            context->ProcessEvent(event);
            if (context->ConsumesInput()) {
                break;  // Stop propagation
            }
        }
    }
}

// Event handlers
void InputManager::OnKeyEvent(int key, int scanCode, bool isDown, bool isRepeat) {
    if (key >= 0 && key < 512) {
        m_keyboard.keys[key] = isDown;
    }

    KeyEvent event;
    event.key = static_cast<KeyCode>(key);
    event.scanCode = scanCode;
    event.isDown = isDown;
    event.isRepeat = isRepeat;
    event.shift = IsShiftDown();
    event.ctrl = IsCtrlDown();
    event.alt = IsAltDown();
    event.super = IsSuperDown();

    // Input rebinding
    if (m_listeningForInput && isDown && !isRepeat) {
        InputSource source = InputSource::Key(static_cast<KeyCode>(key),
                                               event.shift, event.ctrl, event.alt);
        if (m_inputListenerCallback) {
            m_inputListenerCallback(source);
        }
        m_listeningForInput = false;
        return;
    }

    DispatchEvent(event);
}

void InputManager::OnMouseButton(int button, bool isDown) {
    if (button >= 0 && button < 5) {
        m_mouse.buttons[button] = isDown;
    }

    MouseButtonEvent event;
    event.button = static_cast<MouseButton>(button);
    event.isDown = isDown;
    event.position = m_mouse.position;

    // Input rebinding
    if (m_listeningForInput && isDown) {
        InputSource source = InputSource::Mouse(static_cast<MouseButton>(button));
        if (m_inputListenerCallback) {
            m_inputListenerCallback(source);
        }
        m_listeningForInput = false;
        return;
    }

    DispatchEvent(event);
}

void InputManager::OnMouseMove(float x, float y) {
    m_mouse.previousPosition = m_mouse.position;
    m_mouse.position = glm::vec2(x, y);
    m_mouse.delta = m_mouse.position - m_mouse.previousPosition;

    MouseMoveEvent event;
    event.position = m_mouse.position;
    event.delta = m_mouse.delta;

    DispatchEvent(event);
}

void InputManager::OnMouseScroll(float delta) {
    m_mouse.scrollDelta = delta;

    MouseScrollEvent event;
    event.delta = delta;
    event.position = m_mouse.position;

    DispatchEvent(event);
}

void InputManager::OnTextInput(uint32_t codepoint) {
    if (!m_textInputActive) return;

    TextInputEvent event;
    event.codepoint = codepoint;

    // Convert codepoint to UTF-8 string
    if (codepoint < 0x80) {
        event.text = static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
        event.text += static_cast<char>(0xC0 | (codepoint >> 6));
        event.text += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
        event.text += static_cast<char>(0xE0 | (codepoint >> 12));
        event.text += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        event.text += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        event.text += static_cast<char>(0xF0 | (codepoint >> 18));
        event.text += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        event.text += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        event.text += static_cast<char>(0x80 | (codepoint & 0x3F));
    }

    if (m_textInputCallback) {
        m_textInputCallback(event.text);
    }

    DispatchEvent(event);
}

void InputManager::OnGamepadConnect(int index) {
    if (index >= 0 && index < MAX_GAMEPADS) {
        m_gamepads[index].connected = true;
    }
}

void InputManager::OnGamepadDisconnect(int index) {
    if (index >= 0 && index < MAX_GAMEPADS) {
        m_gamepads[index] = GamepadState{};
    }
}

void InputManager::OnGamepadButton(int index, int button, bool isDown) {
    if (index >= 0 && index < MAX_GAMEPADS && button >= 0 && button < 16) {
        m_gamepads[index].buttons[button] = isDown;

        GamepadButtonEvent event;
        event.gamepadIndex = index;
        event.button = static_cast<GamepadButton>(button);
        event.isDown = isDown;

        // Input rebinding
        if (m_listeningForInput && isDown) {
            InputSource source = InputSource::Gamepad(static_cast<GamepadButton>(button));
            if (m_inputListenerCallback) {
                m_inputListenerCallback(source);
            }
            m_listeningForInput = false;
            return;
        }

        DispatchEvent(event);
    }
}

void InputManager::OnGamepadAxis(int index, int axis, float value) {
    if (index >= 0 && index < MAX_GAMEPADS && axis >= 0 && axis < 6) {
        m_gamepads[index].axes[axis] = value;

        GamepadAxisEvent event;
        event.gamepadIndex = index;
        event.axis = static_cast<GamepadAxis>(axis);
        event.value = value;

        // Input rebinding (only for significant movement)
        if (m_listeningForInput && std::abs(value) > 0.8f) {
            InputSource source = InputSource::GamepadAxis(static_cast<GamepadAxis>(axis),
                                                           value > 0 ? 1.0f : -1.0f, false);
            if (m_inputListenerCallback) {
                m_inputListenerCallback(source);
            }
            m_listeningForInput = false;
            return;
        }

        DispatchEvent(event);
    }
}

// Context management
InputContext* InputManager::CreateContext(const std::string& name) {
    auto context = std::make_unique<InputContext>(name);
    auto* ptr = context.get();
    m_contexts[name] = std::move(context);
    return ptr;
}

void InputManager::DestroyContext(const std::string& name) {
    m_contexts.erase(name);

    // Remove from stack
    m_contextStack.erase(
        std::remove(m_contextStack.begin(), m_contextStack.end(), name),
        m_contextStack.end());
}

InputContext* InputManager::GetContext(const std::string& name) {
    auto it = m_contexts.find(name);
    return it != m_contexts.end() ? it->second.get() : nullptr;
}

void InputManager::PushContext(const std::string& name) {
    if (m_contexts.find(name) != m_contexts.end()) {
        // Remove if already in stack
        m_contextStack.erase(
            std::remove(m_contextStack.begin(), m_contextStack.end(), name),
            m_contextStack.end());
        m_contextStack.push_back(name);
    }
}

void InputManager::PopContext() {
    if (!m_contextStack.empty()) {
        m_contextStack.pop_back();
    }
}

void InputManager::SetActiveContext(const std::string& name) {
    m_contextStack.clear();
    if (m_contexts.find(name) != m_contexts.end()) {
        m_contextStack.push_back(name);
    }
}

InputContext* InputManager::GetActiveContext() {
    if (m_contextStack.empty()) return nullptr;
    return GetContext(m_contextStack.back());
}

// Global action queries
bool InputManager::IsActionPressed(const std::string& name) const {
    for (auto it = m_contextStack.rbegin(); it != m_contextStack.rend(); ++it) {
        auto ctxIt = m_contexts.find(*it);
        if (ctxIt != m_contexts.end() && ctxIt->second->IsEnabled()) {
            if (ctxIt->second->IsActionPressed(name)) return true;
            if (ctxIt->second->ConsumesInput()) break;
        }
    }
    return false;
}

bool InputManager::IsActionJustPressed(const std::string& name) const {
    for (auto it = m_contextStack.rbegin(); it != m_contextStack.rend(); ++it) {
        auto ctxIt = m_contexts.find(*it);
        if (ctxIt != m_contexts.end() && ctxIt->second->IsEnabled()) {
            if (ctxIt->second->IsActionJustPressed(name)) return true;
            if (ctxIt->second->ConsumesInput()) break;
        }
    }
    return false;
}

bool InputManager::IsActionJustReleased(const std::string& name) const {
    for (auto it = m_contextStack.rbegin(); it != m_contextStack.rend(); ++it) {
        auto ctxIt = m_contexts.find(*it);
        if (ctxIt != m_contexts.end() && ctxIt->second->IsEnabled()) {
            if (ctxIt->second->IsActionJustReleased(name)) return true;
            if (ctxIt->second->ConsumesInput()) break;
        }
    }
    return false;
}

float InputManager::GetActionValue(const std::string& name) const {
    for (auto it = m_contextStack.rbegin(); it != m_contextStack.rend(); ++it) {
        auto ctxIt = m_contexts.find(*it);
        if (ctxIt != m_contexts.end() && ctxIt->second->IsEnabled()) {
            float value = ctxIt->second->GetActionValue(name);
            if (value != 0.0f) return value;
            if (ctxIt->second->ConsumesInput()) break;
        }
    }
    return 0.0f;
}

glm::vec2 InputManager::GetAxis2D(const std::string& name) const {
    for (auto it = m_contextStack.rbegin(); it != m_contextStack.rend(); ++it) {
        auto ctxIt = m_contexts.find(*it);
        if (ctxIt != m_contexts.end() && ctxIt->second->IsEnabled()) {
            glm::vec2 axis = ctxIt->second->GetCompositeAxis2D(name);
            if (axis != glm::vec2(0.0f)) return axis;
            axis = ctxIt->second->GetActionAxis2D(name);
            if (axis != glm::vec2(0.0f)) return axis;
            if (ctxIt->second->ConsumesInput()) break;
        }
    }
    return glm::vec2(0.0f);
}

// Raw state queries
const GamepadState& InputManager::GetGamepadState(int index) const {
    static GamepadState emptyState;
    if (index >= 0 && index < MAX_GAMEPADS) {
        return m_gamepads[index];
    }
    return emptyState;
}

int InputManager::GetConnectedGamepadCount() const {
    int count = 0;
    for (const auto& gamepad : m_gamepads) {
        if (gamepad.connected) count++;
    }
    return count;
}

bool InputManager::IsKeyDown(KeyCode key) const {
    int index = static_cast<int>(key);
    return index >= 0 && index < 512 && m_keyboard.keys[index];
}

bool InputManager::IsKeyJustPressed(KeyCode key) const {
    int index = static_cast<int>(key);
    return index >= 0 && index < 512 && m_keyboard.keys[index] && !m_keyboard.prevKeys[index];
}

bool InputManager::IsKeyJustReleased(KeyCode key) const {
    int index = static_cast<int>(key);
    return index >= 0 && index < 512 && !m_keyboard.keys[index] && m_keyboard.prevKeys[index];
}

bool InputManager::IsMouseButtonDown(MouseButton button) const {
    int index = static_cast<int>(button);
    return index >= 0 && index < 5 && m_mouse.buttons[index];
}

bool InputManager::IsMouseButtonJustPressed(MouseButton button) const {
    int index = static_cast<int>(button);
    return index >= 0 && index < 5 && m_mouse.buttons[index] && !m_mouse.prevButtons[index];
}

bool InputManager::IsMouseButtonJustReleased(MouseButton button) const {
    int index = static_cast<int>(button);
    return index >= 0 && index < 5 && !m_mouse.buttons[index] && m_mouse.prevButtons[index];
}

bool InputManager::IsGamepadButtonDown(int index, GamepadButton button) const {
    if (index < 0 || index >= MAX_GAMEPADS) return false;
    int btnIdx = static_cast<int>(button);
    return btnIdx >= 0 && btnIdx < 16 && m_gamepads[index].buttons[btnIdx];
}

float InputManager::GetGamepadAxis(int index, GamepadAxis axis) const {
    if (index < 0 || index >= MAX_GAMEPADS) return 0.0f;
    int axisIdx = static_cast<int>(axis);
    return axisIdx >= 0 && axisIdx < 6 ? m_gamepads[index].axes[axisIdx] : 0.0f;
}

bool InputManager::IsShiftDown() const {
    return IsKeyDown(KeyCode::LeftShift) || IsKeyDown(KeyCode::RightShift);
}

bool InputManager::IsCtrlDown() const {
    return IsKeyDown(KeyCode::LeftCtrl) || IsKeyDown(KeyCode::RightCtrl);
}

bool InputManager::IsAltDown() const {
    return IsKeyDown(KeyCode::LeftAlt) || IsKeyDown(KeyCode::RightAlt);
}

bool InputManager::IsSuperDown() const {
    return IsKeyDown(KeyCode::LeftSuper) || IsKeyDown(KeyCode::RightSuper);
}

void InputManager::SetMouseRelativeMode(bool enabled) {
    m_mouse.isRelativeMode = enabled;
    // Platform-specific mouse capture would be called here
}

void InputManager::SetGamepadRumble(int index, float leftMotor, float rightMotor, float duration) {
    if (index >= 0 && index < MAX_GAMEPADS && m_gamepads[index].connected) {
        m_gamepads[index].rumbleLeft = leftMotor;
        m_gamepads[index].rumbleRight = rightMotor;
        m_rumbleStates[index].duration = duration;
        m_rumbleStates[index].elapsed = 0.0f;
        // Platform-specific rumble API would be called here
    }
}

void InputManager::StopGamepadRumble(int index) {
    if (index >= 0 && index < MAX_GAMEPADS) {
        m_gamepads[index].rumbleLeft = 0.0f;
        m_gamepads[index].rumbleRight = 0.0f;
        m_rumbleStates[index].duration = 0.0f;
        // Platform-specific rumble stop would be called here
    }
}

void InputManager::StartListeningForInput(std::function<void(const InputSource&)> callback) {
    m_listeningForInput = true;
    m_inputListenerCallback = callback;
}

void InputManager::StopListeningForInput() {
    m_listeningForInput = false;
    m_inputListenerCallback = nullptr;
}

bool InputManager::LoadBindings(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    try {
        json j;
        file >> j;

        for (auto& [contextName, contextData] : j.items()) {
            auto* context = GetContext(contextName);
            if (!context) {
                context = CreateContext(contextName);
            }

            for (auto& [actionName, actionData] : contextData.items()) {
                ActionType type = ActionType::Button;
                if (actionData.contains("type")) {
                    std::string typeStr = actionData["type"];
                    if (typeStr == "axis") type = ActionType::Axis;
                    else if (typeStr == "axis2d") type = ActionType::Axis2D;
                }

                context->RegisterAction(actionName, type);

                if (actionData.contains("bindings")) {
                    InputBinding binding;
                    for (auto& bindingData : actionData["bindings"]) {
                        InputSource source;
                        std::string sourceType = bindingData["type"];

                        if (sourceType == "key") {
                            source = InputSource::Key(
                                InputUtils::StringToKeyCode(bindingData["key"]),
                                bindingData.value("shift", false),
                                bindingData.value("ctrl", false),
                                bindingData.value("alt", false)
                            );
                        } else if (sourceType == "mouse_button") {
                            source = InputSource::Mouse(
                                InputUtils::StringToMouseButton(bindingData["button"])
                            );
                        } else if (sourceType == "gamepad_button") {
                            source = InputSource::Gamepad(
                                InputUtils::StringToGamepadButton(bindingData["button"])
                            );
                        } else if (sourceType == "gamepad_axis") {
                            source = InputSource::GamepadAxis(
                                InputUtils::StringToGamepadAxis(bindingData["axis"]),
                                bindingData.value("scale", 1.0f),
                                bindingData.value("invert", false)
                            );
                        }

                        binding.AddSource(source);
                    }

                    binding.deadzone = actionData.value("deadzone", 0.15f);
                    binding.sensitivity = actionData.value("sensitivity", 1.0f);
                    context->SetBinding(actionName, binding);
                }
            }
        }

        return true;
    } catch (...) {
        return false;
    }
}

bool InputManager::SaveBindings(const std::string& path) {
    json j;

    for (const auto& [contextName, context] : m_contexts) {
        json contextData;
        // Would need to iterate through actions in context
        // This is a simplified version
        j[contextName] = contextData;
    }

    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << j.dump(2);
    return true;
}

void InputManager::ResetToDefaults() {
    // Reset all contexts to default bindings
    for (auto& [name, context] : m_contexts) {
        // Clear and re-setup bindings
    }
}

void InputManager::StartTextInput() {
    m_textInputActive = true;
    // Platform-specific text input mode would be enabled here
}

void InputManager::StopTextInput() {
    m_textInputActive = false;
    // Platform-specific text input mode would be disabled here
}

void InputManager::SetTextInputCallback(std::function<void(const std::string&)> callback) {
    m_textInputCallback = callback;
}

// ============================================================================
// InputUtils Implementation
// ============================================================================

namespace InputUtils {

std::string KeyCodeToString(KeyCode key) {
    switch (key) {
        // Letters
        case KeyCode::A: return "A"; case KeyCode::B: return "B"; case KeyCode::C: return "C";
        case KeyCode::D: return "D"; case KeyCode::E: return "E"; case KeyCode::F: return "F";
        case KeyCode::G: return "G"; case KeyCode::H: return "H"; case KeyCode::I: return "I";
        case KeyCode::J: return "J"; case KeyCode::K: return "K"; case KeyCode::L: return "L";
        case KeyCode::M: return "M"; case KeyCode::N: return "N"; case KeyCode::O: return "O";
        case KeyCode::P: return "P"; case KeyCode::Q: return "Q"; case KeyCode::R: return "R";
        case KeyCode::S: return "S"; case KeyCode::T: return "T"; case KeyCode::U: return "U";
        case KeyCode::V: return "V"; case KeyCode::W: return "W"; case KeyCode::X: return "X";
        case KeyCode::Y: return "Y"; case KeyCode::Z: return "Z";

        // Numbers
        case KeyCode::Num0: return "0"; case KeyCode::Num1: return "1"; case KeyCode::Num2: return "2";
        case KeyCode::Num3: return "3"; case KeyCode::Num4: return "4"; case KeyCode::Num5: return "5";
        case KeyCode::Num6: return "6"; case KeyCode::Num7: return "7"; case KeyCode::Num8: return "8";
        case KeyCode::Num9: return "9";

        // Function keys
        case KeyCode::F1: return "F1"; case KeyCode::F2: return "F2"; case KeyCode::F3: return "F3";
        case KeyCode::F4: return "F4"; case KeyCode::F5: return "F5"; case KeyCode::F6: return "F6";
        case KeyCode::F7: return "F7"; case KeyCode::F8: return "F8"; case KeyCode::F9: return "F9";
        case KeyCode::F10: return "F10"; case KeyCode::F11: return "F11"; case KeyCode::F12: return "F12";

        // Special
        case KeyCode::Escape: return "Escape"; case KeyCode::Enter: return "Enter";
        case KeyCode::Tab: return "Tab"; case KeyCode::Backspace: return "Backspace";
        case KeyCode::Insert: return "Insert"; case KeyCode::Delete: return "Delete";
        case KeyCode::Home: return "Home"; case KeyCode::End: return "End";
        case KeyCode::PageUp: return "PageUp"; case KeyCode::PageDown: return "PageDown";
        case KeyCode::Up: return "Up"; case KeyCode::Down: return "Down";
        case KeyCode::Left: return "Left"; case KeyCode::Right: return "Right";
        case KeyCode::Space: return "Space";

        // Modifiers
        case KeyCode::LeftShift: return "LeftShift"; case KeyCode::RightShift: return "RightShift";
        case KeyCode::LeftCtrl: return "LeftCtrl"; case KeyCode::RightCtrl: return "RightCtrl";
        case KeyCode::LeftAlt: return "LeftAlt"; case KeyCode::RightAlt: return "RightAlt";

        default: return "Unknown";
    }
}

KeyCode StringToKeyCode(const std::string& str) {
    if (str == "A") return KeyCode::A; if (str == "B") return KeyCode::B;
    if (str == "C") return KeyCode::C; if (str == "D") return KeyCode::D;
    if (str == "E") return KeyCode::E; if (str == "F") return KeyCode::F;
    if (str == "G") return KeyCode::G; if (str == "H") return KeyCode::H;
    if (str == "I") return KeyCode::I; if (str == "J") return KeyCode::J;
    if (str == "K") return KeyCode::K; if (str == "L") return KeyCode::L;
    if (str == "M") return KeyCode::M; if (str == "N") return KeyCode::N;
    if (str == "O") return KeyCode::O; if (str == "P") return KeyCode::P;
    if (str == "Q") return KeyCode::Q; if (str == "R") return KeyCode::R;
    if (str == "S") return KeyCode::S; if (str == "T") return KeyCode::T;
    if (str == "U") return KeyCode::U; if (str == "V") return KeyCode::V;
    if (str == "W") return KeyCode::W; if (str == "X") return KeyCode::X;
    if (str == "Y") return KeyCode::Y; if (str == "Z") return KeyCode::Z;
    if (str == "Space") return KeyCode::Space;
    if (str == "Escape") return KeyCode::Escape;
    if (str == "Enter") return KeyCode::Enter;
    if (str == "Tab") return KeyCode::Tab;
    if (str == "Backspace") return KeyCode::Backspace;
    // ... more conversions
    return KeyCode::Unknown;
}

std::string MouseButtonToString(MouseButton button) {
    switch (button) {
        case MouseButton::Left: return "MouseLeft";
        case MouseButton::Right: return "MouseRight";
        case MouseButton::Middle: return "MouseMiddle";
        case MouseButton::Button4: return "Mouse4";
        case MouseButton::Button5: return "Mouse5";
        default: return "Unknown";
    }
}

MouseButton StringToMouseButton(const std::string& str) {
    if (str == "MouseLeft" || str == "Left") return MouseButton::Left;
    if (str == "MouseRight" || str == "Right") return MouseButton::Right;
    if (str == "MouseMiddle" || str == "Middle") return MouseButton::Middle;
    if (str == "Mouse4") return MouseButton::Button4;
    if (str == "Mouse5") return MouseButton::Button5;
    return MouseButton::Left;
}

std::string GamepadButtonToString(GamepadButton button) {
    switch (button) {
        case GamepadButton::A: return "A";
        case GamepadButton::B: return "B";
        case GamepadButton::X: return "X";
        case GamepadButton::Y: return "Y";
        case GamepadButton::LeftBumper: return "LB";
        case GamepadButton::RightBumper: return "RB";
        case GamepadButton::Back: return "Back";
        case GamepadButton::Start: return "Start";
        case GamepadButton::Guide: return "Guide";
        case GamepadButton::LeftStick: return "LS";
        case GamepadButton::RightStick: return "RS";
        case GamepadButton::DPadUp: return "DPadUp";
        case GamepadButton::DPadDown: return "DPadDown";
        case GamepadButton::DPadLeft: return "DPadLeft";
        case GamepadButton::DPadRight: return "DPadRight";
        default: return "Unknown";
    }
}

GamepadButton StringToGamepadButton(const std::string& str) {
    if (str == "A") return GamepadButton::A;
    if (str == "B") return GamepadButton::B;
    if (str == "X") return GamepadButton::X;
    if (str == "Y") return GamepadButton::Y;
    if (str == "LB") return GamepadButton::LeftBumper;
    if (str == "RB") return GamepadButton::RightBumper;
    if (str == "Back") return GamepadButton::Back;
    if (str == "Start") return GamepadButton::Start;
    if (str == "Guide") return GamepadButton::Guide;
    if (str == "LS") return GamepadButton::LeftStick;
    if (str == "RS") return GamepadButton::RightStick;
    if (str == "DPadUp") return GamepadButton::DPadUp;
    if (str == "DPadDown") return GamepadButton::DPadDown;
    if (str == "DPadLeft") return GamepadButton::DPadLeft;
    if (str == "DPadRight") return GamepadButton::DPadRight;
    return GamepadButton::A;
}

std::string GamepadAxisToString(GamepadAxis axis) {
    switch (axis) {
        case GamepadAxis::LeftX: return "LeftX";
        case GamepadAxis::LeftY: return "LeftY";
        case GamepadAxis::RightX: return "RightX";
        case GamepadAxis::RightY: return "RightY";
        case GamepadAxis::LeftTrigger: return "LT";
        case GamepadAxis::RightTrigger: return "RT";
        default: return "Unknown";
    }
}

GamepadAxis StringToGamepadAxis(const std::string& str) {
    if (str == "LeftX") return GamepadAxis::LeftX;
    if (str == "LeftY") return GamepadAxis::LeftY;
    if (str == "RightX") return GamepadAxis::RightX;
    if (str == "RightY") return GamepadAxis::RightY;
    if (str == "LT") return GamepadAxis::LeftTrigger;
    if (str == "RT") return GamepadAxis::RightTrigger;
    return GamepadAxis::LeftX;
}

float ApplyDeadzone(float value, float deadzone) {
    if (std::abs(value) < deadzone) return 0.0f;

    float sign = value >= 0.0f ? 1.0f : -1.0f;
    return sign * (std::abs(value) - deadzone) / (1.0f - deadzone);
}

glm::vec2 ApplyRadialDeadzone(const glm::vec2& value, float deadzone) {
    float length = glm::length(value);
    if (length < deadzone) return glm::vec2(0.0f);

    glm::vec2 normalized = value / length;
    float remapped = (length - deadzone) / (1.0f - deadzone);
    return normalized * remapped;
}

float SmoothInput(float current, float target, float smoothing, float deltaTime) {
    if (smoothing <= 0.0f) return target;
    return current + (target - current) * std::min(1.0f, deltaTime / smoothing);
}

} // namespace InputUtils

// ============================================================================
// DefaultBindings Implementation
// ============================================================================

namespace DefaultBindings {

void SetupFPSControls(InputContext* context) {
    if (!context) return;

    // Movement
    context->RegisterAction("MoveForward", ActionType::Button);
    context->AddBinding("MoveForward", InputSource::Key(KeyCode::W));

    context->RegisterAction("MoveBackward", ActionType::Button);
    context->AddBinding("MoveBackward", InputSource::Key(KeyCode::S));

    context->RegisterAction("MoveLeft", ActionType::Button);
    context->AddBinding("MoveLeft", InputSource::Key(KeyCode::A));

    context->RegisterAction("MoveRight", ActionType::Button);
    context->AddBinding("MoveRight", InputSource::Key(KeyCode::D));

    // Composite for WASD movement
    context->RegisterCompositeAxis2D("Move", {
        "MoveRight", "MoveLeft", "MoveForward", "MoveBackward"
    });

    // Looking (mouse + gamepad)
    context->RegisterAction("LookX", ActionType::Axis);
    context->AddBinding("LookX", InputSource::MouseAxis(0, 0.1f));
    context->AddBinding("LookX", InputSource::GamepadAxis(GamepadAxis::RightX));

    context->RegisterAction("LookY", ActionType::Axis);
    context->AddBinding("LookY", InputSource::MouseAxis(1, 0.1f));
    context->AddBinding("LookY", InputSource::GamepadAxis(GamepadAxis::RightY));

    // Actions
    context->RegisterAction("Jump", ActionType::Button);
    context->AddBinding("Jump", InputSource::Key(KeyCode::Space));
    context->AddBinding("Jump", InputSource::Gamepad(GamepadButton::A));

    context->RegisterAction("Crouch", ActionType::Button);
    context->AddBinding("Crouch", InputSource::Key(KeyCode::LeftCtrl));
    context->AddBinding("Crouch", InputSource::Gamepad(GamepadButton::B));

    context->RegisterAction("Sprint", ActionType::Button);
    context->AddBinding("Sprint", InputSource::Key(KeyCode::LeftShift));
    context->AddBinding("Sprint", InputSource::Gamepad(GamepadButton::LeftStick));

    context->RegisterAction("Fire", ActionType::Button);
    context->AddBinding("Fire", InputSource::Mouse(MouseButton::Left));
    context->AddBinding("Fire", InputSource::GamepadAxis(GamepadAxis::RightTrigger, 1.0f));

    context->RegisterAction("Aim", ActionType::Button);
    context->AddBinding("Aim", InputSource::Mouse(MouseButton::Right));
    context->AddBinding("Aim", InputSource::GamepadAxis(GamepadAxis::LeftTrigger, 1.0f));

    context->RegisterAction("Reload", ActionType::Button);
    context->AddBinding("Reload", InputSource::Key(KeyCode::R));
    context->AddBinding("Reload", InputSource::Gamepad(GamepadButton::X));

    context->RegisterAction("Interact", ActionType::Button);
    context->AddBinding("Interact", InputSource::Key(KeyCode::E));
    context->AddBinding("Interact", InputSource::Gamepad(GamepadButton::Y));
}

void SetupThirdPersonControls(InputContext* context) {
    if (!context) return;

    // Similar to FPS but with camera controls
    SetupFPSControls(context);

    // Additional camera controls
    context->RegisterAction("CameraZoom", ActionType::Axis);
    context->AddBinding("CameraZoom", InputSource::MouseAxis(2, 1.0f));

    context->RegisterAction("LockOn", ActionType::Button);
    context->AddBinding("LockOn", InputSource::Key(KeyCode::Tab));
    context->AddBinding("LockOn", InputSource::Gamepad(GamepadButton::RightStick));
}

void SetupMenuControls(InputContext* context) {
    if (!context) return;

    context->RegisterAction("Navigate", ActionType::Axis2D);
    context->AddBinding("Navigate", InputSource::GamepadAxis(GamepadAxis::LeftX));
    context->AddBinding("Navigate", InputSource::GamepadAxis(GamepadAxis::LeftY));

    context->RegisterAction("Select", ActionType::Button);
    context->AddBinding("Select", InputSource::Key(KeyCode::Enter));
    context->AddBinding("Select", InputSource::Gamepad(GamepadButton::A));

    context->RegisterAction("Back", ActionType::Button);
    context->AddBinding("Back", InputSource::Key(KeyCode::Escape));
    context->AddBinding("Back", InputSource::Gamepad(GamepadButton::B));

    context->RegisterAction("TabLeft", ActionType::Button);
    context->AddBinding("TabLeft", InputSource::Key(KeyCode::Q));
    context->AddBinding("TabLeft", InputSource::Gamepad(GamepadButton::LeftBumper));

    context->RegisterAction("TabRight", ActionType::Button);
    context->AddBinding("TabRight", InputSource::Key(KeyCode::E));
    context->AddBinding("TabRight", InputSource::Gamepad(GamepadButton::RightBumper));
}

void SetupVehicleControls(InputContext* context) {
    if (!context) return;

    context->RegisterAction("Accelerate", ActionType::Axis);
    context->AddBinding("Accelerate", InputSource::Key(KeyCode::W));
    context->AddBinding("Accelerate", InputSource::GamepadAxis(GamepadAxis::RightTrigger));

    context->RegisterAction("Brake", ActionType::Axis);
    context->AddBinding("Brake", InputSource::Key(KeyCode::S));
    context->AddBinding("Brake", InputSource::GamepadAxis(GamepadAxis::LeftTrigger));

    context->RegisterAction("Steer", ActionType::Axis);
    context->AddBinding("Steer", InputSource::Key(KeyCode::A));  // Would need composite
    context->AddBinding("Steer", InputSource::GamepadAxis(GamepadAxis::LeftX));

    context->RegisterAction("Handbrake", ActionType::Button);
    context->AddBinding("Handbrake", InputSource::Key(KeyCode::Space));
    context->AddBinding("Handbrake", InputSource::Gamepad(GamepadButton::A));

    context->RegisterAction("Horn", ActionType::Button);
    context->AddBinding("Horn", InputSource::Key(KeyCode::H));
    context->AddBinding("Horn", InputSource::Gamepad(GamepadButton::LeftStick));

    context->RegisterAction("ExitVehicle", ActionType::Button);
    context->AddBinding("ExitVehicle", InputSource::Key(KeyCode::F));
    context->AddBinding("ExitVehicle", InputSource::Gamepad(GamepadButton::Y));
}

} // namespace DefaultBindings

} // namespace Cortex::Input
