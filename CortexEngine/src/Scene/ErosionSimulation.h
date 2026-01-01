#pragma once

// ErosionSimulation.h
// Background erosion simulation system for terrain weathering.
// Implements hydraulic (water) and thermal (rockfall) erosion.

#include <glm/glm.hpp>
#include <vector>
#include <queue>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>
#include <cstdint>

namespace Cortex::Scene {

// Heightmap patch that has been modified by erosion
struct ErosionPatch {
    int32_t chunkX = 0;
    int32_t chunkZ = 0;
    int32_t localX = 0;          // Local offset within chunk
    int32_t localZ = 0;
    int32_t width = 0;           // Patch dimensions
    int32_t height = 0;
    std::vector<float> heights;  // Modified height values
    std::vector<float> sediment; // Sediment layer (optional)
    bool dirty = true;
};

// Parameters for hydraulic erosion (water-based)
struct HydraulicErosionParams {
    uint32_t numDroplets = 50000;    // Number of water droplets per iteration
    uint32_t maxDropletLifetime = 64; // Max steps a droplet can travel
    float inertia = 0.05f;           // How much droplet keeps its direction (0-1)
    float sedimentCapacityFactor = 4.0f; // Multiplier for sediment carrying capacity
    float minSedimentCapacity = 0.01f;   // Minimum capacity before depositing
    float erosionSpeed = 0.3f;       // How fast terrain erodes
    float depositSpeed = 0.3f;       // How fast sediment deposits
    float evaporateSpeed = 0.01f;    // Water evaporation rate
    float gravity = 4.0f;            // Acceleration due to gravity
    float minSlope = 0.01f;          // Minimum slope for erosion to occur
    float erosionRadius = 3.0f;      // Brush radius for erosion/deposition
    float initialWaterVolume = 1.0f; // Starting water per droplet
    float initialSpeed = 1.0f;       // Starting velocity
};

// Parameters for thermal erosion (rockfall/weathering)
struct ThermalErosionParams {
    uint32_t iterations = 50;        // Iterations per step
    float talusAngle = 0.5f;         // Angle of repose (radians, ~30 degrees)
    float thermalRate = 0.5f;        // How much material moves per iteration
    float cellSize = 1.0f;           // World space size of each cell
    bool enableSlumping = true;      // Enable material slumping on steep slopes
};

// Combined erosion parameters
struct ErosionParams {
    HydraulicErosionParams hydraulic;
    ThermalErosionParams thermal;

    // General settings
    uint32_t seed = 12345;
    bool enableHydraulic = true;
    bool enableThermal = true;
    float blendFactor = 1.0f;        // How much erosion affects final terrain (0-1)

    // Performance settings
    uint32_t maxPatchesPerFrame = 4; // Max patches to return per update
    uint32_t iterationsPerStep = 1;  // Erosion iterations per simulation step
};

// Water droplet for hydraulic erosion
struct WaterDroplet {
    glm::vec2 position;
    glm::vec2 direction;
    float speed;
    float water;
    float sediment;
    int lifetime;
};

// Erosion simulation state for a heightmap region
class ErosionSimulation {
public:
    ErosionSimulation();
    ~ErosionSimulation();

    // Initialize with heightmap data
    // heightmap: pointer to height values (row-major)
    // width, height: dimensions
    // cellSize: world space size of each cell
    void Initialize(float* heightmap, int width, int height, float cellSize);

    // Set erosion parameters
    void SetParams(const ErosionParams& params);
    const ErosionParams& GetParams() const { return m_params; }

    // Run erosion steps
    void StepHydraulic(uint32_t iterations);
    void StepThermal(uint32_t iterations);
    void StepCombined(uint32_t iterations);

    // Get modified patches (thread-safe)
    // Returns patches that have been modified since last call
    std::vector<ErosionPatch> GetUpdatedPatches();

    // Check if simulation has pending updates
    bool HasPendingUpdates() const;

    // Get current heightmap (read-only)
    const std::vector<float>& GetHeightmap() const { return m_heightmap; }
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

    // Background thread control
    void StartBackgroundSimulation();
    void StopBackgroundSimulation();
    bool IsSimulationRunning() const { return m_running.load(); }

    // Set callback for when patches are ready
    using PatchReadyCallback = std::function<void(const std::vector<ErosionPatch>&)>;
    void SetPatchReadyCallback(PatchReadyCallback callback);

    // Statistics
    uint64_t GetTotalIterations() const { return m_totalIterations; }
    float GetAverageHeightChange() const;

private:
    // Heightmap data
    std::vector<float> m_heightmap;
    std::vector<float> m_originalHeightmap;  // For blending
    std::vector<float> m_sedimentMap;        // Accumulated sediment
    int m_width = 0;
    int m_height = 0;
    float m_cellSize = 1.0f;

    // Parameters
    ErosionParams m_params;

    // Dirty tracking for patches
    std::vector<bool> m_dirtyFlags;
    int m_patchSize = 32;  // Size of each patch for dirty tracking

    // Thread safety
    mutable std::mutex m_mutex;
    std::queue<ErosionPatch> m_pendingPatches;

    // Background thread
    std::thread m_workerThread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_shouldStop{false};
    PatchReadyCallback m_patchCallback;

    // Statistics
    std::atomic<uint64_t> m_totalIterations{0};
    float m_totalHeightChange = 0.0f;

    // Random number generation
    uint32_t m_rngState;
    float RandomFloat();
    float RandomFloat(float min, float max);

    // Hydraulic erosion helpers
    float SampleHeight(float x, float z) const;
    glm::vec2 SampleGradient(float x, float z) const;
    void ErodeAt(float x, float z, float amount);
    void DepositAt(float x, float z, float amount);
    void SimulateDroplet(WaterDroplet& droplet);

    // Thermal erosion helpers
    void ThermalErodeCell(int x, int z);
    float GetMaxSlope(int x, int z, int& lowestNeighborX, int& lowestNeighborZ) const;

    // Patch management
    void MarkDirty(int x, int z);
    void GeneratePatches();

    // Background worker
    void WorkerThreadFunc();
};

// Global erosion manager for multiple chunks
class ErosionManager {
public:
    static ErosionManager& Instance();

    // Initialize manager
    void Initialize(const ErosionParams& params);
    void Shutdown();

    // Queue a chunk for erosion
    void QueueChunk(int32_t chunkX, int32_t chunkZ,
                    float* heightmap, int width, int height, float cellSize);

    // Update and get ready patches
    std::vector<ErosionPatch> Update();

    // Check if chunk has pending erosion
    bool IsChunkPending(int32_t chunkX, int32_t chunkZ) const;

    // Set global erosion parameters
    void SetParams(const ErosionParams& params);

private:
    ErosionManager() = default;
    ~ErosionManager();

    ErosionParams m_params;

    struct ChunkErosion {
        int32_t chunkX, chunkZ;
        std::unique_ptr<ErosionSimulation> simulation;
        bool complete = false;
    };

    std::vector<std::unique_ptr<ChunkErosion>> m_activeChunks;
    std::queue<ChunkErosion> m_pendingChunks;
    mutable std::mutex m_mutex;

    std::thread m_workerThread;
    std::atomic<bool> m_running{false};

    void WorkerThreadFunc();
};

} // namespace Cortex::Scene
