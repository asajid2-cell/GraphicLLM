// VegetationSpawner.cpp
// Implementation of procedural vegetation spawning.
// Uses academic sampling algorithms: Bridson's Poisson Disk, Blue Noise, Lloyd Relaxation

#include "VegetationSpawner.h"
#include "../Scene/BiomeMap.h"
#include "../Utils/PoissonDisk.h"
#include "../Utils/BlueNoise.h"
#include "../Utils/LloydRelaxation.h"
#include <cmath>
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

namespace Cortex::Editor {

VegetationSpawner::VegetationSpawner() {
}

VegetationSpawner::~VegetationSpawner() {
}

void VegetationSpawner::Initialize(const BiomeMap* biomeMap, TerrainQueryFunc terrainQuery) {
    m_biomeMap = biomeMap;
    m_terrainQuery = std::move(terrainQuery);
}

void VegetationSpawner::SetParams(const VegetationSpawnParams& params) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_params = params;
}

void VegetationSpawner::AddPrototype(const VegetationPrototype& prototype) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_prototypes.push_back(prototype);
}

void VegetationSpawner::ClearPrototypes() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_prototypes.clear();
}

void VegetationSpawner::SetBiomeDensity(BiomeType biome, const BiomeVegetationDensity& density) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_biomeDensities[biome] = density;
}

const BiomeVegetationDensity* VegetationSpawner::GetBiomeDensity(BiomeType biome) const {
    auto it = m_biomeDensities.find(biome);
    if (it != m_biomeDensities.end()) {
        return &it->second;
    }
    return nullptr;
}

// ============================================================================
// RANDOM NUMBER GENERATION
// ============================================================================

void VegetationSpawner::SeedRNG(int32_t chunkX, int32_t chunkZ) {
    // Combine chunk coords with global seed for deterministic per-chunk RNG
    m_rngState = m_params.seed ^ (static_cast<uint32_t>(chunkX) * 73856093u) ^
                 (static_cast<uint32_t>(chunkZ) * 19349663u);
}

float VegetationSpawner::RandomFloat() {
    // Xorshift RNG
    m_rngState ^= m_rngState << 13;
    m_rngState ^= m_rngState >> 17;
    m_rngState ^= m_rngState << 5;
    return static_cast<float>(m_rngState) / static_cast<float>(0xFFFFFFFF);
}

float VegetationSpawner::RandomFloat(float min, float max) {
    return min + RandomFloat() * (max - min);
}

int VegetationSpawner::RandomInt(int min, int max) {
    return min + static_cast<int>(RandomFloat() * (max - min + 1));
}

// ============================================================================
// POISSON DISK SAMPLING
// ============================================================================

std::vector<glm::vec2> VegetationSpawner::PoissonDiskSample(
    float minX, float minZ, float maxX, float maxZ,
    float minDistance, int maxAttempts)
{
    std::vector<glm::vec2> points;
    std::vector<glm::vec2> activeList;

    float cellSize = minDistance / std::sqrt(2.0f);
    int gridWidth = static_cast<int>(std::ceil((maxX - minX) / cellSize));
    int gridHeight = static_cast<int>(std::ceil((maxZ - minZ) / cellSize));

    PoissonGrid grid;
    grid.cells.resize(gridWidth * gridHeight, -1);
    grid.gridWidth = gridWidth;
    grid.gridHeight = gridHeight;
    grid.cellSize = cellSize;

    // Start with a random point
    glm::vec2 firstPoint(RandomFloat(minX, maxX), RandomFloat(minZ, maxZ));
    points.push_back(firstPoint);
    activeList.push_back(firstPoint);

    int cellX = static_cast<int>((firstPoint.x - minX) / cellSize);
    int cellZ = static_cast<int>((firstPoint.y - minZ) / cellSize);
    if (cellX >= 0 && cellX < gridWidth && cellZ >= 0 && cellZ < gridHeight) {
        grid.cells[cellZ * gridWidth + cellX] = 0;
    }

    while (!activeList.empty()) {
        // Pick a random active point
        int activeIdx = RandomInt(0, static_cast<int>(activeList.size()) - 1);
        glm::vec2 center = activeList[activeIdx];
        bool foundValid = false;

        for (int attempt = 0; attempt < maxAttempts; ++attempt) {
            // Generate random point in annulus around center
            float angle = RandomFloat(0.0f, 6.28318f);
            float radius = RandomFloat(minDistance, 2.0f * minDistance);

            glm::vec2 candidate(
                center.x + radius * std::cos(angle),
                center.y + radius * std::sin(angle)
            );

            // Check bounds
            if (candidate.x < minX || candidate.x >= maxX ||
                candidate.y < minZ || candidate.y >= maxZ) {
                continue;
            }

            // Check distance to existing points
            if (IsValidPoissonPoint(candidate, minDistance, grid, points, minX, minZ)) {
                points.push_back(candidate);
                activeList.push_back(candidate);

                int cx = static_cast<int>((candidate.x - minX) / cellSize);
                int cz = static_cast<int>((candidate.y - minZ) / cellSize);
                if (cx >= 0 && cx < gridWidth && cz >= 0 && cz < gridHeight) {
                    grid.cells[cz * gridWidth + cx] = static_cast<int>(points.size()) - 1;
                }

                foundValid = true;
                break;
            }
        }

        if (!foundValid) {
            // Remove from active list
            activeList.erase(activeList.begin() + activeIdx);
        }
    }

    return points;
}

