// NetworkTransport.cpp
// Network transport layer implementation.

#include "NetworkTransport.h"
#include <algorithm>
#include <cstring>
#include <sstream>

// Platform-specific includes
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define closesocket close
#endif

namespace Cortex::Network {

// ============================================================================
// Packet Header Format
// ============================================================================

// Packet types
enum class PacketType : uint8_t {
    ConnectionRequest = 1,
    ConnectionAccept = 2,
    ConnectionDeny = 3,
    Disconnect = 4,
    Ping = 5,
    Pong = 6,
    Data = 7,
    Ack = 8,
    Fragment = 9
};

// Packet header (8 bytes)
struct PacketHeader {
    uint32_t protocolId;    // Magic number for protocol identification
    uint8_t packetType;
    uint8_t flags;
    uint16_t sequence;
};

constexpr uint32_t PROTOCOL_MAGIC = 0x434F5254;  // "CORT"

// ============================================================================
// NetAddress Implementation
// ============================================================================

NetAddress NetAddress::FromString(const std::string& str) {
    NetAddress addr;
    size_t colonPos = str.rfind(':');
    if (colonPos != std::string::npos) {
        addr.host = str.substr(0, colonPos);
        addr.port = static_cast<uint16_t>(std::stoi(str.substr(colonPos + 1)));
    } else {
        addr.host = str;
        addr.port = 0;
    }
    return addr;
}

// ============================================================================
// ReliableChannel Implementation
// ============================================================================

ReliableChannel::ReliableChannel() {
}

void ReliableChannel::Send(const std::vector<uint8_t>& data, bool /*ordered*/) {
    SentPacket packet;
    packet.sequence = m_localSequence++;
    packet.data = data;
    packet.timeSent = m_currentTime;
    packet.lastResendTime = m_currentTime;
    packet.resendCount = 0;
    packet.acked = false;

    m_sentPackets[packet.sequence] = std::move(packet);
    m_totalSent++;
}

void ReliableChannel::ProcessAck(uint16_t ackSequence, uint32_t ackBits) {
    // Process the ack sequence
    auto it = m_sentPackets.find(ackSequence);
    if (it != m_sentPackets.end() && !it->second.acked) {
        it->second.acked = true;
        m_totalAcked++;
    }

    // Process the ack bits (previous 32 packets)
    for (int i = 0; i < 32; ++i) {
        if (ackBits & (1u << i)) {
            uint16_t seq = ackSequence - static_cast<uint16_t>(i + 1);
            auto seqIt = m_sentPackets.find(seq);
            if (seqIt != m_sentPackets.end() && !seqIt->second.acked) {
                seqIt->second.acked = true;
                m_totalAcked++;
            }
        }
    }

    // Clean up acked packets
    for (auto it = m_sentPackets.begin(); it != m_sentPackets.end();) {
        if (it->second.acked) {
            it = m_sentPackets.erase(it);
        } else {
            ++it;
        }
    }
}

std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ReliableChannel::GetPendingPackets() {
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> result;

    for (auto& [seq, packet] : m_sentPackets) {
        if (packet.acked) continue;

        // Check if needs resend
        float timeSinceLastSend = m_currentTime - packet.lastResendTime;
        float resendDelay = m_resendDelay * (1 << std::min(packet.resendCount, 4));  // Exponential backoff

        if (packet.resendCount == 0 || timeSinceLastSend >= resendDelay) {
            result.emplace_back(packet.sequence, packet.data);
            packet.lastResendTime = m_currentTime;
            packet.resendCount++;

            if (packet.resendCount > m_maxResends) {
                m_totalLost++;
            }
        }
    }

    return result;
}

bool ReliableChannel::ProcessReceived(uint16_t sequence, const std::vector<uint8_t>& data, bool /*ordered*/) {
    // Check if already received
    if (m_receivedPackets.find(sequence) != m_receivedPackets.end()) {
        return false;  // Duplicate
    }

    // Check if too old
    int16_t diff = static_cast<int16_t>(sequence - m_remoteSequence);
    if (diff < -32768) diff += 65536;
    if (diff > 32768) diff -= 65536;

    if (diff < -32) {
        return false;  // Too old
    }

    // Store received packet
    ReceivedPacket packet;
    packet.sequence = sequence;
    packet.data = data;
    packet.received = true;
    m_receivedPackets[sequence] = std::move(packet);

    // Update remote sequence
    if (diff > 0) {
        // Update received bits
        int shift = diff;
        if (shift < 32) {
            m_receivedBits = (m_receivedBits << shift) | (1u << (shift - 1));
        } else {
            m_receivedBits = 0;
        }
        m_remoteSequence = sequence;
    } else {
        // Mark bit for older packet
        int bit = -diff - 1;
        if (bit < 32) {
            m_receivedBits |= (1u << bit);
        }
    }

    return true;
}

std::vector<std::vector<uint8_t>> ReliableChannel::GetOrderedReceived() {
    std::vector<std::vector<uint8_t>> result;

    // Deliver in order
    while (true) {
        uint16_t nextSeq = m_lastDeliveredSequence + 1;
        auto it = m_receivedPackets.find(nextSeq);
        if (it == m_receivedPackets.end() || !it->second.received) {
            break;
        }

        result.push_back(std::move(it->second.data));
        m_receivedPackets.erase(it);
        m_lastDeliveredSequence = nextSeq;
    }

    return result;
}

void ReliableChannel::Update(float deltaTime) {
    m_currentTime += deltaTime;

    // Clean up old unacked packets that exceeded max resends
    for (auto it = m_sentPackets.begin(); it != m_sentPackets.end();) {
        if (it->second.resendCount > m_maxResends) {
            it = m_sentPackets.erase(it);
        } else {
            ++it;
        }
    }
}

float ReliableChannel::GetPacketLoss() const {
    if (m_totalSent == 0) return 0.0f;
    return static_cast<float>(m_totalLost) / static_cast<float>(m_totalSent);
}

// ============================================================================
// NetConnection Implementation
// ============================================================================

NetConnection::NetConnection(uint32_t id, const NetAddress& address)
    : m_id(id), m_address(address) {
    m_lastReceiveTime = std::chrono::steady_clock::now();
    m_connectTime = std::chrono::steady_clock::now();
}

void NetConnection::Send(const std::vector<uint8_t>& data, DeliveryMode mode) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_outgoing.emplace(mode, data);
}

