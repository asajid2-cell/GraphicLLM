#pragma once

// NetMessages.h
// Network message types and RPC (Remote Procedure Call) system.
// Defines game-level network communication protocol.

#include "NetPacket.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <memory>
#include <any>

namespace Cortex::Network {

// Forward declarations
class NetworkTransport;

// ============================================================================
// Message Types
// ============================================================================

enum class NetMessageType : uint16_t {
    // System messages (0-99)
    Invalid = 0,
    Heartbeat = 1,
    TimeSync = 2,
    ServerInfo = 3,
    ClientInfo = 4,

    // Authentication (100-199)
    AuthRequest = 100,
    AuthResponse = 101,
    AuthChallenge = 102,

    // Entity management (200-299)
    SpawnEntity = 200,
    DestroyEntity = 201,
    EntityState = 202,
    EntityOwnership = 203,
    EntityRPC = 204,

    // Player (300-399)
    PlayerInput = 300,
    PlayerState = 301,
    PlayerSpawn = 302,
    PlayerDeath = 303,
    PlayerRespawn = 304,

    // World (400-499)
    WorldState = 400,
    ChunkData = 401,
    ChunkUpdate = 402,
    WeatherSync = 403,
    TimeOfDaySync = 404,

    // Game events (500-599)
    GameEvent = 500,
    ChatMessage = 501,
    VoiceData = 502,
    ScoreUpdate = 503,
    MatchState = 504,

    // Custom game messages (1000+)
    CustomStart = 1000
};

// ============================================================================
// Base Message
// ============================================================================

struct NetMessage {
    virtual ~NetMessage() = default;
    virtual NetMessageType GetType() const = 0;
    virtual void Serialize(BitWriter& writer) const = 0;
    virtual void Deserialize(BitReader& reader) = 0;

    // Timestamp when message was created
    uint64_t timestamp = 0;
};

// ============================================================================
// System Messages
// ============================================================================

struct HeartbeatMessage : NetMessage {
    uint32_t sequence = 0;
    uint64_t clientTime = 0;

    NetMessageType GetType() const override { return NetMessageType::Heartbeat; }

    void Serialize(BitWriter& writer) const override {
        writer.WriteUInt32(sequence);
        writer.WriteUInt64(clientTime);
    }

    void Deserialize(BitReader& reader) override {
        sequence = reader.ReadUInt32();
        clientTime = reader.ReadUInt64();
    }
};

struct TimeSyncMessage : NetMessage {
    uint64_t serverTime = 0;
    uint64_t clientTime = 0;  // Echoed back

    NetMessageType GetType() const override { return NetMessageType::TimeSync; }

    void Serialize(BitWriter& writer) const override {
        writer.WriteUInt64(serverTime);
        writer.WriteUInt64(clientTime);
    }

    void Deserialize(BitReader& reader) override {
        serverTime = reader.ReadUInt64();
        clientTime = reader.ReadUInt64();
    }
};

struct ServerInfoMessage : NetMessage {
    std::string serverName;
    std::string mapName;
    std::string gameMode;
    uint32_t maxPlayers = 0;
    uint32_t currentPlayers = 0;
    uint32_t tickRate = 60;
    uint32_t protocolVersion = 1;

    NetMessageType GetType() const override { return NetMessageType::ServerInfo; }

    void Serialize(BitWriter& writer) const override {
        writer.WriteString(serverName);
        writer.WriteString(mapName);
        writer.WriteString(gameMode);
        writer.WriteUInt32(maxPlayers);
        writer.WriteUInt32(currentPlayers);
        writer.WriteUInt32(tickRate);
        writer.WriteUInt32(protocolVersion);
    }

    void Deserialize(BitReader& reader) override {
        serverName = reader.ReadString();
        mapName = reader.ReadString();
        gameMode = reader.ReadString();
        maxPlayers = reader.ReadUInt32();
        currentPlayers = reader.ReadUInt32();
        tickRate = reader.ReadUInt32();
        protocolVersion = reader.ReadUInt32();
    }
};

// ============================================================================
// Entity Messages
// ============================================================================

struct SpawnEntityMessage : NetMessage {
    uint32_t networkId = 0;
    uint32_t entityType = 0;
    uint32_t ownerId = 0;       // Connection ID of owner (0 = server)
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 scale = glm::vec3(1.0f);
    std::vector<uint8_t> initialState;  // Serialized initial component data

