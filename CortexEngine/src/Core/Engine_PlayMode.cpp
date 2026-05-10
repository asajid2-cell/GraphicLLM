#include "Engine.h"
#include "EngineEditorMode.h"
#include "Graphics/Renderer.h"
#include "Scene/Components.h"
#include "Scene/TerrainNoise.h"
#include "Utils/MeshGenerator.h"
#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <vector>

namespace Cortex {

// =============================================================================
// Play Mode and Terrain System (appended - does not modify existing code)
// =============================================================================

void Engine::EnterPlayMode() {
    if (m_playModeActive) return;

    m_playModeActive = true;
    m_engineMode = EngineMode::Play;

    // Initialize interaction system
    if (m_registry) {
        m_interactionSystem.Initialize(m_registry.get());
        m_interactionSystem.SetTerrainParams(m_terrainParams, m_terrainEnabled);
    }

    // Capture mouse for FPS controls
    if (m_window && m_window->GetSDLWindow()) {
        SDL_SetWindowRelativeMouseMode(m_window->GetSDLWindow(), true);
    }

    // Get starting position from EditorCamera if EditorWorld is active
    glm::vec3 startPos(0.0f, 50.0f, 0.0f);
    if (m_editorModeController && m_editorModeController->IsInitialized()) {
        startPos = m_editorModeController->GetCameraPosition();
    }

    // Initialize player at camera position
    if (m_registry && m_activeCameraEntity != entt::null &&
        m_registry->GetRegistry().valid(m_activeCameraEntity) &&
        m_registry->HasComponent<Scene::TransformComponent>(m_activeCameraEntity)) {
        auto& camTransform = m_registry->GetComponent<Scene::TransformComponent>(m_activeCameraEntity);

        // Use EditorCamera position if available
        if (m_editorModeController && m_editorModeController->IsInitialized()) {
            camTransform.position = startPos;
        }

        // If terrain is enabled, place player on ground
        if (m_terrainEnabled) {
            float groundY = Scene::SampleTerrainHeight(
                static_cast<double>(camTransform.position.x),
                static_cast<double>(camTransform.position.z),
                m_terrainParams);
            camTransform.position.y = groundY + m_playerEyeHeight;
        }
    }

    // Initialize camera controller if needed
    if (!m_cameraControllerInitialized) {
        InitializeCameraController();
    }

    // Override yaw/pitch from EditorCamera AFTER InitializeCameraController
    // (since SetCameraToSceneDefault in InitializeCameraController sets defaults)
    if (m_editorModeController && m_editorModeController->IsInitialized()) {
        m_cameraYaw = m_editorModeController->GetCameraYaw();
        m_cameraPitch = m_editorModeController->GetCameraPitch();

        // Also update camera transform rotation to match
        if (m_registry && m_activeCameraEntity != entt::null &&
            m_registry->GetRegistry().valid(m_activeCameraEntity) &&
            m_registry->HasComponent<Scene::TransformComponent>(m_activeCameraEntity)) {
            auto& camTransform = m_registry->GetComponent<Scene::TransformComponent>(m_activeCameraEntity);
            float cosPitch = std::cos(m_cameraPitch);
            glm::vec3 forward(
                std::sin(m_cameraYaw) * cosPitch,
                std::sin(m_cameraPitch),
                std::cos(m_cameraYaw) * cosPitch
            );
            forward = glm::normalize(forward);
            glm::vec3 up(0.0f, 1.0f, 0.0f);
            if (std::abs(glm::dot(forward, up)) > 0.99f) {
                up = glm::vec3(0.0f, 0.0f, 1.0f);
            }
            camTransform.rotation = glm::quatLookAtLH(forward, up);
        }
    }

    m_playerVelocity = glm::vec3(0.0f);
    m_playerGrounded = false;

    spdlog::info("Entered Play Mode (F5) - camera={} terrain={}",
        m_activeCameraEntity != entt::null ? "valid" : "null",
        m_terrainEnabled ? "enabled" : "disabled");
}

void Engine::ExitPlayMode() {
    if (!m_playModeActive) return;

    m_playModeActive = false;
    m_engineMode = EngineMode::Editor;

    // Release mouse
    if (m_window && m_window->GetSDLWindow()) {
        SDL_SetWindowRelativeMouseMode(m_window->GetSDLWindow(), false);
    }

    spdlog::info("Exited Play Mode (F5)");
}

glm::vec3 Engine::GetActiveCameraPosition() const {
    if (m_registry && m_activeCameraEntity != entt::null &&
        m_registry->GetRegistry().valid(m_activeCameraEntity) &&
        m_registry->HasComponent<Scene::TransformComponent>(m_activeCameraEntity)) {
        return m_registry->GetComponent<Scene::TransformComponent>(m_activeCameraEntity).position;
    }
    return glm::vec3(0.0f, 50.0f, 0.0f);  // Default fallback
}

// =============================================================================
// WorldState Implementation (Time-of-Day System)
// =============================================================================

void Engine::WorldState::Update(float deltaTime) {
    if (!timePaused) {
        // Advance time: timeScale of 60 means 1 real second = 1 game minute
        timeOfDay += deltaTime * timeScale / 3600.0f;
        if (timeOfDay >= 24.0f) timeOfDay -= 24.0f;
        if (timeOfDay < 0.0f) timeOfDay += 24.0f;
    }

    dayProgress = timeOfDay / 24.0f;

    // Sun angle calculation:
    // 6h = sunrise (sun at horizon, angle = 0)
    // 12h = noon (sun at zenith, angle = 90°)
    // 18h = sunset (sun at horizon, angle = 180°)
    // 0h/24h = midnight (sun below horizon, angle = -90°)
    float sunAngle = (timeOfDay - 6.0f) / 12.0f * 3.14159265f;
    float altitude = glm::sin(sunAngle);
    float azimuth = glm::cos(sunAngle * 0.5f);

    // Sun direction: rises in east (+X), sets in west (-X), travels through south (+Z)
    sunDirection = glm::normalize(glm::vec3(
        azimuth * 0.6f,                    // East-West component
        glm::max(altitude, -0.2f),         // Altitude (clamped to prevent fully underground)
        glm::abs(azimuth) * 0.4f           // North-South component (sun travels through south)
    ));

    // Sun color: warm orange at horizon, white at noon
    float horizonFactor = 1.0f - glm::abs(altitude);
    sunColor = glm::mix(
        glm::vec3(1.0f, 0.95f, 0.9f),      // White noon
        glm::vec3(1.0f, 0.5f, 0.2f),       // Orange horizon (sunrise/sunset)
        horizonFactor * horizonFactor       // Squared for more dramatic effect
    );

    // Sun intensity: peaks at noon (~10), provides moonlight at night (~0.3-0.5)
    float dayIntensity = glm::max(0.0f, altitude) * 10.0f;
    float nightIntensity = glm::max(0.0f, -altitude) * 0.3f;  // Moonlight
    sunIntensity = glm::max(dayIntensity, nightIntensity + 0.2f);  // Minimum ambient
}

void Engine::WorldState::AdvanceTime(float hours) {
    timeOfDay += hours;
    if (timeOfDay >= 24.0f) timeOfDay -= 24.0f;
    if (timeOfDay < 0.0f) timeOfDay += 24.0f;
}

void Engine::WorldState::SetTime(float hour) {
    timeOfDay = glm::clamp(hour, 0.0f, 23.999f);
}

void Engine::UpdatePlayMode(float deltaTime) {
    if (!m_playModeActive || !m_registry) return;

    // Get camera for interaction raycasting
    glm::vec3 cameraPos(0.0f);
    glm::vec3 cameraForward(0.0f, 0.0f, 1.0f);

    if (m_activeCameraEntity != entt::null &&
        m_registry->GetRegistry().valid(m_activeCameraEntity) &&
        m_registry->HasComponent<Scene::TransformComponent>(m_activeCameraEntity)) {
        auto& camTransform = m_registry->GetComponent<Scene::TransformComponent>(m_activeCameraEntity);
        cameraPos = camTransform.position;
        cameraForward = camTransform.rotation * glm::vec3(0.0f, 0.0f, 1.0f);
    }

    // Update interaction system
    m_interactionSystem.Update(cameraPos, cameraForward, deltaTime);

    // FPS movement with terrain collision
    if (m_terrainEnabled && m_activeCameraEntity != entt::null &&
        m_registry->GetRegistry().valid(m_activeCameraEntity) &&
        m_registry->HasComponent<Scene::TransformComponent>(m_activeCameraEntity)) {
        auto& camTransform = m_registry->GetComponent<Scene::TransformComponent>(m_activeCameraEntity);

        const float gravity = 20.0f;
        const float jumpVelocity = 8.0f;

        // Apply gravity
        if (!m_playerGrounded) {
            m_playerVelocity.y -= gravity * deltaTime;
        }

        // Sample terrain height at current position
        float groundY = Scene::SampleTerrainHeight(
            static_cast<double>(camTransform.position.x),
            static_cast<double>(camTransform.position.z),
            m_terrainParams);

        float targetY = groundY + m_playerEyeHeight;

        // Check if on ground
        if (camTransform.position.y <= targetY + 0.1f) {
            m_playerGrounded = true;
            camTransform.position.y = targetY;
            if (m_playerVelocity.y < 0.0f) {
                m_playerVelocity.y = 0.0f;
            }
        } else {
            m_playerGrounded = false;
        }

        // Apply vertical velocity
        camTransform.position.y += m_playerVelocity.y * deltaTime;

        // Clamp to ground
        if (camTransform.position.y < targetY) {
            camTransform.position.y = targetY;
            m_playerVelocity.y = 0.0f;
            m_playerGrounded = true;
        }

        // WASD horizontal movement (relative to camera facing direction)
        const bool* keyState = SDL_GetKeyboardState(nullptr);
        if (keyState) {
            // Get forward and right vectors from camera yaw (ignore pitch for movement)
            glm::vec3 forward(
                std::sin(m_cameraYaw),
                0.0f,
                std::cos(m_cameraYaw)
            );
            forward = glm::normalize(forward);
            glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));

            glm::vec3 moveDir(0.0f);
            // NOTE: W/S are swapped to match the inverted controls in UpdateCameraController
            if (keyState[SDL_SCANCODE_W]) moveDir -= forward;  // W = forward
            if (keyState[SDL_SCANCODE_S]) moveDir += forward;  // S = backward
            if (keyState[SDL_SCANCODE_A]) moveDir -= right;    // A = left
            if (keyState[SDL_SCANCODE_D]) moveDir += right;    // D = right

            if (glm::length(moveDir) > 0.0f) {
                moveDir = glm::normalize(moveDir);
                float moveSpeed = 8.0f;  // Base walking speed
                if (keyState[SDL_SCANCODE_LSHIFT] || keyState[SDL_SCANCODE_RSHIFT]) {
                    moveSpeed *= 2.0f;  // Sprint multiplier
                }
                camTransform.position.x += moveDir.x * moveSpeed * deltaTime;
                camTransform.position.z += moveDir.z * moveSpeed * deltaTime;

                // Re-sample terrain height after horizontal movement
                groundY = Scene::SampleTerrainHeight(
                    static_cast<double>(camTransform.position.x),
                    static_cast<double>(camTransform.position.z),
                    m_terrainParams);
                targetY = groundY + m_playerEyeHeight;

                // Snap to ground if grounded and terrain changed
                if (m_playerGrounded) {
                    camTransform.position.y = targetY;
                }
            }

            // Jump input
            if (m_playerGrounded && keyState[SDL_SCANCODE_SPACE]) {
                m_playerVelocity.y = jumpVelocity;
                m_playerGrounded = false;
            }
        }
    }
}


