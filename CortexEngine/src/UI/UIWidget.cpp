// UIWidget.cpp
// Implementation of base UI widget classes.

#include "UIWidget.h"
#include "UIRenderer.h"
#include <algorithm>
#include <cmath>

namespace Cortex::UI {

// ============================================================================
// RectTransform
// ============================================================================

void RectTransform::SetRect(float x, float y, float width, float height) {
    anchorMin = glm::vec2(0.0f);
    anchorMax = glm::vec2(0.0f);
    offsetMin = glm::vec2(x, y);
    offsetMax = glm::vec2(x + width, y + height);
}

void RectTransform::SetAnchorPreset(AnchorPreset preset, bool keepOffsets) {
    glm::vec2 oldMin = anchorMin;
    glm::vec2 oldMax = anchorMax;

    switch (preset) {
        case AnchorPreset::TopLeft:
            anchorMin = anchorMax = glm::vec2(0, 0);
            break;
        case AnchorPreset::TopCenter:
            anchorMin = anchorMax = glm::vec2(0.5f, 0);
            break;
        case AnchorPreset::TopRight:
            anchorMin = anchorMax = glm::vec2(1, 0);
            break;
        case AnchorPreset::MiddleLeft:
            anchorMin = anchorMax = glm::vec2(0, 0.5f);
            break;
        case AnchorPreset::MiddleCenter:
            anchorMin = anchorMax = glm::vec2(0.5f, 0.5f);
            break;
        case AnchorPreset::MiddleRight:
            anchorMin = anchorMax = glm::vec2(1, 0.5f);
            break;
        case AnchorPreset::BottomLeft:
            anchorMin = anchorMax = glm::vec2(0, 1);
            break;
        case AnchorPreset::BottomCenter:
            anchorMin = anchorMax = glm::vec2(0.5f, 1);
            break;
        case AnchorPreset::BottomRight:
            anchorMin = anchorMax = glm::vec2(1, 1);
            break;
        case AnchorPreset::StretchTop:
            anchorMin = glm::vec2(0, 0);
            anchorMax = glm::vec2(1, 0);
            break;
        case AnchorPreset::StretchMiddle:
            anchorMin = glm::vec2(0, 0.5f);
            anchorMax = glm::vec2(1, 0.5f);
            break;
        case AnchorPreset::StretchBottom:
            anchorMin = glm::vec2(0, 1);
            anchorMax = glm::vec2(1, 1);
            break;
        case AnchorPreset::StretchLeft:
            anchorMin = glm::vec2(0, 0);
            anchorMax = glm::vec2(0, 1);
            break;
        case AnchorPreset::StretchCenter:
            anchorMin = glm::vec2(0.5f, 0);
            anchorMax = glm::vec2(0.5f, 1);
            break;
        case AnchorPreset::StretchRight:
            anchorMin = glm::vec2(1, 0);
            anchorMax = glm::vec2(1, 1);
            break;
        case AnchorPreset::StretchAll:
            anchorMin = glm::vec2(0, 0);
            anchorMax = glm::vec2(1, 1);
            break;
    }

    if (!keepOffsets) {
        offsetMin = glm::vec2(0);
        offsetMax = glm::vec2(0);
    }
}

glm::vec4 RectTransform::GetWorldRect(const glm::vec2& parentPos, const glm::vec2& parentSize) const {
    // Calculate anchor positions in parent space
    glm::vec2 anchorPosMin = parentPos + anchorMin * parentSize;
    glm::vec2 anchorPosMax = parentPos + anchorMax * parentSize;

    // Apply offsets
    glm::vec2 rectMin = anchorPosMin + offsetMin;
    glm::vec2 rectMax = anchorPosMax + offsetMax;

    // Handle case where anchors are the same (fixed size)
    if (anchorMin == anchorMax) {
        rectMax = rectMin + calculatedSize;
        // Apply pivot
        glm::vec2 pivotOffset = pivot * calculatedSize;
        rectMin -= pivotOffset;
        rectMax -= pivotOffset;
    }

    return glm::vec4(rectMin.x, rectMin.y, rectMax.x - rectMin.x, rectMax.y - rectMin.y);
}

// ============================================================================
// UIStyle
// ============================================================================

glm::vec4 UIStyle::GetBackgroundColor(WidgetState state) const {
    if (state & WidgetState::Disabled) return disabledColor;
    if (state & WidgetState::Pressed) return pressedColor;
    if (state & WidgetState::Hovered) return hoverColor;
    return backgroundColor;
}

// ============================================================================
// UIWidget
// ============================================================================

UIWidget::UIWidget(const std::string& name) : m_name(name) {}

void UIWidget::AddChild(std::shared_ptr<UIWidget> child) {
    if (!child || child.get() == this) return;

    // Remove from previous parent
    if (child->m_parent) {
        child->RemoveFromParent();
    }

    child->m_parent = this;
    m_children.push_back(child);
}

void UIWidget::RemoveChild(std::shared_ptr<UIWidget> child) {
    if (!child) return;

    auto it = std::find(m_children.begin(), m_children.end(), child);
    if (it != m_children.end()) {
        (*it)->m_parent = nullptr;
        m_children.erase(it);
    }
}

void UIWidget::RemoveFromParent() {
    if (m_parent) {
        auto& siblings = m_parent->m_children;
        for (auto it = siblings.begin(); it != siblings.end(); ++it) {
            if (it->get() == this) {
                m_parent = nullptr;
                siblings.erase(it);
                return;
            }
        }
    }
}

std::shared_ptr<UIWidget> UIWidget::FindChild(const std::string& name, bool recursive) {
    for (auto& child : m_children) {
        if (child->m_name == name) return child;

        if (recursive) {
            auto found = child->FindChild(name, true);
            if (found) return found;
        }
    }
    return nullptr;
}

void UIWidget::SetPosition(float x, float y) {
    m_rectTransform.offsetMin = glm::vec2(x, y);
    m_rectTransform.calculatedPosition = glm::vec2(x, y);
}

void UIWidget::SetSize(float width, float height) {
    m_rectTransform.calculatedSize = glm::vec2(width, height);
    if (m_rectTransform.anchorMin == m_rectTransform.anchorMax) {
        m_rectTransform.offsetMax = m_rectTransform.offsetMin + glm::vec2(width, height);
    }
}

glm::vec4 UIWidget::GetWorldRect() const {
    if (m_parent) {
        glm::vec4 parentRect = m_parent->GetWorldRect();
        return m_rectTransform.GetWorldRect(
            glm::vec2(parentRect.x, parentRect.y),
            glm::vec2(parentRect.z, parentRect.w)
        );
    }
    return glm::vec4(m_rectTransform.calculatedPosition, m_rectTransform.calculatedSize);
}

void UIWidget::SetEnabled(bool enabled) {
    SetState(WidgetState::Disabled, !enabled);
}

void UIWidget::Focus() {
    SetState(WidgetState::Focused, true);
    OnFocusGained();
}

void UIWidget::Unfocus() {
    SetState(WidgetState::Focused, false);
    OnFocusLost();
}

bool UIWidget::HitTest(const glm::vec2& point) const {
    if (!IsVisible() || !IsEnabled()) return false;

    glm::vec4 rect = GetWorldRect();
    return point.x >= rect.x && point.x < rect.x + rect.z &&
           point.y >= rect.y && point.y < rect.y + rect.w;
}

void UIWidget::Update(float deltaTime) {
    for (auto& child : m_children) {
        if (child->IsVisible()) {
            child->Update(deltaTime);
        }
    }
}

void UIWidget::Render(UIRenderer* renderer) {
    if (!IsVisible()) return;

    // Render children
    for (auto& child : m_children) {
        child->Render(renderer);
    }
}

glm::vec2 UIWidget::GetPreferredSize() const {
    return m_rectTransform.calculatedSize;
}

void UIWidget::Layout() {
    // Calculate rect based on parent
    if (m_parent) {
        glm::vec4 parentRect = m_parent->GetWorldRect();
        glm::vec4 worldRect = m_rectTransform.GetWorldRect(
            glm::vec2(parentRect.x, parentRect.y),
            glm::vec2(parentRect.z, parentRect.w)
        );
        m_rectTransform.calculatedPosition = glm::vec2(worldRect.x, worldRect.y);
        m_rectTransform.calculatedSize = glm::vec2(worldRect.z, worldRect.w);
    }

    // Layout children
    for (auto& child : m_children) {
        child->Layout();
    }
}

bool UIWidget::OnMouseEnter(const UIMouseEvent& event) {
    SetState(WidgetState::Hovered, true);
    if (onHoverEnter) onHoverEnter(this);
    return false;
}

bool UIWidget::OnMouseLeave(const UIMouseEvent& event) {
    SetState(WidgetState::Hovered, false);
    SetState(WidgetState::Pressed, false);
    if (onHoverExit) onHoverExit(this);
    return false;
}

bool UIWidget::OnMouseMove(const UIMouseEvent& event) { return false; }
bool UIWidget::OnMouseDown(const UIMouseEvent& event) { return false; }
bool UIWidget::OnMouseUp(const UIMouseEvent& event) { return false; }
bool UIWidget::OnMouseClick(const UIMouseEvent& event) {
    if (onClick) onClick(this);
    return false;
}
bool UIWidget::OnMouseDoubleClick(const UIMouseEvent& event) { return false; }
bool UIWidget::OnMouseWheel(const UIMouseEvent& event) { return false; }
bool UIWidget::OnKeyDown(const UIKeyEvent& event) { return false; }
bool UIWidget::OnKeyUp(const UIKeyEvent& event) { return false; }
bool UIWidget::OnTextInput(const UITextEvent& event) { return false; }
void UIWidget::OnFocusGained() {}
void UIWidget::OnFocusLost() {}

void UIWidget::SetState(WidgetState state, bool set) {
    if (set) {
        m_state = m_state | state;
    } else {
        m_state = static_cast<WidgetState>(static_cast<uint8_t>(m_state) & ~static_cast<uint8_t>(state));
    }
}

// ============================================================================
// UIPanel
// ============================================================================

UIPanel::UIPanel(const std::string& name) : UIWidget(name) {}

void UIPanel::Render(UIRenderer* renderer) {
    if (!IsVisible()) return;

    glm::vec4 rect = GetWorldRect();

    // Draw background
    if (m_backgroundTexture != 0 && m_sliceBorders != glm::vec4(0.0f)) {
        renderer->DrawImage9Slice(rect, m_backgroundTexture, m_sliceBorders,
                                   m_style.GetBackgroundColor(m_state));
    } else if (m_backgroundTexture != 0) {
        renderer->DrawImage(rect, m_backgroundTexture, m_style.GetBackgroundColor(m_state));
    } else {
        renderer->DrawRect(rect, m_style.GetBackgroundColor(m_state), m_style.cornerRadius);
    }

    // Draw border
    if (m_style.borderWidth > 0) {
        renderer->DrawRectOutline(rect, m_style.borderColor, m_style.borderWidth, m_style.cornerRadius);
    }

    // Render children
    UIWidget::Render(renderer);
}

void UIPanel::SetBackgroundImage(uint32_t textureId) {
    m_backgroundTexture = textureId;
}

void UIPanel::Set9SliceBorders(float left, float top, float right, float bottom) {
    m_sliceBorders = glm::vec4(left, top, right, bottom);
}

// ============================================================================
// UIText
// ============================================================================

UIText::UIText(const std::string& name) : UIWidget(name) {}

void UIText::SetText(const std::string& text) {
    m_text = text;
}

void UIText::SetAlignment(int horizontal, int vertical) {
    m_hAlign = horizontal;
    m_vAlign = vertical;
}

glm::vec2 UIText::GetPreferredSize() const {
    // Would calculate from font metrics
    return glm::vec2(m_text.length() * m_style.fontSize * 0.6f, m_style.fontSize * 1.2f);
}

void UIText::Render(UIRenderer* renderer) {
    if (!IsVisible() || m_text.empty()) return;

    glm::vec4 rect = GetWorldRect();
    glm::vec2 textSize = GetPreferredSize();

    // Calculate position based on alignment
    glm::vec2 pos = glm::vec2(rect.x, rect.y);

    switch (m_hAlign) {
        case 0: pos.x += (rect.z - textSize.x) * 0.5f; break;  // Center
        case 1: pos.x += rect.z - textSize.x; break;           // Right
    }

    switch (m_vAlign) {
        case 0: pos.y += (rect.w - textSize.y) * 0.5f; break;  // Center
        case 1: pos.y += rect.w - textSize.y; break;           // Bottom
    }

    renderer->DrawText(m_text, pos, m_style.textColor, m_style.fontName, m_style.fontSize);

    UIWidget::Render(renderer);
}

// ============================================================================
// UIImage
// ============================================================================

UIImage::UIImage(const std::string& name) : UIWidget(name) {}

void UIImage::SetTexture(uint32_t textureId) {
    m_textureId = textureId;
}

void UIImage::Render(UIRenderer* renderer) {
    if (!IsVisible()) return;

    glm::vec4 rect = GetWorldRect();

    if (m_textureId != 0) {
        renderer->DrawImage(rect, m_textureId, m_tintColor, m_uvRect);
    }

    UIWidget::Render(renderer);
}

// ============================================================================
// UIButton
// ============================================================================

UIButton::UIButton(const std::string& name) : UIWidget(name) {
    m_style.backgroundColor = glm::vec4(0.3f, 0.3f, 0.3f, 1.0f);
    m_style.hoverColor = glm::vec4(0.4f, 0.4f, 0.4f, 1.0f);
    m_style.pressedColor = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f);
    m_style.cornerRadius = 4.0f;
}

