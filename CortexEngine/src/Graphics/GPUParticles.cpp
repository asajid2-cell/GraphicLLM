// GPUParticles.cpp
// GPU particle system implementation.

#include "GPUParticles.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace Cortex::Graphics {

static constexpr float PI = 3.14159265358979f;

GPUParticleSystem::GPUParticleSystem()
    : m_rng(std::random_device{}())
{
}

GPUParticleSystem::~GPUParticleSystem() {
    Shutdown();
}

bool GPUParticleSystem::Initialize(uint32_t maxTotalParticles) {
    if (m_initialized) return true;

    m_maxTotalParticles = maxTotalParticles;

    // Allocate particle buffer
    m_particlesCPU.resize(maxTotalParticles);
    m_sortedIndices.resize(maxTotalParticles);

    // Initialize particles as dead
    for (auto& p : m_particlesCPU) {
        p.params.y = -1.0f;  // Negative lifetime = dead
    }

    // Build free list
    m_freeList.reserve(maxTotalParticles);
    for (uint32_t i = 0; i < maxTotalParticles; ++i) {
        m_freeList.push_back(maxTotalParticles - 1 - i);  // Reverse order
    }

    m_initialized = true;
    return true;
}

void GPUParticleSystem::Shutdown() {
    m_emitters.clear();
    m_particlesCPU.clear();
    m_sortedIndices.clear();
    m_freeList.clear();
    m_forceFields.clear();
    m_initialized = false;
}

uint32_t GPUParticleSystem::CreateEmitter(const ParticleEmitterConfig& config) {
    auto emitter = std::make_unique<ParticleEmitter>();
    emitter->id = m_nextEmitterId++;
    emitter->config = config;
    emitter->maxParticles = config.maxParticles;

    // Allocate particle range
    if (!AllocateParticles(*emitter)) {
        return 0;  // Failed to allocate
    }

    uint32_t id = emitter->id;
    m_emitters.push_back(std::move(emitter));

    m_stats.totalEmitters++;
    return id;
}

void GPUParticleSystem::DestroyEmitter(uint32_t emitterId) {
    for (auto it = m_emitters.begin(); it != m_emitters.end(); ++it) {
        if ((*it)->id == emitterId) {
            FreeParticles(**it);
            m_emitters.erase(it);
            m_stats.totalEmitters--;
            return;
        }
    }
}

ParticleEmitter* GPUParticleSystem::GetEmitter(uint32_t emitterId) {
    for (auto& emitter : m_emitters) {
        if (emitter->id == emitterId) {
            return emitter.get();
        }
    }
    return nullptr;
}

void GPUParticleSystem::SetEmitterTransform(uint32_t emitterId, const glm::vec3& position,
                                             const glm::quat& rotation, const glm::vec3& scale) {
    ParticleEmitter* emitter = GetEmitter(emitterId);
    if (emitter) {
        emitter->velocity = position - emitter->position;  // Approximate velocity
        emitter->position = position;
        emitter->rotation = rotation;
        emitter->scale = scale;

        // Update world matrix
        glm::mat4 T = glm::translate(glm::mat4(1.0f), position);
        glm::mat4 R = glm::mat4_cast(rotation);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
        emitter->worldMatrix = T * R * S;
    }
}

void GPUParticleSystem::SetEmitterVelocity(uint32_t emitterId, const glm::vec3& velocity) {
    ParticleEmitter* emitter = GetEmitter(emitterId);
    if (emitter) {
        emitter->velocity = velocity;
    }
}

void GPUParticleSystem::Play(uint32_t emitterId) {
    ParticleEmitter* emitter = GetEmitter(emitterId);
    if (emitter) {
        emitter->playing = true;
    }
}

void GPUParticleSystem::Stop(uint32_t emitterId) {
    ParticleEmitter* emitter = GetEmitter(emitterId);
    if (emitter) {
        emitter->playing = false;
    }
}

void GPUParticleSystem::Pause(uint32_t emitterId) {
    ParticleEmitter* emitter = GetEmitter(emitterId);
    if (emitter) {
        emitter->playing = false;
    }
}

