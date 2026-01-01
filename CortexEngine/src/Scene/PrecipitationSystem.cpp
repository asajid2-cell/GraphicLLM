// PrecipitationSystem.cpp
// GPU-based precipitation particle system implementation.

#include "PrecipitationSystem.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace Cortex::Scene {

PrecipitationSystem::PrecipitationSystem()
    : m_rng(std::random_device{}())
{
}

PrecipitationSystem::~PrecipitationSystem() {
    Shutdown();
}

bool PrecipitationSystem::Initialize(const PrecipitationConfig& config) {
    m_config = config;

    // Allocate particle buffer
    m_particlesCPU.resize(config.maxParticles);
    for (auto& p : m_particlesCPU) {
        p.velocityLife.w = -1.0f;  // Mark as dead
    }

    m_activeParticles = 0;
    m_splashes.reserve(MAX_SPLASHES);

    PackConstantBuffer();

    m_initialized = true;
    return true;
}

void PrecipitationSystem::Shutdown() {
    m_particlesCPU.clear();
    m_splashes.clear();
    m_initialized = false;
    m_activeParticles = 0;
}

void PrecipitationSystem::Update(float deltaTime, const glm::vec3& cameraPos, const glm::vec3& cameraForward) {
    if (!m_initialized || !m_enabled) {
        return;
    }

    m_time += deltaTime;
    m_cameraPos = cameraPos;
    m_cameraForward = cameraForward;

    // Reset stats
    m_stats.particlesSpawned = 0;
    m_stats.particlesKilled = 0;

    // CPU simulation (GPU compute would be preferred for large counts)
    SimulateCPU(deltaTime);

    // Update splashes
    for (auto& splash : m_splashes) {
        splash.lifetime -= deltaTime;
        splash.alpha = splash.lifetime / splash.maxLifetime;
    }

    // Remove dead splashes
    m_splashes.erase(
        std::remove_if(m_splashes.begin(), m_splashes.end(),
            [](const PrecipitationSplash& s) { return s.lifetime <= 0.0f; }),
        m_splashes.end()
    );

    m_stats.activeParticles = m_activeParticles;
    m_stats.splashesActive = static_cast<uint32_t>(m_splashes.size());

    PackConstantBuffer();
}

void PrecipitationSystem::Render() {
    // Rendering would be handled by the main renderer using the particle data
    // This is a placeholder for any render-specific logic
}

void PrecipitationSystem::SetConfig(const PrecipitationConfig& config) {
    bool needsResize = config.maxParticles != m_config.maxParticles;
    m_config = config;

    if (needsResize && m_initialized) {
        m_particlesCPU.resize(config.maxParticles);
        for (size_t i = m_activeParticles; i < m_particlesCPU.size(); ++i) {
            m_particlesCPU[i].velocityLife.w = -1.0f;
        }
    }
}

void PrecipitationSystem::SetIntensity(float intensity) {
    m_intensity = std::clamp(intensity, 0.0f, 1.0f);
}

void PrecipitationSystem::SetWind(const glm::vec2& direction, float speed) {
    m_windDirection = glm::length(direction) > 0.001f ? glm::normalize(direction) : glm::vec2(1.0f, 0.0f);
    m_windSpeed = speed;
}

void PrecipitationSystem::SetPrecipitationType(PrecipitationType type) {
    if (type != m_config.type) {
        // Switch configuration based on type
        if (type == PrecipitationType::Rain) {
            m_config = GetDefaultRainConfig();
        } else if (type == PrecipitationType::Snow) {
            m_config = GetDefaultSnowConfig();
        }
        m_config.type = type;
    }
}

void PrecipitationSystem::SpawnSplash(const glm::vec3& position, float size) {
    if (m_splashes.size() >= MAX_SPLASHES) {
        return;
    }

    PrecipitationSplash splash;
    splash.position = position;
    splash.size = size * 2.0f;
    splash.maxLifetime = 0.3f;
    splash.lifetime = splash.maxLifetime;
    splash.color = m_config.baseColor;
    splash.alpha = 1.0f;

    m_splashes.push_back(splash);
}