void UIButton::SetText(const std::string& text) {
    m_text = text;
}

glm::vec2 UIButton::GetPreferredSize() const {
    float textWidth = m_text.length() * m_style.fontSize * 0.6f;
    return glm::vec2(textWidth + m_style.padding.x + m_style.padding.z,
                     m_style.fontSize * 1.2f + m_style.padding.y + m_style.padding.w);
}

void UIButton::Render(UIRenderer* renderer) {
    if (!IsVisible()) return;

    glm::vec4 rect = GetWorldRect();

    // Draw background
    renderer->DrawRect(rect, m_style.GetBackgroundColor(m_state), m_style.cornerRadius);

    // Draw border
    if (m_style.borderWidth > 0) {
        renderer->DrawRectOutline(rect, m_style.borderColor, m_style.borderWidth, m_style.cornerRadius);
    }

    // Draw text centered
    if (!m_text.empty()) {
        glm::vec2 textSize(m_text.length() * m_style.fontSize * 0.6f, m_style.fontSize);
        glm::vec2 textPos(
            rect.x + (rect.z - textSize.x) * 0.5f,
            rect.y + (rect.w - textSize.y) * 0.5f
        );
        renderer->DrawText(m_text, textPos, m_style.textColor, m_style.fontName, m_style.fontSize);
    }

    UIWidget::Render(renderer);
}

