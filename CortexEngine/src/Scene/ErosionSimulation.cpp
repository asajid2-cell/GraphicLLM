// ErosionSimulation.cpp
// Implementation of hydraulic and thermal erosion algorithms.

#include "ErosionSimulation.h"
#include <cmath>
#include <algorithm>
#include <chrono>

namespace Cortex::Scene {

// ============================================================================
// EROSION SIMULATION
// ============================================================================

ErosionSimulation::ErosionSimulation()
    : m_rngState(12345)
{
}

ErosionSimulation::~ErosionSimulation() {
    StopBackgroundSimulation();
}

void ErosionSimulation::Initialize(float* heightmap, int width, int height, float cellSize) {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_width = width;
    m_height = height;
    m_cellSize = cellSize;

    // Copy heightmap data
    size_t size = static_cast<size_t>(width * height);
    m_heightmap.resize(size);
    m_originalHeightmap.resize(size);
    m_sedimentMap.resize(size, 0.0f);

    std::copy(heightmap, heightmap + size, m_heightmap.begin());
    std::copy(heightmap, heightmap + size, m_originalHeightmap.begin());

    // Initialize dirty tracking
    int patchCountX = (width + m_patchSize - 1) / m_patchSize;
    int patchCountZ = (height + m_patchSize - 1) / m_patchSize;
    m_dirtyFlags.resize(patchCountX * patchCountZ, false);

    m_totalHeightChange = 0.0f;
    m_totalIterations = 0;
}

void ErosionSimulation::SetParams(const ErosionParams& params) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_params = params;
    m_rngState = params.seed;
}

// ============================================================================
// RANDOM NUMBER GENERATION
// ============================================================================

float ErosionSimulation::RandomFloat() {
    // Xorshift RNG
    m_rngState ^= m_rngState << 13;
    m_rngState ^= m_rngState >> 17;
    m_rngState ^= m_rngState << 5;
    return static_cast<float>(m_rngState) / static_cast<float>(0xFFFFFFFF);
}

float ErosionSimulation::RandomFloat(float min, float max) {
    return min + RandomFloat() * (max - min);
}

// ============================================================================
// HEIGHTMAP SAMPLING
// ============================================================================

float ErosionSimulation::SampleHeight(float x, float z) const {
    // Bilinear interpolation
    int x0 = static_cast<int>(std::floor(x));
    int z0 = static_cast<int>(std::floor(z));
    int x1 = x0 + 1;
    int z1 = z0 + 1;

    // Clamp to bounds
    x0 = std::clamp(x0, 0, m_width - 1);
    x1 = std::clamp(x1, 0, m_width - 1);
    z0 = std::clamp(z0, 0, m_height - 1);
    z1 = std::clamp(z1, 0, m_height - 1);

    float fx = x - std::floor(x);
    float fz = z - std::floor(z);

    float h00 = m_heightmap[z0 * m_width + x0];
    float h10 = m_heightmap[z0 * m_width + x1];
    float h01 = m_heightmap[z1 * m_width + x0];
    float h11 = m_heightmap[z1 * m_width + x1];

    float h0 = h00 * (1.0f - fx) + h10 * fx;
    float h1 = h01 * (1.0f - fx) + h11 * fx;

    return h0 * (1.0f - fz) + h1 * fz;
}

glm::vec2 ErosionSimulation::SampleGradient(float x, float z) const {
    // Central differences for gradient
    float delta = 1.0f;

    float hL = SampleHeight(x - delta, z);
    float hR = SampleHeight(x + delta, z);
    float hD = SampleHeight(x, z - delta);
    float hU = SampleHeight(x, z + delta);

    return glm::vec2(hR - hL, hU - hD) / (2.0f * delta * m_cellSize);
}

// ============================================================================
// EROSION / DEPOSITION
// ============================================================================