void PrecipitationSystem::SimulateCPU(float deltaTime) {
    // Spawn new particles
    SpawnParticlesCPU(deltaTime);

    // Update existing particles
    UpdateParticlesCPU(deltaTime);

    // Remove dead particles
    KillDeadParticlesCPU();
}

void PrecipitationSystem::SpawnParticlesCPU(float deltaTime) {
    float spawnRate = m_config.spawnRate * m_intensity;
    m_spawnAccumulator += spawnRate * deltaTime;

    int particlesToSpawn = static_cast<int>(m_spawnAccumulator);
    m_spawnAccumulator -= static_cast<float>(particlesToSpawn);

    for (int i = 0; i < particlesToSpawn && m_activeParticles < m_config.maxParticles; ++i) {
        // Find dead particle slot
        size_t slot = m_activeParticles;
        for (size_t j = 0; j < m_particlesCPU.size(); ++j) {
            if (m_particlesCPU[j].velocityLife.w < 0.0f) {
                slot = j;
                break;
            }
        }

        if (slot >= m_config.maxParticles) {
            break;
        }

        PrecipitationParticleGPU& p = m_particlesCPU[slot];

        // Initialize particle
        glm::vec3 pos = GetSpawnPosition();
        glm::vec3 vel = GetInitialVelocity();
        float size = GetParticleSize();
        glm::vec4 color = GetParticleColor();

        p.positionSize = glm::vec4(pos, size);
        p.velocityLife = glm::vec4(vel, 10.0f);  // 10 second lifetime
        p.color = color;

        std::uniform_real_distribution<float> rotDist(0.0f, 6.28318f);
        p.params.x = rotDist(m_rng);  // rotation
        p.params.y = m_config.snowRotationSpeed * (rotDist(m_rng) - 3.14159f) / 3.14159f;  // rotSpeed
        p.params.z = static_cast<float>(m_config.type);
        p.params.w = 0.0f;  // distCam

        m_activeParticles++;
        m_stats.particlesSpawned++;
    }
}

void PrecipitationSystem::UpdateParticlesCPU(float deltaTime) {
    glm::vec3 windVec(m_windDirection.x * m_windSpeed * m_config.windInfluence,
                       0.0f,
                       m_windDirection.y * m_windSpeed * m_config.windInfluence);

    std::uniform_real_distribution<float> turbDist(-1.0f, 1.0f);

    for (size_t i = 0; i < m_particlesCPU.size(); ++i) {
        PrecipitationParticleGPU& p = m_particlesCPU[i];

        if (p.velocityLife.w < 0.0f) {
            continue;  // Dead particle
        }

        // Update lifetime
        p.velocityLife.w -= deltaTime;

        // Apply gravity
        p.velocityLife.y += m_config.gravity * deltaTime;

        // Clamp to terminal velocity
        if (p.velocityLife.y < -m_config.terminalVelocity) {
            p.velocityLife.y = -m_config.terminalVelocity;
        }

        // Apply wind
        p.velocityLife.x += windVec.x * deltaTime;
        p.velocityLife.z += windVec.z * deltaTime;

        // Apply turbulence
        if (m_config.turbulence > 0.0f) {
            p.velocityLife.x += turbDist(m_rng) * m_config.turbulence * deltaTime;
            p.velocityLife.z += turbDist(m_rng) * m_config.turbulence * deltaTime;
        }

        // Snow sway
        if (m_config.type == PrecipitationType::Snow) {
            float sway = std::sin(m_time * m_config.snowSwayFrequency + p.params.x) * m_config.snowSwayAmplitude;
            p.velocityLife.x += sway * deltaTime;

            // Update rotation
            p.params.x += p.params.y * deltaTime;
        }

        // Update position
        p.positionSize.x += p.velocityLife.x * deltaTime;
        p.positionSize.y += p.velocityLife.y * deltaTime;
        p.positionSize.z += p.velocityLife.z * deltaTime;

        // Distance to camera
        glm::vec3 pos(p.positionSize);
        p.params.w = glm::length(pos - m_cameraPos);

        // Kill if too far from camera horizontally
        glm::vec2 horizDist(pos.x - m_cameraPos.x, pos.z - m_cameraPos.z);
        if (glm::length(horizDist) > m_config.spawnRadius * 2.0f) {
            p.velocityLife.w = -1.0f;
            m_stats.particlesKilled++;
            m_activeParticles--;
            continue;
        }

        // Kill if below kill height
        if (pos.y < m_cameraPos.y + m_config.killHeight) {
            p.velocityLife.w = -1.0f;
            m_stats.particlesKilled++;
            m_activeParticles--;
            continue;
        }

        // Terrain collision
        if (m_config.enableCollision && m_heightQuery) {
            float groundHeight;
            if (CheckTerrainCollision(pos, groundHeight)) {
                HandleCollision(p, groundHeight);
            }
        }
    }
}

