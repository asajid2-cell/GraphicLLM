// DecalManager.cpp
// Implementation of the deferred decal system.

#include "DecalManager.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

namespace Cortex::Graphics {

// Decal bounds calculation
void Decal::UpdateBounds() {
    // Get the 8 corners of the OBB
    glm::vec3 halfSize = size * 0.5f;
    glm::vec3 corners[8] = {
        glm::vec3(-halfSize.x, -halfSize.y, -halfSize.z),
        glm::vec3( halfSize.x, -halfSize.y, -halfSize.z),
        glm::vec3(-halfSize.x,  halfSize.y, -halfSize.z),
        glm::vec3( halfSize.x,  halfSize.y, -halfSize.z),
        glm::vec3(-halfSize.x, -halfSize.y,  halfSize.z),
        glm::vec3( halfSize.x, -halfSize.y,  halfSize.z),
        glm::vec3(-halfSize.x,  halfSize.y,  halfSize.z),
        glm::vec3( halfSize.x,  halfSize.y,  halfSize.z)
    };

    // Transform corners to world space
    glm::mat3 rotMat = glm::mat3_cast(rotation);

    aabbMin = glm::vec3(std::numeric_limits<float>::max());
    aabbMax = glm::vec3(std::numeric_limits<float>::lowest());

    for (int i = 0; i < 8; i++) {
        glm::vec3 worldCorner = position + rotMat * corners[i];
        aabbMin = glm::min(aabbMin, worldCorner);
        aabbMax = glm::max(aabbMax, worldCorner);
    }
}

DecalManager::DecalManager() {
    // Pre-allocate storage
    m_decals.reserve(m_maxDecals);
    m_activeDecals.reserve(m_maxDecals);
    m_freeIndices.reserve(m_maxDecals);
}

DecalManager::~DecalManager() {
    Shutdown();
}

void DecalManager::Initialize(Renderer* renderer) {
    m_renderer = renderer;

    // Initialize pool with all indices free
    m_decals.resize(m_maxDecals);
    for (uint32_t i = 0; i < m_maxDecals; i++) {
        m_freeIndices.push_back(m_maxDecals - 1 - i);  // Reverse order for pop_back
        m_decals[i].id = 0;  // Mark as free
    }
}

void DecalManager::Shutdown() {
    m_decals.clear();
    m_activeDecals.clear();
    m_freeIndices.clear();
    m_templates.clear();
}

void DecalManager::Update(float deltaTime) {
    if (!m_enabled) {
        return;
    }

    // Update and remove expired decals
    auto it = m_activeDecals.begin();
    while (it != m_activeDecals.end()) {
        uint32_t index = *it;
        Decal& decal = m_decals[index];

        // Update age
        if (decal.lifetime > 0.0f) {
            decal.age += deltaTime;

            // Check if expired
            if (decal.age >= decal.lifetime) {
                // Start fade out
                float fadeProgress = (decal.age - decal.lifetime) / decal.fadeOutTime;
                if (fadeProgress >= 1.0f) {
                    // Fully faded, remove
                    FreeDecal(index);
                    it = m_activeDecals.erase(it);
                    continue;
                }
            }
        }

        ++it;
    }
}

void DecalManager::RegisterTemplate(const std::string& name, const DecalTemplate& decalTemplate) {
    m_templates[name] = decalTemplate;
}

const DecalTemplate* DecalManager::GetTemplate(const std::string& name) const {
    auto it = m_templates.find(name);
    if (it != m_templates.end()) {
        return &it->second;
    }
    return nullptr;
}

uint32_t DecalManager::AllocateDecal() {
    if (m_freeIndices.empty()) {
        // Pool exhausted, try to find lowest priority decal to replace
        uint32_t lowestPriority = FindLowestPriorityDecal();
        if (lowestPriority != UINT32_MAX) {
            // Remove and reuse this slot
            auto it = std::find(m_activeDecals.begin(), m_activeDecals.end(), lowestPriority);
            if (it != m_activeDecals.end()) {
                m_activeDecals.erase(it);
            }
            return lowestPriority;
        }
        return UINT32_MAX;  // Cannot allocate
    }

    uint32_t index = m_freeIndices.back();
    m_freeIndices.pop_back();
    return index;
}

void DecalManager::FreeDecal(uint32_t index) {
    if (index < m_decals.size()) {
        m_decals[index].id = 0;  // Mark as free
        m_decals[index].enabled = false;
        m_freeIndices.push_back(index);
    }
}

uint32_t DecalManager::FindLowestPriorityDecal() const {
    uint32_t lowestIndex = UINT32_MAX;
    int lowestPriority = static_cast<int>(DecalPriority::COUNT);
    float oldestAge = 0.0f;

    for (uint32_t index : m_activeDecals) {
        const Decal& decal = m_decals[index];
        int priority = static_cast<int>(decal.priority);

        // Find lowest priority, or if same priority, oldest
        if (priority < lowestPriority ||
            (priority == lowestPriority && decal.age > oldestAge)) {
            lowestPriority = priority;
            oldestAge = decal.age;
            lowestIndex = index;
        }
    }

    return lowestIndex;
}

uint32_t DecalManager::SpawnDecal(const Decal& decal) {
    uint32_t index = AllocateDecal();
    if (index == UINT32_MAX) {
        return 0;  // Failed to allocate
    }

    m_decals[index] = decal;
    m_decals[index].id = m_nextId++;
    m_decals[index].age = 0.0f;
    m_decals[index].UpdateBounds();

    m_activeDecals.push_back(index);

    return m_decals[index].id;
}

uint32_t DecalManager::SpawnFromTemplate(const std::string& templateName,
                                          const glm::vec3& position,
                                          const glm::vec3& normal,
                                          float scale) {
    const DecalTemplate* tmpl = GetTemplate(templateName);
    if (!tmpl) {
        return 0;
    }

    // Create rotation from normal
    glm::vec3 up = glm::abs(normal.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 right = glm::normalize(glm::cross(up, normal));
    up = glm::cross(normal, right);

    glm::mat3 rotMat(right, up, normal);
    glm::quat rotation = glm::quat_cast(rotMat);

    // Apply random rotation around normal
    if (tmpl->rotationVariation > 0.0f) {
        float angle = RandomFloat(0.0f, glm::radians(tmpl->rotationVariation));
        rotation = rotation * glm::angleAxis(angle, glm::vec3(0, 0, 1));
    }

    return SpawnFromTemplate(templateName, position, rotation, scale);
}

uint32_t DecalManager::SpawnFromTemplate(const std::string& templateName,
                                          const glm::vec3& position,
                                          const glm::quat& rotation,
                                          float scale) {
    const DecalTemplate* tmpl = GetTemplate(templateName);
    if (!tmpl) {
        return 0;
    }

    Decal decal;
    decal.position = position;
    decal.rotation = rotation;

    // Random size within range
    glm::vec3 size = RandomVector3(tmpl->sizeMin, tmpl->sizeMax);
    size *= scale;

    // Apply size variation
    if (tmpl->sizeVariation > 0.0f) {
        float variation = 1.0f + RandomFloat(-tmpl->sizeVariation, tmpl->sizeVariation);
        size *= variation;
    }
    decal.size = size;

    // TODO: Look up texture indices from names
    decal.albedoTexIndex = 0;
    decal.normalTexIndex = 0;
    decal.maskTexIndex = 0;

    // Color with variation
    decal.color = tmpl->color;
    if (tmpl->colorVariation > 0.0f) {
        glm::vec3 variation = glm::vec3(
            RandomFloat(-tmpl->colorVariation, tmpl->colorVariation),
            RandomFloat(-tmpl->colorVariation, tmpl->colorVariation),
            RandomFloat(-tmpl->colorVariation, tmpl->colorVariation)
        );
        decal.color = glm::vec4(glm::clamp(glm::vec3(decal.color) + variation, 0.0f, 1.0f), decal.color.a);
    }

    decal.normalStrength = tmpl->normalStrength;
    decal.roughnessModifier = tmpl->roughnessModifier;
    decal.metallicModifier = tmpl->metallicModifier;
    decal.blendMode = tmpl->blendMode;
    decal.channels = tmpl->channels;
    decal.priority = tmpl->priority;
    decal.lifetime = tmpl->lifetime;
    decal.fadeDistance = tmpl->fadeDistance;
    decal.angleFade = tmpl->angleFade;

    return SpawnDecal(decal);
}

void DecalManager::RemoveDecal(uint32_t id) {
    for (auto it = m_activeDecals.begin(); it != m_activeDecals.end(); ++it) {
        if (m_decals[*it].id == id) {
            FreeDecal(*it);
            m_activeDecals.erase(it);
            return;
        }
    }
}

void DecalManager::RemoveAllDecals() {
    for (uint32_t index : m_activeDecals) {
        FreeDecal(index);
    }
    m_activeDecals.clear();
}

void DecalManager::RemoveDecalsInRadius(const glm::vec3& center, float radius) {
    float radiusSq = radius * radius;

    auto it = m_activeDecals.begin();
    while (it != m_activeDecals.end()) {
        const Decal& decal = m_decals[*it];
        float distSq = glm::dot(decal.position - center, decal.position - center);

        if (distSq <= radiusSq) {
            FreeDecal(*it);
            it = m_activeDecals.erase(it);
        } else {
            ++it;
        }
    }
}

void DecalManager::RemoveDecalsOlderThan(float age) {
    auto it = m_activeDecals.begin();
    while (it != m_activeDecals.end()) {
        const Decal& decal = m_decals[*it];

        if (decal.age >= age) {
            FreeDecal(*it);
            it = m_activeDecals.erase(it);
        } else {
            ++it;
        }
    }
}

Decal* DecalManager::GetDecal(uint32_t id) {
    for (uint32_t index : m_activeDecals) {
        if (m_decals[index].id == id) {
            return &m_decals[index];
        }
    }
    return nullptr;
}

const Decal* DecalManager::GetDecal(uint32_t id) const {
    for (uint32_t index : m_activeDecals) {
        if (m_decals[index].id == id) {
            return &m_decals[index];
        }
    }
    return nullptr;
}

bool DecalManager::IsDecalVisible(const Decal& decal,
                                   const glm::vec3& cameraPos,
                                   const glm::mat4& viewProj) const {
    if (!decal.enabled) {
        return false;
    }

    // Distance culling
    float distSq = glm::dot(decal.position - cameraPos, decal.position - cameraPos);
    float maxDist = decal.fadeDistance * 2.0f;  // Fade completely at 2x fade distance
    if (distSq > maxDist * maxDist) {
        return false;
    }

    // Frustum culling (AABB test against frustum planes)
    // Transform AABB corners to clip space
    glm::vec3 center = (decal.aabbMin + decal.aabbMax) * 0.5f;
    glm::vec3 extents = (decal.aabbMax - decal.aabbMin) * 0.5f;

    glm::vec4 clipCenter = viewProj * glm::vec4(center, 1.0f);

    // Simple sphere frustum test (conservative)
    float radius = glm::length(extents);
    if (clipCenter.w > 0) {
        float ndcX = clipCenter.x / clipCenter.w;
        float ndcY = clipCenter.y / clipCenter.w;
        float ndcRadius = radius / clipCenter.w;

        if (ndcX < -1.0f - ndcRadius || ndcX > 1.0f + ndcRadius ||
            ndcY < -1.0f - ndcRadius || ndcY > 1.0f + ndcRadius) {
            return false;
        }
    }

    return true;
}

void DecalManager::CullDecals(const glm::vec3& cameraPos,
                               const glm::mat4& viewProj,
                               std::vector<uint32_t>& visibleDecals) {
    visibleDecals.clear();
    visibleDecals.reserve(m_activeDecals.size());

    for (uint32_t index : m_activeDecals) {
        if (IsDecalVisible(m_decals[index], cameraPos, viewProj)) {
            visibleDecals.push_back(index);
        }
    }
}

void DecalManager::SortDecals(std::vector<uint32_t>& decals, const glm::vec3& cameraPos) {
    // Sort by priority first, then by distance (back to front for correct blending)
    std::sort(decals.begin(), decals.end(),
        [this, &cameraPos](uint32_t a, uint32_t b) {
            const Decal& decalA = m_decals[a];
            const Decal& decalB = m_decals[b];

            // Priority takes precedence
            if (decalA.priority != decalB.priority) {
                return static_cast<int>(decalA.priority) < static_cast<int>(decalB.priority);
            }

            // Same priority: sort by distance (back to front)
            float distA = glm::dot(decalA.position - cameraPos, decalA.position - cameraPos);
            float distB = glm::dot(decalB.position - cameraPos, decalB.position - cameraPos);
            return distA > distB;
        });
}

void DecalManager::BatchDecals(const std::vector<uint32_t>& decals,
                                std::vector<DecalBatch>& batches) {
    batches.clear();

    for (uint32_t index : decals) {
        const Decal& decal = m_decals[index];

        // Find existing batch with matching properties
        bool foundBatch = false;
        for (DecalBatch& batch : batches) {
            if (batch.albedoTexIndex == decal.albedoTexIndex &&
                batch.normalTexIndex == decal.normalTexIndex &&
                batch.maskTexIndex == decal.maskTexIndex &&
                batch.blendMode == decal.blendMode) {
                batch.decalIndices.push_back(index);
                foundBatch = true;
                break;
            }
        }

        if (!foundBatch) {
            DecalBatch newBatch;
            newBatch.albedoTexIndex = decal.albedoTexIndex;
            newBatch.normalTexIndex = decal.normalTexIndex;
            newBatch.maskTexIndex = decal.maskTexIndex;
            newBatch.blendMode = decal.blendMode;
            newBatch.decalIndices.push_back(index);
            batches.push_back(newBatch);
        }
    }
}

DecalCB DecalManager::GetDecalCB(uint32_t decalId) const {
    DecalCB cb = {};

    const Decal* decal = GetDecal(decalId);
    if (!decal) {
        return cb;
    }

    // Build decal matrix (world to decal local space)
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), -decal->position);
    glm::mat4 rotation = glm::mat4_cast(glm::inverse(decal->rotation));
    glm::mat4 scale = glm::scale(glm::mat4(1.0f), 1.0f / decal->size);

