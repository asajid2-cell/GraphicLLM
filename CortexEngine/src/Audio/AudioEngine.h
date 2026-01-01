#pragma once

// AudioEngine.h
// Core audio engine using XAudio2 for Windows.
// Provides sound playback, mixing, and 3D spatial audio.
//
// Reference: "Game Audio Programming: Principles and Practices" - Baca
// Reference: Microsoft XAudio2 Programming Guide

#include <xaudio2.h>
#include <x3daudio.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <queue>
#include <atomic>
#include <cstdint>

namespace Cortex::Audio {

// Forward declarations
class SoundBuffer;
class AudioSource;
class AmbientZone;

// Audio handle for tracking playing sounds
struct AudioHandle {
    uint32_t id = 0;
    uint32_t generation = 0;

    bool IsValid() const { return id != 0; }
    bool operator==(const AudioHandle& other) const {
        return id == other.id && generation == other.generation;
    }
};

// Audio delivery mode
enum class AudioDelivery {
    Immediate,      // Play now
    Queued,         // Queue for next frame
    Streamed        // Stream from disk (for long audio)
};

// Sound playback parameters
struct AudioParams {
    float volume = 1.0f;            // 0-1 volume multiplier
    float pitch = 1.0f;             // Pitch multiplier (1 = normal)
    float pan = 0.0f;               // -1 (left) to 1 (right), 0 = center
    bool loop = false;              // Loop playback
    bool spatial = false;           // Use 3D positioning
    float priority = 0.5f;          // Voice priority (higher = more important)

    // Spatial audio parameters
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);  // For Doppler
    float minDistance = 1.0f;       // Distance at full volume
    float maxDistance = 100.0f;     // Distance at zero volume
    float rolloffFactor = 1.0f;     // Attenuation curve

    // Fade parameters
    float fadeInTime = 0.0f;
    float fadeOutTime = 0.0f;

    // Delay
    float startDelay = 0.0f;        // Seconds before playing
};

// Listener (camera/player) parameters
struct AudioListener {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 forward = glm::vec3(0.0f, 0.0f, 1.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
};

// Audio bus (submix) for grouping sounds
enum class AudioBus : uint8_t {
    Master = 0,     // Final output
    Music = 1,      // Background music
    SFX = 2,        // Sound effects
    Voice = 3,      // Dialog/speech
    Ambient = 4,    // Environmental sounds
    UI = 5,         // Interface sounds
    COUNT
};

// Audio engine configuration
struct AudioEngineConfig {
    uint32_t sampleRate = 44100;
    uint32_t channels = 2;          // Stereo output
    uint32_t maxVoices = 64;        // Maximum concurrent sounds
    uint32_t maxStreamingVoices = 4;
    float masterVolume = 1.0f;
    bool enableHRTF = false;        // Head-related transfer function
    bool enableReverb = true;
    float dopplerScale = 1.0f;
    float speedOfSound = 343.0f;    // Meters per second
};

// Active voice tracking
struct ActiveVoice {
    AudioHandle handle;
    IXAudio2SourceVoice* voice = nullptr;
    std::shared_ptr<SoundBuffer> buffer;
    AudioParams params;
    AudioBus bus = AudioBus::SFX;

    float currentVolume = 1.0f;
    float targetVolume = 1.0f;
    float volumeFadeRate = 0.0f;

    float playbackTime = 0.0f;
    float remainingDelay = 0.0f;

    bool isPlaying = false;
    bool isPaused = false;
    bool isFadingOut = false;
    bool pendingStop = false;

    uint32_t generation = 0;
};

// Audio statistics
struct AudioStats {
    uint32_t activeVoices = 0;
    uint32_t totalVoices = 0;
    uint32_t streamingVoices = 0;
    uint32_t buffersLoaded = 0;
    size_t memoryUsedBytes = 0;
    float cpuUsage = 0.0f;
};

// Voice callback for end-of-playback notification
class VoiceCallback : public IXAudio2VoiceCallback {
public:
    VoiceCallback(AudioHandle handle, class AudioEngine* engine);

    void STDMETHODCALLTYPE OnStreamEnd() override;
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
    void STDMETHODCALLTYPE OnBufferEnd(void*) override {}
    void STDMETHODCALLTYPE OnBufferStart(void*) override {}
    void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {}

private:
    AudioHandle m_handle;
    class AudioEngine* m_engine;
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    // Lifecycle
    bool Initialize(const AudioEngineConfig& config = AudioEngineConfig());
    void Shutdown();
    void Update(float deltaTime);

    // Sound playback
    AudioHandle Play(const std::string& soundName, const AudioParams& params = AudioParams());
    AudioHandle Play(std::shared_ptr<SoundBuffer> buffer, const AudioParams& params = AudioParams());
    AudioHandle PlayOneShot(const std::string& soundName, const glm::vec3& position, float volume = 1.0f);
    AudioHandle PlayMusic(const std::string& musicName, float fadeInTime = 1.0f, bool loop = true);

