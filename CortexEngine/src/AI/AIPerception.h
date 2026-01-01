#pragma once

// AIPerception.h
// AI sensory system for perceiving entities and environment.
// Supports sight, hearing, and custom senses with memory.
// Reference: "Game AI Pro" - Awareness System Architecture

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <string>

namespace Cortex::AI {

// Forward declarations
class AIPerceptionSystem;
class AISense;

// Stimulus types
enum class StimulusType : uint8_t {
    Sight,          // Visual detection
    Sound,          // Audio detection
    Damage,         // Taking damage
    Touch,          // Physical contact
    Custom          // User-defined
};

// Affiliation for threat assessment
enum class Affiliation : uint8_t {
    Neutral,
    Friendly,
    Hostile,
    Unknown
};

// Stimulus event
struct AIStimulus {
    StimulusType type = StimulusType::Sight;
    uint32_t sourceEntityId = UINT32_MAX;   // Entity that caused the stimulus
    uint32_t targetEntityId = UINT32_MAX;   // Entity receiving the stimulus
    glm::vec3 location = glm::vec3(0.0f);
    float strength = 1.0f;                   // 0-1, used for priority
    float age = 0.0f;                        // Time since stimulus was received
    float expirationTime = 5.0f;             // When to forget this stimulus
    std::string tag;                         // Optional identifier
    bool isExpired = false;

    // Additional data for specific stimulus types
    float radius = 0.0f;                     // For sounds
    glm::vec3 velocity = glm::vec3(0.0f);   // For moving targets
};

// Perceived entity memory
struct PerceivedEntity {
    uint32_t entityId = UINT32_MAX;
    Affiliation affiliation = Affiliation::Unknown;

    // Current perception state
    bool isCurrentlySeen = false;
    bool isCurrentlyHeard = false;

    // Last known state
    glm::vec3 lastKnownPosition = glm::vec3(0.0f);
    glm::vec3 lastKnownVelocity = glm::vec3(0.0f);
    float lastSeenTime = 0.0f;
    float lastHeardTime = 0.0f;
    float firstPerceivedTime = 0.0f;

    // Threat assessment
    float threatLevel = 0.0f;               // 0-1
    float distance = FLT_MAX;
    bool isTarget = false;

    // Prediction
    glm::vec3 PredictPosition(float futureTime) const;
    float GetTimeSinceLastSeen(float currentTime) const;
    float GetTimeSinceLastHeard(float currentTime) const;
    bool IsStale(float currentTime, float staleThreshold = 5.0f) const;
};

// Sight sense configuration
struct SightSenseConfig {
    float maxDistance = 50.0f;
    float peripheralAngle = 60.0f;          // Half-angle in degrees
    float focusAngle = 15.0f;               // Tight cone for focused detection
    float heightOffset = 1.7f;              // Eye height
    bool requiresLineOfSight = true;
    float detectionTimeNear = 0.0f;         // Instant detection close up
    float detectionTimeFar = 1.0f;          // Detection time at max range

    // Detection modifiers
    float stealthMultiplier = 1.0f;         // Applied to target's stealth
    float lightingMultiplier = 1.0f;        // Lighting affects detection
};

// Hearing sense configuration
struct HearingSenseConfig {
    float maxDistance = 30.0f;
    float minDistance = 1.0f;               // Full volume within this range
    float heightOffset = 1.7f;              // Ear height
    bool occlusionEnabled = true;           // Sound blocked by walls
    float occlusionMultiplier = 0.3f;       // Volume reduction through walls
};

// Damage sense configuration
struct DamageSenseConfig {
    bool enabled = true;
    float threatBoostOnDamage = 0.5f;       // Increase threat when damaged by entity
    float memoryDuration = 30.0f;           // Remember damage source
};

// Base sense class
class AISense {
public:
    AISense(const std::string& name) : m_name(name) {}
    virtual ~AISense() = default;

    // Process a stimulus
    virtual bool CanHandle(StimulusType type) const = 0;
    virtual float CalculateStrength(const AIStimulus& stimulus,
                                     const glm::vec3& perceiverPos,
                                     const glm::vec3& perceiverForward) const = 0;

    // Enable/disable
    bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool enabled) { m_enabled = enabled; }

    const std::string& GetName() const { return m_name; }

protected:
    std::string m_name;
    bool m_enabled = true;
};

// Sight sense implementation
class SightSense : public AISense {
public:
    SightSense() : AISense("Sight") {}

    bool CanHandle(StimulusType type) const override { return type == StimulusType::Sight; }
    float CalculateStrength(const AIStimulus& stimulus,
                            const glm::vec3& perceiverPos,
                            const glm::vec3& perceiverForward) const override;

    SightSenseConfig config;

    // Line of sight checking (set externally)
    using LineOfSightFunc = std::function<bool(const glm::vec3& from, const glm::vec3& to)>;
    LineOfSightFunc lineOfSightCheck;
};

// Hearing sense implementation
class HearingSense : public AISense {
public:
    HearingSense() : AISense("Hearing") {}

    bool CanHandle(StimulusType type) const override { return type == StimulusType::Sound; }
    float CalculateStrength(const AIStimulus& stimulus,
                            const glm::vec3& perceiverPos,
                            const glm::vec3& perceiverForward) const override;

    HearingSenseConfig config;

    // Occlusion checking
    using OcclusionFunc = std::function<bool(const glm::vec3& from, const glm::vec3& to)>;
    OcclusionFunc occlusionCheck;
};

