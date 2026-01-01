#pragma once

// NetworkTransport.h
// Low-level network transport layer using UDP sockets.
// Provides reliable and unreliable packet delivery.

#include <string>
#include <vector>
#include <queue>
#include <unordered_map>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>

namespace Cortex::Network {

// Forward declarations
class NetPacket;
class NetConnection;

// ============================================================================
// Network Types
// ============================================================================

// Network address
struct NetAddress {
    std::string host;
    uint16_t port = 0;

    bool operator==(const NetAddress& other) const {
        return host == other.host && port == other.port;
    }

    bool operator!=(const NetAddress& other) const {
        return !(*this == other);
    }

    std::string ToString() const {
        return host + ":" + std::to_string(port);
    }

    static NetAddress FromString(const std::string& str);
};

// Hash for NetAddress
struct NetAddressHash {
    size_t operator()(const NetAddress& addr) const {
        return std::hash<std::string>{}(addr.host) ^ (std::hash<uint16_t>{}(addr.port) << 1);
    }
};

// Delivery mode for packets
enum class DeliveryMode : uint8_t {
    Unreliable,         // Fire and forget (UDP)
    UnreliableSequenced,// Drop out of order packets
    Reliable,           // Guaranteed delivery with retransmission
    ReliableOrdered     // Guaranteed delivery, in-order
};

// Connection state
enum class ConnectionState : uint8_t {
    Disconnected,
    Connecting,
    Connected,
    Disconnecting
};

// Disconnect reason
enum class DisconnectReason : uint8_t {
    None,
    Timeout,
    Kicked,
    Banned,
    ServerFull,
    VersionMismatch,
    UserRequested,
    ConnectionFailed,
    InvalidPacket
};

// Network statistics
struct NetStats {
    // Bandwidth
    uint64_t bytesSent = 0;
    uint64_t bytesReceived = 0;
    uint32_t packetsSent = 0;
    uint32_t packetsReceived = 0;
    uint32_t packetsLost = 0;
    uint32_t packetsResent = 0;

    // Latency
    float rtt = 0.0f;           // Round-trip time in ms
    float jitter = 0.0f;        // RTT variance
    float packetLoss = 0.0f;    // 0-1

    // Rate
    float sendRate = 0.0f;      // Bytes per second
    float receiveRate = 0.0f;

    // Timestamps
    std::chrono::steady_clock::time_point lastSendTime;
    std::chrono::steady_clock::time_point lastReceiveTime;
};

// ============================================================================
// Network Events
// ============================================================================

struct NetConnectEvent {
    uint32_t connectionId;
    NetAddress address;
};

struct NetDisconnectEvent {
    uint32_t connectionId;
    DisconnectReason reason;
    std::string message;
};

struct NetReceiveEvent {
    uint32_t connectionId;
    std::vector<uint8_t> data;
    DeliveryMode mode;
};

// Network event callbacks
struct NetEventCallbacks {
    std::function<void(const NetConnectEvent&)> onConnect;
    std::function<void(const NetDisconnectEvent&)> onDisconnect;
    std::function<void(const NetReceiveEvent&)> onReceive;
    std::function<void(const std::string&)> onError;
};

// ============================================================================
// Reliable Channel
// ============================================================================

class ReliableChannel {
public:
    ReliableChannel();
    ~ReliableChannel() = default;

    // Send reliable packet
    void Send(const std::vector<uint8_t>& data, bool ordered);

    // Process acknowledgment
    void ProcessAck(uint16_t ackSequence, uint32_t ackBits);

    // Get packets to send (includes retransmissions)
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> GetPendingPackets();

    // Process received packet
    bool ProcessReceived(uint16_t sequence, const std::vector<uint8_t>& data, bool ordered);

    // Get received packets in order
    std::vector<std::vector<uint8_t>> GetOrderedReceived();

    // Update (check for timeouts)
    void Update(float deltaTime);

    // Get statistics
    uint32_t GetPacketsInFlight() const { return static_cast<uint32_t>(m_sentPackets.size()); }
    float GetPacketLoss() const;

private:
    struct SentPacket {
        uint16_t sequence;
        std::vector<uint8_t> data;
        float timeSent;
        float lastResendTime;
        int resendCount;
        bool acked;
    };

    struct ReceivedPacket {
        uint16_t sequence;
        std::vector<uint8_t> data;
        bool received;
    };

    // Sending
    uint16_t m_localSequence = 0;
    std::unordered_map<uint16_t, SentPacket> m_sentPackets;

    // Receiving
    uint16_t m_remoteSequence = 0;
    uint32_t m_receivedBits = 0;
    std::unordered_map<uint16_t, ReceivedPacket> m_receivedPackets;
    uint16_t m_lastDeliveredSequence = 0;

