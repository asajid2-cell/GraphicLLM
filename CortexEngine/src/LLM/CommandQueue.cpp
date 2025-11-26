#include "CommandQueue.h"
#include "Graphics/Renderer.h"
#include "Utils/MeshGenerator.h"
#include "Scene/Components.h"
#include "CompoundLibrary.h"
#include <spdlog/spdlog.h>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/component_wise.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace Cortex::LLM {

namespace {
constexpr float kWorldExtent = 50.0f;
constexpr float kMinWorldY = -2.0f;

float SaturateScalar(float value) {
    if (!std::isfinite(value)) {
        return 0.0f;
    }
    return std::clamp(value, 0.0f, 1.0f);
}

glm::vec4 SanitizeColor(const glm::vec4& color) {
    glm::vec4 result = color;
    for (int i = 0; i < 4; ++i) {
        if (!std::isfinite(result[i])) {
            result[i] = 0.0f;
        }
        result[i] = std::clamp(result[i], 0.0f, 1.0f);
    }
    return result;
}

glm::vec3 ClampToWorld(const glm::vec3& v) {
    glm::vec3 out = v;
    out.x = std::clamp(out.x, -kWorldExtent, kWorldExtent);
    out.y = std::clamp(out.y, kMinWorldY, kWorldExtent);
    out.z = std::clamp(out.z, -kWorldExtent, kWorldExtent);
    return out;
}

glm::vec3 NextPlacementOffset(uint32_t index, float spacing) {
    // Golden-angle spiral to spread new spawns
    const float golden = 2.39996323f;
    float radius = spacing * (1.0f + 0.1f * static_cast<float>(index));
    float angle = golden * static_cast<float>(index);
    return glm::vec3(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);
}

// Simple grid search to avoid spawning on top of existing entities
glm::vec3 FindNonOverlappingPosition(Scene::ECS_Registry* registry, const glm::vec3& desired, float radius) {
    auto view = registry->View<Scene::TransformComponent>();
    const float baseSpacing = std::max(1.5f, radius * 2.2f);
    const float minDist2 = baseSpacing * baseSpacing;

    auto collides = [&](const glm::vec3& candidate) {
        for (auto entity : view) {
            const auto& t = view.get<Scene::TransformComponent>(entity);
            if (glm::distance2(candidate, t.position) < minDist2) {
                return true;
            }
        }
        return false;
    };

    glm::vec3 clampedDesired = ClampToWorld(desired);

    if (!collides(clampedDesired)) {
        return clampedDesired;
    }

    // Try a small spiral around the desired spot
    for (int ring = 1; ring <= 6; ++ring) {
        for (int dx = -ring; dx <= ring; ++dx) {
            for (int dz = -ring; dz <= ring; ++dz) {
                if (std::abs(dx) != ring && std::abs(dz) != ring) continue; // only outer ring
                glm::vec3 candidate = clampedDesired + glm::vec3(dx * baseSpacing, 0.0f, dz * baseSpacing);
                candidate = ClampToWorld(candidate);
                if (!collides(candidate)) {
                    return candidate;
                }
            }
        }
    }

    // Fallback: return clamped desired even if overlapping
    return clampedDesired;
}

glm::vec3 SanitizeVec3(const glm::vec3& v, float minAbs = 0.0f, bool clampToWorldBounds = true) {
    glm::vec3 out = v;
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(out[i])) {
            out[i] = 0.0f;
        }
        if (minAbs > 0.0f) {
            float sign = out[i] >= 0.0f ? 1.0f : -1.0f;
            out[i] = std::max(std::abs(out[i]), minAbs) * sign;
        }
        // Prevent absurdly large magnitudes
        out[i] = std::clamp(out[i], -kWorldExtent, kWorldExtent);
    }
    return clampToWorldBounds ? ClampToWorld(out) : out;
}
} // namespace

void CommandQueue::Push(std::shared_ptr<SceneCommand> command) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_commands.push(command);
    spdlog::debug("Command queued: {}", command->ToString());
}

void CommandQueue::PushBatch(const std::vector<std::shared_ptr<SceneCommand>>& commands) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& cmd : commands) {
        m_commands.push(cmd);
        spdlog::debug("Command queued: {}", cmd->ToString());
    }
}

bool CommandQueue::HasPending() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_commands.empty();
}

size_t CommandQueue::GetPendingCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_commands.size();
}

void CommandQueue::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    while (!m_commands.empty()) {
        m_commands.pop();
    }
    spdlog::info("Command queue cleared");
}

std::vector<CommandStatus> CommandQueue::ConsumeStatus() {
    std::vector<CommandStatus> out;
    std::lock_guard<std::mutex> lock(m_statusMutex);
    while (!m_status.empty()) {
        out.push_back(std::move(m_status.front()));
        m_status.pop();
    }
    return out;
}

std::optional<std::string> CommandQueue::GetLastSpawnedName(Scene::ECS_Registry* registry) const {
    return m_lookup.GetLastSpawnedName(registry);
}

void CommandQueue::RefreshLookup(Scene::ECS_Registry* registry) {
    m_lookup.Rebuild(registry);
}

