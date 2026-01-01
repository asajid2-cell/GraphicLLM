// TerrainBrush.h
// Terrain sculpting brush system.
// Provides tools for raising, lowering, smoothing, flattening, and painting terrain.

#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace Cortex::Editor {

// Forward declarations
class ChunkGenerator;

// ============================================================================
// Brush Mode Enumeration
// ============================================================================

enum class BrushMode : uint8_t {
    Raise,      // Raise terrain height
    Lower,      // Lower terrain height
    Smooth,     // Smooth terrain (average neighbors)
    Flatten,    // Flatten to target height
    Noise,      // Add procedural noise
    Paint,      // Paint biome/material
    Stamp,      // Stamp heightmap pattern
    Erode,      // Simulate erosion
    Clone,      // Clone terrain from another location
    Mask        // Edit mask layer
};

// ============================================================================
// Brush Falloff Types
// ============================================================================

enum class BrushFalloff : uint8_t {
    Linear,         // Linear falloff
    Smooth,         // Smooth (cosine) falloff
    Spherical,      // Spherical falloff (sqrt-based)
    Tip,            // Sharp tip (exponential)
    Flat,           // No falloff (constant)
    Custom          // Custom falloff curve
};

// ============================================================================
// Brush Shape
// ============================================================================

enum class BrushShape : uint8_t {
    Circle,
    Square,
    Custom      // Custom mask texture
};

// ============================================================================
// Brush Settings
// ============================================================================

struct BrushSettings {
    BrushMode mode = BrushMode::Raise;
    BrushFalloff falloff = BrushFalloff::Smooth;
    BrushShape shape = BrushShape::Circle;

    float radius = 10.0f;           // Brush radius in world units
    float strength = 1.0f;          // Brush strength (0-1)
    float falloffAmount = 0.5f;     // Falloff curve steepness
    float targetHeight = 0.0f;      // Target height for flatten mode
    float noiseScale = 1.0f;        // Noise frequency for noise mode
    float noiseAmplitude = 1.0f;    // Noise amplitude for noise mode

    uint32_t biomeIndex = 0;        // Biome index for paint mode
    uint32_t textureIndex = 0;      // Texture layer for paint mode

    float rotation = 0.0f;          // Brush rotation (degrees)
    float spacing = 0.25f;          // Spacing between brush applications (0-1)

    bool invertFalloff = false;     // Invert falloff (ring brush)
    bool useWorldHeight = false;    // Use world height for flatten
    bool additiveMode = false;      // Add to existing values vs replace

    std::string customBrushPath;    // Path to custom brush texture
    std::string stampTexturePath;   // Path to stamp texture

    // Calculate falloff at normalized distance (0-1)
    float GetFalloff(float normalizedDistance) const;

    // Get brush intensity at world position
    float GetIntensityAt(const glm::vec3& center, const glm::vec3& position) const;
};

// ============================================================================
// Brush Stroke
// ============================================================================

struct BrushStroke {
    glm::vec3 startPosition;
    glm::vec3 endPosition;
    glm::vec3 currentPosition;

    float startTime;
    float currentTime;
    float lastApplyTime;

    std::vector<glm::vec3> path;        // Full stroke path
    std::vector<float> pressures;        // Pressure at each point (for tablets)

    bool isActive = false;
    bool isDragging = false;

    // Get stroke length
    float GetLength() const;

    // Get stroke direction at current position
    glm::vec3 GetDirection() const;

    // Clear stroke data
    void Clear();
};

// ============================================================================
// Terrain Brush
// ============================================================================

class TerrainBrush {
public:
    TerrainBrush();
    ~TerrainBrush();

    // Initialize with chunk generator
    void Initialize(ChunkGenerator* chunkGen);

    // Update brush (call each frame when active)
    void Update(float deltaTime);

