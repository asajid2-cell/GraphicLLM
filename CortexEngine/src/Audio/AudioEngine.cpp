// AudioEngine.cpp
// XAudio2-based audio engine implementation.

#include "AudioEngine.h"
#include "SoundBuffer.h"
#include "AmbientZone.h"
#include <algorithm>
#include <cmath>

#pragma comment(lib, "xaudio2.lib")

namespace Cortex::Audio {

// VoiceCallback implementation
VoiceCallback::VoiceCallback(AudioHandle handle, AudioEngine* engine)
    : m_handle(handle)
    , m_engine(engine)
{
}

void VoiceCallback::OnStreamEnd() {
    if (m_engine) {
        m_engine->OnVoiceEnd(m_handle);
    }
}

// AudioEngine implementation
AudioEngine::AudioEngine() {
    m_busVolumes.fill(1.0f);
    m_submixVoices.fill(nullptr);
}

AudioEngine::~AudioEngine() {
    Shutdown();
}

bool AudioEngine::Initialize(const AudioEngineConfig& config) {
    if (m_initialized) {
        return true;
    }

    m_config = config;
    m_masterVolume = config.masterVolume;

    // Initialize COM (required for XAudio2)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        return false;
    }

    // Create XAudio2 engine
    hr = XAudio2Create(&m_xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) {
        return false;
    }

    // Create mastering voice
    hr = m_xaudio->CreateMasteringVoice(
        &m_masterVoice,
        config.channels,
        config.sampleRate,
        0,
        nullptr,
        nullptr,
        AudioCategory_GameEffects
    );
    if (FAILED(hr)) {
        m_xaudio->Release();
        m_xaudio = nullptr;
        return false;
    }

    // Create submix voices for each bus
    XAUDIO2_SEND_DESCRIPTOR sendDesc = {};
    sendDesc.pOutputVoice = m_masterVoice;

    XAUDIO2_VOICE_SENDS sendList = {};
    sendList.SendCount = 1;
    sendList.pSends = &sendDesc;

    for (int i = 0; i < static_cast<int>(AudioBus::COUNT); ++i) {
        if (i == static_cast<int>(AudioBus::Master)) {
            continue;  // Master is the mastering voice
        }

        hr = m_xaudio->CreateSubmixVoice(
            &m_submixVoices[i],
            config.channels,
            config.sampleRate,
            0,
            0,
            &sendList,
            nullptr
        );

        if (FAILED(hr)) {
            m_submixVoices[i] = nullptr;
        }
    }

    // Initialize X3DAudio for spatial processing
    DWORD channelMask;
    m_masterVoice->GetChannelMask(&channelMask);

    hr = X3DAudioInitialize(channelMask, config.speedOfSound, m_x3dAudio);
    m_x3dInitialized = SUCCEEDED(hr);

    // Initialize listener
    memset(&m_x3dListener, 0, sizeof(m_x3dListener));
    m_x3dListener.Position = { 0, 0, 0 };
    m_x3dListener.OrientFront = { 0, 0, 1 };
    m_x3dListener.OrientTop = { 0, 1, 0 };

    // Pre-allocate voice pool
    m_voices.resize(config.maxVoices);
    m_callbacks.reserve(config.maxVoices);

    for (auto& voice : m_voices) {
        voice.handle = AudioHandle{};
        voice.voice = nullptr;
        voice.isPlaying = false;
    }

    m_initialized = true;
    return true;
}

void AudioEngine::Shutdown() {
    if (!m_initialized) {
        return;
    }

    // Stop all sounds
    StopAll(0.0f);

    // Destroy voices
    for (auto& voice : m_voices) {
        if (voice.voice) {
            voice.voice->DestroyVoice();
            voice.voice = nullptr;
        }
    }

    // Clear buffers
    m_callbacks.clear();
    m_soundCache.clear();

    // Destroy submix voices
    for (auto& submix : m_submixVoices) {
        if (submix) {
            submix->DestroyVoice();
            submix = nullptr;
        }
    }

    // Destroy mastering voice
    if (m_masterVoice) {
        m_masterVoice->DestroyVoice();
        m_masterVoice = nullptr;
    }

    // Release XAudio2
    if (m_xaudio) {
        m_xaudio->Release();
        m_xaudio = nullptr;
    }

    m_initialized = false;
}