bool VegetationSpawner::IsValidPoissonPoint(
    const glm::vec2& point, float minDistance,
    const PoissonGrid& grid, const std::vector<glm::vec2>& points,
    float minX, float minZ)
{
    int cellX = static_cast<int>((point.x - minX) / grid.cellSize);
    int cellZ = static_cast<int>((point.y - minZ) / grid.cellSize);

    // Check neighboring cells
    int searchRadius = 2;
    for (int dz = -searchRadius; dz <= searchRadius; ++dz) {
        for (int dx = -searchRadius; dx <= searchRadius; ++dx) {
            int nx = cellX + dx;
            int nz = cellZ + dz;

            if (nx < 0 || nx >= grid.gridWidth || nz < 0 || nz >= grid.gridHeight) {
                continue;
            }

            int pointIdx = grid.cells[nz * grid.gridWidth + nx];
            if (pointIdx >= 0) {
                float dist = glm::length(point - points[pointIdx]);
                if (dist < minDistance) {
                    return false;
                }
            }
        }
    }

    return true;
}

// ============================================================================
// ACADEMIC SAMPLING METHODS
// Reference: Bridson's Poisson Disk, Blue Noise Textures, Lloyd Relaxation
// ============================================================================

std::vector<glm::vec2> VegetationSpawner::SamplePoints(
    SamplingMethod method,
    float minX, float minZ, float maxX, float maxZ,
    float minDistance, VegetationType type)
{
    switch (method) {
        case SamplingMethod::Random:
            return SampleRandom(minX, minZ, maxX, maxZ,
                               1.0f / (minDistance * minDistance));

        case SamplingMethod::PoissonDisk:
            return SampleBridsonPoisson(minX, minZ, maxX, maxZ, minDistance, type);

        case SamplingMethod::BlueNoise:
            return SampleBlueNoise(minX, minZ, maxX, maxZ,
                                   1.0f / (minDistance * minDistance));

        case SamplingMethod::PoissonRelaxed:
            return SamplePoissonRelaxed(minX, minZ, maxX, maxZ, minDistance, type);

        case SamplingMethod::Stratified:
            return SampleStratified(minX, minZ, maxX, maxZ, minDistance);

        default:
            // Fallback to legacy implementation
            return PoissonDiskSample(minX, minZ, maxX, maxZ, minDistance, m_params.poissonMaxAttempts);
    }
}

std::vector<glm::vec2> VegetationSpawner::SampleRandom(
    float minX, float minZ, float maxX, float maxZ,
    float density)
{
    std::vector<glm::vec2> points;
    float area = (maxX - minX) * (maxZ - minZ);
    int count = static_cast<int>(area * density);

    points.reserve(count);
    for (int i = 0; i < count; ++i) {
        points.emplace_back(RandomFloat(minX, maxX), RandomFloat(minZ, maxZ));
    }

    return points;
}

