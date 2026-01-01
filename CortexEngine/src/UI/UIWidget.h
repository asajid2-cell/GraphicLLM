#pragma once

// UIWidget.h
// Base UI widget classes for retained-mode UI rendering.
// Supports anchoring, layouts, styling, and input handling.

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

namespace Cortex::UI {

// Forward declarations
class UIWidget;
class UICanvas;
class UISystem;

// Anchor presets
enum class AnchorPreset : uint8_t {
    TopLeft,
    TopCenter,
    TopRight,
    MiddleLeft,
    MiddleCenter,
    MiddleRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
    StretchTop,
    StretchMiddle,
    StretchBottom,
    StretchLeft,
    StretchCenter,
    StretchRight,
    StretchAll
};

// Widget visibility
enum class Visibility : uint8_t {
    Visible,
    Hidden,         // Not rendered but takes up space
    Collapsed       // Not rendered, doesn't take space
};

// Widget state flags
enum class WidgetState : uint8_t {
    Normal = 0,
    Hovered = 1 << 0,
    Pressed = 1 << 1,
    Focused = 1 << 2,
    Disabled = 1 << 3,
    Selected = 1 << 4
};

inline WidgetState operator|(WidgetState a, WidgetState b) {
    return static_cast<WidgetState>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline bool operator&(WidgetState a, WidgetState b) {
    return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
}

// Mouse button enumeration
enum class MouseButton : uint8_t {
    Left = 0,
    Right = 1,
    Middle = 2
};

// UI event types
struct UIMouseEvent {
    glm::vec2 position;
    glm::vec2 delta;
    MouseButton button;
    bool isDown;
    int clicks;         // 1 = single, 2 = double click
    float wheelDelta;
};

struct UIKeyEvent {
    int keyCode;
    int scanCode;
    bool isDown;
    bool isRepeat;
    bool ctrl, shift, alt;
};

struct UITextEvent {
    std::string text;
    uint32_t codepoint;
};

// Rect transform for positioning
struct RectTransform {
    // Anchor points (0-1 relative to parent)
    glm::vec2 anchorMin = glm::vec2(0.0f);      // Top-left anchor
    glm::vec2 anchorMax = glm::vec2(0.0f);      // Bottom-right anchor

    // Offset from anchors (in pixels)
    glm::vec2 offsetMin = glm::vec2(0.0f);      // Offset from anchorMin
    glm::vec2 offsetMax = glm::vec2(0.0f);      // Offset from anchorMax

    // Pivot point (0-1 relative to widget size)
    glm::vec2 pivot = glm::vec2(0.5f);

    // Rotation and scale
    float rotation = 0.0f;                       // Degrees
    glm::vec2 scale = glm::vec2(1.0f);

    // Calculated bounds (set by layout system)
    glm::vec2 calculatedPosition = glm::vec2(0.0f);
    glm::vec2 calculatedSize = glm::vec2(100.0f);

    // Set position and size directly
    void SetRect(float x, float y, float width, float height);

    // Apply anchor preset
    void SetAnchorPreset(AnchorPreset preset, bool keepOffsets = false);

    // Get world-space rect
    glm::vec4 GetWorldRect(const glm::vec2& parentPos, const glm::vec2& parentSize) const;
};

// UI styling
struct UIStyle {
    // Colors
    glm::vec4 backgroundColor = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f);
    glm::vec4 borderColor = glm::vec4(0.4f, 0.4f, 0.4f, 1.0f);
    glm::vec4 textColor = glm::vec4(1.0f);
    glm::vec4 hoverColor = glm::vec4(0.3f, 0.3f, 0.3f, 1.0f);
    glm::vec4 pressedColor = glm::vec4(0.15f, 0.15f, 0.15f, 1.0f);
    glm::vec4 disabledColor = glm::vec4(0.1f, 0.1f, 0.1f, 0.5f);

    // Border
    float borderWidth = 0.0f;
    float cornerRadius = 0.0f;

    // Padding and margin
    glm::vec4 padding = glm::vec4(4.0f);        // left, top, right, bottom
    glm::vec4 margin = glm::vec4(0.0f);

    // Font
    std::string fontName = "default";
    float fontSize = 14.0f;

    // Get color based on state
    glm::vec4 GetBackgroundColor(WidgetState state) const;
};

// Base widget class
class UIWidget : public std::enable_shared_from_this<UIWidget> {
public:
    UIWidget(const std::string& name = "Widget");
    virtual ~UIWidget() = default;

