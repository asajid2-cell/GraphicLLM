#include "Engine.h"
#include "Graphics/RendererControlApplier.h"
#include "Graphics/Renderer.h"
#include "Scene/Components.h"
#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/norm.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace Cortex {
namespace {
    using nlohmann::json;
    constexpr float kHeroPoolZ = -3.0f;

    std::vector<std::filesystem::path> GetShowcaseSceneConfigCandidates() {
        const std::filesystem::path cwd = std::filesystem::current_path();
        return {
            cwd / "assets" / "config" / "showcase_scenes.json",
            cwd / ".." / ".." / "assets" / "config" / "showcase_scenes.json",
            cwd / ".." / ".." / ".." / "assets" / "config" / "showcase_scenes.json"
        };
    }

    bool ReadVec3(const json& value, glm::vec3& out) {
        if (!value.is_array() || value.size() != 3) {
            return false;
        }
        for (const auto& component : value) {
            if (!component.is_number()) {
                return false;
            }
        }
        out = glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
        return true;
    }

    bool RayIntersectsAABB(const glm::vec3& rayOrigin,
                           const glm::vec3& rayDir,
                           const glm::vec3& aabbMin,
                           const glm::vec3& aabbMax,
                           float& outT) {
        float tMin = 0.0f;
        float tMax = std::numeric_limits<float>::max();

        for (int i = 0; i < 3; ++i) {
            float origin = rayOrigin[i];
            float dir = rayDir[i];
            if (std::abs(dir) < 1e-6f) {
                if (origin < aabbMin[i] || origin > aabbMax[i]) {
                    return false;
                }
                continue;
            }

            float invD = 1.0f / dir;
            float t0 = (aabbMin[i] - origin) * invD;
            float t1 = (aabbMax[i] - origin) * invD;
            if (t0 > t1) std::swap(t0, t1);

            tMin = std::max(tMin, t0);
            tMax = std::min(tMax, t1);
            if (tMax < tMin) {
                return false;
            }
        }

        outT = tMin;
        return tMax >= 0.0f;
    }

    bool RayHitsAxis(const glm::vec3& rayOrigin,
                     const glm::vec3& rayDir,
                     const glm::vec3& axisOrigin,
                     const glm::vec3& axisDir,
                     float axisLength,
                     float threshold,
                     float& outRayT) {
        glm::vec3 d1 = glm::normalize(rayDir);
        glm::vec3 d2 = glm::normalize(axisDir);
        glm::vec3 w0 = rayOrigin - axisOrigin;

        float a = glm::dot(d1, d1);
        float b = glm::dot(d1, d2);
        float c = glm::dot(d2, d2);
        float d = glm::dot(d1, w0);
        float e = glm::dot(d2, w0);
        float denom = a * c - b * b;
        if (std::abs(denom) < 1e-6f) {
            return false;
        }

        float tRay = (b * e - c * d) / denom;
        float tAxis = (a * e - b * d) / denom;
        tAxis = std::clamp(tAxis, 0.0f, axisLength);

        if (tRay < 0.0f) {
            return false;
        }

        glm::vec3 pRay = rayOrigin + d1 * tRay;
        glm::vec3 pAxis = axisOrigin + d2 * tAxis;
        float dist = glm::length(pRay - pAxis);
        if (dist > threshold) {
            return false;
        }

        outRayT = tRay;
        return true;
    }

    inline void ComputeGizmoScale(float distance,
                                  float& outAxisLength,
                                  float& outThreshold) {
        distance = std::max(distance, 0.1f);
        float axisLength = distance * 0.15f;
        axisLength = std::clamp(axisLength, 0.5f, 10.0f);
        float threshold = axisLength * 0.15f;

        outAxisLength = axisLength;
        outThreshold = threshold;
    }
}