std::vector<glm::vec2> VegetationSpawner::SampleBridsonPoisson(
    float minX, float minZ, float maxX, float maxZ,
    float minDistance, VegetationType type)
{
    using namespace Utils;

    PoissonDiskParams params;
    params.minX = minX;
    params.minY = minZ;
    params.maxX = maxX;
    params.maxY = maxZ;
    params.minDistance = minDistance;
    params.maxAttempts = m_params.poissonMaxAttempts;
    params.seed = m_rngState;

    // Enable variable density based on biome
    if (m_params.useVariableDensity && m_biomeMap) {
        params.variableDensity = true;
        params.densityFunc = [this, type](float x, float y) -> float {
            return GetDensityAtPosition(type, x, y);
        };
    }

    // Add terrain rejection function
    if (m_terrainQuery) {
        params.rejectFunc = [this, type](float x, float y) -> bool {
            return !IsValidTerrainPosition(x, y, type);
        };
    }

    return m_poissonSampler.Sample(params);
}

std::vector<glm::vec2> VegetationSpawner::SampleBlueNoise(
    float minX, float minZ, float maxX, float maxZ,
    float density)
{
    using namespace Utils;

    BlueNoiseParams params;
    params.minX = minX;
    params.minY = minZ;
    params.maxX = maxX;
    params.maxY = maxZ;
    params.density = density;
    params.tileSize = m_params.blueNoiseTileSize;
    params.seed = m_rngState;

    auto& sampler = GetBlueNoiseSampler();
    return sampler.Sample(params);
}

std::vector<glm::vec2> VegetationSpawner::SamplePoissonRelaxed(
    float minX, float minZ, float maxX, float maxZ,
    float minDistance, VegetationType type)
{
    using namespace Utils;

    // First generate Poisson disk samples
    auto points = SampleBridsonPoisson(minX, minZ, maxX, maxZ, minDistance, type);

    if (points.size() < 3) {
        return points;  // Not enough points for relaxation
    }

    // Apply Lloyd relaxation for more uniform distribution
    LloydParams lloydParams;
    lloydParams.minX = minX;
    lloydParams.minY = minZ;
    lloydParams.maxX = maxX;
    lloydParams.maxY = maxZ;
    lloydParams.maxIterations = m_params.lloydRelaxIterations;
    lloydParams.convergenceThreshold = 0.01f;
    lloydParams.dampingFactor = 0.8f;  // Gentle movement to preserve Poisson properties

    // Add density weighting if enabled
    if (m_params.useVariableDensity && m_biomeMap) {
        lloydParams.densityFunc = [this, type](float x, float y) -> float {
            return GetDensityAtPosition(type, x, y);
        };
    }

    return m_lloydRelaxation.Relax(points, lloydParams);
}

std::vector<glm::vec2> VegetationSpawner::SampleStratified(
    float minX, float minZ, float maxX, float maxZ,
    float spacing)
{
    std::vector<glm::vec2> points;

    float width = maxX - minX;
    float height = maxZ - minZ;

    int cellsX = std::max(1, static_cast<int>(width / spacing));
    int cellsZ = std::max(1, static_cast<int>(height / spacing));

    float cellWidth = width / static_cast<float>(cellsX);
    float cellHeight = height / static_cast<float>(cellsZ);

    points.reserve(cellsX * cellsZ);

    for (int cz = 0; cz < cellsZ; ++cz) {
        for (int cx = 0; cx < cellsX; ++cx) {
            // Jittered sample within cell
            float jitterX = RandomFloat(0.0f, cellWidth);
            float jitterZ = RandomFloat(0.0f, cellHeight);

            float x = minX + cx * cellWidth + jitterX;
            float z = minZ + cz * cellHeight + jitterZ;

            points.emplace_back(x, z);
        }
    }

    return points;
}

bool VegetationSpawner::IsValidTerrainPosition(float x, float z, VegetationType type) const {
    if (!m_terrainQuery) return true;

    float height;
    glm::vec3 normal;
    if (!m_terrainQuery(x, z, height, normal)) {
        return false;
    }

    // Get slope (0 = flat, 1 = vertical)
    float slope = 1.0f - std::abs(normal.y);

    // Type-specific constraints
    switch (type) {
        case VegetationType::Tree:
            // Trees prefer gentler slopes
            return slope < 0.4f;

        case VegetationType::Bush:
            return slope < 0.6f;

        case VegetationType::Grass:
        case VegetationType::Flower:
            return slope < 0.7f;

        case VegetationType::Rock:
            // Rocks can appear on steeper slopes
            return slope < 0.9f;

        default:
            return true;
    }
}

// ============================================================================
// SPAWNING
// ============================================================================