    // Hierarchy
    void AddChild(std::shared_ptr<UIWidget> child);
    void RemoveChild(std::shared_ptr<UIWidget> child);
    void RemoveFromParent();
    UIWidget* GetParent() const { return m_parent; }
    const std::vector<std::shared_ptr<UIWidget>>& GetChildren() const { return m_children; }
    std::shared_ptr<UIWidget> FindChild(const std::string& name, bool recursive = true);

    // Transform
    RectTransform& GetRectTransform() { return m_rectTransform; }
    const RectTransform& GetRectTransform() const { return m_rectTransform; }
    void SetPosition(float x, float y);
    void SetSize(float width, float height);
    glm::vec2 GetPosition() const { return m_rectTransform.calculatedPosition; }
    glm::vec2 GetSize() const { return m_rectTransform.calculatedSize; }
    glm::vec4 GetWorldRect() const;

    // Style
    UIStyle& GetStyle() { return m_style; }
    const UIStyle& GetStyle() const { return m_style; }

    // State
    WidgetState GetState() const { return m_state; }
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return !(m_state & WidgetState::Disabled); }
    void SetVisibility(Visibility vis) { m_visibility = vis; }
    Visibility GetVisibility() const { return m_visibility; }
    bool IsVisible() const { return m_visibility == Visibility::Visible; }

    // Focus
    void SetFocusable(bool focusable) { m_focusable = focusable; }
    bool IsFocusable() const { return m_focusable; }
    bool IsFocused() const { return m_state & WidgetState::Focused; }
    void Focus();
    void Unfocus();

    // Name and tag
    const std::string& GetName() const { return m_name; }
    void SetName(const std::string& name) { m_name = name; }
    const std::string& GetTag() const { return m_tag; }
    void SetTag(const std::string& tag) { m_tag = tag; }

    // Hit testing
    virtual bool HitTest(const glm::vec2& point) const;

    // Update and render (called by system)
    virtual void Update(float deltaTime);
    virtual void Render(class UIRenderer* renderer);

    // Layout
    virtual glm::vec2 GetPreferredSize() const;
    virtual void Layout();

    // Input events (return true if handled)
    virtual bool OnMouseEnter(const UIMouseEvent& event);
    virtual bool OnMouseLeave(const UIMouseEvent& event);
    virtual bool OnMouseMove(const UIMouseEvent& event);
    virtual bool OnMouseDown(const UIMouseEvent& event);
    virtual bool OnMouseUp(const UIMouseEvent& event);
    virtual bool OnMouseClick(const UIMouseEvent& event);
    virtual bool OnMouseDoubleClick(const UIMouseEvent& event);
    virtual bool OnMouseWheel(const UIMouseEvent& event);
    virtual bool OnKeyDown(const UIKeyEvent& event);
    virtual bool OnKeyUp(const UIKeyEvent& event);
    virtual bool OnTextInput(const UITextEvent& event);
    virtual void OnFocusGained();
    virtual void OnFocusLost();

    // Callbacks
    std::function<void(UIWidget*)> onClick;
    std::function<void(UIWidget*)> onHoverEnter;
    std::function<void(UIWidget*)> onHoverExit;
    std::function<void(UIWidget*, const std::string&)> onValueChanged;

protected:
    std::string m_name;
    std::string m_tag;
    UIWidget* m_parent = nullptr;
    std::vector<std::shared_ptr<UIWidget>> m_children;

    RectTransform m_rectTransform;
    UIStyle m_style;
    WidgetState m_state = WidgetState::Normal;
    Visibility m_visibility = Visibility::Visible;
    bool m_focusable = false;