bool UIButton::OnMouseDown(const UIMouseEvent& event) {
    if (event.button == MouseButton::Left) {
        SetState(WidgetState::Pressed, true);
        return true;
    }
    return false;
}

bool UIButton::OnMouseUp(const UIMouseEvent& event) {
    if (event.button == MouseButton::Left && (m_state & WidgetState::Pressed)) {
        SetState(WidgetState::Pressed, false);
        if (HitTest(event.position)) {
            if (onClick) onClick(this);
        }
        return true;
    }
    return false;
}

bool UIButton::OnMouseEnter(const UIMouseEvent& event) {
    SetState(WidgetState::Hovered, true);
    return true;
}

bool UIButton::OnMouseLeave(const UIMouseEvent& event) {
    SetState(WidgetState::Hovered, false);
    SetState(WidgetState::Pressed, false);
    return true;
}

// ============================================================================
// UISlider
// ============================================================================

UISlider::UISlider(const std::string& name) : UIWidget(name) {
    m_style.backgroundColor = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f);
}

void UISlider::SetValue(float value) {
    float newValue = glm::clamp(value, m_min, m_max);
    if (m_step > 0) {
        newValue = std::round((newValue - m_min) / m_step) * m_step + m_min;
    }
    if (newValue != m_value) {
        m_value = newValue;
        if (onValueChanged) onValueChanged(m_value);
    }
}