VegetationChunk VegetationSpawner::SpawnChunk(int32_t chunkX, int32_t chunkZ, float chunkSize, int resolution) {
    VegetationChunk chunk;
    chunk.chunkX = chunkX;
    chunk.chunkZ = chunkZ;

    float minX = chunkX * chunkSize;
    float minZ = chunkZ * chunkSize;
    float maxX = minX + chunkSize;
    float maxZ = minZ + chunkSize;

    chunk.instances = SpawnRegion(minX, minZ, maxX, maxZ);

    // Calculate bounds
    chunk.boundsMin = glm::vec3(minX, 1e10f, minZ);
    chunk.boundsMax = glm::vec3(maxX, -1e10f, maxZ);

    for (const auto& instance : chunk.instances) {
        chunk.boundsMin.y = std::min(chunk.boundsMin.y, instance.position.y);
        chunk.boundsMax.y = std::max(chunk.boundsMax.y, instance.position.y +
            m_prototypes[instance.prototypeIndex].maxScale.y * 10.0f); // Rough height estimate
    }

    chunk.instanceCount = static_cast<uint32_t>(chunk.instances.size());
    chunk.isLoaded = true;
    chunk.isDirty = true;

    return chunk;
}

std::vector<VegetationInstance> VegetationSpawner::SpawnRegion(float minX, float minZ, float maxX, float maxZ) {
    std::vector<VegetationInstance> instances;

    if (!m_biomeMap || !m_terrainQuery || m_prototypes.empty()) {
        return instances;
    }

    // Seed RNG based on region center
    int32_t regionCenterX = static_cast<int32_t>((minX + maxX) * 0.5f);
    int32_t regionCenterZ = static_cast<int32_t>((minZ + maxZ) * 0.5f);
    SeedRNG(regionCenterX, regionCenterZ);

    // Get the dominant biome for this region
    float centerX = (minX + maxX) * 0.5f;
    float centerZ = (minZ + maxZ) * 0.5f;
    BiomeSample biomeSample = m_biomeMap->Sample(centerX, centerZ);
    const BiomeVegetationDensity* density = GetBiomeDensity(biomeSample.primary);

    if (!density) {
        return instances;
    }

    // Spawn each vegetation category
    float densityMult = m_params.densityMultiplier;

    // Trees
    if (density->treeDensity > 0.0f && !density->treeWeights.empty()) {
        SpawnCategory(VegetationType::Tree, density->treeDensity * densityMult,
                      density->treeWeights, m_params.minTreeSpacing,
                      minX, minZ, maxX, maxZ, instances);
    }

    // Bushes
    if (density->bushDensity > 0.0f && !density->bushWeights.empty()) {
        SpawnCategory(VegetationType::Bush, density->bushDensity * densityMult,
                      density->bushWeights, m_params.minBushSpacing,
                      minX, minZ, maxX, maxZ, instances);
    }

    // Grass
    if (density->grassDensity > 0.0f && !density->grassWeights.empty()) {
        SpawnCategory(VegetationType::Grass, density->grassDensity * densityMult,
                      density->grassWeights, m_params.minGrassSpacing,
                      minX, minZ, maxX, maxZ, instances);
    }

    // Flowers
    if (density->flowerDensity > 0.0f && !density->flowerWeights.empty()) {
        SpawnCategory(VegetationType::Flower, density->flowerDensity * densityMult,
                      density->flowerWeights, m_params.minGrassSpacing,
                      minX, minZ, maxX, maxZ, instances);
    }

    // Rocks
    if (density->rockDensity > 0.0f && !density->rockWeights.empty()) {
        SpawnCategory(VegetationType::Rock, density->rockDensity * densityMult,
                      density->rockWeights, m_params.minBushSpacing,
                      minX, minZ, maxX, maxZ, instances);
    }

    // Limit instance count
    if (instances.size() > m_params.maxInstancesPerChunk) {
        instances.resize(m_params.maxInstancesPerChunk);
    }

    m_stats.totalInstances += static_cast<uint32_t>(instances.size());
    return instances;
}