    // Brush actions
    void BeginStroke(const glm::vec3& position, float pressure = 1.0f);
    void UpdateStroke(const glm::vec3& position, float pressure = 1.0f);
    void EndStroke();
    void CancelStroke();

    // Apply brush at position (single application)
    void ApplyAtPosition(const glm::vec3& position, float pressure = 1.0f);

    // Settings
    BrushSettings& GetSettings() { return m_settings; }
    const BrushSettings& GetSettings() const { return m_settings; }
    void SetSettings(const BrushSettings& settings) { m_settings = settings; }

    // Mode shortcuts
    void SetMode(BrushMode mode) { m_settings.mode = mode; }
    BrushMode GetMode() const { return m_settings.mode; }

    void SetRadius(float radius) { m_settings.radius = std::max(0.1f, radius); }
    float GetRadius() const { return m_settings.radius; }

    void SetStrength(float strength) { m_settings.strength = std::clamp(strength, 0.0f, 1.0f); }
    float GetStrength() const { return m_settings.strength; }

    // Undo/Redo
    bool CanUndo() const { return !m_undoStack.empty(); }
    bool CanRedo() const { return !m_redoStack.empty(); }
    void Undo();
    void Redo();
    void ClearHistory();

    // Cursor
    glm::vec3 GetCursorPosition() const { return m_cursorPosition; }
    void SetCursorPosition(const glm::vec3& pos) { m_cursorPosition = pos; }
    bool IsCursorValid() const { return m_cursorValid; }
    void SetCursorValid(bool valid) { m_cursorValid = valid; }

    // Stroke state
    bool IsStrokeActive() const { return m_stroke.isActive; }
    const BrushStroke& GetStroke() const { return m_stroke; }

    // Preview
    void EnablePreview(bool enable) { m_previewEnabled = enable; }
    bool IsPreviewEnabled() const { return m_previewEnabled; }

    // Custom brush loading
    bool LoadCustomBrush(const std::string& path);
    void ClearCustomBrush();

    // Sample height at position
    float SampleHeight(const glm::vec3& position) const;

    // Sample biome at position
    uint32_t SampleBiome(const glm::vec3& position) const;

    // Get affected terrain bounds
    struct TerrainBounds {
        glm::ivec2 chunkMin;
        glm::ivec2 chunkMax;
        glm::ivec2 vertexMin;
        glm::ivec2 vertexMax;
    };
    TerrainBounds GetAffectedBounds(const glm::vec3& position) const;

    // Callbacks
    using BrushCallback = std::function<void(const glm::vec3&, float, float)>;
    void SetOnApply(BrushCallback callback) { m_onApply = callback; }
    void SetOnStrokeEnd(std::function<void()> callback) { m_onStrokeEnd = callback; }

private:
    // Apply brush in specific modes
    void ApplyRaise(const glm::vec3& center, float pressure);
    void ApplyLower(const glm::vec3& center, float pressure);
    void ApplySmooth(const glm::vec3& center, float pressure);
    void ApplyFlatten(const glm::vec3& center, float pressure);
    void ApplyNoise(const glm::vec3& center, float pressure);
    void ApplyPaint(const glm::vec3& center, float pressure);
    void ApplyStamp(const glm::vec3& center, float pressure);
    void ApplyErode(const glm::vec3& center, float pressure);
    void ApplyClone(const glm::vec3& center, float pressure);
    void ApplyMask(const glm::vec3& center, float pressure);

    // Heightmap modification
    void ModifyHeight(int x, int z, float delta);
    float GetHeight(int x, int z) const;
    void SetHeight(int x, int z, float height);

    // Undo data
    struct UndoData {
        std::vector<std::pair<glm::ivec2, float>> heightChanges;
        std::vector<std::pair<glm::ivec2, uint32_t>> biomeChanges;
        std::string description;
    };
    void PushUndo(const std::string& description);
    void BeginUndoGroup();
    void EndUndoGroup();