void PrecipitationSystem::KillDeadParticlesCPU() {
    // Compact buffer (optional optimization)
    // For now, dead particles are tracked by negative lifetime
}

glm::vec3 PrecipitationSystem::GetSpawnPosition() const {
    std::uniform_real_distribution<float> angleDist(0.0f, 6.28318f);
    std::uniform_real_distribution<float> radiusDist(0.0f, m_config.spawnRadius);
    std::uniform_real_distribution<float> heightDist(0.0f, m_config.spawnHeight * 0.2f);

    float angle = angleDist(m_rng);
    float radius = radiusDist(m_rng);
    float height = m_cameraPos.y + m_config.spawnHeight + heightDist(m_rng);

    return glm::vec3(
        m_cameraPos.x + std::cos(angle) * radius,
        height,
        m_cameraPos.z + std::sin(angle) * radius
    );
}

glm::vec3 PrecipitationSystem::GetInitialVelocity() const {
    glm::vec3 velocity(0.0f, 0.0f, 0.0f);

    // Initial downward velocity
    if (m_config.type == PrecipitationType::Rain) {
        velocity.y = -m_config.terminalVelocity * 0.5f;
    } else if (m_config.type == PrecipitationType::Snow) {
        velocity.y = -m_config.terminalVelocity * 0.1f;
    }

    // Add wind influence
    velocity.x = m_windDirection.x * m_windSpeed * m_config.windInfluence * 0.3f;
    velocity.z = m_windDirection.y * m_windSpeed * m_config.windInfluence * 0.3f;

    return velocity;
}

float PrecipitationSystem::GetParticleSize() const {
    std::uniform_real_distribution<float> sizeDist(
        1.0f - m_config.sizeVariation,
        1.0f + m_config.sizeVariation
    );
    return m_config.baseSize * sizeDist(m_rng);
}

glm::vec4 PrecipitationSystem::GetParticleColor() const {
    std::uniform_real_distribution<float> colorDist(
        1.0f - m_config.colorVariation,
        1.0f + m_config.colorVariation
    );

    return glm::vec4(
        m_config.baseColor.r * colorDist(m_rng),
        m_config.baseColor.g * colorDist(m_rng),
        m_config.baseColor.b * colorDist(m_rng),
        m_config.baseColor.a
    );
}

bool PrecipitationSystem::CheckTerrainCollision(const glm::vec3& pos, float& groundHeight) {
    if (!m_heightQuery) {
        return false;
    }

    groundHeight = m_heightQuery(pos.x, pos.z);
    return pos.y <= groundHeight + 0.1f;
}

