#pragma once

// SoundBuffer.h
// Audio data container for WAV/OGG file loading.
// Supports streaming for long audio files.

#include <xaudio2.h>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace Cortex::Audio {

// Audio file format
enum class AudioFormat {
    Unknown,
    WAV,
    OGG,
    MP3
};

// Sound buffer load mode
enum class LoadMode {
    Immediate,      // Load entire file into memory
    Streaming       // Stream from disk (for long audio)
};

// Sound buffer metadata
struct SoundMetadata {
    std::string name;
    std::string filePath;
    AudioFormat format = AudioFormat::Unknown;
    uint32_t sampleRate = 44100;
    uint16_t channels = 2;
    uint16_t bitsPerSample = 16;
    float durationSeconds = 0.0f;
    size_t dataSize = 0;
    bool isStreaming = false;
};

class SoundBuffer {
public:
    SoundBuffer();
    ~SoundBuffer();

    // Loading
    bool LoadFromFile(const std::string& path, LoadMode mode = LoadMode::Immediate);
    bool LoadFromMemory(const uint8_t* data, size_t size, AudioFormat format);

    // Unload
    void Unload();

    // Access
    bool IsValid() const { return m_valid; }
    bool IsStreaming() const { return m_metadata.isStreaming; }

    const uint8_t* GetData() const { return m_data.data(); }
    size_t GetDataSize() const { return m_data.size(); }
    WAVEFORMATEX GetFormat() const { return m_waveFormat; }
    const SoundMetadata& GetMetadata() const { return m_metadata; }

    float GetDuration() const { return m_metadata.durationSeconds; }
    uint32_t GetSampleRate() const { return m_metadata.sampleRate; }
    uint16_t GetChannels() const { return m_metadata.channels; }

    // Streaming interface
    bool IsStreamingEnabled() const { return m_streamingEnabled; }
    size_t ReadStreamChunk(uint8_t* buffer, size_t maxBytes);
    bool SeekStream(float timeSeconds);
    float GetStreamPosition() const;
    bool IsStreamEnd() const;
    void ResetStream();

private:
    // File loading
    bool LoadWAV(const std::string& path);
    bool LoadWAVFromMemory(const uint8_t* data, size_t size);
    bool LoadOGG(const std::string& path);
    bool LoadOGGFromMemory(const uint8_t* data, size_t size);

    // Detect format from extension
    AudioFormat DetectFormat(const std::string& path) const;

    // Convert to standard format
    void ConvertToStandardFormat();

private:
    std::vector<uint8_t> m_data;
    WAVEFORMATEX m_waveFormat;
    SoundMetadata m_metadata;

    // Streaming state
    bool m_streamingEnabled = false;
    std::string m_streamPath;
    size_t m_streamPosition = 0;
    size_t m_streamChunkSize = 65536;  // 64KB chunks

    bool m_valid = false;
};

// WAV file structures
#pragma pack(push, 1)
struct WAVRIFFHeader {
    char riffTag[4];        // "RIFF"
    uint32_t fileSize;
    char waveTag[4];        // "WAVE"
};

struct WAVChunkHeader {
    char chunkId[4];
    uint32_t chunkSize;
};

struct WAVFmtChunk {
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
};
#pragma pack(pop)

// Utility functions
AudioFormat GetAudioFormatFromExtension(const std::string& extension);
std::string GetExtensionFromPath(const std::string& path);
float CalculateDuration(size_t dataSize, uint32_t sampleRate, uint16_t channels, uint16_t bitsPerSample);

} // namespace Cortex::Audio
