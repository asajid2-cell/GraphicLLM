// FontRenderer.cpp
// Font rendering system implementation with SDF support.

#include "FontRenderer.h"
#include "UISystem.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <iomanip>

// FreeType headers (optional - stub implementation if not available)
#ifdef CORTEX_USE_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H
#endif

namespace Cortex::UI {

// ============================================================================
// Font Implementation
// ============================================================================

Font::~Font() {
#ifdef CORTEX_USE_FREETYPE
    if (m_ftFace) {
        FT_Done_Face(static_cast<FT_Face>(m_ftFace));
        m_ftFace = nullptr;
    }
#endif
}

bool Font::LoadFromFile(const std::string& path, float defaultSize, bool generateSDF) {
    // Read file into memory
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) return false;

    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fseek(file, 0, SEEK_SET);

    m_fontData.resize(size);
    fread(m_fontData.data(), 1, size, file);
    fclose(file);

    return LoadFromMemory(m_fontData.data(), m_fontData.size(), defaultSize, generateSDF);
}

bool Font::LoadFromMemory(const uint8_t* data, size_t size, float defaultSize, bool generateSDF) {
    m_defaultSize = defaultSize;
    m_atlas.isSDF = generateSDF;

    // Store font data
    if (m_fontData.empty()) {
        m_fontData.assign(data, data + size);
    }

#ifdef CORTEX_USE_FREETYPE
    // Initialize FreeType face
    FT_Library library = nullptr;  // Would be passed from FontRenderer
    FT_Face face = nullptr;

    if (FT_New_Memory_Face(library, m_fontData.data(),
                           static_cast<FT_Long>(m_fontData.size()), 0, &face) != 0) {
        return false;
    }

    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(defaultSize));
    m_ftFace = face;

    // Extract font name
    if (face->family_name) {
        m_name = face->family_name;
    }

    // Calculate metrics
    CalculateMetrics();

    // Add basic ASCII range by default
    AddGlyphRange(32, 127);
    BuildAtlas();

    return true;
#else
    // Stub implementation - create basic ASCII glyphs
    m_name = "default";

    // Create placeholder metrics
    m_metrics.ascender = defaultSize * 0.8f;
    m_metrics.descender = -defaultSize * 0.2f;
    m_metrics.lineHeight = defaultSize * 1.2f;
    m_metrics.underlinePos = defaultSize * 0.1f;
    m_metrics.underlineThickness = 1.0f;
    m_metrics.strikeoutPos = defaultSize * 0.3f;
    m_metrics.spaceAdvance = defaultSize * 0.3f;

    // Create basic ASCII glyphs
    AddGlyphRange(32, 127);
    BuildAtlas();

    return true;
#endif
}

const GlyphInfo* Font::GetGlyph(uint32_t codepoint) const {
    auto it = m_glyphs.find(codepoint);
    if (it != m_glyphs.end()) {
        return &it->second;
    }

    // Return space glyph as fallback
    it = m_glyphs.find(' ');
    if (it != m_glyphs.end()) {
        return &it->second;
    }

    return nullptr;
}

float Font::GetKerning(uint32_t left, uint32_t right) const {
    uint64_t key = (static_cast<uint64_t>(left) << 32) | right;
    auto it = m_kerningPairs.find(key);
    if (it != m_kerningPairs.end()) {
        return it->second;
    }
    return 0.0f;
}

void Font::AddGlyphRange(uint32_t start, uint32_t end) {
    m_pendingRanges.emplace_back(start, end);
}

