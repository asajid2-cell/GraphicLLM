// UISystem.cpp
// UI system manager and renderer implementation.

#include "UISystem.h"
#include <algorithm>
#include <cmath>

namespace Cortex::UI {

// ============================================================================
// UIBatchRenderer Implementation
// ============================================================================

UIBatchRenderer::UIBatchRenderer() {
    m_commands.reserve(1024);
    m_transformStack.push(glm::mat4(1.0f));
}

void UIBatchRenderer::BeginFrame() {
    m_commands.clear();
    while (!m_scissorStack.empty()) m_scissorStack.pop();
    while (!m_transformStack.empty()) m_transformStack.pop();
    m_transformStack.push(glm::mat4(1.0f));
}

void UIBatchRenderer::EndFrame() {
    // Commands are ready for GPU submission
}

void UIBatchRenderer::DrawRect(const glm::vec4& rect, const glm::vec4& color, float cornerRadius) {
    UIRenderCommand cmd;
    cmd.type = UIRenderCommandType::Rect;
    cmd.rect = rect;
    cmd.color = color;
    cmd.cornerRadius = cornerRadius;
    cmd.transform = m_transformStack.top();
    m_commands.push_back(cmd);
}

void UIBatchRenderer::DrawRectOutline(const glm::vec4& rect, const glm::vec4& color, float width, float cornerRadius) {
    UIRenderCommand cmd;
    cmd.type = UIRenderCommandType::RectOutline;
    cmd.rect = rect;
    cmd.color = color;
    cmd.lineWidth = width;
    cmd.cornerRadius = cornerRadius;
    cmd.transform = m_transformStack.top();
    m_commands.push_back(cmd);
}

void UIBatchRenderer::DrawImage(const glm::vec4& rect, uint32_t textureId, const glm::vec4& color, const glm::vec4& uvRect) {
    UIRenderCommand cmd;
    cmd.type = UIRenderCommandType::Image;
    cmd.rect = rect;
    cmd.textureId = textureId;
    cmd.color = color;
    cmd.uvRect = uvRect;
    cmd.transform = m_transformStack.top();
    m_commands.push_back(cmd);
}

void UIBatchRenderer::DrawImage9Slice(const glm::vec4& rect, uint32_t textureId, const glm::vec4& borders, const glm::vec4& color) {
    UIRenderCommand cmd;
    cmd.type = UIRenderCommandType::Image9Slice;
    cmd.rect = rect;
    cmd.textureId = textureId;
    cmd.sliceBorders = borders;
    cmd.color = color;
    cmd.transform = m_transformStack.top();
    m_commands.push_back(cmd);
}

void UIBatchRenderer::DrawText(const std::string& text, const glm::vec2& position, const glm::vec4& color,
                                const std::string& fontName, float fontSize) {
    UIRenderCommand cmd;
    cmd.type = UIRenderCommandType::Text;
    cmd.text = text;
    cmd.position = position;
    cmd.color = color;
    cmd.fontName = fontName;
    cmd.fontSize = fontSize;
    cmd.transform = m_transformStack.top();
    m_commands.push_back(cmd);
}

void UIBatchRenderer::DrawLine(const glm::vec2& start, const glm::vec2& end, const glm::vec4& color, float width) {
    UIRenderCommand cmd;
    cmd.type = UIRenderCommandType::Line;
    cmd.lineStart = start;
    cmd.lineEnd = end;
    cmd.color = color;
    cmd.lineWidth = width;
    cmd.transform = m_transformStack.top();
    m_commands.push_back(cmd);
}

void UIBatchRenderer::DrawCircle(const glm::vec2& center, float radius, const glm::vec4& color, bool filled) {
    UIRenderCommand cmd;
    cmd.type = UIRenderCommandType::Circle;
    cmd.position = center;
    cmd.cornerRadius = radius;  // Reuse cornerRadius for circle radius
    cmd.color = color;
    cmd.lineWidth = filled ? 0.0f : 1.0f;  // 0 = filled, >0 = outline
    cmd.transform = m_transformStack.top();
    m_commands.push_back(cmd);
}

void UIBatchRenderer::PushScissor(const glm::vec4& rect) {
    UIRenderCommand cmd;
    cmd.type = UIRenderCommandType::PushScissor;
    cmd.scissorRect = rect;
    m_commands.push_back(cmd);
    m_scissorStack.push(rect);
}

void UIBatchRenderer::PopScissor() {
    if (!m_scissorStack.empty()) {
        UIRenderCommand cmd;
        cmd.type = UIRenderCommandType::PopScissor;
        m_commands.push_back(cmd);
        m_scissorStack.pop();
    }
}

void UIBatchRenderer::PushTransform(const glm::mat4& transform) {
    UIRenderCommand cmd;
    cmd.type = UIRenderCommandType::PushTransform;
    cmd.transform = transform;
    m_commands.push_back(cmd);
    m_transformStack.push(m_transformStack.top() * transform);
}

void UIBatchRenderer::PopTransform() {
    if (m_transformStack.size() > 1) {
        UIRenderCommand cmd;
        cmd.type = UIRenderCommandType::PopTransform;
        m_commands.push_back(cmd);
        m_transformStack.pop();
    }
}

// ============================================================================
// UICanvas Implementation
// ============================================================================

UICanvas::UICanvas(const std::string& name) : UIWidget(name) {
    // Canvas fills entire screen by default
    m_rectTransform.anchorMin = glm::vec2(0.0f);
    m_rectTransform.anchorMax = glm::vec2(1.0f);
    m_rectTransform.offsetMin = glm::vec2(0.0f);
    m_rectTransform.offsetMax = glm::vec2(0.0f);
}

void UICanvas::Render(UIRenderer* renderer) {
    if (!IsVisible()) return;

    // Calculate scale factor based on screen size
    glm::vec2 screenSize = renderer->GetScreenSize();

    if (m_scaleMode == 1 && m_referenceResolution.x > 0 && m_referenceResolution.y > 0) {
        float widthRatio = screenSize.x / m_referenceResolution.x;
        float heightRatio = screenSize.y / m_referenceResolution.y;
        m_scaleFactor = std::min(widthRatio, heightRatio);
    } else {
        m_scaleFactor = 1.0f;
    }

    // Update canvas size
    SetSize(screenSize);

    // Apply scale transform
    if (m_scaleFactor != 1.0f) {
        glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(m_scaleFactor, m_scaleFactor, 1.0f));
        renderer->PushTransform(scaleMatrix);
    }

