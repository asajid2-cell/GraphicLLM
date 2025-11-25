#include "CommandQueue.h"
#include "Utils/MeshGenerator.h"
#include <spdlog/spdlog.h>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtx/norm.hpp>
#include <algorithm>

namespace Cortex::LLM {

namespace {
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

    if (!collides(desired)) {
        return desired;
    }

    // Try a small spiral around the desired spot
    for (int ring = 1; ring <= 6; ++ring) {
        for (int dx = -ring; dx <= ring; ++dx) {
            for (int dz = -ring; dz <= ring; ++dz) {
                if (std::abs(dx) != ring && std::abs(dz) != ring) continue; // only outer ring
                glm::vec3 candidate = desired + glm::vec3(dx * baseSpacing, 0.0f, dz * baseSpacing);
                if (!collides(candidate)) {
                    return candidate;
                }
            }
        }
    }

    // Fallback: return desired even if overlapping
    return desired;
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
            break;
    }
}

void CommandQueue::ExecuteAddEntity(AddEntityCommand* cmd, Scene::ECS_Registry* registry, Graphics::Renderer* renderer) {
    // Fetch or create cached mesh for this primitive so multiple objects share GPU buffers
    std::shared_ptr<Scene::MeshData> mesh;
    auto cached = m_meshCache.find(cmd->entityType);
    if (cached != m_meshCache.end()) {
        mesh = cached->second;
    }

    if (!mesh || !mesh->vertexBuffer || !mesh->indexBuffer) {
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
        }

        if (!mesh) {
            spdlog::error("Failed to generate mesh for entity");
            return;
        }

        auto uploadResult = renderer->UploadMesh(mesh);
        if (uploadResult.IsErr()) {
            spdlog::error("Failed to upload mesh: {}", uploadResult.Error());
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
    glm::vec3 desiredPos = cmd->position;
    if (shouldAutoPlace) {
        auto view = registry->View<Scene::RenderableComponent>();
        float offset = 1.5f * static_cast<float>(view.size());
        desiredPos = glm::vec3(offset, 1.0f, 0.0f);
    }
    // keep entities off the floor plane to reduce z-fighting on y=0
    desiredPos.y = std::max(desiredPos.y, 0.5f);
    glm::vec3 safeScale = glm::max(cmd->scale, glm::vec3(0.1f));
    transform.position = FindNonOverlappingPosition(registry, desiredPos, glm::compMax(glm::abs(safeScale)));
    transform.scale = safeScale;

    // Add renderable
    auto& renderable = registry->AddComponent<Scene::RenderableComponent>(entity);
    renderable.mesh = mesh;
    renderable.albedoColor = cmd->color;
    renderable.visible = true;
    renderable.texture = renderer->GetPlaceholderTexture();

    spdlog::info("Created entity '{}' at ({}, {}, {})",
                 name, transform.position.x, transform.position.y, transform.position.z);
}

void CommandQueue::ExecuteRemoveEntity(RemoveEntityCommand* cmd, Scene::ECS_Registry* registry) {
    // Find entity by tag
    auto view = registry->View<Scene::TagComponent>();

    for (auto entity : view) {
        auto& tag = view.get<Scene::TagComponent>(entity);
        if (tag.tag == cmd->targetName) {
            registry->DestroyEntity(entity);
            spdlog::info("Removed entity '{}'", cmd->targetName);
            return;
        }
    }

    spdlog::warn("Entity '{}' not found", cmd->targetName);
}

void CommandQueue::ExecuteModifyTransform(ModifyTransformCommand* cmd, Scene::ECS_Registry* registry) {
    // Find entity by tag
    auto view = registry->View<Scene::TagComponent, Scene::TransformComponent>();

    for (auto entity : view) {
        auto& tag = view.get<Scene::TagComponent>(entity);
        if (tag.tag == cmd->targetName) {
            auto& transform = view.get<Scene::TransformComponent>(entity);

            if (cmd->setPosition) {
                transform.position = cmd->position;
                spdlog::info("Moved '{}' to ({}, {}, {})",
                           cmd->targetName, cmd->position.x, cmd->position.y, cmd->position.z);
            }
            if (cmd->setScale) {
                transform.scale = cmd->scale;
                spdlog::info("Scaled '{}' to ({}, {}, {})",
                           cmd->targetName, cmd->scale.x, cmd->scale.y, cmd->scale.z);
            }
            return;
        }
    }

    spdlog::warn("Entity '{}' not found", cmd->targetName);
}

void CommandQueue::ExecuteModifyMaterial(ModifyMaterialCommand* cmd, Scene::ECS_Registry* registry) {
    // Find entity by tag
    auto view = registry->View<Scene::TagComponent, Scene::RenderableComponent>();

    for (auto entity : view) {
        auto& tag = view.get<Scene::TagComponent>(entity);
        if (tag.tag == cmd->targetName) {
            auto& renderable = view.get<Scene::RenderableComponent>(entity);

            if (cmd->setColor) {
                renderable.albedoColor = cmd->color;
                spdlog::info("Changed '{}' color to ({}, {}, {})",
                           cmd->targetName, cmd->color.r, cmd->color.g, cmd->color.b);
            }
            if (cmd->setMetallic) {
                renderable.metallic = cmd->metallic;
            }
            if (cmd->setRoughness) {
                renderable.roughness = cmd->roughness;
            }
            return;
        }
    }

    spdlog::warn("Entity '{}' not found", cmd->targetName);
}

void CommandQueue::ExecuteModifyCamera(ModifyCameraCommand* cmd, Scene::ECS_Registry* registry) {
    // Find active camera
    auto view = registry->View<Scene::CameraComponent, Scene::TransformComponent>();

    for (auto entity : view) {
        auto& camera = view.get<Scene::CameraComponent>(entity);
        if (camera.isActive) {
            auto& transform = view.get<Scene::TransformComponent>(entity);

            if (cmd->setPosition) {
                transform.position = cmd->position;
                spdlog::info("Moved camera to ({}, {}, {})",
                           cmd->position.x, cmd->position.y, cmd->position.z);
            }
            if (cmd->setFOV) {
                camera.fov = cmd->fov;
                spdlog::info("Changed camera FOV to {}", cmd->fov);
            }
            return;
        }
    }

    spdlog::warn("No active camera found");
}

} // namespace Cortex::LLM
