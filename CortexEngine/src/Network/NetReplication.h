#pragma once

// NetReplication.h
// Entity replication system for networked games.
// Handles spawning, state synchronization, and ownership.

#include "NetPacket.h"
#include "NetMessages.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <memory>
#include <any>

// Forward declare ECS types
namespace entt { class registry; }
using Entity = uint32_t;

namespace Cortex::Network {

// Forward declarations
class NetworkTransport;
class ReplicationManager;

// ============================================================================
// Network Identity Component
// ============================================================================

struct NetIdentityComponent {
    uint32_t networkId = 0;         // Unique network ID
    uint32_t ownerId = 0;           // Connection ID of owner (0 = server)
    uint32_t prefabId = 0;          // Prefab type for spawning
    uint8_t priority = 128;         // Replication priority (higher = more frequent)
    bool isServerOnly = false;      // Only exists on server
    bool isLocalPlayer = false;     // Is local player's entity

    // Replication state
    uint32_t lastReplicatedTick = 0;
    uint32_t lastModifiedTick = 0;
    bool isDirty = false;

    // Relevancy
    float relevancyDistance = 0.0f;  // 0 = always relevant
    std::unordered_set<uint32_t> alwaysRelevantTo;  // Connection IDs
};

// ============================================================================
// Component Replication
// ============================================================================

// Base interface for component replicators
class IComponentReplicator {
public:
    virtual ~IComponentReplicator() = default;

    // Get component type ID
    virtual uint32_t GetComponentTypeId() const = 0;

    // Check if component exists on entity
    virtual bool HasComponent(entt::registry& reg, Entity entity) const = 0;

    // Serialize component
    virtual void Serialize(entt::registry& reg, Entity entity, BitWriter& writer) const = 0;

    // Deserialize component
    virtual void Deserialize(entt::registry& reg, Entity entity, BitReader& reader) const = 0;

    // Check if component changed
    virtual bool IsDirty(entt::registry& reg, Entity entity) const = 0;

    // Clear dirty flag
    virtual void ClearDirty(entt::registry& reg, Entity entity) const = 0;

    // Delta compression
    virtual void SerializeDelta(entt::registry& reg, Entity entity,
                                 const std::vector<uint8_t>& baseline, BitWriter& writer) const;
    virtual void DeserializeDelta(entt::registry& reg, Entity entity,
                                   const std::vector<uint8_t>& baseline, BitReader& reader) const;
};

// Template implementation for specific components
template<typename T>
class ComponentReplicator : public IComponentReplicator {
public:
    using SerializeFunc = std::function<void(const T&, BitWriter&)>;
    using DeserializeFunc = std::function<void(T&, BitReader&)>;
    using IsDirtyFunc = std::function<bool(const T&)>;
    using ClearDirtyFunc = std::function<void(T&)>;

    ComponentReplicator(uint32_t typeId,
                         SerializeFunc serialize,
                         DeserializeFunc deserialize,
                         IsDirtyFunc isDirty = nullptr,
                         ClearDirtyFunc clearDirty = nullptr)
        : m_typeId(typeId)
        , m_serialize(serialize)
        , m_deserialize(deserialize)
        , m_isDirty(isDirty)
        , m_clearDirty(clearDirty) {}

    uint32_t GetComponentTypeId() const override { return m_typeId; }

    bool HasComponent(entt::registry& reg, Entity entity) const override;
    void Serialize(entt::registry& reg, Entity entity, BitWriter& writer) const override;
    void Deserialize(entt::registry& reg, Entity entity, BitReader& reader) const override;
    bool IsDirty(entt::registry& reg, Entity entity) const override;
    void ClearDirty(entt::registry& reg, Entity entity) const override;

private:
    uint32_t m_typeId;
    SerializeFunc m_serialize;
    DeserializeFunc m_deserialize;
    IsDirtyFunc m_isDirty;
    ClearDirtyFunc m_clearDirty;
};

// ============================================================================
// Entity Relevancy
// ============================================================================

class RelevancyManager {
public:
    RelevancyManager();
    ~RelevancyManager() = default;

    // Set player position for relevancy calculations
    void SetPlayerPosition(uint32_t connectionId, const glm::vec3& position);

    // Check if entity is relevant to connection
    bool IsRelevant(uint32_t connectionId, Entity entity, entt::registry& reg) const;

    // Get all relevant entities for connection
    std::vector<Entity> GetRelevantEntities(uint32_t connectionId, entt::registry& reg) const;

    // Configuration
    void SetDefaultRelevancyDistance(float distance) { m_defaultRelevancyDistance = distance; }
    float GetDefaultRelevancyDistance() const { return m_defaultRelevancyDistance; }

private:
    std::unordered_map<uint32_t, glm::vec3> m_playerPositions;
    float m_defaultRelevancyDistance = 500.0f;
};

// ============================================================================
// Replication Priority
// ============================================================================

class PriorityManager {
public:
    PriorityManager();
    ~PriorityManager() = default;

    // Calculate priority for entity/connection pair
    float CalculatePriority(uint32_t connectionId, Entity entity,
                             entt::registry& reg, float deltaTime);