    cb.decalMatrix = scale * rotation * translation;
    cb.decalMatrixInv = glm::inverse(cb.decalMatrix);

    // Color with fade
    cb.decalColor = decal->color;

    // Apply fade-in
    if (decal->age < decal->fadeInTime) {
        cb.decalColor.a *= decal->age / decal->fadeInTime;
    }

    // Apply fade-out
    if (decal->lifetime > 0.0f && decal->age > decal->lifetime) {
        float fadeProgress = (decal->age - decal->lifetime) / decal->fadeOutTime;
        cb.decalColor.a *= 1.0f - glm::clamp(fadeProgress, 0.0f, 1.0f);
    }

    cb.decalColor.a *= m_fadeMultiplier;

    // Parameters
    cb.decalParams = glm::vec4(
        decal->normalStrength,
        decal->roughnessModifier,
        decal->metallicModifier,
        decal->angleFade
    );

    float ageRatio = decal->lifetime > 0.0f ? decal->age / decal->lifetime : 0.0f;
    cb.decalParams2 = glm::vec4(
        decal->fadeDistance,
        ageRatio,
        static_cast<float>(decal->blendMode),
        static_cast<float>(decal->channels)
    );

    cb.decalSize = glm::vec4(decal->size, 0.0f);

    return cb;
}