void Font::BuildAtlas() {
    // Calculate atlas size needed
    size_t totalGlyphs = 0;
    for (const auto& range : m_pendingRanges) {
        totalGlyphs += range.second - range.first + 1;
    }

    // Determine atlas dimensions (power of 2)
    uint32_t glyphSize = static_cast<uint32_t>(m_defaultSize) + 4;  // padding
    uint32_t glyphsPerRow = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(totalGlyphs))));
    uint32_t atlasWidth = 1;
    while (atlasWidth < glyphsPerRow * glyphSize) atlasWidth *= 2;
    atlasWidth = std::max(atlasWidth, 256u);
    atlasWidth = std::min(atlasWidth, 4096u);

    uint32_t atlasHeight = atlasWidth;

    // Allocate atlas
    m_atlas.width = atlasWidth;
    m_atlas.height = atlasHeight;
    m_atlas.pixels.resize(atlasWidth * atlasHeight, 0);

    // Pack glyphs into atlas
    uint32_t cursorX = 0;
    uint32_t cursorY = 0;
    uint32_t rowHeight = 0;

    for (const auto& range : m_pendingRanges) {
        for (uint32_t cp = range.first; cp <= range.second; ++cp) {
            GlyphInfo glyph;
            glyph.codepoint = cp;

#ifdef CORTEX_USE_FREETYPE
            if (m_ftFace) {
                FT_Face face = static_cast<FT_Face>(m_ftFace);
                FT_UInt glyphIndex = FT_Get_Char_Index(face, cp);

                if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_RENDER) == 0) {
                    FT_GlyphSlot slot = face->glyph;
                    FT_Bitmap& bitmap = slot->bitmap;

                    glyph.width = static_cast<uint16_t>(bitmap.width);
                    glyph.height = static_cast<uint16_t>(bitmap.rows);
                    glyph.bearingX = static_cast<int16_t>(slot->bitmap_left);
                    glyph.bearingY = static_cast<int16_t>(slot->bitmap_top);
                    glyph.advance = static_cast<uint16_t>(slot->advance.x >> 6);

                    // Check if glyph fits in current row
                    if (cursorX + glyph.width + 2 > atlasWidth) {
                        cursorX = 0;
                        cursorY += rowHeight + 2;
                        rowHeight = 0;
                    }

                    // Copy glyph bitmap to atlas
                    glyph.atlasX = static_cast<uint16_t>(cursorX);
                    glyph.atlasY = static_cast<uint16_t>(cursorY);
                    glyph.atlasWidth = glyph.width;
                    glyph.atlasHeight = glyph.height;

                    for (uint32_t y = 0; y < bitmap.rows; ++y) {
                        for (uint32_t x = 0; x < bitmap.width; ++x) {
                            uint32_t srcIdx = y * bitmap.pitch + x;
                            uint32_t dstIdx = (cursorY + y) * atlasWidth + (cursorX + x);
                            if (dstIdx < m_atlas.pixels.size()) {
                                m_atlas.pixels[dstIdx] = bitmap.buffer[srcIdx];
                            }
                        }
                    }

                    cursorX += glyph.width + 2;
                    rowHeight = std::max(rowHeight, static_cast<uint32_t>(glyph.height));
                }
            }
#else
            // Stub: create placeholder glyphs
            uint32_t glyphW = static_cast<uint32_t>(m_defaultSize * 0.5f);
            uint32_t glyphH = static_cast<uint32_t>(m_defaultSize);

            if (cp == ' ') {
                glyphW = static_cast<uint32_t>(m_defaultSize * 0.3f);
            }

            glyph.width = static_cast<uint16_t>(glyphW);
            glyph.height = static_cast<uint16_t>(glyphH);
            glyph.bearingX = 0;
            glyph.bearingY = static_cast<int16_t>(glyphH * 0.8f);
            glyph.advance = static_cast<uint16_t>(glyphW + 2);

            // Check if glyph fits in current row
            if (cursorX + glyphW + 2 > atlasWidth) {
                cursorX = 0;
                cursorY += rowHeight + 2;
                rowHeight = 0;
            }

            glyph.atlasX = static_cast<uint16_t>(cursorX);
            glyph.atlasY = static_cast<uint16_t>(cursorY);
            glyph.atlasWidth = static_cast<uint16_t>(glyphW);
            glyph.atlasHeight = static_cast<uint16_t>(glyphH);

            // Draw placeholder rectangle
            for (uint32_t y = 0; y < glyphH; ++y) {
                for (uint32_t x = 0; x < glyphW; ++x) {
                    uint32_t dstIdx = (cursorY + y) * atlasWidth + (cursorX + x);
                    if (dstIdx < m_atlas.pixels.size()) {
                        // Border or fill
                        bool isBorder = (x == 0 || x == glyphW - 1 || y == 0 || y == glyphH - 1);
                        m_atlas.pixels[dstIdx] = (cp != ' ' && isBorder) ? 255 : 0;
                    }
                }
            }

            cursorX += glyphW + 2;
            rowHeight = std::max(rowHeight, glyphH);
#endif

            // Calculate UV rect
            glyph.uvRect = glm::vec4(
                static_cast<float>(glyph.atlasX) / static_cast<float>(atlasWidth),
                static_cast<float>(glyph.atlasY) / static_cast<float>(atlasHeight),
                static_cast<float>(glyph.atlasWidth) / static_cast<float>(atlasWidth),
                static_cast<float>(glyph.atlasHeight) / static_cast<float>(atlasHeight)
            );

            m_glyphs[cp] = glyph;
        }
    }

    m_pendingRanges.clear();
}