    // Get entities sorted by priority
    std::vector<Entity> GetPrioritizedEntities(uint32_t connectionId,
                                                 const std::vector<Entity>& entities,
                                                 entt::registry& reg,
                                                 float deltaTime);

    // Mark entity as replicated (reset accumulator)
    void MarkReplicated(uint32_t connectionId, Entity entity);

    // Configuration
    void SetBasePriority(float priority) { m_basePriority = priority; }
    void SetDistanceFalloff(float falloff) { m_distanceFalloff = falloff; }

private:
    struct PriorityState {
        float accumulator = 0.0f;
        uint32_t lastReplicatedTick = 0;
    };

    std::unordered_map<uint64_t, PriorityState> m_priorityStates;  // (connId << 32 | entityId)
    float m_basePriority = 1.0f;
    float m_distanceFalloff = 0.001f;

    uint64_t MakeKey(uint32_t connectionId, Entity entity) const {
        return (static_cast<uint64_t>(connectionId) << 32) | entity;
    }
};

// ============================================================================
// Replication Manager
// ============================================================================

class ReplicationManager {
public:
    ReplicationManager();
    ~ReplicationManager();

    // Initialize with transport and registry
    void Initialize(NetworkTransport* transport, entt::registry* registry);
    void Shutdown();

    // Server/client mode
    void SetServerMode(bool isServer) { m_isServer = isServer; }
    bool IsServer() const { return m_isServer; }

    // Register component replicators
    void RegisterReplicator(std::unique_ptr<IComponentReplicator> replicator);

    template<typename T>
    void RegisterComponent(uint32_t typeId,
                            typename ComponentReplicator<T>::SerializeFunc serialize,
                            typename ComponentReplicator<T>::DeserializeFunc deserialize) {
        RegisterReplicator(std::make_unique<ComponentReplicator<T>>(typeId, serialize, deserialize));
    }

    // Entity management (server)
    uint32_t SpawnNetworkEntity(uint32_t prefabId, const glm::vec3& position,
                                 const glm::quat& rotation = glm::quat(1, 0, 0, 0),
                                 uint32_t ownerId = 0);
    void DestroyNetworkEntity(uint32_t networkId, uint8_t reason = 0);
    void TransferOwnership(uint32_t networkId, uint32_t newOwnerId);

    // Entity lookup
    Entity GetEntityByNetworkId(uint32_t networkId) const;
    uint32_t GetNetworkId(Entity entity) const;
    bool HasNetworkId(Entity entity) const;

    // Update (call every frame)
    void Update(float deltaTime);

    // Server: replicate state to clients
    void ReplicateToClients(float deltaTime);

    // Client: process incoming state
    void ProcessServerState(const std::vector<uint8_t>& data);

    // Ownership
    bool HasAuthority(Entity entity) const;
    uint32_t GetOwner(Entity entity) const;
    bool IsLocalPlayer(Entity entity) const;

    // Callbacks
    using SpawnCallback = std::function<Entity(uint32_t prefabId, const SpawnEntityMessage&)>;
    using DestroyCallback = std::function<void(Entity entity, uint8_t reason)>;

    void SetSpawnCallback(SpawnCallback callback) { m_spawnCallback = callback; }
    void SetDestroyCallback(DestroyCallback callback) { m_destroyCallback = callback; }

    // Statistics
    uint32_t GetReplicatedEntityCount() const;
    float GetBandwidthUsage() const { return m_bandwidthUsage; }

    // Tick management
    uint32_t GetCurrentTick() const { return m_currentTick; }
    void SetTickRate(float tickRate) { m_tickRate = tickRate; }

private:
    // Serialization
    std::vector<uint8_t> SerializeEntity(Entity entity);
    void DeserializeEntity(Entity entity, const std::vector<uint8_t>& data);

    // Network ID generation
    uint32_t GenerateNetworkId();

    // Internal update
    void UpdateDirtyFlags();
    void SendSpawnMessages(uint32_t connectionId);
    void SendDestroyMessages(uint32_t connectionId);
    void SendStateUpdates(uint32_t connectionId, float deltaTime);

    // Handle incoming messages
    void HandleSpawnEntity(uint32_t connectionId, const SpawnEntityMessage& msg);
    void HandleDestroyEntity(uint32_t connectionId, const DestroyEntityMessage& msg);
    void HandleEntityState(uint32_t connectionId, const EntityStateMessage& msg);

    NetworkTransport* m_transport = nullptr;
    entt::registry* m_registry = nullptr;
    bool m_isServer = false;

    // Network ID mapping
    std::unordered_map<uint32_t, Entity> m_networkIdToEntity;
    std::unordered_map<Entity, uint32_t> m_entityToNetworkId;
    uint32_t m_nextNetworkId = 1;

    // Per-connection state
    struct ConnectionReplicationState {
        std::unordered_set<uint32_t> knownEntities;     // Entities this connection knows about
        std::unordered_set<uint32_t> pendingSpawns;     // Entities to spawn
        std::unordered_set<uint32_t> pendingDestroys;   // Entities to destroy
        std::unordered_map<uint32_t, std::vector<uint8_t>> baselines;  // Last sent state
    };
    std::unordered_map<uint32_t, ConnectionReplicationState> m_connectionStates;