void VegetationSpawner::SpawnCategory(
    VegetationType type, float density,
    const std::vector<std::pair<uint32_t, float>>& weights,
    float minSpacing, float minX, float minZ, float maxX, float maxZ,
    std::vector<VegetationInstance>& outInstances)
{
    float area = (maxX - minX) * (maxZ - minZ);
    int targetCount = static_cast<int>(area * density);

    if (targetCount <= 0) return;

    std::vector<glm::vec2> spawnPoints;

    // Use the configured sampling method
    // Legacy flag support: if usePoissonDisk is false and method is Poisson, use Random
    SamplingMethod method = m_params.samplingMethod;
    if (!m_params.usePoissonDisk && method == SamplingMethod::PoissonDisk) {
        method = SamplingMethod::Random;
    }

    // Use academic sampling algorithms for natural distribution
    spawnPoints = SamplePoints(method, minX, minZ, maxX, maxZ, minSpacing, type);

    // Spawn at each point
    for (const auto& point : spawnPoints) {
        // For methods that don't use variable density internally,
        // apply density rejection here
        if (method == SamplingMethod::Random ||
            method == SamplingMethod::Stratified ||
            method == SamplingMethod::BlueNoise) {
            float localDensity = GetDensityAtPosition(type, point.x, point.y);
            if (RandomFloat() > localDensity) {
                continue; // Skip based on local density
            }
        }

        uint32_t protoIdx = SelectPrototype(weights);
        VegetationInstance instance;

        if (TrySpawnInstance(type, protoIdx, point.x, point.y, instance)) {
            outInstances.push_back(instance);
        }
    }
}

bool VegetationSpawner::TrySpawnInstance(
    VegetationType type, uint32_t prototypeIndex,
    float x, float z, VegetationInstance& outInstance)
{
    if (prototypeIndex >= m_prototypes.size()) {
        return false;
    }

    const VegetationPrototype& proto = m_prototypes[prototypeIndex];

    // Query terrain
    float height;
    glm::vec3 normal;
    if (!m_terrainQuery(x, z, height, normal)) {
        return false;
    }

    // Check slope constraints
    float slope = 1.0f - std::abs(normal.y);
    if (slope < proto.minSlope || slope > proto.maxSlope) {
        return false;
    }

    // Check height constraints
    if (height < proto.minHeight || height > proto.maxHeight) {
        return false;
    }

    // Create instance
    outInstance.position = glm::vec3(x, height, z);
    outInstance.prototypeIndex = prototypeIndex;

    // Random rotation
    float yaw = RandomFloat(0.0f, glm::radians(proto.randomYawRange));
    outInstance.rotation = glm::angleAxis(yaw, glm::vec3(0, 1, 0));

    // Align to terrain if needed
    if (proto.alignToTerrain && std::abs(normal.y) < 0.99f) {
        glm::vec3 up = glm::normalize(normal);
        glm::vec3 right = glm::normalize(glm::cross(up, glm::vec3(0, 0, 1)));
        glm::vec3 forward = glm::cross(right, up);
        glm::mat3 rotMat(right, up, forward);
        glm::quat terrainRot = glm::quat_cast(rotMat);
        outInstance.rotation = terrainRot * outInstance.rotation;
    }

    // Random scale
    if (proto.uniformScale) {
        float s = RandomFloat(proto.minScale.x, proto.maxScale.x);
        outInstance.scale = glm::vec3(s);
    } else {
        outInstance.scale = glm::vec3(
            RandomFloat(proto.minScale.x, proto.maxScale.x),
            RandomFloat(proto.minScale.y, proto.maxScale.y),
            RandomFloat(proto.minScale.z, proto.maxScale.z)
        );
    }

    // Set flags
    outInstance.flags = VegetationInstance::FLAG_VISIBLE;
    if (proto.castShadows) {
        outInstance.flags |= VegetationInstance::FLAG_SHADOW_CASTER;
    }
    if (proto.windStrength > 0.0f) {
        outInstance.flags |= VegetationInstance::FLAG_WIND_AFFECTED;
    }
    if (proto.collisionRadius > 0.0f) {
        outInstance.flags |= VegetationInstance::FLAG_COLLISION;
    }

    outInstance.currentLOD = VegetationLOD::Full;
    outInstance.distanceToCamera = 0.0f;

    return true;
}