void UISlider::SetRange(float min, float max) {
    m_min = min;
    m_max = max;
    SetValue(m_value);  // Clamp to new range
}

glm::vec2 UISlider::GetPreferredSize() const {
    return glm::vec2(200.0f, 20.0f);
}

void UISlider::Render(UIRenderer* renderer) {
    if (!IsVisible()) return;

    glm::vec4 rect = GetWorldRect();
    float trackHeight = 4.0f;
    float handleSize = 16.0f;

    // Draw track
    glm::vec4 trackRect(
        rect.x,
        rect.y + (rect.w - trackHeight) * 0.5f,
        rect.z,
        trackHeight
    );
    renderer->DrawRect(trackRect, m_style.backgroundColor, 2.0f);

    // Draw filled portion
    float fillRatio = (m_value - m_min) / (m_max - m_min);
    glm::vec4 fillRect(
        trackRect.x,
        trackRect.y,
        trackRect.z * fillRatio,
        trackRect.w
    );
    renderer->DrawRect(fillRect, m_style.hoverColor, 2.0f);

    // Draw handle
    float handleX = rect.x + (rect.z - handleSize) * fillRatio;
    glm::vec4 handleRect(
        handleX,
        rect.y + (rect.w - handleSize) * 0.5f,
        handleSize,
        handleSize
    );
    glm::vec4 handleColor = (m_state & WidgetState::Pressed) ? m_style.pressedColor :
                            (m_state & WidgetState::Hovered) ? m_style.hoverColor :
                            glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    renderer->DrawRect(handleRect, handleColor, handleSize * 0.5f);

    UIWidget::Render(renderer);
}