    // Playback control
    void Stop(AudioHandle handle, float fadeOutTime = 0.0f);
    void StopAll(float fadeOutTime = 0.0f);
    void StopAllOnBus(AudioBus bus, float fadeOutTime = 0.0f);
    void Pause(AudioHandle handle);
    void Resume(AudioHandle handle);
    void PauseAll();
    void ResumeAll();

    // Volume control
    void SetVolume(AudioHandle handle, float volume, float fadeTime = 0.0f);
    void SetMasterVolume(float volume);
    void SetBusVolume(AudioBus bus, float volume);
    float GetMasterVolume() const { return m_masterVolume; }
    float GetBusVolume(AudioBus bus) const;

    // Spatial audio
    void SetListenerTransform(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up);
    void SetListenerVelocity(const glm::vec3& velocity);
    void UpdateSourcePosition(AudioHandle handle, const glm::vec3& position);
    void UpdateSourceVelocity(AudioHandle handle, const glm::vec3& velocity);

    // Sound buffer management
    std::shared_ptr<SoundBuffer> LoadSound(const std::string& path);
    std::shared_ptr<SoundBuffer> GetSound(const std::string& name);
    void PreloadSound(const std::string& path);
    void UnloadSound(const std::string& name);
    void UnloadAllSounds();

    // Query
    bool IsPlaying(AudioHandle handle) const;
    bool IsPaused(AudioHandle handle) const;
    float GetPlaybackTime(AudioHandle handle) const;
    const AudioStats& GetStats() const { return m_stats; }

    // Advanced
    void SetPitch(AudioHandle handle, float pitch);
    void SetPan(AudioHandle handle, float pan);
    void SetLooping(AudioHandle handle, bool loop);

    // Ambient zones
    void RegisterAmbientZone(AmbientZone* zone);
    void UnregisterAmbientZone(AmbientZone* zone);
    void UpdateAmbientZones(const glm::vec3& listenerPos);

    // Internal callbacks
    void OnVoiceEnd(AudioHandle handle);

private:
    // Voice management
    ActiveVoice* FindVoice(AudioHandle handle);
    const ActiveVoice* FindVoice(AudioHandle handle) const;
    ActiveVoice* AllocateVoice();
    void FreeVoice(ActiveVoice* voice);

    // 3D audio processing
    void UpdateSpatialAudio(ActiveVoice& voice);
    void Apply3DAudio(ActiveVoice& voice, const X3DAUDIO_DSP_SETTINGS& dspSettings);

    // Volume fade processing
    void ProcessVolumeFades(float deltaTime);

    // Generate unique handle
    AudioHandle GenerateHandle();

private:
    // XAudio2 core
    IXAudio2* m_xaudio = nullptr;
    IXAudio2MasteringVoice* m_masterVoice = nullptr;
    std::array<IXAudio2SubmixVoice*, static_cast<size_t>(AudioBus::COUNT)> m_submixVoices = {};

    // X3DAudio for spatial processing
    X3DAUDIO_HANDLE m_x3dAudio;
    bool m_x3dInitialized = false;

    // Configuration
    AudioEngineConfig m_config;
    float m_masterVolume = 1.0f;
    std::array<float, static_cast<size_t>(AudioBus::COUNT)> m_busVolumes;

    // Listener
    AudioListener m_listener;
    X3DAUDIO_LISTENER m_x3dListener;

    // Voice pool
    std::vector<ActiveVoice> m_voices;
    std::vector<std::unique_ptr<VoiceCallback>> m_callbacks;
    std::queue<AudioHandle> m_endedVoices;
    mutable std::mutex m_voiceMutex;

    // Sound buffer cache
    std::unordered_map<std::string, std::shared_ptr<SoundBuffer>> m_soundCache;
    mutable std::mutex m_cacheMutex;

    // Ambient zones
    std::vector<AmbientZone*> m_ambientZones;

    // Handle generation
    std::atomic<uint32_t> m_nextHandleId{ 1 };
    std::atomic<uint32_t> m_handleGeneration{ 0 };

    // Statistics
    AudioStats m_stats;

    // State
    bool m_initialized = false;
    bool m_paused = false;
};

// Global audio engine access
AudioEngine& GetAudioEngine();

// Convenience functions
AudioHandle PlaySound(const std::string& name, float volume = 1.0f);
AudioHandle PlaySound3D(const std::string& name, const glm::vec3& position, float volume = 1.0f);
void StopSound(AudioHandle handle, float fadeOut = 0.0f);
void SetSoundVolume(AudioHandle handle, float volume);

} // namespace Cortex::Audio