std::string CommandQueue::BuildSceneSummary(Scene::ECS_Registry* registry, size_t maxChars) const {
    return m_lookup.BuildSummary(registry, maxChars);
}

void CommandQueue::PushStatus(bool success, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_statusMutex);
    m_status.push(CommandStatus{success, message});
}

void CommandQueue::ExecuteAll(Scene::ECS_Registry* registry, Graphics::Renderer* renderer) {
    // Move all commands to a local queue to minimize lock time
    std::queue<std::shared_ptr<SceneCommand>> localQueue;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::swap(localQueue, m_commands);
    }

    // Execute commands
    while (!localQueue.empty()) {
        auto cmd = localQueue.front();
        localQueue.pop();

        spdlog::debug("Executing: {}", cmd->ToString());
        ExecuteCommand(cmd.get(), registry, renderer);
    }
}

void CommandQueue::ExecuteCommand(SceneCommand* command, Scene::ECS_Registry* registry, Graphics::Renderer* renderer) {
    switch (command->type) {
        case CommandType::AddEntity:
            ExecuteAddEntity(static_cast<AddEntityCommand*>(command), registry, renderer);
            break;
        case CommandType::RemoveEntity:
            ExecuteRemoveEntity(static_cast<RemoveEntityCommand*>(command), registry);
            break;
        case CommandType::ModifyTransform:
            ExecuteModifyTransform(static_cast<ModifyTransformCommand*>(command), registry);
            break;
        case CommandType::ModifyMaterial:
            ExecuteModifyMaterial(static_cast<ModifyMaterialCommand*>(command), registry);
            break;
        case CommandType::ModifyCamera:
            ExecuteModifyCamera(static_cast<ModifyCameraCommand*>(command), registry);
            break;
        case CommandType::AddPattern:
            ExecuteAddPattern(static_cast<AddPatternCommand*>(command), registry, renderer);
            break;
        case CommandType::AddCompound:
            ExecuteAddCompound(static_cast<AddCompoundCommand*>(command), registry, renderer);
            break;
        case CommandType::ModifyGroup:
            ExecuteModifyGroup(static_cast<ModifyGroupCommand*>(command), registry);
            break;
        case CommandType::AddLight:
            ExecuteAddLight(static_cast<AddLightCommand*>(command), registry);
            break;
        case CommandType::ModifyLight:
            ExecuteModifyLight(static_cast<ModifyLightCommand*>(command), registry);
            break;
        case CommandType::ModifyRenderer:
            ExecuteModifyRenderer(static_cast<ModifyRendererCommand*>(command), renderer);
            break;
        default:
            spdlog::warn("Unknown command type");
            PushStatus(false, "unknown command type");
            break;
    }
}

