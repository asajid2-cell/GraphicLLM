// NetPacket.cpp
// Network packet serialization implementation.

#include "NetPacket.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Cortex::Network {

// ============================================================================
// BitWriter Implementation
// ============================================================================

BitWriter::BitWriter(size_t initialCapacity) {
    m_data.reserve(initialCapacity);
}

void BitWriter::EnsureCapacity(size_t bits) {
    size_t neededBytes = (m_bitPosition + bits + 7) / 8;
    if (neededBytes > m_data.size()) {
        m_data.resize(neededBytes, 0);
    }
}

void BitWriter::WriteBits(uint32_t value, int numBits) {
    if (numBits <= 0 || numBits > 32) return;

    EnsureCapacity(numBits);

    for (int i = 0; i < numBits; ++i) {
        size_t byteIndex = m_bitPosition / 8;
        size_t bitIndex = m_bitPosition % 8;

        if (value & (1u << i)) {
            m_data[byteIndex] |= (1u << bitIndex);
        }

        m_bitPosition++;
    }
}

void BitWriter::WriteBit(bool value) {
    WriteBits(value ? 1 : 0, 1);
}

void BitWriter::WriteUInt8(uint8_t value) {
    AlignToByte();
    EnsureCapacity(8);
    m_data[m_bitPosition / 8] = value;
    m_bitPosition += 8;
}