    // Configuration
    float m_resendDelay = 0.1f;     // 100ms initial resend
    int m_maxResends = 10;
    float m_currentTime = 0.0f;

    // Statistics
    uint32_t m_totalSent = 0;
    uint32_t m_totalAcked = 0;
    uint32_t m_totalLost = 0;
};

// ============================================================================
// Net Connection
// ============================================================================

class NetConnection {
public:
    NetConnection(uint32_t id, const NetAddress& address);
    ~NetConnection() = default;

    // Getters
    uint32_t GetId() const { return m_id; }
    const NetAddress& GetAddress() const { return m_address; }
    ConnectionState GetState() const { return m_state; }
    const NetStats& GetStats() const { return m_stats; }

    // State management
    void SetState(ConnectionState state) { m_state = state; }

    // Send data
    void Send(const std::vector<uint8_t>& data, DeliveryMode mode);

    // Queue received data
    void QueueReceived(const std::vector<uint8_t>& data, DeliveryMode mode);

    // Get queued data to send
    std::vector<std::pair<DeliveryMode, std::vector<uint8_t>>> GetOutgoing();

    // Get received data
    std::vector<std::pair<DeliveryMode, std::vector<uint8_t>>> GetReceived();

    // Update connection
    void Update(float deltaTime);

    // Ping/RTT management
    void UpdateRTT(float rtt);
    float GetRTT() const { return m_stats.rtt; }

    // Timeout
    bool IsTimedOut(float timeout) const;
    void ResetTimeout();

    // User data
    void SetUserData(void* data) { m_userData = data; }
    void* GetUserData() const { return m_userData; }

private:
    uint32_t m_id;
    NetAddress m_address;
    ConnectionState m_state = ConnectionState::Disconnected;
    NetStats m_stats;

    // Channels
    ReliableChannel m_reliableOrderedChannel;
    ReliableChannel m_reliableUnorderedChannel;
    uint16_t m_unreliableSequence = 0;

    // Queues
    std::queue<std::pair<DeliveryMode, std::vector<uint8_t>>> m_outgoing;
    std::queue<std::pair<DeliveryMode, std::vector<uint8_t>>> m_received;
    std::mutex m_queueMutex;

    // Timing
    std::chrono::steady_clock::time_point m_lastReceiveTime;
    std::chrono::steady_clock::time_point m_connectTime;

    // RTT calculation
    float m_rttSmoothing = 0.1f;

    // User data
    void* m_userData = nullptr;
};

// ============================================================================
// Network Transport
// ============================================================================

class NetworkTransport {
public:
    NetworkTransport();
    ~NetworkTransport();

    // Initialization
    bool Initialize();
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Set callbacks
    void SetCallbacks(const NetEventCallbacks& callbacks) { m_callbacks = callbacks; }

    // Server operations
    bool StartServer(uint16_t port, int maxConnections = 32);
    void StopServer();
    bool IsServer() const { return m_isServer; }

    // Client operations
    bool Connect(const std::string& address, uint16_t port);
    void Disconnect();
    bool IsConnected() const;
    bool IsConnecting() const;

    // Send data
    void Send(uint32_t connectionId, const std::vector<uint8_t>& data, DeliveryMode mode = DeliveryMode::ReliableOrdered);
    void SendToAll(const std::vector<uint8_t>& data, DeliveryMode mode = DeliveryMode::ReliableOrdered);
    void SendToAllExcept(uint32_t excludeId, const std::vector<uint8_t>& data, DeliveryMode mode = DeliveryMode::ReliableOrdered);

    // Receive (call from main thread)
    void Poll();
    void Update(float deltaTime);

    // Connection management
    NetConnection* GetConnection(uint32_t id);
    const NetConnection* GetConnection(uint32_t id) const;
    std::vector<uint32_t> GetConnectionIds() const;
    int GetConnectionCount() const;
    void Kick(uint32_t connectionId, const std::string& reason = "");
    void Ban(uint32_t connectionId, const std::string& reason = "");

    // Statistics
    const NetStats& GetStats() const { return m_stats; }
    float GetAverageRTT() const;

    // Configuration
    void SetTimeout(float seconds) { m_timeout = seconds; }
    float GetTimeout() const { return m_timeout; }
    void SetMaxPacketSize(uint32_t size) { m_maxPacketSize = size; }
    uint32_t GetMaxPacketSize() const { return m_maxPacketSize; }

    // Protocol version
    void SetProtocolVersion(uint32_t version) { m_protocolVersion = version; }
    uint32_t GetProtocolVersion() const { return m_protocolVersion; }