void DecalManager::LoadTemplatesFromConfig(const std::string& configPath) {
    std::ifstream file(configPath);
    if (!file.is_open()) {
        return;
    }

    try {
        nlohmann::json config = nlohmann::json::parse(file);

        if (config.contains("templates")) {
            for (auto& [name, data] : config["templates"].items()) {
                DecalTemplate tmpl;
                tmpl.name = name;

                if (data.contains("albedoTexture")) {
                    tmpl.albedoTexture = data["albedoTexture"].get<std::string>();
                }
                if (data.contains("normalTexture")) {
                    tmpl.normalTexture = data["normalTexture"].get<std::string>();
                }
                if (data.contains("maskTexture")) {
                    tmpl.maskTexture = data["maskTexture"].get<std::string>();
                }

                if (data.contains("sizeMin")) {
                    auto arr = data["sizeMin"];
                    tmpl.sizeMin = glm::vec3(arr[0], arr[1], arr[2]);
                }
                if (data.contains("sizeMax")) {
                    auto arr = data["sizeMax"];
                    tmpl.sizeMax = glm::vec3(arr[0], arr[1], arr[2]);
                }

                if (data.contains("color")) {
                    auto arr = data["color"];
                    tmpl.color = glm::vec4(arr[0], arr[1], arr[2], arr[3]);
                }

                if (data.contains("normalStrength")) {
                    tmpl.normalStrength = data["normalStrength"].get<float>();
                }
                if (data.contains("roughnessModifier")) {
                    tmpl.roughnessModifier = data["roughnessModifier"].get<float>();
                }
                if (data.contains("metallicModifier")) {
                    tmpl.metallicModifier = data["metallicModifier"].get<float>();
                }

                if (data.contains("lifetime")) {
                    tmpl.lifetime = data["lifetime"].get<float>();
                }
                if (data.contains("fadeDistance")) {
                    tmpl.fadeDistance = data["fadeDistance"].get<float>();
                }
                if (data.contains("angleFade")) {
                    tmpl.angleFade = data["angleFade"].get<float>();
                }

                if (data.contains("rotationVariation")) {
                    tmpl.rotationVariation = data["rotationVariation"].get<float>();
                }
                if (data.contains("sizeVariation")) {
                    tmpl.sizeVariation = data["sizeVariation"].get<float>();
                }
                if (data.contains("colorVariation")) {
                    tmpl.colorVariation = data["colorVariation"].get<float>();
                }

                if (data.contains("priority")) {
                    std::string priority = data["priority"].get<std::string>();
                    if (priority == "verylow") tmpl.priority = DecalPriority::VeryLow;
                    else if (priority == "low") tmpl.priority = DecalPriority::Low;
                    else if (priority == "normal") tmpl.priority = DecalPriority::Normal;
                    else if (priority == "high") tmpl.priority = DecalPriority::High;
                    else if (priority == "veryhigh") tmpl.priority = DecalPriority::VeryHigh;
                }

                RegisterTemplate(name, tmpl);
            }
        }
    } catch (const std::exception& e) {
        // Log error
    }
}

