#pragma once

// GPUParticles.h
// Compute shader-based GPU particle system.
// Supports millions of particles with physics, collision, and sorting.
//
// Reference: "GPU Gems 3: Real-Time Simulation and Rendering of 3D Fluids"
// Reference: "Destiny's Multithreaded Rendering Architecture" - GDC 2017

#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include <cstdint>

namespace Cortex::Graphics {

// Forward declarations
class Renderer;

// Particle blend mode
enum class ParticleBlendMode : uint8_t {
    Additive = 0,       // Add to background (fire, sparks)
    AlphaBlend = 1,     // Standard alpha blend (smoke, dust)
    Premultiplied = 2,  // Premultiplied alpha
    Multiply = 3,       // Darken (shadows)
    SoftAdditive = 4    // Soft light addition
};

// Particle render mode
enum class ParticleRenderMode : uint8_t {
    Billboard = 0,      // Camera-facing quad
    StretchedBillboard = 1, // Stretched along velocity
    HorizontalBillboard = 2, // Flat horizontal
    VerticalBillboard = 3,   // Vertical, rotates to face camera
    Mesh = 4,           // 3D mesh instance
    Trail = 5           // Connected ribbon trail
};

// Emitter shape
enum class EmitterShape : uint8_t {
    Point = 0,
    Sphere = 1,
    Hemisphere = 2,
    Cone = 3,
    Box = 4,
    Circle = 5,
    Edge = 6,
    Mesh = 7            // Emit from mesh surface
};

// GPU particle data (112 bytes, aligned)
struct alignas(16) GPUParticle {
    glm::vec4 position;         // xyz=position, w=size
    glm::vec4 velocity;         // xyz=velocity, w=rotation
    glm::vec4 color;            // rgba
    glm::vec4 params;           // x=age, y=lifetime, z=emitterIdx, w=seed
    glm::vec4 params2;          // x=rotSpeed, y=sizeStart, z=sizeEnd, w=flags
    glm::vec4 params3;          // x=colorLerp, y=gravityMod, z=dragCoeff, w=unused
    glm::vec4 sortKey;          // x=depth, y=unused, z=unused, w=unused
};

// Emitter configuration
struct ParticleEmitterConfig {
    std::string name;

    // Emission
    EmitterShape shape = EmitterShape::Point;
    float emissionRate = 100.0f;        // Particles per second
    uint32_t burstCount = 0;            // Instant burst (0 = continuous)
    float burstInterval = 0.0f;         // Time between bursts

    // Shape parameters
    glm::vec3 shapeSize = glm::vec3(1.0f);
    float shapeRadius = 1.0f;
    float shapeAngle = 45.0f;           // Cone angle
    float shapeArc = 360.0f;            // Arc for circle/cone
    bool emitFromEdge = false;          // Edge vs volume emission

    // Initial velocity
    glm::vec3 initialVelocity = glm::vec3(0.0f, 1.0f, 0.0f);
    float initialSpeed = 5.0f;
    float speedVariation = 0.2f;        // +/- percentage
    bool inheritVelocity = false;       // Inherit emitter velocity
    float inheritVelocityScale = 1.0f;

    // Lifetime
    float lifetime = 2.0f;
    float lifetimeVariation = 0.2f;

    // Size
    float startSize = 0.1f;
    float endSize = 0.05f;
    float sizeVariation = 0.2f;

    // Color
    glm::vec4 startColor = glm::vec4(1.0f);
    glm::vec4 endColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
    bool useColorOverLife = true;

    // Rotation
    float startRotation = 0.0f;
    float rotationVariation = 180.0f;   // Degrees
    float rotationSpeed = 0.0f;
    float rotationSpeedVariation = 90.0f;

    // Physics
    float gravity = -9.81f;
    float gravityModifier = 1.0f;
    float drag = 0.1f;
    glm::vec3 constantForce = glm::vec3(0.0f);

    // Collision
    bool enableCollision = false;
    float collisionBounce = 0.5f;
    float collisionFriction = 0.1f;
    float collisionLifetimeLoss = 0.2f; // Lose 20% lifetime on bounce

    // Noise/turbulence
    float noiseStrength = 0.0f;
    float noiseFrequency = 1.0f;
    float noiseSpeed = 1.0f;

    // Rendering
    ParticleRenderMode renderMode = ParticleRenderMode::Billboard;
    ParticleBlendMode blendMode = ParticleBlendMode::AlphaBlend;
    std::string texturePath;
    uint32_t textureSheetX = 1;         // Flipbook columns
    uint32_t textureSheetY = 1;         // Flipbook rows
    float animationSpeed = 1.0f;        // Flipbook FPS
    bool softParticles = true;          // Depth fade
    float softParticleDistance = 0.5f;