bool UISlider::OnMouseDown(const UIMouseEvent& event) {
    if (event.button == MouseButton::Left) {
        m_isDragging = true;
        SetState(WidgetState::Pressed, true);
        UpdateValueFromMouse(event.position.x);
        return true;
    }
    return false;
}

bool UISlider::OnMouseUp(const UIMouseEvent& event) {
    if (event.button == MouseButton::Left) {
        m_isDragging = false;
        SetState(WidgetState::Pressed, false);
        return true;
    }
    return false;
}

bool UISlider::OnMouseMove(const UIMouseEvent& event) {
    if (m_isDragging) {
        UpdateValueFromMouse(event.position.x);
        return true;
    }
    return false;
}

void UISlider::UpdateValueFromMouse(float mouseX) {
    glm::vec4 rect = GetWorldRect();
    float ratio = (mouseX - rect.x) / rect.z;
    SetValue(m_min + ratio * (m_max - m_min));
}

// ============================================================================
// UICheckbox
// ============================================================================

UICheckbox::UICheckbox(const std::string& name) : UIWidget(name) {}

void UICheckbox::SetChecked(bool checked) {
    if (m_checked != checked) {
        m_checked = checked;
        if (onCheckedChanged) onCheckedChanged(m_checked);
    }
}

glm::vec2 UICheckbox::GetPreferredSize() const {
    float boxSize = m_style.fontSize + 4.0f;
    float textWidth = m_text.empty() ? 0 : m_text.length() * m_style.fontSize * 0.6f + 8.0f;
    return glm::vec2(boxSize + textWidth, boxSize);
}

void UICheckbox::Render(UIRenderer* renderer) {
    if (!IsVisible()) return;

    glm::vec4 rect = GetWorldRect();
    float boxSize = m_style.fontSize + 4.0f;

    // Draw checkbox box
    glm::vec4 boxRect(rect.x, rect.y + (rect.w - boxSize) * 0.5f, boxSize, boxSize);
    renderer->DrawRect(boxRect, m_style.backgroundColor, 2.0f);
    renderer->DrawRectOutline(boxRect, m_style.borderColor, 1.0f, 2.0f);

    // Draw check mark
    if (m_checked) {
        glm::vec4 checkRect(boxRect.x + 3, boxRect.y + 3, boxRect.z - 6, boxRect.w - 6);
        renderer->DrawRect(checkRect, m_style.textColor, 1.0f);
    }

    // Draw label
    if (!m_text.empty()) {
        glm::vec2 textPos(rect.x + boxSize + 8.0f, rect.y + (rect.w - m_style.fontSize) * 0.5f);
        renderer->DrawText(m_text, textPos, m_style.textColor, m_style.fontName, m_style.fontSize);
    }

    UIWidget::Render(renderer);
}

bool UICheckbox::OnMouseClick(const UIMouseEvent& event) {
    SetChecked(!m_checked);
    return true;
}

// ============================================================================
// UITextInput
// ============================================================================

UITextInput::UITextInput(const std::string& name) : UIWidget(name) {
    m_focusable = true;
    m_style.backgroundColor = glm::vec4(0.15f, 0.15f, 0.15f, 1.0f);
    m_style.borderWidth = 1.0f;
    m_style.cornerRadius = 2.0f;
}

void UITextInput::SetText(const std::string& text) {
    m_text = text;
    m_cursorPos = text.length();
    m_selectionStart = m_selectionEnd = m_cursorPos;
    if (onTextChanged) onTextChanged(m_text);
}

glm::vec2 UITextInput::GetPreferredSize() const {
    return glm::vec2(200.0f, m_style.fontSize + m_style.padding.y + m_style.padding.w);
}

void UITextInput::Update(float deltaTime) {
    if (m_state & WidgetState::Focused) {
        m_cursorBlinkTimer += deltaTime;
        if (m_cursorBlinkTimer >= 0.5f) {
            m_cursorBlinkTimer = 0.0f;
            m_showCursor = !m_showCursor;
        }
    }
    UIWidget::Update(deltaTime);
}

