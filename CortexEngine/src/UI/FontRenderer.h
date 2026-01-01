#pragma once

// FontRenderer.h
// Font rendering system with SDF (Signed Distance Field) support.
// Handles text shaping, glyph caching, and high-quality text rendering.

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>

namespace Cortex::UI {

// Forward declarations
class UIRenderer;

// Text alignment
enum class TextAlign : uint8_t {
    Left,
    Center,
    Right,
    Justify
};

enum class TextVAlign : uint8_t {
    Top,
    Middle,
    Bottom
};

// Text overflow handling
enum class TextOverflow : uint8_t {
    Visible,    // Text renders outside bounds
    Clip,       // Text is clipped at bounds
    Ellipsis,   // Text ends with "..." when too long
    Wrap        // Text wraps to next line
};

// Glyph information
struct GlyphInfo {
    uint32_t codepoint = 0;

    // Position in atlas (pixels)
    uint16_t atlasX = 0;
    uint16_t atlasY = 0;
    uint16_t atlasWidth = 0;
    uint16_t atlasHeight = 0;

    // Glyph metrics (in font units, typically 1/64th pixel)
    int16_t bearingX = 0;   // Left side bearing
    int16_t bearingY = 0;   // Top bearing (baseline to top)
    uint16_t width = 0;     // Glyph width
    uint16_t height = 0;    // Glyph height
    uint16_t advance = 0;   // Advance to next glyph

    // UV coordinates (normalized 0-1)
    glm::vec4 uvRect = glm::vec4(0.0f);  // x, y, width, height