    // Replicators
    std::unordered_map<uint32_t, std::unique_ptr<IComponentReplicator>> m_replicators;

    // Subsystems
    RelevancyManager m_relevancy;
    PriorityManager m_priority;

    // Callbacks
    SpawnCallback m_spawnCallback;
    DestroyCallback m_destroyCallback;

    // Timing
    uint32_t m_currentTick = 0;
    float m_tickRate = 60.0f;
    float m_tickAccumulator = 0.0f;

    // Statistics
    float m_bandwidthUsage = 0.0f;

    // Configuration
    uint32_t m_maxUpdatesPerFrame = 64;
    float m_snapshotInterval = 0.05f;  // 20 Hz full snapshots
};

// ============================================================================
// Interest Management
// ============================================================================

class InterestManager {
public:
    InterestManager();
    ~InterestManager() = default;

    // Define interest areas
    void DefineArea(const std::string& name, const glm::vec3& center, float radius);
    void RemoveArea(const std::string& name);

    // Assign entity to area
    void AssignEntityToArea(Entity entity, const std::string& areaName);
    void RemoveEntityFromArea(Entity entity, const std::string& areaName);

    // Subscribe connection to area
    void SubscribeToArea(uint32_t connectionId, const std::string& areaName);
    void UnsubscribeFromArea(uint32_t connectionId, const std::string& areaName);

    // Query
    std::vector<Entity> GetEntitiesInArea(const std::string& areaName) const;
    std::vector<uint32_t> GetSubscribersToArea(const std::string& areaName) const;
    bool ShouldReplicate(uint32_t connectionId, Entity entity) const;

private:
    struct InterestArea {
        glm::vec3 center;
        float radius;
        std::unordered_set<Entity> entities;
        std::unordered_set<uint32_t> subscribers;
    };

    std::unordered_map<std::string, InterestArea> m_areas;
    std::unordered_map<Entity, std::unordered_set<std::string>> m_entityAreas;
};

// ============================================================================
// Network Object Pool
// ============================================================================

class NetworkObjectPool {
public:
    using FactoryFunc = std::function<Entity(entt::registry&, const SpawnEntityMessage&)>;

    NetworkObjectPool();
    ~NetworkObjectPool();

    // Register factory for prefab type
    void RegisterFactory(uint32_t prefabId, FactoryFunc factory);

    // Spawn object
    Entity Spawn(entt::registry& reg, const SpawnEntityMessage& msg);

    // Despawn object (returns to pool if poolable)
    void Despawn(entt::registry& reg, Entity entity);

    // Configuration
    void SetPoolSize(uint32_t prefabId, size_t size);
    void PrewarmPool(entt::registry& reg, uint32_t prefabId, size_t count);

private:
    std::unordered_map<uint32_t, FactoryFunc> m_factories;
    std::unordered_map<uint32_t, std::vector<Entity>> m_pools;
    std::unordered_map<uint32_t, size_t> m_poolSizes;
};

// ============================================================================
// Sync Vars (Automatic Property Synchronization)
// ============================================================================

template<typename T>
class SyncVar {
public:
    SyncVar() : m_value(), m_isDirty(false) {}
    SyncVar(const T& value) : m_value(value), m_isDirty(true) {}

    // Get value
    const T& Get() const { return m_value; }
    operator const T&() const { return m_value; }

    // Set value (marks dirty)
    void Set(const T& value) {
        if (m_value != value) {
            m_value = value;
            m_isDirty = true;
        }
    }

    SyncVar& operator=(const T& value) {
        Set(value);
        return *this;
    }

    // Dirty flag
    bool IsDirty() const { return m_isDirty; }
    void ClearDirty() { m_isDirty = false; }

    // Serialization
    void Serialize(BitWriter& writer) const {
        NetSerialize<T>::Write(writer, m_value);
    }

    void Deserialize(BitReader& reader) {
        m_value = NetSerialize<T>::Read(reader);
        m_isDirty = false;
    }

private:
    T m_value;
    bool m_isDirty;
};

// ============================================================================
// Network Transform Component
// ============================================================================

struct NetworkTransformComponent {
    SyncVar<glm::vec3> position;
    SyncVar<glm::quat> rotation;
    SyncVar<glm::vec3> scale{glm::vec3(1.0f)};

    // Interpolation settings
    bool interpolate = true;
    float interpolationSpeed = 10.0f;

    // Extrapolation
    glm::vec3 velocity;
    glm::vec3 angularVelocity;

    // Smoothing state
    glm::vec3 visualPosition;
    glm::quat visualRotation;

    bool IsDirty() const {
        return position.IsDirty() || rotation.IsDirty() || scale.IsDirty();
    }

    void ClearDirty() {
        position.ClearDirty();
        rotation.ClearDirty();
        scale.ClearDirty();
    }
};

} // namespace Cortex::Network