// ============================================================================
// Dynamic Chunk Loading for Infinite Terrain
// ============================================================================

void Engine::UpdateDynamicChunkLoading(const glm::vec3& playerPos) {
    // Calculate which chunk the player is in
    int32_t playerChunkX = static_cast<int32_t>(std::floor(playerPos.x / TERRAIN_CHUNK_SIZE));
    int32_t playerChunkZ = static_cast<int32_t>(std::floor(playerPos.z / TERRAIN_CHUNK_SIZE));

    // Determine which chunks should be loaded (all chunks within CHUNK_LOAD_RADIUS)
    std::unordered_set<ChunkKey, ChunkKeyHash> desiredChunks;
    for (int32_t dz = -CHUNK_LOAD_RADIUS; dz <= CHUNK_LOAD_RADIUS; ++dz) {
        for (int32_t dx = -CHUNK_LOAD_RADIUS; dx <= CHUNK_LOAD_RADIUS; ++dx) {
            desiredChunks.insert({playerChunkX + dx, playerChunkZ + dz});
        }
    }

    // NOTE: WaitForGPU is no longer needed here. The DeferredGPUDeletionQueue
    // pattern ensures GPU resources are kept alive for N frames after destruction,
    // preventing D3D12 error 921 (OBJECT_DELETED_WHILE_STILL_IN_USE).

    // Unload chunks that are no longer needed
    std::vector<ChunkKey> chunksToUnload;
    for (const auto& chunk : m_loadedChunks) {
        if (desiredChunks.find(chunk) == desiredChunks.end()) {
            chunksToUnload.push_back(chunk);
        }
    }
    if (!chunksToUnload.empty()) {
        UnloadChunk(chunksToUnload[0].x, chunksToUnload[0].z);
    }

    // Load new chunks that are needed
    for (const auto& chunk : desiredChunks) {
        if (m_loadedChunks.find(chunk) == m_loadedChunks.end()) {
            LoadChunk(chunk.x, chunk.z);
            break;  // Only load one chunk per frame
        }
    }
}