    void SetState(WidgetState state, bool set);
};

// Panel widget (container with background)
class UIPanel : public UIWidget {
public:
    UIPanel(const std::string& name = "Panel");

    void Render(UIRenderer* renderer) override;

    // 9-slice image support
    void SetBackgroundImage(uint32_t textureId);
    void Set9SliceBorders(float left, float top, float right, float bottom);

private:
    uint32_t m_backgroundTexture = 0;
    glm::vec4 m_sliceBorders = glm::vec4(0.0f);  // 9-slice borders
};

// Text widget
class UIText : public UIWidget {
public:
    UIText(const std::string& name = "Text");

    void SetText(const std::string& text);
    const std::string& GetText() const { return m_text; }

    void SetAlignment(int horizontal, int vertical);  // -1=left/top, 0=center, 1=right/bottom
    void SetWordWrap(bool wrap) { m_wordWrap = wrap; }
    void SetOverflow(bool overflow) { m_allowOverflow = overflow; }

    glm::vec2 GetPreferredSize() const override;
    void Render(UIRenderer* renderer) override;

private:
    std::string m_text;
    int m_hAlign = -1;      // Left
    int m_vAlign = 0;       // Center
    bool m_wordWrap = false;
    bool m_allowOverflow = false;
};

// Image widget
class UIImage : public UIWidget {
public:
    UIImage(const std::string& name = "Image");

    void SetTexture(uint32_t textureId);
    void SetColor(const glm::vec4& color) { m_tintColor = color; }
    void SetPreserveAspect(bool preserve) { m_preserveAspect = preserve; }
    void SetUVRect(const glm::vec4& uvRect) { m_uvRect = uvRect; }

    void Render(UIRenderer* renderer) override;

private:
    uint32_t m_textureId = 0;
    glm::vec4 m_tintColor = glm::vec4(1.0f);
    glm::vec4 m_uvRect = glm::vec4(0, 0, 1, 1);  // x, y, w, h
    bool m_preserveAspect = false;
};

// Button widget
class UIButton : public UIWidget {
public:
    UIButton(const std::string& name = "Button");

    void SetText(const std::string& text);
    const std::string& GetText() const { return m_text; }

    glm::vec2 GetPreferredSize() const override;
    void Render(UIRenderer* renderer) override;

    bool OnMouseDown(const UIMouseEvent& event) override;
    bool OnMouseUp(const UIMouseEvent& event) override;
    bool OnMouseEnter(const UIMouseEvent& event) override;
    bool OnMouseLeave(const UIMouseEvent& event) override;

private:
    std::string m_text;
};

// Slider widget
class UISlider : public UIWidget {
public:
    UISlider(const std::string& name = "Slider");

    void SetValue(float value);
    float GetValue() const { return m_value; }
    void SetRange(float min, float max);
    void SetStep(float step) { m_step = step; }

    glm::vec2 GetPreferredSize() const override;
    void Render(UIRenderer* renderer) override;

    bool OnMouseDown(const UIMouseEvent& event) override;
    bool OnMouseUp(const UIMouseEvent& event) override;
    bool OnMouseMove(const UIMouseEvent& event) override;

    std::function<void(float)> onValueChanged;

private:
    float m_value = 0.5f;
    float m_min = 0.0f;
    float m_max = 1.0f;
    float m_step = 0.0f;
    bool m_isDragging = false;

    void UpdateValueFromMouse(float mouseX);
};

// Checkbox widget
class UICheckbox : public UIWidget {
public:
    UICheckbox(const std::string& name = "Checkbox");

    void SetChecked(bool checked);
    bool IsChecked() const { return m_checked; }
    void SetText(const std::string& text) { m_text = text; }

    glm::vec2 GetPreferredSize() const override;
    void Render(UIRenderer* renderer) override;

    bool OnMouseClick(const UIMouseEvent& event) override;

    std::function<void(bool)> onCheckedChanged;

private:
    bool m_checked = false;
    std::string m_text;
};