void GPUParticleSystem::Restart(uint32_t emitterId) {
    ParticleEmitter* emitter = GetEmitter(emitterId);
    if (emitter) {
        emitter->playing = true;
        emitter->playbackTime = 0.0f;
        emitter->emissionAccumulator = 0.0f;
    }
}

void GPUParticleSystem::StopAll() {
    for (auto& emitter : m_emitters) {
        emitter->playing = false;
    }
}

void GPUParticleSystem::EmitBurst(uint32_t emitterId, uint32_t count) {
    ParticleEmitter* emitter = GetEmitter(emitterId);
    if (emitter) {
        EmitParticles(*emitter, count);
    }
}

void GPUParticleSystem::EmitAtPosition(uint32_t emitterId, const glm::vec3& position, uint32_t count) {
    ParticleEmitter* emitter = GetEmitter(emitterId);
    if (emitter) {
        glm::vec3 oldPos = emitter->position;
        emitter->position = position;
        EmitParticles(*emitter, count);
        emitter->position = oldPos;
    }
}

void GPUParticleSystem::Update(float deltaTime) {
    if (!m_initialized) return;

    m_time += deltaTime;
    m_stats.particlesSpawned = 0;
    m_stats.particlesDied = 0;

    // CPU simulation (would be replaced by compute shader dispatch)
    SimulateCPU(deltaTime);

    // Sort particles for alpha blending
    SortParticles();

    // Update stats
    m_stats.activeEmitters = 0;
    for (const auto& emitter : m_emitters) {
        if (emitter->playing) m_stats.activeEmitters++;
    }
    m_stats.activeParticles = m_activeParticles;
}

void GPUParticleSystem::Render(const glm::mat4& viewProj, const glm::vec3& cameraPos,
                                const glm::vec3& cameraRight, const glm::vec3& cameraUp) {
    // Pack render constant buffer
    m_renderCB.viewProj = viewProj;
    m_renderCB.cameraPosition = glm::vec4(cameraPos, 1.0f);
    m_renderCB.cameraRight = glm::vec4(cameraRight, 0.0f);
    m_renderCB.cameraUp = glm::vec4(cameraUp, 0.0f);
    m_renderCB.time = m_time;

    // Actual rendering would be done by the Renderer using the particle buffer
    // This is a placeholder for the GPU draw calls
}

uint32_t GPUParticleSystem::AddForceField(const ParticleForceField& field) {
    uint32_t id = m_nextForceFieldId++;
    m_forceFields.push_back(field);
    m_forceFieldIds.push_back(id);
    return id;
}

void GPUParticleSystem::RemoveForceField(uint32_t fieldId) {
    for (size_t i = 0; i < m_forceFieldIds.size(); ++i) {
        if (m_forceFieldIds[i] == fieldId) {
            m_forceFields.erase(m_forceFields.begin() + i);
            m_forceFieldIds.erase(m_forceFieldIds.begin() + i);
            return;
        }
    }
}

void GPUParticleSystem::UpdateForceField(uint32_t fieldId, const ParticleForceField& field) {
    for (size_t i = 0; i < m_forceFieldIds.size(); ++i) {
        if (m_forceFieldIds[i] == fieldId) {
            m_forceFields[i] = field;
            return;
        }
    }
}

void GPUParticleSystem::ClearForceFields() {
    m_forceFields.clear();
    m_forceFieldIds.clear();
}

uint32_t GPUParticleSystem::GetActiveParticleCount() const {
    return m_activeParticles;
}

bool GPUParticleSystem::IsEmitterPlaying(uint32_t emitterId) const {
    for (const auto& emitter : m_emitters) {
        if (emitter->id == emitterId) {
            return emitter->playing;
        }
    }
    return false;
}

