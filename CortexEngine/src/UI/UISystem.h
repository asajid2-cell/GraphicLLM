#pragma once

// UISystem.h
// UI system manager and renderer for retained-mode UI.
// Handles input routing, focus management, and rendering.

#include "UIWidget.h"
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <functional>
#include <stack>

namespace Cortex::UI {

// Forward declarations
class UICanvas;
class FontRenderer;

// Render command types
enum class UIRenderCommandType : uint8_t {
    Rect,
    RectOutline,
    Image,
    Image9Slice,
    Text,
    Line,
    Circle,
    PushScissor,
    PopScissor,
    PushTransform,
    PopTransform
};

// Render command data
struct UIRenderCommand {
    UIRenderCommandType type;

    // Common
    glm::vec4 rect;
    glm::vec4 color;
    float cornerRadius = 0.0f;

    // Image specific
    uint32_t textureId = 0;
    glm::vec4 uvRect = glm::vec4(0, 0, 1, 1);
    glm::vec4 sliceBorders;

    // Text specific
    std::string text;
    std::string fontName;
    float fontSize = 14.0f;
    glm::vec2 position;

    // Line specific
    glm::vec2 lineStart;
    glm::vec2 lineEnd;
    float lineWidth = 1.0f;

    // Transform
    glm::mat4 transform;

    // Scissor
    glm::vec4 scissorRect;
};

// UI renderer interface (implemented by graphics backend)
class UIRenderer {
public:
    virtual ~UIRenderer() = default;

    // Begin/end frame
    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;

    // Immediate mode rendering
    virtual void DrawRect(const glm::vec4& rect, const glm::vec4& color, float cornerRadius = 0.0f) = 0;
    virtual void DrawRectOutline(const glm::vec4& rect, const glm::vec4& color, float width = 1.0f, float cornerRadius = 0.0f) = 0;
    virtual void DrawImage(const glm::vec4& rect, uint32_t textureId, const glm::vec4& color = glm::vec4(1.0f), const glm::vec4& uvRect = glm::vec4(0, 0, 1, 1)) = 0;
    virtual void DrawImage9Slice(const glm::vec4& rect, uint32_t textureId, const glm::vec4& borders, const glm::vec4& color = glm::vec4(1.0f)) = 0;
    virtual void DrawText(const std::string& text, const glm::vec2& position, const glm::vec4& color, const std::string& fontName = "default", float fontSize = 14.0f) = 0;
    virtual void DrawLine(const glm::vec2& start, const glm::vec2& end, const glm::vec4& color, float width = 1.0f) = 0;
    virtual void DrawCircle(const glm::vec2& center, float radius, const glm::vec4& color, bool filled = true) = 0;

    // Scissor/clipping
    virtual void PushScissor(const glm::vec4& rect) = 0;
    virtual void PopScissor() = 0;

    // Transform
    virtual void PushTransform(const glm::mat4& transform) = 0;
    virtual void PopTransform() = 0;

    // Screen info
    virtual glm::vec2 GetScreenSize() const = 0;

    // Font access
    virtual FontRenderer* GetFontRenderer() = 0;
};

// Batched UI renderer (command buffer based)
class UIBatchRenderer : public UIRenderer {
public:
    UIBatchRenderer();
    ~UIBatchRenderer() override = default;

    void BeginFrame() override;
    void EndFrame() override;

    void DrawRect(const glm::vec4& rect, const glm::vec4& color, float cornerRadius = 0.0f) override;
    void DrawRectOutline(const glm::vec4& rect, const glm::vec4& color, float width = 1.0f, float cornerRadius = 0.0f) override;
    void DrawImage(const glm::vec4& rect, uint32_t textureId, const glm::vec4& color = glm::vec4(1.0f), const glm::vec4& uvRect = glm::vec4(0, 0, 1, 1)) override;
    void DrawImage9Slice(const glm::vec4& rect, uint32_t textureId, const glm::vec4& borders, const glm::vec4& color = glm::vec4(1.0f)) override;
    void DrawText(const std::string& text, const glm::vec2& position, const glm::vec4& color, const std::string& fontName = "default", float fontSize = 14.0f) override;
    void DrawLine(const glm::vec2& start, const glm::vec2& end, const glm::vec4& color, float width = 1.0f) override;
    void DrawCircle(const glm::vec2& center, float radius, const glm::vec4& color, bool filled = true) override;

    void PushScissor(const glm::vec4& rect) override;
    void PopScissor() override;

    void PushTransform(const glm::mat4& transform) override;
    void PopTransform() override;

    glm::vec2 GetScreenSize() const override { return m_screenSize; }
    void SetScreenSize(const glm::vec2& size) { m_screenSize = size; }

    FontRenderer* GetFontRenderer() override { return m_fontRenderer.get(); }
    void SetFontRenderer(std::shared_ptr<FontRenderer> renderer) { m_fontRenderer = renderer; }

    // Access command buffer for GPU submission
    const std::vector<UIRenderCommand>& GetCommands() const { return m_commands; }

private:
    std::vector<UIRenderCommand> m_commands;
    std::stack<glm::vec4> m_scissorStack;
    std::stack<glm::mat4> m_transformStack;
    glm::vec2 m_screenSize = glm::vec2(1920, 1080);
    std::shared_ptr<FontRenderer> m_fontRenderer;
};

// Canvas render mode
enum class CanvasRenderMode : uint8_t {
    ScreenSpace,        // Rendered in screen space
    WorldSpace,         // Rendered in 3D world
    Camera              // Rendered relative to camera
};

// UI Canvas (root container)
class UICanvas : public UIWidget {
public:
    UICanvas(const std::string& name = "Canvas");

    void SetRenderMode(CanvasRenderMode mode) { m_renderMode = mode; }
    CanvasRenderMode GetRenderMode() const { return m_renderMode; }