    // Render children
    UIWidget::Render(renderer);

    if (m_scaleFactor != 1.0f) {
        renderer->PopTransform();
    }
}

void UICanvas::Layout() {
    // Canvas layout is performed in screen space
    UIWidget::Layout();
}

// ============================================================================
// UISystem Implementation
// ============================================================================

UISystem::UISystem() {
    // Initialize default style
    m_defaultStyle.backgroundColor = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f);
    m_defaultStyle.textColor = glm::vec4(1.0f);
    m_defaultStyle.borderColor = glm::vec4(0.4f, 0.4f, 0.4f, 1.0f);
    m_defaultStyle.highlightColor = glm::vec4(0.3f, 0.5f, 0.8f, 1.0f);
    m_defaultStyle.disabledColor = glm::vec4(0.5f, 0.5f, 0.5f, 0.5f);
    m_defaultStyle.borderWidth = 1.0f;
    m_defaultStyle.cornerRadius = 4.0f;
    m_defaultStyle.padding = glm::vec4(8.0f);
    m_defaultStyle.fontSize = 14.0f;
    m_defaultStyle.fontName = "default";
}

UISystem::~UISystem() {
    Shutdown();
}

bool UISystem::Initialize(UIRenderer* renderer) {
    if (!renderer) return false;
    m_renderer = renderer;
    return true;
}

void UISystem::Shutdown() {
    m_canvases.clear();
    m_modalStack.clear();
    m_hoveredWidget = nullptr;
    m_focusedWidget = nullptr;
    m_pressedWidget = nullptr;
    m_renderer = nullptr;
}

std::shared_ptr<UICanvas> UISystem::CreateCanvas(const std::string& name) {
    auto canvas = std::make_shared<UICanvas>(name);
    canvas->SetSize(m_screenSize);
    m_canvases.push_back(canvas);
    return canvas;
}

