#pragma once

// BlueNoise.h
// Blue Noise Texture-Based Sampling for Procedural Placement
// Reference: "Blue Noise through Optimal Transport" - de Goes et al.
//
// Uses precomputed blue noise textures for fast, high-quality point distribution.
// Supports tileable sampling for infinite terrain.

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <array>
#include <cstdint>

namespace Cortex::Utils {

// Blue noise texture dimensions (power of 2)
constexpr int BLUE_NOISE_SIZE = 128;
constexpr int BLUE_NOISE_LAYERS = 8;  // Multiple layers for different densities

// A single blue noise sample point
struct BlueNoiseSample {
    glm::vec2 position;     // Normalized position (0-1)
    float rank;             // Ordering rank for progressive sampling
    uint32_t layer;         // Which layer this sample came from
};

// Parameters for blue noise sampling
struct BlueNoiseParams {
    float density = 1.0f;           // Points per unit area (affects layer selection)
    float tileSize = 64.0f;         // World-space size of one tile
    uint32_t seed = 0;              // Random offset for tiling
    bool progressive = false;       // Use progressive sampling (lower ranks first)
    float progressiveRatio = 1.0f;  // 0-1, how much of the pattern to use

    // World-space bounds
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 100.0f;
    float maxY = 100.0f;
};

// Statistics for blue noise sampling
struct BlueNoiseStats {
    uint32_t totalPoints = 0;
    uint32_t tilesUsed = 0;
    float pointsPerTile = 0.0f;
    float executionTimeMs = 0.0f;
};

class BlueNoiseSampler {
public:
    BlueNoiseSampler();
    ~BlueNoiseSampler() = default;

    // Initialize with precomputed or generated blue noise texture
    bool Initialize();

    // Load blue noise texture from file
    bool LoadTexture(const std::string& path);

    // Generate blue noise pattern algorithmically
    void GeneratePattern(int size = BLUE_NOISE_SIZE, int layers = BLUE_NOISE_LAYERS);

    // Sample points in world space using blue noise pattern
    std::vector<glm::vec2> Sample(const BlueNoiseParams& params);

    // Sample with full data including rank
    std::vector<BlueNoiseSample> SampleExtended(const BlueNoiseParams& params);

    // Get a single tile's points (normalized 0-1 coordinates)
    std::vector<glm::vec2> GetTilePoints(uint32_t layer) const;

    // Get point at specific texture coordinate
    float GetBlueNoiseValue(int x, int y, int layer = 0) const;

    // Get statistics from last sampling operation
    const BlueNoiseStats& GetStats() const { return m_stats; }

    // Get optimal layer for a given density
    uint32_t GetLayerForDensity(float density) const;

    // Jitter a position using blue noise (for anti-aliasing)
    glm::vec2 Jitter(const glm::vec2& position, float amount) const;

    // Get random rotation from blue noise (for vegetation orientation)
    float GetRotation(float x, float y) const;

    // Get scale variation from blue noise
    float GetScaleVariation(float x, float y, float minScale, float maxScale) const;

private:
    // Blue noise texture data
    // Each layer contains BLUE_NOISE_SIZE^2 values
    std::array<std::vector<float>, BLUE_NOISE_LAYERS> m_textures;

    // Precomputed point lists per layer (for fast tile lookup)
    std::array<std::vector<glm::vec2>, BLUE_NOISE_LAYERS> m_tilePoints;

    // Number of points per layer
    std::array<uint32_t, BLUE_NOISE_LAYERS> m_pointsPerLayer;

    BlueNoiseStats m_stats;
    bool m_initialized = false;

    // Generate a single layer of blue noise using void-and-cluster
    std::vector<float> GenerateVoidAndCluster(int size, int numPoints);

    // Extract point positions from texture
    void ExtractPoints(int layer, int numPoints);

    // Hash function for tile offset
    glm::vec2 TileOffset(int tileX, int tileY, uint32_t seed) const;

    // Low-discrepancy sequence for fallback
    glm::vec2 HaltonSequence(int index, int baseX = 2, int baseY = 3) const;
};

// Global blue noise sampler instance
BlueNoiseSampler& GetBlueNoiseSampler();

// Convenience functions

// Sample blue noise points in a region
std::vector<glm::vec2> SampleBlueNoise(float minX, float minY, float maxX, float maxY,
                                        float density, float tileSize = 64.0f,
                                        uint32_t seed = 0);

// Get a dither threshold value (for alpha cutout/LOD transitions)
float GetDitherThreshold(float x, float y);

// Get a pseudo-random value from blue noise (better distributed than rand)
float GetBlueNoiseRandom(float x, float y, int layer = 0);

// Get a 2D vector from blue noise (for jittering, offsets)
glm::vec2 GetBlueNoiseVector(float x, float y);

} // namespace Cortex::Utils