bool Engine::ApplyShowcaseCameraBookmark(const std::string& bookmarkId) {
    if (bookmarkId.empty() || !m_registry ||
        m_activeCameraEntity == entt::null ||
        !m_registry->HasComponent<Scene::TransformComponent>(m_activeCameraEntity) ||
        !m_registry->HasComponent<Scene::CameraComponent>(m_activeCameraEntity)) {
        return false;
    }

    const char* sceneId = "default";
    switch (m_currentScenePreset) {
    case ScenePreset::CornellBox: sceneId = "cornell_box"; break;
    case ScenePreset::DragonOverWater: sceneId = "dragon_over_water"; break;
    case ScenePreset::RTShowcase: sceneId = "rt_showcase"; break;
    case ScenePreset::IBLGallery: sceneId = "ibl_gallery"; break;
    case ScenePreset::MaterialLab: sceneId = "material_lab"; break;
    case ScenePreset::GlassWaterCourtyard: sceneId = "glass_water_courtyard"; break;
    case ScenePreset::OutdoorSunsetBeach: sceneId = "outdoor_sunset_beach"; break;
    case ScenePreset::EffectsShowcase: sceneId = "effects_showcase"; break;
    case ScenePreset::GodRays: sceneId = "god_rays"; break;
    case ScenePreset::TemporalValidation: sceneId = "temporal_validation"; break;
    case ScenePreset::ProceduralTerrain: sceneId = "procedural_terrain"; break;
    default: break;
    }

    std::filesystem::path loadedPath;
    json root;
    for (const auto& candidate : GetShowcaseSceneConfigCandidates()) {
        std::error_code ec;
        const auto normalized = std::filesystem::weakly_canonical(candidate, ec);
        const auto& path = ec ? candidate : normalized;
        if (!std::filesystem::is_regular_file(path, ec)) {
            continue;
        }
        try {
            std::ifstream in(path);
            in >> root;
            loadedPath = path;
            break;
        } catch (const std::exception& ex) {
            spdlog::warn("Failed to parse showcase scene config '{}': {}", path.string(), ex.what());
            return false;
        }
    }

    if (root.is_null() || !root.contains("scenes") || !root["scenes"].is_array()) {
        return false;
    }

    const json* scene = nullptr;
    for (const auto& candidateScene : root["scenes"]) {
        if (candidateScene.value("id", std::string{}) == sceneId) {
            scene = &candidateScene;
            break;
        }
    }
    if (!scene || !scene->contains("camera_bookmarks") || !(*scene)["camera_bookmarks"].is_array()) {
        return false;
    }

    const json* bookmark = nullptr;
    for (const auto& candidateBookmark : (*scene)["camera_bookmarks"]) {
        if (candidateBookmark.value("id", std::string{}) == bookmarkId) {
            bookmark = &candidateBookmark;
            break;
        }
    }
    if (!bookmark) {
        return false;
    }

    glm::vec3 position(0.0f);
    glm::vec3 target(0.0f);
    if (!ReadVec3(bookmark->value("position", json::array()), position) ||
        !ReadVec3(bookmark->value("target", json::array()), target)) {
        spdlog::warn("Showcase camera bookmark '{}:{}' has invalid position/target",
                     sceneId, bookmarkId);
        return false;
    }

    glm::vec3 forward = target - position;
    if (glm::length2(forward) < 1e-6f) {
        spdlog::warn("Showcase camera bookmark '{}:{}' has a degenerate target",
                     sceneId, bookmarkId);
        return false;
    }
    forward = glm::normalize(forward);

    glm::vec3 up(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(forward, up)) > 0.99f) {
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    auto& transform = m_registry->GetComponent<Scene::TransformComponent>(m_activeCameraEntity);
    auto& camera = m_registry->GetComponent<Scene::CameraComponent>(m_activeCameraEntity);
    transform.position = position;
    transform.rotation = glm::quatLookAtLH(forward, up);

    const float fov = bookmark->value("fov", camera.fov);
    if (fov > 1.0f && fov < 179.0f) {
        camera.fov = fov;
    }

    m_cameraYaw = std::atan2(forward.x, forward.z);
    m_cameraPitch = std::asin(glm::clamp(forward.y, -1.0f, 1.0f));
    const float pitchLimit = glm::radians(89.0f);
    m_cameraPitch = glm::clamp(m_cameraPitch, -pitchLimit, pitchLimit);
    m_cameraVelocity = glm::vec3(0.0f);
    m_autoDemoEnabled = false;
    m_activeCameraBookmark = bookmarkId;

    spdlog::info("Applied showcase camera bookmark '{}:{}' from '{}'",
                 sceneId, bookmarkId, loadedPath.string());
    return true;
}