void AudioEngine::Update(float deltaTime) {
    if (!m_initialized || m_paused) {
        return;
    }

    // Process ended voices
    {
        std::lock_guard<std::mutex> lock(m_voiceMutex);
        while (!m_endedVoices.empty()) {
            AudioHandle handle = m_endedVoices.front();
            m_endedVoices.pop();

            ActiveVoice* voice = FindVoice(handle);
            if (voice && !voice->params.loop) {
                FreeVoice(voice);
            }
        }
    }

    // Update active voices
    uint32_t activeCount = 0;
    for (auto& voice : m_voices) {
        if (!voice.isPlaying) continue;

        activeCount++;

        // Update delay
        if (voice.remainingDelay > 0.0f) {
            voice.remainingDelay -= deltaTime;
            if (voice.remainingDelay <= 0.0f) {
                voice.remainingDelay = 0.0f;
                if (voice.voice) {
                    voice.voice->Start();
                }
            }
            continue;
        }

        // Update playback time
        voice.playbackTime += deltaTime;

        // Update spatial audio
        if (voice.params.spatial && m_x3dInitialized) {
            UpdateSpatialAudio(voice);
        }

        // Process pending stop
        if (voice.pendingStop) {
            if (voice.voice) {
                voice.voice->Stop();
            }
            FreeVoice(&voice);
        }
    }

    // Process volume fades
    ProcessVolumeFades(deltaTime);

    // Update ambient zones
    UpdateAmbientZones(m_listener.position);

    // Update stats
    m_stats.activeVoices = activeCount;
    m_stats.totalVoices = static_cast<uint32_t>(m_voices.size());
}

AudioHandle AudioEngine::Play(const std::string& soundName, const AudioParams& params) {
    auto buffer = GetSound(soundName);
    if (!buffer) {
        buffer = LoadSound(soundName);
    }
    return Play(buffer, params);
}

AudioHandle AudioEngine::Play(std::shared_ptr<SoundBuffer> buffer, const AudioParams& params) {
    if (!m_initialized || !buffer || !buffer->IsValid()) {
        return AudioHandle{};
    }

    std::lock_guard<std::mutex> lock(m_voiceMutex);

    ActiveVoice* voice = AllocateVoice();
    if (!voice) {
        return AudioHandle{};  // No free voices
    }

    // Generate handle
    voice->handle = GenerateHandle();
    voice->buffer = buffer;
    voice->params = params;
    voice->currentVolume = params.fadeInTime > 0.0f ? 0.0f : params.volume;
    voice->targetVolume = params.volume;
    voice->volumeFadeRate = params.fadeInTime > 0.0f ? params.volume / params.fadeInTime : 0.0f;
    voice->playbackTime = 0.0f;
    voice->remainingDelay = params.startDelay;
    voice->isPlaying = true;
    voice->isPaused = false;
    voice->isFadingOut = false;
    voice->pendingStop = false;

    // Determine bus
    voice->bus = AudioBus::SFX;  // Default

    // Create source voice
    WAVEFORMATEX format = buffer->GetFormat();

    // Set up send list to route through submix
    XAUDIO2_SEND_DESCRIPTOR sendDesc = {};
    int busIndex = static_cast<int>(voice->bus);
    if (busIndex > 0 && m_submixVoices[busIndex]) {
        sendDesc.pOutputVoice = m_submixVoices[busIndex];
    } else {
        sendDesc.pOutputVoice = m_masterVoice;
    }

    XAUDIO2_VOICE_SENDS sendList = {};
    sendList.SendCount = 1;
    sendList.pSends = &sendDesc;

    // Create callback
    auto callback = std::make_unique<VoiceCallback>(voice->handle, this);

    HRESULT hr = m_xaudio->CreateSourceVoice(
        &voice->voice,
        &format,
        0,
        XAUDIO2_DEFAULT_FREQ_RATIO,
        callback.get(),
        &sendList,
        nullptr
    );

    if (FAILED(hr)) {
        FreeVoice(voice);
        return AudioHandle{};
    }

    m_callbacks.push_back(std::move(callback));

    // Submit buffer
    XAUDIO2_BUFFER xbuffer = {};
    xbuffer.pAudioData = buffer->GetData();
    xbuffer.AudioBytes = buffer->GetDataSize();
    xbuffer.Flags = XAUDIO2_END_OF_STREAM;
    if (params.loop) {
        xbuffer.LoopCount = XAUDIO2_LOOP_INFINITE;
    }

    hr = voice->voice->SubmitSourceBuffer(&xbuffer);
    if (FAILED(hr)) {
        voice->voice->DestroyVoice();
        voice->voice = nullptr;
        FreeVoice(voice);
        return AudioHandle{};
    }

    // Set initial volume
    float finalVolume = voice->currentVolume * m_masterVolume * m_busVolumes[busIndex];
    voice->voice->SetVolume(finalVolume);

    // Set pitch
    if (params.pitch != 1.0f) {
        voice->voice->SetFrequencyRatio(params.pitch);
    }

    // Start playback (unless delayed)
    if (voice->remainingDelay <= 0.0f) {
        voice->voice->Start();
    }

    return voice->handle;
}

