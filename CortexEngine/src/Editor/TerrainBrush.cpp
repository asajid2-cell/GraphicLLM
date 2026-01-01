// TerrainBrush.cpp
// Terrain sculpting brush implementation.

#include "TerrainBrush.h"
#include "ChunkGenerator.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <random>

namespace Cortex::Editor {

// ============================================================================
// BrushSettings Implementation
// ============================================================================

float BrushSettings::GetFalloff(float normalizedDistance) const {
    if (normalizedDistance <= 0.0f) return 1.0f;
    if (normalizedDistance >= 1.0f) return 0.0f;

    float t = normalizedDistance;
    float result = 0.0f;

    switch (falloff) {
        case BrushFalloff::Linear:
            result = 1.0f - t;
            break;

        case BrushFalloff::Smooth:
            // Smooth cosine falloff
            result = (std::cos(t * 3.14159265f) + 1.0f) * 0.5f;
            break;

        case BrushFalloff::Spherical:
            // Spherical falloff (sqrt-based)
            result = std::sqrt(1.0f - t * t);
            break;

        case BrushFalloff::Tip:
            // Sharp tip (exponential)
            result = std::exp(-t * falloffAmount * 4.0f);
            break;

        case BrushFalloff::Flat:
            // No falloff
            result = 1.0f;
            break;

        case BrushFalloff::Custom:
            // Would sample custom curve
            result = 1.0f - t;
            break;
    }

    if (invertFalloff) {
        result = 1.0f - result;
    }

    return result;
}

float BrushSettings::GetIntensityAt(const glm::vec3& center, const glm::vec3& position) const {
    glm::vec3 offset = position - center;

    float distance = 0.0f;
    switch (shape) {
        case BrushShape::Circle:
            distance = std::sqrt(offset.x * offset.x + offset.z * offset.z);
            break;

        case BrushShape::Square:
            distance = std::max(std::abs(offset.x), std::abs(offset.z));
            break;

        case BrushShape::Custom:
            // Would sample custom mask
            distance = std::sqrt(offset.x * offset.x + offset.z * offset.z);
            break;
    }

    float normalizedDistance = distance / radius;
    return GetFalloff(normalizedDistance) * strength;
}

// ============================================================================
// BrushStroke Implementation
// ============================================================================

float BrushStroke::GetLength() const {
    float length = 0.0f;
    for (size_t i = 1; i < path.size(); ++i) {
        length += glm::distance(path[i - 1], path[i]);
    }
    return length;
}

glm::vec3 BrushStroke::GetDirection() const {
    if (path.size() < 2) {
        return glm::vec3(0, 0, 1);
    }
    return glm::normalize(path.back() - path[path.size() - 2]);
}

void BrushStroke::Clear() {
    startPosition = glm::vec3(0);
    endPosition = glm::vec3(0);
    currentPosition = glm::vec3(0);
    startTime = 0;
    currentTime = 0;
    lastApplyTime = 0;
    path.clear();
    pressures.clear();
    isActive = false;
    isDragging = false;
}

// ============================================================================
// TerrainBrush Implementation
// ============================================================================

TerrainBrush::TerrainBrush() {}

TerrainBrush::~TerrainBrush() {}

void TerrainBrush::Initialize(ChunkGenerator* chunkGen) {
    m_chunkGen = chunkGen;
}

void TerrainBrush::Update(float deltaTime) {
    if (!m_stroke.isActive) return;

    m_stroke.currentTime += deltaTime;

    // Apply based on spacing
    float strokeLength = m_stroke.GetLength();
    float spacingDistance = m_settings.radius * m_settings.spacing * 2.0f;

    if (spacingDistance > 0.0f) {
        float timeSinceApply = m_stroke.currentTime - m_stroke.lastApplyTime;
        if (timeSinceApply >= 0.016f) {  // ~60 Hz max rate
            // Apply if cursor has moved enough
            if (!m_stroke.path.empty()) {
                float distFromLast = glm::distance(m_cursorPosition, m_stroke.path.back());
                if (distFromLast >= spacingDistance) {
                    ApplyAtPosition(m_cursorPosition, 1.0f);
                    m_stroke.path.push_back(m_cursorPosition);
                    m_stroke.pressures.push_back(1.0f);
                    m_stroke.lastApplyTime = m_stroke.currentTime;
                }
            }
        }
    }
}

void TerrainBrush::BeginStroke(const glm::vec3& position, float pressure) {
    m_stroke.Clear();
    m_stroke.isActive = true;
    m_stroke.isDragging = true;
    m_stroke.startPosition = position;
    m_stroke.currentPosition = position;
    m_stroke.startTime = 0;
    m_stroke.currentTime = 0;
    m_stroke.lastApplyTime = 0;

    m_stroke.path.push_back(position);
    m_stroke.pressures.push_back(pressure);

    BeginUndoGroup();
    ApplyAtPosition(position, pressure);
}

void TerrainBrush::UpdateStroke(const glm::vec3& position, float pressure) {
    if (!m_stroke.isActive) return;

    m_stroke.currentPosition = position;
    m_cursorPosition = position;

    // Update will handle application based on spacing
}

void TerrainBrush::EndStroke() {
    if (!m_stroke.isActive) return;

    m_stroke.endPosition = m_stroke.currentPosition;
    m_stroke.isActive = false;
    m_stroke.isDragging = false;

    EndUndoGroup();

    if (m_onStrokeEnd) {
        m_onStrokeEnd();
    }
}

void TerrainBrush::CancelStroke() {
    if (!m_stroke.isActive) return;

    m_stroke.isActive = false;
    m_stroke.isDragging = false;

    // Undo any changes from this stroke
    if (m_currentUndo) {
        // Revert changes
        for (const auto& change : m_currentUndo->heightChanges) {
            // Would restore original height
        }
        m_currentUndo.reset();
    }
}

void TerrainBrush::ApplyAtPosition(const glm::vec3& position, float pressure) {
    if (!m_chunkGen) return;

    switch (m_settings.mode) {
        case BrushMode::Raise:
            ApplyRaise(position, pressure);
            break;
        case BrushMode::Lower:
            ApplyLower(position, pressure);
            break;
        case BrushMode::Smooth:
            ApplySmooth(position, pressure);
            break;
        case BrushMode::Flatten:
            ApplyFlatten(position, pressure);
            break;
        case BrushMode::Noise:
            ApplyNoise(position, pressure);
            break;
        case BrushMode::Paint:
            ApplyPaint(position, pressure);
            break;
        case BrushMode::Stamp:
            ApplyStamp(position, pressure);
            break;
        case BrushMode::Erode:
            ApplyErode(position, pressure);
            break;
        case BrushMode::Clone:
            ApplyClone(position, pressure);
            break;
        case BrushMode::Mask:
            ApplyMask(position, pressure);
            break;
    }

    if (m_onApply) {
        m_onApply(position, m_settings.radius, pressure * m_settings.strength);
    }
}

void TerrainBrush::ApplyRaise(const glm::vec3& center, float pressure) {
    TerrainBounds bounds = GetAffectedBounds(center);

    float deltaHeight = m_settings.strength * pressure * 0.1f;

    for (int z = bounds.vertexMin.y; z <= bounds.vertexMax.y; ++z) {
        for (int x = bounds.vertexMin.x; x <= bounds.vertexMax.x; ++x) {
            glm::vec3 worldPos(x, 0, z);  // Would convert to world coords
            float intensity = m_settings.GetIntensityAt(center, worldPos);

            if (intensity > 0.0f) {
                float currentHeight = GetHeight(x, z);
                float newHeight = currentHeight + deltaHeight * intensity;
                SetHeight(x, z, newHeight);
            }
        }
    }
}

void TerrainBrush::ApplyLower(const glm::vec3& center, float pressure) {
    TerrainBounds bounds = GetAffectedBounds(center);

    float deltaHeight = m_settings.strength * pressure * 0.1f;

    for (int z = bounds.vertexMin.y; z <= bounds.vertexMax.y; ++z) {
        for (int x = bounds.vertexMin.x; x <= bounds.vertexMax.x; ++x) {
            glm::vec3 worldPos(x, 0, z);
            float intensity = m_settings.GetIntensityAt(center, worldPos);

            if (intensity > 0.0f) {
                float currentHeight = GetHeight(x, z);
                float newHeight = currentHeight - deltaHeight * intensity;
                SetHeight(x, z, newHeight);
            }
        }
    }
}

void TerrainBrush::ApplySmooth(const glm::vec3& center, float pressure) {
    TerrainBounds bounds = GetAffectedBounds(center);

    // First pass: calculate average heights
    std::vector<float> newHeights;
    newHeights.reserve((bounds.vertexMax.x - bounds.vertexMin.x + 1) *
                        (bounds.vertexMax.y - bounds.vertexMin.y + 1));

    for (int z = bounds.vertexMin.y; z <= bounds.vertexMax.y; ++z) {
        for (int x = bounds.vertexMin.x; x <= bounds.vertexMax.x; ++x) {
            glm::vec3 worldPos(x, 0, z);
            float intensity = m_settings.GetIntensityAt(center, worldPos);

            if (intensity > 0.0f) {
                // Sample neighbors
                float sum = 0.0f;
                int count = 0;

                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        sum += GetHeight(x + dx, z + dz);
                        count++;
                    }
                }

                float avgHeight = sum / static_cast<float>(count);
                float currentHeight = GetHeight(x, z);
                float blendedHeight = currentHeight + (avgHeight - currentHeight) *
                                      intensity * m_settings.strength * pressure;
                newHeights.push_back(blendedHeight);
            } else {
                newHeights.push_back(GetHeight(x, z));
            }
        }
    }

    // Second pass: apply new heights
    size_t idx = 0;
    for (int z = bounds.vertexMin.y; z <= bounds.vertexMax.y; ++z) {
        for (int x = bounds.vertexMin.x; x <= bounds.vertexMax.x; ++x) {
            SetHeight(x, z, newHeights[idx++]);
        }
    }
}

