// BlueNoise.cpp
// Blue Noise Texture-Based Sampling Implementation
// Reference: "Blue Noise through Optimal Transport" - de Goes et al.

#include "BlueNoise.h"
#include <chrono>
#include <cmath>
#include <algorithm>
#include <random>
#include <fstream>
#include <queue>

namespace Cortex::Utils {

// ============================================================================
// BlueNoiseSampler Implementation
// ============================================================================

BlueNoiseSampler::BlueNoiseSampler() {
    // Initialize point counts per layer (exponential distribution)
    // Layer 0: densest, Layer N: sparsest
    for (int i = 0; i < BLUE_NOISE_LAYERS; ++i) {
        // Exponential decay: 4096, 2048, 1024, 512, 256, 128, 64, 32
        m_pointsPerLayer[i] = static_cast<uint32_t>(
            (BLUE_NOISE_SIZE * BLUE_NOISE_SIZE) >> i);
        m_pointsPerLayer[i] = std::max(m_pointsPerLayer[i], 16u);
    }
}

bool BlueNoiseSampler::Initialize() {
    if (m_initialized) {
        return true;
    }

    GeneratePattern(BLUE_NOISE_SIZE, BLUE_NOISE_LAYERS);
    return m_initialized;
}

bool BlueNoiseSampler::LoadTexture(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Read header
    int size, layers;
    file.read(reinterpret_cast<char*>(&size), sizeof(int));
    file.read(reinterpret_cast<char*>(&layers), sizeof(int));

    if (size != BLUE_NOISE_SIZE || layers > BLUE_NOISE_LAYERS) {
        return false;
    }

    // Read texture data
    for (int l = 0; l < layers; ++l) {
        m_textures[l].resize(size * size);
        file.read(reinterpret_cast<char*>(m_textures[l].data()),
                  size * size * sizeof(float));

        // Extract points for this layer
        ExtractPoints(l, m_pointsPerLayer[l]);
    }

    m_initialized = true;
    return true;
}

void BlueNoiseSampler::GeneratePattern(int size, int layers) {
    // Generate multiple layers with varying densities
    for (int l = 0; l < layers; ++l) {
        int numPoints = m_pointsPerLayer[l];
        m_textures[l] = GenerateVoidAndCluster(size, numPoints);
        ExtractPoints(l, numPoints);
    }

    m_initialized = true;
}

std::vector<float> BlueNoiseSampler::GenerateVoidAndCluster(int size, int numPoints) {
    // Void-and-cluster algorithm for blue noise generation
    // This is a simplified version that still produces good results

    std::vector<float> texture(size * size, 0.0f);
    std::vector<bool> occupied(size * size, false);
    std::vector<float> energy(size * size, 0.0f);

    std::mt19937 rng(42);  // Fixed seed for reproducibility
    std::uniform_int_distribution<int> posDist(0, size - 1);

    // Gaussian kernel parameters
    float sigma = static_cast<float>(size) / std::sqrt(static_cast<float>(numPoints));
    float sigmaSq2 = 2.0f * sigma * sigma;
    int kernelRadius = static_cast<int>(std::ceil(3.0f * sigma));

    auto addEnergy = [&](int x, int y, float sign) {
        for (int dy = -kernelRadius; dy <= kernelRadius; ++dy) {
            for (int dx = -kernelRadius; dx <= kernelRadius; ++dx) {
                int nx = (x + dx + size) % size;  // Wrap for tiling
                int ny = (y + dy + size) % size;
                float distSq = static_cast<float>(dx * dx + dy * dy);
                float e = std::exp(-distSq / sigmaSq2);
                energy[ny * size + nx] += sign * e;
            }
        }
    };

    // Place initial random points
    int placed = 0;
    while (placed < numPoints / 4) {
        int x = posDist(rng);
        int y = posDist(rng);
        int idx = y * size + x;

        if (!occupied[idx]) {
            occupied[idx] = true;
            texture[idx] = static_cast<float>(placed) / static_cast<float>(numPoints);
            addEnergy(x, y, 1.0f);
            placed++;
        }
    }

    // Iteratively place remaining points at lowest energy locations
    while (placed < numPoints) {
        // Find minimum energy unoccupied location
        float minEnergy = std::numeric_limits<float>::max();
        int minIdx = -1;

        for (int i = 0; i < size * size; ++i) {
            if (!occupied[i] && energy[i] < minEnergy) {
                minEnergy = energy[i];
                minIdx = i;
            }
        }

        if (minIdx == -1) break;

        int x = minIdx % size;
        int y = minIdx / size;

        occupied[minIdx] = true;
        texture[minIdx] = static_cast<float>(placed) / static_cast<float>(numPoints);
        addEnergy(x, y, 1.0f);
        placed++;
    }

    // Normalize and invert so lower values = earlier in progressive sequence
    float maxVal = 0.0f;
    for (float v : texture) {
        maxVal = std::max(maxVal, v);
    }

    if (maxVal > 0.0f) {
        for (float& v : texture) {
            v /= maxVal;
        }
    }

    return texture;
}

void BlueNoiseSampler::ExtractPoints(int layer, int numPoints) {
    if (layer >= BLUE_NOISE_LAYERS) return;

    const auto& texture = m_textures[layer];
    int size = BLUE_NOISE_SIZE;

    // Collect points with their ranks
    struct RankedPoint {
        glm::vec2 position;
        float rank;
    };
    std::vector<RankedPoint> ranked;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float value = texture[y * size + x];
            if (value > 0.0f) {
                ranked.push_back({
                    glm::vec2(
                        (static_cast<float>(x) + 0.5f) / static_cast<float>(size),
                        (static_cast<float>(y) + 0.5f) / static_cast<float>(size)
                    ),
                    value
                });
            }
        }
    }

    // Sort by rank (ascending)
    std::sort(ranked.begin(), ranked.end(),
              [](const RankedPoint& a, const RankedPoint& b) {
                  return a.rank < b.rank;
              });

    // Take first numPoints
    m_tilePoints[layer].clear();
    int count = std::min(static_cast<int>(ranked.size()), numPoints);
    for (int i = 0; i < count; ++i) {
        m_tilePoints[layer].push_back(ranked[i].position);
    }
}

