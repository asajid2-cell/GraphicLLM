#pragma once

// VegetationTypes.h
// Data structures for procedural vegetation spawning and rendering.
// Supports hybrid LOD with 3D meshes near camera, billboards at distance.

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace Cortex::Scene {

// Forward declarations
enum class BiomeType : uint8_t;

// Vegetation type categories
enum class VegetationType : uint8_t {
    Tree = 0,
    Bush = 1,
    Grass = 2,
    Flower = 3,
    Rock = 4,
    Debris = 5,
    COUNT
};

// LOD level for vegetation rendering
enum class VegetationLOD : uint8_t {
    Full = 0,      // Full 3D mesh with all details
    Medium = 1,    // Simplified mesh
    Low = 2,       // Very low poly
    Billboard = 3, // 2D billboard/impostor
    Culled = 4     // Not rendered (too far)
};

// Single vegetation instance in world
struct VegetationInstance {
    glm::vec3 position;          // World position
    glm::quat rotation;          // Orientation
    glm::vec3 scale;             // Non-uniform scale
    uint32_t prototypeIndex;     // Index into prototype array
    VegetationLOD currentLOD;    // Current LOD level
    float distanceToCamera;      // Cached distance for sorting
    uint32_t flags;              // Visibility, shadow caster, etc.

    // Flags
    static constexpr uint32_t FLAG_VISIBLE = 1 << 0;
    static constexpr uint32_t FLAG_SHADOW_CASTER = 1 << 1;
    static constexpr uint32_t FLAG_WIND_AFFECTED = 1 << 2;
    static constexpr uint32_t FLAG_COLLISION = 1 << 3;

    bool IsVisible() const { return (flags & FLAG_VISIBLE) != 0; }
    bool CastsShadow() const { return (flags & FLAG_SHADOW_CASTER) != 0; }
    bool IsWindAffected() const { return (flags & FLAG_WIND_AFFECTED) != 0; }
    bool HasCollision() const { return (flags & FLAG_COLLISION) != 0; }
};

// GPU-friendly instance data for instanced rendering
struct alignas(16) VegetationInstanceGPU {
    glm::mat4 worldMatrix;       // 64 bytes - full transform
    glm::vec4 colorTint;         // 16 bytes - per-instance color variation
    glm::vec4 windParams;        // 16 bytes - x=phase, y=strength, z=frequency, w=unused
    uint32_t prototypeIndex;     // 4 bytes
    uint32_t lodLevel;           // 4 bytes
    float fadeAlpha;             // 4 bytes - for LOD crossfade
    float padding;               // 4 bytes
    // Total: 112 bytes per instance
};

// Vegetation prototype - template for spawning
struct VegetationPrototype {
    std::string name;
    VegetationType type = VegetationType::Tree;

    // Mesh references (paths or asset IDs)
    std::string meshPathLOD0;    // Highest quality mesh
    std::string meshPathLOD1;    // Medium quality
    std::string meshPathLOD2;    // Low quality
    std::string billboardAtlas;  // Billboard texture atlas

    // LOD distances (world units)
    float lodDistance0 = 50.0f;  // Switch from LOD0 to LOD1
    float lodDistance1 = 100.0f; // Switch from LOD1 to LOD2
    float lodDistance2 = 200.0f; // Switch from LOD2 to Billboard
    float cullDistance = 500.0f; // Beyond this, don't render

    // LOD crossfade range (0 = instant switch, 10 = gradual fade)
    float crossfadeRange = 5.0f;

    // Scale variation
    glm::vec3 minScale = glm::vec3(0.8f);
    glm::vec3 maxScale = glm::vec3(1.2f);
    bool uniformScale = true;    // If true, use same scale for x,y,z

    // Rotation constraints
    bool alignToTerrain = false; // Align up vector to terrain normal
    float randomYawRange = 360.0f; // Random rotation around Y axis (degrees)

    // Placement constraints
    float minSlope = 0.0f;       // Minimum terrain slope (0 = flat)
    float maxSlope = 0.5f;       // Maximum terrain slope (1 = vertical)
    float minHeight = -1000.0f;  // Minimum terrain height
    float maxHeight = 1000.0f;   // Maximum terrain height

    // Visual properties
    glm::vec4 colorVariationMin = glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);
    glm::vec4 colorVariationMax = glm::vec4(1.1f, 1.1f, 1.1f, 1.0f);
    float windStrength = 1.0f;   // How much wind affects this prototype
    bool castShadows = true;

    // Collision
    float collisionRadius = 0.0f; // 0 = no collision
    float collisionHeight = 0.0f;
};

// Per-biome vegetation density settings
struct BiomeVegetationDensity {
    float treeDensity = 0.0f;    // Trees per square unit
    float bushDensity = 0.0f;
    float grassDensity = 0.0f;
    float flowerDensity = 0.0f;
    float rockDensity = 0.0f;

    // Prototype weights within each category
    // Maps prototype index to spawn weight (higher = more likely)
    std::vector<std::pair<uint32_t, float>> treeWeights;
    std::vector<std::pair<uint32_t, float>> bushWeights;
    std::vector<std::pair<uint32_t, float>> grassWeights;
    std::vector<std::pair<uint32_t, float>> flowerWeights;
    std::vector<std::pair<uint32_t, float>> rockWeights;
};