void CommandQueue::ExecuteAddEntity(AddEntityCommand* cmd, Scene::ECS_Registry* registry, Graphics::Renderer* renderer) {
    if (!registry || !renderer || !cmd) {
        PushStatus(false, "AddEntity skipped: missing registry or renderer");
        return;
    }

    // Normalize detail values for caching (shapes that don't use segments ignore them)
    uint32_t segPrimary = cmd->segmentsPrimary;
    uint32_t segSecondary = cmd->segmentsSecondary;
    switch (cmd->entityType) {
        case AddEntityCommand::EntityType::Cube:
        case AddEntityCommand::EntityType::Plane:
        case AddEntityCommand::EntityType::Pyramid:
            segPrimary = 0;
            segSecondary = 0;
            break;
        default:
            segPrimary = std::clamp<uint32_t>(segPrimary, 8u, 96u);
            segSecondary = std::clamp<uint32_t>(segSecondary, 4u, 64u);
            break;
    }

    MeshKey key{cmd->entityType, segPrimary, segSecondary};

    // Fetch or create cached mesh for this primitive so multiple objects share GPU buffers
    std::shared_ptr<Scene::MeshData> mesh;
    auto cached = m_meshCache.find(key);
    if (cached != m_meshCache.end()) {
        mesh = cached->second;
    }

    if (!mesh || !mesh->gpuBuffers || !mesh->gpuBuffers->vertexBuffer || !mesh->gpuBuffers->indexBuffer) {
        switch (cmd->entityType) {
            case AddEntityCommand::EntityType::Cube:
                mesh = Utils::MeshGenerator::CreateCube();
                break;
            case AddEntityCommand::EntityType::Sphere:
                mesh = Utils::MeshGenerator::CreateSphere(0.5f, segPrimary);
                break;
            case AddEntityCommand::EntityType::Plane:
                mesh = Utils::MeshGenerator::CreatePlane(2.0f, 2.0f);
                break;
            case AddEntityCommand::EntityType::Cylinder:
                mesh = Utils::MeshGenerator::CreateCylinder(0.5f, 1.0f, segPrimary);
                break;
            case AddEntityCommand::EntityType::Pyramid:
                mesh = Utils::MeshGenerator::CreatePyramid(1.0f, 1.0f);
                break;
            case AddEntityCommand::EntityType::Cone:
                mesh = Utils::MeshGenerator::CreateCone(0.5f, 1.0f, segPrimary);
                break;
            case AddEntityCommand::EntityType::Torus:
                mesh = Utils::MeshGenerator::CreateTorus(0.5f, 0.2f, segPrimary, segSecondary);
                break;
        }

        if (!mesh) {
            spdlog::error("Failed to generate mesh for entity");
            PushStatus(false, "Failed to generate mesh for new entity");
            return;
        }

        auto uploadResult = renderer->UploadMesh(mesh);
        if (uploadResult.IsErr()) {
            spdlog::error("Failed to upload mesh: {}", uploadResult.Error());
            PushStatus(false, "Failed to upload mesh for new entity");
            return;
        }

        m_meshCache[key] = mesh;
    }

    // Create entity
    entt::entity entity = registry->CreateEntity();

    // Add tag
    std::string name = cmd->name.empty() ? "Entity" + std::to_string((uint32_t)entity) : cmd->name;
    registry->AddComponent<Scene::TagComponent>(entity, name);

    // Add transform
    auto& transform = registry->AddComponent<Scene::TransformComponent>(entity);
    const bool shouldAutoPlace = cmd->autoPlace || glm::all(glm::epsilonEqual(cmd->position, glm::vec3(0.0f), 1e-4f));
    glm::vec3 safeScale = SanitizeVec3(cmd->scale, 0.05f, false);
    safeScale = glm::clamp(safeScale, glm::vec3(-100.0f), glm::vec3(100.0f));

    const float spawnRadius = glm::compMax(glm::abs(safeScale));
    glm::vec3 desiredPos = SanitizeVec3(cmd->position);
    const float spacing = std::max(1.5f, spawnRadius * 2.2f);

    glm::vec3 placementBias(0.0f);
    if (shouldAutoPlace || cmd->allowPlacementJitter) {
        placementBias = NextPlacementOffset(m_spawnIndex++, spacing);
    }

    if (shouldAutoPlace) {
        // Place new entities around a ring in front of the camera by default
        desiredPos = glm::vec3(0.0f, 1.0f, -3.0f) + placementBias;
    } else if (cmd->allowPlacementJitter) {
        // Lightly jitter user positions to avoid perfect overlap when reusing same coords
        desiredPos += placementBias * 0.15f;
    }

    // keep entities off the floor plane to reduce z-fighting on y=0
    desiredPos.y = std::max(desiredPos.y, 0.5f);
    if (cmd->disableCollisionAvoidance) {
        transform.position = ClampToWorld(desiredPos);
    } else {
        transform.position = FindNonOverlappingPosition(registry, desiredPos, spawnRadius);
    }
    transform.scale = safeScale;

    // Add renderable
    auto& renderable = registry->AddComponent<Scene::RenderableComponent>(entity);
    renderable.mesh = mesh;
    renderable.albedoColor = SanitizeColor(cmd->color);
    renderable.metallic = SaturateScalar(cmd->metallic);
    renderable.roughness = SaturateScalar(cmd->roughness);
    renderable.ao = SaturateScalar(cmd->ao);
    renderable.visible = true;
    renderable.textures.albedo = renderer->GetPlaceholderTexture();
    renderable.textures.normal = renderer->GetPlaceholderNormal();
    renderable.textures.metallic = renderer->GetPlaceholderMetallic();
    renderable.textures.roughness = renderer->GetPlaceholderRoughness();

    m_lookup.TrackEntity(entity, name, cmd->entityType, renderable.albedoColor);

    spdlog::info("Created entity '{}' at ({}, {}, {})",
                 name, transform.position.x, transform.position.y, transform.position.z);
    {
        std::ostringstream ss;
        ss << "spawned " << name << " at (" << std::fixed << std::setprecision(2)
           << transform.position.x << "," << transform.position.y << "," << transform.position.z << ")";
        PushStatus(true, ss.str());
    }
}

void CommandQueue::ExecuteAddLight(AddLightCommand* cmd, Scene::ECS_Registry* registry) {
    if (!registry || !cmd) {
        PushStatus(false, "AddLight skipped: missing registry");
        return;
    }

    entt::entity e = registry->CreateEntity();

    // Tag for lookup/debugging
    std::string name = cmd->name.empty() ? "Light_" + std::to_string(m_spawnIndex++) : cmd->name;
    registry->AddComponent<Scene::TagComponent>(e, name);

    auto& transform = registry->AddComponent<Scene::TransformComponent>(e);
    transform.position = SanitizeVec3(cmd->position, 0.0f, false);

    // Build rotation from direction for spot/directional lights
    glm::vec3 forward = cmd->direction;
    if (!std::isfinite(forward.x) || !std::isfinite(forward.y) || !std::isfinite(forward.z) ||
        glm::length2(forward) < 1e-4f) {
        forward = glm::vec3(0.0f, -1.0f, 0.0f);
    }
    forward = glm::normalize(forward);
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(up, forward)) > 0.99f) {
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    transform.rotation = glm::quatLookAt(forward, up);

    auto& light = registry->AddComponent<Scene::LightComponent>(e);
    switch (cmd->lightType) {
        case AddLightCommand::LightType::Directional:
            light.type = Scene::LightType::Directional;
            break;
        case AddLightCommand::LightType::Spot:
            light.type = Scene::LightType::Spot;
            break;
        case AddLightCommand::LightType::Point:
        default:
            light.type = Scene::LightType::Point;
            break;
    }

    light.color = glm::max(cmd->color, glm::vec3(0.0f));
    light.intensity = std::max(cmd->intensity, 0.0f);
    light.range = std::max(cmd->range, 0.0f);
    light.innerConeDegrees = cmd->innerConeDegrees;
    light.outerConeDegrees = cmd->outerConeDegrees;
    light.castsShadows = cmd->castsShadows;

    std::ostringstream ss;
    ss << "spawned light " << name << " at (" << std::fixed << std::setprecision(2)
       << transform.position.x << "," << transform.position.y << "," << transform.position.z << ")";
    PushStatus(true, ss.str());
}