void UISystem::DestroyCanvas(std::shared_ptr<UICanvas> canvas) {
    auto it = std::find(m_canvases.begin(), m_canvases.end(), canvas);
    if (it != m_canvases.end()) {
        m_canvases.erase(it);
    }
}

void UISystem::Update(float deltaTime) {
    // Update tooltip timer
    if (m_hoveredWidget && !m_tooltipVisible) {
        m_tooltipTimer += deltaTime;
        if (m_tooltipTimer >= 1.0f) {  // 1 second delay
            const std::string& tooltip = m_hoveredWidget->GetTooltip();
            if (!tooltip.empty()) {
                ShowTooltip(tooltip, m_mousePosition + glm::vec2(16.0f, 16.0f));
            }
        }
    }

    // Update all canvases
    for (auto& canvas : m_canvases) {
        canvas->Update(deltaTime);
    }

    // Update modals
    for (auto& modal : m_modalStack) {
        modal->Update(deltaTime);
    }
}

void UISystem::Render() {
    if (!m_renderer) return;

    m_renderer->BeginFrame();

    // Render canvases in order
    for (auto& canvas : m_canvases) {
        canvas->Render(m_renderer);
    }

    // Render modals on top
    for (auto& modal : m_modalStack) {
        // Darken background
        m_renderer->DrawRect(glm::vec4(0, 0, m_screenSize.x, m_screenSize.y),
                             glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));
        modal->Render(m_renderer);
    }

    // Render tooltip
    if (m_tooltipVisible && !m_tooltipText.empty()) {
        RenderTooltip();
    }

    // Debug drawing
    if (m_debugDraw) {
        RenderDebug();
    }

    m_renderer->EndFrame();
}

void UISystem::RenderTooltip() {
    if (!m_renderer) return;

    // Tooltip style
    glm::vec4 bgColor(0.1f, 0.1f, 0.1f, 0.95f);
    glm::vec4 borderColor(0.3f, 0.3f, 0.3f, 1.0f);
    glm::vec4 textColor(1.0f, 1.0f, 1.0f, 1.0f);

    // Calculate tooltip size (simplified - would use font metrics)
    float textWidth = static_cast<float>(m_tooltipText.length()) * 7.0f;  // Rough estimate
    float textHeight = 16.0f;
    float padding = 8.0f;

    float width = textWidth + padding * 2;
    float height = textHeight + padding * 2;

    // Clamp to screen bounds
    glm::vec2 pos = m_tooltipPosition;
    if (pos.x + width > m_screenSize.x) {
        pos.x = m_screenSize.x - width;
    }
    if (pos.y + height > m_screenSize.y) {
        pos.y = m_screenSize.y - height;
    }

    // Draw background
    glm::vec4 rect(pos.x, pos.y, width, height);
    m_renderer->DrawRect(rect, bgColor, 4.0f);
    m_renderer->DrawRectOutline(rect, borderColor, 1.0f, 4.0f);

    // Draw text
    m_renderer->DrawText(m_tooltipText, pos + glm::vec2(padding), textColor, "default", 12.0f);
}

void UISystem::RenderDebug() {
    if (!m_renderer) return;

    // Draw bounds of hovered widget
    if (m_hoveredWidget) {
        glm::vec4 bounds = m_hoveredWidget->GetWorldBounds();
        m_renderer->DrawRectOutline(bounds, glm::vec4(0.0f, 1.0f, 0.0f, 0.5f), 2.0f);
    }

    // Draw bounds of focused widget
    if (m_focusedWidget) {
        glm::vec4 bounds = m_focusedWidget->GetWorldBounds();
        m_renderer->DrawRectOutline(bounds, glm::vec4(0.0f, 0.0f, 1.0f, 0.5f), 2.0f);
    }
}

void UISystem::OnMouseMove(float x, float y) {
    m_lastMousePosition = m_mousePosition;
    m_mousePosition = glm::vec2(x, y);

    // Calculate delta
    glm::vec2 delta = m_mousePosition - m_lastMousePosition;

    // If dragging a widget
    if (m_pressedWidget) {
        MouseEvent event;
        event.position = m_mousePosition;
        event.delta = delta;
        event.button = MouseButton::Left;
        event.isDown = true;
        m_pressedWidget->OnMouseMove(event);
        return;
    }

    // Hit test for hover
    UIWidget* hitWidget = HitTest(x, y);
    UpdateHovered(hitWidget);

    // Send mouse move to hovered widget
    if (m_hoveredWidget) {
        MouseEvent event;
        event.position = m_mousePosition;
        event.delta = delta;
        m_hoveredWidget->OnMouseMove(event);
    }
}