// Damage sense implementation
class DamageSense : public AISense {
public:
    DamageSense() : AISense("Damage") {}

    bool CanHandle(StimulusType type) const override { return type == StimulusType::Damage; }
    float CalculateStrength(const AIStimulus& stimulus,
                            const glm::vec3& perceiverPos,
                            const glm::vec3& perceiverForward) const override;

    DamageSenseConfig config;
};

// AI perception component for an individual entity
class AIPerceptionComponent {
public:
    AIPerceptionComponent();
    ~AIPerceptionComponent() = default;

    // Update perception (called by system)
    void Update(float deltaTime, float currentTime);

    // Process incoming stimulus
    void ProcessStimulus(const AIStimulus& stimulus, float currentTime);

    // Query perception
    bool IsEntityPerceived(uint32_t entityId) const;
    const PerceivedEntity* GetPerceivedEntity(uint32_t entityId) const;
    PerceivedEntity* GetPerceivedEntity(uint32_t entityId);

    // Get all perceived entities
    const std::vector<PerceivedEntity>& GetPerceivedEntities() const { return m_perceivedEntities; }

    // Get highest threat
    const PerceivedEntity* GetHighestThreat() const;

    // Get nearest visible entity
    const PerceivedEntity* GetNearestVisible(Affiliation filter = Affiliation::Hostile) const;

    // Forget entity
    void ForgetEntity(uint32_t entityId);
    void ForgetAll();

    // Senses
    SightSense& GetSightSense() { return m_sightSense; }
    HearingSense& GetHearingSense() { return m_hearingSense; }
    DamageSense& GetDamageSense() { return m_damageSense; }

    // Owner position/orientation (must be set by system)
    glm::vec3 ownerPosition = glm::vec3(0.0f);
    glm::vec3 ownerForward = glm::vec3(0.0f, 0.0f, 1.0f);
    uint32_t ownerEntityId = UINT32_MAX;

    // Affiliation lookup callback
    using AffiliationFunc = std::function<Affiliation(uint32_t entityId)>;
    AffiliationFunc getAffiliation;

    // Events
    using EntityPerceivedCallback = std::function<void(const PerceivedEntity&)>;
    using EntityLostCallback = std::function<void(uint32_t entityId)>;

    EntityPerceivedCallback onEntityPerceived;
    EntityLostCallback onEntityLost;

private:
    void UpdatePerceivedEntity(const AIStimulus& stimulus, float strength, float currentTime);
    void RemoveStaleEntities(float currentTime);
    void UpdateThreatLevels();

    SightSense m_sightSense;
    HearingSense m_hearingSense;
    DamageSense m_damageSense;

    std::vector<PerceivedEntity> m_perceivedEntities;
    std::vector<AIStimulus> m_activeStimuli;

    float m_forgetTime = 10.0f;             // Time to forget unseen entities
    float m_currentTime = 0.0f;
};

// Global perception system manager
class AIPerceptionSystem {
public:
    AIPerceptionSystem() = default;
    ~AIPerceptionSystem() = default;

    // Singleton access (optional)
    static AIPerceptionSystem& Get();

    // Update all perception components
    void Update(float deltaTime);

    // Broadcast stimulus to all nearby perceivers
    void BroadcastStimulus(const AIStimulus& stimulus);

    // Register/unregister perception components
    void RegisterComponent(AIPerceptionComponent* component);
    void UnregisterComponent(AIPerceptionComponent* component);

    // Sound generation helpers
    void GenerateSound(uint32_t sourceEntity, const glm::vec3& position,
                       float radius, float strength = 1.0f,
                       const std::string& tag = "");

    // Visual stimulus helpers
    void GenerateSightStimulus(uint32_t sourceEntity, const glm::vec3& position,
                                const glm::vec3& velocity = glm::vec3(0.0f));

    // Damage notification
    void NotifyDamage(uint32_t attackerEntity, uint32_t victimEntity,
                      float damageAmount, const glm::vec3& hitPosition);

    // Query
    std::vector<AIPerceptionComponent*> GetComponentsInRange(const glm::vec3& position, float radius) const;

    // Current time (for memory timestamps)
    float GetCurrentTime() const { return m_currentTime; }

private:
    std::vector<AIPerceptionComponent*> m_components;
    float m_currentTime = 0.0f;
    float m_updateInterval = 0.1f;          // 10Hz update rate
    float m_timeSinceUpdate = 0.0f;
};

// Threat assessment utilities
namespace ThreatAssessment {

// Calculate threat level based on various factors
float CalculateThreatLevel(const PerceivedEntity& entity,
                           const glm::vec3& selfPosition,
                           float selfHealth,
                           float selfMaxHealth);

// Compare threat priority
bool CompareThreat(const PerceivedEntity& a, const PerceivedEntity& b);

// Get threat label
const char* GetThreatLabel(float threatLevel);

} // namespace ThreatAssessment

// Detection helpers
namespace Detection {

// Check if target is in field of view
bool IsInFieldOfView(const glm::vec3& observerPos,
                     const glm::vec3& observerForward,
                     const glm::vec3& targetPos,
                     float halfAngleDegrees);

// Calculate detection time based on distance
float CalculateDetectionTime(float distance, float maxDistance,
                              float detectionTimeNear, float detectionTimeFar);

// Calculate sound attenuation
float CalculateSoundAttenuation(float distance, float minDistance, float maxDistance);

} // namespace Detection

} // namespace Cortex::AI
