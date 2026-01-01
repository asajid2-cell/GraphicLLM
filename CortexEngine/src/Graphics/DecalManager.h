#pragma once

// DecalManager.h
// Deferred decal system for dynamic surface marking.
// Supports footprints, scorch marks, blood splatter, graffiti, etc.
// Reference: "Decals in The Last of Us" - GDC
// Reference: "Deferred Decals" - Wicked Engine

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>
#include <memory>

namespace Cortex::Graphics {

// Forward declarations
class Renderer;
struct TextureHandle;

// Decal blend modes
enum class DecalBlendMode : uint8_t {
    Replace = 0,            // Replace target values
    Multiply = 1,           // Multiply with existing
    Additive = 2,           // Add to existing
    AlphaBlend = 3,         // Standard alpha blending
    Overlay = 4             // Photoshop-style overlay
};

// Decal render channels
enum class DecalChannels : uint8_t {
    None = 0,
    Albedo = 1 << 0,        // Modify diffuse color
    Normal = 1 << 1,        // Modify surface normal
    Roughness = 1 << 2,     // Modify roughness
    Metallic = 1 << 3,      // Modify metallic
    Emissive = 1 << 4,      // Add emissive
    All = Albedo | Normal | Roughness | Metallic | Emissive
};

inline DecalChannels operator|(DecalChannels a, DecalChannels b) {
    return static_cast<DecalChannels>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline DecalChannels operator&(DecalChannels a, DecalChannels b) {
    return static_cast<DecalChannels>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

// Decal priority (higher = rendered last, on top)
enum class DecalPriority : uint8_t {
    VeryLow = 0,            // Environment details
    Low = 1,                // Footprints, tire tracks
    Normal = 2,             // Generic marks
    High = 3,               // Blood, burns
    VeryHigh = 4,           // Critical gameplay markers
    COUNT
};

// Decal instance data
struct Decal {
    uint32_t id = 0;

    // Transform
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 size = glm::vec3(1.0f);   // Width, height, depth (projection distance)

    // Textures (indices into decal atlas or texture array)
    uint32_t albedoTexIndex = 0;
    uint32_t normalTexIndex = 0;
    uint32_t maskTexIndex = 0;          // Alpha mask / roughness / metallic packed

    // Appearance
    glm::vec4 color = glm::vec4(1.0f);  // Tint and alpha
    float normalStrength = 1.0f;
    float roughnessModifier = 0.0f;     // -1 to 1, added to surface roughness
    float metallicModifier = 0.0f;      // -1 to 1

    // Blending
    DecalBlendMode blendMode = DecalBlendMode::AlphaBlend;
    DecalChannels channels = DecalChannels::All;
    DecalPriority priority = DecalPriority::Normal;

    // Fade
    float fadeDistance = 50.0f;         // Distance at which decal starts fading
    float angleFade = 0.7f;             // Dot product threshold for angle-based fade

    // Lifetime
    float lifetime = -1.0f;             // -1 = permanent
    float age = 0.0f;
    float fadeInTime = 0.1f;
    float fadeOutTime = 0.5f;

    // State
    bool enabled = true;
    bool isDynamic = false;             // Updated frequently (e.g., following entity)

    // Calculated bounds (AABB for culling)
    glm::vec3 aabbMin;
    glm::vec3 aabbMax;

    // Calculate OBB to AABB
    void UpdateBounds();
};

// Decal template for spawning
struct DecalTemplate {
    std::string name;

    // Texture names
    std::string albedoTexture;
    std::string normalTexture;
    std::string maskTexture;

    // Default values
    glm::vec3 sizeMin = glm::vec3(1.0f);
    glm::vec3 sizeMax = glm::vec3(1.0f);
    glm::vec4 color = glm::vec4(1.0f);
    float normalStrength = 1.0f;
    float roughnessModifier = 0.0f;
    float metallicModifier = 0.0f;

    DecalBlendMode blendMode = DecalBlendMode::AlphaBlend;
    DecalChannels channels = DecalChannels::All;
    DecalPriority priority = DecalPriority::Normal;

    float lifetime = -1.0f;
    float fadeDistance = 50.0f;
    float angleFade = 0.7f;

    // Variation
    float rotationVariation = 360.0f;   // Random rotation range in degrees
    float sizeVariation = 0.1f;         // Random size multiplier
    float colorVariation = 0.0f;        // Random color tint
};

// GPU constant buffer for decal rendering
struct alignas(16) DecalCB {
    glm::mat4 decalMatrix;              // World to decal space
    glm::mat4 decalMatrixInv;           // Decal space to world
    glm::vec4 decalColor;               // RGBA tint
    glm::vec4 decalParams;              // x=normalStrength, y=roughnessMod, z=metallicMod, w=angleFade
    glm::vec4 decalParams2;             // x=fadeDistance, y=age/lifetime, z=blendMode, w=channels
    glm::vec4 decalSize;                // xyz=size, w=unused
};

// Decal batch for rendering
struct DecalBatch {
    uint32_t albedoTexIndex;
    uint32_t normalTexIndex;
    uint32_t maskTexIndex;
    DecalBlendMode blendMode;
    std::vector<uint32_t> decalIndices;
};

class DecalManager {
public:
    DecalManager();
    ~DecalManager();

    // Initialize
    void Initialize(Renderer* renderer);
    void Shutdown();

    // Update (age decals, remove expired)
    void Update(float deltaTime);

    // Template management
    void RegisterTemplate(const std::string& name, const DecalTemplate& decalTemplate);
    const DecalTemplate* GetTemplate(const std::string& name) const;

    // Decal spawning
    uint32_t SpawnDecal(const Decal& decal);
    uint32_t SpawnFromTemplate(const std::string& templateName,
                                const glm::vec3& position,
                                const glm::vec3& normal,
                                float scale = 1.0f);
    uint32_t SpawnFromTemplate(const std::string& templateName,
                                const glm::vec3& position,
                                const glm::quat& rotation,
                                float scale = 1.0f);

    // Decal management
    void RemoveDecal(uint32_t id);
    void RemoveAllDecals();
    void RemoveDecalsInRadius(const glm::vec3& center, float radius);
    void RemoveDecalsOlderThan(float age);

    // Decal access
    Decal* GetDecal(uint32_t id);
    const Decal* GetDecal(uint32_t id) const;

    // Culling and rendering
    void CullDecals(const glm::vec3& cameraPos,
                    const glm::mat4& viewProj,
                    std::vector<uint32_t>& visibleDecals);

    void SortDecals(std::vector<uint32_t>& decals, const glm::vec3& cameraPos);

    void BatchDecals(const std::vector<uint32_t>& decals,
                     std::vector<DecalBatch>& batches);

    // Get constant buffer data for a decal
    DecalCB GetDecalCB(uint32_t decalId) const;

    // Statistics
    uint32_t GetActiveDecalCount() const { return static_cast<uint32_t>(m_activeDecals.size()); }
    uint32_t GetTotalDecalCount() const { return static_cast<uint32_t>(m_decals.size()); }
    uint32_t GetPoolSize() const { return static_cast<uint32_t>(m_freeIndices.size()); }

    // Limits
    void SetMaxDecals(uint32_t max) { m_maxDecals = max; }
    uint32_t GetMaxDecals() const { return m_maxDecals; }

    // Settings
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

    void SetFadeMultiplier(float mult) { m_fadeMultiplier = mult; }
    float GetFadeMultiplier() const { return m_fadeMultiplier; }

    // Load templates from JSON config
    void LoadTemplatesFromConfig(const std::string& configPath);

private:
    // Allocate a decal slot
    uint32_t AllocateDecal();

    // Return decal to pool
    void FreeDecal(uint32_t index);

    // Find lowest priority decal for replacement
    uint32_t FindLowestPriorityDecal() const;

    // Frustum culling test
    bool IsDecalVisible(const Decal& decal,
                        const glm::vec3& cameraPos,
                        const glm::mat4& viewProj) const;

    // Random helpers
    float RandomFloat(float min, float max);
    glm::vec3 RandomVector3(const glm::vec3& min, const glm::vec3& max);

private:
    Renderer* m_renderer = nullptr;

    // Decal storage
    std::vector<Decal> m_decals;
    std::vector<uint32_t> m_activeDecals;       // Indices of active decals
    std::vector<uint32_t> m_freeIndices;        // Pool of free slots

    // Templates
    std::unordered_map<std::string, DecalTemplate> m_templates;

    // ID generation
    uint32_t m_nextId = 1;

    // Limits
    uint32_t m_maxDecals = 2000;
    uint32_t m_maxDecalsPerPriority[static_cast<size_t>(DecalPriority::COUNT)] = {
        500,    // VeryLow
        400,    // Low
        400,    // Normal
        400,    // High
        300     // VeryHigh
    };

    // Settings
    bool m_enabled = true;
    float m_fadeMultiplier = 1.0f;

    // Random state
    uint32_t m_randomSeed = 12345;
};

// Decal spawner helper for common effects
class DecalSpawner {
public:
    DecalSpawner(DecalManager* manager);

    // Footprints
    void SpawnFootprint(const glm::vec3& position,
                        const glm::vec3& forward,
                        bool isLeftFoot,
                        const std::string& surfaceType = "default");

    // Tire tracks
    void SpawnTireTrack(const glm::vec3& start,
                        const glm::vec3& end,
                        float width,
                        const std::string& surfaceType = "default");

    // Impacts
    void SpawnBulletHole(const glm::vec3& position,
                         const glm::vec3& normal,
                         const std::string& surfaceType = "default");

    void SpawnExplosionMark(const glm::vec3& position,
                            float radius,
                            float intensity = 1.0f);

    void SpawnBloodSplatter(const glm::vec3& position,
                            const glm::vec3& direction,
                            float intensity = 1.0f);

    // Environmental
    void SpawnWaterPuddle(const glm::vec3& position, float size);
    void SpawnMossGrowth(const glm::vec3& position, float size);
    void SpawnCracks(const glm::vec3& position, float size);

    // Custom
    void SpawnCustom(const std::string& templateName,
                     const glm::vec3& position,
                     const glm::vec3& normal,
                     float scale = 1.0f);

private:
    DecalManager* m_manager;
};

} // namespace Cortex::Graphics
