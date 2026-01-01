#pragma once

// NetPrediction.h
// Client-side prediction and server reconciliation.
// Also includes lag compensation for hit detection.

#include "NetPacket.h"
#include "NetMessages.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <deque>
#include <unordered_map>
#include <functional>
#include <optional>

// Forward declare ECS types
namespace entt { class registry; }
using Entity = uint32_t;

namespace Cortex::Network {

// ============================================================================
// Input Buffering
// ============================================================================

struct BufferedInput {
    uint32_t sequence;          // Input sequence number
    uint32_t serverTick;        // Server tick when input was generated
    float deltaTime;            // Delta time for this input
    PlayerInputMessage input;   // The actual input
    uint64_t timestamp;         // Local timestamp

    // Result state (for reconciliation)
    glm::vec3 resultPosition;
    glm::vec3 resultVelocity;
    glm::quat resultRotation;
};

class InputBuffer {
public:
    InputBuffer(size_t maxSize = 128);

    // Add input to buffer
    void AddInput(const BufferedInput& input);

    // Get input by sequence
    const BufferedInput* GetInput(uint32_t sequence) const;

    // Get all inputs after sequence
    std::vector<const BufferedInput*> GetInputsAfter(uint32_t sequence) const;

    // Remove inputs up to sequence (acknowledged by server)
    void RemoveUpTo(uint32_t sequence);

    // Get latest sequence
    uint32_t GetLatestSequence() const;

    // Statistics
    size_t GetBufferSize() const { return m_inputs.size(); }
    float GetBufferTime() const;  // Time span of buffered inputs

private:
    std::deque<BufferedInput> m_inputs;
    size_t m_maxSize;
    uint32_t m_nextSequence = 1;
};

// ============================================================================
// State Snapshot (for prediction/reconciliation)
// ============================================================================

struct EntityState {
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 velocity;
    glm::vec3 angularVelocity;

    // Custom state data
    std::vector<uint8_t> customData;

    bool operator==(const EntityState& other) const;
    bool operator!=(const EntityState& other) const { return !(*this == other); }

    // Interpolation
    static EntityState Lerp(const EntityState& a, const EntityState& b, float t);
};

struct PredictedState {
    uint32_t inputSequence;
    uint32_t serverTick;
    EntityState state;
};

// ============================================================================
// Client-Side Prediction
// ============================================================================

class ClientPrediction {
public:
    using SimulateFunc = std::function<EntityState(Entity, const EntityState&, const PlayerInputMessage&, float dt)>;

    ClientPrediction();
    ~ClientPrediction() = default;

    // Initialize with registry
    void Initialize(entt::registry* registry);

    // Set simulation callback (applies input to entity)
    void SetSimulateCallback(SimulateFunc callback) { m_simulateCallback = callback; }

    // Set local player entity
    void SetLocalPlayer(Entity entity) { m_localPlayer = entity; }
    Entity GetLocalPlayer() const { return m_localPlayer; }

    // Process local input (predict immediately)
    void ProcessLocalInput(const PlayerInputMessage& input, float deltaTime);

    // Receive server state (reconcile)
    void ReceiveServerState(const PlayerStateMessage& state);

    // Get current predicted state
    const EntityState& GetPredictedState() const { return m_predictedState; }

    // Get interpolated state for rendering
    EntityState GetRenderState(float alpha) const;

    // Configuration
    void SetMaxPredictionTime(float seconds) { m_maxPredictionTime = seconds; }
    void SetReconciliationThreshold(float distance) { m_reconciliationThreshold = distance; }
    void SetSmoothingTime(float seconds) { m_smoothingTime = seconds; }

    // Statistics
    float GetPredictionError() const { return m_lastPredictionError; }
    uint32_t GetPredictionDepth() const { return static_cast<uint32_t>(m_inputBuffer.GetBufferSize()); }
    bool NeedsReconciliation() const { return m_needsReconciliation; }

private:
    void Reconcile(const PlayerStateMessage& serverState);
    void ReplayInputs(uint32_t fromSequence);
    EntityState SimulateInput(const EntityState& state, const PlayerInputMessage& input, float dt);

    entt::registry* m_registry = nullptr;
    Entity m_localPlayer = 0;

    // Prediction state
    EntityState m_predictedState;
    EntityState m_serverState;
    EntityState m_smoothedState;

    // Input buffer
    InputBuffer m_inputBuffer;