void GPUParticleSystem::SimulateCPU(float deltaTime) {
    // Emit new particles
    for (auto& emitter : m_emitters) {
        if (!emitter->playing) continue;

        emitter->playbackTime += deltaTime;

        // Continuous emission
        if (emitter->config.emissionRate > 0.0f) {
            emitter->emissionAccumulator += emitter->config.emissionRate * deltaTime;
            uint32_t toEmit = static_cast<uint32_t>(emitter->emissionAccumulator);
            emitter->emissionAccumulator -= static_cast<float>(toEmit);

            if (toEmit > 0) {
                EmitParticles(*emitter, toEmit);
            }
        }

        // Burst emission
        if (emitter->config.burstCount > 0 && emitter->config.burstInterval > 0.0f) {
            emitter->burstTimer += deltaTime;
            if (emitter->burstTimer >= emitter->config.burstInterval) {
                emitter->burstTimer = 0.0f;
                EmitParticles(*emitter, emitter->config.burstCount);
            }
        }
    }

    // Update existing particles
    uint32_t activeCount = 0;
    for (size_t i = 0; i < m_particlesCPU.size(); ++i) {
        GPUParticle& p = m_particlesCPU[i];

        if (p.params.y < 0.0f) continue;  // Dead particle

        // Find emitter for this particle
        uint32_t emitterIdx = static_cast<uint32_t>(p.params.z);
        ParticleEmitter* emitter = nullptr;
        for (auto& e : m_emitters) {
            if (e->id == emitterIdx) {
                emitter = e.get();
                break;
            }
        }

        if (emitter) {
            UpdateParticle(p, deltaTime, *emitter);
        }

        // Check if still alive
        if (p.params.x >= p.params.y) {
            // Kill particle
            p.params.y = -1.0f;
            m_freeList.push_back(static_cast<uint32_t>(i));
            m_stats.particlesDied++;
        } else {
            activeCount++;
        }
    }

    m_activeParticles = activeCount;
}

void GPUParticleSystem::EmitParticles(ParticleEmitter& emitter, uint32_t count) {
    const auto& config = emitter.config;

    for (uint32_t i = 0; i < count && !m_freeList.empty(); ++i) {
        uint32_t idx = m_freeList.back();
        m_freeList.pop_back();

        GPUParticle& p = m_particlesCPU[idx];

        // Position
        glm::vec3 localPos = GetEmissionPosition(emitter);
        glm::vec3 worldPos = glm::vec3(emitter.worldMatrix * glm::vec4(localPos, 1.0f));
        p.position = glm::vec4(worldPos, config.startSize * (1.0f + RandomFloat(-config.sizeVariation, config.sizeVariation)));

        // Velocity
        glm::vec3 localVel = GetEmissionVelocity(emitter);
        glm::vec3 worldVel = glm::mat3(emitter.worldMatrix) * localVel;
        if (config.inheritVelocity) {
            worldVel += emitter.velocity * config.inheritVelocityScale;
        }
        float rotation = (config.startRotation + RandomFloat(-config.rotationVariation, config.rotationVariation)) * PI / 180.0f;
        p.velocity = glm::vec4(worldVel, rotation);

        // Color
        p.color = config.startColor;

        // Params
        float lifetime = config.lifetime * (1.0f + RandomFloat(-config.lifetimeVariation, config.lifetimeVariation));
        p.params = glm::vec4(0.0f, lifetime, static_cast<float>(emitter.id), RandomFloat(0.0f, 1000.0f));

        // Params2
        float rotSpeed = (config.rotationSpeed + RandomFloat(-config.rotationSpeedVariation, config.rotationSpeedVariation)) * PI / 180.0f;
        p.params2 = glm::vec4(rotSpeed, config.startSize, config.endSize, 0.0f);

        // Params3
        p.params3 = glm::vec4(0.0f, config.gravityModifier, config.drag, 0.0f);

        // Sort key (will be updated during sorting)
        p.sortKey = glm::vec4(0.0f);

        emitter.particleCount++;
        m_stats.particlesSpawned++;
    }
}