void CommandQueue::ExecuteRemoveEntity(RemoveEntityCommand* cmd, Scene::ECS_Registry* registry) {
    std::string hint;
    entt::entity target = m_lookup.ResolveTarget(cmd->targetName, registry, hint);
    if (target == entt::null) {
        spdlog::warn("Entity '{}' not found ({})", cmd->targetName, hint);
        PushStatus(false, "remove failed: " + (hint.empty() ? "target not found" : hint));
        return;
    }

    std::string tagName = cmd->targetName;
    if (registry->HasComponent<Scene::TagComponent>(target)) {
        tagName = registry->GetComponent<Scene::TagComponent>(target).tag;
    }

    registry->DestroyEntity(target);
    m_lookup.ForgetEntity(target);
    spdlog::info("Removed entity '{}'", tagName);
    PushStatus(true, "removed " + tagName);
}

void CommandQueue::ExecuteModifyTransform(ModifyTransformCommand* cmd, Scene::ECS_Registry* registry) {
    std::string hint;
    entt::entity target = m_lookup.ResolveTarget(cmd->targetName, registry, hint);
    if (target == entt::null) {
        spdlog::warn("Transform target '{}' not found ({})", cmd->targetName, hint);
        PushStatus(false, "move/scale failed: " + (hint.empty() ? "target not found" : hint));
        return;
    }

    if (!registry->HasComponent<Scene::TransformComponent>(target)) {
        PushStatus(false, "target lacks transform component");
        spdlog::warn("Entity '{}' has no transform", cmd->targetName);
        return;
    }

    auto& transform = registry->GetComponent<Scene::TransformComponent>(target);
    std::string tagName = cmd->targetName;
    if (registry->HasComponent<Scene::TagComponent>(target)) {
        tagName = registry->GetComponent<Scene::TagComponent>(target).tag;
    }

    std::ostringstream summary;
    summary << "updated " << tagName << ": ";
    bool touched = false;

    if (cmd->setPosition) {
        transform.position = SanitizeVec3(cmd->position);
        spdlog::info("Moved '{}' to ({}, {}, {})",
                   tagName, transform.position.x, transform.position.y, transform.position.z);
        summary << "pos(" << std::fixed << std::setprecision(2)
                << transform.position.x << "," << transform.position.y << "," << transform.position.z << ") ";
        touched = true;
    }
    if (cmd->setRotation) {
        glm::vec3 clampedEuler = glm::clamp(cmd->rotation, glm::vec3(-720.0f), glm::vec3(720.0f));
        glm::vec3 euler = glm::radians(clampedEuler);
        if (!std::isfinite(euler.x) || !std::isfinite(euler.y) || !std::isfinite(euler.z)) {
            euler = glm::vec3(0.0f);
        }
        transform.rotation = glm::normalize(glm::quat(euler));
        spdlog::info("Rotated '{}' to euler ({}, {}, {})",
                   tagName, clampedEuler.x, clampedEuler.y, clampedEuler.z);
        summary << "rot(" << clampedEuler.x << "," << clampedEuler.y << "," << clampedEuler.z << ") ";
        touched = true;
    }
    if (cmd->setScale) {
        glm::vec3 clampedScale = SanitizeVec3(cmd->scale, 0.05f, false);
        clampedScale = glm::clamp(clampedScale, glm::vec3(-100.0f), glm::vec3(100.0f));
        transform.scale = clampedScale;
        spdlog::info("Scaled '{}' to ({}, {}, {})",
                   tagName, transform.scale.x, transform.scale.y, transform.scale.z);
        summary << "scale(" << transform.scale.x << "," << transform.scale.y << "," << transform.scale.z << ") ";
        touched = true;
    }

    if (touched) {
        PushStatus(true, summary.str());
    }
}