// Text input widget
class UITextInput : public UIWidget {
public:
    UITextInput(const std::string& name = "TextInput");

    void SetText(const std::string& text);
    const std::string& GetText() const { return m_text; }
    void SetPlaceholder(const std::string& placeholder) { m_placeholder = placeholder; }
    void SetPassword(bool password) { m_isPassword = password; }
    void SetMaxLength(size_t maxLen) { m_maxLength = maxLen; }

    glm::vec2 GetPreferredSize() const override;
    void Render(UIRenderer* renderer) override;
    void Update(float deltaTime) override;

    bool OnMouseClick(const UIMouseEvent& event) override;
    bool OnKeyDown(const UIKeyEvent& event) override;
    bool OnTextInput(const UITextEvent& event) override;
    void OnFocusGained() override;
    void OnFocusLost() override;

    std::function<void(const std::string&)> onTextChanged;
    std::function<void(const std::string&)> onSubmit;

private:
    std::string m_text;
    std::string m_placeholder;
    size_t m_cursorPos = 0;
    size_t m_selectionStart = 0;
    size_t m_selectionEnd = 0;
    size_t m_maxLength = 0;
    float m_cursorBlinkTimer = 0.0f;
    bool m_showCursor = true;
    bool m_isPassword = false;

    void InsertText(const std::string& text);
    void DeleteSelection();
    void MoveCursor(int delta, bool shift);
};

// Progress bar widget
class UIProgressBar : public UIWidget {
public:
    UIProgressBar(const std::string& name = "ProgressBar");

    void SetProgress(float progress);
    float GetProgress() const { return m_progress; }
    void SetFillColor(const glm::vec4& color) { m_fillColor = color; }

    glm::vec2 GetPreferredSize() const override;
    void Render(UIRenderer* renderer) override;

private:
    float m_progress = 0.5f;
    glm::vec4 m_fillColor = glm::vec4(0.2f, 0.6f, 1.0f, 1.0f);
};

// Scroll view widget
class UIScrollView : public UIWidget {
public:
    UIScrollView(const std::string& name = "ScrollView");

    void SetContent(std::shared_ptr<UIWidget> content);
    UIWidget* GetContent() const { return m_content.get(); }

    void SetScrollOffset(const glm::vec2& offset);
    glm::vec2 GetScrollOffset() const { return m_scrollOffset; }

    void SetHorizontalScrollEnabled(bool enabled) { m_hScrollEnabled = enabled; }
    void SetVerticalScrollEnabled(bool enabled) { m_vScrollEnabled = enabled; }

    void Layout() override;
    void Render(UIRenderer* renderer) override;

    bool OnMouseWheel(const UIMouseEvent& event) override;
    bool OnMouseDown(const UIMouseEvent& event) override;
    bool OnMouseUp(const UIMouseEvent& event) override;
    bool OnMouseMove(const UIMouseEvent& event) override;

private:
    std::shared_ptr<UIWidget> m_content;
    glm::vec2 m_scrollOffset = glm::vec2(0.0f);
    glm::vec2 m_contentSize = glm::vec2(0.0f);
    bool m_hScrollEnabled = true;
    bool m_vScrollEnabled = true;
    bool m_isDragging = false;
    glm::vec2 m_dragStart;

    void ClampScrollOffset();
};

// Dropdown/combo box widget
class UIDropdown : public UIWidget {
public:
    UIDropdown(const std::string& name = "Dropdown");

    void AddOption(const std::string& option);
    void ClearOptions();
    void SetSelectedIndex(int index);
    int GetSelectedIndex() const { return m_selectedIndex; }
    std::string GetSelectedOption() const;

    glm::vec2 GetPreferredSize() const override;
    void Render(UIRenderer* renderer) override;

    bool OnMouseClick(const UIMouseEvent& event) override;

    std::function<void(int, const std::string&)> onSelectionChanged;

private:
    std::vector<std::string> m_options;
    int m_selectedIndex = -1;
    bool m_isOpen = false;
};

} // namespace Cortex::UI