void TerrainBrush::ApplyFlatten(const glm::vec3& center, float pressure) {
    TerrainBounds bounds = GetAffectedBounds(center);

    float targetHeight = m_settings.targetHeight;
    if (m_settings.useWorldHeight) {
        targetHeight = center.y;
    }

    for (int z = bounds.vertexMin.y; z <= bounds.vertexMax.y; ++z) {
        for (int x = bounds.vertexMin.x; x <= bounds.vertexMax.x; ++x) {
            glm::vec3 worldPos(x, 0, z);
            float intensity = m_settings.GetIntensityAt(center, worldPos);

            if (intensity > 0.0f) {
                float currentHeight = GetHeight(x, z);
                float newHeight = currentHeight + (targetHeight - currentHeight) *
                                  intensity * m_settings.strength * pressure;
                SetHeight(x, z, newHeight);
            }
        }
    }
}

void TerrainBrush::ApplyNoise(const glm::vec3& center, float pressure) {
    TerrainBounds bounds = GetAffectedBounds(center);

    // Simple noise implementation
    std::mt19937 rng(m_noiseSeed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    for (int z = bounds.vertexMin.y; z <= bounds.vertexMax.y; ++z) {
        for (int x = bounds.vertexMin.x; x <= bounds.vertexMax.x; ++x) {
            glm::vec3 worldPos(x, 0, z);
            float intensity = m_settings.GetIntensityAt(center, worldPos);

            if (intensity > 0.0f) {
                // Generate noise value (would use proper Perlin/Simplex in production)
                float noiseX = x * m_settings.noiseScale;
                float noiseZ = z * m_settings.noiseScale;

                // Simple hash-based noise
                int hash = (x * 374761393 + z * 668265263) ^ m_noiseSeed;
                float noise = (static_cast<float>(hash & 0xFFFF) / 32768.0f) - 1.0f;

                float currentHeight = GetHeight(x, z);
                float noiseValue = noise * m_settings.noiseAmplitude;
                float newHeight = currentHeight + noiseValue * intensity *
                                  m_settings.strength * pressure;
                SetHeight(x, z, newHeight);
            }
        }
    }
}

void TerrainBrush::ApplyPaint(const glm::vec3& center, float pressure) {
    // Would modify biome/texture splatmap
    (void)center; (void)pressure;
}

void TerrainBrush::ApplyStamp(const glm::vec3& center, float pressure) {
    if (m_stampData.empty()) return;

    TerrainBounds bounds = GetAffectedBounds(center);

    for (int z = bounds.vertexMin.y; z <= bounds.vertexMax.y; ++z) {
        for (int x = bounds.vertexMin.x; x <= bounds.vertexMax.x; ++x) {
            glm::vec3 worldPos(x, 0, z);
            float intensity = m_settings.GetIntensityAt(center, worldPos);

            if (intensity > 0.0f) {
                // Sample stamp texture
                float u = (x - center.x + m_settings.radius) / (m_settings.radius * 2.0f);
                float v = (z - center.z + m_settings.radius) / (m_settings.radius * 2.0f);

                if (u >= 0 && u <= 1 && v >= 0 && v <= 1) {
                    int stampX = static_cast<int>(u * (m_stampWidth - 1));
                    int stampZ = static_cast<int>(v * (m_stampHeight - 1));
                    float stampValue = m_stampData[stampZ * m_stampWidth + stampX];

                    float currentHeight = GetHeight(x, z);
                    float newHeight = currentHeight + stampValue * intensity *
                                      m_settings.strength * pressure;
                    SetHeight(x, z, newHeight);
                }
            }
        }
    }
}

void TerrainBrush::ApplyErode(const glm::vec3& center, float pressure) {
    // Simple thermal erosion
    TerrainBounds bounds = GetAffectedBounds(center);

    const float talusAngle = 0.5f;  // Maximum slope before material slides

    for (int z = bounds.vertexMin.y + 1; z < bounds.vertexMax.y; ++z) {
        for (int x = bounds.vertexMin.x + 1; x < bounds.vertexMax.x; ++x) {
            glm::vec3 worldPos(x, 0, z);
            float intensity = m_settings.GetIntensityAt(center, worldPos);

            if (intensity > 0.0f) {
                float centerHeight = GetHeight(x, z);

                // Check neighbors
                float maxDiff = 0.0f;
                int maxDx = 0, maxDz = 0;

                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dz == 0) continue;

                        float neighborHeight = GetHeight(x + dx, z + dz);
                        float diff = centerHeight - neighborHeight;

                        if (diff > maxDiff) {
                            maxDiff = diff;
                            maxDx = dx;
                            maxDz = dz;
                        }
                    }
                }

                // Apply erosion if slope exceeds talus angle
                if (maxDiff > talusAngle) {
                    float erodeAmount = (maxDiff - talusAngle) * 0.5f *
                                        intensity * m_settings.strength * pressure;

                    SetHeight(x, z, centerHeight - erodeAmount);
                    SetHeight(x + maxDx, z + maxDz,
                              GetHeight(x + maxDx, z + maxDz) + erodeAmount);
                }
            }
        }
    }
}