void UISystem::OnMouseButton(MouseButton button, bool isDown) {
    int buttonIndex = static_cast<int>(button);
    if (buttonIndex >= 0 && buttonIndex < 3) {
        m_mouseButtons[buttonIndex] = isDown;
    }

    MouseEvent event;
    event.position = m_mousePosition;
    event.button = button;
    event.isDown = isDown;

    if (isDown) {
        // Mouse down
        UIWidget* hitWidget = HitTest(m_mousePosition.x, m_mousePosition.y);

        // Handle modals - clicks outside modal do nothing
        if (!m_modalStack.empty()) {
            bool hitModal = false;
            for (auto& modal : m_modalStack) {
                if (HitTestRecursive(modal.get(), m_mousePosition)) {
                    hitModal = true;
                    break;
                }
            }
            if (!hitModal) {
                return;  // Ignore click outside modal
            }
        }

        if (hitWidget) {
            m_pressedWidget = hitWidget;
            SetFocus(hitWidget);
            hitWidget->OnMouseDown(event);
        } else {
            ClearFocus();
        }
    } else {
        // Mouse up
        if (m_pressedWidget) {
            m_pressedWidget->OnMouseUp(event);

            // Check if still over same widget (click)
            UIWidget* hitWidget = HitTest(m_mousePosition.x, m_mousePosition.y);
            if (hitWidget == m_pressedWidget) {
                m_pressedWidget->OnClick(event);
            }

            m_pressedWidget = nullptr;
        }
    }
}

void UISystem::OnMouseWheel(float delta) {
    MouseEvent event;
    event.position = m_mousePosition;
    event.scrollDelta = delta;

    // Send to focused widget first, then hovered
    if (m_focusedWidget) {
        m_focusedWidget->OnMouseWheel(event);
    } else if (m_hoveredWidget) {
        m_hoveredWidget->OnMouseWheel(event);
    }
}

void UISystem::OnKeyEvent(int keyCode, int scanCode, bool isDown, bool isRepeat) {
    KeyEvent event;
    event.keyCode = keyCode;
    event.scanCode = scanCode;
    event.isDown = isDown;
    event.isRepeat = isRepeat;

    if (m_focusedWidget) {
        if (isDown) {
            m_focusedWidget->OnKeyDown(event);
        } else {
            m_focusedWidget->OnKeyUp(event);
        }
    }
}

void UISystem::OnTextInput(const std::string& text) {
    if (m_focusedWidget) {
        m_focusedWidget->OnTextInput(text);
    }
}

void UISystem::SetFocus(UIWidget* widget) {
    if (m_focusedWidget == widget) return;

    if (m_focusedWidget) {
        m_focusedWidget->OnFocusLost();
    }

    m_focusedWidget = widget;

    if (m_focusedWidget) {
        m_focusedWidget->OnFocusGained();
    }
}

void UISystem::ClearFocus() {
    SetFocus(nullptr);
}

UIWidget* UISystem::HitTest(float x, float y) {
    glm::vec2 point(x, y);

    // Test modals first (top to bottom)
    for (auto it = m_modalStack.rbegin(); it != m_modalStack.rend(); ++it) {
        UIWidget* hit = HitTestRecursive(it->get(), point);
        if (hit) return hit;
    }

    // Test canvases (back to front for proper ordering)
    for (auto it = m_canvases.rbegin(); it != m_canvases.rend(); ++it) {
        UIWidget* hit = HitTestRecursive(it->get(), point);
        if (hit) return hit;
    }

    return nullptr;
}

UIWidget* UISystem::HitTestCanvas(UICanvas* canvas, float x, float y) {
    if (!canvas) return nullptr;
    return HitTestRecursive(canvas, glm::vec2(x, y));
}