    NetMessageType GetType() const override { return NetMessageType::SpawnEntity; }

    void Serialize(BitWriter& writer) const override {
        writer.WriteUInt32(networkId);
        writer.WriteUInt32(entityType);
        writer.WriteUInt32(ownerId);
        writer.WriteCompressedPosition(position);
        writer.WriteCompressedRotation(rotation);
        writer.WriteVec3(scale);
        writer.WriteVarUInt(initialState.size());
        writer.WriteBytes(initialState);
    }

    void Deserialize(BitReader& reader) override {
        networkId = reader.ReadUInt32();
        entityType = reader.ReadUInt32();
        ownerId = reader.ReadUInt32();
        position = reader.ReadCompressedPosition();
        rotation = reader.ReadCompressedRotation();
        scale = reader.ReadVec3();
        size_t stateSize = static_cast<size_t>(reader.ReadVarUInt());
        initialState = reader.ReadBytes(stateSize);
    }
};

struct DestroyEntityMessage : NetMessage {
    uint32_t networkId = 0;
    uint8_t reason = 0;  // 0=normal, 1=death, 2=despawn

    NetMessageType GetType() const override { return NetMessageType::DestroyEntity; }

    void Serialize(BitWriter& writer) const override {
        writer.WriteUInt32(networkId);
        writer.WriteUInt8(reason);
    }

    void Deserialize(BitReader& reader) override {
        networkId = reader.ReadUInt32();
        reason = reader.ReadUInt8();
    }
};

struct EntityStateMessage : NetMessage {
    uint32_t networkId = 0;
    uint32_t tick = 0;          // Server tick for this state
    uint8_t flags = 0;          // Which components are included

    // Transform (if flag set)
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 velocity;
    glm::vec3 angularVelocity;

    // Additional state data
    std::vector<uint8_t> componentData;

    enum Flags : uint8_t {
        HasPosition = 1 << 0,
        HasRotation = 1 << 1,
        HasVelocity = 1 << 2,
        HasAngularVelocity = 1 << 3,
        HasComponents = 1 << 4,
        IsDelta = 1 << 5        // Data is delta compressed
    };

    NetMessageType GetType() const override { return NetMessageType::EntityState; }

    void Serialize(BitWriter& writer) const override {
        writer.WriteUInt32(networkId);
        writer.WriteUInt32(tick);
        writer.WriteUInt8(flags);

        if (flags & HasPosition) {
            writer.WriteCompressedPosition(position);
        }
        if (flags & HasRotation) {
            writer.WriteCompressedRotation(rotation);
        }
        if (flags & HasVelocity) {
            writer.WriteVec3(velocity);
        }
        if (flags & HasAngularVelocity) {
            writer.WriteVec3(angularVelocity);
        }
        if (flags & HasComponents) {
            writer.WriteVarUInt(componentData.size());
            writer.WriteBytes(componentData);
        }
    }

    void Deserialize(BitReader& reader) override {
        networkId = reader.ReadUInt32();
        tick = reader.ReadUInt32();
        flags = reader.ReadUInt8();

        if (flags & HasPosition) {
            position = reader.ReadCompressedPosition();
        }
        if (flags & HasRotation) {
            rotation = reader.ReadCompressedRotation();
        }
        if (flags & HasVelocity) {
            velocity = reader.ReadVec3();
        }
        if (flags & HasAngularVelocity) {
            angularVelocity = reader.ReadVec3();
        }
        if (flags & HasComponents) {
            size_t size = static_cast<size_t>(reader.ReadVarUInt());
            componentData = reader.ReadBytes(size);
        }
    }
};

// ============================================================================
// Player Messages
// ============================================================================

struct PlayerInputMessage : NetMessage {
    uint32_t inputSequence = 0;     // Client input sequence number
    uint32_t serverTick = 0;        // Last acknowledged server tick
    float deltaTime = 0.0f;

    // Input state
    glm::vec2 moveInput;            // WASD/stick
    glm::vec2 lookDelta;            // Mouse/stick look
    uint32_t buttonMask = 0;        // Button press bits

    // Button flags
    enum Buttons : uint32_t {
        Jump = 1 << 0,
        Crouch = 1 << 1,
        Sprint = 1 << 2,
        Fire = 1 << 3,
        AltFire = 1 << 4,
        Reload = 1 << 5,
        Interact = 1 << 6,
        Use = 1 << 7
    };