void NetConnection::QueueReceived(const std::vector<uint8_t>& data, DeliveryMode mode) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_received.emplace(mode, data);
    m_lastReceiveTime = std::chrono::steady_clock::now();
}

std::vector<std::pair<DeliveryMode, std::vector<uint8_t>>> NetConnection::GetOutgoing() {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    std::vector<std::pair<DeliveryMode, std::vector<uint8_t>>> result;

    while (!m_outgoing.empty()) {
        result.push_back(std::move(m_outgoing.front()));
        m_outgoing.pop();
    }

    return result;
}

std::vector<std::pair<DeliveryMode, std::vector<uint8_t>>> NetConnection::GetReceived() {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    std::vector<std::pair<DeliveryMode, std::vector<uint8_t>>> result;

    while (!m_received.empty()) {
        result.push_back(std::move(m_received.front()));
        m_received.pop();
    }

    return result;
}

void NetConnection::Update(float deltaTime) {
    m_reliableOrderedChannel.Update(deltaTime);
    m_reliableUnorderedChannel.Update(deltaTime);
}

void NetConnection::UpdateRTT(float rtt) {
    if (m_stats.rtt == 0.0f) {
        m_stats.rtt = rtt;
    } else {
        m_stats.rtt = m_stats.rtt * (1.0f - m_rttSmoothing) + rtt * m_rttSmoothing;
    }

    // Update jitter
    float diff = std::abs(rtt - m_stats.rtt);
    m_stats.jitter = m_stats.jitter * (1.0f - m_rttSmoothing) + diff * m_rttSmoothing;
}

bool NetConnection::IsTimedOut(float timeout) const {
    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - m_lastReceiveTime).count();
    return elapsed > timeout;
}

void NetConnection::ResetTimeout() {
    m_lastReceiveTime = std::chrono::steady_clock::now();
}

// ============================================================================
// NetworkTransport Implementation
// ============================================================================

NetworkTransport::NetworkTransport() {
}

NetworkTransport::~NetworkTransport() {
    Shutdown();
}

bool NetworkTransport::Initialize() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return false;
    }
#endif

    m_initialized = true;
    return true;
}

void NetworkTransport::Shutdown() {
    StopServer();
    Disconnect();
    StopNetworkThread();

    if (m_socket != 0) {
        CloseSocket();
    }

#ifdef _WIN32
    if (m_initialized) {
        WSACleanup();
    }
#endif

    m_initialized = false;
}