uint32_t VegetationSpawner::SelectPrototype(const std::vector<std::pair<uint32_t, float>>& weights) {
    if (weights.empty()) return 0;
    if (weights.size() == 1) return weights[0].first;

    // Calculate total weight
    float totalWeight = 0.0f;
    for (const auto& w : weights) {
        totalWeight += w.second;
    }

    // Random selection
    float r = RandomFloat(0.0f, totalWeight);
    float accumulated = 0.0f;

    for (const auto& w : weights) {
        accumulated += w.second;
        if (r <= accumulated) {
            return w.first;
        }
    }

    return weights.back().first;
}

float VegetationSpawner::GetDensityAtPosition(VegetationType type, float x, float z) const {
    if (!m_biomeMap) return 1.0f;

    BiomeSample sample = m_biomeMap->Sample(x, z);
    const BiomeVegetationDensity* primaryDensity = GetBiomeDensity(sample.primary);
    const BiomeVegetationDensity* secondaryDensity = GetBiomeDensity(sample.secondary);

    float primaryValue = 0.0f;
    float secondaryValue = 0.0f;

    auto getDensityValue = [type](const BiomeVegetationDensity* d) -> float {
        if (!d) return 0.0f;
        switch (type) {
            case VegetationType::Tree: return d->treeDensity;
            case VegetationType::Bush: return d->bushDensity;
            case VegetationType::Grass: return d->grassDensity;
            case VegetationType::Flower: return d->flowerDensity;
            case VegetationType::Rock: return d->rockDensity;
            default: return 0.0f;
        }
    };

    primaryValue = getDensityValue(primaryDensity);
    secondaryValue = getDensityValue(secondaryDensity);

    // Blend between biomes
    return glm::mix(primaryValue, secondaryValue, sample.blendWeight);
}

// ============================================================================
// LOD AND CULLING
// ============================================================================

void VegetationSpawner::UpdateLODs(VegetationChunk& chunk, const glm::vec3& cameraPos) {
    m_stats.lod0Count = 0;
    m_stats.lod1Count = 0;
    m_stats.lod2Count = 0;
    m_stats.billboardCount = 0;
    m_stats.culledInstances = 0;

    for (auto& instance : chunk.instances) {
        if (instance.prototypeIndex >= m_prototypes.size()) continue;

        const VegetationPrototype& proto = m_prototypes[instance.prototypeIndex];
        float dist = glm::length(instance.position - cameraPos);
        instance.distanceToCamera = dist;

        VegetationLOD newLOD;
        if (dist < proto.lodDistance0) {
            newLOD = VegetationLOD::Full;
            ++m_stats.lod0Count;
        } else if (dist < proto.lodDistance1) {
            newLOD = VegetationLOD::Medium;
            ++m_stats.lod1Count;
        } else if (dist < proto.lodDistance2) {
            newLOD = VegetationLOD::Low;
            ++m_stats.lod2Count;
        } else if (dist < proto.cullDistance) {
            newLOD = VegetationLOD::Billboard;
            ++m_stats.billboardCount;
        } else {
            newLOD = VegetationLOD::Culled;
            ++m_stats.culledInstances;
        }

        if (newLOD != instance.currentLOD) {
            instance.currentLOD = newLOD;
            chunk.isDirty = true;
        }
    }

    m_stats.visibleInstances = m_stats.lod0Count + m_stats.lod1Count +
                               m_stats.lod2Count + m_stats.billboardCount;
}