void GPUParticleSystem::UpdateParticle(GPUParticle& p, float deltaTime, const ParticleEmitter& emitter) {
    const auto& config = emitter.config;

    // Update age
    p.params.x += deltaTime;
    float normalizedAge = p.params.x / p.params.y;

    // Apply gravity
    glm::vec3 gravity(0.0f, config.gravity * p.params3.y, 0.0f);
    glm::vec3 vel(p.velocity);
    vel += gravity * deltaTime;

    // Apply wind
    vel += m_windVector * deltaTime;

    // Apply force fields
    glm::vec3 pos(p.position);
    glm::vec3 forceFieldForce = CalculateForceFieldForce(pos);
    vel += forceFieldForce * deltaTime;

    // Apply curl noise turbulence
    if (config.noiseStrength > 0.0f) {
        glm::vec3 noise = CurlNoise(pos * config.noiseFrequency, m_time * config.noiseSpeed);
        vel += noise * config.noiseStrength * deltaTime;
    }

    // Apply drag
    vel *= 1.0f / (1.0f + config.drag * deltaTime);

    // Update velocity
    p.velocity = glm::vec4(vel, p.velocity.w);

    // Update position
    pos += vel * deltaTime;

    // Collision
    if (config.enableCollision) {
        glm::vec3 hitPos, hitNormal;
        if (CheckCollision(glm::vec3(p.position), vel, deltaTime, hitPos, hitNormal)) {
            pos = hitPos + hitNormal * 0.01f;

            // Reflect velocity
            glm::vec3 reflected = glm::reflect(vel, hitNormal);
            vel = reflected * config.collisionBounce;

            // Apply friction
            glm::vec3 tangent = vel - glm::dot(vel, hitNormal) * hitNormal;
            vel -= tangent * config.collisionFriction;

            p.velocity = glm::vec4(vel, p.velocity.w);

            // Reduce lifetime
            p.params.y *= (1.0f - config.collisionLifetimeLoss);
        }
    }

    p.position = glm::vec4(pos, p.position.w);

    // Update rotation
    p.velocity.w += p.params2.x * deltaTime;

    // Update size over lifetime
    float size = glm::mix(p.params2.y, p.params2.z, normalizedAge);
    p.position.w = size;

    // Update color over lifetime
    if (config.useColorOverLife) {
        p.color = glm::mix(config.startColor, config.endColor, normalizedAge);
    }
    p.params3.x = normalizedAge;

    // Update sort key (distance to camera)
    float distSq = glm::dot(pos - m_cameraPosition, pos - m_cameraPosition);
    p.sortKey.x = distSq;
}

void GPUParticleSystem::SortParticles() {
    // Build sorted indices
    uint32_t count = 0;
    for (size_t i = 0; i < m_particlesCPU.size(); ++i) {
        if (m_particlesCPU[i].params.y >= 0.0f) {
            m_sortedIndices[count++] = static_cast<uint32_t>(i);
        }
    }

    // Sort by depth (back to front for alpha blending)
    std::sort(m_sortedIndices.begin(), m_sortedIndices.begin() + count,
        [this](uint32_t a, uint32_t b) {
            return m_particlesCPU[a].sortKey.x > m_particlesCPU[b].sortKey.x;
        });
}

glm::vec3 GPUParticleSystem::GetEmissionPosition(const ParticleEmitter& emitter) const {
    const auto& config = emitter.config;

    switch (config.shape) {
        case EmitterShape::Point:
            return glm::vec3(0.0f);

        case EmitterShape::Sphere:
            if (config.emitFromEdge) {
                return RandomOnSphere() * config.shapeRadius;
            } else {
                return RandomInSphere() * config.shapeRadius;
            }

        case EmitterShape::Hemisphere: {
            glm::vec3 p = config.emitFromEdge ? RandomOnSphere() : RandomInSphere();
            p.y = std::abs(p.y);
            return p * config.shapeRadius;
        }

        case EmitterShape::Cone: {
            glm::vec3 dir = RandomInCone(config.shapeAngle * PI / 180.0f);
            float dist = RandomFloat(0.0f, config.shapeRadius);
            return dir * dist;
        }

        case EmitterShape::Box: {
            return glm::vec3(
                RandomFloat(-config.shapeSize.x, config.shapeSize.x) * 0.5f,
                RandomFloat(-config.shapeSize.y, config.shapeSize.y) * 0.5f,
                RandomFloat(-config.shapeSize.z, config.shapeSize.z) * 0.5f
            );
        }

        case EmitterShape::Circle: {
            float angle = RandomFloat(0.0f, config.shapeArc * PI / 180.0f);
            float r = config.emitFromEdge ? config.shapeRadius : RandomFloat(0.0f, config.shapeRadius);
            return glm::vec3(std::cos(angle) * r, 0.0f, std::sin(angle) * r);
        }

        case EmitterShape::Edge: {
            float t = RandomFloat(0.0f, 1.0f);
            return glm::vec3(t * config.shapeSize.x - config.shapeSize.x * 0.5f, 0.0f, 0.0f);
        }

        default:
            return glm::vec3(0.0f);
    }
}