void ErosionSimulation::ErodeAt(float x, float z, float amount) {
    const auto& params = m_params.hydraulic;
    float radius = params.erosionRadius;
    int r = static_cast<int>(std::ceil(radius));

    int cx = static_cast<int>(x);
    int cz = static_cast<int>(z);

    float totalWeight = 0.0f;

    // Calculate weights first
    std::vector<std::pair<int, float>> weights;
    for (int dz = -r; dz <= r; ++dz) {
        for (int dx = -r; dx <= r; ++dx) {
            int px = cx + dx;
            int pz = cz + dz;

            if (px < 0 || px >= m_width || pz < 0 || pz >= m_height) continue;

            float dist = std::sqrt(static_cast<float>(dx * dx + dz * dz));
            if (dist > radius) continue;

            float weight = std::max(0.0f, radius - dist) / radius;
            weight = weight * weight;  // Quadratic falloff

            weights.emplace_back(pz * m_width + px, weight);
            totalWeight += weight;
        }
    }

    // Apply erosion with normalized weights
    if (totalWeight > 0.001f) {
        for (const auto& [idx, weight] : weights) {
            float normalizedWeight = weight / totalWeight;
            float erosionAmount = amount * normalizedWeight;

            m_heightmap[idx] -= erosionAmount;
            m_totalHeightChange += erosionAmount;

            // Mark patch dirty
            int px = idx % m_width;
            int pz = idx / m_width;
            MarkDirty(px, pz);
        }
    }
}

void ErosionSimulation::DepositAt(float x, float z, float amount) {
    const auto& params = m_params.hydraulic;
    float radius = params.erosionRadius;
    int r = static_cast<int>(std::ceil(radius));

    int cx = static_cast<int>(x);
    int cz = static_cast<int>(z);

    float totalWeight = 0.0f;
    std::vector<std::pair<int, float>> weights;

    for (int dz = -r; dz <= r; ++dz) {
        for (int dx = -r; dx <= r; ++dx) {
            int px = cx + dx;
            int pz = cz + dz;

            if (px < 0 || px >= m_width || pz < 0 || pz >= m_height) continue;

            float dist = std::sqrt(static_cast<float>(dx * dx + dz * dz));
            if (dist > radius) continue;

            float weight = std::max(0.0f, radius - dist) / radius;
            weight = weight * weight;

            weights.emplace_back(pz * m_width + px, weight);
            totalWeight += weight;
        }
    }

    if (totalWeight > 0.001f) {
        for (const auto& [idx, weight] : weights) {
            float normalizedWeight = weight / totalWeight;
            float depositAmount = amount * normalizedWeight;

            m_heightmap[idx] += depositAmount;
            m_sedimentMap[idx] += depositAmount;

            int px = idx % m_width;
            int pz = idx / m_width;
            MarkDirty(px, pz);
        }
    }
}

// ============================================================================
// HYDRAULIC EROSION
// ============================================================================

