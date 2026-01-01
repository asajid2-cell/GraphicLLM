#pragma once

// NetPacket.h
// Network packet serialization and deserialization.
// Provides efficient binary encoding for network messages.

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace Cortex::Network {

// ============================================================================
// Bit Writer/Reader for Compact Serialization
// ============================================================================

class BitWriter {
public:
    BitWriter(size_t initialCapacity = 256);

    // Write bits
    void WriteBits(uint32_t value, int numBits);
    void WriteBit(bool value);

    // Write aligned values
    void WriteUInt8(uint8_t value);
    void WriteUInt16(uint16_t value);
    void WriteUInt32(uint32_t value);
    void WriteUInt64(uint64_t value);
    void WriteInt8(int8_t value);
    void WriteInt16(int16_t value);
    void WriteInt32(int32_t value);
    void WriteInt64(int64_t value);
    void WriteFloat(float value);
    void WriteDouble(double value);
    void WriteBool(bool value);

    // Write variable-length values
    void WriteVarUInt(uint64_t value);
    void WriteVarInt(int64_t value);

    // Write strings
    void WriteString(const std::string& value);
    void WriteFixedString(const std::string& value, size_t maxLength);

    // Write raw bytes
    void WriteBytes(const uint8_t* data, size_t size);
    void WriteBytes(const std::vector<uint8_t>& data);

    // Write GLM types
    void WriteVec2(const glm::vec2& value);
    void WriteVec3(const glm::vec3& value);
    void WriteVec4(const glm::vec4& value);
    void WriteQuat(const glm::quat& value);
    void WriteMat4(const glm::mat4& value);

    // Compressed float writes
    void WriteCompressedFloat(float value, float min, float max, int bits);
    void WriteNormalizedFloat(float value, int bits);  // 0-1 range
    void WriteSignedNormalizedFloat(float value, int bits);  // -1 to 1

    // Compressed position/rotation
    void WriteCompressedPosition(const glm::vec3& pos, float maxRange = 1000.0f);
    void WriteCompressedRotation(const glm::quat& rot);

    // Align to byte boundary
    void AlignToByte();

    // Get result
    const std::vector<uint8_t>& GetData() const { return m_data; }
    std::vector<uint8_t> TakeData() { return std::move(m_data); }
    size_t GetBitPosition() const { return m_bitPosition; }
    size_t GetByteSize() const { return (m_bitPosition + 7) / 8; }

    // Reset
    void Reset();

private:
    void EnsureCapacity(size_t bits);

    std::vector<uint8_t> m_data;
    size_t m_bitPosition = 0;
};

class BitReader {
public:
    BitReader(const uint8_t* data, size_t size);
    BitReader(const std::vector<uint8_t>& data);

    // Read bits
    uint32_t ReadBits(int numBits);
    bool ReadBit();

    // Read aligned values
    uint8_t ReadUInt8();
    uint16_t ReadUInt16();
    uint32_t ReadUInt32();
    uint64_t ReadUInt64();
    int8_t ReadInt8();
    int16_t ReadInt16();
    int32_t ReadInt32();
    int64_t ReadInt64();
    float ReadFloat();
    double ReadDouble();
    bool ReadBool();

    // Read variable-length values
    uint64_t ReadVarUInt();
    int64_t ReadVarInt();

    // Read strings
    std::string ReadString();
    std::string ReadFixedString(size_t maxLength);

    // Read raw bytes
    void ReadBytes(uint8_t* data, size_t size);
    std::vector<uint8_t> ReadBytes(size_t size);

    // Read GLM types
    glm::vec2 ReadVec2();
    glm::vec3 ReadVec3();
    glm::vec4 ReadVec4();
    glm::quat ReadQuat();
    glm::mat4 ReadMat4();

    // Compressed float reads
    float ReadCompressedFloat(float min, float max, int bits);
    float ReadNormalizedFloat(int bits);
    float ReadSignedNormalizedFloat(int bits);

    // Compressed position/rotation
    glm::vec3 ReadCompressedPosition(float maxRange = 1000.0f);
    glm::quat ReadCompressedRotation();

    // Align to byte boundary
    void AlignToByte();

    // State
    bool IsValid() const { return !m_error; }
    bool HasError() const { return m_error; }
    bool IsAtEnd() const { return m_bitPosition >= m_bitSize; }
    size_t GetBitPosition() const { return m_bitPosition; }
    size_t GetRemainingBits() const { return m_bitSize - m_bitPosition; }

    // Reset
    void Reset();

private:
    const uint8_t* m_data;
    size_t m_bitSize;
    size_t m_bitPosition = 0;
    bool m_error = false;
};

// ============================================================================
// Network Packet
// ============================================================================

class NetPacket {
public:
    NetPacket();
    NetPacket(const std::vector<uint8_t>& data);
    NetPacket(const uint8_t* data, size_t size);

    // Write interface
    BitWriter& GetWriter() { return m_writer; }

    // Read interface
    BitReader& GetReader() { return m_reader; }

    // Get/Set raw data
    void SetData(const std::vector<uint8_t>& data);
    void SetData(const uint8_t* data, size_t size);
    const std::vector<uint8_t>& GetData() const;
    std::vector<uint8_t> TakeData();

    // Switch between read/write modes
    void BeginWrite();
    void EndWrite();
    void BeginRead();

    // Packet metadata
    void SetChannel(uint8_t channel) { m_channel = channel; }
    uint8_t GetChannel() const { return m_channel; }

    // Size
    size_t GetSize() const;

    // Validation
    bool IsValid() const;

private:
    BitWriter m_writer;
    BitReader m_reader;
    std::vector<uint8_t> m_data;
    uint8_t m_channel = 0;
    bool m_isWriting = true;
};

// ============================================================================
// Serialization Traits
// ============================================================================

template<typename T>
struct NetSerialize {
    static void Write(BitWriter& writer, const T& value) {
        static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable or specialized");
        writer.WriteBytes(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
    }

    static T Read(BitReader& reader) {
        static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable or specialized");
        T value;
        reader.ReadBytes(reinterpret_cast<uint8_t*>(&value), sizeof(T));
        return value;
    }
};

// Specializations for common types
template<> struct NetSerialize<bool> {
    static void Write(BitWriter& writer, bool value) { writer.WriteBool(value); }
    static bool Read(BitReader& reader) { return reader.ReadBool(); }
};

template<> struct NetSerialize<uint8_t> {
    static void Write(BitWriter& writer, uint8_t value) { writer.WriteUInt8(value); }
    static uint8_t Read(BitReader& reader) { return reader.ReadUInt8(); }
};

template<> struct NetSerialize<uint16_t> {
    static void Write(BitWriter& writer, uint16_t value) { writer.WriteUInt16(value); }
    static uint16_t Read(BitReader& reader) { return reader.ReadUInt16(); }
};

template<> struct NetSerialize<uint32_t> {
    static void Write(BitWriter& writer, uint32_t value) { writer.WriteUInt32(value); }
    static uint32_t Read(BitReader& reader) { return reader.ReadUInt32(); }
};

template<> struct NetSerialize<uint64_t> {
    static void Write(BitWriter& writer, uint64_t value) { writer.WriteUInt64(value); }
    static uint64_t Read(BitReader& reader) { return reader.ReadUInt64(); }
};

template<> struct NetSerialize<int8_t> {
    static void Write(BitWriter& writer, int8_t value) { writer.WriteInt8(value); }
    static int8_t Read(BitReader& reader) { return reader.ReadInt8(); }
};

template<> struct NetSerialize<int16_t> {
    static void Write(BitWriter& writer, int16_t value) { writer.WriteInt16(value); }
    static int16_t Read(BitReader& reader) { return reader.ReadInt16(); }
};

template<> struct NetSerialize<int32_t> {
    static void Write(BitWriter& writer, int32_t value) { writer.WriteInt32(value); }
    static int32_t Read(BitReader& reader) { return reader.ReadInt32(); }
};

template<> struct NetSerialize<int64_t> {
    static void Write(BitWriter& writer, int64_t value) { writer.WriteInt64(value); }
    static int64_t Read(BitReader& reader) { return reader.ReadInt64(); }
};

template<> struct NetSerialize<float> {
    static void Write(BitWriter& writer, float value) { writer.WriteFloat(value); }
    static float Read(BitReader& reader) { return reader.ReadFloat(); }
};

template<> struct NetSerialize<double> {
    static void Write(BitWriter& writer, double value) { writer.WriteDouble(value); }
    static double Read(BitReader& reader) { return reader.ReadDouble(); }
};

template<> struct NetSerialize<std::string> {
    static void Write(BitWriter& writer, const std::string& value) { writer.WriteString(value); }
    static std::string Read(BitReader& reader) { return reader.ReadString(); }
};

template<> struct NetSerialize<glm::vec2> {
    static void Write(BitWriter& writer, const glm::vec2& value) { writer.WriteVec2(value); }
    static glm::vec2 Read(BitReader& reader) { return reader.ReadVec2(); }
};

template<> struct NetSerialize<glm::vec3> {
    static void Write(BitWriter& writer, const glm::vec3& value) { writer.WriteVec3(value); }
    static glm::vec3 Read(BitReader& reader) { return reader.ReadVec3(); }
};

template<> struct NetSerialize<glm::vec4> {
    static void Write(BitWriter& writer, const glm::vec4& value) { writer.WriteVec4(value); }
    static glm::vec4 Read(BitReader& reader) { return reader.ReadVec4(); }
};

template<> struct NetSerialize<glm::quat> {
    static void Write(BitWriter& writer, const glm::quat& value) { writer.WriteQuat(value); }
    static glm::quat Read(BitReader& reader) { return reader.ReadQuat(); }
};

// Vector serialization
template<typename T>
struct NetSerialize<std::vector<T>> {
    static void Write(BitWriter& writer, const std::vector<T>& value) {
        writer.WriteVarUInt(value.size());
        for (const auto& item : value) {
            NetSerialize<T>::Write(writer, item);
        }
    }

    static std::vector<T> Read(BitReader& reader) {
        std::vector<T> result;
        size_t size = static_cast<size_t>(reader.ReadVarUInt());
        result.reserve(size);
        for (size_t i = 0; i < size; ++i) {
            result.push_back(NetSerialize<T>::Read(reader));
        }
        return result;
    }
};

// ============================================================================
// Delta Compression
// ============================================================================

namespace DeltaCompression {

// Compute delta between two values
template<typename T>
std::vector<uint8_t> ComputeDelta(const T& baseline, const T& current);

// Apply delta to baseline
template<typename T>
T ApplyDelta(const T& baseline, const std::vector<uint8_t>& delta);

// Specialized delta compression for transforms
struct TransformDelta {
    bool hasPosition = false;
    bool hasRotation = false;
    bool hasScale = false;
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 scale;
};

void WriteTransformDelta(BitWriter& writer, const TransformDelta& delta);
TransformDelta ReadTransformDelta(BitReader& reader);

} // namespace DeltaCompression

// ============================================================================
// Packet Pool (for avoiding allocations)
// ============================================================================

class PacketPool {
public:
    PacketPool(size_t initialSize = 64, size_t maxSize = 1024);
    ~PacketPool();

    // Get a packet from the pool
    NetPacket* Acquire();

    // Return a packet to the pool
    void Release(NetPacket* packet);

    // Statistics
    size_t GetActiveCount() const { return m_activeCount; }
    size_t GetPoolSize() const { return m_pool.size(); }

private:
    std::vector<NetPacket*> m_pool;
    size_t m_maxSize;
    size_t m_activeCount = 0;
    std::mutex m_mutex;
};

} // namespace Cortex::Network