bool NetworkTransport::CreateSocket() {
    m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket == INVALID_SOCKET) {
        return false;
    }

    // Set non-blocking
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(static_cast<SOCKET>(m_socket), FIONBIO, &mode);
#else
    int flags = fcntl(m_socket, F_GETFL, 0);
    fcntl(m_socket, F_SETFL, flags | O_NONBLOCK);
#endif

    // Set socket options
    int yes = 1;
    setsockopt(static_cast<int>(m_socket), SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&yes), sizeof(yes));

    return true;
}

void NetworkTransport::CloseSocket() {
    if (m_socket != 0) {
        closesocket(static_cast<int>(m_socket));
        m_socket = 0;
    }
}

bool NetworkTransport::BindSocket(uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(static_cast<int>(m_socket), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        return false;
    }

    m_localPort = port;
    return true;
}

void NetworkTransport::SendRaw(const NetAddress& address, const std::vector<uint8_t>& data) {
    if (m_socket == 0 || data.empty()) return;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(address.port);
    inet_pton(AF_INET, address.host.c_str(), &addr.sin_addr);

    sendto(static_cast<int>(m_socket), reinterpret_cast<const char*>(data.data()),
           static_cast<int>(data.size()), 0,
           reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    m_stats.bytesSent += data.size();
    m_stats.packetsSent++;
    m_stats.lastSendTime = std::chrono::steady_clock::now();
}

bool NetworkTransport::ReceiveRaw(NetAddress& outAddress, std::vector<uint8_t>& outData) {
    if (m_socket == 0) return false;

    outData.resize(m_maxPacketSize);

    sockaddr_in addr{};
    socklen_t addrLen = sizeof(addr);

    int received = recvfrom(static_cast<int>(m_socket),
                            reinterpret_cast<char*>(outData.data()),
                            static_cast<int>(outData.size()), 0,
                            reinterpret_cast<sockaddr*>(&addr), &addrLen);

    if (received <= 0) {
        return false;
    }

    outData.resize(received);

    char hostStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, hostStr, sizeof(hostStr));
    outAddress.host = hostStr;
    outAddress.port = ntohs(addr.sin_port);

    m_stats.bytesReceived += received;
    m_stats.packetsReceived++;
    m_stats.lastReceiveTime = std::chrono::steady_clock::now();

    return true;
}

bool NetworkTransport::StartServer(uint16_t port, int maxConnections) {
    if (!m_initialized) return false;
    if (m_isServer) return false;

    if (!CreateSocket()) {
        return false;
    }

    if (!BindSocket(port)) {
        CloseSocket();
        return false;
    }

    m_isServer = true;
    m_maxConnections = maxConnections;
    m_running = true;

    StartNetworkThread();
    return true;
}

void NetworkTransport::StopServer() {
    if (!m_isServer) return;

    // Disconnect all clients
    std::vector<uint32_t> connectionIds;
    {
        std::lock_guard<std::mutex> lock(m_connectionMutex);
        for (auto& [id, conn] : m_connections) {
            connectionIds.push_back(id);
        }
    }

    for (uint32_t id : connectionIds) {
        DestroyConnection(id, DisconnectReason::UserRequested, "Server shutting down");
    }

    m_running = false;
    StopNetworkThread();
    CloseSocket();
    m_isServer = false;
}

bool NetworkTransport::Connect(const std::string& address, uint16_t port) {
    if (!m_initialized) return false;
    if (m_isServer) return false;
    if (IsConnected() || IsConnecting()) return false;

    if (!CreateSocket()) {
        return false;
    }

    // Bind to any available port
    if (!BindSocket(0)) {
        CloseSocket();
        return false;
    }

    m_serverAddress.host = address;
    m_serverAddress.port = port;

    // Create connection object
    m_serverConnectionId = CreateConnection(m_serverAddress);
    auto* conn = GetConnection(m_serverConnectionId);
    if (conn) {
        conn->SetState(ConnectionState::Connecting);
    }

    m_running = true;
    StartNetworkThread();

    // Send connection request
    auto request = BuildConnectionRequest();
    SendRaw(m_serverAddress, request);

    return true;
}

void NetworkTransport::Disconnect() {
    if (!IsConnected() && !IsConnecting()) return;

    if (m_serverConnectionId != 0) {
        auto* conn = GetConnection(m_serverConnectionId);
        if (conn) {
            auto disconnect = BuildDisconnect(DisconnectReason::UserRequested);
            SendRaw(conn->GetAddress(), disconnect);
        }
        DestroyConnection(m_serverConnectionId, DisconnectReason::UserRequested);
        m_serverConnectionId = 0;
    }

    m_running = false;
    StopNetworkThread();
    CloseSocket();
}