void TerrainBrush::ApplyClone(const glm::vec3& center, float pressure) {
    if (!m_cloneSourceSet) return;

    TerrainBounds bounds = GetAffectedBounds(center);
    glm::vec3 offset = center - m_cloneSource;

    for (int z = bounds.vertexMin.y; z <= bounds.vertexMax.y; ++z) {
        for (int x = bounds.vertexMin.x; x <= bounds.vertexMax.x; ++x) {
            glm::vec3 worldPos(x, 0, z);
            float intensity = m_settings.GetIntensityAt(center, worldPos);

            if (intensity > 0.0f) {
                // Get height from source location
                int srcX = x - static_cast<int>(offset.x);
                int srcZ = z - static_cast<int>(offset.z);
                float sourceHeight = GetHeight(srcX, srcZ);

                float currentHeight = GetHeight(x, z);
                float newHeight = currentHeight + (sourceHeight - currentHeight) *
                                  intensity * m_settings.strength * pressure;
                SetHeight(x, z, newHeight);
            }
        }
    }
}

void TerrainBrush::ApplyMask(const glm::vec3& center, float pressure) {
    // Would modify mask layer used for other operations
    (void)center; (void)pressure;
}

void TerrainBrush::ModifyHeight(int x, int z, float delta) {
    float current = GetHeight(x, z);
    SetHeight(x, z, current + delta);
}