void CommandQueue::ExecuteModifyMaterial(ModifyMaterialCommand* cmd, Scene::ECS_Registry* registry) {
    std::string hint;
    entt::entity target = m_lookup.ResolveTarget(cmd->targetName, registry, hint);
    if (target == entt::null) {
        spdlog::warn("Material target '{}' not found ({})", cmd->targetName, hint);
        PushStatus(false, "material failed: " + (hint.empty() ? "target not found" : hint));
        return;
    }

    if (!registry->HasComponent<Scene::RenderableComponent>(target)) {
        PushStatus(false, "target lacks renderable component");
        spdlog::warn("Entity '{}' has no renderable component", cmd->targetName);
        return;
    }

    auto& renderable = registry->GetComponent<Scene::RenderableComponent>(target);
    std::string tagName = cmd->targetName;
    if (registry->HasComponent<Scene::TagComponent>(target)) {
        tagName = registry->GetComponent<Scene::TagComponent>(target).tag;
    }

    std::ostringstream summary;
    summary << "material " << tagName << ": ";
    bool touched = false;

    if (cmd->setColor) {
        renderable.albedoColor = SanitizeColor(cmd->color);
        spdlog::info("Changed '{}' color to ({}, {}, {})",
                   tagName, renderable.albedoColor.r, renderable.albedoColor.g, renderable.albedoColor.b);
        summary << "color ";
        touched = true;
    }
    if (cmd->setMetallic) {
        renderable.metallic = SaturateScalar(cmd->metallic);
        summary << "metallic ";
        touched = true;
    }
    if (cmd->setRoughness) {
        renderable.roughness = SaturateScalar(cmd->roughness);
        summary << "roughness ";
        touched = true;
    }

    if (touched) {
        PushStatus(true, summary.str());
    }
}

void CommandQueue::ExecuteModifyLight(ModifyLightCommand* cmd, Scene::ECS_Registry* registry) {
    if (!registry || cmd->targetName.empty()) {
        PushStatus(false, "modify_light failed: missing registry or target");
        return;
    }

    // Resolve by tag name (lights are not tracked in SceneLookup yet)
    auto view = registry->View<Scene::TagComponent, Scene::LightComponent, Scene::TransformComponent>();
    entt::entity target = entt::null;
    for (auto entity : view) {
        const auto& tag = view.get<Scene::TagComponent>(entity);
        if (tag.tag == cmd->targetName) {
            target = entity;
            break;
        }
    }

    if (target == entt::null) {
        PushStatus(false, "modify_light failed: target '" + cmd->targetName + "' not found");
        return;
    }

    auto& light = view.get<Scene::LightComponent>(target);
    auto& transform = view.get<Scene::TransformComponent>(target);

    std::ostringstream summary;
    summary << "light " << cmd->targetName << ": ";
    bool touched = false;

    if (cmd->setPosition) {
        transform.position = SanitizeVec3(cmd->position, 0.0f, true);
        summary << "pos ";
        touched = true;
    }
    if (cmd->setDirection) {
        glm::vec3 forward = cmd->direction;
        if (!std::isfinite(forward.x) || !std::isfinite(forward.y) || !std::isfinite(forward.z) ||
            glm::length2(forward) < 1e-4f) {
            forward = glm::vec3(0.0f, -1.0f, 0.0f);
        }
        forward = glm::normalize(forward);
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(up, forward)) > 0.99f) {
            up = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        transform.rotation = glm::quatLookAt(forward, up);
        summary << "dir ";
        touched = true;
    }
    if (cmd->setColor) {
        light.color = glm::max(cmd->color, glm::vec3(0.0f));
        summary << "color ";
        touched = true;
    }
    if (cmd->setIntensity) {
        light.intensity = std::max(cmd->intensity, 0.0f);
        summary << "intensity ";
        touched = true;
    }
    if (cmd->setRange) {
        light.range = std::max(cmd->range, 0.0f);
        summary << "range ";
        touched = true;
    }
    if (cmd->setInnerCone) {
        light.innerConeDegrees = cmd->innerConeDegrees;
        summary << "inner_cone ";
        touched = true;
    }
    if (cmd->setOuterCone) {
        light.outerConeDegrees = cmd->outerConeDegrees;
        summary << "outer_cone ";
        touched = true;
    }
    if (cmd->setType) {
        switch (cmd->lightType) {
            case AddLightCommand::LightType::Directional:
                light.type = Scene::LightType::Directional; break;
            case AddLightCommand::LightType::Spot:
                light.type = Scene::LightType::Spot; break;
            case AddLightCommand::LightType::Point:
            default:
                light.type = Scene::LightType::Point; break;
        }
        summary << "type ";
        touched = true;
    }
    if (cmd->setCastsShadows) {
        light.castsShadows = cmd->castsShadows;
        summary << "casts_shadows ";
        touched = true;
    }

    if (touched) {
        PushStatus(true, summary.str());
    } else {
        PushStatus(false, "modify_light had no effect (no fields set)");
    }
}