AudioHandle AudioEngine::PlayOneShot(const std::string& soundName, const glm::vec3& position, float volume) {
    AudioParams params;
    params.volume = volume;
    params.spatial = true;
    params.position = position;
    params.loop = false;
    return Play(soundName, params);
}

AudioHandle AudioEngine::PlayMusic(const std::string& musicName, float fadeInTime, bool loop) {
    // Stop current music
    StopAllOnBus(AudioBus::Music, fadeInTime * 0.5f);

    AudioParams params;
    params.volume = 1.0f;
    params.fadeInTime = fadeInTime;
    params.loop = loop;
    params.spatial = false;

    // Load and play
    auto buffer = LoadSound(musicName);
    if (!buffer) {
        return AudioHandle{};
    }

    std::lock_guard<std::mutex> lock(m_voiceMutex);

    ActiveVoice* voice = AllocateVoice();
    if (!voice) {
        return AudioHandle{};
    }

    voice->bus = AudioBus::Music;

    // Continue with normal play setup...
    voice->handle = GenerateHandle();
    voice->buffer = buffer;
    voice->params = params;
    voice->currentVolume = 0.0f;
    voice->targetVolume = params.volume;
    voice->volumeFadeRate = params.volume / fadeInTime;
    voice->isPlaying = true;

    WAVEFORMATEX format = buffer->GetFormat();

    XAUDIO2_SEND_DESCRIPTOR sendDesc = {};
    sendDesc.pOutputVoice = m_submixVoices[static_cast<int>(AudioBus::Music)]
        ? m_submixVoices[static_cast<int>(AudioBus::Music)]
        : m_masterVoice;

    XAUDIO2_VOICE_SENDS sendList = {};
    sendList.SendCount = 1;
    sendList.pSends = &sendDesc;

    auto callback = std::make_unique<VoiceCallback>(voice->handle, this);

    HRESULT hr = m_xaudio->CreateSourceVoice(
        &voice->voice,
        &format,
        0,
        XAUDIO2_DEFAULT_FREQ_RATIO,
        callback.get(),
        &sendList,
        nullptr
    );

    if (FAILED(hr)) {
        FreeVoice(voice);
        return AudioHandle{};
    }

    m_callbacks.push_back(std::move(callback));

    XAUDIO2_BUFFER xbuffer = {};
    xbuffer.pAudioData = buffer->GetData();
    xbuffer.AudioBytes = buffer->GetDataSize();
    xbuffer.Flags = XAUDIO2_END_OF_STREAM;
    if (loop) {
        xbuffer.LoopCount = XAUDIO2_LOOP_INFINITE;
    }

    voice->voice->SubmitSourceBuffer(&xbuffer);
    voice->voice->SetVolume(0.0f);  // Start silent for fade in
    voice->voice->Start();

    return voice->handle;
}