    // Simulation callback
    SimulateFunc m_simulateCallback;

    // Reconciliation state
    bool m_needsReconciliation = false;
    float m_lastPredictionError = 0.0f;
    uint32_t m_lastAcknowledgedInput = 0;

    // Configuration
    float m_maxPredictionTime = 0.5f;       // Max time to predict ahead
    float m_reconciliationThreshold = 0.1f;  // Distance to trigger reconciliation
    float m_smoothingTime = 0.1f;            // Time to smooth corrections

    // Smoothing
    float m_smoothingProgress = 1.0f;
    EntityState m_smoothingStart;
    EntityState m_smoothingTarget;
};

// ============================================================================
// Entity Interpolation (for non-local entities)
// ============================================================================

class EntityInterpolation {
public:
    EntityInterpolation();
    ~EntityInterpolation() = default;

    // Initialize
    void Initialize(entt::registry* registry);

    // Add state snapshot for entity
    void AddSnapshot(Entity entity, uint32_t tick, const EntityState& state);

    // Get interpolated state
    EntityState GetInterpolatedState(Entity entity, float renderTime) const;

    // Configuration
    void SetInterpolationDelay(float seconds) { m_interpolationDelay = seconds; }
    float GetInterpolationDelay() const { return m_interpolationDelay; }

    // Extrapolation settings
    void SetMaxExtrapolationTime(float seconds) { m_maxExtrapolationTime = seconds; }

    // Clear old snapshots
    void CleanupOldSnapshots(float currentTime);

private:
    struct SnapshotEntry {
        uint32_t tick;
        float timestamp;
        EntityState state;
    };

    entt::registry* m_registry = nullptr;
    std::unordered_map<Entity, std::deque<SnapshotEntry>> m_snapshots;

    float m_interpolationDelay = 0.1f;      // 100ms delay
    float m_maxExtrapolationTime = 0.25f;   // Max extrapolation time
    size_t m_maxSnapshots = 32;

    float m_currentTime = 0.0f;
};

// ============================================================================
// Lag Compensation (Server-Side)
// ============================================================================

struct HitboxSnapshot {
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 halfExtents;  // For box hitbox
    float radius;           // For capsule/sphere hitbox
    float height;           // For capsule hitbox
};

struct EntityHitboxHistory {
    Entity entity;
    std::deque<std::pair<uint32_t, HitboxSnapshot>> snapshots;  // tick -> snapshot
};

class LagCompensation {
public:
    LagCompensation();
    ~LagCompensation() = default;

    // Initialize
    void Initialize(entt::registry* registry);

    // Record hitbox state each tick
    void RecordSnapshot(uint32_t tick);

    // Get hitbox at specific tick (for hit detection)
    std::optional<HitboxSnapshot> GetHitboxAtTick(Entity entity, uint32_t tick) const;

    // Rewind world to tick for hit detection
    void BeginRewind(uint32_t tick);
    void EndRewind();

    // Perform raycast at rewound tick
    struct RaycastResult {
        bool hit = false;
        Entity entity = 0;
        glm::vec3 hitPoint;
        glm::vec3 hitNormal;
        float distance = 0.0f;
    };

    RaycastResult Raycast(const glm::vec3& origin, const glm::vec3& direction,
                           float maxDistance, uint32_t tick) const;

    // Configuration
    void SetMaxRewindTime(float seconds) { m_maxRewindTime = seconds; }
    float GetMaxRewindTime() const { return m_maxRewindTime; }

    void SetTickRate(float tickRate) { m_tickRate = tickRate; }

    // Clean up old history
    void CleanupOldSnapshots(uint32_t currentTick);

private:
    entt::registry* m_registry = nullptr;
    std::unordered_map<Entity, EntityHitboxHistory> m_hitboxHistory;

    float m_maxRewindTime = 1.0f;   // Maximum rewind time (1 second)
    float m_tickRate = 60.0f;
    uint32_t m_currentTick = 0;

    // Rewind state
    bool m_isRewound = false;
    uint32_t m_rewindTick = 0;
    std::unordered_map<Entity, HitboxSnapshot> m_originalHitboxes;
};

// ============================================================================
// Network Time Synchronization
// ============================================================================

class NetworkClock {
public:
    NetworkClock();
    ~NetworkClock() = default;

    // Process time sync message
    void ProcessTimeSync(uint64_t serverTime, uint64_t clientSendTime, uint64_t clientReceiveTime);

