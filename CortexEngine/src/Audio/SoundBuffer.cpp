// SoundBuffer.cpp
// Audio buffer loading implementation.

#include "SoundBuffer.h"
#include <fstream>
#include <algorithm>
#include <cstring>

namespace Cortex::Audio {

SoundBuffer::SoundBuffer() {
    memset(&m_waveFormat, 0, sizeof(m_waveFormat));
}

SoundBuffer::~SoundBuffer() {
    Unload();
}

bool SoundBuffer::LoadFromFile(const std::string& path, LoadMode mode) {
    Unload();

    m_metadata.filePath = path;
    m_metadata.format = DetectFormat(path);
    m_metadata.isStreaming = (mode == LoadMode::Streaming);

    if (mode == LoadMode::Streaming) {
        m_streamingEnabled = true;
        m_streamPath = path;
        m_streamPosition = 0;

        // For streaming, we still need to read the header
        // to get format info, but not the full data
    }

    bool success = false;

    switch (m_metadata.format) {
        case AudioFormat::WAV:
            success = LoadWAV(path);
            break;
        case AudioFormat::OGG:
            success = LoadOGG(path);
            break;
        default:
            return false;
    }

    if (success) {
        m_valid = true;
        m_metadata.durationSeconds = CalculateDuration(
            m_data.size(),
            m_metadata.sampleRate,
            m_metadata.channels,
            m_metadata.bitsPerSample
        );
    }

    return success;
}

bool SoundBuffer::LoadFromMemory(const uint8_t* data, size_t size, AudioFormat format) {
    Unload();

    m_metadata.format = format;

    bool success = false;

    switch (format) {
        case AudioFormat::WAV:
            success = LoadWAVFromMemory(data, size);
            break;
        case AudioFormat::OGG:
            success = LoadOGGFromMemory(data, size);
            break;
        default:
            return false;
    }

    if (success) {
        m_valid = true;
        m_metadata.durationSeconds = CalculateDuration(
            m_data.size(),
            m_metadata.sampleRate,
            m_metadata.channels,
            m_metadata.bitsPerSample
        );
    }

    return success;
}

void SoundBuffer::Unload() {
    m_data.clear();
    m_data.shrink_to_fit();
    m_valid = false;
    m_streamingEnabled = false;
    m_streamPosition = 0;
    memset(&m_waveFormat, 0, sizeof(m_waveFormat));
    m_metadata = SoundMetadata();
}

bool SoundBuffer::LoadWAV(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Read RIFF header
    WAVRIFFHeader riffHeader;
    file.read(reinterpret_cast<char*>(&riffHeader), sizeof(riffHeader));

    if (strncmp(riffHeader.riffTag, "RIFF", 4) != 0 ||
        strncmp(riffHeader.waveTag, "WAVE", 4) != 0) {
        return false;
    }

    // Read chunks
    bool foundFmt = false;
    bool foundData = false;

    while (!file.eof() && (!foundFmt || !foundData)) {
        WAVChunkHeader chunkHeader;
        file.read(reinterpret_cast<char*>(&chunkHeader), sizeof(chunkHeader));

        if (file.eof()) break;

        if (strncmp(chunkHeader.chunkId, "fmt ", 4) == 0) {
            // Format chunk
            WAVFmtChunk fmt;
            file.read(reinterpret_cast<char*>(&fmt), sizeof(fmt));

            // Skip extra format bytes if present
            if (chunkHeader.chunkSize > sizeof(fmt)) {
                file.seekg(chunkHeader.chunkSize - sizeof(fmt), std::ios::cur);
            }

            // Fill WAVEFORMATEX
            m_waveFormat.wFormatTag = fmt.audioFormat;
            m_waveFormat.nChannels = fmt.numChannels;
            m_waveFormat.nSamplesPerSec = fmt.sampleRate;
            m_waveFormat.nAvgBytesPerSec = fmt.byteRate;
            m_waveFormat.nBlockAlign = fmt.blockAlign;
            m_waveFormat.wBitsPerSample = fmt.bitsPerSample;
            m_waveFormat.cbSize = 0;

            m_metadata.sampleRate = fmt.sampleRate;
            m_metadata.channels = fmt.numChannels;
            m_metadata.bitsPerSample = fmt.bitsPerSample;

            foundFmt = true;
        }
        else if (strncmp(chunkHeader.chunkId, "data", 4) == 0) {
            // Data chunk
            m_data.resize(chunkHeader.chunkSize);
            file.read(reinterpret_cast<char*>(m_data.data()), chunkHeader.chunkSize);
            m_metadata.dataSize = chunkHeader.chunkSize;
            foundData = true;
        }
        else {
            // Skip unknown chunks
            file.seekg(chunkHeader.chunkSize, std::ios::cur);
        }
    }

    // Extract name from path
    size_t lastSlash = path.find_last_of("/\\");
    size_t lastDot = path.find_last_of('.');
    if (lastSlash != std::string::npos && lastDot != std::string::npos) {
        m_metadata.name = path.substr(lastSlash + 1, lastDot - lastSlash - 1);
    }

    return foundFmt && foundData;
}

bool SoundBuffer::LoadWAVFromMemory(const uint8_t* data, size_t size) {
    if (size < sizeof(WAVRIFFHeader)) {
        return false;
    }

    const uint8_t* ptr = data;
    const uint8_t* end = data + size;

    // Read RIFF header
    const WAVRIFFHeader* riffHeader = reinterpret_cast<const WAVRIFFHeader*>(ptr);
    ptr += sizeof(WAVRIFFHeader);

    if (strncmp(riffHeader->riffTag, "RIFF", 4) != 0 ||
        strncmp(riffHeader->waveTag, "WAVE", 4) != 0) {
        return false;
    }

    bool foundFmt = false;
    bool foundData = false;

    while (ptr < end && (!foundFmt || !foundData)) {
        if (ptr + sizeof(WAVChunkHeader) > end) break;

        const WAVChunkHeader* chunkHeader = reinterpret_cast<const WAVChunkHeader*>(ptr);
        ptr += sizeof(WAVChunkHeader);

        if (strncmp(chunkHeader->chunkId, "fmt ", 4) == 0) {
            if (ptr + sizeof(WAVFmtChunk) > end) break;

            const WAVFmtChunk* fmt = reinterpret_cast<const WAVFmtChunk*>(ptr);

            m_waveFormat.wFormatTag = fmt->audioFormat;
            m_waveFormat.nChannels = fmt->numChannels;
            m_waveFormat.nSamplesPerSec = fmt->sampleRate;
            m_waveFormat.nAvgBytesPerSec = fmt->byteRate;
            m_waveFormat.nBlockAlign = fmt->blockAlign;
            m_waveFormat.wBitsPerSample = fmt->bitsPerSample;
            m_waveFormat.cbSize = 0;

            m_metadata.sampleRate = fmt->sampleRate;
            m_metadata.channels = fmt->numChannels;
            m_metadata.bitsPerSample = fmt->bitsPerSample;

            ptr += chunkHeader->chunkSize;
            foundFmt = true;
        }
        else if (strncmp(chunkHeader->chunkId, "data", 4) == 0) {
            size_t dataSize = std::min(static_cast<size_t>(chunkHeader->chunkSize),
                                       static_cast<size_t>(end - ptr));
            m_data.resize(dataSize);
            memcpy(m_data.data(), ptr, dataSize);
            m_metadata.dataSize = dataSize;
            ptr += chunkHeader->chunkSize;
            foundData = true;
        }
        else {
            ptr += chunkHeader->chunkSize;
        }
    }

    return foundFmt && foundData;
}

bool SoundBuffer::LoadOGG(const std::string& path) {
    // OGG Vorbis loading would require stb_vorbis or similar library
    // For now, provide a stub that returns false

    // In a full implementation:
    // 1. Open file with stb_vorbis_open_filename
    // 2. Get info (channels, sample rate)
    // 3. Decode to PCM
    // 4. Store in m_data

    // Stub: Try to load as WAV fallback
    std::string wavPath = path;
    size_t dotPos = wavPath.find_last_of('.');
    if (dotPos != std::string::npos) {
        wavPath = wavPath.substr(0, dotPos) + ".wav";
        return LoadWAV(wavPath);
    }

    return false;
}

bool SoundBuffer::LoadOGGFromMemory(const uint8_t* data, size_t size) {
    // Stub implementation
    (void)data;
    (void)size;
    return false;
}

AudioFormat SoundBuffer::DetectFormat(const std::string& path) const {
    std::string ext = GetExtensionFromPath(path);
    return GetAudioFormatFromExtension(ext);
}

void SoundBuffer::ConvertToStandardFormat() {
    // Convert to 16-bit stereo 44100Hz if needed
    // This would be implemented for format normalization
}

size_t SoundBuffer::ReadStreamChunk(uint8_t* buffer, size_t maxBytes) {
    if (!m_streamingEnabled || m_streamPath.empty()) {
        return 0;
    }

    std::ifstream file(m_streamPath, std::ios::binary);
    if (!file.is_open()) {
        return 0;
    }

    // Seek to current position (after header)
    // Note: This is simplified - real streaming would need proper header offset
    file.seekg(static_cast<std::streamoff>(m_streamPosition));

    size_t bytesToRead = std::min(maxBytes, m_streamChunkSize);
    file.read(reinterpret_cast<char*>(buffer), bytesToRead);

    size_t bytesRead = static_cast<size_t>(file.gcount());
    m_streamPosition += bytesRead;

    return bytesRead;
}

bool SoundBuffer::SeekStream(float timeSeconds) {
    if (!m_streamingEnabled) {
        return false;
    }

    // Calculate byte position from time
    size_t bytesPerSecond = m_metadata.sampleRate * m_metadata.channels * (m_metadata.bitsPerSample / 8);
    m_streamPosition = static_cast<size_t>(timeSeconds * bytesPerSecond);

    // Clamp to valid range
    if (m_streamPosition > m_metadata.dataSize) {
        m_streamPosition = m_metadata.dataSize;
    }

    return true;
}

float SoundBuffer::GetStreamPosition() const {
    if (!m_streamingEnabled || m_metadata.dataSize == 0) {
        return 0.0f;
    }

    return static_cast<float>(m_streamPosition) / static_cast<float>(m_metadata.dataSize) * m_metadata.durationSeconds;
}

bool SoundBuffer::IsStreamEnd() const {
    return m_streamPosition >= m_metadata.dataSize;
}

void SoundBuffer::ResetStream() {
    m_streamPosition = 0;
}

// Utility functions
AudioFormat GetAudioFormatFromExtension(const std::string& extension) {
    std::string ext = extension;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".wav" || ext == "wav") return AudioFormat::WAV;
    if (ext == ".ogg" || ext == "ogg") return AudioFormat::OGG;
    if (ext == ".mp3" || ext == "mp3") return AudioFormat::MP3;

    return AudioFormat::Unknown;
}

std::string GetExtensionFromPath(const std::string& path) {
    size_t dotPos = path.find_last_of('.');
    if (dotPos != std::string::npos) {
        return path.substr(dotPos);
    }
    return "";
}

float CalculateDuration(size_t dataSize, uint32_t sampleRate, uint16_t channels, uint16_t bitsPerSample) {
    if (sampleRate == 0 || channels == 0 || bitsPerSample == 0) {
        return 0.0f;
    }

    size_t bytesPerSample = (bitsPerSample / 8) * channels;
    size_t totalSamples = dataSize / bytesPerSample;

    return static_cast<float>(totalSamples) / static_cast<float>(sampleRate);
}

} // namespace Cortex::Audio