void BitWriter::WriteUInt16(uint16_t value) {
    AlignToByte();
    EnsureCapacity(16);
    size_t bytePos = m_bitPosition / 8;
    m_data[bytePos] = static_cast<uint8_t>(value & 0xFF);
    m_data[bytePos + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    m_bitPosition += 16;
}

void BitWriter::WriteUInt32(uint32_t value) {
    AlignToByte();
    EnsureCapacity(32);
    size_t bytePos = m_bitPosition / 8;
    for (int i = 0; i < 4; ++i) {
        m_data[bytePos + i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
    }
    m_bitPosition += 32;
}

void BitWriter::WriteUInt64(uint64_t value) {
    AlignToByte();
    EnsureCapacity(64);
    size_t bytePos = m_bitPosition / 8;
    for (int i = 0; i < 8; ++i) {
        m_data[bytePos + i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
    }
    m_bitPosition += 64;
}

void BitWriter::WriteInt8(int8_t value) {
    WriteUInt8(static_cast<uint8_t>(value));
}

void BitWriter::WriteInt16(int16_t value) {
    WriteUInt16(static_cast<uint16_t>(value));
}

void BitWriter::WriteInt32(int32_t value) {
    WriteUInt32(static_cast<uint32_t>(value));
}

void BitWriter::WriteInt64(int64_t value) {
    WriteUInt64(static_cast<uint64_t>(value));
}

void BitWriter::WriteFloat(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    WriteUInt32(bits);
}

void BitWriter::WriteDouble(double value) {
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    WriteUInt64(bits);
}

void BitWriter::WriteBool(bool value) {
    WriteBit(value);
}

void BitWriter::WriteVarUInt(uint64_t value) {
    while (value >= 0x80) {
        WriteUInt8(static_cast<uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    WriteUInt8(static_cast<uint8_t>(value));
}

void BitWriter::WriteVarInt(int64_t value) {
    uint64_t zigzag = (static_cast<uint64_t>(value) << 1) ^ (value >> 63);
    WriteVarUInt(zigzag);
}

void BitWriter::WriteString(const std::string& value) {
    WriteVarUInt(value.size());
    WriteBytes(reinterpret_cast<const uint8_t*>(value.data()), value.size());
}

void BitWriter::WriteFixedString(const std::string& value, size_t maxLength) {
    size_t writeLen = std::min(value.size(), maxLength);
    WriteBytes(reinterpret_cast<const uint8_t*>(value.data()), writeLen);

    // Pad with zeros
    for (size_t i = writeLen; i < maxLength; ++i) {
        WriteUInt8(0);
    }
}

void BitWriter::WriteBytes(const uint8_t* data, size_t size) {
    AlignToByte();
    EnsureCapacity(size * 8);
    std::memcpy(m_data.data() + m_bitPosition / 8, data, size);
    m_bitPosition += size * 8;
}

void BitWriter::WriteBytes(const std::vector<uint8_t>& data) {
    WriteBytes(data.data(), data.size());
}

void BitWriter::WriteVec2(const glm::vec2& value) {
    WriteFloat(value.x);
    WriteFloat(value.y);
}

void BitWriter::WriteVec3(const glm::vec3& value) {
    WriteFloat(value.x);
    WriteFloat(value.y);
    WriteFloat(value.z);
}

void BitWriter::WriteVec4(const glm::vec4& value) {
    WriteFloat(value.x);
    WriteFloat(value.y);
    WriteFloat(value.z);
    WriteFloat(value.w);
}

void BitWriter::WriteQuat(const glm::quat& value) {
    WriteFloat(value.x);
    WriteFloat(value.y);
    WriteFloat(value.z);
    WriteFloat(value.w);
}

void BitWriter::WriteMat4(const glm::mat4& value) {
    for (int i = 0; i < 4; ++i) {
        WriteVec4(value[i]);
    }
}

void BitWriter::WriteCompressedFloat(float value, float min, float max, int bits) {
    value = std::clamp(value, min, max);
    float normalized = (value - min) / (max - min);
    uint32_t maxValue = (1u << bits) - 1;
    uint32_t intValue = static_cast<uint32_t>(normalized * maxValue + 0.5f);
    WriteBits(intValue, bits);
}

void BitWriter::WriteNormalizedFloat(float value, int bits) {
    WriteCompressedFloat(value, 0.0f, 1.0f, bits);
}

void BitWriter::WriteSignedNormalizedFloat(float value, int bits) {
    WriteCompressedFloat(value, -1.0f, 1.0f, bits);
}

void BitWriter::WriteCompressedPosition(const glm::vec3& pos, float maxRange) {
    WriteCompressedFloat(pos.x, -maxRange, maxRange, 20);
    WriteCompressedFloat(pos.y, -maxRange, maxRange, 20);
    WriteCompressedFloat(pos.z, -maxRange, maxRange, 20);
}

void BitWriter::WriteCompressedRotation(const glm::quat& rot) {
    // Use smallest three representation
    glm::quat q = glm::normalize(rot);

    // Find largest component
    float absMax = std::abs(q.x);
    int maxIndex = 0;
    if (std::abs(q.y) > absMax) { absMax = std::abs(q.y); maxIndex = 1; }
    if (std::abs(q.z) > absMax) { absMax = std::abs(q.z); maxIndex = 2; }
    if (std::abs(q.w) > absMax) { absMax = std::abs(q.w); maxIndex = 3; }

    // Ensure largest component is positive
    float sign = (maxIndex == 0 ? q.x : (maxIndex == 1 ? q.y : (maxIndex == 2 ? q.z : q.w))) < 0 ? -1.0f : 1.0f;
    q.x *= sign;
    q.y *= sign;
    q.z *= sign;
    q.w *= sign;

    WriteBits(maxIndex, 2);

    // Write the three smallest components
    float components[3];
    int idx = 0;
    if (maxIndex != 0) components[idx++] = q.x;
    if (maxIndex != 1) components[idx++] = q.y;
    if (maxIndex != 2) components[idx++] = q.z;
    if (maxIndex != 3) components[idx++] = q.w;

    for (int i = 0; i < 3; ++i) {
        WriteSignedNormalizedFloat(components[i], 10);
    }
}

void BitWriter::AlignToByte() {
    if (m_bitPosition % 8 != 0) {
        m_bitPosition = ((m_bitPosition / 8) + 1) * 8;
        EnsureCapacity(0);
    }
}

void BitWriter::Reset() {
    m_data.clear();
    m_bitPosition = 0;
}

// ============================================================================
// BitReader Implementation
// ============================================================================

BitReader::BitReader(const uint8_t* data, size_t size)
    : m_data(data), m_bitSize(size * 8) {}

BitReader::BitReader(const std::vector<uint8_t>& data)
    : m_data(data.data()), m_bitSize(data.size() * 8) {}

uint32_t BitReader::ReadBits(int numBits) {
    if (numBits <= 0 || numBits > 32) {
        m_error = true;
        return 0;
    }

    if (m_bitPosition + numBits > m_bitSize) {
        m_error = true;
        return 0;
    }

    uint32_t value = 0;
    for (int i = 0; i < numBits; ++i) {
        size_t byteIndex = m_bitPosition / 8;
        size_t bitIndex = m_bitPosition % 8;

        if (m_data[byteIndex] & (1u << bitIndex)) {
            value |= (1u << i);
        }

        m_bitPosition++;
    }

    return value;
}

bool BitReader::ReadBit() {
    return ReadBits(1) != 0;
}

uint8_t BitReader::ReadUInt8() {
    AlignToByte();
    if (m_bitPosition + 8 > m_bitSize) {
        m_error = true;
        return 0;
    }
    uint8_t value = m_data[m_bitPosition / 8];
    m_bitPosition += 8;
    return value;
}

uint16_t BitReader::ReadUInt16() {
    AlignToByte();
    if (m_bitPosition + 16 > m_bitSize) {
        m_error = true;
        return 0;
    }
    size_t bytePos = m_bitPosition / 8;
    uint16_t value = static_cast<uint16_t>(m_data[bytePos]) |
                     (static_cast<uint16_t>(m_data[bytePos + 1]) << 8);
    m_bitPosition += 16;
    return value;
}

uint32_t BitReader::ReadUInt32() {
    AlignToByte();
    if (m_bitPosition + 32 > m_bitSize) {
        m_error = true;
        return 0;
    }
    size_t bytePos = m_bitPosition / 8;
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<uint32_t>(m_data[bytePos + i]) << (i * 8);
    }
    m_bitPosition += 32;
    return value;
}

uint64_t BitReader::ReadUInt64() {
    AlignToByte();
    if (m_bitPosition + 64 > m_bitSize) {
        m_error = true;
        return 0;
    }
    size_t bytePos = m_bitPosition / 8;
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<uint64_t>(m_data[bytePos + i]) << (i * 8);
    }
    m_bitPosition += 64;
    return value;
}

int8_t BitReader::ReadInt8() {
    return static_cast<int8_t>(ReadUInt8());
}

int16_t BitReader::ReadInt16() {
    return static_cast<int16_t>(ReadUInt16());
}

int32_t BitReader::ReadInt32() {
    return static_cast<int32_t>(ReadUInt32());
}

int64_t BitReader::ReadInt64() {
    return static_cast<int64_t>(ReadUInt64());
}

float BitReader::ReadFloat() {
    uint32_t bits = ReadUInt32();
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

double BitReader::ReadDouble() {
    uint64_t bits = ReadUInt64();
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool BitReader::ReadBool() {
    return ReadBit();
}

uint64_t BitReader::ReadVarUInt() {
    uint64_t value = 0;
    int shift = 0;
    uint8_t byte;

    do {
        if (shift >= 64) {
            m_error = true;
            return 0;
        }
        byte = ReadUInt8();
        value |= static_cast<uint64_t>(byte & 0x7F) << shift;
        shift += 7;
    } while (byte & 0x80);

    return value;
}

int64_t BitReader::ReadVarInt() {
    uint64_t zigzag = ReadVarUInt();
    return static_cast<int64_t>((zigzag >> 1) ^ -(static_cast<int64_t>(zigzag) & 1));
}

std::string BitReader::ReadString() {
    size_t length = static_cast<size_t>(ReadVarUInt());
    if (m_error || length > 65536) {
        m_error = true;
        return "";
    }

    std::string result(length, '\0');
    ReadBytes(reinterpret_cast<uint8_t*>(result.data()), length);
    return result;
}

std::string BitReader::ReadFixedString(size_t maxLength) {
    std::string result(maxLength, '\0');
    ReadBytes(reinterpret_cast<uint8_t*>(result.data()), maxLength);

    // Trim null characters
    size_t len = result.find('\0');
    if (len != std::string::npos) {
        result.resize(len);
    }
    return result;
}

void BitReader::ReadBytes(uint8_t* data, size_t size) {
    AlignToByte();
    if (m_bitPosition + size * 8 > m_bitSize) {
        m_error = true;
        return;
    }
    std::memcpy(data, m_data + m_bitPosition / 8, size);
    m_bitPosition += size * 8;
}

std::vector<uint8_t> BitReader::ReadBytes(size_t size) {
    std::vector<uint8_t> result(size);
    ReadBytes(result.data(), size);
    return result;
}

glm::vec2 BitReader::ReadVec2() {
    return glm::vec2(ReadFloat(), ReadFloat());
}

glm::vec3 BitReader::ReadVec3() {
    return glm::vec3(ReadFloat(), ReadFloat(), ReadFloat());
}

glm::vec4 BitReader::ReadVec4() {
    return glm::vec4(ReadFloat(), ReadFloat(), ReadFloat(), ReadFloat());
}

glm::quat BitReader::ReadQuat() {
    return glm::quat(ReadFloat(), ReadFloat(), ReadFloat(), ReadFloat());
}

glm::mat4 BitReader::ReadMat4() {
    glm::mat4 result;
    for (int i = 0; i < 4; ++i) {
        result[i] = ReadVec4();
    }
    return result;
}

float BitReader::ReadCompressedFloat(float min, float max, int bits) {
    uint32_t maxValue = (1u << bits) - 1;
    uint32_t intValue = ReadBits(bits);
    float normalized = static_cast<float>(intValue) / static_cast<float>(maxValue);
    return min + normalized * (max - min);
}

float BitReader::ReadNormalizedFloat(int bits) {
    return ReadCompressedFloat(0.0f, 1.0f, bits);
}

float BitReader::ReadSignedNormalizedFloat(int bits) {
    return ReadCompressedFloat(-1.0f, 1.0f, bits);
}

glm::vec3 BitReader::ReadCompressedPosition(float maxRange) {
    return glm::vec3(
        ReadCompressedFloat(-maxRange, maxRange, 20),
        ReadCompressedFloat(-maxRange, maxRange, 20),
        ReadCompressedFloat(-maxRange, maxRange, 20)
    );
}

glm::quat BitReader::ReadCompressedRotation() {
    int maxIndex = ReadBits(2);

    float components[3];
    for (int i = 0; i < 3; ++i) {
        components[i] = ReadSignedNormalizedFloat(10);
    }

    // Reconstruct quaternion
    float sum = components[0] * components[0] +
                 components[1] * components[1] +
                 components[2] * components[2];
    float largest = std::sqrt(std::max(0.0f, 1.0f - sum));

    glm::quat q;
    int idx = 0;
    q.x = (maxIndex == 0) ? largest : components[idx++];
    q.y = (maxIndex == 1) ? largest : components[idx++];
    q.z = (maxIndex == 2) ? largest : components[idx++];
    q.w = (maxIndex == 3) ? largest : components[idx++];

    return glm::normalize(q);
}

void BitReader::AlignToByte() {
    if (m_bitPosition % 8 != 0) {
        m_bitPosition = ((m_bitPosition / 8) + 1) * 8;
    }
}

void BitReader::Reset() {
    m_bitPosition = 0;
    m_error = false;
}

// ============================================================================
// NetPacket Implementation
// ============================================================================

NetPacket::NetPacket()
    : m_writer(), m_reader(nullptr, 0) {}

NetPacket::NetPacket(const std::vector<uint8_t>& data)
    : m_writer(), m_reader(data), m_data(data), m_isWriting(false) {}

NetPacket::NetPacket(const uint8_t* data, size_t size)
    : m_writer(), m_reader(data, size), m_data(data, data + size), m_isWriting(false) {}

void NetPacket::SetData(const std::vector<uint8_t>& data) {
    m_data = data;
    m_reader = BitReader(m_data);
    m_isWriting = false;
}

void NetPacket::SetData(const uint8_t* data, size_t size) {
    m_data.assign(data, data + size);
    m_reader = BitReader(m_data);
    m_isWriting = false;
}

const std::vector<uint8_t>& NetPacket::GetData() const {
    if (m_isWriting) {
        return m_writer.GetData();
    }
    return m_data;
}

std::vector<uint8_t> NetPacket::TakeData() {
    if (m_isWriting) {
        return m_writer.TakeData();
    }
    return std::move(m_data);
}

void NetPacket::BeginWrite() {
    m_writer.Reset();
    m_isWriting = true;
}

void NetPacket::EndWrite() {
    m_data = m_writer.GetData();
    m_isWriting = false;
}

void NetPacket::BeginRead() {
    m_reader = BitReader(m_data);
    m_isWriting = false;
}

size_t NetPacket::GetSize() const {
    if (m_isWriting) {
        return m_writer.GetByteSize();
    }
    return m_data.size();
}

bool NetPacket::IsValid() const {
    if (m_isWriting) {
        return true;
    }
    return !m_reader.HasError();
}

// ============================================================================
// PacketPool Implementation
// ============================================================================

PacketPool::PacketPool(size_t initialSize, size_t maxSize)
    : m_maxSize(maxSize) {
    m_pool.reserve(initialSize);
    for (size_t i = 0; i < initialSize; ++i) {
        m_pool.push_back(new NetPacket());
    }
}

PacketPool::~PacketPool() {
    for (auto* packet : m_pool) {
        delete packet;
    }
}

NetPacket* PacketPool::Acquire() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_pool.empty()) {
        m_activeCount++;
        return new NetPacket();
    }

    NetPacket* packet = m_pool.back();
    m_pool.pop_back();
    m_activeCount++;
    return packet;
}

void PacketPool::Release(NetPacket* packet) {
    if (!packet) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_pool.size() < m_maxSize) {
        packet->BeginWrite();  // Reset for reuse
        m_pool.push_back(packet);
    } else {
        delete packet;
    }

    m_activeCount--;
}

// ============================================================================
// MessageFactory Implementation
// ============================================================================

MessageFactory& MessageFactory::Instance() {
    static MessageFactory instance;
    return instance;
}

MessageFactory::MessageFactory() {
    // Register built-in message types
    Register<HeartbeatMessage>();
    Register<TimeSyncMessage>();
    Register<ServerInfoMessage>();
    Register<SpawnEntityMessage>();
    Register<DestroyEntityMessage>();
    Register<EntityStateMessage>();
    Register<PlayerInputMessage>();
    Register<PlayerStateMessage>();
    Register<ChatMessageNet>();
    Register<WorldStateMessage>();
    Register<EntityRPCMessage>();
}

std::unique_ptr<NetMessage> MessageFactory::Create(NetMessageType type) {
    return Create(static_cast<uint16_t>(type));
}

std::unique_ptr<NetMessage> MessageFactory::Create(uint16_t typeId) {
    auto it = m_creators.find(typeId);
    if (it != m_creators.end()) {
        return it->second();
    }
    return nullptr;
}

std::vector<uint8_t> MessageFactory::Serialize(const NetMessage& message) {
    BitWriter writer;
    writer.WriteUInt16(static_cast<uint16_t>(message.GetType()));
    message.Serialize(writer);
    return writer.TakeData();
}

std::unique_ptr<NetMessage> MessageFactory::Deserialize(const std::vector<uint8_t>& data) {
    return Deserialize(data.data(), data.size());
}

std::unique_ptr<NetMessage> MessageFactory::Deserialize(const uint8_t* data, size_t size) {
    if (size < 2) return nullptr;

    BitReader reader(data, size);
    uint16_t typeId = reader.ReadUInt16();

    auto message = Instance().Create(typeId);
    if (message) {
        message->Deserialize(reader);
        if (reader.HasError()) {
            return nullptr;
        }
    }

    return message;
}

// ============================================================================
// MessageHandler Implementation
// ============================================================================

void MessageHandler::Handle(uint32_t connectionId, const NetMessage& message) {
    uint16_t typeId = static_cast<uint16_t>(message.GetType());
    auto it = m_handlers.find(typeId);
    if (it != m_handlers.end()) {
        it->second(connectionId, message);
    }
}

void MessageHandler::Handle(uint32_t connectionId, const std::vector<uint8_t>& data) {
    auto message = MessageFactory::Deserialize(data);
    if (message) {
        Handle(connectionId, *message);
    }
}

bool MessageHandler::HasHandler(NetMessageType type) const {
    return m_handlers.find(static_cast<uint16_t>(type)) != m_handlers.end();
}

// ============================================================================
// WorldSnapshot Implementation
// ============================================================================

void WorldSnapshot::Serialize(BitWriter& writer) const {
    writer.WriteUInt32(tick);
    writer.WriteUInt64(timestamp);
    writer.WriteVarUInt(entities.size());

    for (const auto& entity : entities) {
        writer.WriteUInt32(entity.networkId);
        writer.WriteUInt32(entity.tick);
        writer.WriteCompressedPosition(entity.position);
        writer.WriteCompressedRotation(entity.rotation);
        writer.WriteVec3(entity.velocity);
        writer.WriteVarUInt(entity.componentData.size());
        writer.WriteBytes(entity.componentData);
    }
}

void WorldSnapshot::Deserialize(BitReader& reader) {
    tick = reader.ReadUInt32();
    timestamp = reader.ReadUInt64();
    size_t count = static_cast<size_t>(reader.ReadVarUInt());

    entities.resize(count);
    for (auto& entity : entities) {
        entity.networkId = reader.ReadUInt32();
        entity.tick = reader.ReadUInt32();
        entity.position = reader.ReadCompressedPosition();
        entity.rotation = reader.ReadCompressedRotation();
        entity.velocity = reader.ReadVec3();
        size_t dataSize = static_cast<size_t>(reader.ReadVarUInt());
        entity.componentData = reader.ReadBytes(dataSize);
    }
}

// ============================================================================
// SnapshotBuffer Implementation
// ============================================================================

SnapshotBuffer::SnapshotBuffer(size_t maxSnapshots)
    : m_maxSnapshots(maxSnapshots) {
    m_snapshots.reserve(maxSnapshots);
}

void SnapshotBuffer::AddSnapshot(const WorldSnapshot& snapshot) {
    if (m_snapshots.size() >= m_maxSnapshots) {
        m_snapshots.erase(m_snapshots.begin());
    }
    m_snapshots.push_back(snapshot);
}

const WorldSnapshot* SnapshotBuffer::GetSnapshot(uint32_t tick) const {
    for (const auto& snapshot : m_snapshots) {
        if (snapshot.tick == tick) {
            return &snapshot;
        }
    }
    return nullptr;
}

bool SnapshotBuffer::GetInterpolationSnapshots(uint32_t tick, const WorldSnapshot*& before,
                                                 const WorldSnapshot*& after, float& t) const {
    before = nullptr;
    after = nullptr;

    for (size_t i = 0; i < m_snapshots.size(); ++i) {
        if (m_snapshots[i].tick > tick) {
            if (i > 0) {
                before = &m_snapshots[i - 1];
                after = &m_snapshots[i];
                t = static_cast<float>(tick - before->tick) /
                    static_cast<float>(after->tick - before->tick);
                return true;
            }
            break;
        }
    }

    return false;
}

const WorldSnapshot* SnapshotBuffer::GetLatestSnapshot() const {
    if (m_snapshots.empty()) return nullptr;
    return &m_snapshots.back();
}

void SnapshotBuffer::ClearBefore(uint32_t tick) {
    m_snapshots.erase(
        std::remove_if(m_snapshots.begin(), m_snapshots.end(),
                        [tick](const WorldSnapshot& s) { return s.tick < tick; }),
        m_snapshots.end()
    );
}

} // namespace Cortex::Network
