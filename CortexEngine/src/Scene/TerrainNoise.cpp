#include "TerrainNoise.h"

#include <cmath>

namespace Cortex::Scene {

namespace {

static inline uint32_t Hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline uint32_t Hash2D(int32_t x, int32_t z, uint32_t seed) {
    // Stable hash for large coordinates; mix signed coords into uint.
    uint32_t ux = static_cast<uint32_t>(x);
    uint32_t uz = static_cast<uint32_t>(z);
    uint32_t h = seed;
    h = Hash32(h ^ (ux + 0x9e3779b9U));
    h = Hash32(h ^ (uz + 0x7f4a7c15U));
    return h;
}

static inline float Smoothstep(float t) {
    // Classic smoothstep for value noise interpolation.
    t = (t < 0.0f) ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

static inline float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static inline float HashToSignedUnitFloat(uint32_t h) {
    // Map to [-1, 1] deterministically.
    constexpr float inv = 1.0f / 4294967295.0f; // 1/(2^32-1)
    float u = static_cast<float>(h) * inv;
    return u * 2.0f - 1.0f;
}

static float ValueNoise2D(float x, float z, uint32_t seed) {
    const int32_t xi = static_cast<int32_t>(std::floor(x));
    const int32_t zi = static_cast<int32_t>(std::floor(z));
    const float tx = x - static_cast<float>(xi);
    const float tz = z - static_cast<float>(zi);

    const float sx = Smoothstep(tx);
    const float sz = Smoothstep(tz);

    const float v00 = HashToSignedUnitFloat(Hash2D(xi + 0, zi + 0, seed));
    const float v10 = HashToSignedUnitFloat(Hash2D(xi + 1, zi + 0, seed));
    const float v01 = HashToSignedUnitFloat(Hash2D(xi + 0, zi + 1, seed));
    const float v11 = HashToSignedUnitFloat(Hash2D(xi + 1, zi + 1, seed));

    const float a = Lerp(v00, v10, sx);
    const float b = Lerp(v01, v11, sx);
    return Lerp(a, b, sz);
}

static float FBM(float x, float z, const TerrainNoiseParams& p) {
    float sum = 0.0f;
    float amp = 1.0f;
    float freq = p.frequency;
    for (uint32_t i = 0; i < p.octaves; ++i) {
        sum += amp * ValueNoise2D(x * freq, z * freq, p.seed + i * 1013u);
        freq *= p.lacunarity;
        amp *= p.gain;
    }
    return sum;
}

static float DomainWarpedFBM(float x, float z, const TerrainNoiseParams& p) {
    if (p.warp <= 0.0f) {
        return FBM(x, z, p);
    }

    float wx = FBM(x + 37.2f, z - 11.8f, p) * p.warp;
    float wz = FBM(x - 19.1f, z + 53.7f, p) * p.warp;
    return FBM(x + wx, z + wz, p);
}

} // namespace

float SampleTerrainHeight(double worldX, double worldZ, const TerrainNoiseParams& params) {
    // Convert to float for consistent hash lattice behavior.
    const float x = static_cast<float>(worldX);
    const float z = static_cast<float>(worldZ);

    // A blended profile: base fBm hills + a ridged component for sharper peaks.
    float base = DomainWarpedFBM(x, z, params);

    TerrainNoiseParams ridgedP = params;
    ridgedP.frequency *= 0.55f;
    ridgedP.gain = 0.6f;
    float ridge = DomainWarpedFBM(x + 101.0f, z - 73.0f, ridgedP);
    ridge = 1.0f - std::abs(ridge);
    ridge = ridge * ridge;

    float h = base * 0.75f + ridge * 0.45f;
    return h * params.amplitude;
}

} // namespace Cortex::Scene