void ErosionSimulation::SimulateDroplet(WaterDroplet& droplet) {
    const auto& params = m_params.hydraulic;

    for (int i = 0; i < static_cast<int>(params.maxDropletLifetime); ++i) {
        // Get current cell
        int cellX = static_cast<int>(droplet.position.x);
        int cellZ = static_cast<int>(droplet.position.y);

        // Check bounds
        if (cellX < 0 || cellX >= m_width - 1 || cellZ < 0 || cellZ >= m_height - 1) {
            break;
        }

        // Sample height and gradient at current position
        float currentHeight = SampleHeight(droplet.position.x, droplet.position.y);
        glm::vec2 gradient = SampleGradient(droplet.position.x, droplet.position.y);

        // Update direction with inertia
        droplet.direction = droplet.direction * params.inertia -
                           gradient * (1.0f - params.inertia);

        float dirLength = glm::length(droplet.direction);
        if (dirLength < 0.0001f) {
            // Random direction if stuck
            float angle = RandomFloat() * 6.28318f;
            droplet.direction = glm::vec2(std::cos(angle), std::sin(angle));
        } else {
            droplet.direction /= dirLength;
        }

        // Move droplet
        glm::vec2 newPos = droplet.position + droplet.direction;

        // Clamp to bounds
        newPos.x = std::clamp(newPos.x, 0.0f, static_cast<float>(m_width - 2));
        newPos.y = std::clamp(newPos.y, 0.0f, static_cast<float>(m_height - 2));

        // Sample new height
        float newHeight = SampleHeight(newPos.x, newPos.y);
        float heightDiff = newHeight - currentHeight;

        // Calculate sediment capacity
        float sedimentCapacity = std::max(
            -heightDiff * droplet.speed * droplet.water * params.sedimentCapacityFactor,
            params.minSedimentCapacity
        );

        // Erosion or deposition
        if (droplet.sediment > sedimentCapacity || heightDiff > 0) {
            // Deposit sediment
            float depositAmount = (heightDiff > 0)
                ? std::min(heightDiff, droplet.sediment)
                : (droplet.sediment - sedimentCapacity) * params.depositSpeed;

            droplet.sediment -= depositAmount;
            DepositAt(droplet.position.x, droplet.position.y, depositAmount);
        } else {
            // Erode terrain
            float erosionAmount = std::min(
                (sedimentCapacity - droplet.sediment) * params.erosionSpeed,
                -heightDiff
            );

            droplet.sediment += erosionAmount;
            ErodeAt(droplet.position.x, droplet.position.y, erosionAmount);
        }

        // Update speed
        float speedSq = droplet.speed * droplet.speed + heightDiff * params.gravity;
        droplet.speed = std::sqrt(std::max(0.0f, speedSq));

        // Evaporate water
        droplet.water *= (1.0f - params.evaporateSpeed);

        // Update position
        droplet.position = newPos;

        // Stop if water depleted
        if (droplet.water < 0.001f) {
            break;
        }
    }

    // Deposit remaining sediment
    if (droplet.sediment > 0.001f) {
        DepositAt(droplet.position.x, droplet.position.y, droplet.sediment);
    }
}

void ErosionSimulation::StepHydraulic(uint32_t iterations) {
    std::lock_guard<std::mutex> lock(m_mutex);

    const auto& params = m_params.hydraulic;

    for (uint32_t iter = 0; iter < iterations; ++iter) {
        for (uint32_t d = 0; d < params.numDroplets; ++d) {
            // Create random droplet
            WaterDroplet droplet;
            droplet.position.x = RandomFloat(1.0f, static_cast<float>(m_width - 2));
            droplet.position.y = RandomFloat(1.0f, static_cast<float>(m_height - 2));
            droplet.direction = glm::vec2(0.0f, 0.0f);
            droplet.speed = params.initialSpeed;
            droplet.water = params.initialWaterVolume;
            droplet.sediment = 0.0f;
            droplet.lifetime = 0;

            SimulateDroplet(droplet);
        }

        ++m_totalIterations;
    }

    // Generate patches for modified areas
    GeneratePatches();
}

// ============================================================================
// THERMAL EROSION
// ============================================================================

float ErosionSimulation::GetMaxSlope(int x, int z, int& lowestNeighborX, int& lowestNeighborZ) const {
    float centerHeight = m_heightmap[z * m_width + x];
    float maxSlope = 0.0f;
    lowestNeighborX = x;
    lowestNeighborZ = z;

    // 8-connected neighbors
    static const int dx[] = { -1, 0, 1, -1, 1, -1, 0, 1 };
    static const int dz[] = { -1, -1, -1, 0, 0, 1, 1, 1 };
    static const float dist[] = { 1.414f, 1.0f, 1.414f, 1.0f, 1.0f, 1.414f, 1.0f, 1.414f };

    for (int i = 0; i < 8; ++i) {
        int nx = x + dx[i];
        int nz = z + dz[i];

        if (nx < 0 || nx >= m_width || nz < 0 || nz >= m_height) continue;

        float neighborHeight = m_heightmap[nz * m_width + nx];
        float slope = (centerHeight - neighborHeight) / (dist[i] * m_cellSize);

        if (slope > maxSlope) {
            maxSlope = slope;
            lowestNeighborX = nx;
            lowestNeighborZ = nz;
        }
    }

    return maxSlope;
}