float TerrainBrush::GetHeight(int x, int z) const {
    if (!m_chunkGen) return 0.0f;
    // Would get height from terrain system
    (void)x; (void)z;
    return 0.0f;
}

void TerrainBrush::SetHeight(int x, int z, float height) {
    if (!m_chunkGen) return;

    // Record for undo
    if (m_currentUndo) {
        m_currentUndo->heightChanges.push_back({{x, z}, GetHeight(x, z)});
    }

    // Would set height in terrain system
    (void)x; (void)z; (void)height;
}

void TerrainBrush::PushUndo(const std::string& description) {
    if (!m_currentUndo) return;

    m_currentUndo->description = description;
    m_undoStack.push_back(std::move(*m_currentUndo));
    m_currentUndo.reset();

    // Clear redo stack
    m_redoStack.clear();

    // Limit undo history
    while (m_undoStack.size() > m_maxUndoLevels) {
        m_undoStack.erase(m_undoStack.begin());
    }
}

void TerrainBrush::BeginUndoGroup() {
    m_currentUndo = std::make_unique<UndoData>();
    m_inUndoGroup = true;
}

void TerrainBrush::EndUndoGroup() {
    if (m_currentUndo && !m_currentUndo->heightChanges.empty()) {
        PushUndo("Brush Stroke");
    }
    m_inUndoGroup = false;
}