float DecalManager::RandomFloat(float min, float max) {
    m_randomSeed = m_randomSeed * 1103515245 + 12345;
    float normalized = static_cast<float>(m_randomSeed & 0x7FFFFFFF) / static_cast<float>(0x7FFFFFFF);
    return min + normalized * (max - min);
}

glm::vec3 DecalManager::RandomVector3(const glm::vec3& min, const glm::vec3& max) {
    return glm::vec3(
        RandomFloat(min.x, max.x),
        RandomFloat(min.y, max.y),
        RandomFloat(min.z, max.z)
    );
}

// DecalSpawner implementation
DecalSpawner::DecalSpawner(DecalManager* manager)
    : m_manager(manager)
{
}

void DecalSpawner::SpawnFootprint(const glm::vec3& position,
                                   const glm::vec3& forward,
                                   bool isLeftFoot,
                                   const std::string& surfaceType) {
    std::string templateName = "footprint_" + surfaceType;
    if (!m_manager->GetTemplate(templateName)) {
        templateName = "footprint_default";
    }

    glm::vec3 normal(0, 1, 0);

    // Offset slightly for left/right foot
    glm::vec3 right = glm::normalize(glm::cross(forward, normal));
    glm::vec3 offset = right * (isLeftFoot ? -0.1f : 0.1f);

    m_manager->SpawnFromTemplate(templateName, position + offset, normal, 1.0f);
}