glm::vec3 GPUParticleSystem::GetEmissionVelocity(const ParticleEmitter& emitter) const {
    const auto& config = emitter.config;

    glm::vec3 direction = glm::normalize(config.initialVelocity);
    float speed = config.initialSpeed * (1.0f + RandomFloat(-config.speedVariation, config.speedVariation));

    // Modify direction based on shape
    if (config.shape == EmitterShape::Sphere || config.shape == EmitterShape::Hemisphere) {
        direction = RandomOnSphere();
        if (config.shape == EmitterShape::Hemisphere) {
            direction.y = std::abs(direction.y);
        }
    } else if (config.shape == EmitterShape::Cone) {
        direction = RandomInCone(config.shapeAngle * PI / 180.0f);
    }

    return direction * speed;
}

float GPUParticleSystem::RandomFloat(float min, float max) const {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(m_rng);
}

glm::vec3 GPUParticleSystem::RandomInSphere() const {
    glm::vec3 p;
    do {
        p = glm::vec3(
            RandomFloat(-1.0f, 1.0f),
            RandomFloat(-1.0f, 1.0f),
            RandomFloat(-1.0f, 1.0f)
        );
    } while (glm::dot(p, p) > 1.0f);
    return p;
}

glm::vec3 GPUParticleSystem::RandomInCone(float angle) const {
    float cosAngle = std::cos(angle);
    float z = RandomFloat(cosAngle, 1.0f);
    float phi = RandomFloat(0.0f, 2.0f * PI);
    float sinTheta = std::sqrt(1.0f - z * z);
    return glm::vec3(sinTheta * std::cos(phi), sinTheta * std::sin(phi), z);
}

glm::vec3 GPUParticleSystem::RandomOnSphere() const {
    float theta = RandomFloat(0.0f, 2.0f * PI);
    float phi = std::acos(RandomFloat(-1.0f, 1.0f));
    return glm::vec3(
        std::sin(phi) * std::cos(theta),
        std::cos(phi),
        std::sin(phi) * std::sin(theta)
    );
}

bool GPUParticleSystem::CheckCollision(const glm::vec3& pos, const glm::vec3& vel, float deltaTime,
                                        glm::vec3& hitPos, glm::vec3& hitNormal) {
    if (!m_heightQuery) return false;

    glm::vec3 nextPos = pos + vel * deltaTime;
    float groundHeight = m_heightQuery(nextPos.x, nextPos.z);

    if (nextPos.y < groundHeight) {
        hitPos = glm::vec3(nextPos.x, groundHeight, nextPos.z);
        hitNormal = glm::vec3(0.0f, 1.0f, 0.0f);  // Simplified - would sample terrain normal
        return true;
    }

    return false;
}

glm::vec3 GPUParticleSystem::CalculateForceFieldForce(const glm::vec3& position) const {
    glm::vec3 totalForce(0.0f);

    for (const auto& field : m_forceFields) {
        if (!field.enabled) continue;

        glm::vec3 toField = field.position - position;
        float distance = glm::length(toField);

        if (distance < field.radius && distance > 0.001f) {
            float falloff = std::pow(1.0f - distance / field.radius, field.falloff);
            glm::vec3 direction = toField / distance;
            totalForce += direction * field.strength * falloff;
        }
    }

    return totalForce;
}