// Sampling method for procedural placement
// Reference: Academic algorithms for natural point distribution
enum class SamplingMethod : uint8_t {
    Random = 0,            // Simple random (fast, poor distribution)
    PoissonDisk = 1,       // Bridson's Poisson Disk (balanced, natural)
    BlueNoise = 2,         // Tile-based blue noise (very fast, tileable)
    PoissonRelaxed = 3,    // Poisson + Lloyd relaxation (best quality, slower)
    Stratified = 4,        // Stratified jittered (good for dense vegetation)
    COUNT
};

// Vegetation spawning parameters
struct VegetationSpawnParams {
    uint32_t seed = 42;

    // Density multiplier (global)
    float densityMultiplier = 1.0f;

    // Spacing constraints
    float minTreeSpacing = 5.0f;     // Minimum distance between trees
    float minBushSpacing = 2.0f;
    float minGrassSpacing = 0.5f;

    // Placement algorithm
    bool usePoissonDisk = true;      // Legacy flag (use samplingMethod instead)
    SamplingMethod samplingMethod = SamplingMethod::PoissonDisk;
    int poissonMaxAttempts = 30;     // Max attempts per sample point (Bridson's k)

    // Advanced sampling options
    bool useVariableDensity = true;  // Sample density varies by biome/terrain
    int lloydRelaxIterations = 5;    // Lloyd relaxation iterations (for PoissonRelaxed)
    float blueNoiseTileSize = 64.0f; // World-space tile size for blue noise

    // Biome blending
    float biomeBlendRadius = 10.0f;  // Vegetation follows biome boundaries

    // Cluster settings (for forest clumps, flower patches)
    bool enableClustering = false;   // Group vegetation into clusters
    float clusterRadius = 15.0f;     // Maximum cluster size
    float clusterChance = 0.3f;      // Probability of starting a cluster

    // Avoid rules (prevent placement near certain features)
    float avoidWaterDistance = 2.0f; // Minimum distance from water
    float avoidRiverDistance = 3.0f; // Minimum distance from rivers
    float avoidPathDistance = 1.0f;  // Minimum distance from paths

    // Performance
    uint32_t maxInstancesPerChunk = 10000;
    float updateDistanceThreshold = 50.0f; // Re-evaluate LODs when camera moves this far
};

// Chunk-level vegetation data
struct VegetationChunk {
    int32_t chunkX = 0;
    int32_t chunkZ = 0;
    std::vector<VegetationInstance> instances;

    // Cached bounds for frustum culling
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;

    // State
    bool isDirty = true;         // Needs re-upload to GPU
    bool isLoaded = false;       // Has been spawned
    uint32_t gpuBufferOffset = 0; // Offset in global instance buffer
    uint32_t instanceCount = 0;
};

// Billboard vertex for vegetation impostors
struct BillboardVertex {
    glm::vec3 position;          // World position (center of billboard)
    glm::vec2 size;              // Width, height
    glm::vec2 texCoordMin;       // UV min (atlas region)
    glm::vec2 texCoordMax;       // UV max
    glm::vec4 color;             // Tint color
    float rotation;              // Rotation around view axis
};

// Billboard atlas region
struct BillboardAtlasEntry {
    uint32_t prototypeIndex;
    glm::vec2 uvMin;
    glm::vec2 uvMax;
    float aspectRatio;           // Width / Height
    int viewAngleIndex;          // For multi-view impostors
};

// Wind parameters for vegetation animation
struct WindParams {
    glm::vec2 direction = glm::vec2(1.0f, 0.0f);
    float speed = 1.0f;
    float gustStrength = 0.3f;
    float gustFrequency = 0.5f;
    float turbulence = 0.2f;
    float time = 0.0f;           // Accumulated time for animation
};

// GPU constant buffer for vegetation rendering
struct alignas(16) VegetationConstantsCB {
    glm::mat4 viewProj;
    glm::vec4 cameraPosition;    // xyz = position, w = unused
    glm::vec4 windDirection;     // xy = direction, z = speed, w = time
    glm::vec4 windParams;        // x = gustStrength, y = gustFreq, z = turbulence, w = unused
    glm::vec4 lodDistances;      // x = lod0, y = lod1, z = lod2, w = cull
    glm::vec4 fadeParams;        // x = crossfadeRange, yzw = unused
};

// Vegetation render batch - instances grouped by prototype and LOD
struct VegetationBatch {
    uint32_t prototypeIndex;
    VegetationLOD lodLevel;
    uint32_t startIndex;         // Start in instance buffer
    uint32_t instanceCount;
    uint32_t meshIndex;          // Which mesh to use for this LOD
};

// Vegetation system statistics
struct VegetationStats {
    uint32_t totalInstances = 0;
    uint32_t visibleInstances = 0;
    uint32_t culledInstances = 0;
    uint32_t lod0Count = 0;
    uint32_t lod1Count = 0;
    uint32_t lod2Count = 0;
    uint32_t billboardCount = 0;
    uint32_t drawCalls = 0;
    uint32_t trianglesRendered = 0;
};

} // namespace Cortex::Scene