void DecalSpawner::SpawnTireTrack(const glm::vec3& start,
                                   const glm::vec3& end,
                                   float width,
                                   const std::string& surfaceType) {
    std::string templateName = "tiretrack_" + surfaceType;
    if (!m_manager->GetTemplate(templateName)) {
        templateName = "tiretrack_default";
    }

    glm::vec3 direction = end - start;
    float length = glm::length(direction);
    if (length < 0.01f) return;

    glm::vec3 center = (start + end) * 0.5f;
    glm::vec3 forward = direction / length;
    glm::vec3 normal(0, 1, 0);

    // Build rotation
    glm::vec3 right = glm::normalize(glm::cross(normal, forward));
    glm::vec3 up = glm::cross(forward, right);
    glm::mat3 rotMat(right, up, forward);
    glm::quat rotation = glm::quat_cast(rotMat);

    Decal decal;
    decal.position = center;
    decal.rotation = rotation;
    decal.size = glm::vec3(width, 0.1f, length);
    decal.priority = DecalPriority::Low;
    decal.lifetime = 30.0f;
    decal.fadeDistance = 30.0f;

    m_manager->SpawnDecal(decal);
}

void DecalSpawner::SpawnBulletHole(const glm::vec3& position,
                                    const glm::vec3& normal,
                                    const std::string& surfaceType) {
    std::string templateName = "bullethole_" + surfaceType;
    if (!m_manager->GetTemplate(templateName)) {
        templateName = "bullethole_default";
    }

    m_manager->SpawnFromTemplate(templateName, position, normal, 1.0f);
}