    // Trail (for Trail render mode)
    uint32_t trailLength = 10;
    float trailMinDistance = 0.1f;

    // LOD
    float lodNearDistance = 20.0f;
    float lodFarDistance = 100.0f;
    float lodFarSizeScale = 2.0f;       // Increase size at distance
    float lodFarEmissionScale = 0.3f;   // Reduce emission at distance

    // Limits
    uint32_t maxParticles = 10000;
};

// Emitter instance (runtime state)
struct ParticleEmitter {
    uint32_t id = 0;
    ParticleEmitterConfig config;

    // Transform
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
    glm::vec3 velocity = glm::vec3(0.0f);  // For velocity inheritance

    // State
    bool playing = true;
    bool loop = true;
    float playbackTime = 0.0f;
    float emissionAccumulator = 0.0f;
    float burstTimer = 0.0f;

    // GPU buffer offsets
    uint32_t particleOffset = 0;
    uint32_t particleCount = 0;
    uint32_t maxParticles = 0;

    // Cached matrix
    glm::mat4 worldMatrix = glm::mat4(1.0f);
};

// GPU constant buffer for particle simulation
struct alignas(16) ParticleSimulateCB {
    glm::vec4 emitterPosition;      // xyz=pos, w=deltaTime
    glm::vec4 emitterVelocity;      // xyz=vel, w=time
    glm::vec4 emitterParams;        // x=rate, y=lifetime, z=lifeVar, w=speedVar
    glm::vec4 gravityWind;          // xyz=gravity+wind, w=drag
    glm::vec4 noiseParams;          // x=strength, y=freq, z=speed, w=unused
    glm::vec4 collisionParams;      // x=bounce, y=friction, z=lifeLoss, w=enabled
    glm::vec4 sizeParams;           // x=startSize, y=endSize, z=sizeVar, w=unused
    glm::mat4 emitterMatrix;
    uint32_t maxParticles;
    uint32_t activeParticles;
    uint32_t emitterIndex;
    uint32_t randomSeed;
};

// GPU constant buffer for particle rendering
struct alignas(16) ParticleRenderCB {
    glm::mat4 viewProj;
    glm::mat4 invView;
    glm::vec4 cameraPosition;
    glm::vec4 cameraRight;
    glm::vec4 cameraUp;
    glm::vec4 texSheetParams;       // x=cols, y=rows, z=animSpeed, w=unused
    glm::vec4 softParams;           // x=enabled, y=distance, z=unused, w=unused
    float time;
    float aspectRatio;
    uint32_t renderMode;
    uint32_t padding;
};

// Particle system statistics
struct ParticleStats {
    uint32_t totalEmitters = 0;
    uint32_t activeEmitters = 0;
    uint32_t totalParticles = 0;
    uint32_t activeParticles = 0;
    uint32_t particlesSpawned = 0;
    uint32_t particlesDied = 0;
    float gpuSimulateTimeMs = 0.0f;
    float gpuSortTimeMs = 0.0f;
    float gpuRenderTimeMs = 0.0f;
};

// Height query for collision
using HeightQueryFunc = std::function<float(float x, float z)>;

// Attractor/repulsor force field
struct ParticleForceField {
    glm::vec3 position;
    float radius;
    float strength;         // Positive = attract, negative = repel
    float falloff;          // Falloff exponent
    bool enabled = true;
};

class GPUParticleSystem {
public:
    GPUParticleSystem();
    ~GPUParticleSystem();

    // Initialize
    bool Initialize(uint32_t maxTotalParticles = 1000000);
    void Shutdown();

    // Emitter management
    uint32_t CreateEmitter(const ParticleEmitterConfig& config);
    void DestroyEmitter(uint32_t emitterId);
    ParticleEmitter* GetEmitter(uint32_t emitterId);
    void SetEmitterTransform(uint32_t emitterId, const glm::vec3& position,
                             const glm::quat& rotation = glm::quat(1,0,0,0),
                             const glm::vec3& scale = glm::vec3(1.0f));
    void SetEmitterVelocity(uint32_t emitterId, const glm::vec3& velocity);

    // Playback control
    void Play(uint32_t emitterId);
    void Stop(uint32_t emitterId);
    void Pause(uint32_t emitterId);
    void Restart(uint32_t emitterId);
    void StopAll();