void ErosionSimulation::ThermalErodeCell(int x, int z) {
    const auto& params = m_params.thermal;

    int lowestX, lowestZ;
    float maxSlope = GetMaxSlope(x, z, lowestX, lowestZ);

    // Convert talus angle to slope threshold
    float talusSlope = std::tan(params.talusAngle);

    if (maxSlope > talusSlope) {
        // Calculate material to move
        float excessSlope = maxSlope - talusSlope;
        float moveAmount = excessSlope * params.thermalRate * m_cellSize;

        // Clamp to available material
        int srcIdx = z * m_width + x;
        int dstIdx = lowestZ * m_width + lowestX;

        float maxMove = (m_heightmap[srcIdx] - m_heightmap[dstIdx]) * 0.5f;
        moveAmount = std::min(moveAmount, maxMove);

        if (moveAmount > 0.001f) {
            m_heightmap[srcIdx] -= moveAmount;
            m_heightmap[dstIdx] += moveAmount;
            m_totalHeightChange += moveAmount;

            MarkDirty(x, z);
            MarkDirty(lowestX, lowestZ);
        }
    }
}

void ErosionSimulation::StepThermal(uint32_t iterations) {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (uint32_t iter = 0; iter < iterations; ++iter) {
        // Process cells in random order to avoid bias
        std::vector<int> indices(m_width * m_height);
        for (int i = 0; i < m_width * m_height; ++i) {
            indices[i] = i;
        }

        // Fisher-Yates shuffle
        for (int i = static_cast<int>(indices.size()) - 1; i > 0; --i) {
            int j = static_cast<int>(RandomFloat() * (i + 1));
            std::swap(indices[i], indices[j]);
        }

        // Process each cell
        for (int idx : indices) {
            int x = idx % m_width;
            int z = idx / m_width;

            // Skip edges
            if (x == 0 || x == m_width - 1 || z == 0 || z == m_height - 1) continue;

            ThermalErodeCell(x, z);
        }

        ++m_totalIterations;
    }

    GeneratePatches();
}

// ============================================================================
// COMBINED EROSION
// ============================================================================

void ErosionSimulation::StepCombined(uint32_t iterations) {
    for (uint32_t i = 0; i < iterations; ++i) {
        if (m_params.enableHydraulic) {
            StepHydraulic(1);
        }
        if (m_params.enableThermal) {
            StepThermal(m_params.thermal.iterations);
        }
    }
}

// ============================================================================
// PATCH MANAGEMENT
// ============================================================================

void ErosionSimulation::MarkDirty(int x, int z) {
    int patchCountX = (m_width + m_patchSize - 1) / m_patchSize;
    int px = x / m_patchSize;
    int pz = z / m_patchSize;

    if (px >= 0 && px < patchCountX && pz >= 0 && pz < static_cast<int>(m_dirtyFlags.size()) / patchCountX) {
        m_dirtyFlags[pz * patchCountX + px] = true;
    }
}

void ErosionSimulation::GeneratePatches() {
    int patchCountX = (m_width + m_patchSize - 1) / m_patchSize;
    int patchCountZ = (m_height + m_patchSize - 1) / m_patchSize;

    for (int pz = 0; pz < patchCountZ; ++pz) {
        for (int px = 0; px < patchCountX; ++px) {
            int patchIdx = pz * patchCountX + px;
            if (!m_dirtyFlags[patchIdx]) continue;

            m_dirtyFlags[patchIdx] = false;

            // Create patch
            ErosionPatch patch;
            patch.localX = px * m_patchSize;
            patch.localZ = pz * m_patchSize;
            patch.width = std::min(m_patchSize, m_width - patch.localX);
            patch.height = std::min(m_patchSize, m_height - patch.localZ);

            patch.heights.resize(patch.width * patch.height);
            patch.sediment.resize(patch.width * patch.height);

            for (int z = 0; z < patch.height; ++z) {
                for (int x = 0; x < patch.width; ++x) {
                    int srcIdx = (patch.localZ + z) * m_width + (patch.localX + x);
                    int dstIdx = z * patch.width + x;

                    // Apply blend factor
                    float original = m_originalHeightmap[srcIdx];
                    float eroded = m_heightmap[srcIdx];
                    patch.heights[dstIdx] = glm::mix(original, eroded, m_params.blendFactor);
                    patch.sediment[dstIdx] = m_sedimentMap[srcIdx];
                }
            }

            m_pendingPatches.push(std::move(patch));
        }
    }
}