std::vector<glm::vec2> BlueNoiseSampler::Sample(const BlueNoiseParams& params) {
    auto startTime = std::chrono::high_resolution_clock::now();

    if (!m_initialized) {
        Initialize();
    }

    std::vector<glm::vec2> result;
    m_stats = BlueNoiseStats();

    // Determine which layer to use based on density
    uint32_t layer = GetLayerForDensity(params.density);
    const auto& tilePoints = m_tilePoints[layer];

    if (tilePoints.empty()) {
        return result;
    }

    // Calculate tile grid
    int startTileX = static_cast<int>(std::floor(params.minX / params.tileSize));
    int startTileY = static_cast<int>(std::floor(params.minY / params.tileSize));
    int endTileX = static_cast<int>(std::ceil(params.maxX / params.tileSize));
    int endTileY = static_cast<int>(std::ceil(params.maxY / params.tileSize));

    // Collect points from all tiles
    for (int ty = startTileY; ty <= endTileY; ++ty) {
        for (int tx = startTileX; tx <= endTileX; ++tx) {
            glm::vec2 offset = TileOffset(tx, ty, params.seed);
            float tileMinX = static_cast<float>(tx) * params.tileSize;
            float tileMinY = static_cast<float>(ty) * params.tileSize;

            int pointCount = static_cast<int>(tilePoints.size());
            if (params.progressive) {
                pointCount = static_cast<int>(
                    static_cast<float>(pointCount) * params.progressiveRatio);
                pointCount = std::max(1, pointCount);
            }

            for (int i = 0; i < pointCount; ++i) {
                glm::vec2 localPos = tilePoints[i] + offset;
                localPos = glm::fract(localPos);  // Wrap to [0,1]

                glm::vec2 worldPos(
                    tileMinX + localPos.x * params.tileSize,
                    tileMinY + localPos.y * params.tileSize
                );

                // Bounds check
                if (worldPos.x >= params.minX && worldPos.x <= params.maxX &&
                    worldPos.y >= params.minY && worldPos.y <= params.maxY) {
                    result.push_back(worldPos);
                }
            }

            m_stats.tilesUsed++;
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    m_stats.totalPoints = static_cast<uint32_t>(result.size());
    m_stats.pointsPerTile = static_cast<float>(tilePoints.size());
    m_stats.executionTimeMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();

    return result;
}

std::vector<BlueNoiseSample> BlueNoiseSampler::SampleExtended(const BlueNoiseParams& params) {
    auto points = Sample(params);

    std::vector<BlueNoiseSample> result;
    result.reserve(points.size());

    uint32_t layer = GetLayerForDensity(params.density);

    for (const auto& pos : points) {
        BlueNoiseSample sample;
        sample.position = pos;
        sample.layer = layer;

        // Compute rank from blue noise texture
        int tx = static_cast<int>(pos.x / params.tileSize) % BLUE_NOISE_SIZE;
        int ty = static_cast<int>(pos.y / params.tileSize) % BLUE_NOISE_SIZE;
        if (tx < 0) tx += BLUE_NOISE_SIZE;
        if (ty < 0) ty += BLUE_NOISE_SIZE;
        sample.rank = GetBlueNoiseValue(tx, ty, layer);

        result.push_back(sample);
    }

    return result;
}

std::vector<glm::vec2> BlueNoiseSampler::GetTilePoints(uint32_t layer) const {
    if (layer >= BLUE_NOISE_LAYERS) {
        return {};
    }
    return m_tilePoints[layer];
}

float BlueNoiseSampler::GetBlueNoiseValue(int x, int y, int layer) const {
    if (layer >= BLUE_NOISE_LAYERS || m_textures[layer].empty()) {
        return 0.5f;
    }

    x = ((x % BLUE_NOISE_SIZE) + BLUE_NOISE_SIZE) % BLUE_NOISE_SIZE;
    y = ((y % BLUE_NOISE_SIZE) + BLUE_NOISE_SIZE) % BLUE_NOISE_SIZE;

    return m_textures[layer][y * BLUE_NOISE_SIZE + x];
}

uint32_t BlueNoiseSampler::GetLayerForDensity(float density) const {
    // Map density to layer index
    // Higher density = lower layer index (more points)
    if (density >= 1.0f) return 0;
    if (density <= 0.01f) return BLUE_NOISE_LAYERS - 1;

    // Logarithmic mapping
    float logDensity = -std::log2(density);
    uint32_t layer = static_cast<uint32_t>(logDensity);
    return std::min(layer, static_cast<uint32_t>(BLUE_NOISE_LAYERS - 1));
}

glm::vec2 BlueNoiseSampler::Jitter(const glm::vec2& position, float amount) const {
    int x = static_cast<int>(position.x * 10.0f);
    int y = static_cast<int>(position.y * 10.0f);

    float jx = GetBlueNoiseValue(x, y, 0) * 2.0f - 1.0f;
    float jy = GetBlueNoiseValue(x + 37, y + 97, 1) * 2.0f - 1.0f;

    return position + glm::vec2(jx, jy) * amount;
}

float BlueNoiseSampler::GetRotation(float x, float y) const {
    int ix = static_cast<int>(x * 5.0f);
    int iy = static_cast<int>(y * 5.0f);

    return GetBlueNoiseValue(ix, iy, 2) * 2.0f * 3.14159265358979f;
}

float BlueNoiseSampler::GetScaleVariation(float x, float y, float minScale, float maxScale) const {
    int ix = static_cast<int>(x * 7.0f);
    int iy = static_cast<int>(y * 7.0f);

    float t = GetBlueNoiseValue(ix + 53, iy + 29, 3);
    return minScale + t * (maxScale - minScale);
}

glm::vec2 BlueNoiseSampler::TileOffset(int tileX, int tileY, uint32_t seed) const {
    // Hash-based offset for each tile (prevents visible patterns)
    uint32_t h = seed;
    h ^= static_cast<uint32_t>(tileX) * 374761393u;
    h ^= static_cast<uint32_t>(tileY) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;

    float ox = static_cast<float>(h & 0xFFFF) / 65535.0f;
    float oy = static_cast<float>((h >> 16) & 0xFFFF) / 65535.0f;

    return glm::vec2(ox, oy);
}

glm::vec2 BlueNoiseSampler::HaltonSequence(int index, int baseX, int baseY) const {
    // Halton sequence for low-discrepancy fallback
    auto halton = [](int index, int base) {
        float result = 0.0f;
        float f = 1.0f / static_cast<float>(base);
        int i = index;
        while (i > 0) {
            result += f * static_cast<float>(i % base);
            i /= base;
            f /= static_cast<float>(base);
        }
        return result;
    };

    return glm::vec2(halton(index, baseX), halton(index, baseY));
}

// ============================================================================
// Global Instance
// ============================================================================

BlueNoiseSampler& GetBlueNoiseSampler() {
    static BlueNoiseSampler instance;
    if (!instance.GetStats().totalPoints && !instance.GetTilePoints(0).empty()) {
        // Already initialized
    } else {
        instance.Initialize();
    }
    return instance;
}

// ============================================================================
// Convenience Functions
// ============================================================================

std::vector<glm::vec2> SampleBlueNoise(float minX, float minY, float maxX, float maxY,
                                        float density, float tileSize, uint32_t seed) {
    BlueNoiseParams params;
    params.minX = minX;
    params.minY = minY;
    params.maxX = maxX;
    params.maxY = maxY;
    params.density = density;
    params.tileSize = tileSize;
    params.seed = seed;

    return GetBlueNoiseSampler().Sample(params);
}

float GetDitherThreshold(float x, float y) {
    int ix = static_cast<int>(x) % BLUE_NOISE_SIZE;
    int iy = static_cast<int>(y) % BLUE_NOISE_SIZE;
    if (ix < 0) ix += BLUE_NOISE_SIZE;
    if (iy < 0) iy += BLUE_NOISE_SIZE;

    return GetBlueNoiseSampler().GetBlueNoiseValue(ix, iy, 0);
}

float GetBlueNoiseRandom(float x, float y, int layer) {
    int ix = static_cast<int>(x * 10.0f) % BLUE_NOISE_SIZE;
    int iy = static_cast<int>(y * 10.0f) % BLUE_NOISE_SIZE;
    if (ix < 0) ix += BLUE_NOISE_SIZE;
    if (iy < 0) iy += BLUE_NOISE_SIZE;

    return GetBlueNoiseSampler().GetBlueNoiseValue(ix, iy, layer);
}

glm::vec2 GetBlueNoiseVector(float x, float y) {
    float vx = GetBlueNoiseRandom(x, y, 0) * 2.0f - 1.0f;
    float vy = GetBlueNoiseRandom(x + 0.5f, y + 0.5f, 1) * 2.0f - 1.0f;
    return glm::vec2(vx, vy);
}

} // namespace Cortex::Utils
