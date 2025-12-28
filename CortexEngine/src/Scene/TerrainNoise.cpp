#include "TerrainNoise.h"
#include <cmath>

namespace Cortex::Scene {

// Hash function matching GPU implementation.
static uint32_t Hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x85ebca6bu;
    x ^= x >> 13;
    x *= 0xc2b2ae35u;
    x ^= x >> 16;
    return x;
}

// Gradient noise implementation matching GPU.
static float GradientNoise2D(double x, double z, uint32_t seed) {
    int32_t ix = static_cast<int32_t>(std::floor(x));
    int32_t iz = static_cast<int32_t>(std::floor(z));
    double fx = x - std::floor(x);
    double fz = z - std::floor(z);

    // Quintic interpolation for smoother results.
    double ux = fx * fx * fx * (fx * (fx * 6.0 - 15.0) + 10.0);
    double uz = fz * fz * fz * (fz * (fz * 6.0 - 15.0) + 10.0);

    auto hash = [&](int32_t cx, int32_t cz) -> double {
        uint32_t h = Hash32(static_cast<uint32_t>(cx) + Hash32(static_cast<uint32_t>(cz) + seed));
        return static_cast<double>(h) / static_cast<double>(0xFFFFFFFFu) * 2.0 - 1.0;
    };

    double v00 = hash(ix, iz);
    double v10 = hash(ix + 1, iz);
    double v01 = hash(ix, iz + 1);
    double v11 = hash(ix + 1, iz + 1);

    double v0 = v00 + ux * (v10 - v00);
    double v1 = v01 + ux * (v11 - v01);
    return static_cast<float>(v0 + uz * (v1 - v0));
}

// FBM (Fractal Brownian Motion) noise.
static float FBM(double x, double z, uint32_t seed, int octaves, float lacunarity, float gain) {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        value += amplitude * GradientNoise2D(x * frequency, z * frequency, seed + i);
        maxValue += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }

    return value / maxValue;
}

// Domain-warped FBM for more interesting terrain shapes.
static float DomainWarpedFBM(double x, double z, uint32_t seed, int octaves,
                              float lacunarity, float gain, float warpStrength) {
    if (warpStrength > 0.001f) {
        float warpX = FBM(x + 100.0, z + 100.0, seed + 1000, octaves / 2 + 1, lacunarity, gain);
        float warpZ = FBM(x + 200.0, z + 200.0, seed + 2000, octaves / 2 + 1, lacunarity, gain);
        x += warpX * warpStrength;
        z += warpZ * warpStrength;
    }
    return FBM(x, z, seed, octaves, lacunarity, gain);
}

float SampleTerrainHeight(double worldX, double worldZ, const TerrainNoiseParams& params) {
    double scaledX = worldX * static_cast<double>(params.frequency);
    double scaledZ = worldZ * static_cast<double>(params.frequency);

    float noise = DomainWarpedFBM(
        scaledX, scaledZ,
        params.seed,
        static_cast<int>(params.octaves),
        params.lacunarity,
        params.gain,
        params.warp
    );

    // Map from [-1, 1] to [0, 1] then scale by amplitude.
    return (noise * 0.5f + 0.5f) * params.amplitude;
}

} // namespace Cortex::Scene
