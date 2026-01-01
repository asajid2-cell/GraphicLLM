#pragma once

// InputManager.h
// Unified input handling for keyboard, mouse, and gamepad.
// Supports rebindable controls, input contexts, and action mapping.

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <variant>

namespace Cortex::Input {

// Forward declarations
class InputDevice;
class InputContext;

// ============================================================================
// Input Source Types
// ============================================================================

// Key codes (subset of common keys)
enum class KeyCode : uint16_t {
    Unknown = 0,

    // Letters
    A = 'A', B = 'B', C = 'C', D = 'D', E = 'E', F = 'F', G = 'G', H = 'H',
    I = 'I', J = 'J', K = 'K', L = 'L', M = 'M', N = 'N', O = 'O', P = 'P',
    Q = 'Q', R = 'R', S = 'S', T = 'T', U = 'U', V = 'V', W = 'W', X = 'X',
    Y = 'Y', Z = 'Z',

    // Numbers
    Num0 = '0', Num1 = '1', Num2 = '2', Num3 = '3', Num4 = '4',
    Num5 = '5', Num6 = '6', Num7 = '7', Num8 = '8', Num9 = '9',

    // Function keys
    F1 = 256, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    // Special keys
    Escape = 300, Enter, Tab, Backspace, Insert, Delete, Home, End,
    PageUp, PageDown, PrintScreen, Pause, CapsLock, ScrollLock, NumLock,

    // Arrow keys
    Up, Down, Left, Right,

    // Modifiers
    LeftShift, RightShift, LeftCtrl, RightCtrl, LeftAlt, RightAlt,
    LeftSuper, RightSuper, Menu,

    // Punctuation
    Space = 400, Apostrophe, Comma, Minus, Period, Slash, Semicolon,
    Equal, LeftBracket, Backslash, RightBracket, GraveAccent,

    // Numpad
    Numpad0 = 450, Numpad1, Numpad2, Numpad3, Numpad4,
    Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
    NumpadDecimal, NumpadDivide, NumpadMultiply, NumpadMinus,
    NumpadPlus, NumpadEnter, NumpadEqual
};

// Mouse buttons
enum class MouseButton : uint8_t {
    Left = 0,
    Right = 1,
    Middle = 2,
    Button4 = 3,
    Button5 = 4
};

// Gamepad buttons (Xbox layout)
enum class GamepadButton : uint8_t {
    A = 0,          // Cross on PlayStation
    B,              // Circle
    X,              // Square
    Y,              // Triangle
    LeftBumper,     // L1
    RightBumper,    // R1
    Back,           // Select/Share
    Start,          // Options
    Guide,          // Home/PS
    LeftStick,      // L3
    RightStick,     // R3
    DPadUp,
    DPadDown,
    DPadLeft,
    DPadRight
};

// Gamepad axes
enum class GamepadAxis : uint8_t {
    LeftX = 0,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,    // L2
    RightTrigger    // R2
};

// Input source (key, button, or axis)
struct InputSource {
    enum class Type : uint8_t {
        None,
        Key,
        MouseButton,
        MouseAxis,
        GamepadButton,
        GamepadAxis
    };

    Type type = Type::None;

    union {
        KeyCode key;
        MouseButton mouseButton;
        uint8_t mouseAxis;      // 0=X, 1=Y, 2=Wheel
        GamepadButton gamepadButton;
        GamepadAxis gamepadAxis;
    };

    // Modifiers
    bool requireShift = false;
    bool requireCtrl = false;
    bool requireAlt = false;

    // Axis configuration
    float axisScale = 1.0f;     // Multiply axis value
    bool axisInvert = false;

    InputSource() : type(Type::None), key(KeyCode::Unknown) {}

    static InputSource Key(KeyCode k, bool shift = false, bool ctrl = false, bool alt = false);
    static InputSource Mouse(MouseButton b);
    static InputSource MouseAxis(uint8_t axis, float scale = 1.0f, bool invert = false);
    static InputSource Gamepad(GamepadButton b);
    static InputSource GamepadAxis(GamepadAxis axis, float scale = 1.0f, bool invert = false);