void TerrainBrush::Undo() {
    if (m_undoStack.empty()) return;

    UndoData& data = m_undoStack.back();

    // Restore heights
    for (const auto& change : data.heightChanges) {
        // Save current for redo
        // SetHeight(change.first.x, change.first.y, change.second);
    }

    m_redoStack.push_back(std::move(data));
    m_undoStack.pop_back();
}

void TerrainBrush::Redo() {
    if (m_redoStack.empty()) return;

    UndoData& data = m_redoStack.back();

    // Re-apply changes
    for (const auto& change : data.heightChanges) {
        // SetHeight...
    }

    m_undoStack.push_back(std::move(data));
    m_redoStack.pop_back();
}

void TerrainBrush::ClearHistory() {
    m_undoStack.clear();
    m_redoStack.clear();
}

float TerrainBrush::SampleHeight(const glm::vec3& position) const {
    return GetHeight(static_cast<int>(position.x), static_cast<int>(position.z));
}

uint32_t TerrainBrush::SampleBiome(const glm::vec3& position) const {
    (void)position;
    return 0;
}

TerrainBrush::TerrainBounds TerrainBrush::GetAffectedBounds(const glm::vec3& position) const {
    TerrainBounds bounds;

    int radiusInt = static_cast<int>(std::ceil(m_settings.radius));

    bounds.vertexMin = glm::ivec2(
        static_cast<int>(position.x) - radiusInt,
        static_cast<int>(position.z) - radiusInt
    );
    bounds.vertexMax = glm::ivec2(
        static_cast<int>(position.x) + radiusInt,
        static_cast<int>(position.z) + radiusInt
    );

    // Would calculate chunk bounds
    bounds.chunkMin = bounds.vertexMin / 64;  // Assuming 64 verts per chunk
    bounds.chunkMax = bounds.vertexMax / 64;

    return bounds;
}

bool TerrainBrush::LoadCustomBrush(const std::string& path) {
    // Would load brush texture
    (void)path;
    return false;
}

void TerrainBrush::ClearCustomBrush() {
    m_customBrushData.clear();
    m_customBrushSize = 0;
}

// ============================================================================
// BrushManager Implementation
// ============================================================================

BrushManager::BrushManager() {}

BrushManager::~BrushManager() {}

void BrushManager::Initialize() {
    m_activeBrush = std::make_unique<TerrainBrush>();

    // Load presets from file
    // ...
}

void BrushManager::Shutdown() {
    m_activeBrush.reset();
    m_presets.clear();
}

void BrushManager::SavePreset(const std::string& name, const BrushSettings& settings) {
    m_presets[name] = settings;
    // Would save to file
}

bool BrushManager::LoadPreset(const std::string& name, BrushSettings& settings) {
    auto it = m_presets.find(name);
    if (it != m_presets.end()) {
        settings = it->second;
        return true;
    }
    return false;
}

void BrushManager::DeletePreset(const std::string& name) {
    m_presets.erase(name);
}

std::vector<std::string> BrushManager::GetPresetNames() const {
    std::vector<std::string> names;
    for (const auto& [name, settings] : m_presets) {
        names.push_back(name);
    }
    return names;
}

BrushSettings BrushManager::GetDefaultRaiseBrush() const {
    BrushSettings settings;
    settings.mode = BrushMode::Raise;
    settings.falloff = BrushFalloff::Smooth;
    settings.radius = 10.0f;
    settings.strength = 0.5f;
    return settings;
}