void AudioEngine::Stop(AudioHandle handle, float fadeOutTime) {
    std::lock_guard<std::mutex> lock(m_voiceMutex);

    ActiveVoice* voice = FindVoice(handle);
    if (!voice || !voice->isPlaying) {
        return;
    }

    if (fadeOutTime > 0.0f) {
        voice->isFadingOut = true;
        voice->targetVolume = 0.0f;
        voice->volumeFadeRate = voice->currentVolume / fadeOutTime;
    } else {
        if (voice->voice) {
            voice->voice->Stop();
        }
        FreeVoice(voice);
    }
}

void AudioEngine::StopAll(float fadeOutTime) {
    std::lock_guard<std::mutex> lock(m_voiceMutex);

    for (auto& voice : m_voices) {
        if (!voice.isPlaying) continue;

        if (fadeOutTime > 0.0f) {
            voice.isFadingOut = true;
            voice.targetVolume = 0.0f;
            voice.volumeFadeRate = voice.currentVolume / fadeOutTime;
        } else {
            if (voice.voice) {
                voice.voice->Stop();
            }
            FreeVoice(&voice);
        }
    }
}

void AudioEngine::StopAllOnBus(AudioBus bus, float fadeOutTime) {
    std::lock_guard<std::mutex> lock(m_voiceMutex);

    for (auto& voice : m_voices) {
        if (!voice.isPlaying || voice.bus != bus) continue;

        if (fadeOutTime > 0.0f) {
            voice.isFadingOut = true;
            voice.targetVolume = 0.0f;
            voice.volumeFadeRate = voice.currentVolume / fadeOutTime;
        } else {
            if (voice.voice) {
                voice.voice->Stop();
            }
            FreeVoice(&voice);
        }
    }
}

void AudioEngine::Pause(AudioHandle handle) {
    std::lock_guard<std::mutex> lock(m_voiceMutex);

    ActiveVoice* voice = FindVoice(handle);
    if (voice && voice->isPlaying && !voice->isPaused) {
        if (voice->voice) {
            voice->voice->Stop();
        }
        voice->isPaused = true;
    }
}

void AudioEngine::Resume(AudioHandle handle) {
    std::lock_guard<std::mutex> lock(m_voiceMutex);

    ActiveVoice* voice = FindVoice(handle);
    if (voice && voice->isPlaying && voice->isPaused) {
        if (voice->voice) {
            voice->voice->Start();
        }
        voice->isPaused = false;
    }
}

void AudioEngine::PauseAll() {
    if (m_xaudio) {
        m_xaudio->StopEngine();
    }
    m_paused = true;
}

void AudioEngine::ResumeAll() {
    if (m_xaudio) {
        m_xaudio->StartEngine();
    }
    m_paused = false;
}

void AudioEngine::SetVolume(AudioHandle handle, float volume, float fadeTime) {
    std::lock_guard<std::mutex> lock(m_voiceMutex);

    ActiveVoice* voice = FindVoice(handle);
    if (!voice) return;

    voice->targetVolume = volume;
    if (fadeTime > 0.0f) {
        voice->volumeFadeRate = std::abs(volume - voice->currentVolume) / fadeTime;
    } else {
        voice->currentVolume = volume;
        if (voice->voice) {
            float finalVolume = volume * m_masterVolume * m_busVolumes[static_cast<int>(voice->bus)];
            voice->voice->SetVolume(finalVolume);
        }
    }
}

void AudioEngine::SetMasterVolume(float volume) {
    m_masterVolume = std::clamp(volume, 0.0f, 1.0f);

    // Update all playing voices
    std::lock_guard<std::mutex> lock(m_voiceMutex);
    for (auto& voice : m_voices) {
        if (voice.isPlaying && voice.voice) {
            float finalVolume = voice.currentVolume * m_masterVolume * m_busVolumes[static_cast<int>(voice.bus)];
            voice.voice->SetVolume(finalVolume);
        }
    }
}

void AudioEngine::SetBusVolume(AudioBus bus, float volume) {
    int index = static_cast<int>(bus);
    if (index >= 0 && index < static_cast<int>(AudioBus::COUNT)) {
        m_busVolumes[index] = std::clamp(volume, 0.0f, 1.0f);

        if (m_submixVoices[index]) {
            m_submixVoices[index]->SetVolume(volume);
        }
    }
}

