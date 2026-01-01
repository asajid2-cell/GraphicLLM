// AmbientZone.cpp
// Biome-specific ambient audio implementation.

#include "AmbientZone.h"
#include <algorithm>
#include <random>
#include <cmath>

namespace Cortex::Audio {

// AmbientZone implementation
AmbientZone::AmbientZone() {
}

void AmbientZone::AddLayer(const AmbientLayer& layer) {
    m_layers.push_back(layer);
}

void AmbientZone::RemoveLayer(const std::string& soundName) {
    m_layers.erase(
        std::remove_if(m_layers.begin(), m_layers.end(),
            [&](const AmbientLayer& layer) {
                return layer.soundName == soundName;
            }),
        m_layers.end()
    );
}

void AmbientZone::ClearLayers() {
    m_layers.clear();
}

void AmbientZone::Update(const glm::vec3& listenerPos, AudioEngine& engine) {
    float blendFactor = GetBlendFactor(listenerPos);

    // Update current blend with smoothing
    float blendSpeed = 2.0f;  // Per second
    if (blendFactor > m_currentBlend) {
        m_currentBlend = std::min(blendFactor, m_currentBlend + blendSpeed * 0.016f);
    } else {
        m_currentBlend = std::max(blendFactor, m_currentBlend - blendSpeed * 0.016f);
    }

    // Activate/deactivate based on blend
    if (m_currentBlend > 0.01f && !m_isActive) {
        Activate(engine);
    } else if (m_currentBlend < 0.01f && m_isActive) {
        Deactivate(engine);
    }

    if (m_isActive) {
        UpdateLayerVolumes(m_currentBlend, engine);
    }
}

bool AmbientZone::IsListenerInside(const glm::vec3& listenerPos) const {
    return DistanceToZone(listenerPos) <= 0.0f;
}

float AmbientZone::GetBlendFactor(const glm::vec3& listenerPos) const {
    float distance = DistanceToZone(listenerPos);

    if (distance <= 0.0f) {
        return 1.0f;  // Inside zone
    }

    if (distance >= m_fadeDistance) {
        return 0.0f;  // Outside fade range
    }

    // Linear fade
    return 1.0f - (distance / m_fadeDistance);
}

float AmbientZone::DistanceToZone(const glm::vec3& pos) const {
    switch (m_shape) {
        case ZoneShape::Sphere: {
            float dist = glm::length(pos - m_position);
            return dist - m_radius;
        }

        case ZoneShape::Box: {
            glm::vec3 local = pos - m_position;
            glm::vec3 d = glm::abs(local) - m_boxExtents;
            glm::vec3 clamped = glm::max(d, glm::vec3(0.0f));
            return glm::length(clamped) + std::min(std::max(d.x, std::max(d.y, d.z)), 0.0f);
        }

        case ZoneShape::Cylinder: {
            glm::vec2 horizontal(pos.x - m_position.x, pos.z - m_position.z);
            float horizDist = glm::length(horizontal) - m_radius;
            float vertDist = std::abs(pos.y - m_position.y) - m_boxExtents.y;
            return std::max(horizDist, vertDist);
        }
    }

    return 0.0f;
}

void AmbientZone::Activate(AudioEngine& engine) {
    m_isActive = true;

    for (auto& layer : m_layers) {
        if (!layer.isActive && layer.loop) {
            AudioParams params;
            params.volume = 0.0f;  // Start silent, will fade in
            params.loop = layer.loop;
            params.spatial = layer.spatial;
            if (layer.spatial) {
                params.position = m_position;
                params.maxDistance = layer.spatialRadius;
            }

            layer.activeHandle = engine.Play(layer.soundName, params);
            layer.isActive = true;
            layer.currentVolume = 0.0f;
        }
    }
}

void AmbientZone::Deactivate(AudioEngine& engine) {
    for (auto& layer : m_layers) {
        if (layer.isActive) {
            engine.Stop(layer.activeHandle, layer.fadeOutTime);
            layer.isActive = false;
            layer.activeHandle = AudioHandle{};
        }
    }

    m_isActive = false;
}

void AmbientZone::UpdateLayerVolumes(float blendFactor, AudioEngine& engine) {
    for (auto& layer : m_layers) {
        float targetVol = GetEffectiveVolume(layer, blendFactor);

        // Smooth volume transition
        float fadeRate = 1.0f / layer.fadeInTime;
        if (targetVol > layer.currentVolume) {
            layer.currentVolume = std::min(targetVol, layer.currentVolume + fadeRate * 0.016f);
        } else {
            fadeRate = 1.0f / layer.fadeOutTime;
            layer.currentVolume = std::max(targetVol, layer.currentVolume - fadeRate * 0.016f);
        }

        // Apply volume
        if (layer.isActive && layer.activeHandle.IsValid()) {
            engine.SetVolume(layer.activeHandle, layer.currentVolume);
        }

        // Handle non-looping layers (one-shots)
        if (!layer.loop && layer.isActive) {
            m_accumulatedTime += 0.016f;
            if (m_accumulatedTime >= layer.nextPlayTime) {
                // Play one-shot
                AudioParams params;
                params.volume = layer.currentVolume;
                params.loop = false;
                params.spatial = layer.spatial;
                if (layer.spatial) {
                    params.position = m_position;
                }

                engine.Play(layer.soundName, params);

                // Schedule next play
                static std::mt19937 rng(42);
                std::uniform_real_distribution<float> dist(layer.randomDelayMin, layer.randomDelayMax);
                layer.nextPlayTime = m_accumulatedTime + dist(rng);
            }
        }
    }
}

float AmbientZone::GetEffectiveVolume(const AmbientLayer& layer, float blendFactor) const {
    float volume = layer.baseVolume * blendFactor;

    // Time variation
    if (layer.useTimeVariation) {
        int timeIndex = static_cast<int>(m_timeOfDay);
        volume *= layer.timeVolumes[timeIndex];
    }

    // Weather variation
    if (layer.useWeatherVariation) {
        int weatherIndex = static_cast<int>(m_weather);
        volume *= layer.weatherVolumes[weatherIndex];
    }

    return std::clamp(volume, layer.minVolume, layer.maxVolume);
}

// AmbientZoneManager implementation
AmbientZoneManager::AmbientZoneManager() {
}

void AmbientZoneManager::AddZone(std::unique_ptr<AmbientZone> zone) {
    m_zones.push_back(std::move(zone));
}

void AmbientZoneManager::RemoveZone(const std::string& biomeName) {
    m_zones.erase(
        std::remove_if(m_zones.begin(), m_zones.end(),
            [&](const std::unique_ptr<AmbientZone>& zone) {
                return zone->GetBiomeName() == biomeName;
            }),
        m_zones.end()
    );
}

void AmbientZoneManager::ClearZones() {
    m_zones.clear();
    m_activeZones.clear();
}

void AmbientZoneManager::Update(const glm::vec3& listenerPos, AudioEngine& engine, float deltaTime) {
    (void)deltaTime;

    // Update all zones
    for (auto& zone : m_zones) {
        zone->SetTimeOfDay(m_globalTime);
        zone->SetWeather(m_globalWeather);
        zone->Update(listenerPos, engine);
    }

    // Sort active zones by priority
    m_activeZones.clear();
    for (auto& zone : m_zones) {
        if (zone->IsActive()) {
            m_activeZones.push_back(zone.get());
        }
    }

    std::sort(m_activeZones.begin(), m_activeZones.end(),
        [](const AmbientZone* a, const AmbientZone* b) {
            return a->GetPriority() > b->GetPriority();
        });
}

void AmbientZoneManager::SetTimeOfDay(TimeOfDay time) {
    m_globalTime = time;
}

void AmbientZoneManager::SetWeather(WeatherCondition weather) {
    m_globalWeather = weather;
}

std::unique_ptr<AmbientZone> AmbientZoneManager::CreateZoneFromBiome(
    const BiomeAmbient& biome,
    const glm::vec3& position,
    float radius
) {
    auto zone = std::make_unique<AmbientZone>();
    zone->SetPosition(position);
    zone->SetRadius(radius);
    zone->SetBiomeName(biome.biomeName);

    for (const auto& layer : biome.layers) {
        zone->AddLayer(layer);
    }

    return zone;
}

BiomeAmbient AmbientZoneManager::GetForestAmbient() {
    BiomeAmbient ambient;
    ambient.biomeName = "Forest";
    ambient.transitionTime = 3.0f;

    // Background ambience
    AmbientLayer base;
    base.soundName = "ambient/forest_base";
    base.baseVolume = 0.6f;
    base.loop = true;
    base.useTimeVariation = true;
    base.timeVolumes = { 0.7f, 0.8f, 0.8f, 0.7f, 0.4f };  // Quieter at night
    ambient.layers.push_back(base);

    // Birds
    AmbientLayer birds;
    birds.soundName = "ambient/forest_birds";
    birds.baseVolume = 0.4f;
    birds.loop = true;
    birds.useTimeVariation = true;
    birds.timeVolumes = { 1.0f, 0.8f, 0.6f, 0.8f, 0.0f };  // Dawn chorus, no birds at night
    ambient.layers.push_back(birds);

    // Wind in trees
    AmbientLayer wind;
    wind.soundName = "ambient/wind_trees";
    wind.baseVolume = 0.3f;
    wind.loop = true;
    wind.useWeatherVariation = true;
    wind.weatherVolumes = { 0.3f, 0.5f, 0.8f, 1.0f, 0.4f, 0.2f };
    ambient.layers.push_back(wind);

    // Crickets
    AmbientLayer crickets;
    crickets.soundName = "ambient/crickets";
    crickets.baseVolume = 0.5f;
    crickets.loop = true;
    crickets.useTimeVariation = true;
    crickets.timeVolumes = { 0.3f, 0.0f, 0.0f, 0.5f, 1.0f };  // Evening/night only
    ambient.layers.push_back(crickets);

    return ambient;
}

BiomeAmbient AmbientZoneManager::GetDesertAmbient() {
    BiomeAmbient ambient;
    ambient.biomeName = "Desert";
    ambient.transitionTime = 4.0f;

    // Base wind
    AmbientLayer wind;
    wind.soundName = "ambient/desert_wind";
    wind.baseVolume = 0.5f;
    wind.loop = true;
    ambient.layers.push_back(wind);

    // Hot shimmer (daytime)
    AmbientLayer shimmer;
    shimmer.soundName = "ambient/heat_shimmer";
    shimmer.baseVolume = 0.3f;
    shimmer.loop = true;
    shimmer.useTimeVariation = true;
    shimmer.timeVolumes = { 0.2f, 0.8f, 1.0f, 0.5f, 0.0f };
    ambient.layers.push_back(shimmer);

    return ambient;
}

BiomeAmbient AmbientZoneManager::GetSwampAmbient() {
    BiomeAmbient ambient;
    ambient.biomeName = "Swamp";
    ambient.transitionTime = 3.0f;

    // Base
    AmbientLayer base;
    base.soundName = "ambient/swamp_base";
    base.baseVolume = 0.5f;
    base.loop = true;
    ambient.layers.push_back(base);

    // Frogs
    AmbientLayer frogs;
    frogs.soundName = "ambient/frogs";
    frogs.baseVolume = 0.6f;
    frogs.loop = true;
    frogs.useTimeVariation = true;
    frogs.timeVolumes = { 0.5f, 0.2f, 0.1f, 0.7f, 1.0f };
    ambient.layers.push_back(frogs);

    // Insects
    AmbientLayer insects;
    insects.soundName = "ambient/swamp_insects";
    insects.baseVolume = 0.4f;
    insects.loop = true;
    ambient.layers.push_back(insects);

    // Bubbles
    AmbientLayer bubbles;
    bubbles.soundName = "ambient/swamp_bubbles";
    bubbles.baseVolume = 0.2f;
    bubbles.loop = false;
    bubbles.randomDelayMin = 3.0f;
    bubbles.randomDelayMax = 12.0f;
    ambient.layers.push_back(bubbles);

    return ambient;
}

BiomeAmbient AmbientZoneManager::GetTundraAmbient() {
    BiomeAmbient ambient;
    ambient.biomeName = "Tundra";
    ambient.transitionTime = 4.0f;

    // Cold wind
    AmbientLayer wind;
    wind.soundName = "ambient/cold_wind";
    wind.baseVolume = 0.6f;
    wind.loop = true;
    wind.useWeatherVariation = true;
    wind.weatherVolumes = { 0.4f, 0.5f, 0.3f, 0.8f, 1.0f, 0.3f };
    ambient.layers.push_back(wind);

    // Snow crunch (subtle base)
    AmbientLayer snow;
    snow.soundName = "ambient/distant_snow";
    snow.baseVolume = 0.2f;
    snow.loop = true;
    ambient.layers.push_back(snow);

    return ambient;
}

BiomeAmbient AmbientZoneManager::GetMountainAmbient() {
    BiomeAmbient ambient;
    ambient.biomeName = "Mountain";
    ambient.transitionTime = 3.0f;

    // High altitude wind
    AmbientLayer wind;
    wind.soundName = "ambient/mountain_wind";
    wind.baseVolume = 0.7f;
    wind.loop = true;
    ambient.layers.push_back(wind);

    // Echo
    AmbientLayer echo;
    echo.soundName = "ambient/mountain_echo";
    echo.baseVolume = 0.1f;
    echo.loop = false;
    echo.randomDelayMin = 10.0f;
    echo.randomDelayMax = 30.0f;
    ambient.layers.push_back(echo);

    return ambient;
}

BiomeAmbient AmbientZoneManager::GetOceanAmbient() {
    BiomeAmbient ambient;
    ambient.biomeName = "Ocean";
    ambient.transitionTime = 3.0f;

    // Waves
    AmbientLayer waves;
    waves.soundName = "ambient/ocean_waves";
    waves.baseVolume = 0.8f;
    waves.loop = true;
    waves.useWeatherVariation = true;
    waves.weatherVolumes = { 0.6f, 0.7f, 1.0f, 1.0f, 0.5f, 0.4f };
    ambient.layers.push_back(waves);

    // Seagulls
    AmbientLayer gulls;
    gulls.soundName = "ambient/seagulls";
    gulls.baseVolume = 0.3f;
    gulls.loop = true;
    gulls.useTimeVariation = true;
    gulls.timeVolumes = { 0.8f, 1.0f, 0.8f, 0.5f, 0.0f };
    ambient.layers.push_back(gulls);

    return ambient;
}

// AmbientEmitterManager implementation
void AmbientEmitterManager::AddEmitter(const AmbientEmitter& emitter) {
    m_emitters.push_back(emitter);
}

void AmbientEmitterManager::RemoveEmittersInRadius(const glm::vec3& center, float radius) {
    m_emitters.erase(
        std::remove_if(m_emitters.begin(), m_emitters.end(),
            [&](const AmbientEmitter& emitter) {
                return glm::length(emitter.position - center) <= radius;
            }),
        m_emitters.end()
    );
}

void AmbientEmitterManager::Clear() {
    m_emitters.clear();
}

void AmbientEmitterManager::Update(
    const glm::vec3& listenerPos,
    AudioEngine& engine,
    float deltaTime,
    TimeOfDay time
) {
    m_accumulatedTime += deltaTime;

    static std::mt19937 rng(std::random_device{}());

    for (auto& emitter : m_emitters) {
        float distance = glm::length(emitter.position - listenerPos);

        // Activate/deactivate based on distance
        if (distance <= m_activationRadius) {
            emitter.isActive = true;
        } else {
            emitter.isActive = false;
            continue;
        }

        // Check if it's time to play
        if (m_accumulatedTime >= emitter.nextPlayTime) {
            // Check time-based chance
            float chance = 1.0f;
            if (emitter.useTimeVariation) {
                chance = emitter.timeChances[static_cast<int>(time)];
            }

            std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);
            if (chanceDist(rng) <= chance && !emitter.sounds.empty()) {
                // Pick random sound
                std::uniform_int_distribution<size_t> soundDist(0, emitter.sounds.size() - 1);
                const std::string& sound = emitter.sounds[soundDist(rng)];

                // Calculate volume based on distance
                float volume = emitter.volume;
                if (distance > emitter.radius * 0.5f) {
                    float falloff = 1.0f - ((distance - emitter.radius * 0.5f) / (m_activationRadius - emitter.radius * 0.5f));
                    volume *= falloff;
                }

                // Play sound
                engine.PlayOneShot(sound, emitter.position, volume);
            }

            // Schedule next
            std::uniform_real_distribution<float> delayDist(emitter.minInterval, emitter.maxInterval);
            emitter.nextPlayTime = m_accumulatedTime + delayDist(rng);
        }
    }
}

} // namespace Cortex::Audio