void DecalSpawner::SpawnExplosionMark(const glm::vec3& position,
                                       float radius,
                                       float intensity) {
    m_manager->SpawnFromTemplate("explosion_scorch", position, glm::vec3(0, 1, 0), radius * intensity);
}

void DecalSpawner::SpawnBloodSplatter(const glm::vec3& position,
                                       const glm::vec3& direction,
                                       float intensity) {
    // Spawn main splatter
    m_manager->SpawnFromTemplate("blood_splatter", position, direction, intensity);

    // Spawn some smaller droplets
    for (int i = 0; i < 3; i++) {
        glm::vec3 offset = glm::vec3(
            (rand() / (float)RAND_MAX - 0.5f) * 0.5f,
            0.0f,
            (rand() / (float)RAND_MAX - 0.5f) * 0.5f
        );
        m_manager->SpawnFromTemplate("blood_droplet", position + offset, direction, intensity * 0.3f);
    }
}

void DecalSpawner::SpawnWaterPuddle(const glm::vec3& position, float size) {
    m_manager->SpawnFromTemplate("water_puddle", position, glm::vec3(0, 1, 0), size);
}

void DecalSpawner::SpawnMossGrowth(const glm::vec3& position, float size) {
    m_manager->SpawnFromTemplate("moss_growth", position, glm::vec3(0, 1, 0), size);
}

void DecalSpawner::SpawnCracks(const glm::vec3& position, float size) {
    m_manager->SpawnFromTemplate("ground_cracks", position, glm::vec3(0, 1, 0), size);
}

void DecalSpawner::SpawnCustom(const std::string& templateName,
                                const glm::vec3& position,
                                const glm::vec3& normal,
                                float scale) {
    m_manager->SpawnFromTemplate(templateName, position, normal, scale);
}

} // namespace Cortex::Graphics