float AudioEngine::GetBusVolume(AudioBus bus) const {
    int index = static_cast<int>(bus);
    if (index >= 0 && index < static_cast<int>(AudioBus::COUNT)) {
        return m_busVolumes[index];
    }
    return 1.0f;
}

void AudioEngine::SetListenerTransform(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up) {
    m_listener.position = position;
    m_listener.forward = glm::normalize(forward);
    m_listener.up = glm::normalize(up);

    // Update X3DAudio listener
    m_x3dListener.Position = { position.x, position.y, position.z };
    m_x3dListener.OrientFront = { forward.x, forward.y, forward.z };
    m_x3dListener.OrientTop = { up.x, up.y, up.z };
}

void AudioEngine::SetListenerVelocity(const glm::vec3& velocity) {
    m_listener.velocity = velocity;
    m_x3dListener.Velocity = { velocity.x, velocity.y, velocity.z };
}

void AudioEngine::UpdateSourcePosition(AudioHandle handle, const glm::vec3& position) {
    std::lock_guard<std::mutex> lock(m_voiceMutex);

    ActiveVoice* voice = FindVoice(handle);
    if (voice) {
        voice->params.position = position;
    }
}

void AudioEngine::UpdateSourceVelocity(AudioHandle handle, const glm::vec3& velocity) {
    std::lock_guard<std::mutex> lock(m_voiceMutex);

    ActiveVoice* voice = FindVoice(handle);
    if (voice) {
        voice->params.velocity = velocity;
    }
}

std::shared_ptr<SoundBuffer> AudioEngine::LoadSound(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);

    // Check cache
    auto it = m_soundCache.find(path);
    if (it != m_soundCache.end()) {
        return it->second;
    }

    // Load new buffer
    auto buffer = std::make_shared<SoundBuffer>();
    if (buffer->LoadFromFile(path)) {
        m_soundCache[path] = buffer;
        m_stats.buffersLoaded++;
        m_stats.memoryUsedBytes += buffer->GetDataSize();
        return buffer;
    }

    return nullptr;
}

std::shared_ptr<SoundBuffer> AudioEngine::GetSound(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);

    auto it = m_soundCache.find(name);
    if (it != m_soundCache.end()) {
        return it->second;
    }
    return nullptr;
}

void AudioEngine::PreloadSound(const std::string& path) {
    LoadSound(path);
}

void AudioEngine::UnloadSound(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);

    auto it = m_soundCache.find(name);
    if (it != m_soundCache.end()) {
        m_stats.memoryUsedBytes -= it->second->GetDataSize();
        m_stats.buffersLoaded--;
        m_soundCache.erase(it);
    }
}

void AudioEngine::UnloadAllSounds() {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_soundCache.clear();
    m_stats.buffersLoaded = 0;
    m_stats.memoryUsedBytes = 0;
}

bool AudioEngine::IsPlaying(AudioHandle handle) const {
    std::lock_guard<std::mutex> lock(m_voiceMutex);
    const ActiveVoice* voice = FindVoice(handle);
    return voice && voice->isPlaying && !voice->isPaused;
}

bool AudioEngine::IsPaused(AudioHandle handle) const {
    std::lock_guard<std::mutex> lock(m_voiceMutex);
    const ActiveVoice* voice = FindVoice(handle);
    return voice && voice->isPaused;
}

float AudioEngine::GetPlaybackTime(AudioHandle handle) const {
    std::lock_guard<std::mutex> lock(m_voiceMutex);
    const ActiveVoice* voice = FindVoice(handle);
    return voice ? voice->playbackTime : 0.0f;
}

void AudioEngine::SetPitch(AudioHandle handle, float pitch) {
    std::lock_guard<std::mutex> lock(m_voiceMutex);

    ActiveVoice* voice = FindVoice(handle);
    if (voice && voice->voice) {
        pitch = std::clamp(pitch, XAUDIO2_MIN_FREQ_RATIO, XAUDIO2_MAX_FREQ_RATIO);
        voice->voice->SetFrequencyRatio(pitch);
        voice->params.pitch = pitch;
    }
}