    NetMessageType GetType() const override { return NetMessageType::PlayerInput; }

    void Serialize(BitWriter& writer) const override {
        writer.WriteUInt32(inputSequence);
        writer.WriteUInt32(serverTick);
        writer.WriteFloat(deltaTime);
        writer.WriteSignedNormalizedFloat(moveInput.x, 10);
        writer.WriteSignedNormalizedFloat(moveInput.y, 10);
        writer.WriteFloat(lookDelta.x);
        writer.WriteFloat(lookDelta.y);
        writer.WriteUInt32(buttonMask);
    }

    void Deserialize(BitReader& reader) override {
        inputSequence = reader.ReadUInt32();
        serverTick = reader.ReadUInt32();
        deltaTime = reader.ReadFloat();
        moveInput.x = reader.ReadSignedNormalizedFloat(10);
        moveInput.y = reader.ReadSignedNormalizedFloat(10);
        lookDelta.x = reader.ReadFloat();
        lookDelta.y = reader.ReadFloat();
        buttonMask = reader.ReadUInt32();
    }
};

struct PlayerStateMessage : NetMessage {
    uint32_t playerId = 0;
    uint32_t serverTick = 0;
    uint32_t lastProcessedInput = 0;    // For client reconciliation

    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 velocity;

    // Player stats
    float health = 100.0f;
    float stamina = 100.0f;
    uint8_t state = 0;  // idle, walking, running, jumping, etc.

    NetMessageType GetType() const override { return NetMessageType::PlayerState; }

    void Serialize(BitWriter& writer) const override {
        writer.WriteUInt32(playerId);
        writer.WriteUInt32(serverTick);
        writer.WriteUInt32(lastProcessedInput);
        writer.WriteCompressedPosition(position);
        writer.WriteCompressedRotation(rotation);
        writer.WriteVec3(velocity);
        writer.WriteCompressedFloat(health, 0.0f, 100.0f, 8);
        writer.WriteCompressedFloat(stamina, 0.0f, 100.0f, 8);
        writer.WriteUInt8(state);
    }

    void Deserialize(BitReader& reader) override {
        playerId = reader.ReadUInt32();
        serverTick = reader.ReadUInt32();
        lastProcessedInput = reader.ReadUInt32();
        position = reader.ReadCompressedPosition();
        rotation = reader.ReadCompressedRotation();
        velocity = reader.ReadVec3();
        health = reader.ReadCompressedFloat(0.0f, 100.0f, 8);
        stamina = reader.ReadCompressedFloat(0.0f, 100.0f, 8);
        state = reader.ReadUInt8();
    }
};

// ============================================================================
// Chat Message
// ============================================================================

struct ChatMessageNet : NetMessage {
    uint32_t senderId = 0;
    std::string senderName;
    std::string message;
    uint8_t channel = 0;    // 0=all, 1=team, 2=whisper

    NetMessageType GetType() const override { return NetMessageType::ChatMessage; }

    void Serialize(BitWriter& writer) const override {
        writer.WriteUInt32(senderId);
        writer.WriteString(senderName);
        writer.WriteString(message);
        writer.WriteUInt8(channel);
    }

    void Deserialize(BitReader& reader) override {
        senderId = reader.ReadUInt32();
        senderName = reader.ReadString();
        message = reader.ReadString();
        channel = reader.ReadUInt8();
    }
};

// ============================================================================
// World State Message
// ============================================================================

struct WorldStateMessage : NetMessage {
    uint32_t serverTick = 0;
    float timeOfDay = 0.0f;
    uint8_t weatherType = 0;
    float weatherIntensity = 0.0f;

    NetMessageType GetType() const override { return NetMessageType::WorldState; }

    void Serialize(BitWriter& writer) const override {
        writer.WriteUInt32(serverTick);
        writer.WriteNormalizedFloat(timeOfDay / 24.0f, 12);
        writer.WriteUInt8(weatherType);
        writer.WriteNormalizedFloat(weatherIntensity, 8);
    }