bool NetworkTransport::IsConnected() const {
    if (m_serverConnectionId == 0) return false;
    auto* conn = const_cast<NetworkTransport*>(this)->GetConnection(m_serverConnectionId);
    return conn && conn->GetState() == ConnectionState::Connected;
}

bool NetworkTransport::IsConnecting() const {
    if (m_serverConnectionId == 0) return false;
    auto* conn = const_cast<NetworkTransport*>(this)->GetConnection(m_serverConnectionId);
    return conn && conn->GetState() == ConnectionState::Connecting;
}

void NetworkTransport::Send(uint32_t connectionId, const std::vector<uint8_t>& data, DeliveryMode mode) {
    std::lock_guard<std::mutex> lock(m_connectionMutex);
    auto it = m_connections.find(connectionId);
    if (it != m_connections.end()) {
        it->second->Send(data, mode);
    }
}

void NetworkTransport::SendToAll(const std::vector<uint8_t>& data, DeliveryMode mode) {
    std::lock_guard<std::mutex> lock(m_connectionMutex);
    for (auto& [id, conn] : m_connections) {
        if (conn->GetState() == ConnectionState::Connected) {
            conn->Send(data, mode);
        }
    }
}

void NetworkTransport::SendToAllExcept(uint32_t excludeId, const std::vector<uint8_t>& data, DeliveryMode mode) {
    std::lock_guard<std::mutex> lock(m_connectionMutex);
    for (auto& [id, conn] : m_connections) {
        if (id != excludeId && conn->GetState() == ConnectionState::Connected) {
            conn->Send(data, mode);
        }
    }
}

void NetworkTransport::Poll() {
    // Process events from network thread
    std::lock_guard<std::mutex> lock(m_eventMutex);

    // Connect events
    while (!m_connectEvents.empty()) {
        auto& event = m_connectEvents.front();
        if (m_callbacks.onConnect) {
            m_callbacks.onConnect(event);
        }
        m_connectEvents.pop();
    }

    // Disconnect events
    while (!m_disconnectEvents.empty()) {
        auto& event = m_disconnectEvents.front();
        if (m_callbacks.onDisconnect) {
            m_callbacks.onDisconnect(event);
        }
        m_disconnectEvents.pop();
    }

    // Receive events
    while (!m_receiveEvents.empty()) {
        auto& event = m_receiveEvents.front();
        if (m_callbacks.onReceive) {
            m_callbacks.onReceive(event);
        }
        m_receiveEvents.pop();
    }
}

void NetworkTransport::Update(float deltaTime) {
    Poll();

    // Update ping timer
    m_pingTimer += deltaTime;
    if (m_pingTimer >= m_pingInterval) {
        m_pingTimer = 0.0f;

        // Send pings to all connections
        std::lock_guard<std::mutex> lock(m_connectionMutex);
        for (auto& [id, conn] : m_connections) {
            if (conn->GetState() == ConnectionState::Connected) {
                auto ping = BuildPing();
                SendRaw(conn->GetAddress(), ping);
            }
        }
    }

    // Update connections and check timeouts
    std::vector<uint32_t> timedOut;
    {
        std::lock_guard<std::mutex> lock(m_connectionMutex);
        for (auto& [id, conn] : m_connections) {
            conn->Update(deltaTime);

            if (conn->IsTimedOut(m_timeout)) {
                timedOut.push_back(id);
            }

            // Process outgoing packets
            auto outgoing = conn->GetOutgoing();
            for (auto& [mode, data] : outgoing) {
                // Build data packet
                std::vector<uint8_t> packet;
                packet.reserve(sizeof(PacketHeader) + data.size());

                PacketHeader header;
                header.protocolId = PROTOCOL_MAGIC;
                header.packetType = static_cast<uint8_t>(PacketType::Data);
                header.flags = static_cast<uint8_t>(mode);
                header.sequence = 0;

                packet.insert(packet.end(), reinterpret_cast<uint8_t*>(&header),
                              reinterpret_cast<uint8_t*>(&header) + sizeof(header));
                packet.insert(packet.end(), data.begin(), data.end());

                SendRaw(conn->GetAddress(), packet);
            }
        }
    }

    // Disconnect timed out connections
    for (uint32_t id : timedOut) {
        DestroyConnection(id, DisconnectReason::Timeout);
    }
}

