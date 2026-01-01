// AIPerception.cpp
// Implementation of AI perception system.

#include "AIPerception.h"
#include <algorithm>
#include <cmath>

namespace Cortex::AI {

// ============================================================================
// PerceivedEntity
// ============================================================================

glm::vec3 PerceivedEntity::PredictPosition(float futureTime) const {
    return lastKnownPosition + lastKnownVelocity * futureTime;
}

float PerceivedEntity::GetTimeSinceLastSeen(float currentTime) const {
    return currentTime - lastSeenTime;
}

float PerceivedEntity::GetTimeSinceLastHeard(float currentTime) const {
    return currentTime - lastHeardTime;
}

bool PerceivedEntity::IsStale(float currentTime, float staleThreshold) const {
    float timeSinceSeen = GetTimeSinceLastSeen(currentTime);
    float timeSinceHeard = GetTimeSinceLastHeard(currentTime);
    return timeSinceSeen > staleThreshold && timeSinceHeard > staleThreshold;
}

// ============================================================================
// SightSense
// ============================================================================

float SightSense::CalculateStrength(const AIStimulus& stimulus,
                                     const glm::vec3& perceiverPos,
                                     const glm::vec3& perceiverForward) const {
    if (!m_enabled) return 0.0f;

    glm::vec3 eyePos = perceiverPos + glm::vec3(0, config.heightOffset, 0);
    glm::vec3 toTarget = stimulus.location - eyePos;
    float distance = glm::length(toTarget);

    // Distance check
    if (distance > config.maxDistance) {
        return 0.0f;
    }

    if (distance < 0.001f) {
        return 1.0f;  // Right on top of us
    }

    glm::vec3 toTargetDir = toTarget / distance;

    // Field of view check
    float dot = glm::dot(perceiverForward, toTargetDir);
    float angleRad = std::acos(glm::clamp(dot, -1.0f, 1.0f));
    float angleDeg = glm::degrees(angleRad);

    if (angleDeg > config.peripheralAngle) {
        return 0.0f;  // Outside peripheral vision
    }

    // Line of sight check
    if (config.requiresLineOfSight && lineOfSightCheck) {
        if (!lineOfSightCheck(eyePos, stimulus.location)) {
            return 0.0f;  // Blocked by geometry
        }
    }

    // Calculate strength based on distance and angle
    float distanceFactor = 1.0f - (distance / config.maxDistance);

    // Bonus for being in focus cone
    float angleFactor = 1.0f;
    if (angleDeg < config.focusAngle) {
        angleFactor = 1.5f;  // Focused detection bonus
    } else {
        // Linear falloff in peripheral vision
        float t = (angleDeg - config.focusAngle) / (config.peripheralAngle - config.focusAngle);
        angleFactor = 1.0f - (t * 0.5f);  // 50% reduction at edge
    }

    float strength = distanceFactor * angleFactor * stimulus.strength;

    // Apply modifiers
    strength *= config.stealthMultiplier;
    strength *= config.lightingMultiplier;

    return glm::clamp(strength, 0.0f, 1.0f);
}

// ============================================================================
// HearingSense
// ============================================================================

float HearingSense::CalculateStrength(const AIStimulus& stimulus,
                                       const glm::vec3& perceiverPos,
                                       const glm::vec3& perceiverForward) const {
    if (!m_enabled) return 0.0f;

    glm::vec3 earPos = perceiverPos + glm::vec3(0, config.heightOffset, 0);
    float distance = glm::length(stimulus.location - earPos);

    // Check against sound radius
    float effectiveRange = stimulus.radius > 0.0f ? stimulus.radius : config.maxDistance;
    if (distance > effectiveRange) {
        return 0.0f;
    }

    // Calculate attenuation
    float strength = Detection::CalculateSoundAttenuation(
        distance, config.minDistance, effectiveRange);

    strength *= stimulus.strength;

    // Apply occlusion
    if (config.occlusionEnabled && occlusionCheck) {
        if (occlusionCheck(earPos, stimulus.location)) {
            strength *= config.occlusionMultiplier;
        }
    }

    return glm::clamp(strength, 0.0f, 1.0f);
}

// ============================================================================
// DamageSense
// ============================================================================

float DamageSense::CalculateStrength(const AIStimulus& stimulus,
                                      const glm::vec3& perceiverPos,
                                      const glm::vec3& perceiverForward) const {
    if (!m_enabled || !config.enabled) return 0.0f;

    // Damage is always fully perceived
    return stimulus.strength;
}

// ============================================================================
// AIPerceptionComponent
// ============================================================================

AIPerceptionComponent::AIPerceptionComponent() {
    // Default affiliation lookup returns Unknown
    getAffiliation = [](uint32_t) { return Affiliation::Unknown; };
}

void AIPerceptionComponent::Update(float deltaTime, float currentTime) {
    m_currentTime = currentTime;

    // Update stimulus ages and remove expired
    for (auto it = m_activeStimuli.begin(); it != m_activeStimuli.end(); ) {
        it->age += deltaTime;
        if (it->age > it->expirationTime) {
            it->isExpired = true;
            it = m_activeStimuli.erase(it);
        } else {
            ++it;
        }
    }

    // Update perceived entity states
    for (auto& entity : m_perceivedEntities) {
        // Check if still visible/audible
        bool stillSeen = false;
        bool stillHeard = false;

        for (const auto& stim : m_activeStimuli) {
            if (stim.sourceEntityId != entity.entityId) continue;

            if (stim.type == StimulusType::Sight) {
                float strength = m_sightSense.CalculateStrength(stim, ownerPosition, ownerForward);
                if (strength > 0.1f) stillSeen = true;
            }
            if (stim.type == StimulusType::Sound) {
                float strength = m_hearingSense.CalculateStrength(stim, ownerPosition, ownerForward);
                if (strength > 0.1f) stillHeard = true;
            }
        }

        entity.isCurrentlySeen = stillSeen;
        entity.isCurrentlyHeard = stillHeard;
    }

    // Remove stale entities
    RemoveStaleEntities(currentTime);

    // Update threat levels
    UpdateThreatLevels();
}

void AIPerceptionComponent::ProcessStimulus(const AIStimulus& stimulus, float currentTime) {
    // Skip stimuli from self
    if (stimulus.sourceEntityId == ownerEntityId) return;

    // Calculate strength using appropriate sense
    float strength = 0.0f;

    switch (stimulus.type) {
        case StimulusType::Sight:
            strength = m_sightSense.CalculateStrength(stimulus, ownerPosition, ownerForward);
            break;
        case StimulusType::Sound:
            strength = m_hearingSense.CalculateStrength(stimulus, ownerPosition, ownerForward);
            break;
        case StimulusType::Damage:
            strength = m_damageSense.CalculateStrength(stimulus, ownerPosition, ownerForward);
            break;
        default:
            strength = stimulus.strength;
            break;
    }

    if (strength < 0.01f) return;  // Below threshold

    // Store the stimulus
    m_activeStimuli.push_back(stimulus);

    // Update perceived entity
    if (stimulus.sourceEntityId != UINT32_MAX) {
        UpdatePerceivedEntity(stimulus, strength, currentTime);
    }
}

void AIPerceptionComponent::UpdatePerceivedEntity(const AIStimulus& stimulus,
                                                   float strength,
                                                   float currentTime) {
    // Find or create perceived entity
    PerceivedEntity* entity = GetPerceivedEntity(stimulus.sourceEntityId);
    bool isNew = (entity == nullptr);

    if (isNew) {
        m_perceivedEntities.push_back(PerceivedEntity());
        entity = &m_perceivedEntities.back();
        entity->entityId = stimulus.sourceEntityId;
        entity->firstPerceivedTime = currentTime;
        entity->affiliation = getAffiliation(stimulus.sourceEntityId);
    }

    // Update last known info
    entity->lastKnownPosition = stimulus.location;
    entity->lastKnownVelocity = stimulus.velocity;
    entity->distance = glm::length(stimulus.location - ownerPosition);

    // Update per-sense timestamps
    switch (stimulus.type) {
        case StimulusType::Sight:
            entity->lastSeenTime = currentTime;
            entity->isCurrentlySeen = true;
            break;
        case StimulusType::Sound:
            entity->lastHeardTime = currentTime;
            entity->isCurrentlyHeard = true;
            break;
        case StimulusType::Damage:
            entity->threatLevel = glm::min(entity->threatLevel +
                                           m_damageSense.config.threatBoostOnDamage, 1.0f);
            break;
        default:
            break;
    }

    // Fire event for new perception
    if (isNew && onEntityPerceived) {
        onEntityPerceived(*entity);
    }
}

void AIPerceptionComponent::RemoveStaleEntities(float currentTime) {
    for (auto it = m_perceivedEntities.begin(); it != m_perceivedEntities.end(); ) {
        if (it->IsStale(currentTime, m_forgetTime)) {
            uint32_t entityId = it->entityId;
            it = m_perceivedEntities.erase(it);
            if (onEntityLost) {
                onEntityLost(entityId);
            }
        } else {
            ++it;
        }
    }
}

void AIPerceptionComponent::UpdateThreatLevels() {
    for (auto& entity : m_perceivedEntities) {
        // Base threat on affiliation
        float baseThreat = 0.0f;
        switch (entity.affiliation) {
            case Affiliation::Hostile:
                baseThreat = 0.7f;
                break;
            case Affiliation::Neutral:
                baseThreat = 0.2f;
                break;
            case Affiliation::Friendly:
                baseThreat = 0.0f;
                break;
            default:
                baseThreat = 0.3f;
                break;
        }

        // Modify by distance (closer = more threatening)
        float distanceFactor = 1.0f;
        if (entity.distance > 0.0f) {
            distanceFactor = glm::clamp(50.0f / entity.distance, 0.5f, 2.0f);
        }

        // Modify by visibility (seen = more threatening)
        float visibilityFactor = entity.isCurrentlySeen ? 1.2f : 0.8f;

        // Calculate final threat (preserve damage boost)
        float calculatedThreat = baseThreat * distanceFactor * visibilityFactor;
        entity.threatLevel = glm::max(entity.threatLevel * 0.95f, calculatedThreat);
        entity.threatLevel = glm::clamp(entity.threatLevel, 0.0f, 1.0f);
    }
}

bool AIPerceptionComponent::IsEntityPerceived(uint32_t entityId) const {
    for (const auto& entity : m_perceivedEntities) {
        if (entity.entityId == entityId) return true;
    }
    return false;
}

const PerceivedEntity* AIPerceptionComponent::GetPerceivedEntity(uint32_t entityId) const {
    for (const auto& entity : m_perceivedEntities) {
        if (entity.entityId == entityId) return &entity;
    }
    return nullptr;
}

PerceivedEntity* AIPerceptionComponent::GetPerceivedEntity(uint32_t entityId) {
    for (auto& entity : m_perceivedEntities) {
        if (entity.entityId == entityId) return &entity;
    }
    return nullptr;
}

const PerceivedEntity* AIPerceptionComponent::GetHighestThreat() const {
    const PerceivedEntity* highest = nullptr;
    float highestThreat = 0.0f;

    for (const auto& entity : m_perceivedEntities) {
        if (entity.threatLevel > highestThreat) {
            highestThreat = entity.threatLevel;
            highest = &entity;
        }
    }

    return highest;
}

const PerceivedEntity* AIPerceptionComponent::GetNearestVisible(Affiliation filter) const {
    const PerceivedEntity* nearest = nullptr;
    float nearestDist = FLT_MAX;

    for (const auto& entity : m_perceivedEntities) {
        if (!entity.isCurrentlySeen) continue;
        if (filter != Affiliation::Unknown && entity.affiliation != filter) continue;

        if (entity.distance < nearestDist) {
            nearestDist = entity.distance;
            nearest = &entity;
        }
    }

    return nearest;
}

void AIPerceptionComponent::ForgetEntity(uint32_t entityId) {
    for (auto it = m_perceivedEntities.begin(); it != m_perceivedEntities.end(); ++it) {
        if (it->entityId == entityId) {
            m_perceivedEntities.erase(it);
            break;
        }
    }

    // Also remove active stimuli from this entity
    m_activeStimuli.erase(
        std::remove_if(m_activeStimuli.begin(), m_activeStimuli.end(),
            [entityId](const AIStimulus& s) { return s.sourceEntityId == entityId; }),
        m_activeStimuli.end());
}

void AIPerceptionComponent::ForgetAll() {
    m_perceivedEntities.clear();
    m_activeStimuli.clear();
}

// ============================================================================
// AIPerceptionSystem
// ============================================================================

AIPerceptionSystem& AIPerceptionSystem::Get() {
    static AIPerceptionSystem instance;
    return instance;
}

void AIPerceptionSystem::Update(float deltaTime) {
    m_timeSinceUpdate += deltaTime;
    m_currentTime += deltaTime;

    // Throttle updates
    if (m_timeSinceUpdate < m_updateInterval) {
        return;
    }
    m_timeSinceUpdate = 0.0f;

    // Update all components
    for (auto* component : m_components) {
        if (component) {
            component->Update(m_updateInterval, m_currentTime);
        }
    }
}

void AIPerceptionSystem::BroadcastStimulus(const AIStimulus& stimulus) {
    // Find components in range based on stimulus type
    float range = stimulus.radius;
    if (stimulus.type == StimulusType::Sight) {
        range = 100.0f;  // Large range for sight
    }

    for (auto* component : m_components) {
        if (!component) continue;

        float dist = glm::length(component->ownerPosition - stimulus.location);
        if (dist <= range) {
            component->ProcessStimulus(stimulus, m_currentTime);
        }
    }
}

void AIPerceptionSystem::RegisterComponent(AIPerceptionComponent* component) {
    if (!component) return;

    // Check if already registered
    for (auto* c : m_components) {
        if (c == component) return;
    }

    m_components.push_back(component);
}

void AIPerceptionSystem::UnregisterComponent(AIPerceptionComponent* component) {
    m_components.erase(
        std::remove(m_components.begin(), m_components.end(), component),
        m_components.end());
}

void AIPerceptionSystem::GenerateSound(uint32_t sourceEntity, const glm::vec3& position,
                                        float radius, float strength, const std::string& tag) {
    AIStimulus stimulus;
    stimulus.type = StimulusType::Sound;
    stimulus.sourceEntityId = sourceEntity;
    stimulus.location = position;
    stimulus.radius = radius;
    stimulus.strength = strength;
    stimulus.tag = tag;
    stimulus.expirationTime = 1.0f;  // Short-lived

    BroadcastStimulus(stimulus);
}

void AIPerceptionSystem::GenerateSightStimulus(uint32_t sourceEntity, const glm::vec3& position,
                                                const glm::vec3& velocity) {
    AIStimulus stimulus;
    stimulus.type = StimulusType::Sight;
    stimulus.sourceEntityId = sourceEntity;
    stimulus.location = position;
    stimulus.velocity = velocity;
    stimulus.strength = 1.0f;
    stimulus.radius = 100.0f;  // Wide range for sight checks
    stimulus.expirationTime = 0.2f;  // Need continuous updates

    BroadcastStimulus(stimulus);
}

void AIPerceptionSystem::NotifyDamage(uint32_t attackerEntity, uint32_t victimEntity,
                                       float damageAmount, const glm::vec3& hitPosition) {
    AIStimulus stimulus;
    stimulus.type = StimulusType::Damage;
    stimulus.sourceEntityId = attackerEntity;
    stimulus.targetEntityId = victimEntity;
    stimulus.location = hitPosition;
    stimulus.strength = glm::clamp(damageAmount / 100.0f, 0.1f, 1.0f);
    stimulus.expirationTime = 30.0f;

    // Only broadcast to the victim
    for (auto* component : m_components) {
        if (component && component->ownerEntityId == victimEntity) {
            component->ProcessStimulus(stimulus, m_currentTime);
            break;
        }
    }
}

std::vector<AIPerceptionComponent*> AIPerceptionSystem::GetComponentsInRange(
    const glm::vec3& position, float radius) const {

    std::vector<AIPerceptionComponent*> result;

    for (auto* component : m_components) {
        if (!component) continue;

        float dist = glm::length(component->ownerPosition - position);
        if (dist <= radius) {
            result.push_back(component);
        }
    }

    return result;
}

// ============================================================================
// ThreatAssessment Namespace
// ============================================================================

namespace ThreatAssessment {

float CalculateThreatLevel(const PerceivedEntity& entity,
                           const glm::vec3& selfPosition,
                           float selfHealth,
                           float selfMaxHealth) {
    float threat = 0.0f;

    // Base threat from affiliation
    switch (entity.affiliation) {
        case Affiliation::Hostile:
            threat = 0.5f;
            break;
        case Affiliation::Neutral:
            threat = 0.1f;
            break;
        case Affiliation::Friendly:
            threat = 0.0f;
            break;
        default:
            threat = 0.2f;
            break;
    }

    // Distance factor (closer = more threat)
    float distance = glm::length(entity.lastKnownPosition - selfPosition);
    float distanceThreat = 0.0f;
    if (distance < 5.0f) {
        distanceThreat = 0.4f;
    } else if (distance < 15.0f) {
        distanceThreat = 0.2f;
    } else if (distance < 30.0f) {
        distanceThreat = 0.1f;
    }

    threat += distanceThreat;

    // Visibility factor
    if (entity.isCurrentlySeen) {
        threat += 0.1f;
    }

    // Health factor (lower health = more threatened)
    if (selfMaxHealth > 0.0f) {
        float healthRatio = selfHealth / selfMaxHealth;
        if (healthRatio < 0.3f) {
            threat *= 1.5f;  // More threatened when low health
        }
    }

    return glm::clamp(threat, 0.0f, 1.0f);
}

bool CompareThreat(const PerceivedEntity& a, const PerceivedEntity& b) {
    // Higher threat first
    if (a.threatLevel != b.threatLevel) {
        return a.threatLevel > b.threatLevel;
    }
    // If equal threat, prioritize visible
    if (a.isCurrentlySeen != b.isCurrentlySeen) {
        return a.isCurrentlySeen;
    }
    // If still equal, prioritize closer
    return a.distance < b.distance;
}

const char* GetThreatLabel(float threatLevel) {
    if (threatLevel >= 0.8f) return "Critical";
    if (threatLevel >= 0.6f) return "High";
    if (threatLevel >= 0.4f) return "Medium";
    if (threatLevel >= 0.2f) return "Low";
    return "Minimal";
}

} // namespace ThreatAssessment

// ============================================================================
// Detection Namespace
// ============================================================================

namespace Detection {

bool IsInFieldOfView(const glm::vec3& observerPos,
                     const glm::vec3& observerForward,
                     const glm::vec3& targetPos,
                     float halfAngleDegrees) {
    glm::vec3 toTarget = glm::normalize(targetPos - observerPos);
    float dot = glm::dot(observerForward, toTarget);
    float angleRad = std::acos(glm::clamp(dot, -1.0f, 1.0f));
    float angleDeg = glm::degrees(angleRad);
    return angleDeg <= halfAngleDegrees;
}

float CalculateDetectionTime(float distance, float maxDistance,
                              float detectionTimeNear, float detectionTimeFar) {
    if (distance <= 0.0f) return detectionTimeNear;
    if (distance >= maxDistance) return detectionTimeFar;

    float t = distance / maxDistance;
    return glm::mix(detectionTimeNear, detectionTimeFar, t);
}

float CalculateSoundAttenuation(float distance, float minDistance, float maxDistance) {
    if (distance <= minDistance) return 1.0f;
    if (distance >= maxDistance) return 0.0f;

    // Inverse distance falloff (approximating real-world sound)
    float t = (distance - minDistance) / (maxDistance - minDistance);
    return 1.0f - (t * t);  // Quadratic falloff
}

} // namespace Detection

} // namespace Cortex::AI