    ChunkGenerator* m_chunkGen = nullptr;
    BrushSettings m_settings;
    BrushStroke m_stroke;

    glm::vec3 m_cursorPosition;
    bool m_cursorValid = false;
    bool m_previewEnabled = true;

    // Custom brush
    std::vector<float> m_customBrushData;
    int m_customBrushSize = 0;

    // Stamp texture
    std::vector<float> m_stampData;
    int m_stampWidth = 0;
    int m_stampHeight = 0;

    // Clone source
    glm::vec3 m_cloneSource;
    bool m_cloneSourceSet = false;

    // Undo/Redo
    std::vector<UndoData> m_undoStack;
    std::vector<UndoData> m_redoStack;
    std::unique_ptr<UndoData> m_currentUndo;
    bool m_inUndoGroup = false;
    size_t m_maxUndoLevels = 50;

    // Callbacks
    BrushCallback m_onApply;
    std::function<void()> m_onStrokeEnd;

    // Noise state for consistent noise generation
    int m_noiseSeed = 12345;
};

// ============================================================================
// Brush Manager (handles brush presets and tools)
// ============================================================================

class BrushManager {
public:
    BrushManager();
    ~BrushManager();

    // Initialize
    void Initialize();
    void Shutdown();

    // Get active brush
    TerrainBrush* GetActiveBrush() { return m_activeBrush.get(); }

    // Preset management
    void SavePreset(const std::string& name, const BrushSettings& settings);
    bool LoadPreset(const std::string& name, BrushSettings& settings);
    void DeletePreset(const std::string& name);
    std::vector<std::string> GetPresetNames() const;

    // Quick presets
    BrushSettings GetDefaultRaiseBrush() const;
    BrushSettings GetDefaultSmoothBrush() const;
    BrushSettings GetDefaultFlattenBrush() const;
    BrushSettings GetDefaultPaintBrush() const;

    // Tool shortcuts (creates brush with preset settings)
    void SelectRaiseTool();
    void SelectLowerTool();
    void SelectSmoothTool();
    void SelectFlattenTool();
    void SelectPaintTool();
    void SelectErodeTool();
    void SelectCloneTool();

    // Keyboard modifiers
    void SetShiftHeld(bool held) { m_shiftHeld = held; }
    void SetCtrlHeld(bool held) { m_ctrlHeld = held; }
    void SetAltHeld(bool held) { m_altHeld = held; }

    // When shift is held, switch to opposite action (raise <-> lower)
    BrushMode GetEffectiveMode() const;

private:
    std::unique_ptr<TerrainBrush> m_activeBrush;
    std::unordered_map<std::string, BrushSettings> m_presets;

    bool m_shiftHeld = false;
    bool m_ctrlHeld = false;
    bool m_altHeld = false;

    std::string m_presetsPath;
};

// ============================================================================
// Brush Visualization
// ============================================================================

struct BrushVisualization {
    // Ring visualization
    std::vector<glm::vec3> outerRing;
    std::vector<glm::vec3> innerRing;
    std::vector<glm::vec3> falloffRing;

    // Surface preview (affected heightmap)
    std::vector<glm::vec3> previewPositions;
    std::vector<glm::vec3> previewNormals;
    std::vector<float> previewHeights;

    // Arrow for direction (clone, stamp)
    glm::vec3 directionArrowStart;
    glm::vec3 directionArrowEnd;
    bool showDirectionArrow = false;

    // Colors
    glm::vec4 outerColor = glm::vec4(0.0f, 0.5f, 1.0f, 0.8f);
    glm::vec4 innerColor = glm::vec4(1.0f, 0.5f, 0.0f, 0.6f);
    glm::vec4 previewColor = glm::vec4(0.0f, 1.0f, 0.0f, 0.3f);
};

// Generate brush visualization data
BrushVisualization GenerateBrushVisualization(const TerrainBrush& brush);

} // namespace Cortex::Editor