uint32_t NetworkTransport::CreateConnection(const NetAddress& address) {
    std::lock_guard<std::mutex> lock(m_connectionMutex);

    uint32_t id = m_nextConnectionId++;
    auto conn = std::make_unique<NetConnection>(id, address);
    m_addressToConnection[address] = id;
    m_connections[id] = std::move(conn);

    return id;
}

void NetworkTransport::DestroyConnection(uint32_t id, DisconnectReason reason, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_connectionMutex);

    auto it = m_connections.find(id);
    if (it == m_connections.end()) return;

    // Send disconnect packet
    auto disconnect = BuildDisconnect(reason);
    SendRaw(it->second->GetAddress(), disconnect);

    // Queue disconnect event
    {
        std::lock_guard<std::mutex> eventLock(m_eventMutex);
        NetDisconnectEvent event;
        event.connectionId = id;
        event.reason = reason;
        event.message = message;
        m_disconnectEvents.push(event);
    }

    // Remove from maps
    m_addressToConnection.erase(it->second->GetAddress());
    m_connections.erase(it);
}

NetConnection* NetworkTransport::GetConnection(uint32_t id) {
    std::lock_guard<std::mutex> lock(m_connectionMutex);
    auto it = m_connections.find(id);
    return it != m_connections.end() ? it->second.get() : nullptr;
}

const NetConnection* NetworkTransport::GetConnection(uint32_t id) const {
    return const_cast<NetworkTransport*>(this)->GetConnection(id);
}

NetConnection* NetworkTransport::FindConnectionByAddress(const NetAddress& address) {
    std::lock_guard<std::mutex> lock(m_connectionMutex);
    auto it = m_addressToConnection.find(address);
    if (it != m_addressToConnection.end()) {
        auto connIt = m_connections.find(it->second);
        if (connIt != m_connections.end()) {
            return connIt->second.get();
        }
    }
    return nullptr;
}

std::vector<uint32_t> NetworkTransport::GetConnectionIds() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_connectionMutex));
    std::vector<uint32_t> ids;
    for (auto& [id, conn] : m_connections) {
        ids.push_back(id);
    }
    return ids;
}

int NetworkTransport::GetConnectionCount() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_connectionMutex));
    return static_cast<int>(m_connections.size());
}

void NetworkTransport::Kick(uint32_t connectionId, const std::string& reason) {
    DestroyConnection(connectionId, DisconnectReason::Kicked, reason);
}

void NetworkTransport::Ban(uint32_t connectionId, const std::string& reason) {
    auto* conn = GetConnection(connectionId);
    if (conn) {
        m_bannedAddresses.insert(conn->GetAddress().host);
    }
    DestroyConnection(connectionId, DisconnectReason::Banned, reason);
}

float NetworkTransport::GetAverageRTT() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_connectionMutex));
    if (m_connections.empty()) return 0.0f;

    float total = 0.0f;
    for (auto& [id, conn] : m_connections) {
        total += conn->GetRTT();
    }
    return total / static_cast<float>(m_connections.size());
}

NetAddress NetworkTransport::GetLocalAddress() const {
    NetAddress addr;
    addr.host = NetUtils::GetLocalIPAddress();
    addr.port = m_localPort;
    return addr;
}

// Packet building
std::vector<uint8_t> NetworkTransport::BuildConnectionRequest() {
    std::vector<uint8_t> packet;

    PacketHeader header;
    header.protocolId = PROTOCOL_MAGIC;
    header.packetType = static_cast<uint8_t>(PacketType::ConnectionRequest);
    header.flags = 0;
    header.sequence = 0;

    packet.insert(packet.end(), reinterpret_cast<uint8_t*>(&header),
                  reinterpret_cast<uint8_t*>(&header) + sizeof(header));

    // Add protocol version
    uint32_t version = m_protocolVersion;
    packet.insert(packet.end(), reinterpret_cast<uint8_t*>(&version),
                  reinterpret_cast<uint8_t*>(&version) + sizeof(version));

    return packet;
}