void CommandQueue::ExecuteModifyRenderer(ModifyRendererCommand* cmd, Graphics::Renderer* renderer) {
    if (!renderer || !cmd) {
        PushStatus(false, "modify_renderer failed: renderer not available");
        return;
    }

    std::ostringstream summary;
    summary << "renderer: ";
    bool touched = false;

    if (cmd->setExposure) {
        renderer->SetExposure(cmd->exposure);
        summary << "exposure=" << cmd->exposure << " ";
        touched = true;
    }
    if (cmd->setShadowsEnabled) {
        renderer->SetShadowsEnabled(cmd->shadowsEnabled);
        summary << "shadows=" << (cmd->shadowsEnabled ? "on" : "off") << " ";
        touched = true;
    }
    if (cmd->setDebugMode) {
        renderer->SetDebugViewMode(cmd->debugMode);
        summary << "debug_mode=" << cmd->debugMode << " ";
        touched = true;
    }
    if (cmd->setShadowBias) {
        renderer->SetShadowBias(cmd->shadowBias);
        summary << "bias=" << cmd->shadowBias << " ";
        touched = true;
    }
    if (cmd->setShadowPCFRadius) {
        renderer->SetShadowPCFRadius(cmd->shadowPCFRadius);
        summary << "pcf=" << cmd->shadowPCFRadius << " ";
        touched = true;
    }
    if (cmd->setCascadeSplitLambda) {
        renderer->SetCascadeSplitLambda(cmd->cascadeSplitLambda);
        summary << "lambda=" << cmd->cascadeSplitLambda << " ";
        touched = true;
    }

    if (touched) {
        PushStatus(true, summary.str());
    } else {
        PushStatus(false, "modify_renderer had no effect (no fields set)");
    }
}

void CommandQueue::ExecuteModifyCamera(ModifyCameraCommand* cmd, Scene::ECS_Registry* registry) {
    // Find active camera
    auto view = registry->View<Scene::CameraComponent, Scene::TransformComponent>();

    for (auto entity : view) {
        auto& camera = view.get<Scene::CameraComponent>(entity);
        if (camera.isActive) {
            auto& transform = view.get<Scene::TransformComponent>(entity);

            std::ostringstream summary;
            bool touched = false;
            if (cmd->setPosition) {
                transform.position = SanitizeVec3(cmd->position);
                spdlog::info("Moved camera to ({}, {}, {})",
                           transform.position.x, transform.position.y, transform.position.z);
                summary << "pos(" << std::fixed << std::setprecision(2)
                        << transform.position.x << "," << transform.position.y << "," << transform.position.z << ") ";
                touched = true;
            }
            if (cmd->setFOV) {
                camera.fov = std::clamp(cmd->fov, 10.0f, 140.0f);
                spdlog::info("Changed camera FOV to {}", camera.fov);
                summary << "fov " << camera.fov;
                touched = true;
            }
            if (touched) {
                PushStatus(true, "camera: " + summary.str());
            }
            return;
        }
    }

    spdlog::warn("No active camera found");
    PushStatus(false, "camera change failed: no active camera");
}

void CommandQueue::ExecuteAddCompound(AddCompoundCommand* cmd, Scene::ECS_Registry* registry, Graphics::Renderer* renderer) {
    if (!cmd || !registry || !renderer) {
        PushStatus(false, "add_compound failed: missing registry or renderer");
        return;
    }

    const CompoundTemplate* templ = CompoundLibrary::FindTemplate(cmd->templateName);
    if (!templ) {
        PushStatus(false, "add_compound failed: unknown template '" + cmd->templateName + "'");
        return;
    }

    // Derive a stable instance name/group prefix
    std::string instanceName = cmd->instanceName;
    if (instanceName.empty()) {
        instanceName = templ->defaultGroupPrefix + "_" + std::to_string(m_spawnIndex++);
    }

    glm::vec3 basePos = SanitizeVec3(cmd->position);
    glm::vec3 baseScale = cmd->scale;
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(baseScale[i]) || std::abs(baseScale[i]) < 0.01f) {
            baseScale[i] = (baseScale[i] >= 0.0f ? 1.0f : -1.0f);
        }
    }

    int partIndex = 0;
    for (const auto& part : templ->parts) {
        AddEntityCommand partCmd;
        partCmd.entityType = part.type;
        partCmd.autoPlace = false;
        partCmd.allowPlacementJitter = false;
        partCmd.disableCollisionAvoidance = true;

        partCmd.segmentsPrimary = part.segmentsPrimary ? part.segmentsPrimary : partCmd.segmentsPrimary;
        partCmd.segmentsSecondary = part.segmentsSecondary ? part.segmentsSecondary : partCmd.segmentsSecondary;

        glm::vec3 scaledLocal = part.localPosition * baseScale;
        partCmd.position = basePos + scaledLocal;
        partCmd.scale = baseScale * part.localScale;

        partCmd.color = part.color;

        std::string partName = part.partName.empty() ? ("Part" + std::to_string(partIndex)) : part.partName;
        partCmd.name = instanceName + "." + partName;

        ExecuteAddEntity(&partCmd, registry, renderer);
        ++partIndex;
    }

    std::ostringstream ss;
    ss << "spawned compound " << templ->name << " as " << instanceName
       << " (" << templ->parts.size() << " parts)";
    PushStatus(true, ss.str());
}