    // Burst emission
    void EmitBurst(uint32_t emitterId, uint32_t count);
    void EmitAtPosition(uint32_t emitterId, const glm::vec3& position, uint32_t count);

    // Update (simulation)
    void Update(float deltaTime);

    // Render (called by renderer)
    void Render(const glm::mat4& viewProj, const glm::vec3& cameraPos,
                const glm::vec3& cameraRight, const glm::vec3& cameraUp);

    // Force fields
    uint32_t AddForceField(const ParticleForceField& field);
    void RemoveForceField(uint32_t fieldId);
    void UpdateForceField(uint32_t fieldId, const ParticleForceField& field);
    void ClearForceFields();

    // Collision
    void SetHeightQuery(HeightQueryFunc func) { m_heightQuery = func; }
    void SetDepthTexture(void* depthSRV) { m_depthTextureSRV = depthSRV; }

    // Wind integration
    void SetWindVector(const glm::vec3& wind) { m_windVector = wind; }

    // Query
    const ParticleStats& GetStats() const { return m_stats; }
    uint32_t GetActiveParticleCount() const;
    bool IsEmitterPlaying(uint32_t emitterId) const;

    // LOD
    void SetCameraPosition(const glm::vec3& pos) { m_cameraPosition = pos; }

    // GPU buffers for external access
    const std::vector<GPUParticle>& GetParticleBuffer() const { return m_particlesCPU; }
    const ParticleRenderCB& GetRenderConstants() const { return m_renderCB; }

private:
    // Simulation (CPU fallback)
    void SimulateCPU(float deltaTime);
    void EmitParticles(ParticleEmitter& emitter, uint32_t count);
    void UpdateParticle(GPUParticle& particle, float deltaTime, const ParticleEmitter& emitter);
    void SortParticles();

    // Particle emission helpers
    glm::vec3 GetEmissionPosition(const ParticleEmitter& emitter) const;
    glm::vec3 GetEmissionVelocity(const ParticleEmitter& emitter) const;

    // Random utilities
    float RandomFloat(float min, float max) const;
    glm::vec3 RandomInSphere() const;
    glm::vec3 RandomInCone(float angle) const;
    glm::vec3 RandomOnSphere() const;

    // Collision
    bool CheckCollision(const glm::vec3& pos, const glm::vec3& vel, float deltaTime,
                        glm::vec3& hitPos, glm::vec3& hitNormal);

    // Force field application
    glm::vec3 CalculateForceFieldForce(const glm::vec3& position) const;

    // Curl noise
    glm::vec3 CurlNoise(const glm::vec3& pos, float time) const;

    // Allocate particle range for emitter
    bool AllocateParticles(ParticleEmitter& emitter);
    void FreeParticles(ParticleEmitter& emitter);

private:
    bool m_initialized = false;
    uint32_t m_maxTotalParticles = 0;

    // Emitters
    std::vector<std::unique_ptr<ParticleEmitter>> m_emitters;
    uint32_t m_nextEmitterId = 1;

    // Particle buffer (CPU for now, would be GPU buffer)
    std::vector<GPUParticle> m_particlesCPU;
    std::vector<uint32_t> m_freeList;       // Free particle indices
    uint32_t m_activeParticles = 0;

    // Sorted indices for rendering
    std::vector<uint32_t> m_sortedIndices;

    // Force fields
    std::vector<ParticleForceField> m_forceFields;
    std::vector<uint32_t> m_forceFieldIds;
    uint32_t m_nextForceFieldId = 1;

    // External integrations
    HeightQueryFunc m_heightQuery;
    void* m_depthTextureSRV = nullptr;
    glm::vec3 m_windVector = glm::vec3(0.0f);
    glm::vec3 m_cameraPosition = glm::vec3(0.0f);

    // Time
    float m_time = 0.0f;

    // Random
    mutable std::mt19937 m_rng;

    // Constant buffers
    ParticleSimulateCB m_simulateCB;
    ParticleRenderCB m_renderCB;

    // Statistics
    ParticleStats m_stats;
};

// Default emitter configurations
ParticleEmitterConfig CreateFireEmitter();
ParticleEmitterConfig CreateSmokeEmitter();
ParticleEmitterConfig CreateSparkEmitter();
ParticleEmitterConfig CreateDustEmitter();
ParticleEmitterConfig CreateBloodEmitter();
ParticleEmitterConfig CreateMagicEmitter();
ParticleEmitterConfig CreateWaterfallEmitter();
ParticleEmitterConfig CreateLeafEmitter();

} // namespace Cortex::Graphics