std::vector<uint8_t> NetworkTransport::BuildConnectionAccept(uint32_t connectionId) {
    std::vector<uint8_t> packet;

    PacketHeader header;
    header.protocolId = PROTOCOL_MAGIC;
    header.packetType = static_cast<uint8_t>(PacketType::ConnectionAccept);
    header.flags = 0;
    header.sequence = 0;

    packet.insert(packet.end(), reinterpret_cast<uint8_t*>(&header),
                  reinterpret_cast<uint8_t*>(&header) + sizeof(header));

    // Add connection ID
    packet.insert(packet.end(), reinterpret_cast<uint8_t*>(&connectionId),
                  reinterpret_cast<uint8_t*>(&connectionId) + sizeof(connectionId));

    return packet;
}

std::vector<uint8_t> NetworkTransport::BuildConnectionDeny(DisconnectReason reason) {
    std::vector<uint8_t> packet;

    PacketHeader header;
    header.protocolId = PROTOCOL_MAGIC;
    header.packetType = static_cast<uint8_t>(PacketType::ConnectionDeny);
    header.flags = static_cast<uint8_t>(reason);
    header.sequence = 0;

    packet.insert(packet.end(), reinterpret_cast<uint8_t*>(&header),
                  reinterpret_cast<uint8_t*>(&header) + sizeof(header));

    return packet;
}

std::vector<uint8_t> NetworkTransport::BuildDisconnect(DisconnectReason reason) {
    std::vector<uint8_t> packet;

    PacketHeader header;
    header.protocolId = PROTOCOL_MAGIC;
    header.packetType = static_cast<uint8_t>(PacketType::Disconnect);
    header.flags = static_cast<uint8_t>(reason);
    header.sequence = 0;

    packet.insert(packet.end(), reinterpret_cast<uint8_t*>(&header),
                  reinterpret_cast<uint8_t*>(&header) + sizeof(header));

    return packet;
}

std::vector<uint8_t> NetworkTransport::BuildPing() {
    std::vector<uint8_t> packet;

    PacketHeader header;
    header.protocolId = PROTOCOL_MAGIC;
    header.packetType = static_cast<uint8_t>(PacketType::Ping);
    header.flags = 0;
    header.sequence = 0;

    packet.insert(packet.end(), reinterpret_cast<uint8_t*>(&header),
                  reinterpret_cast<uint8_t*>(&header) + sizeof(header));

    // Add timestamp
    uint64_t timestamp = NetUtils::GetTimestampMicros();
    packet.insert(packet.end(), reinterpret_cast<uint8_t*>(&timestamp),
                  reinterpret_cast<uint8_t*>(&timestamp) + sizeof(timestamp));

    return packet;
}

std::vector<uint8_t> NetworkTransport::BuildPong(uint64_t timestamp) {
    std::vector<uint8_t> packet;

    PacketHeader header;
    header.protocolId = PROTOCOL_MAGIC;
    header.packetType = static_cast<uint8_t>(PacketType::Pong);
    header.flags = 0;
    header.sequence = 0;

    packet.insert(packet.end(), reinterpret_cast<uint8_t*>(&header),
                  reinterpret_cast<uint8_t*>(&header) + sizeof(header));

    // Echo timestamp
    packet.insert(packet.end(), reinterpret_cast<uint8_t*>(&timestamp),
                  reinterpret_cast<uint8_t*>(&timestamp) + sizeof(timestamp));

    return packet;
}

// Packet processing
void NetworkTransport::ProcessPacket(const NetAddress& sender, const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(PacketHeader)) return;

    PacketHeader header;
    std::memcpy(&header, data.data(), sizeof(header));

    // Verify protocol magic
    if (header.protocolId != PROTOCOL_MAGIC) return;

    PacketType type = static_cast<PacketType>(header.packetType);

    switch (type) {
        case PacketType::ConnectionRequest:
            ProcessConnectionRequest(sender, data);
            break;
        case PacketType::ConnectionAccept:
            ProcessConnectionAccept(data);
            break;
        case PacketType::ConnectionDeny:
            ProcessConnectionDeny(data);
            break;
        case PacketType::Disconnect: {
            auto* conn = FindConnectionByAddress(sender);
            if (conn) {
                ProcessDisconnect(conn->GetId(), data);
            }
            break;
        }
        case PacketType::Ping: {
            auto* conn = FindConnectionByAddress(sender);
            if (conn) {
                ProcessPing(conn->GetId());
            }
            break;
        }
        case PacketType::Pong: {
            auto* conn = FindConnectionByAddress(sender);
            if (conn) {
                ProcessPong(conn->GetId(), data);
            }
            break;
        }
        case PacketType::Data: {
            auto* conn = FindConnectionByAddress(sender);
            if (conn) {
                ProcessData(conn->GetId(), data);
            }
            break;
        }
        default:
            break;
    }
}