void CommandQueue::ExecuteAddPattern(AddPatternCommand* cmd, Scene::ECS_Registry* registry, Graphics::Renderer* renderer) {
    if (!cmd || !registry || !renderer) {
        PushStatus(false, "add_pattern failed: missing registry or renderer");
        return;
    }

    constexpr int kMaxPatternCountPerCommand = 256;

    int requested = std::max(1, cmd->count);
    int count = std::min(requested, kMaxPatternCountPerCommand);
    if (requested != count) {
        std::ostringstream ss;
        ss << "add_pattern: clamped count from " << requested << " to " << count;
        PushStatus(true, ss.str());
    }

    glm::vec3 regionMin = cmd->regionMin;
    glm::vec3 regionMax = cmd->regionMax;
    if (cmd->hasRegionBox) {
        regionMin = glm::min(regionMin, regionMax);
        regionMax = glm::max(regionMin, regionMax);
    } else {
        regionMax = regionMin;
    }
    glm::vec3 center = 0.5f * (regionMin + regionMax);

    const CompoundTemplate* compoundTempl = nullptr;
    if (!cmd->element.empty()) {
        compoundTempl = CompoundLibrary::FindTemplate(cmd->element);
    }

    std::string groupName = cmd->groupName;
    if (groupName.empty()) {
        if (compoundTempl) {
            groupName = compoundTempl->defaultGroupPrefix;
        } else if (!cmd->element.empty()) {
            groupName = cmd->element;
        } else {
            groupName = "Pattern";
        }
    }

    std::string namePrefix = cmd->namePrefix;
    if (namePrefix.empty()) {
        if (compoundTempl) {
            namePrefix = compoundTempl->defaultGroupPrefix;
        } else if (!cmd->element.empty()) {
            namePrefix = cmd->element;
        } else {
            namePrefix = "Element";
        }
    }

    auto elementTypeFromString = [](const std::string& elem) -> AddEntityCommand::EntityType {
        std::string lowered = elem;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lowered == "cube" || lowered == "box" || lowered == "rounded_box") {
            return AddEntityCommand::EntityType::Cube;
        }
        if (lowered == "sphere" || lowered == "ball" ||
            lowered == "lowpoly_sphere" || lowered == "highpoly_sphere") {
            return AddEntityCommand::EntityType::Sphere;
        }
        if (lowered == "plane" || lowered == "thin_plane" ||
            lowered == "leaf" || lowered == "wing" ||
            lowered == "grass_blade" || lowered == "grass blade" || lowered == "grass") {
            return AddEntityCommand::EntityType::Plane;
        }
        if (lowered == "cylinder" || lowered == "capsule" || lowered == "pillar") {
            return AddEntityCommand::EntityType::Cylinder;
        }
        if (lowered == "pyramid" || lowered == "wedge") {
            return AddEntityCommand::EntityType::Pyramid;
        }
        if (lowered == "cone") {
            return AddEntityCommand::EntityType::Cone;
        }
        // Treat "arch" as torus segment
        return AddEntityCommand::EntityType::Torus;
    };

    auto safeSpacing = [&](float v, float fallback) {
        if (!std::isfinite(v) || std::abs(v) < 0.1f) return fallback;
        return std::abs(v);
    };

    float stepX = cmd->hasSpacing ? safeSpacing(cmd->spacing.x, 1.5f) : 1.5f;
    float stepZ = cmd->hasSpacing ? safeSpacing(cmd->spacing.z, 1.5f) : 1.5f;

    auto sampleHeight = [](const glm::vec3& /*pos*/) {
        // Hook for future terrain support; currently we place patterns slightly above ground plane.
        return 0.5f;
    };

    for (int i = 0; i < count; ++i) {
        glm::vec3 localOffset(0.0f);
        switch (cmd->pattern) {
            case AddPatternCommand::PatternType::Row: {
                float t = static_cast<float>(i) - static_cast<float>(count - 1) * 0.5f;
                localOffset.x = t * stepX;
                break;
            }
            case AddPatternCommand::PatternType::Grid: {
                int nx = static_cast<int>(std::round(std::sqrt(static_cast<float>(count))));
                nx = std::max(1, nx);
                int ix = i % nx;
                int iz = i / nx;
                float ox = static_cast<float>(ix) - static_cast<float>(nx - 1) * 0.5f;
                float oz = static_cast<float>(iz) - static_cast<float>((count + nx - 1) / nx - 1) * 0.5f;
                localOffset.x = ox * stepX;
                localOffset.z = oz * stepZ;
                break;
            }
            case AddPatternCommand::PatternType::Ring: {
                float radius;
                if (cmd->hasRegionBox) {
                    glm::vec2 ext(std::abs(regionMax.x - regionMin.x), std::abs(regionMax.z - regionMin.z));
                    radius = std::max(1.0f, 0.25f * (ext.x + ext.y));
                } else {
                    radius = std::max(2.0f, stepX * static_cast<float>(count) / (2.0f * glm::pi<float>()));
                }
                float angle = (static_cast<float>(i) / static_cast<float>(count)) * 2.0f * glm::pi<float>();
                localOffset.x = std::cos(angle) * radius;
                localOffset.z = std::sin(angle) * radius;
                break;
            }
            case AddPatternCommand::PatternType::Random: {
                // Deterministic pseudo-random scatter in region box
                glm::vec3 extents = regionMax - regionMin;
                if (!cmd->hasRegionBox) {
                    extents = glm::vec3(10.0f, 0.0f, 10.0f);
                    regionMin = center - 0.5f * extents;
                }
                auto hash = [](uint32_t x) {
                    x ^= x >> 17; x *= 0xed5ad4bbU;
                    x ^= x >> 11; x *= 0xac4c1b51U;
                    x ^= x >> 15; x *= 0x31848babU;
                    x ^= x >> 14;
                    return x;
                };
                uint32_t h = hash(static_cast<uint32_t>(i + 1));
                float rx = static_cast<float>(h & 0x3FF) / 1023.0f;
                float rz = static_cast<float>((h >> 10) & 0x3FF) / 1023.0f;
                localOffset.x = regionMin.x + rx * extents.x - center.x;
                localOffset.z = regionMin.z + rz * extents.z - center.z;
                break;
            }
        }

        glm::vec3 worldPos = center + localOffset;
        worldPos.y = sampleHeight(worldPos);
        worldPos = SanitizeVec3(worldPos);

        if (compoundTempl) {
            AddCompoundCommand sub;
            sub.templateName = compoundTempl->name;
            sub.instanceName = groupName + "_" + std::to_string(i);
            sub.position = worldPos;
            sub.scale = cmd->hasElementScale ? cmd->elementScale : glm::vec3(1.0f);
            if (!std::isfinite(sub.scale.x) || std::abs(sub.scale.x) < 0.01f) sub.scale.x = 1.0f;
            if (!std::isfinite(sub.scale.y) || std::abs(sub.scale.y) < 0.01f) sub.scale.y = 1.0f;
            if (!std::isfinite(sub.scale.z) || std::abs(sub.scale.z) < 0.01f) sub.scale.z = 1.0f;
            ExecuteAddCompound(&sub, registry, renderer);
        } else {
            AddEntityCommand elemCmd;
            elemCmd.entityType = elementTypeFromString(cmd->element);
            elemCmd.position = worldPos;
            elemCmd.autoPlace = false;
            elemCmd.allowPlacementJitter = false;
            elemCmd.disableCollisionAvoidance = true;

            elemCmd.name = groupName + "_" + std::to_string(i);

            // Optional explicit element scale from the pattern
            if (cmd->hasElementScale) {
                elemCmd.scale = cmd->elementScale;
            }

            // Grass fields: smaller, denser, with safe defaults when not explicitly overridden
            std::string lowered = cmd->element;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (!cmd->hasElementScale &&
                (lowered == "grass_blade" || lowered == "grass blade" || lowered == "grass")) {
                elemCmd.scale = glm::vec3(0.05f, 0.6f, 0.4f);
                elemCmd.color = glm::vec4(0.1f, 0.6f, 0.2f, 1.0f);
                elemCmd.segmentsPrimary = 8;
                elemCmd.segmentsSecondary = 4;
            }

            ExecuteAddEntity(&elemCmd, registry, renderer);
        }
    }

    std::ostringstream ss;
    ss << "add_pattern '" << groupName << "' (" << count << " elements, pattern=";
    switch (cmd->pattern) {
        case AddPatternCommand::PatternType::Row:   ss << "row"; break;
        case AddPatternCommand::PatternType::Grid:  ss << "grid"; break;
        case AddPatternCommand::PatternType::Ring:  ss << "ring"; break;
        case AddPatternCommand::PatternType::Random:ss << "random"; break;
    }
    ss << ")";
    PushStatus(true, ss.str());
}