    bool operator==(const InputSource& other) const;
    std::string ToString() const;
};

// ============================================================================
// Input Binding
// ============================================================================

struct InputBinding {
    std::vector<InputSource> sources;  // Multiple bindings per action
    float deadzone = 0.15f;
    bool invertAxis = false;
    float sensitivity = 1.0f;
    float scale = 1.0f;

    void AddSource(const InputSource& source);
    void RemoveSource(size_t index);
    void ClearSources();
};

// ============================================================================
// Input Action
// ============================================================================

enum class ActionType : uint8_t {
    Button,     // Binary on/off
    Axis,       // Single axis -1 to 1
    Axis2D      // Two axes (e.g., movement)
};

struct InputAction {
    std::string name;
    ActionType type = ActionType::Button;
    InputBinding binding;

    // Current state
    bool isPressed = false;
    bool wasPressed = false;
    float value = 0.0f;
    glm::vec2 axis2D = glm::vec2(0.0f);

    // Timestamps
    float pressedTime = 0.0f;
    float releasedTime = 0.0f;

    // Callbacks
    std::function<void()> onPressed;
    std::function<void()> onReleased;
    std::function<void(float)> onValueChanged;
};

// ============================================================================
// Composite Actions (WASD to 2D vector)
// ============================================================================

struct CompositeAxis2D {
    std::string positiveX;  // Action name for +X
    std::string negativeX;  // Action name for -X
    std::string positiveY;  // Action name for +Y
    std::string negativeY;  // Action name for -Y
};

// ============================================================================
// Input State
// ============================================================================

struct KeyboardState {
    bool keys[512] = {false};
    bool prevKeys[512] = {false};
};

struct MouseState {
    glm::vec2 position = glm::vec2(0.0f);
    glm::vec2 previousPosition = glm::vec2(0.0f);
    glm::vec2 delta = glm::vec2(0.0f);
    float scrollDelta = 0.0f;
    bool buttons[5] = {false};
    bool prevButtons[5] = {false};
    bool isRelativeMode = false;  // Mouse capture mode
};

struct GamepadState {
    bool connected = false;
    std::string name;
    bool buttons[16] = {false};
    bool prevButtons[16] = {false};
    float axes[6] = {0.0f};
    float prevAxes[6] = {0.0f};
    float rumbleLeft = 0.0f;
    float rumbleRight = 0.0f;
};

// ============================================================================
// Input Event Types
// ============================================================================

struct KeyEvent {
    KeyCode key;
    int scanCode;
    bool isDown;
    bool isRepeat;
    bool shift;
    bool ctrl;
    bool alt;
    bool super;
};

struct MouseButtonEvent {
    MouseButton button;
    bool isDown;
    glm::vec2 position;
};

struct MouseMoveEvent {
    glm::vec2 position;
    glm::vec2 delta;
};

struct MouseScrollEvent {
    float delta;
    glm::vec2 position;
};

struct GamepadButtonEvent {
    int gamepadIndex;
    GamepadButton button;
    bool isDown;
};

struct GamepadAxisEvent {
    int gamepadIndex;
    GamepadAxis axis;
    float value;
};

struct TextInputEvent {
    std::string text;
    uint32_t codepoint;
};

// Unified input event
using InputEvent = std::variant<
    KeyEvent,
    MouseButtonEvent,
    MouseMoveEvent,
    MouseScrollEvent,
    GamepadButtonEvent,
    GamepadAxisEvent,
    TextInputEvent
>;

// ============================================================================
// Input Context
// ============================================================================

class InputContext {
public:
    InputContext(const std::string& name);
    ~InputContext() = default;

    const std::string& GetName() const { return m_name; }