UIWidget* UISystem::HitTestRecursive(UIWidget* widget, const glm::vec2& point) {
    if (!widget || !widget->IsVisible() || !widget->IsInteractable()) {
        return nullptr;
    }

    // Test children first (front to back for proper z-order)
    const auto& children = widget->GetChildren();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        UIWidget* hit = HitTestRecursive(it->get(), point);
        if (hit) return hit;
    }

    // Test this widget
    if (widget->HitTest(point)) {
        return widget;
    }

    return nullptr;
}

void UISystem::UpdateHovered(UIWidget* widget) {
    if (m_hoveredWidget == widget) return;

    // Mouse leave old widget
    if (m_hoveredWidget) {
        MouseEvent event;
        event.position = m_mousePosition;
        m_hoveredWidget->OnMouseLeave(event);
        m_hoveredWidget->SetState(WidgetState::Normal);
    }

    m_hoveredWidget = widget;

    // Mouse enter new widget
    if (m_hoveredWidget) {
        MouseEvent event;
        event.position = m_mousePosition;
        m_hoveredWidget->OnMouseEnter(event);
        m_hoveredWidget->SetState(WidgetState::Hovered);
    }

    // Reset tooltip
    HideTooltip();
    m_tooltipTimer = 0.0f;
}

void UISystem::PushModal(std::shared_ptr<UIWidget> widget) {
    if (widget) {
        m_modalStack.push_back(widget);
    }
}

void UISystem::PopModal() {
    if (!m_modalStack.empty()) {
        m_modalStack.pop_back();
    }
}

void UISystem::ShowTooltip(const std::string& text, const glm::vec2& position) {
    m_tooltipText = text;
    m_tooltipPosition = position;
    m_tooltipVisible = true;
}

void UISystem::HideTooltip() {
    m_tooltipVisible = false;
}

void UISystem::SetCursor(int cursorType) {
    // Platform-specific cursor setting would go here
    // 0=default, 1=pointer, 2=text, 3=resize, etc.
}

void UISystem::SetScreenSize(float width, float height) {
    m_screenSize = glm::vec2(width, height);

    // Update all canvases
    for (auto& canvas : m_canvases) {
        canvas->SetSize(m_screenSize);
        canvas->Layout();
    }
}

// ============================================================================
// UILayout Namespace
// ============================================================================

