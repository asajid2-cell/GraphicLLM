#pragma once

#include <cstdint>

namespace Cortex::Scene {

struct TerrainNoiseParams {
    uint32_t seed = 1337;
    float amplitude = 35.0f;
    float frequency = 0.0025f;
    uint32_t octaves = 5;
    float lacunarity = 2.0f;
    float gain = 0.5f;
    float warp = 0.0f;
};

// Deterministic CPU-side terrain height function used for traversal/collision.
// The renderer uses a shader-side equivalent for displacement.
float SampleTerrainHeight(double worldX, double worldZ, const TerrainNoiseParams& params);

} // namespace Cortex::Scene