void Font::CalculateMetrics() {
#ifdef CORTEX_USE_FREETYPE
    if (m_ftFace) {
        FT_Face face = static_cast<FT_Face>(m_ftFace);
        float scale = m_defaultSize / static_cast<float>(face->units_per_EM);

        m_metrics.ascender = static_cast<float>(face->ascender) * scale;
        m_metrics.descender = static_cast<float>(face->descender) * scale;
        m_metrics.lineHeight = static_cast<float>(face->height) * scale;
        m_metrics.underlinePos = static_cast<float>(-face->underline_position) * scale;
        m_metrics.underlineThickness = static_cast<float>(face->underline_thickness) * scale;
        m_metrics.strikeoutPos = m_metrics.ascender * 0.3f;

        // Get space advance
        FT_UInt spaceIndex = FT_Get_Char_Index(face, ' ');
        if (FT_Load_Glyph(face, spaceIndex, FT_LOAD_NO_BITMAP) == 0) {
            m_metrics.spaceAdvance = static_cast<float>(face->glyph->advance.x >> 6);
        } else {
            m_metrics.spaceAdvance = m_defaultSize * 0.3f;
        }
    }
#endif
}

void Font::GenerateSDFGlyph(uint32_t /*codepoint*/) {
    // SDF generation would go here
    // Uses distance field algorithm from glyph outline
}

// ============================================================================
// FontRenderer Implementation
// ============================================================================

FontRenderer::FontRenderer() {
}

FontRenderer::~FontRenderer() {
    Shutdown();
}

bool FontRenderer::Initialize() {
#ifdef CORTEX_USE_FREETYPE
    if (FT_Init_FreeType(reinterpret_cast<FT_Library*>(&m_ftLibrary)) != 0) {
        return false;
    }
#endif

    // Load built-in default font
    size_t builtInSize = 0;
    const uint8_t* builtInData = GetBuiltInFontData(builtInSize);
    if (builtInData && builtInSize > 0) {
        LoadFontFromMemory("default", builtInData, builtInSize, 32.0f);
    } else {
        // Create a minimal placeholder font
        auto font = std::make_unique<Font>();
        font->LoadFromMemory(nullptr, 0, 32.0f, false);
        m_fonts["default"] = std::move(font);
    }

    m_defaultFontName = "default";
    return true;
}

void FontRenderer::Shutdown() {
    m_fonts.clear();

#ifdef CORTEX_USE_FREETYPE
    if (m_ftLibrary) {
        FT_Done_FreeType(static_cast<FT_Library>(m_ftLibrary));
        m_ftLibrary = nullptr;
    }
#endif
}

bool FontRenderer::LoadFont(const std::string& name, const std::string& path, float defaultSize) {
    auto font = std::make_unique<Font>();
    if (!font->LoadFromFile(path, defaultSize, true)) {
        return false;
    }
    m_fonts[name] = std::move(font);
    return true;
}

bool FontRenderer::LoadFontFromMemory(const std::string& name, const uint8_t* data, size_t size, float defaultSize) {
    auto font = std::make_unique<Font>();
    if (!font->LoadFromMemory(data, size, defaultSize, true)) {
        return false;
    }
    m_fonts[name] = std::move(font);
    return true;
}

void FontRenderer::UnloadFont(const std::string& name) {
    m_fonts.erase(name);
}

Font* FontRenderer::GetFont(const std::string& name) {
    auto it = m_fonts.find(name);
    if (it != m_fonts.end()) {
        return it->second.get();
    }

    // Fall back to default
    it = m_fonts.find(m_defaultFontName);
    if (it != m_fonts.end()) {
        return it->second.get();
    }

    return nullptr;
}

const Font* FontRenderer::GetFont(const std::string& name) const {
    auto it = m_fonts.find(name);
    if (it != m_fonts.end()) {
        return it->second.get();
    }

    it = m_fonts.find(m_defaultFontName);
    if (it != m_fonts.end()) {
        return it->second.get();
    }

    return nullptr;
}

bool FontRenderer::HasFont(const std::string& name) const {
    return m_fonts.find(name) != m_fonts.end();
}

void FontRenderer::SetDefaultFont(const std::string& name) {
    if (HasFont(name)) {
        m_defaultFontName = name;
    }
}

