#ifndef CORTEX_TERRAIN_NOISE_HLSLI
#define CORTEX_TERRAIN_NOISE_HLSLI

// Shared deterministic noise helpers for procedural terrain displacement.
// Keep these functions identical across shader stages so terrain height and
// shading remain stable and match the CPU sampler.

uint Hash32(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

uint Hash2D(int x, int z, uint seed)
{
    uint ux = (uint)x;
    uint uz = (uint)z;
    uint h = seed;
    h = Hash32(h ^ (ux + 0x9e3779b9U));
    h = Hash32(h ^ (uz + 0x7f4a7c15U));
    return h;
}

float Smoothstep01(float t)
{
    t = saturate(t);
    return t * t * (3.0f - 2.0f * t);
}

float HashToSignedUnit(uint h)
{
    // Map to [-1,1].
    float u = (float)h / 4294967295.0f;
    return u * 2.0f - 1.0f;
}

float ValueNoise2D(float x, float z, uint seed)
{
    int xi = (int)floor(x);
    int zi = (int)floor(z);
    float tx = x - (float)xi;
    float tz = z - (float)zi;

    float sx = Smoothstep01(tx);
    float sz = Smoothstep01(tz);

    float v00 = HashToSignedUnit(Hash2D(xi + 0, zi + 0, seed));
    float v10 = HashToSignedUnit(Hash2D(xi + 1, zi + 0, seed));
    float v01 = HashToSignedUnit(Hash2D(xi + 0, zi + 1, seed));
    float v11 = HashToSignedUnit(Hash2D(xi + 1, zi + 1, seed));

    float a = lerp(v00, v10, sx);
    float b = lerp(v01, v11, sx);
    return lerp(a, b, sz);
}

float FBM(float x, float z, uint seed, int octaves, float freq, float lac, float gain)
{
    float sum = 0.0f;
    float amp = 1.0f;
    float f = freq;
    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        if (i >= octaves) break;
        sum += amp * ValueNoise2D(x * f, z * f, seed + (uint)i * 1013u);
        f *= lac;
        amp *= gain;
    }
    return sum;
}

float DomainWarpedFBM(float x, float z, uint seed, int octaves, float freq, float lac, float gain, float warp)
{
    if (warp <= 0.0f)
    {
        return FBM(x, z, seed, octaves, freq, lac, gain);
    }

    float wx = FBM(x + 37.2f, z - 11.8f, seed, octaves, freq, lac, gain) * warp;
    float wz = FBM(x - 19.1f, z + 53.7f, seed, octaves, freq, lac, gain) * warp;
    return FBM(x + wx, z + wz, seed, octaves, freq, lac, gain);
}

float TerrainHeightParams(float worldX, float worldZ,
                          uint seed, int octaves,
                          float amplitude, float frequency, float lacunarity, float gain, float warp)
{
    float base = DomainWarpedFBM(worldX, worldZ, seed, octaves, frequency, lacunarity, gain, warp);

    float ridge = DomainWarpedFBM(worldX + 101.0f, worldZ - 73.0f,
                                  seed + 1337u, octaves, frequency * 0.55f, lacunarity, 0.6f, warp);
    ridge = 1.0f - abs(ridge);
    ridge = ridge * ridge;

    float h = base * 0.75f + ridge * 0.45f;
    return h * amplitude;
}

#endif // CORTEX_TERRAIN_NOISE_HLSLI