glm::vec3 GPUParticleSystem::CurlNoise(const glm::vec3& pos, float time) const {
    // Simplified curl noise approximation
    float eps = 0.01f;

    auto noise = [&](float x, float y, float z) {
        float n = std::sin(x * 1.0f + time) * std::cos(y * 1.3f) * std::sin(z * 0.9f + time * 0.5f);
        n += std::sin(x * 2.1f) * std::cos(y * 1.7f + time * 0.3f) * std::sin(z * 2.3f) * 0.5f;
        return n;
    };

    float dx = (noise(pos.x + eps, pos.y, pos.z) - noise(pos.x - eps, pos.y, pos.z)) / (2.0f * eps);
    float dy = (noise(pos.x, pos.y + eps, pos.z) - noise(pos.x, pos.y - eps, pos.z)) / (2.0f * eps);
    float dz = (noise(pos.x, pos.y, pos.z + eps) - noise(pos.x, pos.y, pos.z - eps)) / (2.0f * eps);

    return glm::vec3(dy - dz, dz - dx, dx - dy);
}

bool GPUParticleSystem::AllocateParticles(ParticleEmitter& emitter) {
    // Simple allocation - particles share global pool
    emitter.particleOffset = 0;
    emitter.particleCount = 0;
    return true;
}

void GPUParticleSystem::FreeParticles(ParticleEmitter& emitter) {
    // Kill all particles belonging to this emitter
    for (size_t i = 0; i < m_particlesCPU.size(); ++i) {
        if (static_cast<uint32_t>(m_particlesCPU[i].params.z) == emitter.id) {
            if (m_particlesCPU[i].params.y >= 0.0f) {
                m_particlesCPU[i].params.y = -1.0f;
                m_freeList.push_back(static_cast<uint32_t>(i));
            }
        }
    }
    emitter.particleCount = 0;
}

// Default emitter configurations
ParticleEmitterConfig CreateFireEmitter() {
    ParticleEmitterConfig config;
    config.name = "Fire";
    config.shape = EmitterShape::Cone;
    config.shapeAngle = 15.0f;
    config.shapeRadius = 0.3f;
    config.emissionRate = 200.0f;
    config.initialSpeed = 3.0f;
    config.speedVariation = 0.3f;
    config.lifetime = 1.0f;
    config.lifetimeVariation = 0.3f;
    config.startSize = 0.2f;
    config.endSize = 0.05f;
    config.startColor = glm::vec4(1.0f, 0.8f, 0.2f, 1.0f);
    config.endColor = glm::vec4(1.0f, 0.2f, 0.0f, 0.0f);
    config.gravity = 0.0f;
    config.gravityModifier = -0.5f;  // Rise up
    config.blendMode = ParticleBlendMode::Additive;
    config.noiseStrength = 1.0f;
    config.noiseFrequency = 2.0f;
    return config;
}

ParticleEmitterConfig CreateSmokeEmitter() {
    ParticleEmitterConfig config;
    config.name = "Smoke";
    config.shape = EmitterShape::Cone;
    config.shapeAngle = 30.0f;
    config.emissionRate = 50.0f;
    config.initialSpeed = 1.0f;
    config.lifetime = 4.0f;
    config.startSize = 0.3f;
    config.endSize = 1.5f;
    config.startColor = glm::vec4(0.3f, 0.3f, 0.3f, 0.6f);
    config.endColor = glm::vec4(0.5f, 0.5f, 0.5f, 0.0f);
    config.gravity = 0.0f;
    config.gravityModifier = -0.3f;
    config.drag = 0.5f;
    config.blendMode = ParticleBlendMode::AlphaBlend;
    config.noiseStrength = 0.5f;
    config.rotationSpeed = 20.0f;
    return config;
}

ParticleEmitterConfig CreateSparkEmitter() {
    ParticleEmitterConfig config;
    config.name = "Sparks";
    config.shape = EmitterShape::Sphere;
    config.emissionRate = 0.0f;
    config.burstCount = 50;
    config.initialSpeed = 8.0f;
    config.speedVariation = 0.5f;
    config.lifetime = 0.8f;
    config.startSize = 0.02f;
    config.endSize = 0.01f;
    config.startColor = glm::vec4(1.0f, 0.9f, 0.5f, 1.0f);
    config.endColor = glm::vec4(1.0f, 0.3f, 0.0f, 0.0f);
    config.gravity = -9.81f;
    config.drag = 0.3f;
    config.blendMode = ParticleBlendMode::Additive;
    config.renderMode = ParticleRenderMode::StretchedBillboard;
    config.enableCollision = true;
    config.collisionBounce = 0.3f;
    return config;
}