void Engine::LoadChunk(int32_t cx, int32_t cz) {
    // NOTE: No GPU sync needed - mesh uploads use the renderer's job queue
    // and deferred deletion handles resource lifetime safely.
    entt::entity chunk = m_registry->CreateEntity();

    char tagName[64];
    snprintf(tagName, sizeof(tagName), "TerrainChunk_%d_%d", cx, cz);
    m_registry->AddComponent<Scene::TagComponent>(chunk, tagName);

    auto& transform = m_registry->AddComponent<Scene::TransformComponent>(chunk);
    // Position chunk at correct world location - mesh uses local coords
    transform.position = glm::vec3(cx * TERRAIN_CHUNK_SIZE, 0.0f, cz * TERRAIN_CHUNK_SIZE);
    transform.scale = glm::vec3(1.0f);

    constexpr uint32_t gridDim = 64;
    auto mesh = Utils::MeshGenerator::CreateTerrainHeightmapChunk(
        gridDim, TERRAIN_CHUNK_SIZE, cx, cz, m_terrainParams);

    auto& renderable = m_registry->AddComponent<Scene::RenderableComponent>(chunk);
    renderable.mesh = mesh;
    renderable.presetName = "terrain";
    renderable.albedoColor = glm::vec4(0.18f, 0.35f, 0.12f, 1.0f);
    renderable.roughness = 0.95f;
    renderable.metallic = 0.0f;

    auto& terrainComp = m_registry->AddComponent<Scene::TerrainChunkComponent>(chunk);
    terrainComp.chunkX = cx;
    terrainComp.chunkZ = cz;
    terrainComp.chunkSize = TERRAIN_CHUNK_SIZE;
    terrainComp.lodLevel = 0;

    m_loadedChunks.insert({cx, cz});

    spdlog::debug("Loaded terrain chunk ({}, {})", cx, cz);
}

void Engine::UnloadChunk(int32_t cx, int32_t cz) {
    auto view = m_registry->View<Scene::TerrainChunkComponent>();
    for (auto entity : view) {
        auto& comp = view.get<Scene::TerrainChunkComponent>(entity);
        if (comp.chunkX == cx && comp.chunkZ == cz) {
            m_registry->DestroyEntity(entity);
            break;
        }
    }

    m_loadedChunks.erase({cx, cz});

    spdlog::debug("Unloaded terrain chunk ({}, {})", cx, cz);
}

} // namespace Cortex