#include "CommandQueue.h"
#include "Graphics/Renderer.h"
#include "Utils/MeshGenerator.h"
#include "Utils/GLTFLoader.h"
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
#include <cctype>

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

// Derive a logical group name from a tag so that
// Pig_1.Body -> Pig_1 and Field_Grass_12 -> Field_Grass.
std::string DeriveLogicalGroupName(const std::string& tag) {
    if (tag.empty()) return {};

    auto dotPos = tag.find('.');
    if (dotPos != std::string::npos && dotPos > 0) {
        return tag.substr(0, dotPos);
    }

    auto underscore = tag.find_last_of('_');
    if (underscore != std::string::npos && underscore + 1 < tag.size()) {
        bool allDigits = true;
        for (size_t i = underscore + 1; i < tag.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(tag[i]))) {
                allDigits = false;
                break;
            }
        }
        if (allDigits && underscore > 0) {
            return tag.substr(0, underscore);
        }
    }

    return tag;
}

std::optional<glm::vec3> FindAutoPlaceAnchor(Scene::ECS_Registry* registry, SceneLookup& lookup) {
    if (!registry) return std::nullopt;

    // Prefer the most recently spawned/edited entity name from the lookup.
    if (auto lastName = lookup.GetLastSpawnedName(registry)) {
        std::string hint;
        entt::entity e = lookup.ResolveTarget(*lastName, registry, hint);
        if (e != entt::null && registry->HasComponent<Scene::TransformComponent>(e)) {
            auto& t = registry->GetComponent<Scene::TransformComponent>(e);
            return t.position;
        }
    }

    // Fallback: use a point in front of the active camera, with distance scaled
    // by the camera's far plane so that "autoPlace" feels reasonable across
    // small rooms and large outdoor scenes.
    auto camView = registry->View<Scene::CameraComponent, Scene::TransformComponent>();
    for (auto entity : camView) {
        auto& camera = camView.get<Scene::CameraComponent>(entity);
        if (!camera.isActive) continue;
        auto& t = camView.get<Scene::TransformComponent>(entity);
        glm::vec3 camPos = t.position;
        glm::vec3 forward = glm::normalize(t.rotation * glm::vec3(0.0f, 0.0f, 1.0f));
        if (!std::isfinite(forward.x) || !std::isfinite(forward.y) || !std::isfinite(forward.z) ||
            glm::length2(forward) < 1e-6f) {
            forward = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        float distance = 3.0f;
        if (std::isfinite(camera.farPlane) && camera.farPlane > 0.0f) {
            // Place new objects roughly a few percent into the view depth,
            // clamped to sane near/mid distances for traversal.
            float scaled = camera.farPlane * 0.02f;
            distance = std::clamp(scaled, 3.0f, 50.0f);
        }
        glm::vec3 anchor = camPos + forward * distance;
        anchor.y = std::max(anchor.y, 0.5f);
        return anchor;
    }

    return std::nullopt;
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
        case CommandType::ScenePlan:
            ExecuteScenePlan(static_cast<ScenePlanCommand*>(command), registry, renderer);
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
            ExecuteAddLight(static_cast<AddLightCommand*>(command), registry, renderer);
            break;
        case CommandType::ModifyLight:
            ExecuteModifyLight(static_cast<ModifyLightCommand*>(command), registry);
            break;
        case CommandType::ModifyRenderer:
            ExecuteModifyRenderer(static_cast<ModifyRendererCommand*>(command), renderer, registry);
            break;
        case CommandType::SelectEntity: {
            auto* cmd = static_cast<SelectEntityCommand*>(command);
            if (m_selectionCallback && !cmd->targetName.empty()) {
                auto resolved = m_selectionCallback(cmd->targetName);
                if (resolved.has_value()) {
                    PushStatus(true, "Selected entity '" + *resolved + "'");
                } else {
                    PushStatus(false, "SelectEntity failed (no entity matching '" + cmd->targetName + "')");
                }
            } else {
                PushStatus(false, "SelectEntity ignored (no callback or target)");
            }
            break;
        }
        case CommandType::FocusCamera: {
            auto* cmd = static_cast<FocusCameraCommand*>(command);
            if (m_focusCameraCallback && !cmd->targetName.empty()) {
                m_focusCameraCallback(cmd->targetName);
                PushStatus(true, "Requested camera focus on '" + cmd->targetName + "'");
            } else if (m_focusCameraCallback && cmd->hasTargetPosition) {
                // For position-only focus, we encode a synthetic name that the
                // Engine can interpret if desired; for now we just log it.
                PushStatus(true, "Requested camera focus on explicit position");
                m_focusCameraCallback(std::string{});
            } else {
                PushStatus(false, "FocusCamera ignored (no callback)");
            }
            break;
        }
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

    std::shared_ptr<Scene::MeshData> mesh;

    if (cmd->entityType == AddEntityCommand::EntityType::Model) {
        // glTF sample model path: asset name must be provided.
        if (cmd->asset.empty()) {
            spdlog::warn("AddEntity model requested without an 'asset' name; falling back to cube");
        } else {
            auto it = m_modelMeshCache.find(cmd->asset);
            if (it != m_modelMeshCache.end()) {
                mesh = it->second;
            }

            if (!mesh || !mesh->gpuBuffers || !mesh->gpuBuffers->vertexBuffer || !mesh->gpuBuffers->indexBuffer) {
                auto meshResult = Utils::LoadSampleModelMesh(cmd->asset);
                if (meshResult.IsErr()) {
                    spdlog::warn("Failed to load sample model '{}': {}", cmd->asset, meshResult.Error());
                } else {
                    mesh = meshResult.Value();
                    auto uploadResult = renderer->UploadMesh(mesh);
                    if (uploadResult.IsErr()) {
                        spdlog::warn("Failed to upload sample model mesh '{}': {}", cmd->asset, uploadResult.Error());
                        mesh.reset();
                    } else {
                        m_modelMeshCache[cmd->asset] = mesh;
                    }
                }
            }
        }

        // If anything went wrong, gracefully fall back to a simple sphere so
        // the command still produces something visible.
        if (!mesh) {
            mesh = Utils::MeshGenerator::CreateSphere(0.5f, 32);
            auto uploadResult = renderer->UploadMesh(mesh);
            if (uploadResult.IsErr()) {
                spdlog::error("Failed to upload fallback mesh for model entity: {}", uploadResult.Error());
                PushStatus(false, "Failed to upload mesh for model entity");
                return;
            }
        }
    } else {
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
                case AddEntityCommand::EntityType::Model:
                    // Handled above.
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
        glm::vec3 baseOrigin(0.0f, 1.0f, -3.0f);
        if (auto anchor = FindAutoPlaceAnchor(registry, m_lookup)) {
            baseOrigin = *anchor;
            baseOrigin.y = std::max(baseOrigin.y, 0.5f);
        }
        desiredPos = baseOrigin + placementBias;
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

    if (cmd->hasPositionOffset) {
        glm::vec3 offset = SanitizeVec3(cmd->positionOffset, 0.0f, false);
        transform.position = ClampToWorld(transform.position + offset);
    }

    transform.scale = safeScale;

    // Add renderable
    auto& renderable = registry->AddComponent<Scene::RenderableComponent>(entity);
    renderable.mesh = mesh;
    renderable.albedoColor = SanitizeColor(cmd->color);

    auto sanitizeChannel = [](float value, float defValue, const char* fieldName) {
        if (!std::isfinite(value) || value < 0.0f || value > 1.0f) {
            spdlog::warn("AddEntity {} value {} out of range [0,1], using default {}", fieldName, value, defValue);
            return defValue;
        }
        return value;
    };

    float metallic = sanitizeChannel(cmd->metallic, 0.0f, "metallic");
    float roughness = sanitizeChannel(cmd->roughness, 0.5f, "roughness");
    float ao = sanitizeChannel(cmd->ao, 1.0f, "ao");

    renderable.metallic = SaturateScalar(metallic);
    renderable.roughness = SaturateScalar(roughness);
    renderable.ao = SaturateScalar(ao);
    if (cmd->hasPreset) {
        renderable.presetName = cmd->presetName;
    }
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

    // Newly spawned entities become the current focus, using their logical group name.
    if (m_focusCallback) {
        m_focusCallback(DeriveLogicalGroupName(name));
    }
}

void CommandQueue::ExecuteAddLight(AddLightCommand* cmd, Scene::ECS_Registry* registry, Graphics::Renderer* renderer) {
    if (!registry || !cmd) {
        PushStatus(false, "AddLight skipped: missing registry");
        return;
    }

    entt::entity e = registry->CreateEntity();

    // Tag for lookup/debugging
    std::string name = cmd->name.empty() ? "Light_" + std::to_string(m_spawnIndex++) : cmd->name;
    registry->AddComponent<Scene::TagComponent>(e, name);

    auto& transform = registry->AddComponent<Scene::TransformComponent>(e);

    // Auto-placement relative to the active camera when requested. This lets
    // LLM commands like "add a light here" or "place a lantern where I'm
    // looking" omit explicit world coordinates.
    glm::vec3 lightPos = cmd->position;
    bool useAuto = cmd->autoPlace ||
                   glm::all(glm::epsilonEqual(cmd->position, glm::vec3(0.0f), 1e-4f));
    AddLightCommand::AnchorMode anchorMode = cmd->anchorMode;
    if (useAuto && anchorMode == AddLightCommand::AnchorMode::None) {
        anchorMode = AddLightCommand::AnchorMode::CameraForward;
    }

    if (useAuto && anchorMode != AddLightCommand::AnchorMode::None) {
        auto camView = registry->View<Scene::CameraComponent, Scene::TransformComponent>();
        for (auto entity : camView) {
            auto& camera = camView.get<Scene::CameraComponent>(entity);
            if (!camera.isActive) continue;
            auto& camTransform = camView.get<Scene::TransformComponent>(entity);
            glm::vec3 camPos = camTransform.position;
            glm::vec3 forward = glm::normalize(camTransform.rotation * glm::vec3(0.0f, 0.0f, 1.0f));
            if (!std::isfinite(forward.x) || !std::isfinite(forward.y) || !std::isfinite(forward.z) ||
                glm::length2(forward) < 1e-6f) {
                forward = glm::vec3(0.0f, 0.0f, 1.0f);
            }

            if (anchorMode == AddLightCommand::AnchorMode::Camera) {
                lightPos = camPos;
            } else if (anchorMode == AddLightCommand::AnchorMode::CameraForward) {
                float dist = cmd->forwardDistance > 0.0f ? cmd->forwardDistance : 5.0f;
                lightPos = camPos + forward * dist;
            }
            break;
        }
    }

    transform.position = SanitizeVec3(lightPos, 0.0f, false);

    // Build rotation from direction for spot/directional lights. If no explicit
    // direction was provided and we auto-anchored along camera forward, align
    // the light with that forward vector for intuitive spotlights.
    glm::vec3 forward = cmd->direction;
    if (useAuto && anchorMode == AddLightCommand::AnchorMode::CameraForward) {
        auto camView = registry->View<Scene::CameraComponent, Scene::TransformComponent>();
        for (auto entity : camView) {
            auto& camera = camView.get<Scene::CameraComponent>(entity);
            if (!camera.isActive) continue;
            auto& camTransform = camView.get<Scene::TransformComponent>(entity);
            forward = camTransform.rotation * glm::vec3(0.0f, 0.0f, 1.0f);
            break;
        }
    }
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
    entt::entity target = ResolveTargetWithFocus(cmd->targetName, registry, hint);
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
    if (!registry || !cmd) {
        PushStatus(false, "move/scale failed: missing registry");
        return;
    }

    // If the target name looks like a logical group (e.g., "Pig_1") and there
    // are multiple entities whose tags share that prefix ("Pig_1.Body",
    // "Pig_1.Head", ...), treat this as a group-level transform so that
    // commands like "move the pig higher" naturally move the whole compound.
    auto view = registry->View<Scene::TagComponent, Scene::TransformComponent>();
    std::vector<entt::entity> groupMembers;
    std::string groupName = cmd->targetName;
    if (!groupName.empty() && groupName.find('.') == std::string::npos) {
        for (auto entity : view) {
            const auto& tag = view.get<Scene::TagComponent>(entity);
            const std::string& name = tag.tag;
            if (name == groupName ||
                name.rfind(groupName + ".", 0) == 0 ||
                name.rfind(groupName + "_", 0) == 0) {
                groupMembers.push_back(entity);
            }
        }
    }

    if (!groupMembers.empty()) {
        glm::vec3 center(0.0f);
        int count = 0;
        for (auto entity : groupMembers) {
            auto& t = view.get<Scene::TransformComponent>(entity);
            center += t.position;
            ++count;
        }
        if (count > 0) center /= static_cast<float>(count);

        std::ostringstream summary;
        summary << "group " << groupName << ": ";
        bool touchedGroup = false;

        if (cmd->setPosition) {
            glm::vec3 delta(0.0f);
            if (cmd->isRelative) {
                // Treat position as an offset to apply to the current center.
                delta = SanitizeVec3(cmd->position, 0.0f, false);
            } else {
                glm::vec3 newCenter = SanitizeVec3(cmd->position);
                delta = newCenter - center;
            }
            for (auto entity : groupMembers) {
                auto& t = view.get<Scene::TransformComponent>(entity);
                t.position = SanitizeVec3(t.position + delta);
            }
            summary << "offset(" << std::fixed << std::setprecision(2)
                    << delta.x << "," << delta.y << "," << delta.z << ") ";
            touchedGroup = true;
        }

        // For now, group-level ModifyTransform only handles position; scaling
        // and rotation of groups are handled via ModifyGroupCommand.

        if (touchedGroup) {
            PushStatus(true, summary.str());
            if (m_focusCallback && !groupName.empty()) {
                m_focusCallback(groupName);
            }
            return;
        }
        // If nothing was applied, fall through to single-entity behavior.
    }

    std::string hint;
    entt::entity target = ResolveTargetWithFocus(cmd->targetName, registry, hint);
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
        glm::vec3 delta(0.0f);
        if (cmd->isRelative) {
            delta = SanitizeVec3(cmd->position, 0.0f, false);
            transform.position = SanitizeVec3(transform.position + delta);
        } else {
            transform.position = SanitizeVec3(cmd->position);
        }
        spdlog::info("Moved '{}' to ({}, {}, {})",
                   tagName, transform.position.x, transform.position.y, transform.position.z);
        summary << "pos(" << std::fixed << std::setprecision(2)
                << transform.position.x << "," << transform.position.y << "," << transform.position.z << ") ";
        if (cmd->isRelative) {
            summary << "+d(" << delta.x << "," << delta.y << "," << delta.z << ") ";
        } else {
            summary << " ";
        }
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
        glm::vec3 resultScale;
        if (cmd->isRelative) {
            // Treat incoming scale as a multiplicative factor, e.g. [1.3,1.3,1.3]
            glm::vec3 factor = cmd->scale;
            for (int i = 0; i < 3; ++i) {
                if (!std::isfinite(factor[i]) || factor[i] == 0.0f) {
                    factor[i] = 1.0f;
                }
            }
            // Clamp relative multipliers to a sane range to avoid extreme sizes.
            factor = glm::clamp(factor, glm::vec3(0.25f), glm::vec3(4.0f));
            resultScale = transform.scale * factor;
        } else {
            resultScale = cmd->scale;
        }

        glm::vec3 clampedScale = SanitizeVec3(resultScale, 0.05f, false);
        clampedScale = glm::clamp(clampedScale, glm::vec3(-100.0f), glm::vec3(100.0f));
        transform.scale = clampedScale;
        spdlog::info("Scaled '{}' to ({}, {}, {})",
                   tagName, transform.scale.x, transform.scale.y, transform.scale.z);
        summary << "scale(" << transform.scale.x << "," << transform.scale.y << "," << transform.scale.z << ") ";
        touched = true;
    }

    if (touched) {
        PushStatus(true, summary.str());
        if (m_focusCallback && !tagName.empty()) {
            m_focusCallback(DeriveLogicalGroupName(tagName));
        }
    }
}

void CommandQueue::ExecuteModifyMaterial(ModifyMaterialCommand* cmd, Scene::ECS_Registry* registry) {
    std::string hint;
    entt::entity target = ResolveTargetWithFocus(cmd->targetName, registry, hint);
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

    // Optional preset application (base), before specific overrides.
    if (cmd->setPreset && !cmd->presetName.empty()) {
        std::string name = cmd->presetName;
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        struct MaterialPreset {
            glm::vec4 color;
            float metallic;
            float roughness;
        };

        static const std::unordered_map<std::string, MaterialPreset> kPresets = {
            {"chrome",        {{0.8f, 0.8f, 0.85f, 1.0f}, 1.0f, 0.05f}},
            {"gold",          {{1.0f, 0.85f, 0.3f, 1.0f}, 1.0f, 0.2f}},
            {"brushed_metal", {{0.7f, 0.7f, 0.7f, 1.0f}, 1.0f, 0.35f}},
            {"steel",         {{0.75f, 0.75f, 0.8f, 1.0f}, 1.0f, 0.25f}},
            {"plastic",       {{0.8f, 0.8f, 0.8f, 1.0f}, 0.0f, 0.4f}},
            {"rubber",        {{0.1f, 0.1f, 0.1f, 1.0f}, 0.0f, 0.9f}},
            {"wood",          {{0.6f, 0.4f, 0.25f, 1.0f}, 0.0f, 0.6f}},
            {"stone",         {{0.5f, 0.5f, 0.55f, 1.0f}, 0.0f, 0.8f}},
            {"glass",         {{0.8f, 0.9f, 1.0f, 0.3f}, 1.0f, 0.02f}},
        };

        auto it = kPresets.find(name);
        if (it != kPresets.end()) {
            const auto& preset = it->second;
            renderable.albedoColor = SanitizeColor(preset.color);
            renderable.metallic = SaturateScalar(preset.metallic);
            renderable.roughness = SaturateScalar(preset.roughness);
            renderable.presetName = name;
            summary << "preset=" << name << " ";
            touched = true;
        } else {
            spdlog::warn("Unknown material preset '{}'", name);
        }
    }

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
    if (cmd->setAO) {
        renderable.ao = SaturateScalar(cmd->ao);
        summary << "ao ";
        touched = true;
    }

    if (touched) {
        PushStatus(true, summary.str());
        if (m_focusCallback && !tagName.empty()) {
            m_focusCallback(DeriveLogicalGroupName(tagName));
        }
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

entt::entity CommandQueue::ResolveTargetWithFocus(const std::string& targetName,
                                                  Scene::ECS_Registry* registry,
                                                  std::string& outHint) {
    outHint.clear();

    // Prefer the engine's currently focused entity when the command's
    // target name matches the advertised focus name. This keeps LLM-driven
    // edits "locked" onto the same object the user has selected/framed,
    // even if other entities share similar names.
    if (!targetName.empty() && !m_currentFocusName.empty() && registry) {
        auto toLower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        };

        if (toLower(targetName) == toLower(m_currentFocusName)) {
            auto& reg = registry->GetRegistry();
            if (m_currentFocusEntity != entt::null && reg.valid(m_currentFocusEntity)) {
                outHint = "Using focused entity";
                return m_currentFocusEntity;
            }
        }
    }

    // Otherwise, fall back to the standard lookup logic (pronouns,
    // color/type hints, exact/partial name matches).
    return m_lookup.ResolveTarget(targetName, registry, outHint);
}

void CommandQueue::ExecuteModifyRenderer(ModifyRendererCommand* cmd, Graphics::Renderer* renderer, Scene::ECS_Registry* registry) {
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
    if (cmd->setEnvironment) {
        renderer->SetEnvironmentPreset(cmd->environment);
        summary << "environment=" << cmd->environment << " ";
        touched = true;
    }
    if (cmd->setIBLEnabled) {
        renderer->SetIBLEnabled(cmd->iblEnabled);
        summary << "ibl=" << (cmd->iblEnabled ? "on" : "off") << " ";
        touched = true;
    }
    if (cmd->setIBLIntensity) {
        renderer->SetIBLIntensity(cmd->iblDiffuseIntensity, cmd->iblSpecularIntensity);
        summary << "ibl_intensity=[" << cmd->iblDiffuseIntensity << "," << cmd->iblSpecularIntensity << "] ";
        touched = true;
    }
    if (cmd->setColorGrade) {
        renderer->SetColorGrade(cmd->colorGradeWarm, cmd->colorGradeCool);
        summary << "grade=(" << cmd->colorGradeWarm << "," << cmd->colorGradeCool << ") ";
        touched = true;
    }
    if (cmd->setSSAOEnabled) {
        renderer->SetSSAOEnabled(cmd->ssaoEnabled);
        summary << "ssao=" << (cmd->ssaoEnabled ? "on" : "off") << " ";
        touched = true;
    }
    if (cmd->setSSAOParams) {
        renderer->SetSSAOParams(cmd->ssaoRadius, cmd->ssaoBias, cmd->ssaoIntensity);
        summary << "ssao_params=(r:" << cmd->ssaoRadius << ",b:" << cmd->ssaoBias << ",i:" << cmd->ssaoIntensity << ") ";
        touched = true;
    }
    if (cmd->setFogEnabled) {
        renderer->SetFogEnabled(cmd->fogEnabled);
        summary << "fog=" << (cmd->fogEnabled ? "on" : "off") << " ";
        touched = true;
    }
    if (cmd->setFogParams) {
        renderer->SetFogParams(cmd->fogDensity, cmd->fogHeight, cmd->fogFalloff);
        summary << "fog_params=(d:" << cmd->fogDensity << ",h:" << cmd->fogHeight << ",f:" << cmd->fogFalloff << ") ";
        touched = true;
    }
    if (cmd->setSunDirection) {
        renderer->SetSunDirection(cmd->sunDirection);
        summary << "sun_dir=(" << cmd->sunDirection.x << "," << cmd->sunDirection.y << "," << cmd->sunDirection.z << ") ";
        touched = true;
    }
    if (cmd->setSunColor) {
        renderer->SetSunColor(cmd->sunColor);
        summary << "sun_color=(" << cmd->sunColor.r << "," << cmd->sunColor.g << "," << cmd->sunColor.b << ") ";
        touched = true;
    }
    if (cmd->setSunIntensity) {
        renderer->SetSunIntensity(cmd->sunIntensity);
        summary << "sun_intensity=" << cmd->sunIntensity << " ";
        touched = true;
    }
    if (cmd->setLightingRig && registry) {
        // Map string identifiers to renderer rigs. Accept a few aliases to
        // keep prompts flexible.
        std::string name = cmd->lightingRig;
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        Graphics::Renderer::LightingRig rig = Graphics::Renderer::LightingRig::Custom;
        if (name == "studio_three_point" || name == "studio" || name == "three_point") {
            rig = Graphics::Renderer::LightingRig::StudioThreePoint;
        } else if (name == "warehouse" || name == "top_down_warehouse" || name == "topdown_warehouse") {
            rig = Graphics::Renderer::LightingRig::TopDownWarehouse;
        } else if (name == "horror_side" || name == "horror" || name == "horror_side_light") {
            rig = Graphics::Renderer::LightingRig::HorrorSideLight;
        } else if (name == "street_lanterns" || name == "streetlights" || name == "street_lights" ||
                   name == "alley_lights" || name == "road_lights") {
            rig = Graphics::Renderer::LightingRig::StreetLanterns;
        }

        if (rig != Graphics::Renderer::LightingRig::Custom) {
            renderer->ApplyLightingRig(rig, registry);
            summary << "lighting_rig=" << name << " ";
            touched = true;
        }
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

    // Look up a built-in prefab first; if not found, try to synthesize a
    // reasonable approximation (e.g., "pig" -> generic quadruped) so
    // add_compound never hard-fails for new nouns.
    bool synthesized = false;
    const CompoundTemplate* templ = CompoundLibrary::FindTemplate(cmd->templateName);
    if (!templ) {
        const glm::vec4* body = cmd->hasBodyColor ? &cmd->bodyColor : nullptr;
        const glm::vec4* accent = cmd->hasAccentColor ? &cmd->accentColor : nullptr;
        templ = CompoundLibrary::SynthesizeTemplate(cmd->templateName, body, accent);
        if (templ) {
            synthesized = true;
        }
    }

    // Derive a stable instance name/group prefix
    std::string instanceName = cmd->instanceName;
    if (instanceName.empty()) {
        const std::string baseName = templ ? templ->defaultGroupPrefix : cmd->templateName;
        instanceName = baseName.empty() ? ("Compound_" + std::to_string(m_spawnIndex++))
                                        : (baseName + "_" + std::to_string(m_spawnIndex++));
    }

    glm::vec3 basePos = SanitizeVec3(cmd->position);
    glm::vec3 baseScale = cmd->scale;
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(baseScale[i]) || std::abs(baseScale[i]) < 0.01f) {
            baseScale[i] = (baseScale[i] >= 0.0f ? 1.0f : -1.0f);
        }
    }

    // If no specific location was given (position near zero), treat this as
    // an auto-placed compound and try to spawn it in free space in front of
    // the camera, avoiding overlap with existing entities.
    const bool autoPlace =
        glm::all(glm::epsilonEqual(cmd->position, glm::vec3(0.0f), 1e-4f));
    if (autoPlace) {
        const float spawnRadius = std::max(1.5f, glm::compMax(glm::abs(baseScale)));
        const float spacing = std::max(1.5f, spawnRadius * 2.2f);
        glm::vec3 placementBias = NextPlacementOffset(m_spawnIndex++, spacing);
        glm::vec3 baseOrigin(0.0f, 1.0f, -3.0f);
        if (auto anchor = FindAutoPlaceAnchor(registry, m_lookup)) {
            baseOrigin = *anchor;
            baseOrigin.y = std::max(baseOrigin.y, 0.5f);
        }
        glm::vec3 desired = baseOrigin + placementBias;
        basePos = FindNonOverlappingPosition(registry, desired, spawnRadius);
    }

    if (!templ) {
        // Final safety net: spawn a single proxy sphere so the engine
        // always creates something instead of failing.
        AddEntityCommand proxy;
        proxy.entityType = AddEntityCommand::EntityType::Sphere;
        proxy.autoPlace = false;
        proxy.allowPlacementJitter = false;
        proxy.disableCollisionAvoidance = true;
        proxy.segmentsPrimary = 20;
        proxy.segmentsSecondary = 12;
        proxy.position = basePos;
        proxy.scale = glm::vec3(
            std::max(0.5f, std::abs(baseScale.x)),
            std::max(0.5f, std::abs(baseScale.y)),
            std::max(0.5f, std::abs(baseScale.z)));
        proxy.color = glm::vec4(0.8f, 0.7f, 0.9f, 1.0f);
        proxy.name = instanceName.empty() ? "CompoundProxy" : instanceName;
        ExecuteAddEntity(&proxy, registry, renderer);

        std::ostringstream ss;
        ss << "add_compound '" << cmd->templateName << "' not recognized; spawned proxy sphere '" << proxy.name << "'";
        PushStatus(true, ss.str());
        return;
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

    // Optionally attach a light source at the compound's base when the template
    // requests it (used for lanterns, streetlights, etc.). This lets a single
    // add_compound create both geometry and its emitting light.
    if (templ->hasAttachedLight) {
        AddLightCommand lightCmd;
        lightCmd.lightType = templ->lightType;
        lightCmd.position = basePos + templ->lightLocalPosition;
        lightCmd.direction = glm::vec3(0.0f, -1.0f, 0.0f);
        lightCmd.color = templ->lightColor;
        lightCmd.intensity = templ->lightIntensity;
        lightCmd.range = templ->lightRange;
        lightCmd.innerConeDegrees = templ->lightInnerConeDegrees;
        lightCmd.outerConeDegrees = templ->lightOuterConeDegrees;
        lightCmd.castsShadows = templ->lightCastsShadows;
        lightCmd.name = instanceName + ".Light";
        ExecuteAddLight(&lightCmd, registry, renderer);
    }

    std::ostringstream ss;
    if (synthesized) {
        ss << "synthesized compound " << cmd->templateName << " as " << instanceName
           << " (" << templ->parts.size() << " parts)";
    } else {
        ss << "spawned compound " << templ->name << " as " << instanceName
           << " (" << templ->parts.size() << " parts)";
    }
    if (templ->hasAttachedLight) {
        ss << " with attached light";
    }
    PushStatus(true, ss.str());
}

void CommandQueue::ExecuteScenePlan(ScenePlanCommand* cmd, Scene::ECS_Registry* registry, Graphics::Renderer* renderer) {
    if (!cmd || !registry || !renderer) {
        PushStatus(false, "scene_plan failed: missing registry or renderer");
        return;
    }

    auto resolveGroupCenter = [&](const std::string& groupName, glm::vec3& outCenter) -> bool {
        if (groupName.empty() || !registry) return false;

        struct GroupInfo {
            glm::vec3 minPos{0.0f};
            glm::vec3 maxPos{0.0f};
            bool hasBounds = false;
        };

        std::unordered_map<std::string, GroupInfo> groups;
        auto view = registry->View<Scene::TagComponent, Scene::TransformComponent>();
        for (auto entity : view) {
            const auto& tag = view.get<Scene::TagComponent>(entity);
            const auto& transform = view.get<Scene::TransformComponent>(entity);
            const std::string& name = tag.tag;

            // Match either the base group or numbered variants like Field_Grass_2.*
            if (name == groupName ||
                name.rfind(groupName + ".", 0) == 0 ||
                name.rfind(groupName + "_", 0) == 0) {
                std::string key = groupName;
                if (name.rfind(groupName + "_", 0) == 0) {
                    // Extract prefix up to next '.' so Field_Grass_2.Body -> Field_Grass_2
                    size_t start = groupName.size() + 1;
                    size_t dot = name.find('.', start);
                    if (dot == std::string::npos) {
                        key = name;
                    } else {
                        key = name.substr(0, dot);
                    }
                }

                auto& g = groups[key];
                if (!g.hasBounds) {
                    g.minPos = g.maxPos = transform.position;
                    g.hasBounds = true;
                } else {
                    g.minPos = glm::min(g.minPos, transform.position);
                    g.maxPos = glm::max(g.maxPos, transform.position);
                }
            }
        }

        float bestArea = -1.0f;
        bool found = false;
        for (const auto& [key, g] : groups) {
            if (!g.hasBounds) continue;
            glm::vec3 extents = g.maxPos - g.minPos;
            float ex = std::abs(extents.x);
            float ez = std::abs(extents.z);
            float area = ex * ez;
            if (area > bestArea) {
                bestArea = area;
                outCenter = 0.5f * (g.minPos + g.maxPos);
                found = true;
            }
        }
        return found;
    };

    std::ostringstream recipe;
    recipe << "ScenePlan: ";

    for (const auto& region : cmd->regions) {
        ScenePlanCommand::Region resolved = region;
        if (!resolved.attachToGroup.empty()) {
            glm::vec3 baseCenter;
            if (resolveGroupCenter(resolved.attachToGroup, baseCenter)) {
                resolved.center = baseCenter;
                if (resolved.hasOffset) {
                    resolved.center += resolved.offset;
                }
            }
        }

        std::string kind = region.kind;
        std::transform(kind.begin(), kind.end(), kind.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (kind == "field") {
            BuildFieldRegion(resolved, registry, renderer);
        } else if (kind == "road") {
            BuildRoadRegion(resolved, registry, renderer);
        } else {
            BuildGenericRegion(resolved, registry, renderer);
        }

        if (!resolved.name.empty()) {
            recipe << resolved.name << "(" << kind << ",center=("
                   << std::round(resolved.center.x) << "," << std::round(resolved.center.z)
                   << "),size=(" << std::round(resolved.size.x) << "," << std::round(resolved.size.z)
                   << ")); ";
        }
    }

    m_lastSceneRecipe = recipe.str();
    const size_t kMaxRecipeChars = 2048;
    if (m_lastSceneRecipe.size() > kMaxRecipeChars) {
        m_lastSceneRecipe.resize(kMaxRecipeChars);
    }

    PushStatus(true, "scene_plan executed");
}

void CommandQueue::BuildFieldRegion(const ScenePlanCommand::Region& region,
                                    Scene::ECS_Registry* registry,
                                    Graphics::Renderer* renderer) {
    if (!registry || !renderer) {
        return;
    }

    glm::vec3 center = region.center;
    glm::vec3 size = region.size;

    auto pattern = std::make_shared<AddPatternCommand>();
    pattern->pattern = AddPatternCommand::PatternType::Grid;
    pattern->element = "grass_blade";
    pattern->count = 64;
    pattern->regionMin = center - 0.5f * size;
    pattern->regionMax = center + 0.5f * size;
    pattern->hasRegionBox = true;
    pattern->groupName = region.name.empty() ? "Field_Grass" : region.name;

    ExecuteAddPattern(pattern.get(), registry, renderer);

    // Optionally add a few trees via compounds so fields feel richer.
    for (int i = 0; i < 3; ++i) {
        auto comp = std::make_shared<AddCompoundCommand>();
        comp->templateName = "tree";
        comp->instanceName = pattern->groupName + "_Tree" + std::to_string(i);
        comp->position = center + glm::vec3((i - 1) * 3.0f, 0.0f, size.z * 0.25f);
        comp->scale = glm::vec3(1.0f);
        ExecuteAddCompound(comp.get(), registry, renderer);
    }
}

void CommandQueue::BuildRoadRegion(const ScenePlanCommand::Region& region,
                                   Scene::ECS_Registry* registry,
                                   Graphics::Renderer* renderer) {
    if (!registry || !renderer) {
        return;
    }

    glm::vec3 center = region.center;
    glm::vec3 size = region.size;

    auto road = std::make_shared<AddEntityCommand>();
    road->entityType = AddEntityCommand::EntityType::Plane;
    road->name = region.name.empty() ? "Road" : region.name;
    road->position = center;
    road->scale = glm::vec3(size.x, 1.0f, size.z);
    road->color = glm::vec4(0.3f, 0.3f, 0.3f, 1.0f);
    ExecuteAddEntity(road.get(), registry, renderer);

    auto lanes = std::make_shared<AddPatternCommand>();
    lanes->pattern = AddPatternCommand::PatternType::Row;
    lanes->element = "plane";
    lanes->count = 6;
    lanes->hasRegionBox = false;
    lanes->regionMin = center + glm::vec3(0.0f, 0.01f, 0.0f);
    lanes->spacing = glm::vec3(size.x / 6.0f, 0.0f, 0.0f);
    lanes->hasSpacing = true;
    lanes->groupName = road->name + "_Lanes";
    ExecuteAddPattern(lanes.get(), registry, renderer);
}

void CommandQueue::BuildGenericRegion(const ScenePlanCommand::Region& region,
                                      Scene::ECS_Registry* registry,
                                      Graphics::Renderer* renderer) {
    if (!registry || !renderer) {
        return;
    }

    auto pattern = std::make_shared<AddPatternCommand>();
    pattern->pattern = AddPatternCommand::PatternType::Random;
    pattern->element = "cube";
    pattern->count = 8;
    pattern->regionMin = region.center - 0.5f * region.size;
    pattern->regionMax = region.center + 0.5f * region.size;
    pattern->hasRegionBox = true;
    pattern->groupName = region.name.empty() ? "Region" : region.name;
    ExecuteAddPattern(pattern.get(), registry, renderer);
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
    std::string kindLower = cmd->kind;
    std::transform(kindLower.begin(), kindLower.end(), kindLower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    bool herdMode = (kindLower == "herd");
    bool trafficMode = (kindLower == "traffic");
    bool useCompoundPattern = herdMode || trafficMode;

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

        // Optional jitter for structured patterns to avoid perfectly rigid rows/grids/rings.
        if (cmd->jitter && cmd->jitterAmount > 0.0f &&
            cmd->pattern != AddPatternCommand::PatternType::Random) {
            std::string key = groupName.empty() ? "Pattern" : groupName;
            uint32_t h = std::hash<std::string>{}(key) ^ (0x9E3779B9u + static_cast<uint32_t>(i) * 0x85EBCA6Bu);
            float jx = (static_cast<float>(h & 0xFFu) / 255.0f - 0.5f) * cmd->jitterAmount;
            float jz = (static_cast<float>((h >> 8) & 0xFFu) / 255.0f - 0.5f) * cmd->jitterAmount;
            worldPos.x += jx;
            worldPos.z += jz;
            worldPos = SanitizeVec3(worldPos);
        }

        if (herdMode) {
            // Small positional jitter so herds don't look like perfect lattices.
            uint32_t h = std::hash<int>{}(i + 1u);
            float jx = (static_cast<float>(h & 0xFFu) / 255.0f - 0.5f) * 0.6f;
            float jz = (static_cast<float>((h >> 8) & 0xFFu) / 255.0f - 0.5f) * 0.6f;
            worldPos.x += jx;
            worldPos.z += jz;
            worldPos = SanitizeVec3(worldPos);
        }

        bool spawnCompound = (compoundTempl != nullptr) || useCompoundPattern;
        if (spawnCompound) {
            AddCompoundCommand sub;
            if (compoundTempl) {
                sub.templateName = compoundTempl->name;
            } else {
                // Use the element name if provided, otherwise fall back to generic motifs.
                if (!cmd->element.empty()) {
                    sub.templateName = cmd->element;
                } else if (herdMode) {
                    sub.templateName = "quadruped";
                } else if (trafficMode) {
                    sub.templateName = "vehicle";
                } else {
                    sub.templateName = "compound";
                }
            }
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