void NetworkTransport::ProcessConnectionRequest(const NetAddress& sender, const std::vector<uint8_t>& data) {
    if (!m_isServer) return;

    // Check if banned
    if (m_bannedAddresses.count(sender.host) > 0) {
        auto deny = BuildConnectionDeny(DisconnectReason::Banned);
        SendRaw(sender, deny);
        return;
    }

    // Check if server is full
    if (GetConnectionCount() >= m_maxConnections) {
        auto deny = BuildConnectionDeny(DisconnectReason::ServerFull);
        SendRaw(sender, deny);
        return;
    }

    // Check protocol version
    if (data.size() >= sizeof(PacketHeader) + sizeof(uint32_t)) {
        uint32_t clientVersion;
        std::memcpy(&clientVersion, data.data() + sizeof(PacketHeader), sizeof(clientVersion));
        if (clientVersion != m_protocolVersion) {
            auto deny = BuildConnectionDeny(DisconnectReason::VersionMismatch);
            SendRaw(sender, deny);
            return;
        }
    }

    // Create connection
    uint32_t connectionId = CreateConnection(sender);
    auto* conn = GetConnection(connectionId);
    if (conn) {
        conn->SetState(ConnectionState::Connected);

        // Send accept
        auto accept = BuildConnectionAccept(connectionId);
        SendRaw(sender, accept);

        // Queue connect event
        {
            std::lock_guard<std::mutex> lock(m_eventMutex);
            NetConnectEvent event;
            event.connectionId = connectionId;
            event.address = sender;
            m_connectEvents.push(event);
        }
    }
}

void NetworkTransport::ProcessConnectionAccept(const std::vector<uint8_t>& data) {
    if (m_isServer) return;
    if (m_serverConnectionId == 0) return;

    auto* conn = GetConnection(m_serverConnectionId);
    if (!conn || conn->GetState() != ConnectionState::Connecting) return;

    conn->SetState(ConnectionState::Connected);

    // Queue connect event
    {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        NetConnectEvent event;
        event.connectionId = m_serverConnectionId;
        event.address = m_serverAddress;
        m_connectEvents.push(event);
    }
}

void NetworkTransport::ProcessConnectionDeny(const std::vector<uint8_t>& data) {
    if (m_isServer) return;
    if (m_serverConnectionId == 0) return;

    PacketHeader header;
    std::memcpy(&header, data.data(), sizeof(header));
    DisconnectReason reason = static_cast<DisconnectReason>(header.flags);

    DestroyConnection(m_serverConnectionId, reason, "Connection denied");
    m_serverConnectionId = 0;
}

void NetworkTransport::ProcessDisconnect(uint32_t connectionId, const std::vector<uint8_t>& data) {
    PacketHeader header;
    std::memcpy(&header, data.data(), sizeof(header));
    DisconnectReason reason = static_cast<DisconnectReason>(header.flags);

    DestroyConnection(connectionId, reason, "Remote disconnected");

    if (connectionId == m_serverConnectionId) {
        m_serverConnectionId = 0;
    }
}

void NetworkTransport::ProcessData(uint32_t connectionId, const std::vector<uint8_t>& data) {
    if (data.size() <= sizeof(PacketHeader)) return;

    PacketHeader header;
    std::memcpy(&header, data.data(), sizeof(header));
    DeliveryMode mode = static_cast<DeliveryMode>(header.flags);

    // Extract payload
    std::vector<uint8_t> payload(data.begin() + sizeof(PacketHeader), data.end());

    auto* conn = GetConnection(connectionId);
    if (conn) {
        conn->QueueReceived(payload, mode);

        // Queue receive event
        std::lock_guard<std::mutex> lock(m_eventMutex);
        NetReceiveEvent event;
        event.connectionId = connectionId;
        event.data = std::move(payload);
        event.mode = mode;
        m_receiveEvents.push(event);
    }
}

void NetworkTransport::ProcessPing(uint32_t connectionId) {
    auto* conn = GetConnection(connectionId);
    if (!conn) return;

    // Respond with pong
    auto pong = BuildPong(NetUtils::GetTimestampMicros());
    SendRaw(conn->GetAddress(), pong);
}