TextMeasurement FontRenderer::MeasureText(const std::string& text, const TextRenderOptions& options) {
    TextMeasurement result;

    Font* font = GetFont(options.fontName);
    if (!font) return result;

    float scale = options.fontSize / font->GetDefaultSize();
    const FontMetrics& metrics = font->GetMetrics();

    std::vector<uint32_t> codepoints = UTF8ToCodepoints(text);

    float x = 0.0f;
    float maxWidth = 0.0f;
    int lineCount = 1;
    std::vector<float> lineWidths;

    uint32_t prevCodepoint = 0;

    for (uint32_t cp : codepoints) {
        if (cp == '\n') {
            lineWidths.push_back(x);
            maxWidth = std::max(maxWidth, x);
            x = 0.0f;
            lineCount++;
            prevCodepoint = 0;
            continue;
        }

        const GlyphInfo* glyph = font->GetGlyph(cp);
        if (!glyph) continue;

        // Apply kerning
        if (options.kerning && prevCodepoint != 0) {
            x += font->GetKerning(prevCodepoint, cp) * scale;
        }

        x += glyph->advance * scale + options.letterSpacing;

        if (cp == ' ') {
            x += options.wordSpacing;
        }

        prevCodepoint = cp;
    }

    lineWidths.push_back(x);
    maxWidth = std::max(maxWidth, x);

    result.size.x = maxWidth;
    result.size.y = static_cast<float>(lineCount) * metrics.lineHeight * scale * options.lineSpacing;
    result.baseline = metrics.ascender * scale;
    result.lineCount = lineCount;
    result.lineWidths = std::move(lineWidths);

    return result;
}

float FontRenderer::MeasureTextWidth(const std::string& text, const TextRenderOptions& options) {
    return MeasureText(text, options).size.x;
}

float FontRenderer::MeasureTextHeight(const std::string& text, const TextRenderOptions& options) {
    return MeasureText(text, options).size.y;
}

TextLayout FontRenderer::LayoutText(const std::string& text, const glm::vec2& position,
                                     const TextRenderOptions& options) {
    TextLayout layout;

    Font* font = GetFont(options.fontName);
    if (!font) return layout;

    // Shape text into positioned glyphs
    layout.glyphs = ShapeText(text, font, options);

    // Measure
    layout.measurement = MeasureText(text, options);

    // Apply alignment
    ApplyAlignment(layout.glyphs, layout.measurement, position, options);

    // Calculate bounds
    layout.bounds = glm::vec4(position.x, position.y,
                               layout.measurement.size.x, layout.measurement.size.y);

    return layout;
}

std::vector<PositionedGlyph> FontRenderer::ShapeText(const std::string& text, Font* font,
                                                      const TextRenderOptions& options) {
    std::vector<PositionedGlyph> glyphs;

    if (!font) return glyphs;

    float scale = options.fontSize / font->GetDefaultSize();
    const FontMetrics& metrics = font->GetMetrics();

    std::vector<uint32_t> codepoints = UTF8ToCodepoints(text);
    glyphs.reserve(codepoints.size());

    float x = 0.0f;
    float y = metrics.ascender * scale;  // Start at baseline
    uint32_t prevCodepoint = 0;
    int lineIndex = 0;

    for (uint32_t cp : codepoints) {
        if (cp == '\n') {
            x = 0.0f;
            y += metrics.lineHeight * scale * options.lineSpacing;
            lineIndex++;
            prevCodepoint = 0;
            continue;
        }

        const GlyphInfo* glyph = font->GetGlyph(cp);
        if (!glyph) continue;

        // Apply kerning
        if (options.kerning && prevCodepoint != 0) {
            x += font->GetKerning(prevCodepoint, cp) * scale;
        }

        PositionedGlyph pg;
        pg.glyph = glyph;
        pg.position = glm::vec2(
            x + glyph->bearingX * scale,
            y - glyph->bearingY * scale
        );
        pg.scale = scale;
        pg.color = options.color;

        glyphs.push_back(pg);

        x += glyph->advance * scale + options.letterSpacing;

        if (cp == ' ') {
            x += options.wordSpacing;
        }

        prevCodepoint = cp;
    }

    return glyphs;
}