bool Engine::ComputeCameraRayFromMouse(float mouseX, float mouseY,
                                       glm::vec3& outOrigin,
                                       glm::vec3& outDirection) {
    if (!m_window || !m_registry) {
        return false;
    }

    float width = static_cast<float>(m_window->GetWidth());
    float height = static_cast<float>(m_window->GetHeight());
    if (width <= 0.0f || height <= 0.0f) {
        return false;
    }

    Scene::TransformComponent* camTransform = nullptr;
    Scene::CameraComponent* camComp = nullptr;

    // Prefer the cached active camera entity when valid; fall back to a scan
    // to recover if the active flag has changed.
    if (m_activeCameraEntity != entt::null &&
        m_registry->HasComponent<Scene::TransformComponent>(m_activeCameraEntity) &&
        m_registry->HasComponent<Scene::CameraComponent>(m_activeCameraEntity)) {
        camTransform = &m_registry->GetComponent<Scene::TransformComponent>(m_activeCameraEntity);
        camComp = &m_registry->GetComponent<Scene::CameraComponent>(m_activeCameraEntity);
    } else {
        auto camView = m_registry->View<Scene::CameraComponent, Scene::TransformComponent>();
        for (auto entity : camView) {
            auto& camera = camView.get<Scene::CameraComponent>(entity);
            auto& transform = camView.get<Scene::TransformComponent>(entity);
            if (camera.isActive) {
                camComp = &camera;
                camTransform = &transform;
                m_activeCameraEntity = entity;
                break;
            }
        }
    }

    if (!camTransform || !camComp) {
        return false;
    }

    glm::mat4 view = camComp->GetViewMatrix(*camTransform);
    glm::mat4 proj = camComp->GetProjectionMatrix(m_window->GetAspectRatio());
    glm::mat4 invViewProj = glm::inverse(proj * view);

    float ndcX = (2.0f * (mouseX / width)) - 1.0f;
    float ndcY = 1.0f - (2.0f * (mouseY / height));

    glm::vec4 nearClip(ndcX, ndcY, 0.0f, 1.0f); // LH_ZO: z=0 near
    glm::vec4 farClip(ndcX, ndcY, 1.0f, 1.0f);  // z=1 far

    glm::vec4 nearWorld = invViewProj * nearClip;
    glm::vec4 farWorld = invViewProj * farClip;
    if (std::abs(nearWorld.w) > 1e-6f) nearWorld /= nearWorld.w;
    if (std::abs(farWorld.w) > 1e-6f) farWorld /= farWorld.w;

    glm::vec3 pNear = glm::vec3(nearWorld);
    glm::vec3 pFar = glm::vec3(farWorld);

    outOrigin = camTransform->position;
    outDirection = glm::normalize(pFar - outOrigin);
    return glm::length2(outDirection) > 0.0f;
}

entt::entity Engine::PickEntityAt(float mouseX, float mouseY) {
    if (!m_registry) {
        return entt::null;
    }

    glm::vec3 rayOrigin, rayDir;
    if (!ComputeCameraRayFromMouse(mouseX, mouseY, rayOrigin, rayDir)) {
        return entt::null;
    }

    auto view = m_registry->View<Scene::TransformComponent, Scene::RenderableComponent>();
    const glm::vec3 aabbMin(-0.5f);
    const glm::vec3 aabbMax(0.5f);

    float bestDist = std::numeric_limits<float>::max();
    entt::entity best = entt::null;

    for (auto entity : view) {
        auto& transform = view.get<Scene::TransformComponent>(entity);
        auto& renderable = view.get<Scene::RenderableComponent>(entity);
        if (!renderable.visible) continue;

        glm::mat4 world = transform.worldMatrix;
        glm::mat4 invWorld = transform.inverseWorldMatrix;
        glm::vec3 localOrigin = glm::vec3(invWorld * glm::vec4(rayOrigin, 1.0f));
        glm::vec3 localDir = glm::vec3(invWorld * glm::vec4(rayDir, 0.0f));
        if (glm::length2(localDir) < 1e-6f) continue;
        localDir = glm::normalize(localDir);

        float tLocal = 0.0f;
        if (!RayIntersectsAABB(localOrigin, localDir, aabbMin, aabbMax, tLocal)) continue;
        if (tLocal < 0.0f) continue;

        glm::vec3 hitLocal = localOrigin + localDir * tLocal;
        glm::vec3 hitWorld = glm::vec3(world * glm::vec4(hitLocal, 1.0f));
        float dist = glm::length(hitWorld - rayOrigin);
        if (dist < bestDist) {
            bestDist = dist;
            best = entity;
        }
    }

    if (best != entt::null && m_registry->HasComponent<Scene::TagComponent>(best)) {
        const auto& tag = m_registry->GetComponent<Scene::TagComponent>(best);
        spdlog::info("Picked entity '{}' (id={})", tag.tag,
                     static_cast<uint32_t>(entt::to_integral(best)));
    } else if (best == entt::null) {
        spdlog::info("Pick miss (no entity under cursor)");
    }

    return best;
}