    // Local address
    NetAddress GetLocalAddress() const;

private:
    // Socket operations
    bool CreateSocket();
    void CloseSocket();
    bool BindSocket(uint16_t port);
    void SendRaw(const NetAddress& address, const std::vector<uint8_t>& data);
    bool ReceiveRaw(NetAddress& outAddress, std::vector<uint8_t>& outData);

    // Connection management
    uint32_t CreateConnection(const NetAddress& address);
    void DestroyConnection(uint32_t id, DisconnectReason reason, const std::string& message = "");
    NetConnection* FindConnectionByAddress(const NetAddress& address);

    // Packet processing
    void ProcessPacket(const NetAddress& sender, const std::vector<uint8_t>& data);
    void ProcessConnectionRequest(const NetAddress& sender, const std::vector<uint8_t>& data);
    void ProcessConnectionAccept(const std::vector<uint8_t>& data);
    void ProcessConnectionDeny(const std::vector<uint8_t>& data);
    void ProcessDisconnect(uint32_t connectionId, const std::vector<uint8_t>& data);
    void ProcessData(uint32_t connectionId, const std::vector<uint8_t>& data);
    void ProcessPing(uint32_t connectionId);
    void ProcessPong(uint32_t connectionId, const std::vector<uint8_t>& data);

    // Internal packet building
    std::vector<uint8_t> BuildConnectionRequest();
    std::vector<uint8_t> BuildConnectionAccept(uint32_t connectionId);
    std::vector<uint8_t> BuildConnectionDeny(DisconnectReason reason);
    std::vector<uint8_t> BuildDisconnect(DisconnectReason reason);
    std::vector<uint8_t> BuildPing();
    std::vector<uint8_t> BuildPong(uint64_t timestamp);

    // Network thread
    void NetworkThread();
    void StartNetworkThread();
    void StopNetworkThread();

    // State
    bool m_initialized = false;
    bool m_isServer = false;
    std::atomic<bool> m_running{false};

    // Socket (platform-specific handle)
    uintptr_t m_socket = 0;
    uint16_t m_localPort = 0;

    // Connections
    std::unordered_map<uint32_t, std::unique_ptr<NetConnection>> m_connections;
    std::unordered_map<NetAddress, uint32_t, NetAddressHash> m_addressToConnection;
    uint32_t m_nextConnectionId = 1;
    int m_maxConnections = 32;
    std::mutex m_connectionMutex;

    // Client-specific
    NetAddress m_serverAddress;
    uint32_t m_serverConnectionId = 0;

    // Callbacks
    NetEventCallbacks m_callbacks;

    // Event queues (thread-safe)
    std::queue<NetConnectEvent> m_connectEvents;
    std::queue<NetDisconnectEvent> m_disconnectEvents;
    std::queue<NetReceiveEvent> m_receiveEvents;
    std::mutex m_eventMutex;

    // Statistics
    NetStats m_stats;

    // Configuration
    float m_timeout = 30.0f;
    uint32_t m_maxPacketSize = 1400;  // MTU safe
    uint32_t m_protocolVersion = 1;
    float m_pingInterval = 1.0f;
    float m_pingTimer = 0.0f;

    // Threading
    std::thread m_networkThread;
    std::atomic<bool> m_threadRunning{false};

    // Banned addresses
    std::unordered_set<std::string> m_bannedAddresses;
};

// ============================================================================
// Packet Compression
// ============================================================================

namespace PacketCompression {

// Compress data using LZ4 (stub - would use actual LZ4 library)
std::vector<uint8_t> Compress(const std::vector<uint8_t>& data);
std::vector<uint8_t> Decompress(const std::vector<uint8_t>& data, size_t originalSize);

// Check if compression is worthwhile
bool ShouldCompress(const std::vector<uint8_t>& data);

} // namespace PacketCompression

// ============================================================================
// Network Utilities
// ============================================================================

namespace NetUtils {

// Get local IP address
std::string GetLocalIPAddress();

// Check if address is LAN
bool IsLANAddress(const std::string& address);

// Check if address is loopback
bool IsLoopbackAddress(const std::string& address);

// Resolve hostname to IP
std::string ResolveHostname(const std::string& hostname);

// Get current timestamp in microseconds
uint64_t GetTimestampMicros();

// Calculate checksum
uint32_t CalculateCRC32(const uint8_t* data, size_t size);

// Byte order conversion
uint16_t HostToNetwork16(uint16_t value);
uint32_t HostToNetwork32(uint32_t value);
uint64_t HostToNetwork64(uint64_t value);
uint16_t NetworkToHost16(uint16_t value);
uint32_t NetworkToHost32(uint32_t value);
uint64_t NetworkToHost64(uint64_t value);

} // namespace NetUtils

} // namespace Cortex::Network