void FontRenderer::ApplyAlignment(std::vector<PositionedGlyph>& glyphs,
                                   const TextMeasurement& measurement,
                                   const glm::vec2& position,
                                   const TextRenderOptions& options) {
    if (glyphs.empty()) return;

    // Horizontal alignment offset
    float xOffset = 0.0f;
    switch (options.align) {
        case TextAlign::Left:
            xOffset = 0.0f;
            break;
        case TextAlign::Center:
            xOffset = -measurement.size.x / 2.0f;
            break;
        case TextAlign::Right:
            xOffset = -measurement.size.x;
            break;
        case TextAlign::Justify:
            // Would need per-line handling
            xOffset = 0.0f;
            break;
    }

    // Vertical alignment offset
    float yOffset = 0.0f;
    switch (options.valign) {
        case TextVAlign::Top:
            yOffset = 0.0f;
            break;
        case TextVAlign::Middle:
            yOffset = -measurement.size.y / 2.0f;
            break;
        case TextVAlign::Bottom:
            yOffset = -measurement.size.y;
            break;
    }

    // Apply offsets
    for (auto& glyph : glyphs) {
        glyph.position.x += position.x + xOffset;
        glyph.position.y += position.y + yOffset;
    }
}

void FontRenderer::DrawText(UIRenderer* renderer, const std::string& text,
                             const glm::vec2& position, const TextRenderOptions& options) {
    if (!renderer) return;

    Font* font = GetFont(options.fontName);
    if (!font) return;

    TextLayout layout = LayoutText(text, position, options);

    // Draw shadow first
    if (options.shadowOffset.x != 0.0f || options.shadowOffset.y != 0.0f) {
        for (const auto& pg : layout.glyphs) {
            if (pg.glyph) {
                glm::vec4 glyphRect(
                    pg.position.x + options.shadowOffset.x,
                    pg.position.y + options.shadowOffset.y,
                    pg.glyph->width * pg.scale,
                    pg.glyph->height * pg.scale
                );

                renderer->DrawImage(glyphRect, font->GetAtlas().textureId,
                                    options.shadowColor, pg.glyph->uvRect);
            }
        }
    }

    // Draw outline (simplified - would need SDF for proper outline)
    if (options.outlineWidth > 0.0f) {
        glm::vec2 offsets[] = {
            {-1, 0}, {1, 0}, {0, -1}, {0, 1},
            {-1, -1}, {1, -1}, {-1, 1}, {1, 1}
        };

        for (const auto& offset : offsets) {
            for (const auto& pg : layout.glyphs) {
                if (pg.glyph) {
                    glm::vec4 glyphRect(
                        pg.position.x + offset.x * options.outlineWidth,
                        pg.position.y + offset.y * options.outlineWidth,
                        pg.glyph->width * pg.scale,
                        pg.glyph->height * pg.scale
                    );

                    renderer->DrawImage(glyphRect, font->GetAtlas().textureId,
                                        options.outlineColor, pg.glyph->uvRect);
                }
            }
        }
    }

    // Draw main text
    for (const auto& pg : layout.glyphs) {
        if (pg.glyph) {
            glm::vec4 glyphRect(
                pg.position.x,
                pg.position.y,
                pg.glyph->width * pg.scale,
                pg.glyph->height * pg.scale
            );

            renderer->DrawImage(glyphRect, font->GetAtlas().textureId,
                                pg.color, pg.glyph->uvRect);
        }
    }

    // Draw underline
    if (static_cast<uint8_t>(options.style & FontStyle::Underline)) {
        float lineY = position.y + font->GetMetrics().underlinePos * (options.fontSize / font->GetDefaultSize());
        renderer->DrawLine(
            glm::vec2(position.x, lineY),
            glm::vec2(position.x + layout.measurement.size.x, lineY),
            options.color,
            font->GetMetrics().underlineThickness
        );
    }

    // Draw strikethrough
    if (static_cast<uint8_t>(options.style & FontStyle::Strikethrough)) {
        float lineY = position.y + font->GetMetrics().strikeoutPos * (options.fontSize / font->GetDefaultSize());
        renderer->DrawLine(
            glm::vec2(position.x, lineY),
            glm::vec2(position.x + layout.measurement.size.x, lineY),
            options.color,
            font->GetMetrics().underlineThickness
        );
    }
}