void UITextInput::Render(UIRenderer* renderer) {
    if (!IsVisible()) return;

    glm::vec4 rect = GetWorldRect();

    // Draw background
    glm::vec4 bgColor = (m_state & WidgetState::Focused) ?
                        glm::vec4(0.2f, 0.2f, 0.25f, 1.0f) : m_style.backgroundColor;
    renderer->DrawRect(rect, bgColor, m_style.cornerRadius);

    // Draw border
    glm::vec4 borderColor = (m_state & WidgetState::Focused) ?
                            glm::vec4(0.4f, 0.6f, 1.0f, 1.0f) : m_style.borderColor;
    renderer->DrawRectOutline(rect, borderColor, m_style.borderWidth, m_style.cornerRadius);

    // Calculate text area
    glm::vec4 textArea(
        rect.x + m_style.padding.x,
        rect.y + m_style.padding.y,
        rect.z - m_style.padding.x - m_style.padding.z,
        rect.w - m_style.padding.y - m_style.padding.w
    );

    // Draw placeholder or text
    std::string displayText = m_isPassword ?
                              std::string(m_text.length(), '*') : m_text;

    if (m_text.empty() && !m_placeholder.empty() && !(m_state & WidgetState::Focused)) {
        renderer->DrawText(m_placeholder, glm::vec2(textArea.x, textArea.y),
                          glm::vec4(0.5f, 0.5f, 0.5f, 1.0f), m_style.fontName, m_style.fontSize);
    } else {
        renderer->DrawText(displayText, glm::vec2(textArea.x, textArea.y),
                          m_style.textColor, m_style.fontName, m_style.fontSize);
    }

    // Draw cursor
    if ((m_state & WidgetState::Focused) && m_showCursor) {
        float cursorX = textArea.x + m_cursorPos * m_style.fontSize * 0.6f;
        glm::vec4 cursorRect(cursorX, textArea.y, 2.0f, m_style.fontSize);
        renderer->DrawRect(cursorRect, m_style.textColor, 0.0f);
    }

    UIWidget::Render(renderer);
}

bool UITextInput::OnMouseClick(const UIMouseEvent& event) {
    Focus();
    // TODO: Calculate cursor position from click position
    return true;
}

bool UITextInput::OnKeyDown(const UIKeyEvent& event) {
    if (!(m_state & WidgetState::Focused)) return false;

    switch (event.keyCode) {
        case 8:  // Backspace
            if (m_cursorPos > 0) {
                m_text.erase(m_cursorPos - 1, 1);
                m_cursorPos--;
                if (onTextChanged) onTextChanged(m_text);
            }
            return true;

        case 127:  // Delete
            if (m_cursorPos < m_text.length()) {
                m_text.erase(m_cursorPos, 1);
                if (onTextChanged) onTextChanged(m_text);
            }
            return true;

        case 37:  // Left arrow
            MoveCursor(-1, event.shift);
            return true;

        case 39:  // Right arrow
            MoveCursor(1, event.shift);
            return true;

        case 36:  // Home
            m_cursorPos = 0;
            return true;

        case 35:  // End
            m_cursorPos = m_text.length();
            return true;

        case 13:  // Enter
            if (onSubmit) onSubmit(m_text);
            return true;
    }

    return false;
}

bool UITextInput::OnTextInput(const UITextEvent& event) {
    if (!(m_state & WidgetState::Focused)) return false;

    InsertText(event.text);
    return true;
}

void UITextInput::OnFocusGained() {
    m_showCursor = true;
    m_cursorBlinkTimer = 0.0f;
}

void UITextInput::OnFocusLost() {
    m_showCursor = false;
}

void UITextInput::InsertText(const std::string& text) {
    if (m_maxLength > 0 && m_text.length() + text.length() > m_maxLength) {
        return;
    }

    m_text.insert(m_cursorPos, text);
    m_cursorPos += text.length();
    if (onTextChanged) onTextChanged(m_text);
}

void UITextInput::DeleteSelection() {
    if (m_selectionStart != m_selectionEnd) {
        size_t start = std::min(m_selectionStart, m_selectionEnd);
        size_t end = std::max(m_selectionStart, m_selectionEnd);
        m_text.erase(start, end - start);
        m_cursorPos = start;
        m_selectionStart = m_selectionEnd = m_cursorPos;
        if (onTextChanged) onTextChanged(m_text);
    }
}