void NetworkTransport::ProcessPong(uint32_t connectionId, const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(PacketHeader) + sizeof(uint64_t)) return;

    uint64_t sentTimestamp;
    std::memcpy(&sentTimestamp, data.data() + sizeof(PacketHeader), sizeof(sentTimestamp));

    uint64_t now = NetUtils::GetTimestampMicros();
    float rtt = static_cast<float>(now - sentTimestamp) / 1000.0f;  // Convert to ms

    auto* conn = GetConnection(connectionId);
    if (conn) {
        conn->UpdateRTT(rtt);
    }
}

// Network thread
void NetworkTransport::NetworkThread() {
    while (m_threadRunning) {
        NetAddress sender;
        std::vector<uint8_t> data;

        while (ReceiveRaw(sender, data)) {
            ProcessPacket(sender, data);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void NetworkTransport::StartNetworkThread() {
    if (m_threadRunning) return;

    m_threadRunning = true;
    m_networkThread = std::thread(&NetworkTransport::NetworkThread, this);
}

void NetworkTransport::StopNetworkThread() {
    if (!m_threadRunning) return;

    m_threadRunning = false;
    if (m_networkThread.joinable()) {
        m_networkThread.join();
    }
}

// ============================================================================
// PacketCompression Implementation
// ============================================================================

namespace PacketCompression {

std::vector<uint8_t> Compress(const std::vector<uint8_t>& data) {
    // Stub - would use LZ4 library
    return data;
}

std::vector<uint8_t> Decompress(const std::vector<uint8_t>& data, size_t /*originalSize*/) {
    // Stub - would use LZ4 library
    return data;
}

bool ShouldCompress(const std::vector<uint8_t>& data) {
    return data.size() > 256;  // Only compress larger packets
}

} // namespace PacketCompression

// ============================================================================
// NetUtils Implementation
// ============================================================================

namespace NetUtils {

std::string GetLocalIPAddress() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        struct addrinfo hints{}, *res;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;

        if (getaddrinfo(hostname, nullptr, &hints, &res) == 0) {
            char ip[INET_ADDRSTRLEN];
            struct sockaddr_in* addr = reinterpret_cast<struct sockaddr_in*>(res->ai_addr);
            inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
            freeaddrinfo(res);
            return ip;
        }
    }
    return "127.0.0.1";
}

bool IsLANAddress(const std::string& address) {
    // Check private IP ranges
    if (address.substr(0, 3) == "10.") return true;
    if (address.substr(0, 8) == "192.168.") return true;
    if (address.substr(0, 4) == "172.") {
        int second = std::stoi(address.substr(4, address.find('.', 4) - 4));
        if (second >= 16 && second <= 31) return true;
    }
    return false;
}

bool IsLoopbackAddress(const std::string& address) {
    return address == "127.0.0.1" || address == "localhost";
}

std::string ResolveHostname(const std::string& hostname) {
    struct addrinfo hints{}, *res;
    hints.ai_family = AF_INET;

    if (getaddrinfo(hostname.c_str(), nullptr, &hints, &res) == 0) {
        char ip[INET_ADDRSTRLEN];
        struct sockaddr_in* addr = reinterpret_cast<struct sockaddr_in*>(res->ai_addr);
        inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
        freeaddrinfo(res);
        return ip;
    }

    return hostname;
}

uint64_t GetTimestampMicros() {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

uint32_t CalculateCRC32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFF;
    static const uint32_t table[256] = {
        // CRC32 lookup table (IEEE polynomial)
        0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
        0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
        0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
        // ... (abbreviated for brevity - full table would be here)
    };

    for (size_t i = 0; i < size; ++i) {
        crc = (crc >> 8) ^ table[(crc ^ data[i]) & 0xFF];
    }

    return crc ^ 0xFFFFFFFF;
}

uint16_t HostToNetwork16(uint16_t value) { return htons(value); }
uint32_t HostToNetwork32(uint32_t value) { return htonl(value); }
uint64_t HostToNetwork64(uint64_t value) {
    return (static_cast<uint64_t>(htonl(static_cast<uint32_t>(value))) << 32) |
           htonl(static_cast<uint32_t>(value >> 32));
}
uint16_t NetworkToHost16(uint16_t value) { return ntohs(value); }
uint32_t NetworkToHost32(uint32_t value) { return ntohl(value); }
uint64_t NetworkToHost64(uint64_t value) {
    return (static_cast<uint64_t>(ntohl(static_cast<uint32_t>(value))) << 32) |
           ntohl(static_cast<uint32_t>(value >> 32));
}

} // namespace NetUtils

} // namespace Cortex::Network