    // Get estimated server time
    uint64_t GetServerTime() const;

    // Get RTT
    float GetRTT() const { return m_rtt; }

    // Get clock offset
    int64_t GetClockOffset() const { return m_clockOffset; }

    // Convert between local and server time
    uint64_t LocalToServerTime(uint64_t localTime) const;
    uint64_t ServerToLocalTime(uint64_t serverTime) const;

    // Tick synchronization
    void SetTickRate(float tickRate) { m_tickRate = tickRate; }
    uint32_t GetEstimatedServerTick() const;
    float GetTickProgress() const;  // 0-1 progress to next tick

private:
    float m_rtt = 0.0f;
    float m_jitter = 0.0f;
    int64_t m_clockOffset = 0;      // Server time = local time + offset
    float m_tickRate = 60.0f;

    // For smoothing
    std::deque<int64_t> m_offsetSamples;
    size_t m_maxSamples = 10;
};

// ============================================================================
// Jitter Buffer
// ============================================================================

template<typename T>
class JitterBuffer {
public:
    JitterBuffer(float targetDelay = 0.1f, size_t maxSize = 64)
        : m_targetDelay(targetDelay), m_maxSize(maxSize) {}

    // Add item with timestamp
    void Add(const T& item, float timestamp) {
        m_buffer.push_back({item, timestamp});
        if (m_buffer.size() > m_maxSize) {
            m_buffer.pop_front();
        }

        // Sort by timestamp
        std::sort(m_buffer.begin(), m_buffer.end(),
                   [](const auto& a, const auto& b) { return a.timestamp < b.timestamp; });
    }

    // Get item at playback time
    std::optional<T> Get(float playbackTime) {
        float targetTime = playbackTime - m_targetDelay;

        // Find item at target time
        for (auto it = m_buffer.begin(); it != m_buffer.end(); ++it) {
            if (it->timestamp >= targetTime) {
                T result = it->item;
                m_buffer.erase(m_buffer.begin(), it);
                return result;
            }
        }

        return std::nullopt;
    }

    // Check if buffer is ready
    bool IsReady(float playbackTime) const {
        if (m_buffer.empty()) return false;
        return m_buffer.front().timestamp <= playbackTime - m_targetDelay;
    }

    // Configuration
    void SetTargetDelay(float delay) { m_targetDelay = delay; }
    float GetTargetDelay() const { return m_targetDelay; }

    // Statistics
    size_t GetBufferSize() const { return m_buffer.size(); }
    float GetBufferTime() const {
        if (m_buffer.size() < 2) return 0.0f;
        return m_buffer.back().timestamp - m_buffer.front().timestamp;
    }

private:
    struct Entry {
        T item;
        float timestamp;
    };

    std::deque<Entry> m_buffer;
    float m_targetDelay;
    size_t m_maxSize;
};

// ============================================================================
// Prediction System (High-Level Interface)
// ============================================================================

class PredictionSystem {
public:
    PredictionSystem();
    ~PredictionSystem();

    // Initialize
    void Initialize(entt::registry* registry);
    void Shutdown();

    // Set as client or server
    void SetIsServer(bool isServer) { m_isServer = isServer; }
    bool IsServer() const { return m_isServer; }

    // Update
    void Update(float deltaTime);

    // Client methods
    void SetLocalPlayer(Entity entity);
    void ProcessLocalInput(const PlayerInputMessage& input, float deltaTime);
    void ReceiveServerState(uint32_t connectionId, const PlayerStateMessage& state);
    EntityState GetPredictedState() const;

    // Server methods
    void ProcessClientInput(uint32_t connectionId, const PlayerInputMessage& input);
    void RecordHitboxes(uint32_t tick);

    // Get subsystems
    ClientPrediction& GetClientPrediction() { return m_clientPrediction; }
    EntityInterpolation& GetInterpolation() { return m_interpolation; }
    LagCompensation& GetLagCompensation() { return m_lagCompensation; }
    NetworkClock& GetNetworkClock() { return m_networkClock; }

private:
    entt::registry* m_registry = nullptr;
    bool m_isServer = false;

    ClientPrediction m_clientPrediction;
    EntityInterpolation m_interpolation;
    LagCompensation m_lagCompensation;
    NetworkClock m_networkClock;

    float m_currentTime = 0.0f;
};

} // namespace Cortex::Network