    // SDF specific
    float sdfScale = 1.0f;
    float sdfPadding = 0.0f;
};

// Font metrics
struct FontMetrics {
    float ascender = 0.0f;      // Distance from baseline to top
    float descender = 0.0f;     // Distance from baseline to bottom (negative)
    float lineHeight = 0.0f;    // Total line height
    float underlinePos = 0.0f;  // Underline position below baseline
    float underlineThickness = 0.0f;
    float strikeoutPos = 0.0f;  // Strikethrough position
    float spaceAdvance = 0.0f;  // Width of space character
};

// Font style flags
enum class FontStyle : uint8_t {
    Normal = 0,
    Bold = 1 << 0,
    Italic = 1 << 1,
    Underline = 1 << 2,
    Strikethrough = 1 << 3
};

inline FontStyle operator|(FontStyle a, FontStyle b) {
    return static_cast<FontStyle>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline FontStyle operator&(FontStyle a, FontStyle b) {
    return static_cast<FontStyle>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

// Text rendering options
struct TextRenderOptions {
    std::string fontName = "default";
    float fontSize = 14.0f;
    glm::vec4 color = glm::vec4(1.0f);
    glm::vec4 outlineColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    float outlineWidth = 0.0f;
    glm::vec4 shadowColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.5f);
    glm::vec2 shadowOffset = glm::vec2(0.0f);
    float shadowBlur = 0.0f;
    TextAlign align = TextAlign::Left;
    TextVAlign valign = TextVAlign::Top;
    TextOverflow overflow = TextOverflow::Visible;
    float lineSpacing = 1.0f;      // Line height multiplier
    float letterSpacing = 0.0f;    // Extra spacing between characters
    float wordSpacing = 0.0f;      // Extra spacing between words
    float maxWidth = 0.0f;         // Max width for wrapping (0 = no limit)
    FontStyle style = FontStyle::Normal;
    bool kerning = true;
};

// Text measurement result
struct TextMeasurement {
    glm::vec2 size = glm::vec2(0.0f);
    float baseline = 0.0f;
    int lineCount = 1;
    std::vector<float> lineWidths;
};

// Positioned glyph for rendering
struct PositionedGlyph {
    const GlyphInfo* glyph = nullptr;
    glm::vec2 position = glm::vec2(0.0f);
    float scale = 1.0f;
    glm::vec4 color = glm::vec4(1.0f);
};

// Text layout result (ready for rendering)
struct TextLayout {
    std::vector<PositionedGlyph> glyphs;
    TextMeasurement measurement;
    glm::vec4 bounds = glm::vec4(0.0f);  // x, y, width, height
};

// Font atlas
struct FontAtlas {
    uint32_t textureId = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;
    bool isSDF = false;
};

// Font data
class Font {
public:
    Font() = default;
    ~Font();

    // Load font from file
    bool LoadFromFile(const std::string& path, float defaultSize = 32.0f, bool generateSDF = true);

    // Load font from memory
    bool LoadFromMemory(const uint8_t* data, size_t size, float defaultSize = 32.0f, bool generateSDF = true);

    // Get glyph info
    const GlyphInfo* GetGlyph(uint32_t codepoint) const;

    // Get kerning between two glyphs
    float GetKerning(uint32_t left, uint32_t right) const;

    // Get font metrics
    const FontMetrics& GetMetrics() const { return m_metrics; }

    // Get atlas
    const FontAtlas& GetAtlas() const { return m_atlas; }

    // Properties
    const std::string& GetName() const { return m_name; }
    float GetDefaultSize() const { return m_defaultSize; }
    bool IsSDF() const { return m_atlas.isSDF; }

    // Add glyph range (for dynamic atlas building)
    void AddGlyphRange(uint32_t start, uint32_t end);
    void BuildAtlas();

private:
    void GenerateSDFGlyph(uint32_t codepoint);
    void CalculateMetrics();

    std::string m_name;
    float m_defaultSize = 32.0f;
    FontMetrics m_metrics;
    FontAtlas m_atlas;

    std::unordered_map<uint32_t, GlyphInfo> m_glyphs;
    std::unordered_map<uint64_t, float> m_kerningPairs;  // (left << 32 | right) -> kerning

    // FreeType handle (opaque)
    void* m_ftFace = nullptr;
    std::vector<uint8_t> m_fontData;  // Keep font data in memory for FreeType

    // Pending glyph ranges for atlas building
    std::vector<std::pair<uint32_t, uint32_t>> m_pendingRanges;
};

// Font renderer
class FontRenderer {
public:
    FontRenderer();
    ~FontRenderer();

    // Initialize with default fonts
    bool Initialize();
    void Shutdown();

    // Font management
    bool LoadFont(const std::string& name, const std::string& path, float defaultSize = 32.0f);
    bool LoadFontFromMemory(const std::string& name, const uint8_t* data, size_t size, float defaultSize = 32.0f);
    void UnloadFont(const std::string& name);
    Font* GetFont(const std::string& name);
    const Font* GetFont(const std::string& name) const;
    bool HasFont(const std::string& name) const;

    // Set default font
    void SetDefaultFont(const std::string& name);
    const std::string& GetDefaultFont() const { return m_defaultFontName; }

    // Text measurement
    TextMeasurement MeasureText(const std::string& text, const TextRenderOptions& options = {});
    float MeasureTextWidth(const std::string& text, const TextRenderOptions& options = {});
    float MeasureTextHeight(const std::string& text, const TextRenderOptions& options = {});

    // Text layout (prepare for rendering)
    TextLayout LayoutText(const std::string& text, const glm::vec2& position,
                          const TextRenderOptions& options = {});

    // Direct text rendering (if using immediate mode)
    void DrawText(UIRenderer* renderer, const std::string& text,
                  const glm::vec2& position, const TextRenderOptions& options = {});

    // Word wrapping
    std::vector<std::string> WrapText(const std::string& text, float maxWidth,
                                       const TextRenderOptions& options = {});

    // Get text caret position
    glm::vec2 GetCaretPosition(const std::string& text, size_t caretIndex,
                                const TextRenderOptions& options = {});

    // Get character index from position
    size_t GetCharacterIndex(const std::string& text, const glm::vec2& position,
                              const TextRenderOptions& options = {});

    // Unicode utilities
    static std::vector<uint32_t> UTF8ToCodepoints(const std::string& text);
    static std::string CodepointsToUTF8(const std::vector<uint32_t>& codepoints);
    static size_t GetUTF8CharLength(uint8_t leadByte);
    static uint32_t DecodeUTF8Char(const char* text, size_t& bytesRead);

    // Built-in font data
    static const uint8_t* GetBuiltInFontData(size_t& outSize);

private:
    // Text shaping (basic implementation)
    std::vector<PositionedGlyph> ShapeText(const std::string& text, Font* font,
                                            const TextRenderOptions& options);

    // Apply text alignment
    void ApplyAlignment(std::vector<PositionedGlyph>& glyphs,
                        const TextMeasurement& measurement,
                        const glm::vec2& position,
                        const TextRenderOptions& options);

    // Line breaking
    struct LineBreakInfo {
        size_t charIndex = 0;
        float width = 0.0f;
    };
    std::vector<LineBreakInfo> FindLineBreaks(const std::string& text, Font* font,
                                               const TextRenderOptions& options);

    std::unordered_map<std::string, std::unique_ptr<Font>> m_fonts;
    std::string m_defaultFontName = "default";

    // FreeType library handle
    void* m_ftLibrary = nullptr;
};

// Text formatting utilities
namespace TextUtils {

// Simple rich text parsing (supports basic tags like <b>, <i>, <color=#RRGGBB>)
struct TextSpan {
    std::string text;
    FontStyle style = FontStyle::Normal;
    glm::vec4 color = glm::vec4(1.0f);
    float fontSize = 0.0f;  // 0 = use default
};

std::vector<TextSpan> ParseRichText(const std::string& text);

// Format numbers
std::string FormatNumber(double value, int decimals = 2);
std::string FormatWithCommas(int64_t value);
std::string FormatBytes(size_t bytes);
std::string FormatDuration(float seconds);
std::string FormatPercent(float value, int decimals = 1);

// Truncate text with ellipsis
std::string TruncateWithEllipsis(const std::string& text, size_t maxLength);

// Word wrapping (simple version without font metrics)
std::vector<std::string> WrapTextSimple(const std::string& text, size_t maxCharsPerLine);

} // namespace TextUtils

} // namespace Cortex::UI