    // Action management
    void RegisterAction(const std::string& name, ActionType type = ActionType::Button);
    void UnregisterAction(const std::string& name);
    InputAction* GetAction(const std::string& name);
    const InputAction* GetAction(const std::string& name) const;

    // Binding
    void SetBinding(const std::string& actionName, const InputBinding& binding);
    void AddBinding(const std::string& actionName, const InputSource& source);
    InputBinding* GetBinding(const std::string& actionName);

    // Composite axes
    void RegisterCompositeAxis2D(const std::string& name, const CompositeAxis2D& composite);
    glm::vec2 GetCompositeAxis2D(const std::string& name) const;

    // Update
    void Update(float deltaTime);
    void ProcessEvent(const InputEvent& event);

    // State queries
    bool IsActionPressed(const std::string& name) const;
    bool IsActionJustPressed(const std::string& name) const;
    bool IsActionJustReleased(const std::string& name) const;
    float GetActionValue(const std::string& name) const;
    glm::vec2 GetActionAxis2D(const std::string& name) const;

    // Enable/disable
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

    // Input consumption
    void SetConsumeInput(bool consume) { m_consumeInput = consume; }
    bool ConsumesInput() const { return m_consumeInput; }

private:
    std::string m_name;
    std::unordered_map<std::string, InputAction> m_actions;
    std::unordered_map<std::string, CompositeAxis2D> m_compositeAxes;
    bool m_enabled = true;
    bool m_consumeInput = true;
};

// ============================================================================
// Input Manager
// ============================================================================

class InputManager {
public:
    InputManager();
    ~InputManager();

    // Initialization
    bool Initialize();
    void Shutdown();

    // Update (call once per frame)
    void Update(float deltaTime);

    // Event processing (call from window callbacks)
    void OnKeyEvent(int key, int scanCode, bool isDown, bool isRepeat);
    void OnMouseButton(int button, bool isDown);
    void OnMouseMove(float x, float y);
    void OnMouseScroll(float delta);
    void OnTextInput(uint32_t codepoint);
    void OnGamepadConnect(int index);
    void OnGamepadDisconnect(int index);
    void OnGamepadButton(int index, int button, bool isDown);
    void OnGamepadAxis(int index, int axis, float value);

    // Context management
    InputContext* CreateContext(const std::string& name);
    void DestroyContext(const std::string& name);
    InputContext* GetContext(const std::string& name);
    void PushContext(const std::string& name);
    void PopContext();
    void SetActiveContext(const std::string& name);
    InputContext* GetActiveContext();

    // Global action queries (searches active context stack)
    bool IsActionPressed(const std::string& name) const;
    bool IsActionJustPressed(const std::string& name) const;
    bool IsActionJustReleased(const std::string& name) const;
    float GetActionValue(const std::string& name) const;
    glm::vec2 GetAxis2D(const std::string& name) const;

    // Raw input state
    const KeyboardState& GetKeyboardState() const { return m_keyboard; }
    const MouseState& GetMouseState() const { return m_mouse; }
    const GamepadState& GetGamepadState(int index = 0) const;
    int GetConnectedGamepadCount() const;

    // Direct key/button queries
    bool IsKeyDown(KeyCode key) const;
    bool IsKeyJustPressed(KeyCode key) const;
    bool IsKeyJustReleased(KeyCode key) const;
    bool IsMouseButtonDown(MouseButton button) const;
    bool IsMouseButtonJustPressed(MouseButton button) const;
    bool IsMouseButtonJustReleased(MouseButton button) const;
    bool IsGamepadButtonDown(int index, GamepadButton button) const;
    float GetGamepadAxis(int index, GamepadAxis axis) const;

    // Mouse
    glm::vec2 GetMousePosition() const { return m_mouse.position; }
    glm::vec2 GetMouseDelta() const { return m_mouse.delta; }
    float GetMouseScrollDelta() const { return m_mouse.scrollDelta; }
    void SetMouseRelativeMode(bool enabled);
    bool IsMouseRelativeMode() const { return m_mouse.isRelativeMode; }