void VegetationSpawner::FrustumCull(VegetationChunk& chunk, const glm::mat4& viewProj) {
    // Extract frustum planes from view-projection matrix
    glm::vec4 planes[6];

    // Left
    planes[0] = glm::vec4(viewProj[0][3] + viewProj[0][0],
                          viewProj[1][3] + viewProj[1][0],
                          viewProj[2][3] + viewProj[2][0],
                          viewProj[3][3] + viewProj[3][0]);
    // Right
    planes[1] = glm::vec4(viewProj[0][3] - viewProj[0][0],
                          viewProj[1][3] - viewProj[1][0],
                          viewProj[2][3] - viewProj[2][0],
                          viewProj[3][3] - viewProj[3][0]);
    // Bottom
    planes[2] = glm::vec4(viewProj[0][3] + viewProj[0][1],
                          viewProj[1][3] + viewProj[1][1],
                          viewProj[2][3] + viewProj[2][1],
                          viewProj[3][3] + viewProj[3][1]);
    // Top
    planes[3] = glm::vec4(viewProj[0][3] - viewProj[0][1],
                          viewProj[1][3] - viewProj[1][1],
                          viewProj[2][3] - viewProj[2][1],
                          viewProj[3][3] - viewProj[3][1]);
    // Near
    planes[4] = glm::vec4(viewProj[0][3] + viewProj[0][2],
                          viewProj[1][3] + viewProj[1][2],
                          viewProj[2][3] + viewProj[2][2],
                          viewProj[3][3] + viewProj[3][2]);
    // Far
    planes[5] = glm::vec4(viewProj[0][3] - viewProj[0][2],
                          viewProj[1][3] - viewProj[1][2],
                          viewProj[2][3] - viewProj[2][2],
                          viewProj[3][3] - viewProj[3][2]);

    // Normalize planes
    for (int i = 0; i < 6; ++i) {
        float len = glm::length(glm::vec3(planes[i]));
        planes[i] /= len;
    }

    // Test each instance
    for (auto& instance : chunk.instances) {
        if (instance.currentLOD == VegetationLOD::Culled) {
            instance.flags &= ~VegetationInstance::FLAG_VISIBLE;
            continue;
        }

        // Simple sphere test (assumes instance occupies a sphere of radius 5)
        float radius = 5.0f * glm::max(instance.scale.x, glm::max(instance.scale.y, instance.scale.z));
        bool visible = true;

        for (int i = 0; i < 6 && visible; ++i) {
            float dist = glm::dot(glm::vec3(planes[i]), instance.position) + planes[i].w;
            if (dist < -radius) {
                visible = false;
            }
        }

        if (visible) {
            instance.flags |= VegetationInstance::FLAG_VISIBLE;
        } else {
            instance.flags &= ~VegetationInstance::FLAG_VISIBLE;
        }
    }
}

// ============================================================================
// CONFIGURATION
// ============================================================================

bool VegetationSpawner::LoadConfig(const std::string& path) {
    try {
        std::ifstream file(path);
        if (!file.is_open()) return false;

        nlohmann::json json;
        file >> json;

        // Load prototypes
        if (json.contains("prototypes")) {
            m_prototypes.clear();
            for (const auto& protoJson : json["prototypes"]) {
                VegetationPrototype proto;
                if (protoJson.contains("name")) proto.name = protoJson["name"];
                if (protoJson.contains("type")) proto.type = static_cast<VegetationType>(protoJson["type"].get<int>());
                if (protoJson.contains("meshLOD0")) proto.meshPathLOD0 = protoJson["meshLOD0"];
                if (protoJson.contains("meshLOD1")) proto.meshPathLOD1 = protoJson["meshLOD1"];
                if (protoJson.contains("meshLOD2")) proto.meshPathLOD2 = protoJson["meshLOD2"];
                if (protoJson.contains("billboard")) proto.billboardAtlas = protoJson["billboard"];
                if (protoJson.contains("lodDistance0")) proto.lodDistance0 = protoJson["lodDistance0"];
                if (protoJson.contains("lodDistance1")) proto.lodDistance1 = protoJson["lodDistance1"];
                if (protoJson.contains("lodDistance2")) proto.lodDistance2 = protoJson["lodDistance2"];
                if (protoJson.contains("cullDistance")) proto.cullDistance = protoJson["cullDistance"];
                m_prototypes.push_back(proto);
            }
        }

        return true;
    } catch (...) {
        return false;
    }
}

bool VegetationSpawner::SaveConfig(const std::string& path) const {
    try {
        nlohmann::json json;

        // Save prototypes
        json["prototypes"] = nlohmann::json::array();
        for (const auto& proto : m_prototypes) {
            nlohmann::json protoJson;
            protoJson["name"] = proto.name;
            protoJson["type"] = static_cast<int>(proto.type);
            protoJson["meshLOD0"] = proto.meshPathLOD0;
            protoJson["meshLOD1"] = proto.meshPathLOD1;
            protoJson["meshLOD2"] = proto.meshPathLOD2;
            protoJson["billboard"] = proto.billboardAtlas;
            protoJson["lodDistance0"] = proto.lodDistance0;
            protoJson["lodDistance1"] = proto.lodDistance1;
            protoJson["lodDistance2"] = proto.lodDistance2;
            protoJson["cullDistance"] = proto.cullDistance;
            json["prototypes"].push_back(protoJson);
        }

        std::ofstream file(path);
        file << json.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace Cortex::Editor