namespace UILayout {

void LayoutHorizontal(UIWidget* container, float spacing, bool expandChildren) {
    if (!container) return;

    const auto& children = container->GetChildren();
    if (children.empty()) return;

    glm::vec2 containerSize = container->GetSize();
    glm::vec4 padding = container->GetStyle().padding;

    float availableWidth = containerSize.x - padding.x - padding.z;
    float availableHeight = containerSize.y - padding.y - padding.w;

    float totalSpacing = spacing * (static_cast<float>(children.size()) - 1);
    float contentWidth = availableWidth - totalSpacing;

    float x = padding.x;
    float y = padding.y;
    float childWidth = expandChildren ? contentWidth / static_cast<float>(children.size()) : 0.0f;

    for (auto& child : children) {
        if (!child->IsVisible()) continue;

        glm::vec2 childSize = child->GetSize();
        if (expandChildren) {
            childSize.x = childWidth;
            childSize.y = availableHeight;
        }

        child->SetPosition(glm::vec2(x, y));
        child->SetSize(childSize);

        x += childSize.x + spacing;
    }
}

void LayoutVertical(UIWidget* container, float spacing, bool expandChildren) {
    if (!container) return;

    const auto& children = container->GetChildren();
    if (children.empty()) return;

    glm::vec2 containerSize = container->GetSize();
    glm::vec4 padding = container->GetStyle().padding;

    float availableWidth = containerSize.x - padding.x - padding.z;
    float availableHeight = containerSize.y - padding.y - padding.w;

    float totalSpacing = spacing * (static_cast<float>(children.size()) - 1);
    float contentHeight = availableHeight - totalSpacing;

    float x = padding.x;
    float y = padding.y;
    float childHeight = expandChildren ? contentHeight / static_cast<float>(children.size()) : 0.0f;

    for (auto& child : children) {
        if (!child->IsVisible()) continue;

        glm::vec2 childSize = child->GetSize();
        if (expandChildren) {
            childSize.x = availableWidth;
            childSize.y = childHeight;
        }

        child->SetPosition(glm::vec2(x, y));
        child->SetSize(childSize);

        y += childSize.y + spacing;
    }
}

void LayoutGrid(UIWidget* container, int columns, float hSpacing, float vSpacing) {
    if (!container || columns <= 0) return;

    const auto& children = container->GetChildren();
    if (children.empty()) return;

    glm::vec2 containerSize = container->GetSize();
    glm::vec4 padding = container->GetStyle().padding;

    float availableWidth = containerSize.x - padding.x - padding.z;
    float availableHeight = containerSize.y - padding.y - padding.w;

    float totalHSpacing = hSpacing * (static_cast<float>(columns) - 1);
    float cellWidth = (availableWidth - totalHSpacing) / static_cast<float>(columns);

    int rows = (static_cast<int>(children.size()) + columns - 1) / columns;
    float totalVSpacing = vSpacing * (static_cast<float>(rows) - 1);
    float cellHeight = (availableHeight - totalVSpacing) / static_cast<float>(rows);

    int index = 0;
    for (auto& child : children) {
        if (!child->IsVisible()) {
            continue;
        }

        int col = index % columns;
        int row = index / columns;

        float x = padding.x + static_cast<float>(col) * (cellWidth + hSpacing);
        float y = padding.y + static_cast<float>(row) * (cellHeight + vSpacing);

        child->SetPosition(glm::vec2(x, y));
        child->SetSize(glm::vec2(cellWidth, cellHeight));

        index++;
    }
}

glm::vec2 FitToContent(UIWidget* container, const glm::vec4& padding) {
    if (!container) return glm::vec2(0.0f);

    const auto& children = container->GetChildren();
    if (children.empty()) {
        return glm::vec2(padding.x + padding.z, padding.y + padding.w);
    }

    float maxX = 0.0f;
    float maxY = 0.0f;

    for (const auto& child : children) {
        if (!child->IsVisible()) continue;

        glm::vec2 childPos = child->GetPosition();
        glm::vec2 childSize = child->GetSize();

        maxX = std::max(maxX, childPos.x + childSize.x);
        maxY = std::max(maxY, childPos.y + childSize.y);
    }

    glm::vec2 contentSize(maxX + padding.z, maxY + padding.w);
    container->SetSize(contentSize);
    return contentSize;
}

} // namespace UILayout

// ============================================================================
// UIFactory Namespace
// ============================================================================