    void SetReferenceResolution(const glm::vec2& resolution) { m_referenceResolution = resolution; }
    glm::vec2 GetReferenceResolution() const { return m_referenceResolution; }

    void SetScaleMode(int mode) { m_scaleMode = mode; }  // 0=constant, 1=scale with screen

    // Render the canvas
    void Render(UIRenderer* renderer) override;
    void Layout() override;

    // Scale factor for UI scaling
    float GetScaleFactor() const { return m_scaleFactor; }

private:
    CanvasRenderMode m_renderMode = CanvasRenderMode::ScreenSpace;
    glm::vec2 m_referenceResolution = glm::vec2(1920, 1080);
    int m_scaleMode = 1;
    float m_scaleFactor = 1.0f;
};

// UI System manager
class UISystem {
public:
    UISystem();
    ~UISystem();

    // Initialize/shutdown
    bool Initialize(UIRenderer* renderer);
    void Shutdown();

    // Create canvas
    std::shared_ptr<UICanvas> CreateCanvas(const std::string& name = "Canvas");
    void DestroyCanvas(std::shared_ptr<UICanvas> canvas);
    const std::vector<std::shared_ptr<UICanvas>>& GetCanvases() const { return m_canvases; }

    // Update and render
    void Update(float deltaTime);
    void Render();

    // Input routing
    void OnMouseMove(float x, float y);
    void OnMouseButton(MouseButton button, bool isDown);
    void OnMouseWheel(float delta);
    void OnKeyEvent(int keyCode, int scanCode, bool isDown, bool isRepeat);
    void OnTextInput(const std::string& text);

    // Focus management
    void SetFocus(UIWidget* widget);
    UIWidget* GetFocusedWidget() const { return m_focusedWidget; }
    void ClearFocus();

    // Hit testing
    UIWidget* HitTest(float x, float y);
    UIWidget* HitTestCanvas(UICanvas* canvas, float x, float y);

    // Modal dialogs
    void PushModal(std::shared_ptr<UIWidget> widget);
    void PopModal();
    bool HasModal() const { return !m_modalStack.empty(); }

    // Tooltips
    void ShowTooltip(const std::string& text, const glm::vec2& position);
    void HideTooltip();

    // Cursor
    void SetCursor(int cursorType);  // 0=default, 1=pointer, 2=text, etc.

    // Debug
    void SetDebugDraw(bool enabled) { m_debugDraw = enabled; }
    bool IsDebugDraw() const { return m_debugDraw; }

    // Theme/styling
    void SetDefaultStyle(const UIStyle& style) { m_defaultStyle = style; }
    const UIStyle& GetDefaultStyle() const { return m_defaultStyle; }

    // Screen size
    void SetScreenSize(float width, float height);
    glm::vec2 GetScreenSize() const { return m_screenSize; }

private:
    UIWidget* HitTestRecursive(UIWidget* widget, const glm::vec2& point);
    void UpdateHovered(UIWidget* widget);

    UIRenderer* m_renderer = nullptr;
    std::vector<std::shared_ptr<UICanvas>> m_canvases;

    // Input state
    glm::vec2 m_mousePosition = glm::vec2(0.0f);
    glm::vec2 m_lastMousePosition = glm::vec2(0.0f);
    bool m_mouseButtons[3] = {false, false, false};

    // Widget tracking
    UIWidget* m_hoveredWidget = nullptr;
    UIWidget* m_focusedWidget = nullptr;
    UIWidget* m_pressedWidget = nullptr;

    // Modal stack
    std::vector<std::shared_ptr<UIWidget>> m_modalStack;

    // Tooltip
    std::string m_tooltipText;
    glm::vec2 m_tooltipPosition;
    bool m_tooltipVisible = false;
    float m_tooltipTimer = 0.0f;

    // Settings
    glm::vec2 m_screenSize = glm::vec2(1920, 1080);
    UIStyle m_defaultStyle;
    bool m_debugDraw = false;
};

// Layout helpers
namespace UILayout {

// Horizontal layout (arrange children left to right)
void LayoutHorizontal(UIWidget* container, float spacing = 4.0f, bool expandChildren = false);

// Vertical layout (arrange children top to bottom)
void LayoutVertical(UIWidget* container, float spacing = 4.0f, bool expandChildren = false);

// Grid layout
void LayoutGrid(UIWidget* container, int columns, float hSpacing = 4.0f, float vSpacing = 4.0f);

// Fit content to children
glm::vec2 FitToContent(UIWidget* container, const glm::vec4& padding = glm::vec4(0.0f));

} // namespace UILayout

// Widget factory helpers
namespace UIFactory {

std::shared_ptr<UIButton> CreateButton(const std::string& text, std::function<void()> onClick = nullptr);
std::shared_ptr<UIText> CreateLabel(const std::string& text);
std::shared_ptr<UITextInput> CreateTextInput(const std::string& placeholder = "");
std::shared_ptr<UISlider> CreateSlider(float min, float max, float value = 0.5f);
std::shared_ptr<UICheckbox> CreateCheckbox(const std::string& label, bool checked = false);
std::shared_ptr<UIProgressBar> CreateProgressBar(float progress = 0.0f);
std::shared_ptr<UIPanel> CreatePanel();
std::shared_ptr<UIImage> CreateImage(uint32_t textureId);

// Compound widgets
std::shared_ptr<UIWidget> CreateLabeledSlider(const std::string& label, float min, float max, float value = 0.5f);
std::shared_ptr<UIWidget> CreateColorPicker();
std::shared_ptr<UIWidget> CreateMessageBox(const std::string& title, const std::string& message,
                                            const std::vector<std::string>& buttons = {"OK"});

} // namespace UIFactory

} // namespace Cortex::UI