void Engine::FrameSelectedEntity() {
    if (!m_registry) return;
    if (m_selectedEntity == entt::null) return;

    if (!m_registry->HasComponent<Scene::TransformComponent>(m_selectedEntity)) {
        return;
    }

    // Find active camera
    Scene::TransformComponent* camTransform = nullptr;
    Scene::CameraComponent* camComp = nullptr;
    auto camView = m_registry->View<Scene::CameraComponent, Scene::TransformComponent>();
    for (auto entity : camView) {
        auto& camera = camView.get<Scene::CameraComponent>(entity);
        auto& transform = camView.get<Scene::TransformComponent>(entity);
        if (camera.isActive) {
            camComp = &camera;
            camTransform = &transform;
            m_activeCameraEntity = entity;
            break;
        }
    }
    if (!camTransform || !camComp) return;

    const auto& selTransform = m_registry->GetComponent<Scene::TransformComponent>(m_selectedEntity);

    // Build a world-space bounding sphere from the mesh if available; fall back
    // to a simple scale-based heuristic otherwise.
    glm::vec3 focus = glm::vec3(selTransform.worldMatrix[3]);
    float radius = 0.5f;

    if (m_registry->HasComponent<Scene::RenderableComponent>(m_selectedEntity)) {
        const auto& renderable = m_registry->GetComponent<Scene::RenderableComponent>(m_selectedEntity);
        if (renderable.mesh && !renderable.mesh->positions.empty()) {
            glm::vec3 localMin(std::numeric_limits<float>::max());
            glm::vec3 localMax(-std::numeric_limits<float>::max());
            for (const auto& p : renderable.mesh->positions) {
                localMin = glm::min(localMin, p);
                localMax = glm::max(localMax, p);
            }

            glm::vec3 localCorners[8] = {
                glm::vec3(localMin.x, localMin.y, localMin.z),
                glm::vec3(localMax.x, localMin.y, localMin.z),
                glm::vec3(localMax.x, localMax.y, localMin.z),
                glm::vec3(localMin.x, localMax.y, localMin.z),
                glm::vec3(localMin.x, localMin.y, localMax.z),
                glm::vec3(localMax.x, localMin.y, localMax.z),
                glm::vec3(localMax.x, localMax.y, localMax.z),
                glm::vec3(localMin.x, localMax.y, localMax.z)
            };

            glm::vec3 worldMin(std::numeric_limits<float>::max());
            glm::vec3 worldMax(-std::numeric_limits<float>::max());
            for (const auto& c : localCorners) {
                glm::vec3 wc = glm::vec3(selTransform.worldMatrix * glm::vec4(c, 1.0f));
                worldMin = glm::min(worldMin, wc);
                worldMax = glm::max(worldMax, wc);
            }

            focus = (worldMin + worldMax) * 0.5f;
            glm::vec3 extents = (worldMax - worldMin) * 0.5f;
            radius = glm::length(extents);
        }
    }

    if (radius < 0.5f) {
        glm::vec3 absScale = glm::abs(selTransform.scale);
        radius = std::max({absScale.x, absScale.y, absScale.z}) * 0.5f;
        if (radius < 0.5f) radius = 0.5f;
    }

    float fovRad = glm::radians(camComp->fov);
    float distance = radius / std::max(std::sin(fovRad * 0.5f), 0.1f);
    distance = std::clamp(distance, camComp->nearPlane + radius, camComp->farPlane * 0.5f);

    // Position camera behind current view direction looking at focus
    glm::vec3 forward = glm::normalize(focus - camTransform->position);
    if (glm::length2(forward) < 1e-6f) {
        forward = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    // If forward is nearly parallel to worldUp, choose an alternate up vector
    // to avoid degeneracy in the cross product.
    if (std::abs(glm::dot(forward, worldUp)) > 0.98f) {
        worldUp = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    glm::vec3 right = glm::cross(forward, worldUp);
    if (glm::length2(right) < 1e-6f) {
        right = glm::vec3(1.0f, 0.0f, 0.0f);
    } else {
        right = glm::normalize(right);
    }
    glm::vec3 up = glm::normalize(glm::cross(right, forward));

    camTransform->position = focus - forward * distance;
    camTransform->rotation = glm::quatLookAtLH(forward, up);

    // Update yaw/pitch to keep flycam in sync.
    forward = glm::normalize(forward);
    m_cameraYaw = std::atan2(forward.x, forward.z);
    m_cameraPitch = std::asin(glm::clamp(forward.y, -1.0f, 1.0f));

    // Update logical focus target so LLM/Dreamer edits apply to this object.
    if (m_registry->HasComponent<Scene::TagComponent>(m_selectedEntity)) {
        const auto& tag = m_registry->GetComponent<Scene::TagComponent>(m_selectedEntity);
        SetFocusTarget(tag.tag);
    }

    spdlog::info("Framed entity (distance ~{}, fov={} deg)",
                 distance, camComp->fov);
}

bool Engine::HitTestGizmoAxis(const glm::vec3& rayOrigin,
                              const glm::vec3& rayDir,
                              const glm::vec3& center,
                              const glm::vec3 axes[3],
                              float axisLength,
                              float threshold,
                              GizmoAxis& outAxis) {
    float bestT = std::numeric_limits<float>::max();
    GizmoAxis best = GizmoAxis::None;

    for (int i = 0; i < 3; ++i) {
        if (glm::length2(axes[i]) < 1e-6f) {
            continue;
        }
        float tRay = 0.0f;
        if (RayHitsAxis(rayOrigin, rayDir, center, axes[i], axisLength, threshold, tRay)) {
            if (tRay < bestT) {
                bestT = tRay;
                best = (i == 0 ? GizmoAxis::X : (i == 1 ? GizmoAxis::Y : GizmoAxis::Z));
            }
        }
    }

    outAxis = best;
    return best != GizmoAxis::None;
}

void Engine::UpdateGizmoHover() {
    m_gizmoHoveredAxis = GizmoAxis::None;

    // While dragging we keep the active axis locked and skip hover tests.
    if (m_gizmoDragging) {
        return;
    }

    // Only update hover if gizmos are visible
    if (!m_showGizmos) {
        return;
    }

    if (!m_window || !m_registry || m_selectedEntity == entt::null) {
        return;
    }

    glm::vec3 rayOrigin, rayDir;
    if (!ComputeCameraRayFromMouse(m_lastMousePos.x, m_lastMousePos.y, rayOrigin, rayDir)) {
        return;
    }

    auto& reg = m_registry->GetRegistry();
    if (!reg.valid(m_selectedEntity) ||
        !reg.all_of<Scene::TransformComponent>(m_selectedEntity)) {
        return;
    }

    const auto& t = reg.get<Scene::TransformComponent>(m_selectedEntity);
    glm::vec3 center = glm::vec3(t.worldMatrix[3]);

    glm::vec3 axisWorld[3] = {
        glm::vec3(t.worldMatrix * glm::vec4(1,0,0,0)),
        glm::vec3(t.worldMatrix * glm::vec4(0,1,0,0)),
        glm::vec3(t.worldMatrix * glm::vec4(0,0,1,0))
    };

    glm::vec3 axes[3];
    for (int i = 0; i < 3; ++i) {
        float len2 = glm::length2(axisWorld[i]);
        axes[i] = (len2 > 1e-6f) ? (axisWorld[i] / std::sqrt(len2))
                                 : glm::vec3(0.0f);
    }

    float axisLength = 1.0f;
    float threshold = 0.15f;
    float distance = glm::length(center - rayOrigin);
    ComputeGizmoScale(distance, axisLength, threshold);

    GizmoAxis axis = GizmoAxis::None;
    HitTestGizmoAxis(rayOrigin, rayDir, center, axes, axisLength, threshold, axis);
    m_gizmoHoveredAxis = axis;
}

void Engine::DebugDrawSceneGraph() {
    if (!m_renderer || !m_registry) {
        return;
    }

    // Clear any lines generated in previous frame.
    m_renderer->ClearDebugLines();

    // World origin axes (toggled with H key along with gizmos)
    if (m_showOriginAxes) {
        const glm::vec3 origin(0.0f);
        m_renderer->AddDebugLine(origin, origin + glm::vec3(1,0,0), glm::vec4(1,0,0,1));
        m_renderer->AddDebugLine(origin, origin + glm::vec3(0,1,0), glm::vec4(0,1,0,1));
        m_renderer->AddDebugLine(origin, origin + glm::vec3(0,0,1), glm::vec4(0,0,1,1));
    }

    auto& reg = m_registry->GetRegistry();
    auto view = m_registry->View<Scene::TransformComponent>();

    // Selection highlight (simple wireframe box in world space).
    if (m_selectedEntity != entt::null &&
        reg.valid(m_selectedEntity) &&
        reg.all_of<Scene::TransformComponent>(m_selectedEntity)) {
        const auto& selT = reg.get<Scene::TransformComponent>(m_selectedEntity);
        glm::vec3 c = glm::vec3(selT.worldMatrix[3]);

        // Use a unit cube in local space and transform it into world space so
        // the box respects hierarchy and rotation.
        glm::vec3 localCorners[8] = {
            glm::vec3(-0.5f, -0.5f, -0.5f),
            glm::vec3( 0.5f, -0.5f, -0.5f),
            glm::vec3( 0.5f,  0.5f, -0.5f),
            glm::vec3(-0.5f,  0.5f, -0.5f),
            glm::vec3(-0.5f, -0.5f,  0.5f),
            glm::vec3( 0.5f, -0.5f,  0.5f),
            glm::vec3( 0.5f,  0.5f,  0.5f),
            glm::vec3(-0.5f,  0.5f,  0.5f),
        };

        glm::vec3 corners[8];
        for (int i = 0; i < 8; ++i) {
            corners[i] = glm::vec3(selT.worldMatrix * glm::vec4(localCorners[i], 1.0f));
        }

        auto edge = [&](int a, int b) {
            m_renderer->AddDebugLine(corners[a], corners[b],
                                     glm::vec4(1.0f, 1.0f, 0.0f, 0.9f));
        };

        // Bottom
        edge(0,1); edge(1,2); edge(2,3); edge(3,0);
        // Top
        edge(4,5); edge(5,6); edge(6,7); edge(7,4);
        // Vertical
        edge(0,4); edge(1,5); edge(2,6); edge(3,7);

        // Translation gizmo centered at c, using object-space axes in world space.
        // Only draw if gizmos are enabled (toggle with H key)
        if (m_showGizmos) {
        glm::vec3 axisX = glm::vec3(selT.worldMatrix * glm::vec4(1,0,0,0));
        glm::vec3 axisY = glm::vec3(selT.worldMatrix * glm::vec4(0,1,0,0));
        glm::vec3 axisZ = glm::vec3(selT.worldMatrix * glm::vec4(0,0,1,0));

        float len = 1.0f;
        float dummyThreshold = 0.0f;
        float camDistance = len;
        if (m_activeCameraEntity != entt::null &&
            reg.valid(m_activeCameraEntity) &&
            reg.all_of<Scene::TransformComponent, Scene::CameraComponent>(m_activeCameraEntity)) {
            const auto& camT = reg.get<Scene::TransformComponent>(m_activeCameraEntity);
            camDistance = glm::length(c - camT.position);
        }
        ComputeGizmoScale(camDistance, len, dummyThreshold);

        auto safeNormalize = [](const glm::vec3& v) {
            float l2 = glm::length2(v);
            return (l2 > 1e-6f) ? (v / std::sqrt(l2)) : glm::vec3(0.0f);
        };

        axisX = safeNormalize(axisX);
        axisY = safeNormalize(axisY);
        axisZ = safeNormalize(axisZ);

        auto colorForAxis = [&](GizmoAxis axis, const glm::vec3& base) {
            if (m_gizmoActiveAxis == axis || m_gizmoHoveredAxis == axis) {
                return glm::vec4(1.0f);
            }
            return glm::vec4(base, 1.0f);
        };

        float axisThickness = len * 0.02f;

        glm::vec4 xColor = colorForAxis(GizmoAxis::X, {1,0,0});
        glm::vec4 yColor = colorForAxis(GizmoAxis::Y, {0,1,0});
        glm::vec4 zColor = colorForAxis(GizmoAxis::Z, {0,0,1});

        glm::vec3 xTip = c + axisX * len;
        glm::vec3 xOffset = (axisY + axisZ) * (0.5f * axisThickness);
        m_renderer->AddDebugLine(c, xTip, xColor);
        m_renderer->AddDebugLine(c + xOffset, xTip + xOffset, xColor);
        m_renderer->AddDebugLine(c - xOffset, xTip - xOffset, xColor);
        m_renderer->AddDebugLine(xTip - axisY*0.05f, xTip + axisY*0.05f, xColor);

        glm::vec3 yTip = c + axisY * len;
        glm::vec3 yOffset = (axisZ + axisX) * (0.5f * axisThickness);
        m_renderer->AddDebugLine(c, yTip, yColor);
        m_renderer->AddDebugLine(c + yOffset, yTip + yOffset, yColor);
        m_renderer->AddDebugLine(c - yOffset, yTip - yOffset, yColor);
        m_renderer->AddDebugLine(yTip - axisZ*0.05f, yTip + axisZ*0.05f, yColor);

        glm::vec3 zTip = c + axisZ * len;
        glm::vec3 zOffset = (axisX + axisY) * (0.5f * axisThickness);
        m_renderer->AddDebugLine(c, zTip, zColor);
        m_renderer->AddDebugLine(c + zOffset, zTip + zOffset, zColor);
        m_renderer->AddDebugLine(c - zOffset, zTip - zOffset, zColor);
        m_renderer->AddDebugLine(zTip - axisX*0.05f, zTip + axisX*0.05f, zColor);
        }  // if (m_showGizmos)
    }
}

void Engine::InitializeCameraController() {
    if (!m_registry) {
        return;
    }

    m_activeCameraEntity = entt::null;
    m_cameraControllerInitialized = false;
    m_cameraControlActive = false;
    m_pendingMouseDeltaX = 0.0f;
    m_pendingMouseDeltaY = 0.0f;
    m_cameraVelocity = glm::vec3(0.0f);
    m_cameraRoll = 0.0f;

    // Find active camera
    auto cameraView = m_registry->View<Scene::CameraComponent, Scene::TransformComponent>();
    for (auto entity : cameraView) {
        auto& camera = cameraView.get<Scene::CameraComponent>(entity);
        if (camera.isActive) {
            m_activeCameraEntity = entity;
            break;
        }
    }

    if (m_activeCameraEntity == entt::null) {
        spdlog::warn("InitializeCameraController: no active camera found");
        return;
    }

    auto& transform = m_registry->GetComponent<Scene::TransformComponent>(m_activeCameraEntity);

    // Reset to the default position/orientation for the current scene preset
    // and derive yaw/pitch from the resulting forward vector.
    SetCameraToSceneDefault(transform);
    m_activeCameraBookmark.clear();

    m_cameraControllerInitialized = true;
}

void Engine::UpdateCameraController(float deltaTime) {
    if (!m_cameraControllerInitialized || !m_registry) {
        return;
    }

    // When the auto-demo is active, camera motion is driven by UpdateAutoDemo
    // so manual input is ignored for the duration of the scripted flythrough.
    if (m_autoDemoEnabled) {
        m_pendingMouseDeltaX = 0.0f;
        m_pendingMouseDeltaY = 0.0f;
        m_cameraVelocity = glm::vec3(0.0f);
        return;
    }

    if (m_activeCameraEntity == entt::null ||
        !m_registry->HasComponent<Scene::TransformComponent>(m_activeCameraEntity) ||
        !m_registry->HasComponent<Scene::CameraComponent>(m_activeCameraEntity)) {
        m_cameraControllerInitialized = false;
        return;
    }

    auto& transform = m_registry->GetComponent<Scene::TransformComponent>(m_activeCameraEntity);

    // Apply mouse look deltas (yaw/pitch) from accumulated motion.
    // Process in both editor camera control mode and play mode.
    if (m_cameraControlActive || m_playModeActive) {
        float dx = m_pendingMouseDeltaX;
        float dy = m_pendingMouseDeltaY;
        m_pendingMouseDeltaX = 0.0f;
        m_pendingMouseDeltaY = 0.0f;

        m_cameraYaw += dx * m_mouseSensitivity;
        // Invert Y so that moving the mouse up pitches the camera down and
        // moving it down pitches up, matching the requested flight-style
        // controls.
        m_cameraPitch += dy * m_mouseSensitivity;

        float pitchLimit = glm::radians(89.0f);
        m_cameraPitch = glm::clamp(m_cameraPitch, -pitchLimit, pitchLimit);
    } else {
        m_pendingMouseDeltaX = 0.0f;
        m_pendingMouseDeltaY = 0.0f;
    }

    // Build camera basis from yaw/pitch
    float cosPitch = std::cos(m_cameraPitch);
    glm::vec3 forward(
        std::sin(m_cameraYaw) * cosPitch,
        std::sin(m_cameraPitch),
        std::cos(m_cameraYaw) * cosPitch
    );
    forward = glm::normalize(forward);

    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
    glm::vec3 up = glm::normalize(glm::cross(right, forward));

    // Optional roll for drone-style banking, only in drone mode.
    if (m_droneFlightEnabled) {
        int numKeys = 0;
        const bool* keys = SDL_GetKeyboardState(&numKeys);
        auto keyDown = [&](SDL_Scancode scancode) {
            return scancode >= 0 && scancode < numKeys && keys[scancode];
        };

        float rollInput = 0.0f;
        // Q/E control roll in drone mode; vertical thrust is Space/Ctrl.
        if (keyDown(SDL_SCANCODE_Q)) rollInput -= 1.0f;
        if (keyDown(SDL_SCANCODE_E)) rollInput += 1.0f;

        if (std::abs(rollInput) > 0.0f) {
            m_cameraRoll += rollInput * m_cameraRollSpeed * deltaTime;
        } else {
            // Exponential decay back toward level horizon when no roll input.
            float decay = std::exp(-m_cameraRollDamping * deltaTime);
            m_cameraRoll *= decay;
        }

        // Clamp roll to a reasonable banking range.
        float maxRoll = glm::radians(75.0f);
        m_cameraRoll = glm::clamp(m_cameraRoll, -maxRoll, maxRoll);

        if (std::abs(m_cameraRoll) > 1e-4f) {
            glm::quat rollQuat = glm::angleAxis(m_cameraRoll, forward);
            right = rollQuat * right;
            up = rollQuat * up;
        }
    }

    // Keyboard movement (WASD, vertical) in camera-local axes
    if (m_cameraControlActive) {
        int numKeys = 0;
        const bool* keys = SDL_GetKeyboardState(&numKeys);
        auto keyDown = [&](SDL_Scancode scancode) {
            return scancode >= 0 && scancode < numKeys && keys[scancode];
        };

        glm::vec3 moveDir(0.0f);
        // Standard FPS controls: WASD for horizontal/forward movement
        if (keyDown(SDL_SCANCODE_S)) moveDir += forward;  // W = forward
        if (keyDown(SDL_SCANCODE_W)) moveDir -= forward;  // S = backward
        if (keyDown(SDL_SCANCODE_D)) moveDir += right;    // D = right
        if (keyDown(SDL_SCANCODE_A)) moveDir -= right;    // A = left

        // Space for up, Ctrl for down (vertical movement)
        if (keyDown(SDL_SCANCODE_SPACE)) moveDir += up;   // Space = up
        if (keyDown(SDL_SCANCODE_LCTRL) || keyDown(SDL_SCANCODE_RCTRL)) {
            moveDir -= up;  // Ctrl = down
        }

        if (m_droneFlightEnabled) {

            // Auto-forward cruise: when no explicit movement keys are pressed,
            // keep the camera gliding forward for fast, fluid traversal.
            const bool hasDirectionalInput =
                keyDown(SDL_SCANCODE_W) || keyDown(SDL_SCANCODE_S) ||
                keyDown(SDL_SCANCODE_A) || keyDown(SDL_SCANCODE_D) ||
                keyDown(SDL_SCANCODE_SPACE) ||
                keyDown(SDL_SCANCODE_LCTRL) || keyDown(SDL_SCANCODE_RCTRL);
            if (!hasDirectionalInput) {
                moveDir += forward;
            }
        } else {
            // Legacy non-drone mode keeps Q/E as vertical movement.
            if (keyDown(SDL_SCANCODE_E) || keyDown(SDL_SCANCODE_SPACE)) moveDir += up;
            if (keyDown(SDL_SCANCODE_Q) ||
                keyDown(SDL_SCANCODE_LCTRL) ||
                keyDown(SDL_SCANCODE_RCTRL)) moveDir -= up;
        }

        if (!m_droneFlightEnabled) {
            // Classic immediate flycam for non-drone mode.
            if (glm::length(moveDir) > 0.0f) {
                float speed = m_cameraBaseSpeed;
                if (keyDown(SDL_SCANCODE_LSHIFT) || keyDown(SDL_SCANCODE_RSHIFT)) {
                    speed *= m_cameraSprintMultiplier;
                }
                moveDir = glm::normalize(moveDir) * speed * deltaTime;
                transform.position += moveDir;
            }
        } else {
            // Drone/free-flight mode: velocity-based movement with acceleration and damping.
            // Apply exponential damping so the camera coasts and then gently comes to rest.
            float damping = std::max(0.0f, m_cameraDamping);
            if (damping > 0.0f) {
                float decay = std::exp(-damping * deltaTime);
                m_cameraVelocity *= decay;
            }

            glm::vec3 accel(0.0f);
            if (glm::length(moveDir) > 0.0f) {
                glm::vec3 dir = glm::normalize(moveDir);
                float thrust = m_cameraAcceleration * m_cameraBaseSpeed;
                bool sprint = keyDown(SDL_SCANCODE_LSHIFT) || keyDown(SDL_SCANCODE_RSHIFT);
                if (sprint) {
                    thrust *= m_cameraSprintMultiplier;
                }
                accel = dir * thrust;
            }

            m_cameraVelocity += accel * deltaTime;

            // Clamp velocity magnitude to a maximum cruise speed derived from base speed.
            float maxSpeed = m_cameraMaxSpeed;
            bool sprinting = keyDown(SDL_SCANCODE_LSHIFT) || keyDown(SDL_SCANCODE_RSHIFT);
            if (sprinting) {
                maxSpeed *= m_cameraSprintMultiplier;
            }
            float vLen = glm::length(m_cameraVelocity);
            if (vLen > maxSpeed && vLen > 1e-4f) {
                m_cameraVelocity = (m_cameraVelocity / vLen) * maxSpeed;
            }

            transform.position += m_cameraVelocity * deltaTime;
        }
    } else {
        // When camera control is inactive, keep motion state reset.
        m_cameraVelocity = glm::vec3(0.0f);
        m_cameraRoll = 0.0f;
    }

    // Update camera rotation from forward/up (including any roll).
    transform.rotation = glm::quatLookAtLH(glm::normalize(forward), up);
}

void Engine::UpdateAutoDemo(float deltaTime) {
    if (!m_autoDemoEnabled || !m_registry) {
        return;
    }

    if (m_activeCameraEntity == entt::null ||
        !m_registry->HasComponent<Scene::TransformComponent>(m_activeCameraEntity) ||
        !m_registry->HasComponent<Scene::CameraComponent>(m_activeCameraEntity)) {
        return;
    }

    m_autoDemoTime += deltaTime;

    auto& transform = m_registry->GetComponent<Scene::TransformComponent>(m_activeCameraEntity);

    // Simple orbital camera path around the hero pool.
    const float orbitRadius = 8.0f;
    const float orbitHeight = 3.0f;
    const glm::vec3 center(0.0f, 1.0f, kHeroPoolZ);

    float angle = m_autoDemoTime * 0.35f; // radians per second
    float yOffset = 0.5f * std::sin(m_autoDemoTime * 0.5f);

    transform.position = glm::vec3(
        orbitRadius * std::sin(angle),
        orbitHeight + yOffset,
        center.z - orbitRadius * std::cos(angle));

    glm::vec3 forward = glm::normalize(center - transform.position);
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(forward, up)) > 0.99f) {
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    transform.rotation = glm::quatLookAtLH(forward, up);

    // Keep yaw/pitch consistent with the scripted forward vector so that when
    // auto-demo is disabled, manual controls resume from a sensible state.
    forward = glm::normalize(forward);
    m_cameraYaw = std::atan2(forward.x, forward.z);
    m_cameraPitch = std::asin(glm::clamp(forward.y, -1.0f, 1.0f));

    if (m_renderer) {
        Graphics::ApplyAutoDemoFeatureLock(*m_renderer);
        SyncDebugMenuFromRenderer();
    }
}

} // namespace Cortex