void PrecipitationSystem::HandleCollision(PrecipitationParticleGPU& p, float groundHeight) {
    glm::vec3 pos(p.positionSize);

    // Create splash
    std::uniform_real_distribution<float> splashDist(0.0f, 1.0f);
    if (splashDist(m_rng) < m_config.splashChance) {
        SpawnSplash(glm::vec3(pos.x, groundHeight + 0.02f, pos.z), p.positionSize.w);
    }

    // Kill or bounce
    if (m_config.collisionBounce > 0.0f && m_config.type != PrecipitationType::Rain) {
        // Bounce (for snow/hail)
        p.positionSize.y = groundHeight + 0.05f;
        p.velocityLife.y = -p.velocityLife.y * m_config.collisionBounce;
        p.velocityLife.w *= 0.5f;  // Reduce remaining lifetime
    } else {
        // Kill
        p.velocityLife.w = -1.0f;
        m_stats.particlesKilled++;
        m_activeParticles--;
    }
}

void PrecipitationSystem::PackConstantBuffer() {
    m_cbData.cameraPosition = glm::vec4(m_cameraPos, 0.0f);

    m_cbData.spawnParams = glm::vec4(
        m_config.spawnRadius,
        m_config.spawnHeight,
        m_config.killHeight,
        m_config.spawnRate * m_intensity
    );

    m_cbData.particleParams = glm::vec4(
        m_config.baseSize,
        m_config.sizeVariation,
        m_config.gravity,
        m_config.terminalVelocity
    );

    m_cbData.windParams = glm::vec4(
        m_windDirection.x,
        m_windDirection.y,
        m_windSpeed,
        m_config.windInfluence
    );

    m_cbData.baseColor = m_config.baseColor;

    m_cbData.physicsParams = glm::vec4(
        m_config.turbulence,
        m_config.collisionBounce,
        static_cast<float>(m_config.type),
        0.016f  // deltaTime placeholder
    );

    m_cbData.rainParams = glm::vec4(
        m_config.rainStreakLength,
        m_config.rainAngle,
        0.0f,
        0.0f
    );

    m_cbData.snowParams = glm::vec4(
        m_config.snowSwayAmplitude,
        m_config.snowSwayFrequency,
        m_config.snowRotationSpeed,
        0.0f
    );

    m_cbData.time = m_time;
    m_cbData.deltaTime = 0.016f;
    m_cbData.maxParticles = m_config.maxParticles;
    m_cbData.activeParticles = m_activeParticles;
}

PrecipitationConfig GetDefaultRainConfig() {
    PrecipitationConfig config;
    config.type = PrecipitationType::Rain;
    config.maxParticles = 50000;
    config.spawnRate = 10000.0f;
    config.spawnRadius = 40.0f;
    config.spawnHeight = 50.0f;
    config.killHeight = -5.0f;
    config.baseSize = 0.015f;
    config.sizeVariation = 0.3f;
    config.baseColor = glm::vec4(0.7f, 0.75f, 0.85f, 0.5f);
    config.gravity = -9.81f;
    config.terminalVelocity = 9.0f;
    config.windInfluence = 0.8f;
    config.turbulence = 0.1f;
    config.rainStreakLength = 0.4f;
    config.enableCollision = true;
    config.splashChance = 0.3f;
    config.collisionBounce = 0.0f;
    return config;
}

PrecipitationConfig GetDefaultSnowConfig() {
    PrecipitationConfig config;
    config.type = PrecipitationType::Snow;
    config.maxParticles = 30000;
    config.spawnRate = 3000.0f;
    config.spawnRadius = 50.0f;
    config.spawnHeight = 40.0f;
    config.killHeight = -2.0f;
    config.baseSize = 0.03f;
    config.sizeVariation = 0.5f;
    config.baseColor = glm::vec4(0.95f, 0.97f, 1.0f, 0.8f);
    config.gravity = -1.5f;
    config.terminalVelocity = 1.5f;
    config.windInfluence = 1.5f;
    config.turbulence = 0.3f;
    config.snowSwayAmplitude = 0.8f;
    config.snowSwayFrequency = 1.5f;
    config.snowRotationSpeed = 2.0f;
    config.enableCollision = true;
    config.splashChance = 0.0f;
    config.collisionBounce = 0.1f;
    return config;
}

} // namespace Cortex::Scene