void CommandQueue::ExecuteModifyGroup(ModifyGroupCommand* cmd, Scene::ECS_Registry* registry) {
    if (!cmd || !registry || cmd->groupName.empty()) {
        PushStatus(false, "modify_group failed: missing group name");
        return;
    }

    auto view = registry->View<Scene::TagComponent, Scene::TransformComponent>();
    int affected = 0;
    for (auto entity : view) {
        auto& tag = view.get<Scene::TagComponent>(entity);
        const std::string& name = tag.tag;
        bool matches = false;

        if (name == cmd->groupName) {
            matches = true;
        } else {
            if (name.rfind(cmd->groupName + ".", 0) == 0 ||
                name.rfind(cmd->groupName + "_", 0) == 0) {
                matches = true;
            }
        }

        if (!matches) continue;

        auto& transform = view.get<Scene::TransformComponent>(entity);
        if (cmd->hasPositionOffset) {
            transform.position = ClampToWorld(transform.position + cmd->positionOffset);
        }
        if (cmd->hasScaleMultiplier) {
            glm::vec3 s = transform.scale;
            s *= cmd->scaleMultiplier;
            transform.scale = SanitizeVec3(s, 0.01f, false);
        }
        ++affected;
    }

    if (affected > 0) {
        std::ostringstream ss;
        ss << "modify_group '" << cmd->groupName << "' updated " << affected << " entities";
        PushStatus(true, ss.str());
    } else {
        PushStatus(false, "modify_group: no entities matched group '" + cmd->groupName + "'");
    }
}

} // namespace Cortex::LLM