ParticleEmitterConfig CreateDustEmitter() {
    ParticleEmitterConfig config;
    config.name = "Dust";
    config.shape = EmitterShape::Sphere;
    config.shapeRadius = 0.5f;
    config.emissionRate = 30.0f;
    config.initialSpeed = 0.5f;
    config.lifetime = 3.0f;
    config.startSize = 0.1f;
    config.endSize = 0.3f;
    config.startColor = glm::vec4(0.6f, 0.55f, 0.45f, 0.4f);
    config.endColor = glm::vec4(0.6f, 0.55f, 0.45f, 0.0f);
    config.gravity = -0.5f;
    config.drag = 2.0f;
    config.noiseStrength = 0.3f;
    return config;
}

ParticleEmitterConfig CreateBloodEmitter() {
    ParticleEmitterConfig config;
    config.name = "Blood";
    config.shape = EmitterShape::Cone;
    config.shapeAngle = 45.0f;
    config.burstCount = 30;
    config.initialSpeed = 5.0f;
    config.lifetime = 0.6f;
    config.startSize = 0.03f;
    config.endSize = 0.02f;
    config.startColor = glm::vec4(0.5f, 0.0f, 0.0f, 1.0f);
    config.endColor = glm::vec4(0.3f, 0.0f, 0.0f, 0.5f);
    config.gravity = -9.81f;
    config.enableCollision = true;
    config.collisionBounce = 0.0f;
    return config;
}

ParticleEmitterConfig CreateMagicEmitter() {
    ParticleEmitterConfig config;
    config.name = "Magic";
    config.shape = EmitterShape::Sphere;
    config.shapeRadius = 0.5f;
    config.emitFromEdge = true;
    config.emissionRate = 100.0f;
    config.initialSpeed = 0.0f;
    config.lifetime = 1.5f;
    config.startSize = 0.1f;
    config.endSize = 0.0f;
    config.startColor = glm::vec4(0.3f, 0.5f, 1.0f, 1.0f);
    config.endColor = glm::vec4(0.8f, 0.3f, 1.0f, 0.0f);
    config.gravity = 0.0f;
    config.blendMode = ParticleBlendMode::Additive;
    config.noiseStrength = 2.0f;
    config.noiseFrequency = 3.0f;
    return config;
}

ParticleEmitterConfig CreateWaterfallEmitter() {
    ParticleEmitterConfig config;
    config.name = "Waterfall";
    config.shape = EmitterShape::Edge;
    config.shapeSize = glm::vec3(5.0f, 0.0f, 0.0f);
    config.emissionRate = 500.0f;
    config.initialVelocity = glm::vec3(0.0f, -1.0f, 1.0f);
    config.initialSpeed = 2.0f;
    config.lifetime = 2.0f;
    config.startSize = 0.1f;
    config.endSize = 0.2f;
    config.startColor = glm::vec4(0.8f, 0.9f, 1.0f, 0.7f);
    config.endColor = glm::vec4(0.9f, 0.95f, 1.0f, 0.0f);
    config.gravity = -9.81f;
    config.drag = 0.1f;
    config.enableCollision = true;
    config.splashChance = 0.5f;
    return config;
}

ParticleEmitterConfig CreateLeafEmitter() {
    ParticleEmitterConfig config;
    config.name = "Leaves";
    config.shape = EmitterShape::Box;
    config.shapeSize = glm::vec3(10.0f, 0.0f, 10.0f);
    config.emissionRate = 5.0f;
    config.initialSpeed = 0.5f;
    config.lifetime = 8.0f;
    config.startSize = 0.05f;
    config.endSize = 0.05f;
    config.startColor = glm::vec4(0.4f, 0.6f, 0.2f, 1.0f);
    config.endColor = glm::vec4(0.6f, 0.5f, 0.2f, 0.8f);
    config.gravity = -1.0f;
    config.drag = 3.0f;
    config.rotationSpeed = 180.0f;
    config.rotationSpeedVariation = 90.0f;
    config.noiseStrength = 1.5f;
    config.noiseFrequency = 0.5f;
    config.enableCollision = true;
    return config;
}

} // namespace Cortex::Graphics