namespace UIFactory {

std::shared_ptr<UIButton> CreateButton(const std::string& text, std::function<void()> onClick) {
    auto button = std::make_shared<UIButton>(text);
    button->SetText(text);
    button->SetSize(glm::vec2(120.0f, 32.0f));

    if (onClick) {
        button->SetOnClick([onClick](const MouseEvent&) {
            onClick();
        });
    }

    return button;
}

std::shared_ptr<UIText> CreateLabel(const std::string& text) {
    auto label = std::make_shared<UIText>(text);
    label->SetText(text);
    return label;
}

std::shared_ptr<UITextInput> CreateTextInput(const std::string& placeholder) {
    auto input = std::make_shared<UITextInput>("TextInput");
    input->SetPlaceholder(placeholder);
    input->SetSize(glm::vec2(200.0f, 28.0f));
    return input;
}

std::shared_ptr<UISlider> CreateSlider(float min, float max, float value) {
    auto slider = std::make_shared<UISlider>("Slider");
    slider->SetRange(min, max);
    slider->SetValue(value);
    slider->SetSize(glm::vec2(150.0f, 20.0f));
    return slider;
}

std::shared_ptr<UICheckbox> CreateCheckbox(const std::string& label, bool checked) {
    auto checkbox = std::make_shared<UICheckbox>(label);
    checkbox->SetLabel(label);
    checkbox->SetChecked(checked);
    return checkbox;
}

std::shared_ptr<UIProgressBar> CreateProgressBar(float progress) {
    auto bar = std::make_shared<UIProgressBar>("ProgressBar");
    bar->SetProgress(progress);
    bar->SetSize(glm::vec2(200.0f, 20.0f));
    return bar;
}

std::shared_ptr<UIPanel> CreatePanel() {
    auto panel = std::make_shared<UIPanel>("Panel");
    panel->SetSize(glm::vec2(300.0f, 200.0f));
    return panel;
}

std::shared_ptr<UIImage> CreateImage(uint32_t textureId) {
    auto image = std::make_shared<UIImage>("Image");
    image->SetTextureId(textureId);
    image->SetSize(glm::vec2(64.0f, 64.0f));
    return image;
}

std::shared_ptr<UIWidget> CreateLabeledSlider(const std::string& label, float min, float max, float value) {
    auto container = std::make_shared<UIPanel>("LabeledSlider");
    container->SetSize(glm::vec2(250.0f, 24.0f));

    auto labelWidget = CreateLabel(label);
    labelWidget->SetSize(glm::vec2(80.0f, 24.0f));
    container->AddChild(labelWidget);

    auto slider = CreateSlider(min, max, value);
    slider->SetPosition(glm::vec2(90.0f, 0.0f));
    slider->SetSize(glm::vec2(150.0f, 24.0f));
    container->AddChild(slider);

    return container;
}

std::shared_ptr<UIWidget> CreateColorPicker() {
    auto container = std::make_shared<UIPanel>("ColorPicker");
    container->SetSize(glm::vec2(200.0f, 180.0f));

    // Color preview
    auto preview = std::make_shared<UIPanel>("Preview");
    preview->SetSize(glm::vec2(180.0f, 40.0f));
    preview->SetPosition(glm::vec2(10.0f, 10.0f));
    container->AddChild(preview);

    // RGB sliders
    auto rSlider = CreateLabeledSlider("R", 0.0f, 1.0f, 1.0f);
    rSlider->SetPosition(glm::vec2(10.0f, 60.0f));
    container->AddChild(rSlider);

    auto gSlider = CreateLabeledSlider("G", 0.0f, 1.0f, 1.0f);
    gSlider->SetPosition(glm::vec2(10.0f, 90.0f));
    container->AddChild(gSlider);

    auto bSlider = CreateLabeledSlider("B", 0.0f, 1.0f, 1.0f);
    bSlider->SetPosition(glm::vec2(10.0f, 120.0f));
    container->AddChild(bSlider);

    auto aSlider = CreateLabeledSlider("A", 0.0f, 1.0f, 1.0f);
    aSlider->SetPosition(glm::vec2(10.0f, 150.0f));
    container->AddChild(aSlider);

    return container;
}

std::shared_ptr<UIWidget> CreateMessageBox(const std::string& title, const std::string& message,
                                            const std::vector<std::string>& buttons) {
    auto dialog = std::make_shared<UIPanel>("MessageBox");
    dialog->SetSize(glm::vec2(350.0f, 150.0f));

    // Title bar
    auto titleBar = std::make_shared<UIPanel>("TitleBar");
    titleBar->SetSize(glm::vec2(350.0f, 30.0f));
    UIStyle titleStyle;
    titleStyle.backgroundColor = glm::vec4(0.3f, 0.3f, 0.3f, 1.0f);
    titleBar->SetStyle(titleStyle);
    dialog->AddChild(titleBar);

    auto titleLabel = CreateLabel(title);
    titleLabel->SetPosition(glm::vec2(10.0f, 5.0f));
    titleBar->AddChild(titleLabel);

    // Message
    auto messageLabel = CreateLabel(message);
    messageLabel->SetPosition(glm::vec2(20.0f, 50.0f));
    messageLabel->SetSize(glm::vec2(310.0f, 50.0f));
    dialog->AddChild(messageLabel);

    // Buttons
    float buttonWidth = 80.0f;
    float buttonSpacing = 10.0f;
    float totalButtonWidth = static_cast<float>(buttons.size()) * buttonWidth +
                             (static_cast<float>(buttons.size()) - 1) * buttonSpacing;
    float startX = (350.0f - totalButtonWidth) / 2.0f;

    for (size_t i = 0; i < buttons.size(); ++i) {
        auto button = CreateButton(buttons[i]);
        button->SetSize(glm::vec2(buttonWidth, 28.0f));
        button->SetPosition(glm::vec2(startX + static_cast<float>(i) * (buttonWidth + buttonSpacing), 110.0f));
        dialog->AddChild(button);
    }

    return dialog;
}

} // namespace UIFactory

} // namespace Cortex::UI
