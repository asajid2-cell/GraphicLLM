// NetPrediction.cpp
// Client-side prediction and lag compensation implementation.

#include "NetPrediction.h"
#include <algorithm>
#include <cmath>
#include <chrono>

namespace Cortex::Network {

// ============================================================================
// EntityState Implementation
// ============================================================================

bool EntityState::operator==(const EntityState& other) const {
    const float epsilon = 0.001f;
    return glm::length(position - other.position) < epsilon &&
           glm::dot(rotation, other.rotation) > 0.999f &&
           glm::length(velocity - other.velocity) < epsilon;
}

EntityState EntityState::Lerp(const EntityState& a, const EntityState& b, float t) {
    EntityState result;
    result.position = glm::mix(a.position, b.position, t);
    result.rotation = glm::slerp(a.rotation, b.rotation, t);
    result.velocity = glm::mix(a.velocity, b.velocity, t);
    result.angularVelocity = glm::mix(a.angularVelocity, b.angularVelocity, t);
    return result;
}

// ============================================================================
// InputBuffer Implementation
// ============================================================================

InputBuffer::InputBuffer(size_t maxSize) : m_maxSize(maxSize) {}

void InputBuffer::AddInput(const BufferedInput& input) {
    m_inputs.push_back(input);

    // Remove old inputs
    while (m_inputs.size() > m_maxSize) {
        m_inputs.pop_front();
    }

    m_nextSequence = input.sequence + 1;
}

const BufferedInput* InputBuffer::GetInput(uint32_t sequence) const {
    for (const auto& input : m_inputs) {
        if (input.sequence == sequence) {
            return &input;
        }
    }
    return nullptr;
}

std::vector<const BufferedInput*> InputBuffer::GetInputsAfter(uint32_t sequence) const {
    std::vector<const BufferedInput*> result;
    for (const auto& input : m_inputs) {
        if (input.sequence > sequence) {
            result.push_back(&input);
        }
    }
    return result;
}

void InputBuffer::RemoveUpTo(uint32_t sequence) {
    while (!m_inputs.empty() && m_inputs.front().sequence <= sequence) {
        m_inputs.pop_front();
    }
}

uint32_t InputBuffer::GetLatestSequence() const {
    if (m_inputs.empty()) return 0;
    return m_inputs.back().sequence;
}

float InputBuffer::GetBufferTime() const {
    if (m_inputs.size() < 2) return 0.0f;

    float total = 0.0f;
    for (const auto& input : m_inputs) {
        total += input.deltaTime;
    }
    return total;
}

// ============================================================================
// ClientPrediction Implementation
// ============================================================================

ClientPrediction::ClientPrediction() {}

void ClientPrediction::Initialize(entt::registry* registry) {
    m_registry = registry;
}

void ClientPrediction::ProcessLocalInput(const PlayerInputMessage& input, float deltaTime) {
    if (!m_simulateCallback) return;

    // Store input in buffer
    BufferedInput buffered;
    buffered.sequence = m_inputBuffer.GetLatestSequence() + 1;
    buffered.serverTick = input.serverTick;
    buffered.deltaTime = deltaTime;
    buffered.input = input;
    buffered.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    // Predict new state
    m_predictedState = SimulateInput(m_predictedState, input, deltaTime);

    // Store result
    buffered.resultPosition = m_predictedState.position;
    buffered.resultVelocity = m_predictedState.velocity;
    buffered.resultRotation = m_predictedState.rotation;

    m_inputBuffer.AddInput(buffered);
}

void ClientPrediction::ReceiveServerState(const PlayerStateMessage& state) {
    m_serverState.position = state.position;
    m_serverState.rotation = state.rotation;
    m_serverState.velocity = state.velocity;

    // Check if we need to reconcile
    const BufferedInput* input = m_inputBuffer.GetInput(state.lastProcessedInput);
    if (input) {
        float error = glm::length(input->resultPosition - state.position);
        m_lastPredictionError = error;

        if (error > m_reconciliationThreshold) {
            m_needsReconciliation = true;
            Reconcile(state);
        }
    }

    // Remove acknowledged inputs
    m_inputBuffer.RemoveUpTo(state.lastProcessedInput);
    m_lastAcknowledgedInput = state.lastProcessedInput;
}

EntityState ClientPrediction::GetRenderState(float /*alpha*/) const {
    // Smooth between previous and current state
    if (m_smoothingProgress < 1.0f) {
        return EntityState::Lerp(m_smoothingStart, m_smoothingTarget, m_smoothingProgress);
    }
    return m_predictedState;
}

void ClientPrediction::Reconcile(const PlayerStateMessage& serverState) {
    // Start smoothing from current visual position
    m_smoothingStart = m_smoothedState;

    // Reset to server state
    m_predictedState.position = serverState.position;
    m_predictedState.rotation = serverState.rotation;
    m_predictedState.velocity = serverState.velocity;

    // Replay all unacknowledged inputs
    ReplayInputs(serverState.lastProcessedInput);

    // Set smoothing target to new predicted state
    m_smoothingTarget = m_predictedState;
    m_smoothingProgress = 0.0f;

    m_needsReconciliation = false;
}

void ClientPrediction::ReplayInputs(uint32_t fromSequence) {
    auto unackedInputs = m_inputBuffer.GetInputsAfter(fromSequence);

    for (const BufferedInput* input : unackedInputs) {
        m_predictedState = SimulateInput(m_predictedState, input->input, input->deltaTime);
    }
}

EntityState ClientPrediction::SimulateInput(const EntityState& state,
                                              const PlayerInputMessage& input, float dt) {
    if (m_simulateCallback) {
        return m_simulateCallback(m_localPlayer, state, input, dt);
    }
    return state;
}

// ============================================================================
// EntityInterpolation Implementation
// ============================================================================

EntityInterpolation::EntityInterpolation() {}

void EntityInterpolation::Initialize(entt::registry* registry) {
    m_registry = registry;
}

void EntityInterpolation::AddSnapshot(Entity entity, uint32_t tick, const EntityState& state) {
    auto& snapshots = m_snapshots[entity];

    SnapshotEntry entry;
    entry.tick = tick;
    entry.timestamp = m_currentTime;
    entry.state = state;

    snapshots.push_back(entry);

    // Limit buffer size
    while (snapshots.size() > m_maxSnapshots) {
        snapshots.pop_front();
    }
}

EntityState EntityInterpolation::GetInterpolatedState(Entity entity, float renderTime) const {
    auto it = m_snapshots.find(entity);
    if (it == m_snapshots.end() || it->second.empty()) {
        return EntityState{};
    }

    const auto& snapshots = it->second;
    float targetTime = renderTime - m_interpolationDelay;

    // Find snapshots to interpolate between
    const SnapshotEntry* before = nullptr;
    const SnapshotEntry* after = nullptr;

    for (size_t i = 0; i < snapshots.size(); ++i) {
        if (snapshots[i].timestamp > targetTime) {
            if (i > 0) {
                before = &snapshots[i - 1];
                after = &snapshots[i];
            }
            break;
        }
        before = &snapshots[i];
    }

    if (!before) {
        return EntityState{};
    }

    if (!after) {
        // Extrapolate
        float timeSince = targetTime - before->timestamp;
        if (timeSince > m_maxExtrapolationTime) {
            timeSince = m_maxExtrapolationTime;
        }

        EntityState result = before->state;
        result.position += result.velocity * timeSince;
        return result;
    }

    // Interpolate
    float t = (targetTime - before->timestamp) / (after->timestamp - before->timestamp);
    t = std::clamp(t, 0.0f, 1.0f);

    return EntityState::Lerp(before->state, after->state, t);
}

void EntityInterpolation::CleanupOldSnapshots(float currentTime) {
    m_currentTime = currentTime;
    float cutoff = currentTime - m_interpolationDelay * 2.0f;

    for (auto& [entity, snapshots] : m_snapshots) {
        while (!snapshots.empty() && snapshots.front().timestamp < cutoff) {
            snapshots.pop_front();
        }
    }
}

// ============================================================================
// LagCompensation Implementation
// ============================================================================

LagCompensation::LagCompensation() {}

void LagCompensation::Initialize(entt::registry* registry) {
    m_registry = registry;
}

void LagCompensation::RecordSnapshot(uint32_t tick) {
    m_currentTick = tick;

    // Would iterate over entities with hitbox components and record their positions
    // This is a stub that would be filled in with actual ECS iteration
}

std::optional<HitboxSnapshot> LagCompensation::GetHitboxAtTick(Entity entity, uint32_t tick) const {
    auto it = m_hitboxHistory.find(entity);
    if (it == m_hitboxHistory.end()) {
        return std::nullopt;
    }

    const auto& history = it->second;
    for (const auto& [t, snapshot] : history.snapshots) {
        if (t == tick) {
            return snapshot;
        }
    }

    // Interpolate between ticks if exact tick not found
    for (size_t i = 1; i < history.snapshots.size(); ++i) {
        if (history.snapshots[i].first > tick && history.snapshots[i - 1].first < tick) {
            const auto& a = history.snapshots[i - 1].second;
            const auto& b = history.snapshots[i].second;
            float t = static_cast<float>(tick - history.snapshots[i - 1].first) /
                      static_cast<float>(history.snapshots[i].first - history.snapshots[i - 1].first);

            HitboxSnapshot result;
            result.position = glm::mix(a.position, b.position, t);
            result.rotation = glm::slerp(a.rotation, b.rotation, t);
            result.halfExtents = a.halfExtents;
            result.radius = a.radius;
            result.height = a.height;
            return result;
        }
    }

    return std::nullopt;
}

void LagCompensation::BeginRewind(uint32_t tick) {
    if (m_isRewound) return;

    m_isRewound = true;
    m_rewindTick = tick;

    // Store original hitbox positions and move entities to historical positions
    // This would modify actual entity positions temporarily
}

void LagCompensation::EndRewind() {
    if (!m_isRewound) return;

    // Restore original hitbox positions
    m_originalHitboxes.clear();
    m_isRewound = false;
}

LagCompensation::RaycastResult LagCompensation::Raycast(const glm::vec3& origin,
                                                          const glm::vec3& direction,
                                                          float maxDistance,
                                                          uint32_t tick) const {
    RaycastResult result;
    result.hit = false;
    result.distance = maxDistance;

    // Check each entity's hitbox at the specified tick
    for (const auto& [entity, history] : m_hitboxHistory) {
        auto hitbox = GetHitboxAtTick(entity, tick);
        if (!hitbox) continue;

        // Simple sphere intersection test
        glm::vec3 toSphere = hitbox->position - origin;
        float projLen = glm::dot(toSphere, direction);

        if (projLen < 0) continue;  // Behind ray origin

        glm::vec3 closest = origin + direction * projLen;
        float dist = glm::length(hitbox->position - closest);

        if (dist < hitbox->radius) {
            // Hit
            float offset = std::sqrt(hitbox->radius * hitbox->radius - dist * dist);
            float hitDist = projLen - offset;

            if (hitDist > 0 && hitDist < result.distance) {
                result.hit = true;
                result.entity = entity;
                result.distance = hitDist;
                result.hitPoint = origin + direction * hitDist;
                result.hitNormal = glm::normalize(result.hitPoint - hitbox->position);
            }
        }
    }

    return result;
}

void LagCompensation::CleanupOldSnapshots(uint32_t currentTick) {
    uint32_t maxHistory = static_cast<uint32_t>(m_maxRewindTime * m_tickRate);
    uint32_t cutoffTick = currentTick > maxHistory ? currentTick - maxHistory : 0;

    for (auto& [entity, history] : m_hitboxHistory) {
        while (!history.snapshots.empty() && history.snapshots.front().first < cutoffTick) {
            history.snapshots.pop_front();
        }
    }
}

// ============================================================================
// NetworkClock Implementation
// ============================================================================

NetworkClock::NetworkClock() {}

void NetworkClock::ProcessTimeSync(uint64_t serverTime, uint64_t clientSendTime,
                                     uint64_t clientReceiveTime) {
    // Calculate RTT
    uint64_t rttMicros = clientReceiveTime - clientSendTime;
    float rttMs = static_cast<float>(rttMicros) / 1000.0f;

    // Smooth RTT
    if (m_rtt == 0.0f) {
        m_rtt = rttMs;
    } else {
        m_rtt = m_rtt * 0.9f + rttMs * 0.1f;
    }

    // Calculate clock offset
    // Server time at receive = serverTime + RTT/2
    uint64_t estimatedServerTime = serverTime + rttMicros / 2;
    int64_t offset = static_cast<int64_t>(estimatedServerTime) - static_cast<int64_t>(clientReceiveTime);

    // Add sample
    m_offsetSamples.push_back(offset);
    if (m_offsetSamples.size() > m_maxSamples) {
        m_offsetSamples.pop_front();
    }

    // Use median offset (robust against outliers)
    std::vector<int64_t> sorted(m_offsetSamples.begin(), m_offsetSamples.end());
    std::sort(sorted.begin(), sorted.end());
    m_clockOffset = sorted[sorted.size() / 2];
}

uint64_t NetworkClock::GetServerTime() const {
    auto now = std::chrono::steady_clock::now();
    uint64_t localTime = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
    return LocalToServerTime(localTime);
}

uint64_t NetworkClock::LocalToServerTime(uint64_t localTime) const {
    return static_cast<uint64_t>(static_cast<int64_t>(localTime) + m_clockOffset);
}

uint64_t NetworkClock::ServerToLocalTime(uint64_t serverTime) const {
    return static_cast<uint64_t>(static_cast<int64_t>(serverTime) - m_clockOffset);
}

uint32_t NetworkClock::GetEstimatedServerTick() const {
    uint64_t serverTime = GetServerTime();
    double tickDuration = 1000000.0 / static_cast<double>(m_tickRate);  // Microseconds per tick
    return static_cast<uint32_t>(static_cast<double>(serverTime) / tickDuration);
}

float NetworkClock::GetTickProgress() const {
    uint64_t serverTime = GetServerTime();
    double tickDuration = 1000000.0 / static_cast<double>(m_tickRate);
    double tickTime = std::fmod(static_cast<double>(serverTime), tickDuration);
    return static_cast<float>(tickTime / tickDuration);
}

// ============================================================================
// PredictionSystem Implementation
// ============================================================================

PredictionSystem::PredictionSystem() {}

PredictionSystem::~PredictionSystem() {
    Shutdown();
}

void PredictionSystem::Initialize(entt::registry* registry) {
    m_registry = registry;
    m_clientPrediction.Initialize(registry);
    m_interpolation.Initialize(registry);
    m_lagCompensation.Initialize(registry);
}

void PredictionSystem::Shutdown() {
    m_registry = nullptr;
}

void PredictionSystem::Update(float deltaTime) {
    m_currentTime += deltaTime;

    if (m_isServer) {
        // Server: record hitbox snapshots each tick
        // m_lagCompensation.RecordSnapshot(currentTick);
    } else {
        // Client: update interpolation
        m_interpolation.CleanupOldSnapshots(m_currentTime);
    }
}

void PredictionSystem::SetLocalPlayer(Entity entity) {
    m_clientPrediction.SetLocalPlayer(entity);
}

void PredictionSystem::ProcessLocalInput(const PlayerInputMessage& input, float deltaTime) {
    m_clientPrediction.ProcessLocalInput(input, deltaTime);
}

void PredictionSystem::ReceiveServerState(uint32_t /*connectionId*/, const PlayerStateMessage& state) {
    m_clientPrediction.ReceiveServerState(state);
}

EntityState PredictionSystem::GetPredictedState() const {
    return m_clientPrediction.GetPredictedState();
}

void PredictionSystem::ProcessClientInput(uint32_t /*connectionId*/, const PlayerInputMessage& /*input*/) {
    // Server-side input processing
    // Would apply input to player entity
}

void PredictionSystem::RecordHitboxes(uint32_t tick) {
    m_lagCompensation.RecordSnapshot(tick);
}

} // namespace Cortex::Network