void AudioEngine::SetPan(AudioHandle handle, float pan) {
    std::lock_guard<std::mutex> lock(m_voiceMutex);

    ActiveVoice* voice = FindVoice(handle);
    if (voice && voice->voice) {
        voice->params.pan = std::clamp(pan, -1.0f, 1.0f);

        // Simple stereo panning
        float left = std::cos((pan + 1.0f) * 0.25f * 3.14159f);
        float right = std::sin((pan + 1.0f) * 0.25f * 3.14159f);

        float outputMatrix[2] = { left, right };
        voice->voice->SetOutputMatrix(nullptr, 1, 2, outputMatrix);
    }
}

void AudioEngine::SetLooping(AudioHandle handle, bool loop) {
    std::lock_guard<std::mutex> lock(m_voiceMutex);

    ActiveVoice* voice = FindVoice(handle);
    if (voice) {
        voice->params.loop = loop;
        // Note: Can't change loop on existing buffer, would need to resubmit
    }
}

void AudioEngine::RegisterAmbientZone(AmbientZone* zone) {
    if (zone) {
        m_ambientZones.push_back(zone);
    }
}

void AudioEngine::UnregisterAmbientZone(AmbientZone* zone) {
    auto it = std::find(m_ambientZones.begin(), m_ambientZones.end(), zone);
    if (it != m_ambientZones.end()) {
        m_ambientZones.erase(it);
    }
}

void AudioEngine::UpdateAmbientZones(const glm::vec3& listenerPos) {
    for (auto* zone : m_ambientZones) {
        if (zone) {
            zone->Update(listenerPos, *this);
        }
    }
}

void AudioEngine::OnVoiceEnd(AudioHandle handle) {
    std::lock_guard<std::mutex> lock(m_voiceMutex);
    m_endedVoices.push(handle);
}

ActiveVoice* AudioEngine::FindVoice(AudioHandle handle) {
    if (!handle.IsValid()) return nullptr;

    for (auto& voice : m_voices) {
        if (voice.handle == handle && voice.generation == handle.generation) {
            return &voice;
        }
    }
    return nullptr;
}

const ActiveVoice* AudioEngine::FindVoice(AudioHandle handle) const {
    if (!handle.IsValid()) return nullptr;

    for (const auto& voice : m_voices) {
        if (voice.handle == handle && voice.generation == handle.generation) {
            return &voice;
        }
    }
    return nullptr;
}

ActiveVoice* AudioEngine::AllocateVoice() {
    // Find free slot
    for (auto& voice : m_voices) {
        if (!voice.isPlaying) {
            return &voice;
        }
    }

    // Try to steal lowest priority voice
    ActiveVoice* lowestPriority = nullptr;
    float lowestPriorityValue = std::numeric_limits<float>::max();

    for (auto& voice : m_voices) {
        if (voice.params.priority < lowestPriorityValue) {
            lowestPriority = &voice;
            lowestPriorityValue = voice.params.priority;
        }
    }

    if (lowestPriority && lowestPriority->voice) {
        lowestPriority->voice->Stop();
        lowestPriority->voice->DestroyVoice();
        lowestPriority->voice = nullptr;
        lowestPriority->isPlaying = false;
        return lowestPriority;
    }

    return nullptr;
}

void AudioEngine::FreeVoice(ActiveVoice* voice) {
    if (!voice) return;

    if (voice->voice) {
        voice->voice->DestroyVoice();
        voice->voice = nullptr;
    }

    voice->buffer.reset();
    voice->handle = AudioHandle{};
    voice->isPlaying = false;
    voice->isPaused = false;
    voice->generation++;
}