std::vector<ErosionPatch> ErosionSimulation::GetUpdatedPatches() {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<ErosionPatch> result;
    uint32_t count = 0;

    while (!m_pendingPatches.empty() && count < m_params.maxPatchesPerFrame) {
        result.push_back(std::move(m_pendingPatches.front()));
        m_pendingPatches.pop();
        ++count;
    }

    return result;
}

bool ErosionSimulation::HasPendingUpdates() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_pendingPatches.empty();
}

float ErosionSimulation::GetAverageHeightChange() const {
    if (m_totalIterations == 0) return 0.0f;
    return m_totalHeightChange / static_cast<float>(m_totalIterations);
}

// ============================================================================
// BACKGROUND THREAD
// ============================================================================

void ErosionSimulation::StartBackgroundSimulation() {
    if (m_running.load()) return;

    m_shouldStop = false;
    m_running = true;

    m_workerThread = std::thread(&ErosionSimulation::WorkerThreadFunc, this);
}

void ErosionSimulation::StopBackgroundSimulation() {
    if (!m_running.load()) return;

    m_shouldStop = true;
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    m_running = false;
}

void ErosionSimulation::SetPatchReadyCallback(PatchReadyCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_patchCallback = std::move(callback);
}

void ErosionSimulation::WorkerThreadFunc() {
    while (!m_shouldStop.load()) {
        // Run erosion step
        StepCombined(m_params.iterationsPerStep);

        // Check for callback
        auto patches = GetUpdatedPatches();
        if (!patches.empty() && m_patchCallback) {
            m_patchCallback(patches);
        }

        // Rate limiting - don't run too fast
        std::this_thread::sleep_for(std::chrono::milliseconds(16));  // ~60 Hz max
    }
}

// ============================================================================
// EROSION MANAGER (SINGLETON)
// ============================================================================

ErosionManager& ErosionManager::Instance() {
    static ErosionManager instance;
    return instance;
}

ErosionManager::~ErosionManager() {
    Shutdown();
}

void ErosionManager::Initialize(const ErosionParams& params) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_params = params;
}

void ErosionManager::Shutdown() {
    m_running = false;
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_activeChunks.clear();
}

void ErosionManager::QueueChunk(int32_t chunkX, int32_t chunkZ,
                                 float* heightmap, int width, int height, float cellSize) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Check if already processing
    for (const auto& chunk : m_activeChunks) {
        if (chunk->chunkX == chunkX && chunk->chunkZ == chunkZ) {
            return;  // Already processing
        }
    }

    // Create new simulation
    auto erosion = std::make_unique<ChunkErosion>();
    erosion->chunkX = chunkX;
    erosion->chunkZ = chunkZ;
    erosion->simulation = std::make_unique<ErosionSimulation>();
    erosion->simulation->Initialize(heightmap, width, height, cellSize);
    erosion->simulation->SetParams(m_params);
    erosion->simulation->StartBackgroundSimulation();

    m_activeChunks.push_back(std::move(erosion));
}

std::vector<ErosionPatch> ErosionManager::Update() {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<ErosionPatch> allPatches;

    for (auto& chunk : m_activeChunks) {
        if (!chunk->simulation) continue;

        auto patches = chunk->simulation->GetUpdatedPatches();
        for (auto& patch : patches) {
            patch.chunkX = chunk->chunkX;
            patch.chunkZ = chunk->chunkZ;
            allPatches.push_back(std::move(patch));
        }
    }

    return allPatches;
}

bool ErosionManager::IsChunkPending(int32_t chunkX, int32_t chunkZ) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (const auto& chunk : m_activeChunks) {
        if (chunk->chunkX == chunkX && chunk->chunkZ == chunkZ) {
            return chunk->simulation && chunk->simulation->HasPendingUpdates();
        }
    }
    return false;
}

void ErosionManager::SetParams(const ErosionParams& params) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_params = params;
}

} // namespace Cortex::Scene