    // Modifier state
    bool IsShiftDown() const;
    bool IsCtrlDown() const;
    bool IsAltDown() const;
    bool IsSuperDown() const;

    // Gamepad rumble
    void SetGamepadRumble(int index, float leftMotor, float rightMotor, float duration = 0.0f);
    void StopGamepadRumble(int index);

    // Input rebinding
    void StartListeningForInput(std::function<void(const InputSource&)> callback);
    void StopListeningForInput();
    bool IsListeningForInput() const { return m_listeningForInput; }

    // Serialization
    bool LoadBindings(const std::string& path);
    bool SaveBindings(const std::string& path);
    void ResetToDefaults();

    // Configuration
    void SetDeadzone(float deadzone) { m_globalDeadzone = deadzone; }
    float GetDeadzone() const { return m_globalDeadzone; }
    void SetMouseSensitivity(float sensitivity) { m_mouseSensitivity = sensitivity; }
    float GetMouseSensitivity() const { return m_mouseSensitivity; }

    // Text input mode
    void StartTextInput();
    void StopTextInput();
    bool IsTextInputActive() const { return m_textInputActive; }

    // Callbacks
    void SetTextInputCallback(std::function<void(const std::string&)> callback);

private:
    void UpdateActionState(InputAction& action, float deltaTime);
    float EvaluateBinding(const InputBinding& binding) const;
    float EvaluateSource(const InputSource& source) const;
    bool CheckModifiers(const InputSource& source) const;
    void DispatchEvent(const InputEvent& event);

    // Input state
    KeyboardState m_keyboard;
    MouseState m_mouse;
    std::vector<GamepadState> m_gamepads;

    // Contexts
    std::unordered_map<std::string, std::unique_ptr<InputContext>> m_contexts;
    std::vector<std::string> m_contextStack;

    // Configuration
    float m_globalDeadzone = 0.15f;
    float m_mouseSensitivity = 1.0f;
    bool m_textInputActive = false;

    // Rebinding
    bool m_listeningForInput = false;
    std::function<void(const InputSource&)> m_inputListenerCallback;

    // Text input
    std::function<void(const std::string&)> m_textInputCallback;

    // Rumble timers
    struct RumbleState {
        float duration = 0.0f;
        float elapsed = 0.0f;
    };
    std::vector<RumbleState> m_rumbleStates;

    // Current time
    float m_currentTime = 0.0f;

    // Max gamepads
    static constexpr int MAX_GAMEPADS = 4;
};

// ============================================================================
// Input Utilities
// ============================================================================

namespace InputUtils {

// Convert key code to string
std::string KeyCodeToString(KeyCode key);
KeyCode StringToKeyCode(const std::string& str);

// Convert mouse button to string
std::string MouseButtonToString(MouseButton button);
MouseButton StringToMouseButton(const std::string& str);

// Convert gamepad button to string
std::string GamepadButtonToString(GamepadButton button);
GamepadButton StringToGamepadButton(const std::string& str);

// Convert gamepad axis to string
std::string GamepadAxisToString(GamepadAxis axis);
GamepadAxis StringToGamepadAxis(const std::string& str);

// Apply deadzone
float ApplyDeadzone(float value, float deadzone);
glm::vec2 ApplyRadialDeadzone(const glm::vec2& value, float deadzone);

// Smooth input
float SmoothInput(float current, float target, float smoothing, float deltaTime);

} // namespace InputUtils

// ============================================================================
// Default Bindings
// ============================================================================

namespace DefaultBindings {

// FPS controls
void SetupFPSControls(InputContext* context);

// Third-person controls
void SetupThirdPersonControls(InputContext* context);

// Menu controls
void SetupMenuControls(InputContext* context);

// Vehicle controls
void SetupVehicleControls(InputContext* context);

} // namespace DefaultBindings

} // namespace Cortex::Input