    void Deserialize(BitReader& reader) override {
        serverTick = reader.ReadUInt32();
        timeOfDay = reader.ReadNormalizedFloat(12) * 24.0f;
        weatherType = reader.ReadUInt8();
        weatherIntensity = reader.ReadNormalizedFloat(8);
    }
};

// ============================================================================
// RPC (Remote Procedure Call) System
// ============================================================================

enum class RPCTarget : uint8_t {
    Server,             // Client -> Server
    OwningClient,       // Server -> Owner
    AllClients,         // Server -> All
    AllClientsExceptOwner
};

struct EntityRPCMessage : NetMessage {
    uint32_t networkId = 0;
    uint16_t rpcId = 0;         // RPC function ID
    RPCTarget target = RPCTarget::Server;
    std::vector<uint8_t> parameters;

    NetMessageType GetType() const override { return NetMessageType::EntityRPC; }

    void Serialize(BitWriter& writer) const override {
        writer.WriteUInt32(networkId);
        writer.WriteUInt16(rpcId);
        writer.WriteUInt8(static_cast<uint8_t>(target));
        writer.WriteVarUInt(parameters.size());
        writer.WriteBytes(parameters);
    }

    void Deserialize(BitReader& reader) override {
        networkId = reader.ReadUInt32();
        rpcId = reader.ReadUInt16();
        target = static_cast<RPCTarget>(reader.ReadUInt8());
        size_t size = static_cast<size_t>(reader.ReadVarUInt());
        parameters = reader.ReadBytes(size);
    }
};

// ============================================================================
// Message Factory
// ============================================================================

class MessageFactory {
public:
    using CreateFunc = std::function<std::unique_ptr<NetMessage>()>;

    static MessageFactory& Instance();

    // Register message type
    template<typename T>
    void Register() {
        T temp;
        m_creators[static_cast<uint16_t>(temp.GetType())] = []() {
            return std::make_unique<T>();
        };
    }

    // Create message by type
    std::unique_ptr<NetMessage> Create(NetMessageType type);
    std::unique_ptr<NetMessage> Create(uint16_t typeId);

    // Serialize message
    static std::vector<uint8_t> Serialize(const NetMessage& message);

    // Deserialize message
    static std::unique_ptr<NetMessage> Deserialize(const std::vector<uint8_t>& data);
    static std::unique_ptr<NetMessage> Deserialize(const uint8_t* data, size_t size);

private:
    MessageFactory();
    std::unordered_map<uint16_t, CreateFunc> m_creators;
};

// ============================================================================
// Message Handler
// ============================================================================

class MessageHandler {
public:
    using HandlerFunc = std::function<void(uint32_t connectionId, const NetMessage& message)>;

    // Register handler for message type
    template<typename T>
    void Register(std::function<void(uint32_t, const T&)> handler) {
        T temp;
        m_handlers[static_cast<uint16_t>(temp.GetType())] = [handler](uint32_t connId, const NetMessage& msg) {
            handler(connId, static_cast<const T&>(msg));
        };
    }

    // Handle incoming message
    void Handle(uint32_t connectionId, const NetMessage& message);
    void Handle(uint32_t connectionId, const std::vector<uint8_t>& data);

    // Check if handler exists
    bool HasHandler(NetMessageType type) const;

private:
    std::unordered_map<uint16_t, HandlerFunc> m_handlers;
};

// ============================================================================
// Snapshot System (for entity state synchronization)
// ============================================================================

struct EntitySnapshot {
    uint32_t networkId;
    uint32_t tick;
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 velocity;
    std::vector<uint8_t> componentData;
};

struct WorldSnapshot {
    uint32_t tick = 0;
    uint64_t timestamp = 0;
    std::vector<EntitySnapshot> entities;

    void Serialize(BitWriter& writer) const;
    void Deserialize(BitReader& reader);
};

class SnapshotBuffer {
public:
    SnapshotBuffer(size_t maxSnapshots = 64);

    // Add snapshot
    void AddSnapshot(const WorldSnapshot& snapshot);

    // Get snapshot at tick
    const WorldSnapshot* GetSnapshot(uint32_t tick) const;

    // Get snapshots for interpolation
    bool GetInterpolationSnapshots(uint32_t tick, const WorldSnapshot*& before,
                                    const WorldSnapshot*& after, float& t) const;

    // Get latest snapshot
    const WorldSnapshot* GetLatestSnapshot() const;

    // Clear old snapshots
    void ClearBefore(uint32_t tick);

private:
    std::vector<WorldSnapshot> m_snapshots;
    size_t m_maxSnapshots;
};

} // namespace Cortex::Network
