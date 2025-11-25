#include "CommandQueue.h"
#include "Utils/MeshGenerator.h"
#include "Scene/Components.h"
#include <spdlog/spdlog.h>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/component_wise.hpp>
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

    // Fetch or create cached mesh for this primitive so multiple objects share GPU buffers
    std::shared_ptr<Scene::MeshData> mesh;
    auto cached = m_meshCache.find(cmd->entityType);
    if (cached != m_meshCache.end()) {
        mesh = cached->second;
    }

    if (!mesh || !mesh->gpuBuffers || !mesh->gpuBuffers->vertexBuffer || !mesh->gpuBuffers->indexBuffer) {
        switch (cmd->entityType) {
            case AddEntityCommand::EntityType::Cube:
                mesh = Utils::MeshGenerator::CreateCube();
                break;
            case AddEntityCommand::EntityType::Sphere:
                mesh = Utils::MeshGenerator::CreateSphere(0.5f, 32);
                break;
            case AddEntityCommand::EntityType::Plane:
                mesh = Utils::MeshGenerator::CreatePlane(2.0f, 2.0f);
                break;
            case AddEntityCommand::EntityType::Cylinder:
                mesh = Utils::MeshGenerator::CreateCylinder(0.5f, 1.0f, 32);
                break;
            case AddEntityCommand::EntityType::Pyramid:
                mesh = Utils::MeshGenerator::CreatePyramid(1.0f, 1.0f);
                break;
            case AddEntityCommand::EntityType::Cone:
                mesh = Utils::MeshGenerator::CreateCone(0.5f, 1.0f, 32);
                break;
            case AddEntityCommand::EntityType::Torus:
                mesh = Utils::MeshGenerator::CreateTorus(0.5f, 0.2f, 32, 16);
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

        m_meshCache[cmd->entityType] = mesh;
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
    glm::vec3 placementBias = NextPlacementOffset(m_spawnIndex++, spacing);

    if (shouldAutoPlace) {
        // Place new entities around a ring in front of the camera by default
        desiredPos = glm::vec3(0.0f, 1.0f, -3.0f) + placementBias;
    } else {
        // Lightly jitter user positions to avoid perfect overlap when reusing same coords
        desiredPos += placementBias * 0.15f;
    }

    // keep entities off the floor plane to reduce z-fighting on y=0
    desiredPos.y = std::max(desiredPos.y, 0.5f);
    transform.position = FindNonOverlappingPosition(registry, desiredPos, spawnRadius);
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

} // namespace Cortex::LLM
