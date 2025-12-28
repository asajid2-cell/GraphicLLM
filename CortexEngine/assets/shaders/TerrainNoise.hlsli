// TerrainNoise.hlsli
// GPU-side terrain noise functions matching CPU implementation in TerrainNoise.cpp.
// These must stay in sync to ensure consistent height values for physics/rendering.

#ifndef CORTEX_TERRAIN_NOISE_HLSLI
#define CORTEX_TERRAIN_NOISE_HLSLI

// Terrain noise parameters (matches Scene::TerrainNoiseParams on CPU)
struct TerrainNoiseParams
{
    uint seed;
    float amplitude;
    float frequency;
    uint octaves;
    float lacunarity;
    float gain;
    float warp;
    float _padding;
};

// Hash function matching CPU implementation
uint Hash32(uint x)
{
    x ^= x >> 16;
    x *= 0x85ebca6bu;
    x ^= x >> 13;
    x *= 0xc2b2ae35u;
    x ^= x >> 16;
    return x;
}

// Gradient noise matching CPU implementation
float GradientNoise2D(float2 pos, uint seed)
{
    int2 ipos = int2(floor(pos));
    float2 fpos = frac(pos);

    // Quintic interpolation for smoother results
    float2 u = fpos * fpos * fpos * (fpos * (fpos * 6.0f - 15.0f) + 10.0f);

    float v00 = (float)Hash32((uint)ipos.x + Hash32((uint)ipos.y + seed)) / (float)0xFFFFFFFFu * 2.0f - 1.0f;
    float v10 = (float)Hash32((uint)(ipos.x + 1) + Hash32((uint)ipos.y + seed)) / (float)0xFFFFFFFFu * 2.0f - 1.0f;
    float v01 = (float)Hash32((uint)ipos.x + Hash32((uint)(ipos.y + 1) + seed)) / (float)0xFFFFFFFFu * 2.0f - 1.0f;
    float v11 = (float)Hash32((uint)(ipos.x + 1) + Hash32((uint)(ipos.y + 1) + seed)) / (float)0xFFFFFFFFu * 2.0f - 1.0f;

    float v0 = lerp(v00, v10, u.x);
    float v1 = lerp(v01, v11, u.x);
    return lerp(v0, v1, u.y);
}

// FBM (Fractal Brownian Motion) noise
float FBM(float2 pos, uint seed, int octaves, float lacunarity, float gain)
{
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; ++i)
    {
        value += amplitude * GradientNoise2D(pos * frequency, seed + (uint)i);
        maxValue += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }

    return value / maxValue;
}

// Domain-warped FBM for more interesting terrain shapes
float DomainWarpedFBM(float2 pos, uint seed, int octaves, float lacunarity, float gain, float warpStrength)
{
    if (warpStrength > 0.001f)
    {
        float warpX = FBM(pos + float2(100.0f, 100.0f), seed + 1000, octaves / 2 + 1, lacunarity, gain);
        float warpZ = FBM(pos + float2(200.0f, 200.0f), seed + 2000, octaves / 2 + 1, lacunarity, gain);
        pos.x += warpX * warpStrength;
        pos.y += warpZ * warpStrength;
    }
    return FBM(pos, seed, octaves, lacunarity, gain);
}

// Sample terrain height at world position (matching CPU SampleTerrainHeight)
float SampleTerrainHeight(float2 worldXZ, TerrainNoiseParams params)
{
    float2 scaledPos = worldXZ * params.frequency;

    float noise = DomainWarpedFBM(
        scaledPos,
        params.seed,
        (int)params.octaves,
        params.lacunarity,
        params.gain,
        params.warp
    );

    // Map from [-1, 1] to [0, 1] then scale by amplitude
    return (noise * 0.5f + 0.5f) * params.amplitude;
}

// Compute terrain normal from height samples (finite differences)
float3 ComputeTerrainNormal(float2 worldXZ, TerrainNoiseParams params, float epsilon)
{
    float h0 = SampleTerrainHeight(worldXZ, params);
    float hx = SampleTerrainHeight(worldXZ + float2(epsilon, 0.0f), params);
    float hz = SampleTerrainHeight(worldXZ + float2(0.0f, epsilon), params);

    float3 dx = float3(epsilon, hx - h0, 0.0f);
    float3 dz = float3(0.0f, hz - h0, epsilon);

    return normalize(cross(dz, dx));
}

#endif // CORTEX_TERRAIN_NOISE_HLSLI