void UITextInput::MoveCursor(int delta, bool shift) {
    size_t newPos = static_cast<size_t>(std::max(0, std::min(
        static_cast<int>(m_text.length()),
        static_cast<int>(m_cursorPos) + delta
    )));
    m_cursorPos = newPos;
    if (!shift) {
        m_selectionStart = m_selectionEnd = m_cursorPos;
    }
}

// ============================================================================
// UIProgressBar
// ============================================================================

UIProgressBar::UIProgressBar(const std::string& name) : UIWidget(name) {
    m_style.backgroundColor = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f);
    m_style.cornerRadius = 4.0f;
}

void UIProgressBar::SetProgress(float progress) {
    m_progress = glm::clamp(progress, 0.0f, 1.0f);
}

glm::vec2 UIProgressBar::GetPreferredSize() const {
    return glm::vec2(200.0f, 20.0f);
}

void UIProgressBar::Render(UIRenderer* renderer) {
    if (!IsVisible()) return;

    glm::vec4 rect = GetWorldRect();

    // Draw background
    renderer->DrawRect(rect, m_style.backgroundColor, m_style.cornerRadius);

    // Draw fill
    if (m_progress > 0.0f) {
        glm::vec4 fillRect(rect.x, rect.y, rect.z * m_progress, rect.w);
        renderer->DrawRect(fillRect, m_fillColor, m_style.cornerRadius);
    }

    // Draw border
    if (m_style.borderWidth > 0) {
        renderer->DrawRectOutline(rect, m_style.borderColor, m_style.borderWidth, m_style.cornerRadius);
    }

    UIWidget::Render(renderer);
}

// ============================================================================
// UIScrollView
// ============================================================================

UIScrollView::UIScrollView(const std::string& name) : UIWidget(name) {}

void UIScrollView::SetContent(std::shared_ptr<UIWidget> content) {
    if (m_content) {
        RemoveChild(m_content);
    }
    m_content = content;
    if (m_content) {
        AddChild(m_content);
    }
}

void UIScrollView::SetScrollOffset(const glm::vec2& offset) {
    m_scrollOffset = offset;
    ClampScrollOffset();
}

void UIScrollView::Layout() {
    UIWidget::Layout();

    if (m_content) {
        m_contentSize = m_content->GetPreferredSize();

        // Position content based on scroll offset
        m_content->SetPosition(-m_scrollOffset.x, -m_scrollOffset.y);
        m_content->Layout();
    }
}

void UIScrollView::Render(UIRenderer* renderer) {
    if (!IsVisible()) return;

    glm::vec4 rect = GetWorldRect();

    // Set scissor rect for clipping
    renderer->PushScissor(rect);

    // Render content
    if (m_content) {
        m_content->Render(renderer);
    }

    renderer->PopScissor();

    // Draw scrollbars
    glm::vec4 viewRect = GetWorldRect();

    if (m_vScrollEnabled && m_contentSize.y > viewRect.w) {
        float scrollRatio = viewRect.w / m_contentSize.y;
        float thumbHeight = std::max(20.0f, viewRect.w * scrollRatio);
        float thumbY = viewRect.y + (m_scrollOffset.y / (m_contentSize.y - viewRect.w)) * (viewRect.w - thumbHeight);

        glm::vec4 trackRect(viewRect.x + viewRect.z - 8, viewRect.y, 8, viewRect.w);
        glm::vec4 thumbRect(trackRect.x + 2, thumbY, 4, thumbHeight);

        renderer->DrawRect(trackRect, glm::vec4(0.1f, 0.1f, 0.1f, 0.5f), 0);
        renderer->DrawRect(thumbRect, glm::vec4(0.5f, 0.5f, 0.5f, 0.8f), 2);
    }
}

bool UIScrollView::OnMouseWheel(const UIMouseEvent& event) {
    if (m_vScrollEnabled) {
        m_scrollOffset.y -= event.wheelDelta * 30.0f;
        ClampScrollOffset();
        return true;
    }
    return false;
}

bool UIScrollView::OnMouseDown(const UIMouseEvent& event) {
    m_isDragging = true;
    m_dragStart = event.position;
    return false;
}

bool UIScrollView::OnMouseUp(const UIMouseEvent& event) {
    m_isDragging = false;
    return false;
}

bool UIScrollView::OnMouseMove(const UIMouseEvent& event) {
    if (m_isDragging) {
        glm::vec2 delta = m_dragStart - event.position;
        m_scrollOffset += delta;
        m_dragStart = event.position;
        ClampScrollOffset();
        return true;
    }
    return false;
}