BrushSettings BrushManager::GetDefaultSmoothBrush() const {
    BrushSettings settings;
    settings.mode = BrushMode::Smooth;
    settings.falloff = BrushFalloff::Smooth;
    settings.radius = 15.0f;
    settings.strength = 0.8f;
    return settings;
}

BrushSettings BrushManager::GetDefaultFlattenBrush() const {
    BrushSettings settings;
    settings.mode = BrushMode::Flatten;
    settings.falloff = BrushFalloff::Smooth;
    settings.radius = 20.0f;
    settings.strength = 1.0f;
    settings.useWorldHeight = true;
    return settings;
}

BrushSettings BrushManager::GetDefaultPaintBrush() const {
    BrushSettings settings;
    settings.mode = BrushMode::Paint;
    settings.falloff = BrushFalloff::Smooth;
    settings.radius = 8.0f;
    settings.strength = 1.0f;
    return settings;
}

void BrushManager::SelectRaiseTool() {
    if (m_activeBrush) {
        m_activeBrush->SetSettings(GetDefaultRaiseBrush());
    }
}

void BrushManager::SelectLowerTool() {
    if (m_activeBrush) {
        auto settings = GetDefaultRaiseBrush();
        settings.mode = BrushMode::Lower;
        m_activeBrush->SetSettings(settings);
    }
}

void BrushManager::SelectSmoothTool() {
    if (m_activeBrush) {
        m_activeBrush->SetSettings(GetDefaultSmoothBrush());
    }
}

void BrushManager::SelectFlattenTool() {
    if (m_activeBrush) {
        m_activeBrush->SetSettings(GetDefaultFlattenBrush());
    }
}

void BrushManager::SelectPaintTool() {
    if (m_activeBrush) {
        m_activeBrush->SetSettings(GetDefaultPaintBrush());
    }
}

void BrushManager::SelectErodeTool() {
    if (m_activeBrush) {
        BrushSettings settings;
        settings.mode = BrushMode::Erode;
        settings.falloff = BrushFalloff::Smooth;
        settings.radius = 25.0f;
        settings.strength = 0.3f;
        m_activeBrush->SetSettings(settings);
    }
}

void BrushManager::SelectCloneTool() {
    if (m_activeBrush) {
        BrushSettings settings;
        settings.mode = BrushMode::Clone;
        settings.falloff = BrushFalloff::Smooth;
        settings.radius = 15.0f;
        settings.strength = 1.0f;
        m_activeBrush->SetSettings(settings);
    }
}

BrushMode BrushManager::GetEffectiveMode() const {
    if (!m_activeBrush) return BrushMode::Raise;

    BrushMode mode = m_activeBrush->GetMode();

    // Shift inverts raise/lower
    if (m_shiftHeld) {
        if (mode == BrushMode::Raise) return BrushMode::Lower;
        if (mode == BrushMode::Lower) return BrushMode::Raise;
    }

    return mode;
}

// ============================================================================
// BrushVisualization Implementation
// ============================================================================

BrushVisualization GenerateBrushVisualization(const TerrainBrush& brush) {
    BrushVisualization vis;

    const BrushSettings& settings = brush.GetSettings();
    glm::vec3 center = brush.GetCursorPosition();

    // Generate outer ring
    const int segments = 64;
    for (int i = 0; i <= segments; ++i) {
        float angle = (static_cast<float>(i) / segments) * 3.14159265f * 2.0f;
        float x = std::cos(angle) * settings.radius;
        float z = std::sin(angle) * settings.radius;

        glm::vec3 pos = center + glm::vec3(x, 0, z);
        pos.y = brush.SampleHeight(pos);
        vis.outerRing.push_back(pos);
    }

    // Generate inner/falloff ring
    float innerRadius = settings.radius * (1.0f - settings.falloffAmount);
    for (int i = 0; i <= segments; ++i) {
        float angle = (static_cast<float>(i) / segments) * 3.14159265f * 2.0f;
        float x = std::cos(angle) * innerRadius;
        float z = std::sin(angle) * innerRadius;

        glm::vec3 pos = center + glm::vec3(x, 0, z);
        pos.y = brush.SampleHeight(pos);
        vis.innerRing.push_back(pos);
    }

    return vis;
}

} // namespace Cortex::Editor