std::vector<std::string> FontRenderer::WrapText(const std::string& text, float maxWidth,
                                                  const TextRenderOptions& options) {
    std::vector<std::string> lines;
    if (maxWidth <= 0.0f) {
        lines.push_back(text);
        return lines;
    }

    Font* font = GetFont(options.fontName);
    if (!font) {
        lines.push_back(text);
        return lines;
    }

    float scale = options.fontSize / font->GetDefaultSize();

    std::string currentLine;
    std::string currentWord;
    float currentWidth = 0.0f;
    float wordWidth = 0.0f;

    std::vector<uint32_t> codepoints = UTF8ToCodepoints(text);

    for (uint32_t cp : codepoints) {
        if (cp == '\n') {
            // Force line break
            currentLine += currentWord;
            lines.push_back(currentLine);
            currentLine.clear();
            currentWord.clear();
            currentWidth = 0.0f;
            wordWidth = 0.0f;
            continue;
        }

        const GlyphInfo* glyph = font->GetGlyph(cp);
        float charWidth = glyph ? glyph->advance * scale + options.letterSpacing : 0.0f;

        if (cp == ' ') {
            // End of word
            if (currentWidth + wordWidth <= maxWidth) {
                currentLine += currentWord;
                currentLine += ' ';
                currentWidth += wordWidth + charWidth + options.wordSpacing;
            } else {
                // Word doesn't fit, start new line
                if (!currentLine.empty()) {
                    lines.push_back(currentLine);
                }
                currentLine = currentWord + ' ';
                currentWidth = wordWidth + charWidth + options.wordSpacing;
            }
            currentWord.clear();
            wordWidth = 0.0f;
        } else {
            // Add to current word
            char utf8[5] = {0};
            if (cp < 0x80) {
                utf8[0] = static_cast<char>(cp);
            } else if (cp < 0x800) {
                utf8[0] = static_cast<char>(0xC0 | (cp >> 6));
                utf8[1] = static_cast<char>(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                utf8[0] = static_cast<char>(0xE0 | (cp >> 12));
                utf8[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                utf8[2] = static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                utf8[0] = static_cast<char>(0xF0 | (cp >> 18));
                utf8[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                utf8[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                utf8[3] = static_cast<char>(0x80 | (cp & 0x3F));
            }
            currentWord += utf8;
            wordWidth += charWidth;
        }
    }

    // Add remaining text
    if (currentWidth + wordWidth <= maxWidth) {
        currentLine += currentWord;
    } else {
        if (!currentLine.empty()) {
            lines.push_back(currentLine);
        }
        currentLine = currentWord;
    }

    if (!currentLine.empty()) {
        lines.push_back(currentLine);
    }

    return lines;
}

glm::vec2 FontRenderer::GetCaretPosition(const std::string& text, size_t caretIndex,
                                          const TextRenderOptions& options) {
    Font* font = GetFont(options.fontName);
    if (!font) return glm::vec2(0.0f);

    float scale = options.fontSize / font->GetDefaultSize();
    const FontMetrics& metrics = font->GetMetrics();

    std::vector<uint32_t> codepoints = UTF8ToCodepoints(text);

    float x = 0.0f;
    float y = 0.0f;
    size_t charIndex = 0;
    uint32_t prevCodepoint = 0;

    for (uint32_t cp : codepoints) {
        if (charIndex >= caretIndex) break;

        if (cp == '\n') {
            x = 0.0f;
            y += metrics.lineHeight * scale * options.lineSpacing;
            prevCodepoint = 0;
        } else {
            const GlyphInfo* glyph = font->GetGlyph(cp);
            if (glyph) {
                if (options.kerning && prevCodepoint != 0) {
                    x += font->GetKerning(prevCodepoint, cp) * scale;
                }
                x += glyph->advance * scale + options.letterSpacing;
                if (cp == ' ') {
                    x += options.wordSpacing;
                }
            }
            prevCodepoint = cp;
        }

        charIndex++;
    }

    return glm::vec2(x, y);
}

size_t FontRenderer::GetCharacterIndex(const std::string& text, const glm::vec2& position,
                                        const TextRenderOptions& options) {
    Font* font = GetFont(options.fontName);
    if (!font) return 0;

    float scale = options.fontSize / font->GetDefaultSize();
    const FontMetrics& metrics = font->GetMetrics();

    std::vector<uint32_t> codepoints = UTF8ToCodepoints(text);

    float x = 0.0f;
    float y = 0.0f;
    size_t charIndex = 0;
    uint32_t prevCodepoint = 0;

    // Find line first
    int targetLine = static_cast<int>(position.y / (metrics.lineHeight * scale * options.lineSpacing));
    int currentLine = 0;

    for (uint32_t cp : codepoints) {
        if (currentLine > targetLine) break;

        if (cp == '\n') {
            if (currentLine == targetLine && position.x <= x) {
                return charIndex;
            }
            x = 0.0f;
            y += metrics.lineHeight * scale * options.lineSpacing;
            currentLine++;
            prevCodepoint = 0;
        } else if (currentLine == targetLine) {
            const GlyphInfo* glyph = font->GetGlyph(cp);
            if (glyph) {
                float charWidth = glyph->advance * scale + options.letterSpacing;
                if (position.x <= x + charWidth / 2) {
                    return charIndex;
                }
                x += charWidth;
            }
            prevCodepoint = cp;
        }

        charIndex++;
    }

    return codepoints.size();
}

// ============================================================================
// Unicode Utilities
// ============================================================================

std::vector<uint32_t> FontRenderer::UTF8ToCodepoints(const std::string& text) {
    std::vector<uint32_t> codepoints;
    codepoints.reserve(text.size());

    const char* ptr = text.c_str();
    const char* end = ptr + text.size();

    while (ptr < end) {
        size_t bytesRead = 0;
        uint32_t cp = DecodeUTF8Char(ptr, bytesRead);
        if (bytesRead > 0) {
            codepoints.push_back(cp);
            ptr += bytesRead;
        } else {
            ptr++;  // Skip invalid byte
        }
    }

    return codepoints;
}

std::string FontRenderer::CodepointsToUTF8(const std::vector<uint32_t>& codepoints) {
    std::string result;
    result.reserve(codepoints.size() * 4);  // Max 4 bytes per codepoint

    for (uint32_t cp : codepoints) {
        if (cp < 0x80) {
            result += static_cast<char>(cp);
        } else if (cp < 0x800) {
            result += static_cast<char>(0xC0 | (cp >> 6));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            result += static_cast<char>(0xE0 | (cp >> 12));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            result += static_cast<char>(0xF0 | (cp >> 18));
            result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    return result;
}

size_t FontRenderer::GetUTF8CharLength(uint8_t leadByte) {
    if ((leadByte & 0x80) == 0) return 1;
    if ((leadByte & 0xE0) == 0xC0) return 2;
    if ((leadByte & 0xF0) == 0xE0) return 3;
    if ((leadByte & 0xF8) == 0xF0) return 4;
    return 1;  // Invalid, treat as single byte
}

uint32_t FontRenderer::DecodeUTF8Char(const char* text, size_t& bytesRead) {
    uint8_t lead = static_cast<uint8_t>(text[0]);

    if ((lead & 0x80) == 0) {
        bytesRead = 1;
        return lead;
    }

    if ((lead & 0xE0) == 0xC0) {
        bytesRead = 2;
        return ((lead & 0x1F) << 6) |
               (static_cast<uint8_t>(text[1]) & 0x3F);
    }

    if ((lead & 0xF0) == 0xE0) {
        bytesRead = 3;
        return ((lead & 0x0F) << 12) |
               ((static_cast<uint8_t>(text[1]) & 0x3F) << 6) |
               (static_cast<uint8_t>(text[2]) & 0x3F);
    }

    if ((lead & 0xF8) == 0xF0) {
        bytesRead = 4;
        return ((lead & 0x07) << 18) |
               ((static_cast<uint8_t>(text[1]) & 0x3F) << 12) |
               ((static_cast<uint8_t>(text[2]) & 0x3F) << 6) |
               (static_cast<uint8_t>(text[3]) & 0x3F);
    }

    bytesRead = 1;  // Invalid
    return 0xFFFD;  // Replacement character
}

const uint8_t* FontRenderer::GetBuiltInFontData(size_t& outSize) {
    // In a real implementation, this would return embedded font data
    // For now, return nullptr to use stub font
    outSize = 0;
    return nullptr;
}

// ============================================================================
// TextUtils Namespace
// ============================================================================

namespace TextUtils {

std::vector<TextSpan> ParseRichText(const std::string& text) {
    std::vector<TextSpan> spans;
    TextSpan current;
    std::string buffer;

    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '<') {
            // Found tag start
            size_t tagEnd = text.find('>', i);
            if (tagEnd != std::string::npos) {
                std::string tag = text.substr(i + 1, tagEnd - i - 1);

                // Save current span if has content
                if (!buffer.empty()) {
                    current.text = buffer;
                    spans.push_back(current);
                    buffer.clear();
                }

                // Parse tag
                if (tag == "b") {
                    current.style = current.style | FontStyle::Bold;
                } else if (tag == "/b") {
                    current.style = static_cast<FontStyle>(
                        static_cast<uint8_t>(current.style) & ~static_cast<uint8_t>(FontStyle::Bold));
                } else if (tag == "i") {
                    current.style = current.style | FontStyle::Italic;
                } else if (tag == "/i") {
                    current.style = static_cast<FontStyle>(
                        static_cast<uint8_t>(current.style) & ~static_cast<uint8_t>(FontStyle::Italic));
                } else if (tag == "u") {
                    current.style = current.style | FontStyle::Underline;
                } else if (tag == "/u") {
                    current.style = static_cast<FontStyle>(
                        static_cast<uint8_t>(current.style) & ~static_cast<uint8_t>(FontStyle::Underline));
                } else if (tag.substr(0, 6) == "color=") {
                    // Parse color
                    std::string colorStr = tag.substr(6);
                    if (colorStr.size() >= 7 && colorStr[0] == '#') {
                        uint32_t color = std::stoul(colorStr.substr(1), nullptr, 16);
                        current.color = glm::vec4(
                            ((color >> 16) & 0xFF) / 255.0f,
                            ((color >> 8) & 0xFF) / 255.0f,
                            (color & 0xFF) / 255.0f,
                            1.0f
                        );
                    }
                } else if (tag == "/color") {
                    current.color = glm::vec4(1.0f);
                } else if (tag.substr(0, 5) == "size=") {
                    current.fontSize = std::stof(tag.substr(5));
                } else if (tag == "/size") {
                    current.fontSize = 0.0f;
                }

                i = tagEnd + 1;
                continue;
            }
        }

        buffer += text[i];
        i++;
    }

    // Add remaining content
    if (!buffer.empty()) {
        current.text = buffer;
        spans.push_back(current);
    }

    return spans;
}

std::string FormatNumber(double value, int decimals) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(decimals) << value;
    return ss.str();
}

std::string FormatWithCommas(int64_t value) {
    std::string result = std::to_string(std::abs(value));

    // Insert commas from right to left
    int insertPosition = static_cast<int>(result.length()) - 3;
    while (insertPosition > 0) {
        result.insert(insertPosition, ",");
        insertPosition -= 3;
    }

    if (value < 0) {
        result = "-" + result;
    }

    return result;
}

std::string FormatBytes(size_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unitIndex = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024.0 && unitIndex < 4) {
        size /= 1024.0;
        unitIndex++;
    }

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(unitIndex > 0 ? 2 : 0) << size << " " << units[unitIndex];
    return ss.str();
}

std::string FormatDuration(float seconds) {
    int totalSeconds = static_cast<int>(seconds);
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int secs = totalSeconds % 60;

    std::ostringstream ss;
    if (hours > 0) {
        ss << hours << ":";
        ss << std::setfill('0') << std::setw(2) << minutes << ":";
    } else {
        ss << minutes << ":";
    }
    ss << std::setfill('0') << std::setw(2) << secs;

    return ss.str();
}

std::string FormatPercent(float value, int decimals) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(decimals) << (value * 100.0f) << "%";
    return ss.str();
}

std::string TruncateWithEllipsis(const std::string& text, size_t maxLength) {
    if (text.length() <= maxLength) {
        return text;
    }

    if (maxLength <= 3) {
        return text.substr(0, maxLength);
    }

    return text.substr(0, maxLength - 3) + "...";
}

std::vector<std::string> WrapTextSimple(const std::string& text, size_t maxCharsPerLine) {
    std::vector<std::string> lines;

    std::string currentLine;
    std::string currentWord;

    for (char c : text) {
        if (c == '\n') {
            currentLine += currentWord;
            lines.push_back(currentLine);
            currentLine.clear();
            currentWord.clear();
        } else if (c == ' ') {
            if (currentLine.length() + currentWord.length() + 1 <= maxCharsPerLine) {
                currentLine += currentWord + ' ';
            } else {
                if (!currentLine.empty()) {
                    lines.push_back(currentLine);
                }
                currentLine = currentWord + ' ';
            }
            currentWord.clear();
        } else {
            currentWord += c;
        }
    }

    // Add remaining
    if (currentLine.length() + currentWord.length() <= maxCharsPerLine) {
        currentLine += currentWord;
    } else {
        if (!currentLine.empty()) {
            lines.push_back(currentLine);
        }
        currentLine = currentWord;
    }

    if (!currentLine.empty()) {
        lines.push_back(currentLine);
    }

    return lines;
}

} // namespace TextUtils

} // namespace Cortex::UI