void AudioEngine::UpdateSpatialAudio(ActiveVoice& voice) {
    if (!m_x3dInitialized || !voice.voice) return;

    // Set up emitter
    X3DAUDIO_EMITTER emitter = {};
    emitter.Position = { voice.params.position.x, voice.params.position.y, voice.params.position.z };
    emitter.Velocity = { voice.params.velocity.x, voice.params.velocity.y, voice.params.velocity.z };
    emitter.OrientFront = { 0, 0, 1 };
    emitter.OrientTop = { 0, 1, 0 };
    emitter.ChannelCount = 1;
    emitter.CurveDistanceScaler = voice.params.rolloffFactor;
    emitter.DopplerScaler = m_config.dopplerScale;

    // Inner/outer radius
    emitter.InnerRadius = voice.params.minDistance;
    emitter.InnerRadiusAngle = 0.0f;

    // Distance curve
    X3DAUDIO_DISTANCE_CURVE_POINT curvePoints[2] = {
        { 0.0f, 1.0f },
        { 1.0f, 0.0f }
    };
    X3DAUDIO_DISTANCE_CURVE curve = { curvePoints, 2 };
    emitter.pVolumeCurve = &curve;

    // Calculate DSP settings
    float matrixCoefficients[8];  // Support up to 7.1
    X3DAUDIO_DSP_SETTINGS dspSettings = {};
    dspSettings.SrcChannelCount = 1;
    dspSettings.DstChannelCount = m_config.channels;
    dspSettings.pMatrixCoefficients = matrixCoefficients;

    X3DAudioCalculate(
        m_x3dAudio,
        &m_x3dListener,
        &emitter,
        X3DAUDIO_CALCULATE_MATRIX | X3DAUDIO_CALCULATE_DOPPLER | X3DAUDIO_CALCULATE_LPF_DIRECT,
        &dspSettings
    );

    Apply3DAudio(voice, dspSettings);
}

void AudioEngine::Apply3DAudio(ActiveVoice& voice, const X3DAUDIO_DSP_SETTINGS& dspSettings) {
    if (!voice.voice) return;

    // Apply volume from 3D calculation
    float finalVolume = voice.currentVolume * dspSettings.pMatrixCoefficients[0];
    finalVolume *= m_masterVolume * m_busVolumes[static_cast<int>(voice.bus)];

    // Apply output matrix for panning
    voice.voice->SetOutputMatrix(nullptr, 1, m_config.channels, dspSettings.pMatrixCoefficients);

    // Apply Doppler
    voice.voice->SetFrequencyRatio(dspSettings.DopplerFactor * voice.params.pitch);
}

void AudioEngine::ProcessVolumeFades(float deltaTime) {
    for (auto& voice : m_voices) {
        if (!voice.isPlaying || voice.volumeFadeRate == 0.0f) continue;

        // Fade toward target
        if (voice.currentVolume < voice.targetVolume) {
            voice.currentVolume += voice.volumeFadeRate * deltaTime;
            if (voice.currentVolume >= voice.targetVolume) {
                voice.currentVolume = voice.targetVolume;
                voice.volumeFadeRate = 0.0f;
            }
        } else if (voice.currentVolume > voice.targetVolume) {
            voice.currentVolume -= voice.volumeFadeRate * deltaTime;
            if (voice.currentVolume <= voice.targetVolume) {
                voice.currentVolume = voice.targetVolume;
                voice.volumeFadeRate = 0.0f;

                // If fading out to zero, stop
                if (voice.isFadingOut && voice.targetVolume <= 0.0f) {
                    voice.pendingStop = true;
                }
            }
        }

        // Apply volume
        if (voice.voice) {
            float finalVolume = voice.currentVolume * m_masterVolume * m_busVolumes[static_cast<int>(voice.bus)];
            voice.voice->SetVolume(finalVolume);
        }
    }
}

AudioHandle AudioEngine::GenerateHandle() {
    return AudioHandle{
        m_nextHandleId.fetch_add(1),
        m_handleGeneration.fetch_add(1)
    };
}

// Global instance
AudioEngine& GetAudioEngine() {
    static AudioEngine instance;
    return instance;
}

// Convenience functions
AudioHandle PlaySound(const std::string& name, float volume) {
    AudioParams params;
    params.volume = volume;
    return GetAudioEngine().Play(name, params);
}

AudioHandle PlaySound3D(const std::string& name, const glm::vec3& position, float volume) {
    return GetAudioEngine().PlayOneShot(name, position, volume);
}

void StopSound(AudioHandle handle, float fadeOut) {
    GetAudioEngine().Stop(handle, fadeOut);
}

void SetSoundVolume(AudioHandle handle, float volume) {
    GetAudioEngine().SetVolume(handle, volume);
}

} // namespace Cortex::Audio