void UIScrollView::ClampScrollOffset() {
    glm::vec4 viewRect = GetWorldRect();

    float maxScrollX = std::max(0.0f, m_contentSize.x - viewRect.z);
    float maxScrollY = std::max(0.0f, m_contentSize.y - viewRect.w);

    m_scrollOffset.x = glm::clamp(m_scrollOffset.x, 0.0f, maxScrollX);
    m_scrollOffset.y = glm::clamp(m_scrollOffset.y, 0.0f, maxScrollY);
}

// ============================================================================
// UIDropdown
// ============================================================================

UIDropdown::UIDropdown(const std::string& name) : UIWidget(name) {
    m_style.backgroundColor = glm::vec4(0.25f, 0.25f, 0.25f, 1.0f);
    m_style.cornerRadius = 2.0f;
}

void UIDropdown::AddOption(const std::string& option) {
    m_options.push_back(option);
    if (m_selectedIndex < 0) {
        m_selectedIndex = 0;
    }
}

void UIDropdown::ClearOptions() {
    m_options.clear();
    m_selectedIndex = -1;
}

void UIDropdown::SetSelectedIndex(int index) {
    if (index >= -1 && index < static_cast<int>(m_options.size())) {
        m_selectedIndex = index;
        if (onSelectionChanged && m_selectedIndex >= 0) {
            onSelectionChanged(m_selectedIndex, m_options[m_selectedIndex]);
        }
    }
}

std::string UIDropdown::GetSelectedOption() const {
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_options.size())) {
        return m_options[m_selectedIndex];
    }
    return "";
}

glm::vec2 UIDropdown::GetPreferredSize() const {
    return glm::vec2(150.0f, m_style.fontSize + m_style.padding.y + m_style.padding.w);
}

void UIDropdown::Render(UIRenderer* renderer) {
    if (!IsVisible()) return;

    glm::vec4 rect = GetWorldRect();

    // Draw main box
    renderer->DrawRect(rect, m_style.GetBackgroundColor(m_state), m_style.cornerRadius);
    renderer->DrawRectOutline(rect, m_style.borderColor, 1.0f, m_style.cornerRadius);

    // Draw selected text
    std::string displayText = GetSelectedOption();
    if (displayText.empty()) displayText = "Select...";

    glm::vec2 textPos(rect.x + m_style.padding.x, rect.y + (rect.w - m_style.fontSize) * 0.5f);
    renderer->DrawText(displayText, textPos, m_style.textColor, m_style.fontName, m_style.fontSize);

    // Draw dropdown arrow
    float arrowSize = 8.0f;
    glm::vec2 arrowPos(rect.x + rect.z - arrowSize - 8, rect.y + (rect.w - arrowSize) * 0.5f);
    // Would draw triangle here

    // Draw dropdown list if open
    if (m_isOpen && !m_options.empty()) {
        float itemHeight = m_style.fontSize + 8.0f;
        glm::vec4 listRect(rect.x, rect.y + rect.w, rect.z, itemHeight * m_options.size());

        renderer->DrawRect(listRect, m_style.backgroundColor, 0);
        renderer->DrawRectOutline(listRect, m_style.borderColor, 1.0f, 0);

        for (size_t i = 0; i < m_options.size(); i++) {
            glm::vec4 itemRect(listRect.x, listRect.y + i * itemHeight, listRect.z, itemHeight);

            if (static_cast<int>(i) == m_selectedIndex) {
                renderer->DrawRect(itemRect, m_style.hoverColor, 0);
            }

            glm::vec2 itemTextPos(itemRect.x + m_style.padding.x,
                                  itemRect.y + (itemHeight - m_style.fontSize) * 0.5f);
            renderer->DrawText(m_options[i], itemTextPos, m_style.textColor,
                              m_style.fontName, m_style.fontSize);
        }
    }

    UIWidget::Render(renderer);
}

bool UIDropdown::OnMouseClick(const UIMouseEvent& event) {
    glm::vec4 rect = GetWorldRect();

    if (m_isOpen) {
        // Check if clicked on an option
        float itemHeight = m_style.fontSize + 8.0f;
        float listTop = rect.y + rect.w;

        if (event.position.y >= listTop) {
            int clickedIndex = static_cast<int>((event.position.y - listTop) / itemHeight);
            if (clickedIndex >= 0 && clickedIndex < static_cast<int>(m_options.size())) {
                SetSelectedIndex(clickedIndex);
            }
        }
        m_isOpen = false;
    } else {
        m_isOpen = true;
    }

    return true;
}

} // namespace Cortex::UI
