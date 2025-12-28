#pragma once

#include <cstdint>

namespace Cortex::Scene {

// Parameters for procedural terrain noise generation.
// These are shared between CPU (collision/physics) and GPU (vertex displacement).
struct TerrainNoiseParams {
    uint32_t seed = 1337;
    float amplitude = 35.0f;
    float frequency = 0.0025f;
    uint32_t octaves = 5;
    float lacunarity = 2.0f;
    float gain = 0.5f;
    float warp = 0.0f;
};

// CPU-side terrain height sampling for collision and physics.
// Must match the GPU implementation in TerrainNoise.hlsli exactly.
float SampleTerrainHeight(double worldX, double worldZ, const TerrainNoiseParams& params);

} // namespace Cortex::Scene
