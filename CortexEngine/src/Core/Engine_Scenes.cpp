// Scene construction helpers for Cortex Engine.
// Cornell box + hero "Dragon Over Water Studio" layouts.

#include "Engine.h"
#include "EngineEditorMode.h"
#include "Editor/EditorWorld.h"

#include "Scene/Components.h"
#include "Scene/TerrainNoise.h"
#include "Utils/MeshGenerator.h"
#include "Utils/GLTFLoader.h"
#include "Graphics/RendererControlApplier.h"
#include "Graphics/Renderer.h"
#include "Scene/ParticleEffectLibrary.h"

#include <cstdlib>

namespace Cortex {

using Graphics::Renderer;
using Scene::TransformComponent;

namespace {
    // Local constants for Cornell box and hero pool layout. These mirror the
    // values used in Engine.cpp but are kept TU-local so scene construction
    // helpers here remain self-contained.
    constexpr float kCornellHalfExtent = 4.0f;
    constexpr float kCornellHeight     = 3.0f;
    constexpr float kHeroPoolZ         = -3.0f;

    void AddParticleEffect(Scene::ECS_Registry& registry,
                           const char* tag,
                           std::string_view effectId,
                           const glm::vec3& position) {
        entt::entity e = registry.CreateEntity();
        registry.AddComponent<Scene::TagComponent>(e, tag);
        auto& t = registry.AddComponent<Scene::TransformComponent>(e);
        t.position = position;

        Scene::ParticleEmitterComponent emitter;
        if (!Scene::ApplyParticleEffectDescriptor(effectId, emitter) &&
            !Scene::ApplyParticleEffectDescriptor("smoke", emitter)) {
            return;
        }
        registry.AddComponent<Scene::ParticleEmitterComponent>(e, emitter);
    }
}

void Engine::RebuildScene(ScenePreset preset) {
    // CRITICAL: Full GPU synchronization before destroying resources.
    // WaitForGPU flushes all command queues (main, upload, compute) and waits
    // for their completion. This is more thorough than WaitForAllFrames which
    // only waits for existing fence values.
    if (m_renderer) {
        m_renderer->WaitForGPU();

        // Reset the command list to clear CPU-side references to resources.
        // This closes the current recording, resets the allocator and command list
        // so they no longer hold references to objects we're about to delete.
        m_renderer->ResetCommandList();

        // CRITICAL: Clear BLAS cache AFTER ResetCommandList() completes.
        // At this point, the command list and allocators have been reset, so no
        // GPU operations reference the BLAS resources anymore. Clearing the cache
        // now prevents #921 OBJECT_DELETED_WHILE_STILL_IN_USE when RT is enabled.
        m_renderer->ClearBLASCache();
    }

    // Exit play mode if active before rebuilding
    if (m_playModeActive) {
        ExitPlayMode();
    }

    // Disable terrain system (will be re-enabled if switching to terrain scene)
    m_terrainEnabled = false;
    m_loadedChunks.clear();

    // Clear EditorWorld's chunk tracking BEFORE clearing the registry.
    // This prevents EditorWorld from trying to access destroyed entities.
    if (m_editorModeController && m_editorModeController->GetWorld()) {
        m_editorModeController->GetWorld()->ClearAllChunks();
    }

    // Clear all existing entities/components. This destroys RenderableComponents
    // which may release GPU resources (mesh buffers, etc.).
    m_registry->GetRegistry().clear();

    // CRITICAL: After clearing the registry, force another full GPU sync to ensure
    // all destructor-triggered resource releases have completed. This prevents
    // D3D12 validation error 921 (OBJECT_DELETED_WHILE_STILL_IN_USE) when rapidly
    // rebuilding scenes with many mesh uploads (e.g., terrain chunks).
    if (m_renderer) {
        m_renderer->WaitForGPU();
    }
    m_activeCameraEntity = entt::null;
    m_selectedEntity = entt::null;
    m_autoDemoEnabled = false;
    m_cameraControllerInitialized = false;

    m_currentScenePreset = preset;

    // Reset renderer temporal history so the new scene starts from a clean
    // state (no TAA or RT afterimages from the previous layout).
    if (m_renderer) {
        m_renderer->ResetTemporalHistoryForSceneChange();
    }

    switch (preset) {
    case ScenePreset::CornellBox:
        BuildCornellScene();
        break;
    case ScenePreset::DragonOverWater:
        BuildDragonStudioScene();
        break;
    case ScenePreset::ProceduralTerrain:
        // Skip old terrain system if Engine Editor Mode is active
        // (EditorWorld handles terrain generation with its own chunk system)
        if (!m_engineEditorMode) {
            BuildProceduralTerrainScene();
        } else {
            // In editor mode, EditorWorld handles terrain chunks but we still need
            // a camera and sun for the scene to work properly
            BuildEditorModeTerrainScene();
        }
        break;
    case ScenePreset::RTShowcase:
    case ScenePreset::GodRays: // currently shares layout with RTShowcase
    default:
        BuildRTShowcaseScene();
        break;
    case ScenePreset::MaterialLab:
        BuildMaterialLabScene();
        break;
    case ScenePreset::GlassWaterCourtyard:
        BuildGlassWaterCourtyardScene();
        break;
    case ScenePreset::EffectsShowcase:
        BuildEffectsShowcaseScene();
        break;
    case ScenePreset::TemporalValidation:
        BuildTemporalValidationScene();
        break;
    }

    InitializeCameraController();

    // Refresh LLM scene view so natural-language commands operate on the new layout.
    if (m_commandQueue) {
        m_commandQueue->RefreshLookup(m_registry.get());
    }

    const char* presetName = "Unknown";
    switch (preset) {
    case ScenePreset::CornellBox:        presetName = "Cornell Box"; break;
    case ScenePreset::DragonOverWater:   presetName = "Dragon Over Water Studio"; break;
    case ScenePreset::RTShowcase:        presetName = "RT Showcase Gallery"; break;
    case ScenePreset::MaterialLab:       presetName = "Material Lab"; break;
    case ScenePreset::GlassWaterCourtyard:presetName = "Glass and Water Courtyard"; break;
    case ScenePreset::EffectsShowcase:   presetName = "Effects Showcase"; break;
    case ScenePreset::GodRays:           presetName = "God Rays Atrium"; break;
    case ScenePreset::TemporalValidation:presetName = "Temporal Validation Lab"; break;
    case ScenePreset::ProceduralTerrain: presetName = "Procedural Terrain"; break;
    default:                             presetName = "Unknown"; break;
    }

    spdlog::info("Scene rebuilt as {}", presetName);
    spdlog::info("{}", m_registry->DescribeScene());

    // One-shot asset memory summary to highlight the heaviest categories and
    // assets in the new scene. This complements the frame-level VRAM estimate
    // and helps diagnose oversize textures or geometry.
    if (m_renderer) {
        auto breakdown = m_renderer->GetAssetMemoryBreakdown();
        const double texMB  = static_cast<double>(breakdown.textureBytes) / (1024.0 * 1024.0);
        const double envMB  = static_cast<double>(breakdown.environmentBytes) / (1024.0 * 1024.0);
        const double geomMB = static_cast<double>(breakdown.geometryBytes) / (1024.0 * 1024.0);
        const double rtMB   = static_cast<double>(breakdown.rtStructureBytes) / (1024.0 * 1024.0);
        spdlog::info("Asset memory breakdown after rebuild: tex~{:.0f} MB env~{:.0f} MB geom~{:.0f} MB RT~{:.0f} MB",
                     texMB, envMB, geomMB, rtMB);

        auto heavyTex = m_renderer->GetAssetRegistry().GetHeaviestTextures(3);
        if (!heavyTex.empty()) {
            spdlog::info("Top textures by estimated GPU bytes:");
            for (const auto& t : heavyTex) {
                const double mb = static_cast<double>(t.bytes) / (1024.0 * 1024.0);
                spdlog::info("  {} ~ {:.1f} MB", t.key, mb);
            }
        }
        auto heavyMesh = m_renderer->GetAssetRegistry().GetHeaviestMeshes(3);
        if (!heavyMesh.empty()) {
            spdlog::info("Top meshes by estimated GPU bytes:");
            for (const auto& m : heavyMesh) {
                const double mb = static_cast<double>(m.bytes) / (1024.0 * 1024.0);
                spdlog::info("  {} ~ {:.1f} MB", m.key, mb);
            }
        }
    }

    // Rebuild asset ref-counts from the new ECS graph and prune any meshes
    // that are no longer referenced so BLAS/geometry memory does not
    // accumulate across scene changes. Then prune unused textures from the
    // registry so diagnostics do not track stale entries.
    if (m_renderer) {
        // Mark the voxel volume as dirty so the next voxel render pass
        // rebuilds it from the new ECS layout instead of reusing geometry
        // from the previous scene.
        m_renderer->MarkVoxelGridDirty();
        m_renderer->RebuildAssetRefsFromScene(m_registry.get());

        // CRITICAL: Wait for ALL in-flight frames before pruning old assets.
        // This prevents OBJECT_DELETED_WHILE_STILL_IN_USE error #921 during scene switches.
        m_renderer->WaitForAllFrames();

        m_renderer->PruneUnusedMeshes(m_registry.get());
        m_renderer->PruneUnusedTextures();
    }

    // Apply VRAM-aware quality clamping after large scene rebuilds so that
    // heavy layouts automatically fall back to safe presets when the
    // estimated GPU memory footprint is close to the adapter limit.
    // DISABLED: Keep all graphics features enabled
    // ApplyVRAMQualityGovernor();
}

void Engine::BuildCornellScene() {
    spdlog::info("Building hero scene: Cornell Box with mirror");

    auto* renderer = m_renderer.get();

    // Camera starting inside the box near the front wall, looking toward the
    // center so all mirrored surfaces are visible.
    entt::entity cameraEntity = m_registry->CreateEntity();
    m_registry->AddComponent<Scene::TagComponent>(cameraEntity, "MainCamera");

    auto& cameraTransform = m_registry->AddComponent<TransformComponent>(cameraEntity);
    cameraTransform.position = glm::vec3(0.0f, 1.6f, -3.0f);
    {
        glm::vec3 target(0.0f, 1.2f, 0.0f);
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        glm::vec3 forward = glm::normalize(target - cameraTransform.position);
        cameraTransform.rotation = glm::quatLookAtLH(forward, up);
    }

    auto& camera = m_registry->AddComponent<Scene::CameraComponent>(cameraEntity);
    camera.fov = 50.0f;
    camera.isActive = true;

    if (renderer) {
        Graphics::ApplyCornellSceneControls(*renderer);
    }

    // Shared plane meshes
    auto floorMesh = Utils::MeshGenerator::CreatePlane(2.0f * kCornellHalfExtent,
                                                       2.0f * kCornellHalfExtent);
    auto wallMesh = Utils::MeshGenerator::CreatePlane(2.0f * kCornellHalfExtent,
                                                      kCornellHeight);
    if (renderer) {
        auto floorResult = renderer->UploadMesh(floorMesh);
        if (floorResult.IsErr()) {
            spdlog::warn("Failed to upload Cornell floor mesh: {}", floorResult.Error());
            floorMesh.reset();
        }
        auto wallResult = renderer->UploadMesh(wallMesh);
        if (wallResult.IsErr()) {
            spdlog::warn("Failed to upload Cornell wall mesh: {}", wallResult.Error());
            wallMesh.reset();
        }

        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while building Cornell scene; aborting geometry creation for this run.");
            return;
        }
    }

    if (!floorMesh || !floorMesh->gpuBuffers || !wallMesh || !wallMesh->gpuBuffers) {
        spdlog::warn("Cornell scene meshes are not available; skipping Cornell box geometry.");
        return;
    }

    // Floor
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_Floor");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(0.0f, 0.0f, 0.0f);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = floorMesh;
        r.albedoColor = glm::vec4(0.92f, 0.92f, 0.96f, 1.0f);
        r.metallic = 1.0f;
        r.roughness = 0.03f;
        r.ao = 1.0f;
        r.presetName = "cornell_floor";
        r.doubleSided = true;
        // Reuse the RT showcase wood floor textures so the Cornell floor
        // participates in the same BC7/BC5 material pipeline.
        r.textures.albedoPath = "assets/textures/rtshowcase/rt_gallery_floor_albedo.dds";
        r.textures.normalPath = "assets/textures/rtshowcase/rt_gallery_floor_normal_bc5.dds";
    }

    // Ceiling
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_Ceiling");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(0.0f, kCornellHeight, 0.0f);
        t.rotation = glm::quat(glm::vec3(glm::pi<float>(), 0.0f, 0.0f));

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = floorMesh;
        r.albedoColor = glm::vec4(0.9f, 0.9f, 0.95f, 1.0f);
        r.metallic = 1.0f;
        r.roughness = 0.035f;
        r.ao = 1.0f;
        r.presetName = "cornell_ceiling";
        r.doubleSided = true;
        r.textures.albedoPath = "assets/textures/rtshowcase/rt_gallery_rightwall_albedo.dds";
        r.textures.normalPath = "assets/textures/rtshowcase/rt_gallery_rightwall_normal_bc5.dds";
    }

    // Back wall
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_BackWall");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(0.0f, kCornellHeight * 0.5f, kCornellHalfExtent);
        t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), 0.0f, 0.0f));

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = wallMesh;
        r.albedoColor = glm::vec4(0.9f, 0.9f, 0.93f, 1.0f);
        r.metallic = 1.0f;
        r.roughness = 0.03f;
        r.ao = 1.0f;
        r.presetName = "cornell_back";
        r.doubleSided = true;
        r.textures.albedoPath = "assets/textures/rtshowcase/rt_gallery_rightwall_albedo.dds";
        r.textures.normalPath = "assets/textures/rtshowcase/rt_gallery_rightwall_normal_bc5.dds";
    }

    // Left wall (green)
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_LeftWall");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(-kCornellHalfExtent, kCornellHeight * 0.5f, 0.0f);
        t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), glm::half_pi<float>(), 0.0f));

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = wallMesh;
        r.albedoColor = glm::vec4(0.3f, 0.9f, 0.3f, 1.0f);
        r.metallic = 1.0f;
        r.roughness = 0.035f;
        r.ao = 1.0f;
        r.presetName = "cornell_green";
        r.doubleSided = true;
        // No albedo texture - use pure base color for classic Cornell Box look
    }

    // Right wall (red)
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_RightWall");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(kCornellHalfExtent, kCornellHeight * 0.5f, 0.0f);
        t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), -glm::half_pi<float>(), 0.0f));

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = wallMesh;
        r.albedoColor = glm::vec4(0.9f, 0.25f, 0.25f, 1.0f);
        r.metallic = 1.0f;
        r.roughness = 0.035f;
        r.ao = 1.0f;
        r.presetName = "cornell_red";
        r.doubleSided = true;
        // No albedo texture - use pure base color for classic Cornell Box look
    }

    // Front wall (mirror) closing the box toward -Z so that the interior is
    // fully enclosed and mirror reflections can bounce between back and front.
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_FrontWall");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(0.0f, kCornellHeight * 0.5f, -kCornellHalfExtent);
        t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), glm::pi<float>(), 0.0f));

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = wallMesh;
        r.albedoColor = glm::vec4(0.95f, 0.95f, 0.98f, 1.0f);
        r.metallic = 1.0f;
        r.roughness = 0.025f;
        r.ao = 1.0f;
        r.presetName = "cornell_front_mirror";
        r.doubleSided = true;
    }

    // Mirror panel on the back wall.
    auto mirrorMesh = Utils::MeshGenerator::CreatePlane(1.5f, 1.5f);
    if (renderer) {
        auto mirrorResult = renderer->UploadMesh(mirrorMesh);
        if (mirrorResult.IsErr()) {
            spdlog::warn("Failed to upload Cornell mirror mesh: {}", mirrorResult.Error());
            mirrorMesh.reset();
        }
        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading Cornell mirror mesh; skipping remaining Cornell geometry.");
            return;
        }
    }
    if (mirrorMesh && mirrorMesh->gpuBuffers) {
        // Primary mirror on the back wall.
        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_Mirror");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(0.0f, 1.0f, kCornellHalfExtent - 0.01f);
            t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), 0.0f, 0.0f));

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = mirrorMesh;
            r.albedoColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            r.metallic = 1.0f;
            r.roughness = 0.02f;
            r.ao = 1.0f;
            r.presetName = "mirror";
            r.doubleSided = true;
        }

        // Interior mirror panel facing the back-wall mirror to create a simple
        // "infinity mirror" effect when reflections are enabled. This is placed
        // slightly in front of the back wall so repeated bounces between the
        // two mirrors create a tunnel-like illusion in RT/SSR.
        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_InfinityPanel");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(0.0f, 1.0f, 0.0f);
            t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), 0.0f, 0.0f));

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = mirrorMesh;
            r.albedoColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            r.metallic = 1.0f;
            r.roughness = 0.02f;
            r.ao = 1.0f;
            r.presetName = "infinity_mirror";
            r.doubleSided = true;
        }
    }

    // Test spheres inside the box (re-used for multiple entities).
    auto sphereMesh = Utils::MeshGenerator::CreateSphere(0.25f, 32);
    if (renderer) {
        auto sphereResult = renderer->UploadMesh(sphereMesh);
        if (sphereResult.IsErr()) {
            spdlog::warn("Failed to upload Cornell sphere mesh: {}", sphereResult.Error());
            sphereMesh.reset();
        }
        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading Cornell sphere mesh; remaining geometry will be skipped.");
            return;
        }
    }
    if (sphereMesh && sphereMesh->gpuBuffers) {
        // Polished chrome sphere on the right side.
        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_SphereChrome");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(0.8f, 0.4f, 0.2f);

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = sphereMesh;
            r.albedoColor = glm::vec4(0.75f, 0.75f, 0.8f, 1.0f);
            r.metallic = 1.0f;
            r.roughness = 0.05f;
            r.ao = 1.0f;
            r.presetName = "chrome";
        }

        // Rough painted sphere on the left for GI and diffuse reflection
        // comparison against the polished metal sphere.
        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_SphereRough");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(-0.8f, 0.4f, 0.3f);

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = sphereMesh;
            r.albedoColor = glm::vec4(0.9f, 0.35f, 0.15f, 1.0f);
            r.metallic = 0.0f;
            r.roughness = 0.8f;
            r.ao = 1.0f;
            r.presetName = "cornell_rough_sphere";
        }
    }

    // Tall glossy box column near the back-left corner.
    auto boxMesh = Utils::MeshGenerator::CreateCube();
    if (renderer) {
        auto boxResult = renderer->UploadMesh(boxMesh);
        if (boxResult.IsErr()) {
            spdlog::warn("Failed to upload Cornell box mesh: {}", boxResult.Error());
            boxMesh.reset();
        }
        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading Cornell box mesh; skipping remaining Cornell geometry.");
            return;
        }
    }
    if (boxMesh && boxMesh->gpuBuffers) {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_BoxColumn");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(-0.9f, 0.75f, -0.4f);
        t.scale = glm::vec3(0.6f, 1.5f, 0.6f);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = boxMesh;
        r.albedoColor = glm::vec4(0.55f, 0.28f, 0.18f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.8f;
        r.ao = 1.0f;
        r.presetName = "brick";
        r.textures.albedoPath = "assets/textures/rtshowcase/rt_gallery_leftwall_albedo.dds";
        r.textures.normalPath = "assets/textures/rtshowcase/rt_gallery_leftwall_normal_bc5.dds";
    }

    // Low plinth in the center made from a cylinder for additional curved
    // geometry and self-shadowing.
    auto cylinderMesh = Utils::MeshGenerator::CreateCylinder(0.4f, 0.4f, 32);
    if (renderer) {
        auto cylResult = renderer->UploadMesh(cylinderMesh);
        if (cylResult.IsErr()) {
            spdlog::warn("Failed to upload Cornell cylinder mesh: {}", cylResult.Error());
            cylinderMesh.reset();
        }
        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading Cornell cylinder mesh; remaining extra Cornell geometry will be skipped.");
            return;
        }
    }
    if (cylinderMesh && cylinderMesh->gpuBuffers) {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_Plinth");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(-0.1f, 0.2f, 0.7f);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = cylinderMesh;
        r.albedoColor = glm::vec4(0.25f, 0.3f, 0.85f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.4f;
        r.ao = 1.0f;
        r.presetName = "plastic";
    }

    // Shallow water puddle in the center of the floor so the Cornell
    // layout exercises the same liquid shading path as the hero pool and
    // RT showcase courtyard. The global water function is tuned above.
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_WaterPuddle");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(0.0f, 0.0f, 0.4f);
        t.scale = glm::vec3(0.35f, 1.0f, 0.35f);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = floorMesh;
        r.albedoColor = glm::vec4(0.02f, 0.08f, 0.12f, 0.7f);
        r.metallic = 0.0f;
        r.roughness = 0.06f;
        r.ao = 1.0f;
        r.presetName = "water";
        m_registry->AddComponent<Scene::WaterSurfaceComponent>(e, Scene::WaterSurfaceComponent{0.0f});
    }

    // No hero character mesh in this layout; the Cornell box focuses on
    // spheres, columns, mirrors, liquids, and pure lighting/reflection behavior.

    // Secondary mirror panel on the right wall to create more complex
    // multi-bounce reflections.
    auto sideMirrorMesh = Utils::MeshGenerator::CreatePlane(1.0f, 1.5f);
    if (renderer) {
        auto sideResult = renderer->UploadMesh(sideMirrorMesh);
        if (sideResult.IsErr()) {
            spdlog::warn("Failed to upload Cornell side mirror mesh: {}", sideResult.Error());
            sideMirrorMesh.reset();
        }
        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading Cornell side mirror mesh; stopping additional mirror creation.");
            return;
        }
    }
    if (sideMirrorMesh && sideMirrorMesh->gpuBuffers) {
        // Pure mirror panel on the right wall.
        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_SideMirror");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(kCornellHalfExtent - 0.01f, 1.0f, -0.4f);
            t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), -glm::half_pi<float>(), 0.0f));

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = sideMirrorMesh;
            r.albedoColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            r.metallic = 1.0f;
            r.roughness = 0.03f;
            r.ao = 1.0f;
            r.presetName = "mirror";
            r.doubleSided = true;
        }

        // Small "glass brick" tiles near the side mirror using the same
        // geometry but with glass-like material parameters.
        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_GlassBrick1");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(kCornellHalfExtent - 0.015f, 0.7f, 0.3f);
            t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), -glm::half_pi<float>(), 0.0f));
            t.scale = glm::vec3(0.4f, 0.5f, 1.0f);

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = sideMirrorMesh;
            r.albedoColor = glm::vec4(0.6f, 0.8f, 1.0f, 1.0f);
            r.metallic = 0.0f;
            r.roughness = 0.05f;
            r.ao = 1.0f;
            r.presetName = "glass";
            r.doubleSided = true;
        }

        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_GlassBrick2");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(kCornellHalfExtent - 0.015f, 1.4f, 0.3f);
            t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), -glm::half_pi<float>(), 0.0f));
            t.scale = glm::vec3(0.4f, 0.5f, 1.0f);

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = sideMirrorMesh;
            r.albedoColor = glm::vec4(0.7f, 0.9f, 1.0f, 1.0f);
            r.metallic = 0.0f;
            r.roughness = 0.04f;
            r.ao = 1.0f;
            r.presetName = "glass";
            r.doubleSided = true;
        }
    }

    // Simple interior light rig: a ceiling spot approximating the classic
    // Cornell top light, a large softbox area light, and a small rim light
    // to add specular interest.
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_SoftboxArea");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(0.0f, kCornellHeight - 0.05f, 0.0f);
        glm::vec3 dir(0.0f, -1.0f, 0.0f);
        t.rotation = glm::quatLookAtLH(glm::normalize(dir), glm::vec3(0.0f, 0.0f, 1.0f));

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::AreaRect;
        l.color = glm::vec3(1.0f, 0.98f, 0.96f);
        l.intensity = 2.5f;
        l.range = 10.0f;
        l.areaSize = glm::vec2(3.0f, 2.0f);
        l.twoSided = false;
        l.castsShadows = false;
    }

    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_CeilingLight");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(0.0f, kCornellHeight - 0.1f, 0.0f);
        glm::vec3 dir(0.0f, -1.0f, 0.0f);
        t.rotation = glm::quatLookAtLH(glm::normalize(dir), glm::vec3(0.0f, 0.0f, 1.0f));

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Spot;
        l.color = glm::vec3(1.0f, 0.98f, 0.9f);
        l.intensity = 10.0f;
        l.range = 12.0f;
        l.innerConeDegrees = 35.0f;
        l.outerConeDegrees = 55.0f;
        l.castsShadows = true;
    }

    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_RimLight");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(-kCornellHalfExtent + 0.3f, 1.8f, -1.5f);
        glm::vec3 dir(0.4f, -0.5f, 1.0f);
        t.rotation = glm::quatLookAtLH(glm::normalize(dir), glm::vec3(0.0f, 1.0f, 0.0f));

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Spot;
        l.color = glm::vec3(0.8f, 0.9f, 1.0f);
        l.intensity = 4.0f;
        l.range = 10.0f;
        l.innerConeDegrees = 25.0f;
        l.outerConeDegrees = 40.0f;
        l.castsShadows = false;
    }
}

void Engine::BuildMaterialLabScene() {
    spdlog::info("Building public scene: Material Lab");

    auto* renderer = m_renderer.get();
    if (renderer) {
        Graphics::ApplyMaterialLabSceneControls(*renderer);
    }

    auto floorPlane = Utils::MeshGenerator::CreatePlane(20.0f, 11.0f);
    auto wallPlane = Utils::MeshGenerator::CreatePlane(20.0f, 6.5f);
    auto sphereMesh = Utils::MeshGenerator::CreateSphere(0.5f, 32);
    auto cubeMesh = Utils::MeshGenerator::CreateCube();
    auto cylinderMesh = Utils::MeshGenerator::CreateCylinder(0.32f, 1.3f, 32);
    auto torusMesh = Utils::MeshGenerator::CreateTorus(0.52f, 0.16f, 32, 16);

    if (renderer) {
        auto uploadMesh = [&](const std::shared_ptr<Scene::MeshData>& mesh, const char* label) {
            if (!mesh) return true;
            auto res = renderer->UploadMesh(mesh);
            if (res.IsErr()) {
                spdlog::warn("Failed to upload MaterialLab {} mesh: {}", label, res.Error());
                return false;
            }
            if (renderer->IsDeviceRemoved()) {
                spdlog::error("DX12 device was removed while uploading MaterialLab {} mesh", label);
                return false;
            }
            return true;
        };

        if (!uploadMesh(floorPlane, "floor") ||
            !uploadMesh(wallPlane, "wall") ||
            !uploadMesh(sphereMesh, "sphere") ||
            !uploadMesh(cubeMesh, "cube") ||
            !uploadMesh(cylinderMesh, "cylinder") ||
            !uploadMesh(torusMesh, "torus")) {
            return;
        }
    }

    {
        entt::entity camEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(camEntity, "MainCamera");
        auto& t = m_registry->AddComponent<TransformComponent>(camEntity);
        t.position = glm::vec3(0.0f, 2.45f, -8.2f);
        const glm::vec3 target(0.0f, 1.05f, -0.15f);
        t.rotation = glm::quatLookAtLH(glm::normalize(target - t.position), glm::vec3(0.0f, 1.0f, 0.0f));

        auto& cam = m_registry->AddComponent<Scene::CameraComponent>(camEntity);
        cam.fov = 54.0f;
        cam.isActive = true;
        m_activeCameraEntity = camEntity;
    }

    auto addRenderable = [&](const char* tag,
                             const std::shared_ptr<Scene::MeshData>& mesh,
                             const glm::vec3& position,
                             const glm::vec3& scale,
                             const glm::vec3& euler,
                             const glm::vec4& color,
                             float metallic,
                             float roughness,
                             const char* preset) -> entt::entity {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, tag);
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = position;
        t.scale = scale;
        t.rotation = glm::quat(euler);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = mesh;
        r.albedoColor = color;
        r.metallic = metallic;
        r.roughness = roughness;
        r.ao = 1.0f;
        r.presetName = preset;
        return e;
    };

    if (floorPlane && floorPlane->gpuBuffers) {
        auto floor = addRenderable("MaterialLab_Floor", floorPlane,
                                   glm::vec3(0.0f, 0.0f, 0.0f),
                                   glm::vec3(1.0f),
                                   glm::vec3(0.0f),
                                   glm::vec4(0.56f, 0.58f, 0.58f, 1.0f),
                                   0.0f, 0.78f, "masonry");
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(floor);
        r.doubleSided = true;
        r.normalScale = 0.2f;
    }

    if (wallPlane && wallPlane->gpuBuffers) {
        auto back = addRenderable("MaterialLab_Backdrop", wallPlane,
                                  glm::vec3(0.0f, 3.0f, 4.2f),
                                  glm::vec3(1.0f),
                                  glm::vec3(-glm::half_pi<float>(), 0.0f, 0.0f),
                                  glm::vec4(0.74f, 0.75f, 0.73f, 1.0f),
                                  0.0f, 0.68f, "backdrop");
        m_registry->GetComponent<Scene::RenderableComponent>(back).doubleSided = true;
    }

    struct Swatch {
        const char* tag;
        const char* preset;
        glm::vec4 color;
        float metallic;
        float roughness;
        const std::shared_ptr<Scene::MeshData>* mesh;
        glm::vec3 scale;
        glm::vec3 euler;
    };

    const Swatch swatches[] = {
        {"MaterialLab_MirrorSphere", "mirror", glm::vec4(1.0f), 1.0f, 0.02f, &sphereMesh, glm::vec3(1.0f), glm::vec3(0.0f)},
        {"MaterialLab_ChromeSphere", "chrome", glm::vec4(0.68f, 0.68f, 0.72f, 1.0f), 1.0f, 0.14f, &sphereMesh, glm::vec3(1.0f), glm::vec3(0.0f)},
        {"MaterialLab_BrushedCylinder", "brushed_metal", glm::vec4(0.72f, 0.72f, 0.76f, 1.0f), 1.0f, 0.32f, &cylinderMesh, glm::vec3(1.0f), glm::vec3(0.0f)},
        {"MaterialLab_GoldTorus", "gold", glm::vec4(1.0f, 0.76f, 0.35f, 1.0f), 1.0f, 0.20f, &torusMesh, glm::vec3(1.0f), glm::vec3(glm::half_pi<float>(), 0.0f, 0.0f)},
        {"MaterialLab_ClearcoatCube", "clearcoat", glm::vec4(0.12f, 0.26f, 0.75f, 1.0f), 0.0f, 0.24f, &cubeMesh, glm::vec3(0.9f), glm::vec3(0.0f, 0.42f, 0.0f)},
        {"MaterialLab_PlasticSphere", "plastic", glm::vec4(0.85f, 0.12f, 0.17f, 1.0f), 0.0f, 0.38f, &sphereMesh, glm::vec3(1.0f), glm::vec3(0.0f)},
        {"MaterialLab_GlassCube", "glass", glm::vec4(0.72f, 0.92f, 1.0f, 1.0f), 0.0f, 0.05f, &cubeMesh, glm::vec3(0.9f), glm::vec3(0.0f, -0.35f, 0.0f)},
        {"MaterialLab_EmissiveTorus", "emissive_panel", glm::vec4(1.0f, 0.72f, 0.28f, 1.0f), 0.0f, 0.25f, &torusMesh, glm::vec3(1.0f), glm::vec3(glm::half_pi<float>(), 0.0f, 0.0f)},
        {"MaterialLab_VelvetSphere", "velvet", glm::vec4(0.55f, 0.08f, 0.22f, 1.0f), 0.0f, 0.82f, &sphereMesh, glm::vec3(1.0f), glm::vec3(0.0f)},
        {"MaterialLab_SubsurfaceCube", "skin_ish_wax", glm::vec4(0.92f, 0.54f, 0.42f, 1.0f), 0.0f, 0.46f, &cubeMesh, glm::vec3(0.9f), glm::vec3(0.0f, -0.28f, 0.0f)}
    };

    constexpr int swatchCount = static_cast<int>(sizeof(swatches) / sizeof(swatches[0]));
    for (int i = 0; i < swatchCount; ++i) {
        const int col = i % 5;
        const int row = i / 5;
        const float x = -6.4f + static_cast<float>(col) * 3.2f;
        const float z = -1.9f + static_cast<float>(row) * 2.6f;
        const auto& s = swatches[i];
        if (!s.mesh || !(*s.mesh) || !(*s.mesh)->gpuBuffers) {
            continue;
        }

        entt::entity e = addRenderable(s.tag, *s.mesh, glm::vec3(x, 0.78f, z), s.scale, s.euler,
                                       s.color, s.metallic, s.roughness, s.preset);
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(e);
        if (std::string(s.preset) == "glass") {
            r.transmissionFactor = 0.68f;
            r.ior = 1.45f;
            r.specularFactor = 1.0f;
        } else if (std::string(s.preset) == "clearcoat") {
            r.clearcoatFactor = 0.9f;
            r.clearcoatRoughnessFactor = 0.08f;
            r.wetnessFactor = 0.55f;
        } else if (std::string(s.preset) == "brushed_metal") {
            r.anisotropyStrength = 0.75f;
        } else if (std::string(s.preset) == "emissive_panel") {
            r.emissiveColor = glm::vec3(1.0f, 0.58f, 0.22f);
            r.emissiveStrength = 3.4f;
            r.emissiveBloomFactor = 0.6f;
        }
    }

    // Neutral plinths make reflections and contact shadows easier to inspect.
    if (cubeMesh && cubeMesh->gpuBuffers) {
        for (int i = 0; i < swatchCount; ++i) {
            const int col = i % 5;
            const int row = i / 5;
            const float x = -6.4f + static_cast<float>(col) * 3.2f;
            const float z = -1.9f + static_cast<float>(row) * 2.6f;
            addRenderable(("MaterialLab_Plinth_" + std::to_string(i)).c_str(),
                          cubeMesh,
                          glm::vec3(x, 0.25f, z),
                          glm::vec3(1.35f, 0.5f, 1.35f),
                          glm::vec3(0.0f),
                          glm::vec4(0.66f, 0.66f, 0.64f, 1.0f),
                          0.0f, 0.62f, "backdrop");
        }
    }

    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "MaterialLab_KeySoftbox");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(-2.8f, 5.0f, -3.2f);
        t.rotation = glm::quatLookAtLH(glm::normalize(glm::vec3(0.4f, -1.0f, 0.35f)),
                                       glm::vec3(0.0f, 1.0f, 0.0f));

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::AreaRect;
        l.color = glm::vec3(1.0f, 0.96f, 0.9f);
        l.intensity = 3.2f;
        l.range = 18.0f;
        l.areaSize = glm::vec2(4.5f, 2.2f);
        l.castsShadows = true;
    }

    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "MaterialLab_CoolRim");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(4.8f, 2.7f, 1.9f);

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Point;
        l.color = glm::vec3(0.58f, 0.72f, 1.0f);
        l.intensity = 2.4f;
        l.range = 8.0f;
        l.castsShadows = false;
    }
}

void Engine::BuildGlassWaterCourtyardScene() {
    spdlog::info("Building public scene: Glass and Water Courtyard");

    auto* renderer = m_renderer.get();
    if (renderer) {
        Graphics::ApplyGlassWaterCourtyardSceneControls(*renderer);
    }

    auto floorPlane = Utils::MeshGenerator::CreatePlane(18.0f, 14.0f);
    auto wallPlane = Utils::MeshGenerator::CreatePlane(18.0f, 7.0f);
    auto sideWallPlane = Utils::MeshGenerator::CreatePlane(14.0f, 7.0f);
    auto poolPlane = Utils::MeshGenerator::CreatePlane(7.2f, 5.2f);
    auto quadMesh = Utils::MeshGenerator::CreateQuad(1.0f, 1.0f);
    auto cubeMesh = Utils::MeshGenerator::CreateCube();
    auto sphereMesh = Utils::MeshGenerator::CreateSphere(0.5f, 32);
    auto columnMesh = Utils::MeshGenerator::CreateCylinder(0.28f, 3.2f, 32);

    if (renderer) {
        auto uploadMesh = [&](const std::shared_ptr<Scene::MeshData>& mesh, const char* label) {
            if (!mesh) return true;
            auto res = renderer->UploadMesh(mesh);
            if (res.IsErr()) {
                spdlog::warn("Failed to upload GlassWaterCourtyard {} mesh: {}", label, res.Error());
                return false;
            }
            if (renderer->IsDeviceRemoved()) {
                spdlog::error("DX12 device was removed while uploading GlassWaterCourtyard {} mesh", label);
                return false;
            }
            return true;
        };

        if (!uploadMesh(floorPlane, "floor") ||
            !uploadMesh(wallPlane, "wall") ||
            !uploadMesh(sideWallPlane, "side wall") ||
            !uploadMesh(poolPlane, "pool") ||
            !uploadMesh(quadMesh, "quad") ||
            !uploadMesh(cubeMesh, "cube") ||
            !uploadMesh(sphereMesh, "sphere") ||
            !uploadMesh(columnMesh, "column")) {
            return;
        }
    }

    {
        entt::entity camEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(camEntity, "MainCamera");
        auto& t = m_registry->AddComponent<TransformComponent>(camEntity);
        t.position = glm::vec3(0.0f, 2.55f, -8.8f);
        const glm::vec3 target(0.0f, 1.05f, -0.35f);
        t.rotation = glm::quatLookAtLH(glm::normalize(target - t.position), glm::vec3(0.0f, 1.0f, 0.0f));

        auto& cam = m_registry->AddComponent<Scene::CameraComponent>(camEntity);
        cam.fov = 54.0f;
        cam.isActive = true;
        m_activeCameraEntity = camEntity;
    }

    auto addRenderable = [&](const char* tag,
                             const std::shared_ptr<Scene::MeshData>& mesh,
                             const glm::vec3& position,
                             const glm::vec3& scale,
                             const glm::vec3& euler,
                             const glm::vec4& color,
                             float metallic,
                             float roughness,
                             const char* preset) -> entt::entity {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, tag);
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = position;
        t.scale = scale;
        t.rotation = glm::quat(euler);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = mesh;
        r.albedoColor = color;
        r.metallic = metallic;
        r.roughness = roughness;
        r.ao = 1.0f;
        r.presetName = preset;
        return e;
    };

    if (floorPlane && floorPlane->gpuBuffers) {
        auto floor = addRenderable("GlassWaterCourtyard_Floor", floorPlane,
                                   glm::vec3(0.0f, 0.0f, 0.0f),
                                   glm::vec3(1.0f),
                                   glm::vec3(0.0f),
                                   glm::vec4(0.42f, 0.37f, 0.32f, 1.0f),
                                   0.0f, 0.78f, "masonry");
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(floor);
        r.doubleSided = true;
        r.normalScale = 0.22f;
    }

    if (wallPlane && wallPlane->gpuBuffers) {
        auto back = addRenderable("GlassWaterCourtyard_BackWall", wallPlane,
                                  glm::vec3(0.0f, 3.5f, 5.8f),
                                  glm::vec3(1.0f),
                                  glm::vec3(-glm::half_pi<float>(), 0.0f, 0.0f),
                                  glm::vec4(0.62f, 0.52f, 0.44f, 1.0f),
                                  0.0f, 0.66f, "masonry");
        m_registry->GetComponent<Scene::RenderableComponent>(back).doubleSided = true;
    }

    if (sideWallPlane && sideWallPlane->gpuBuffers) {
        auto left = addRenderable("GlassWaterCourtyard_LeftWall", sideWallPlane,
                                  glm::vec3(-9.0f, 3.5f, -0.2f),
                                  glm::vec3(1.0f),
                                  glm::vec3(-glm::half_pi<float>(), glm::half_pi<float>(), 0.0f),
                                  glm::vec4(0.56f, 0.48f, 0.42f, 1.0f),
                                  0.0f, 0.70f, "masonry");
        auto right = addRenderable("GlassWaterCourtyard_RightWall", sideWallPlane,
                                   glm::vec3(9.0f, 3.5f, -0.2f),
                                   glm::vec3(1.0f),
                                   glm::vec3(-glm::half_pi<float>(), -glm::half_pi<float>(), 0.0f),
                                   glm::vec4(0.56f, 0.48f, 0.42f, 1.0f),
                                   0.0f, 0.70f, "masonry");
        m_registry->GetComponent<Scene::RenderableComponent>(left).doubleSided = true;
        m_registry->GetComponent<Scene::RenderableComponent>(right).doubleSided = true;
    }

    if (poolPlane && poolPlane->gpuBuffers) {
        auto rim = addRenderable("GlassWaterCourtyard_PoolRim", poolPlane,
                                 glm::vec3(0.0f, 0.003f, -0.25f),
                                 glm::vec3(1.15f),
                                 glm::vec3(0.0f),
                                 glm::vec4(0.72f, 0.68f, 0.62f, 1.0f),
                                 0.0f, 0.7f, "masonry");
        m_registry->GetComponent<Scene::RenderableComponent>(rim).doubleSided = true;

        auto water = addRenderable("GlassWaterCourtyard_WaterSurface", poolPlane,
                                   glm::vec3(0.0f, -0.02f, -0.25f),
                                   glm::vec3(0.92f),
                                   glm::vec3(0.0f),
                                   glm::vec4(0.04f, 0.16f, 0.22f, 0.66f),
                                   0.0f, 0.045f, "water");
        m_registry->AddComponent<Scene::WaterSurfaceComponent>(water, Scene::WaterSurfaceComponent{0.0f});
    }

    if (columnMesh && columnMesh->gpuBuffers) {
        const glm::vec3 columnPositions[] = {
            {-4.8f, 1.6f, -2.8f},
            { 4.8f, 1.6f, -2.8f},
            {-4.8f, 1.6f,  2.8f},
            { 4.8f, 1.6f,  2.8f}
        };
        for (int i = 0; i < 4; ++i) {
            addRenderable(("GlassWaterCourtyard_Column_" + std::to_string(i)).c_str(),
                          columnMesh,
                          columnPositions[i],
                          glm::vec3(1.0f),
                          glm::vec3(0.0f),
                          glm::vec4(0.70f, 0.64f, 0.56f, 1.0f),
                          0.0f, 0.42f, "masonry");
        }
    }

    if (quadMesh && quadMesh->gpuBuffers) {
        auto roof = addRenderable("GlassWaterCourtyard_GlassCanopy", quadMesh,
                                  glm::vec3(0.0f, 3.15f, -0.25f),
                                  glm::vec3(6.4f, 4.6f, 1.0f),
                                  glm::vec3(glm::half_pi<float>(), 0.0f, 0.0f),
                                  glm::vec4(0.72f, 0.90f, 1.0f, 1.0f),
                                  0.0f, 0.035f, "glass_panel");
        auto& roofR = m_registry->GetComponent<Scene::RenderableComponent>(roof);
        roofR.transmissionFactor = 0.55f;
        roofR.ior = 1.45f;
        roofR.specularFactor = 1.2f;
        roofR.doubleSided = true;

        auto warmPanel = addRenderable("GlassWaterCourtyard_SunsetPanel", quadMesh,
                                       glm::vec3(-3.2f, 2.5f, 5.65f),
                                       glm::vec3(2.4f, 0.65f, 1.0f),
                                       glm::vec3(0.0f),
                                       glm::vec4(1.0f, 0.58f, 0.28f, 1.0f),
                                       0.0f, 0.2f, "emissive_panel");
        auto& panelR = m_registry->GetComponent<Scene::RenderableComponent>(warmPanel);
        panelR.emissiveColor = glm::vec3(1.0f, 0.48f, 0.20f);
        panelR.emissiveStrength = 2.6f;
        panelR.doubleSided = true;
    }

    if (cubeMesh && cubeMesh->gpuBuffers) {
        auto glassBlock = addRenderable("GlassWaterCourtyard_GlassBlock", cubeMesh,
                                        glm::vec3(2.8f, 0.72f, -1.3f),
                                        glm::vec3(0.85f, 1.1f, 0.85f),
                                        glm::vec3(0.0f, 0.55f, 0.0f),
                                        glm::vec4(0.64f, 0.86f, 1.0f, 1.0f),
                                        0.0f, 0.04f, "glass");
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(glassBlock);
        r.transmissionFactor = 0.68f;
        r.ior = 1.47f;
        r.specularFactor = 1.15f;
    }

    if (sphereMesh && sphereMesh->gpuBuffers) {
        addRenderable("GlassWaterCourtyard_MirrorSphere", sphereMesh,
                      glm::vec3(-2.7f, 0.78f, -1.2f),
                      glm::vec3(1.05f),
                      glm::vec3(0.0f),
                      glm::vec4(0.82f, 0.78f, 0.72f, 1.0f),
                      1.0f, 0.08f, "mirror");
    }

    auto addLight = [&](const char* tag,
                        Scene::LightType type,
                        const glm::vec3& position,
                        const glm::vec3& direction,
                        const glm::vec3& color,
                        float intensity,
                        float range) {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, tag);
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = position;
        if (glm::length(direction) > 0.001f) {
            t.rotation = glm::quatLookAtLH(glm::normalize(direction), glm::vec3(0.0f, 1.0f, 0.0f));
        }
        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = type;
        l.color = color;
        l.intensity = intensity;
        l.range = range;
        l.castsShadows = type != Scene::LightType::Point;
        if (type == Scene::LightType::Spot) {
            l.innerConeDegrees = 24.0f;
            l.outerConeDegrees = 42.0f;
        } else if (type == Scene::LightType::AreaRect) {
            l.areaSize = glm::vec2(4.5f, 2.0f);
        }
    };

    addLight("GlassWaterCourtyard_SunsetKey", Scene::LightType::Spot,
             glm::vec3(-4.2f, 4.8f, -4.4f), glm::vec3(0.58f, -0.85f, 0.72f),
             glm::vec3(1.0f, 0.70f, 0.44f), 6.6f, 24.0f);
    addLight("GlassWaterCourtyard_CoolRim", Scene::LightType::AreaRect,
             glm::vec3(4.6f, 3.2f, -3.8f), glm::vec3(-0.6f, -0.45f, 0.8f),
             glm::vec3(0.52f, 0.72f, 1.0f), 2.5f, 14.0f);
    addLight("GlassWaterCourtyard_UnderwaterFill", Scene::LightType::Point,
             glm::vec3(0.0f, -0.35f, -0.25f), glm::vec3(0.0f),
             glm::vec3(0.16f, 0.42f, 0.95f), 2.4f, 9.0f);
}

void Engine::BuildEffectsShowcaseScene() {
    spdlog::info("Building public scene: Effects Showcase");

    auto* renderer = m_renderer.get();
    if (renderer) {
        Graphics::ApplyEffectsShowcaseSceneControls(*renderer);
    }

    auto floorPlane = Utils::MeshGenerator::CreatePlane(18.0f, 12.0f);
    auto wallPlane = Utils::MeshGenerator::CreatePlane(18.0f, 6.0f);
    auto sideWallPlane = Utils::MeshGenerator::CreatePlane(12.0f, 6.0f);
    auto quadMesh = Utils::MeshGenerator::CreateQuad(1.0f, 1.0f);
    auto cubeMesh = Utils::MeshGenerator::CreateCube();
    auto sphereMesh = Utils::MeshGenerator::CreateSphere(0.5f, 32);
    auto cylinderMesh = Utils::MeshGenerator::CreateCylinder(0.22f, 2.4f, 32);
    auto torusMesh = Utils::MeshGenerator::CreateTorus(0.58f, 0.14f, 32, 16);

    if (renderer) {
        auto uploadMesh = [&](const std::shared_ptr<Scene::MeshData>& mesh, const char* label) {
            if (!mesh) return true;
            auto res = renderer->UploadMesh(mesh);
            if (res.IsErr()) {
                spdlog::warn("Failed to upload EffectsShowcase {} mesh: {}", label, res.Error());
                return false;
            }
            if (renderer->IsDeviceRemoved()) {
                spdlog::error("DX12 device was removed while uploading EffectsShowcase {} mesh", label);
                return false;
            }
            return true;
        };

        if (!uploadMesh(floorPlane, "floor") ||
            !uploadMesh(wallPlane, "wall") ||
            !uploadMesh(sideWallPlane, "side wall") ||
            !uploadMesh(quadMesh, "quad") ||
            !uploadMesh(cubeMesh, "cube") ||
            !uploadMesh(sphereMesh, "sphere") ||
            !uploadMesh(cylinderMesh, "cylinder") ||
            !uploadMesh(torusMesh, "torus")) {
            return;
        }
    }

    {
        entt::entity camEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(camEntity, "MainCamera");
        auto& t = m_registry->AddComponent<TransformComponent>(camEntity);
        t.position = glm::vec3(0.0f, 2.15f, -8.6f);
        const glm::vec3 target(0.0f, 1.25f, -0.15f);
        t.rotation = glm::quatLookAtLH(glm::normalize(target - t.position), glm::vec3(0.0f, 1.0f, 0.0f));

        auto& cam = m_registry->AddComponent<Scene::CameraComponent>(camEntity);
        cam.fov = 55.0f;
        cam.isActive = true;
        m_activeCameraEntity = camEntity;
    }

    auto addRenderable = [&](const char* tag,
                             const std::shared_ptr<Scene::MeshData>& mesh,
                             const glm::vec3& position,
                             const glm::vec3& scale,
                             const glm::vec3& euler,
                             const glm::vec4& color,
                             float metallic,
                             float roughness,
                             const char* preset) -> entt::entity {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, tag);
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = position;
        t.scale = scale;
        t.rotation = glm::quat(euler);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = mesh;
        r.albedoColor = color;
        r.metallic = metallic;
        r.roughness = roughness;
        r.ao = 1.0f;
        r.presetName = preset;
        return e;
    };

    if (floorPlane && floorPlane->gpuBuffers) {
        auto floor = addRenderable("EffectsShowcase_Floor", floorPlane,
                                   glm::vec3(0.0f, 0.0f, 0.0f),
                                   glm::vec3(1.0f),
                                   glm::vec3(0.0f),
                                   glm::vec4(0.10f, 0.11f, 0.14f, 1.0f),
                                   0.0f, 0.62f, "masonry");
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(floor);
        r.doubleSided = true;
        r.normalScale = 0.18f;
    }

    if (wallPlane && wallPlane->gpuBuffers) {
        auto back = addRenderable("EffectsShowcase_BackWall", wallPlane,
                                  glm::vec3(0.0f, 3.0f, 4.8f),
                                  glm::vec3(1.0f),
                                  glm::vec3(-glm::half_pi<float>(), 0.0f, 0.0f),
                                  glm::vec4(0.08f, 0.09f, 0.13f, 1.0f),
                                  0.0f, 0.72f, "backdrop");
        m_registry->GetComponent<Scene::RenderableComponent>(back).doubleSided = true;
    }

    if (sideWallPlane && sideWallPlane->gpuBuffers) {
        auto left = addRenderable("EffectsShowcase_LeftWall", sideWallPlane,
                                  glm::vec3(-9.0f, 3.0f, -1.0f),
                                  glm::vec3(1.0f),
                                  glm::vec3(-glm::half_pi<float>(), glm::half_pi<float>(), 0.0f),
                                  glm::vec4(0.09f, 0.09f, 0.12f, 1.0f),
                                  0.0f, 0.76f, "masonry");
        auto right = addRenderable("EffectsShowcase_RightWall", sideWallPlane,
                                   glm::vec3(9.0f, 3.0f, -1.0f),
                                   glm::vec3(1.0f),
                                   glm::vec3(-glm::half_pi<float>(), -glm::half_pi<float>(), 0.0f),
                                   glm::vec4(0.09f, 0.09f, 0.12f, 1.0f),
                                   0.0f, 0.76f, "masonry");
        m_registry->GetComponent<Scene::RenderableComponent>(left).doubleSided = true;
        m_registry->GetComponent<Scene::RenderableComponent>(right).doubleSided = true;
    }

    if (cubeMesh && cubeMesh->gpuBuffers) {
        auto plinthA = addRenderable("EffectsShowcase_CenterPlinth", cubeMesh,
                                     glm::vec3(-2.1f, 0.28f, -0.1f),
                                     glm::vec3(1.45f, 0.56f, 1.45f),
                                     glm::vec3(0.0f),
                                     glm::vec4(0.18f, 0.18f, 0.22f, 1.0f),
                                     0.0f, 0.5f, "backdrop");
        auto plinthB = addRenderable("EffectsShowcase_GlassPlinth", cubeMesh,
                                     glm::vec3(2.2f, 0.28f, -0.2f),
                                     glm::vec3(1.35f, 0.56f, 1.35f),
                                     glm::vec3(0.0f),
                                     glm::vec4(0.16f, 0.16f, 0.20f, 1.0f),
                                     0.0f, 0.52f, "backdrop");
        (void)plinthA;
        (void)plinthB;
    }

    if (sphereMesh && sphereMesh->gpuBuffers) {
        addRenderable("EffectsShowcase_ChromeOrb", sphereMesh,
                      glm::vec3(-2.1f, 1.08f, -0.1f),
                      glm::vec3(1.25f),
                      glm::vec3(0.0f),
                      glm::vec4(0.66f, 0.68f, 0.75f, 1.0f),
                      1.0f, 0.10f, "chrome");
    }

    if (cubeMesh && cubeMesh->gpuBuffers) {
        auto glass = addRenderable("EffectsShowcase_GlassCube", cubeMesh,
                                   glm::vec3(2.2f, 1.1f, -0.2f),
                                   glm::vec3(1.1f),
                                   glm::vec3(0.0f, 0.55f, 0.0f),
                                   glm::vec4(0.58f, 0.86f, 1.0f, 1.0f),
                                   0.0f, 0.04f, "glass");
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(glass);
        r.transmissionFactor = 0.62f;
        r.ior = 1.45f;
        r.specularFactor = 1.15f;
    }

    if (torusMesh && torusMesh->gpuBuffers) {
        auto torus = addRenderable("EffectsShowcase_ClearcoatTorus", torusMesh,
                                   glm::vec3(0.0f, 1.05f, 1.7f),
                                   glm::vec3(0.95f),
                                   glm::vec3(glm::half_pi<float>(), 0.0f, 0.0f),
                                   glm::vec4(0.22f, 0.15f, 0.80f, 1.0f),
                                   0.0f, 0.18f, "clearcoat");
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(torus);
        r.clearcoatFactor = 0.85f;
        r.clearcoatRoughnessFactor = 0.08f;
        r.specularColorFactor = glm::vec3(0.75f, 0.85f, 1.0f);
    }

    if (cylinderMesh && cylinderMesh->gpuBuffers) {
        for (int i = 0; i < 5; ++i) {
            const float x = -5.4f + static_cast<float>(i) * 2.7f;
            addRenderable(("EffectsShowcase_LightColumn_" + std::to_string(i)).c_str(),
                          cylinderMesh,
                          glm::vec3(x, 1.2f, 2.6f),
                          glm::vec3(0.75f, 1.0f, 0.75f),
                          glm::vec3(0.0f),
                          glm::vec4(0.13f, 0.15f, 0.18f, 1.0f),
                          0.0f, 0.44f, "brushed_metal");
        }
    }

    if (quadMesh && quadMesh->gpuBuffers) {
        const struct NeonPanel {
            const char* tag;
            glm::vec3 position;
            glm::vec3 scale;
            glm::vec3 color;
            float strength;
        } panels[] = {
            {"EffectsShowcase_NeonPanel_Magenta", glm::vec3(-4.6f, 2.2f, 4.68f), glm::vec3(2.0f, 0.52f, 1.0f), glm::vec3(1.0f, 0.18f, 0.78f), 6.0f},
            {"EffectsShowcase_NeonPanel_Cyan", glm::vec3(0.0f, 2.9f, 4.67f), glm::vec3(2.4f, 0.42f, 1.0f), glm::vec3(0.12f, 0.75f, 1.0f), 5.4f},
            {"EffectsShowcase_NeonPanel_Amber", glm::vec3(4.5f, 2.1f, 4.66f), glm::vec3(1.8f, 0.52f, 1.0f), glm::vec3(1.0f, 0.55f, 0.12f), 5.0f}
        };

        for (const auto& panel : panels) {
            auto e = addRenderable(panel.tag, quadMesh,
                                   panel.position,
                                   panel.scale,
                                   glm::vec3(0.0f),
                                   glm::vec4(panel.color, 1.0f),
                                   0.0f, 0.25f, "emissive_panel");
            auto& r = m_registry->GetComponent<Scene::RenderableComponent>(e);
            r.emissiveColor = panel.color;
            r.emissiveStrength = panel.strength;
            r.doubleSided = true;
        }
    }

    auto addPointLight = [&](const char* tag,
                             const glm::vec3& position,
                             const glm::vec3& color,
                             float intensity,
                             float range) {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, tag);
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = position;
        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Point;
        l.color = color;
        l.intensity = intensity;
        l.range = range;
        l.castsShadows = false;
    };

    addPointLight("EffectsShowcase_MagentaGlow", glm::vec3(-4.6f, 2.4f, 3.2f), glm::vec3(1.0f, 0.22f, 0.76f), 5.4f, 8.0f);
    addPointLight("EffectsShowcase_CyanGlow", glm::vec3(0.0f, 3.0f, 2.7f), glm::vec3(0.16f, 0.72f, 1.0f), 4.8f, 8.0f);
    addPointLight("EffectsShowcase_AmberGlow", glm::vec3(4.6f, 2.3f, 3.1f), glm::vec3(1.0f, 0.54f, 0.14f), 4.6f, 7.0f);

    AddParticleEffect(*m_registry, "EffectsShowcase_FireEmitter", "fire", glm::vec3(-2.0f, 1.32f, -0.85f));
    AddParticleEffect(*m_registry, "EffectsShowcase_MoteEmitter", "smoke", glm::vec3(1.2f, 1.65f, 0.2f));
    AddParticleEffect(*m_registry, "EffectsShowcase_DustEmitter", "dust", glm::vec3(-3.4f, 1.7f, 0.35f));
    AddParticleEffect(*m_registry, "EffectsShowcase_SparkEmitter", "sparks", glm::vec3(-1.25f, 1.45f, -0.55f));
    AddParticleEffect(*m_registry, "EffectsShowcase_EmberEmitter", "embers", glm::vec3(-0.55f, 1.50f, -0.35f));
    AddParticleEffect(*m_registry, "EffectsShowcase_MistEmitter", "mist", glm::vec3(2.6f, 1.42f, -0.45f));
    AddParticleEffect(*m_registry, "EffectsShowcase_RainEmitter", "rain", glm::vec3(4.0f, 3.25f, 0.2f));
    AddParticleEffect(*m_registry, "EffectsShowcase_SnowEmitter", "snow", glm::vec3(3.2f, 3.1f, 0.95f));
}

void Engine::BuildRTShowcaseScene() {
    spdlog::info("Building hero scene: RT Showcase Gallery");

    auto* renderer = m_renderer.get();

    if (renderer) {
        const bool conservative = (m_qualityMode != EngineConfig::QualityMode::Default);
        Graphics::ApplyRTShowcaseSceneControls(*renderer, conservative);
    }

    // Shared meshes
    auto floorPlane   = Utils::MeshGenerator::CreatePlane(20.0f, 6.0f);
    auto hubFloor     = Utils::MeshGenerator::CreatePlane(16.0f, 12.0f);
    auto wallPlane    = Utils::MeshGenerator::CreatePlane(6.0f, 4.0f);
    auto tallWall     = Utils::MeshGenerator::CreatePlane(8.0f, 12.0f);
    auto poolPlane    = Utils::MeshGenerator::CreatePlane(8.0f, 8.0f);
    auto quadPanel    = Utils::MeshGenerator::CreateQuad(2.0f, 2.0f);
    auto sphereMesh   = Utils::MeshGenerator::CreateSphere(0.5f, 32);
    auto smallSphere  = Utils::MeshGenerator::CreateSphere(0.25f, 24);
    auto cubeMesh     = Utils::MeshGenerator::CreateCube();
    auto cylinderMesh = Utils::MeshGenerator::CreateCylinder(0.3f, 1.8f, 32);
    auto tallCylinder = Utils::MeshGenerator::CreateCylinder(0.2f, 3.0f, 24);
    auto torusMesh    = Utils::MeshGenerator::CreateTorus(0.6f, 0.18f, 32, 16);

    // Upload shared meshes once.
    if (renderer) {
        auto uploadMesh = [&](const std::shared_ptr<Scene::MeshData>& mesh, const char* label) {
            if (!mesh) return true;
            auto res = renderer->UploadMesh(mesh);
            if (res.IsErr()) {
                spdlog::warn("Failed to upload {} mesh: {}", label, res.Error());
                return false;
            }
            if (renderer->IsDeviceRemoved()) {
                spdlog::error("DX12 device was removed while uploading {} mesh; aborting RT showcase geometry.", label);
                return false;
            }
            return true;
        };

        if (!uploadMesh(floorPlane,   "RTShowcase floor") ||
            !uploadMesh(hubFloor,     "RTShowcase hub floor") ||
            !uploadMesh(wallPlane,    "RTShowcase wall") ||
            !uploadMesh(tallWall,     "RTShowcase tall wall") ||
            !uploadMesh(poolPlane,    "RTShowcase pool") ||
            !uploadMesh(quadPanel,    "RTShowcase quad panel") ||
            !uploadMesh(sphereMesh,   "RTShowcase sphere") ||
            !uploadMesh(smallSphere,  "RTShowcase small sphere") ||
            !uploadMesh(cubeMesh,     "RTShowcase cube") ||
            !uploadMesh(cylinderMesh, "RTShowcase cylinder") ||
            !uploadMesh(tallCylinder, "RTShowcase tall cylinder") ||
            !uploadMesh(torusMesh,    "RTShowcase torus")) {
            return;
        }
    }

    // Camera positioned as a gallery hero shot so the default validation
    // frame exercises representative RT materials instead of mostly skybox.
    {
        entt::entity camEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(camEntity, "MainCamera");
        auto& t = m_registry->AddComponent<TransformComponent>(camEntity);
        t.position = glm::vec3(-14.0f, 2.05f, -6.8f);
        glm::vec3 target(-14.0f, 1.05f, 0.25f);
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        t.rotation = glm::quatLookAtLH(glm::normalize(target - t.position), up);

        auto& cam = m_registry->AddComponent<Scene::CameraComponent>(camEntity);
        cam.fov = 56.0f;
        cam.isActive = true;
        m_activeCameraEntity = camEntity;
    }

    // --------------------
    // Zone A: Reflective gallery (x < 0)
    // --------------------
    const float galleryX = -14.0f;
    const bool overbrightReflectionStress = [] {
        const char* value = std::getenv("CORTEX_RT_REFLECTION_OVERBRIGHT_STRESS");
        return value && value[0] != '\0' && value[0] != '0';
    }();

    if (floorPlane && floorPlane->gpuBuffers) {
        // Floor
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_Floor");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(galleryX, 0.0f, 0.0f);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = floorPlane;
        r.albedoColor = glm::vec4(0.70f, 0.62f, 0.52f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.92f;
        r.ao = 1.0f;
        r.normalScale = 0.12f;
        r.specularFactor = 0.15f;
        r.presetName = "wood_floor";
        r.doubleSided = true;
        // Phase 2: RT showcase floor uses pre-compressed BC7/BC5 textures when
        // available. The loader will fall back to placeholders if these DDS
        // assets are missing.
        r.textures.albedoPath = "assets/textures/rtshowcase/rt_gallery_floor_albedo.dds";
        r.textures.normalPath = "assets/textures/rtshowcase/rt_gallery_floor_normal_bc5.dds";
    }

    if (floorPlane && floorPlane->gpuBuffers) {
        // Ceiling
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_Ceiling");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(galleryX, 4.0f, 0.0f);
        t.rotation = glm::quat(glm::vec3(glm::pi<float>(), 0.0f, 0.0f));

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = floorPlane;
        r.albedoColor = glm::vec4(0.92f, 0.92f, 0.96f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.6f;
        r.ao = 1.0f;
        r.presetName = "backdrop";
        r.doubleSided = true;
    }

    if (floorPlane && floorPlane->gpuBuffers) {
        // Rear wall closes the validation view so the RT showcase reads as an
        // authored gallery instead of an open HDRI probe.
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_RearWall");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(galleryX, 2.0f, 3.0f);
        t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), 0.0f, 0.0f));

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = floorPlane;
        r.albedoColor = glm::vec4(0.68f, 0.64f, 0.58f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.82f;
        r.ao = 1.0f;
        r.presetName = "backdrop";
        r.doubleSided = true;
    }

    if (wallPlane && wallPlane->gpuBuffers) {
        // Left wall (brick)
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_LeftWall");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(galleryX - 10.0f, 2.0f, 0.0f);
        t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), glm::half_pi<float>(), 0.0f));

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = wallPlane;
        r.albedoColor = glm::vec4(0.62f, 0.60f, 0.58f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.85f;
        r.ao = 1.0f;
        r.presetName = "brick";
        r.doubleSided = true;
        r.textures.albedoPath = "assets/textures/rtshowcase/rt_gallery_leftwall_albedo.dds";
        r.textures.normalPath = "assets/textures/rtshowcase/rt_gallery_leftwall_normal_bc5.dds";

        // Mirror panels on the left wall
        if (quadPanel && quadPanel->gpuBuffers) {
            entt::entity m1 = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(m1, "RTGallery_MirrorPanel1");
            auto& mt1 = m_registry->AddComponent<TransformComponent>(m1);
            mt1.position = glm::vec3(galleryX - 9.8f, 1.2f, -1.5f);
            mt1.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), glm::half_pi<float>(), 0.0f));
            mt1.scale = glm::vec3(2.2f, 1.8f, 1.0f);

            auto& mr1 = m_registry->AddComponent<Scene::RenderableComponent>(m1);
            mr1.mesh = quadPanel;
            mr1.albedoColor = glm::vec4(1.0f);
            mr1.metallic = 1.0f;
            mr1.roughness = 0.02f;
            mr1.ao = 1.0f;
            mr1.presetName = "mirror";

            entt::entity m2 = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(m2, "RTGallery_MirrorPanel2");
            auto& mt2 = m_registry->AddComponent<TransformComponent>(m2);
            mt2.position = glm::vec3(galleryX - 9.8f, 2.6f, 1.5f);
            mt2.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), glm::half_pi<float>(), 0.0f));
            mt2.scale = glm::vec3(2.2f, 1.8f, 1.0f);

            auto& mr2 = m_registry->AddComponent<Scene::RenderableComponent>(m2);
            mr2.mesh = quadPanel;
            mr2.albedoColor = glm::vec4(1.0f);
            mr2.metallic = 1.0f;
            mr2.roughness = 0.03f;
            mr2.ao = 1.0f;
            mr2.presetName = "mirror";
        }
    }

    if (wallPlane && wallPlane->gpuBuffers) {
        // Right wall (neutral)
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_RightWall");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(galleryX + 10.0f, 2.0f, 0.0f);
        t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), -glm::half_pi<float>(), 0.0f));

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = wallPlane;
        r.albedoColor = glm::vec4(0.92f, 0.91f, 0.88f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.7f;
        r.ao = 1.0f;
        r.presetName = "backdrop";
        r.doubleSided = true;
        r.textures.albedoPath = "assets/textures/rtshowcase/rt_gallery_rightwall_albedo.dds";
        r.textures.normalPath = "assets/textures/rtshowcase/rt_gallery_rightwall_normal_bc5.dds";
        if (overbrightReflectionStress) {
            r.albedoColor = glm::vec4(1.0f, 0.76f, 0.42f, 1.0f);
            r.roughness = 0.32f;
            r.emissiveColor = glm::vec3(1.0f, 0.74f, 0.36f);
            r.emissiveStrength = 32.0f;
            r.presetName = "emissive_panel";
            r.textures.albedoPath.clear();
            r.textures.normalPath.clear();
        }
    }

    if (overbrightReflectionStress && quadPanel && quadPanel->gpuBuffers) {
        spdlog::info("RTShowcase overbright reflection stress enabled");

        entt::entity hotPanel = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(hotPanel, "RTGallery_OverbrightClampPanel");
        auto& hotT = m_registry->AddComponent<TransformComponent>(hotPanel);
        hotT.position = glm::vec3(galleryX + 9.65f, 2.05f, -1.5f);
        hotT.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), -glm::half_pi<float>(), 0.0f));
        hotT.scale = glm::vec3(1.3f, 1.1f, 1.0f);

        auto& hotR = m_registry->AddComponent<Scene::RenderableComponent>(hotPanel);
        hotR.mesh = quadPanel;
        hotR.albedoColor = glm::vec4(1.0f, 0.78f, 0.42f, 1.0f);
        hotR.metallic = 0.0f;
        hotR.roughness = 0.18f;
        hotR.ao = 1.0f;
        hotR.emissiveColor = glm::vec3(1.0f, 0.72f, 0.36f);
        hotR.emissiveStrength = 48.0f;
        hotR.presetName = "emissive_panel";
        hotR.doubleSided = true;

        entt::entity hotLight = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(hotLight, "RTGallery_OverbrightClampLight");
        auto& lightT = m_registry->AddComponent<Scene::TransformComponent>(hotLight);
        lightT.position = glm::vec3(galleryX + 8.9f, 2.25f, -1.35f);
        lightT.rotation = glm::quatLookAtLH(glm::normalize(glm::vec3(-1.0f, -0.15f, 0.05f)),
                                            glm::vec3(0.0f, 1.0f, 0.0f));

        auto& light = m_registry->AddComponent<Scene::LightComponent>(hotLight);
        light.type = Scene::LightType::Spot;
        light.color = glm::vec3(1.0f, 0.78f, 0.48f);
        light.intensity = 18.0f;
        light.range = 18.0f;
        light.innerConeDegrees = 18.0f;
        light.outerConeDegrees = 36.0f;
        light.castsShadows = false;
    }

    // Local reflection probes exercise VB deferred local IBL selection. They
    // intentionally overlap the hero gallery so debug view 42 shows the
    // probe/global blend gradient instead of a binary on/off mask.
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_LocalProbe_Left");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(galleryX - 5.0f, 1.7f, -0.1f);

        Scene::ReflectionProbeComponent probe{};
        probe.extents = glm::vec3(5.5f, 2.4f, 3.2f);
        probe.blendDistance = 2.25f;
        probe.environmentIndex = 0;
        probe.enabled = 1;
        m_registry->AddComponent<Scene::ReflectionProbeComponent>(e, probe);
    }
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_LocalProbe_Right");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(galleryX + 4.5f, 1.7f, -0.1f);

        Scene::ReflectionProbeComponent probe{};
        probe.extents = glm::vec3(5.5f, 2.4f, 3.2f);
        probe.blendDistance = 2.25f;
        probe.environmentIndex = 0;
        probe.enabled = 1;
        m_registry->AddComponent<Scene::ReflectionProbeComponent>(e, probe);
    }

    // Row of primitives down the gallery
    if (sphereMesh && sphereMesh->gpuBuffers && cubeMesh && cubeMesh->gpuBuffers && torusMesh && torusMesh->gpuBuffers) {
        const float baseZ = -1.0f;
        // Chrome sphere
        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_SphereChrome");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(galleryX - 6.0f, 0.6f, baseZ);

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = sphereMesh;
            r.albedoColor = glm::vec4(0.62f, 0.62f, 0.66f, 1.0f);
            r.metallic = 1.0f;
            r.roughness = 0.18f;
            r.ao = 1.0f;
            r.presetName = "chrome";
        }
        // Brushed metal cylinder
        if (cylinderMesh && cylinderMesh->gpuBuffers) {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_CylinderBrushed");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(galleryX - 2.0f, 0.9f, baseZ);

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = cylinderMesh;
            r.albedoColor = glm::vec4(0.7f, 0.7f, 0.75f, 1.0f);
            r.metallic = 1.0f;
            r.roughness = 0.25f;
            r.ao = 1.0f;
            r.presetName = "brushed_metal";
            r.textures.albedoPath = "assets/textures/rtshowcase/rt_gallery_cylinder_brushed_albedo.dds";
            r.textures.normalPath = "assets/textures/rtshowcase/rt_gallery_cylinder_brushed_normal_bc5.dds";
        }
        // Plastic cube
        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_CubePlastic");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(galleryX + 2.0f, 0.5f, baseZ);
            t.scale = glm::vec3(1.2f, 1.2f, 1.2f);

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = cubeMesh;
            r.albedoColor = glm::vec4(0.9f, 0.15f, 0.2f, 1.0f);
            r.metallic = 0.0f;
            r.roughness = 0.35f;
            r.ao = 1.0f;
            r.presetName = "plastic";
            r.textures.albedoPath = "assets/textures/rtshowcase/rt_gallery_cube_plastic_albedo.dds";
            r.textures.normalPath = "assets/textures/rtshowcase/rt_gallery_cube_plastic_normal_bc5.dds";
        }
        // Anisotropic torus
        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_TorusAniso");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(galleryX + 6.0f, 0.6f, baseZ);

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = torusMesh;
            r.albedoColor = glm::vec4(0.9f, 0.85f, 0.8f, 1.0f);
            r.metallic = 1.0f;
            r.roughness = 0.18f;
            r.ao = 1.0f;
            r.presetName = "brushed_metal";
        }
    }

    // Hero dragon + chrome sphere on plinths reused from the sample model.
    std::shared_ptr<Scene::MeshData> dragonMesh;
    auto dragonResult = Utils::LoadSampleModelMesh("DragonAttenuation");
    if (dragonResult.IsOk()) {
        dragonMesh = dragonResult.Value();
        if (renderer) {
            // Keep the hero asset present in the showcase. Renderer-side
            // upload throttling, BLAS accounting and internal render scaling
            // are responsible for staying within budget; the scene should not
            // remove content just because RT is enabled.
            auto upload = renderer->UploadMesh(dragonMesh);
            if (upload.IsErr()) {
                spdlog::warn("Failed to upload RTShowcase dragon mesh: {}", upload.Error());
                dragonMesh.reset();
            }
        }
    } else {
        spdlog::warn("RTShowcase: failed to load DragonAttenuation: {}", dragonResult.Error());
    }

    if (dragonMesh && cubeMesh && cubeMesh->gpuBuffers) {
        // Dragon plinth
        entt::entity pe = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(pe, "RTGallery_DragonPlinth");
        auto& pt = m_registry->AddComponent<TransformComponent>(pe);
        pt.position = glm::vec3(galleryX, 0.4f, 1.2f);
        pt.scale = glm::vec3(1.6f, 0.8f, 1.6f);

        auto& pr = m_registry->AddComponent<Scene::RenderableComponent>(pe);
        pr.mesh = cubeMesh;
        pr.albedoColor = glm::vec4(0.8f, 0.8f, 0.82f, 1.0f);
        pr.metallic = 0.0f;
        pr.roughness = 0.6f;
        pr.ao = 1.0f;
        pr.presetName = "backdrop";

        // Dragon
        entt::entity de = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(de, "RTGallery_MetalDragon");
        auto& dt = m_registry->AddComponent<TransformComponent>(de);
        dt.position = glm::vec3(galleryX, 1.0f, 1.2f);
        dt.scale = glm::vec3(0.38f);

        auto& dr = m_registry->AddComponent<Scene::RenderableComponent>(de);
        dr.mesh = dragonMesh;
        dr.albedoColor = glm::vec4(0.48f, 0.47f, 0.43f, 1.0f);
        dr.metallic = 1.0f;
        dr.roughness = 0.36f;
        dr.ao = 1.0f;
        dr.presetName = "brushed_metal";
    }

    if (smallSphere && smallSphere->gpuBuffers && cubeMesh && cubeMesh->gpuBuffers) {
        // Chrome sphere on a small plinth.
        entt::entity pe = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(pe, "RTGallery_SpherePlinth");
        auto& pt = m_registry->AddComponent<TransformComponent>(pe);
        pt.position = glm::vec3(galleryX + 4.0f, 0.3f, 1.3f);
        pt.scale = glm::vec3(0.8f, 0.4f, 0.8f);

        auto& pr = m_registry->AddComponent<Scene::RenderableComponent>(pe);
        pr.mesh = cubeMesh;
        pr.albedoColor = glm::vec4(0.8f, 0.8f, 0.82f, 1.0f);
        pr.metallic = 0.0f;
        pr.roughness = 0.6f;
        pr.ao = 1.0f;
        pr.presetName = "backdrop";

        entt::entity se = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(se, "RTGallery_SmallChromeSphere");
        auto& st = m_registry->AddComponent<TransformComponent>(se);
        st.position = glm::vec3(galleryX + 4.0f, 0.8f, 1.3f);

        auto& sr = m_registry->AddComponent<Scene::RenderableComponent>(se);
        sr.mesh = smallSphere;
        sr.albedoColor = glm::vec4(0.68f, 0.68f, 0.72f, 1.0f);
        sr.metallic = 1.0f;
        sr.roughness = 0.18f;
        sr.ao = 1.0f;
        sr.presetName = "chrome";
    }

    // Gallery lights: warm key, cool rim
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_Softbox");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(galleryX, 3.35f, -1.0f);
        glm::vec3 dir(0.0f, -1.0f, 0.25f);
        t.rotation = glm::quatLookAtLH(glm::normalize(dir), glm::vec3(0.0f, 1.0f, 0.0f));

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::AreaRect;
        l.color = glm::vec3(1.0f, 0.93f, 0.82f);
        l.intensity = 3.8f;
        l.range = 18.0f;
        l.areaSize = glm::vec2(5.5f, 2.2f);
        l.twoSided = false;
        l.castsShadows = false;
    }

    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_KeyLight");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(galleryX + 3.0f, 3.5f, -3.0f);
        glm::vec3 dir(-0.4f, -0.8f, 0.6f);
        t.rotation = glm::quatLookAtLH(glm::normalize(dir), glm::vec3(0.0f, 1.0f, 0.0f));

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Spot;
        l.color = glm::vec3(1.0f, 0.95f, 0.85f);
        l.intensity = 6.2f;
        l.range = 30.0f;
        l.innerConeDegrees = 22.0f;
        l.outerConeDegrees = 40.0f;
        l.castsShadows = true;
    }

    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_FillLight");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(galleryX + 1.5f, 2.3f, -4.6f);
        glm::vec3 dir(-0.2f, -0.25f, 1.0f);
        t.rotation = glm::quatLookAtLH(glm::normalize(dir), glm::vec3(0.0f, 1.0f, 0.0f));

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::AreaRect;
        l.color = glm::vec3(0.78f, 0.86f, 1.0f);
        l.intensity = 2.3f;
        l.range = 16.0f;
        l.areaSize = glm::vec2(6.0f, 3.0f);
        l.twoSided = false;
        l.castsShadows = false;
    }

    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_RimLight");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(galleryX - 6.0f, 3.0f, 3.0f);
        glm::vec3 dir(0.2f, -0.6f, -1.0f);
        t.rotation = glm::quatLookAtLH(glm::normalize(dir), glm::vec3(0.0f, 1.0f, 0.0f));

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Spot;
        l.color = glm::vec3(0.8f, 0.9f, 1.0f);
        l.intensity = 3.2f;
        l.range = 25.0f;
        l.innerConeDegrees = 24.0f;
        l.outerConeDegrees = 42.0f;
        l.castsShadows = false;
    }

    // Dragon fire emitter near the gallery dragon's mouth. This uses the
    // shared CPU-driven particle system and renders as small emissive
    // billboards that are bright enough to feed bloom and RT reflections.
    AddParticleEffect(*m_registry, "RTGallery_FireEmitter", "fire", glm::vec3(galleryX + 0.4f, 1.4f, 2.0f));

    // --------------------
    // Zone B: Liquid courtyard (center)
    // --------------------
    const float courtyardZ = -5.5f;

    if (hubFloor && hubFloor->gpuBuffers) {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Courtyard_Floor");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(0.0f, 0.0f, courtyardZ);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = hubFloor;
        r.albedoColor = glm::vec4(0.48f, 0.48f, 0.5f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.8f;
        r.ao = 1.0f;
        r.presetName = "brick";
        r.doubleSided = true;
    }

    if (poolPlane && poolPlane->gpuBuffers) {
        // Pool rim
        entt::entity rim = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(rim, "Courtyard_PoolRim");
        auto& rt = m_registry->AddComponent<Scene::TransformComponent>(rim);
        // Avoid coplanar z-fighting with Courtyard_Floor.
        rt.position = glm::vec3(0.0f, 0.002f, courtyardZ);

        auto& rr = m_registry->AddComponent<Scene::RenderableComponent>(rim);
        rr.mesh = poolPlane;
        rr.albedoColor = glm::vec4(0.9f, 0.9f, 0.92f, 1.0f);
        rr.metallic = 0.0f;
        rr.roughness = 0.75f;
        rr.ao = 1.0f;
        rr.presetName = "concrete";
        rr.doubleSided = true;

        // Water surface
        entt::entity water = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(water, "Courtyard_WaterSurface");
        auto& wt = m_registry->AddComponent<Scene::TransformComponent>(water);
        wt.position = glm::vec3(0.0f, -0.02f, courtyardZ);

        auto& wr = m_registry->AddComponent<Scene::RenderableComponent>(water);
        wr.mesh = poolPlane;
        wr.albedoColor = glm::vec4(0.05f, 0.18f, 0.24f, 0.62f);
        wr.metallic = 0.0f;
        wr.roughness = 0.06f;
        wr.ao = 1.0f;
        wr.presetName = "water";
        m_registry->AddComponent<Scene::WaterSurfaceComponent>(water, Scene::WaterSurfaceComponent{0.0f});
    }

    // Columns / arches around the pool
    if (tallCylinder && tallCylinder->gpuBuffers) {
        const float colRadius = 4.5f;
        for (int i = 0; i < 4; ++i) {
            float angle = glm::half_pi<float>() * static_cast<float>(i);
            float x = std::cos(angle) * colRadius;
            float z = courtyardZ + std::sin(angle) * colRadius;

            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "Courtyard_Column");
            auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
            t.position = glm::vec3(x, 1.5f, z);

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = tallCylinder;
            r.albedoColor = glm::vec4(0.82f, 0.82f, 0.86f, 1.0f);
            r.metallic = 0.0f;
            r.roughness = 0.4f;
            r.ao = 1.0f;
            r.presetName = "concrete";
        }
    }

    // Glass box over the pool and an emissive panel
    if (quadPanel && quadPanel->gpuBuffers) {
        // Glass roof
        entt::entity roof = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(roof, "Courtyard_GlassRoof");
        auto& rt = m_registry->AddComponent<Scene::TransformComponent>(roof);
        rt.position = glm::vec3(0.0f, 2.5f, courtyardZ);
        rt.rotation = glm::quat(glm::vec3(glm::half_pi<float>(), 0.0f, 0.0f));
        rt.scale = glm::vec3(6.0f, 6.0f, 1.0f);

        auto& rr = m_registry->AddComponent<Scene::RenderableComponent>(roof);
        rr.mesh = quadPanel;
        rr.albedoColor = glm::vec4(0.7f, 0.85f, 1.0f, 1.0f);
        rr.metallic = 0.0f;
        rr.roughness = 0.05f;
        rr.ao = 1.0f;
        rr.presetName = "glass_panel";

        // Suspended emissive panel
        entt::entity ep = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(ep, "Courtyard_EmissivePanel");
        auto& et = m_registry->AddComponent<Scene::TransformComponent>(ep);
        et.position = glm::vec3(0.0f, 2.2f, courtyardZ - 2.5f);
        et.rotation = glm::quat(glm::vec3(0.0f, 0.0f, 0.0f));
        et.scale = glm::vec3(3.0f, 1.0f, 1.0f);

        auto& er = m_registry->AddComponent<Scene::RenderableComponent>(ep);
        er.mesh = quadPanel;
        er.albedoColor = glm::vec4(1.0f, 0.86f, 0.58f, 1.0f);
        er.metallic = 0.0f;
        er.roughness = 0.2f;
        er.ao = 1.0f;
        er.emissiveColor = glm::vec3(1.0f, 0.86f, 0.58f);
        er.emissiveStrength = 6.0f;
        er.presetName = "emissive_panel";
    }

    // Courtyard lights
    {
        // Underwater blue fill
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Courtyard_UnderwaterLight");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(0.0f, -0.4f, courtyardZ);

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Point;
        l.color = glm::vec3(0.2f, 0.4f, 0.9f);
        l.intensity = 1.8f;
        l.range = 10.0f;
        l.castsShadows = false;
    }

    // --------------------
    // Zone C: Volumetric atrium (x > 0)
    // --------------------
    const float atriumX = 16.0f;
    const float atriumHeight = 9.0f;

    if (floorPlane && floorPlane->gpuBuffers) {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Atrium_Floor");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(atriumX, 0.0f, 0.0f);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = floorPlane;
        r.albedoColor = glm::vec4(0.36f, 0.36f, 0.39f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.85f;
        r.ao = 1.0f;
        r.presetName = "brick";
    }

    if (tallWall && tallWall->gpuBuffers) {
        // Back wall
        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "Atrium_BackWall");
            auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
            t.position = glm::vec3(atriumX, atriumHeight * 0.5f, 6.0f);
            t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), 0.0f, 0.0f));

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = tallWall;
            r.albedoColor = glm::vec4(0.28f, 0.28f, 0.31f, 1.0f);
            r.metallic = 0.0f;
            r.roughness = 0.9f;
            r.ao = 1.0f;
            r.presetName = "brick";
        }

        // Side wall with slits/windows
        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "Atrium_SlitWall");
            auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
            t.position = glm::vec3(atriumX - 5.0f, atriumHeight * 0.5f, 0.0f);
            t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), glm::half_pi<float>(), 0.0f));

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = tallWall;
            r.albedoColor = glm::vec4(0.32f, 0.32f, 0.36f, 1.0f);
            r.metallic = 0.0f;
            r.roughness = 0.85f;
            r.ao = 1.0f;
            r.presetName = "brick";
        }

        if (quadPanel && quadPanel->gpuBuffers) {
            // Vertical slits/windows for god rays.
            for (int i = 0; i < 3; ++i) {
                float y = 2.0f + static_cast<float>(i) * 2.0f;
                entt::entity e = m_registry->CreateEntity();
                m_registry->AddComponent<Scene::TagComponent>(e, "Atrium_SlitWindow");
                auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
                t.position = glm::vec3(atriumX - 4.99f, y, -1.5f + i * 1.5f);
                t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), glm::half_pi<float>(), 0.0f));
                t.scale = glm::vec3(0.5f, 1.6f, 1.0f);

                auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
                r.mesh = quadPanel;
                r.albedoColor = glm::vec4(0.9f, 0.95f, 1.0f, 0.2f);
                r.metallic = 0.0f;
                r.roughness = 0.15f;
                r.ao = 1.0f;
                r.presetName = "glass_panel";
            }
        }
    }

    // Matte statues/blocks catching beams
    if (cubeMesh && cubeMesh->gpuBuffers) {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Atrium_Block");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(atriumX + 1.5f, 0.75f, -1.5f);
        t.scale = glm::vec3(1.5f, 1.5f, 1.5f);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = cubeMesh;
        r.albedoColor = glm::vec4(0.5f, 0.5f, 0.55f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.9f;
        r.ao = 1.0f;
        r.presetName = "matte";
    }

    if (torusMesh && torusMesh->gpuBuffers) {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Atrium_Torus");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(atriumX - 0.5f, 1.2f, 1.0f);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = torusMesh;
        r.albedoColor = glm::vec4(0.5f, 0.5f, 0.54f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.85f;
        r.ao = 1.0f;
        r.presetName = "matte";
    }

    // Dust / mote particle emitter near the light shafts.
    AddParticleEffect(*m_registry, "Atrium_DustEmitter", "dust", glm::vec3(atriumX - 2.5f, 2.0f, -0.5f));

    // Small highlight light in the atrium
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Atrium_SculptureLight");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(atriumX + 2.0f, 3.0f, 0.0f);
        glm::vec3 dir(-0.4f, -1.0f, 0.1f);
        t.rotation = glm::quatLookAtLH(glm::normalize(dir), glm::vec3(0.0f, 1.0f, 0.0f));

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Spot;
        l.color = glm::vec3(1.0f, 0.95f, 0.9f);
        l.intensity = 2.6f;
        l.range = 15.0f;
        l.innerConeDegrees = 20.0f;
        l.outerConeDegrees = 35.0f;
        l.castsShadows = false;
    }
}

void Engine::BuildTemporalValidationScene() {
    spdlog::info("Building validation scene: Temporal Reprojection Lab");

    auto* renderer = m_renderer.get();
    if (renderer) {
        Graphics::ApplyTemporalValidationSceneControls(*renderer);
    }

    auto floorMesh = Utils::MeshGenerator::CreatePlane(10.0f, 8.0f);
    auto wallMesh = Utils::MeshGenerator::CreatePlane(10.0f, 4.0f);
    auto cubeMesh = Utils::MeshGenerator::CreateCube();
    auto sphereMesh = Utils::MeshGenerator::CreateSphere(0.55f, 32);
    auto torusMesh = Utils::MeshGenerator::CreateTorus(0.55f, 0.16f, 32, 16);
    auto quadMesh = Utils::MeshGenerator::CreateQuad(1.5f, 2.0f);
    auto waterMesh = Utils::MeshGenerator::CreatePlane(4.0f, 2.5f);

    if (renderer) {
        auto upload = [&](const std::shared_ptr<Scene::MeshData>& mesh, const char* label) {
            if (!mesh) {
                return false;
            }
            auto result = renderer->UploadMesh(mesh);
            if (result.IsErr()) {
                spdlog::warn("Failed to upload temporal validation {} mesh: {}", label, result.Error());
                return false;
            }
            return !renderer->IsDeviceRemoved();
        };
        if (!upload(floorMesh, "floor") ||
            !upload(wallMesh, "wall") ||
            !upload(cubeMesh, "cube") ||
            !upload(sphereMesh, "sphere") ||
            !upload(torusMesh, "torus") ||
            !upload(quadMesh, "alpha panel") ||
            !upload(waterMesh, "water")) {
            return;
        }
    }

    {
        entt::entity camEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(camEntity, "MainCamera");
        auto& t = m_registry->AddComponent<TransformComponent>(camEntity);
        t.position = glm::vec3(0.0f, 2.3f, -6.4f);
        const glm::vec3 target(0.0f, 1.0f, 0.1f);
        t.rotation = glm::quatLookAtLH(glm::normalize(target - t.position), glm::vec3(0.0f, 1.0f, 0.0f));
        auto& cam = m_registry->AddComponent<Scene::CameraComponent>(camEntity);
        cam.fov = 54.0f;
        cam.isActive = true;
        m_activeCameraEntity = camEntity;
    }

    auto addRenderable = [&](const char* tag,
                             const std::shared_ptr<Scene::MeshData>& mesh,
                             const glm::vec3& position,
                             const glm::quat& rotation,
                             const glm::vec3& scale,
                             const glm::vec4& albedo,
                             float metallic,
                             float roughness) -> entt::entity {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, tag);
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = position;
        t.rotation = rotation;
        t.scale = scale;
        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = mesh;
        r.albedoColor = albedo;
        r.metallic = metallic;
        r.roughness = roughness;
        r.ao = 1.0f;
        r.presetName = tag ? tag : "";
        return e;
    };

    if (floorMesh && floorMesh->gpuBuffers) {
        auto floor = addRenderable("TemporalLab_Floor",
                                   floorMesh,
                                   glm::vec3(0.0f, 0.0f, 0.5f),
                                   glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                   glm::vec3(1.0f),
                                   glm::vec4(0.45f, 0.48f, 0.52f, 1.0f),
                                   0.0f,
                                   0.72f);
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(floor);
        r.doubleSided = true;
    }

    if (wallMesh && wallMesh->gpuBuffers) {
        auto back = addRenderable("TemporalLab_BackWall",
                                  wallMesh,
                                  glm::vec3(0.0f, 2.0f, 3.8f),
                                  glm::quat(glm::vec3(-glm::half_pi<float>(), 0.0f, 0.0f)),
                                  glm::vec3(1.0f),
                                  glm::vec4(0.78f, 0.80f, 0.84f, 1.0f),
                                  0.0f,
                                  0.58f);
        m_registry->GetComponent<Scene::RenderableComponent>(back).doubleSided = true;
    }

    if (cubeMesh && cubeMesh->gpuBuffers) {
        auto e = addRenderable("TemporalLab_RotatingChromeBlock",
                               cubeMesh,
                               glm::vec3(-1.45f, 0.9f, 0.15f),
                               glm::quat(glm::vec3(0.0f, 0.35f, 0.0f)),
                               glm::vec3(0.75f, 1.25f, 0.75f),
                               glm::vec4(0.85f, 0.88f, 0.92f, 1.0f),
                               1.0f,
                               0.18f);
        m_registry->AddComponent<Scene::RotationComponent>(
            e,
            Scene::RotationComponent{glm::normalize(glm::vec3(0.2f, 1.0f, 0.1f)), 2.2f});
    }

    if (sphereMesh && sphereMesh->gpuBuffers) {
        auto e = addRenderable("TemporalLab_RotatingRedSphere",
                               sphereMesh,
                               glm::vec3(0.0f, 0.85f, -0.05f),
                               glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                               glm::vec3(1.0f),
                               glm::vec4(0.95f, 0.12f, 0.08f, 1.0f),
                               0.0f,
                               0.36f);
        m_registry->AddComponent<Scene::RotationComponent>(
            e,
            Scene::RotationComponent{glm::normalize(glm::vec3(0.0f, 1.0f, 0.6f)), 1.7f});
    }

    if (torusMesh && torusMesh->gpuBuffers) {
        auto e = addRenderable("TemporalLab_EmissiveSpinner",
                               torusMesh,
                               glm::vec3(1.45f, 1.05f, 0.15f),
                               glm::quat(glm::vec3(glm::half_pi<float>(), 0.0f, 0.0f)),
                               glm::vec3(1.0f),
                               glm::vec4(0.10f, 0.18f, 0.95f, 1.0f),
                               0.0f,
                               0.24f);
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(e);
        r.emissiveColor = glm::vec3(0.2f, 0.45f, 1.0f);
        r.emissiveStrength = 1.8f;
        r.presetName = "emissive";
        m_registry->AddComponent<Scene::RotationComponent>(
            e,
            Scene::RotationComponent{glm::normalize(glm::vec3(1.0f, 0.2f, 0.0f)), 2.8f});
    }

    if (quadMesh && quadMesh->gpuBuffers) {
        auto e = addRenderable("TemporalLab_AlphaMaskPanel",
                               quadMesh,
                               glm::vec3(0.0f, 1.1f, 1.15f),
                               glm::quat(glm::vec3(0.0f, 0.0f, 0.0f)),
                               glm::vec3(1.0f),
                               glm::vec4(0.05f, 0.7f, 0.25f, 1.0f),
                               0.0f,
                               0.42f);
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(e);
        r.alphaMode = Scene::RenderableComponent::AlphaMode::Mask;
        r.alphaCutoff = 0.5f;
        r.doubleSided = true;
        m_registry->AddComponent<Scene::RotationComponent>(
            e,
            Scene::RotationComponent{glm::normalize(glm::vec3(0.0f, 1.0f, 0.0f)), 1.1f});
    }

    if (waterMesh && waterMesh->gpuBuffers) {
        auto e = addRenderable("TemporalLab_WaterSurface",
                               waterMesh,
                               glm::vec3(0.0f, 0.015f, -1.9f),
                               glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                               glm::vec3(1.0f),
                               glm::vec4(0.02f, 0.09f, 0.15f, 0.78f),
                               0.0f,
                               0.05f);
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(e);
        r.presetName = "water";
        r.doubleSided = true;
        m_registry->AddComponent<Scene::WaterSurfaceComponent>(e, Scene::WaterSurfaceComponent{0.0f});
    }

    auto addLight = [&](const char* tag,
                        const glm::vec3& position,
                        const glm::vec3& color,
                        float intensity,
                        float range) {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, tag);
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = position;
        auto& light = m_registry->AddComponent<Scene::LightComponent>(e);
        light.type = Scene::LightType::Point;
        light.color = color;
        light.intensity = intensity;
        light.range = range;
        light.castsShadows = false;
    };
    addLight("TemporalLab_KeyLight", glm::vec3(-2.6f, 3.2f, -2.6f), glm::vec3(1.0f, 0.86f, 0.72f), 7.0f, 8.0f);
    addLight("TemporalLab_BlueRim", glm::vec3(2.4f, 2.4f, 1.5f), glm::vec3(0.35f, 0.55f, 1.0f), 4.0f, 7.0f);
}

void Engine::BuildGodRaysScene() {
    spdlog::info("Building hero scene: God Rays Atrium");

    auto* renderer = m_renderer.get();

    // Camera placed at one end of the atrium, looking toward a bright,
    // backlit wall so volumetric beams and water reflections read clearly.
    {
        entt::entity cameraEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(cameraEntity, "MainCamera");

        auto& t = m_registry->AddComponent<TransformComponent>(cameraEntity);
        t.position = glm::vec3(0.0f, 3.0f, -16.0f);
        glm::vec3 focus(0.0f, 1.5f, 0.0f);
        t.rotation = glm::quatLookAtLH(glm::normalize(focus - t.position),
                                     glm::vec3(0.0f, 1.0f, 0.0f));

        auto& cam = m_registry->AddComponent<Scene::CameraComponent>(cameraEntity);
        cam.fov = 55.0f;
        cam.isActive = true;
    }

    if (renderer) {
        Graphics::ApplyGodRaysSceneControls(*renderer);
    }

    Graphics::Renderer* rendererPtr = m_renderer.get();

    // Atrium dimensions (left-handed, +Z forward).
    const float hallLength = 32.0f;
    const float hallWidth  = 12.0f;
    const float wallHeight = 8.0f;

    // Floor
    auto floorMesh = Utils::MeshGenerator::CreatePlane(hallLength, hallWidth);
    if (rendererPtr) {
        auto upload = rendererPtr->UploadMesh(floorMesh);
        if (upload.IsErr()) {
            spdlog::warn("GodRays: failed to upload floor mesh: {}", upload.Error());
            floorMesh.reset();
        }
        if (rendererPtr->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading GodRays floor; aborting scene build.");
            return;
        }
    }
    if (floorMesh && floorMesh->gpuBuffers) {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "GodRays_Floor");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(0.0f, 0.0f, 0.0f);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = floorMesh;
        r.albedoColor = glm::vec4(0.18f, 0.16f, 0.15f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.6f;
        r.ao = 1.0f;
        r.presetName = "godrays_floor";
    }

    // Walls: long planes enclosing the atrium, leaving the far end open so
    // beams can rake across the interior.
    auto wallMesh = Utils::MeshGenerator::CreatePlane(hallLength, wallHeight);
    if (rendererPtr) {
        auto upload = rendererPtr->UploadMesh(wallMesh);
        if (upload.IsErr()) {
            spdlog::warn("GodRays: failed to upload wall mesh: {}", upload.Error());
            wallMesh.reset();
        }
        if (rendererPtr->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading GodRays walls; aborting scene build.");
            return;
        }
    }

    if (wallMesh && wallMesh->gpuBuffers) {
        const float halfWidth = hallWidth * 0.5f;

        // Left wall
        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "GodRays_LeftWall");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(-halfWidth, wallHeight * 0.5f, 0.0f);
            t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), -glm::half_pi<float>(), 0.0f));

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = wallMesh;
            r.albedoColor = glm::vec4(0.65f, 0.65f, 0.7f, 1.0f);
            r.metallic = 0.0f;
            r.roughness = 0.5f;
            r.ao = 1.0f;
            r.presetName = "godrays_wall";
        }

        // Right wall
        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "GodRays_RightWall");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(halfWidth, wallHeight * 0.5f, 0.0f);
            t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), glm::half_pi<float>(), 0.0f));

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = wallMesh;
            r.albedoColor = glm::vec4(0.65f, 0.65f, 0.7f, 1.0f);
            r.metallic = 0.0f;
            r.roughness = 0.5f;
            r.ao = 1.0f;
            r.presetName = "godrays_wall";
        }

        // Back wall that catches the main god rays.
        auto backWallMesh = Utils::MeshGenerator::CreatePlane(hallWidth, wallHeight);
        if (rendererPtr) {
            auto upload = rendererPtr->UploadMesh(backWallMesh);
            if (upload.IsErr()) {
                spdlog::warn("GodRays: failed to upload back wall mesh: {}", upload.Error());
                backWallMesh.reset();
            }
        }
        if (backWallMesh && backWallMesh->gpuBuffers) {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "GodRays_BackWall");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(0.0f, wallHeight * 0.5f, hallLength * 0.5f);
            t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), 0.0f, 0.0f));

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = backWallMesh;
            r.albedoColor = glm::vec4(0.9f, 0.9f, 0.95f, 1.0f);
            r.metallic = 0.0f;
            r.roughness = 0.35f;
            r.ao = 1.0f;
            r.presetName = "godrays_backwall";
        }
    }

    // Shallow central pool running along the atrium floor. This shares plane
    // geometry between the rim and the water surface.
    auto poolMesh = Utils::MeshGenerator::CreatePlane(hallLength * 0.7f, hallWidth * 0.45f);
    if (rendererPtr) {
        auto upload = rendererPtr->UploadMesh(poolMesh);
        if (upload.IsErr()) {
            spdlog::warn("GodRays: failed to upload pool mesh: {}", upload.Error());
            poolMesh.reset();
        }
        if (rendererPtr->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading GodRays pool; aborting remaining geometry.");
            return;
        }
    }

    if (poolMesh && poolMesh->gpuBuffers) {
        // Pool rim
        entt::entity rim = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(rim, "GodRays_PoolRim");
        auto& rimXf = m_registry->AddComponent<TransformComponent>(rim);
        // Avoid coplanar z-fighting with GodRays_Floor.
        rimXf.position = glm::vec3(0.0f, 0.002f, 4.0f);

        auto& rimR = m_registry->AddComponent<Scene::RenderableComponent>(rim);
        rimR.mesh = poolMesh;
        rimR.albedoColor = glm::vec4(0.85f, 0.85f, 0.87f, 1.0f);
        rimR.metallic = 0.0f;
        rimR.roughness = 0.8f;
        rimR.ao = 1.0f;
        rimR.presetName = "godrays_poolrim";

        // Water surface slightly below the rim.
        entt::entity water = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(water, "GodRays_Water");
        auto& waterXf = m_registry->AddComponent<TransformComponent>(water);
        waterXf.position = glm::vec3(0.0f, -0.02f, 4.0f);

        auto& waterR = m_registry->AddComponent<Scene::RenderableComponent>(water);
        waterR.mesh = poolMesh;
        waterR.albedoColor = glm::vec4(0.03f, 0.09f, 0.13f, 0.8f);
        waterR.metallic = 0.0f;
        waterR.roughness = 0.06f;
        waterR.ao = 1.0f;
        waterR.presetName = "godrays_water";
        m_registry->AddComponent<Scene::WaterSurfaceComponent>(water, Scene::WaterSurfaceComponent{0.0f});
    }

    // Simple columns along the pool to break up beams and provide structure.
    auto columnMesh = Utils::MeshGenerator::CreateCylinder(0.25f, wallHeight, 24);
    if (rendererPtr) {
        auto upload = rendererPtr->UploadMesh(columnMesh);
        if (upload.IsErr()) {
            spdlog::warn("GodRays: failed to upload column mesh: {}", upload.Error());
            columnMesh.reset();
        }
    }
    if (columnMesh && columnMesh->gpuBuffers) {
        const float zStart = -2.0f;
        const float zEnd   = 10.0f;
        const int   count  = 4;
        for (int i = 0; i < count; ++i) {
            float t = (count > 1) ? (float(i) / float(count - 1)) : 0.0f;
            float z = glm::mix(zStart, zEnd, t);

            for (int side = -1; side <= 1; side += 2) {
                entt::entity e = m_registry->CreateEntity();
                m_registry->AddComponent<Scene::TagComponent>(e, "GodRays_Column");
                auto& xf = m_registry->AddComponent<TransformComponent>(e);
                xf.position = glm::vec3(side * 3.0f, wallHeight * 0.5f, z);

                auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
                r.mesh = columnMesh;
                r.albedoColor = glm::vec4(0.7f, 0.7f, 0.75f, 1.0f);
                r.metallic = 0.0f;
                r.roughness = 0.4f;
                r.ao = 1.0f;
                r.presetName = "godrays_column";
            }
        }
    }

    // A pair of hero primitives resting near the pool to show reflections and
    // specular highlights inside the beams.
    auto sphereMesh = Utils::MeshGenerator::CreateSphere(0.5f, 32);
    auto cubeMesh   = Utils::MeshGenerator::CreateCube();
    if (rendererPtr) {
        auto upSphere = rendererPtr->UploadMesh(sphereMesh);
        if (upSphere.IsErr()) {
            spdlog::warn("GodRays: failed to upload sphere mesh: {}", upSphere.Error());
            sphereMesh.reset();
        }
        auto upCube = rendererPtr->UploadMesh(cubeMesh);
        if (upCube.IsErr()) {
            spdlog::warn("GodRays: failed to upload cube mesh: {}", upCube.Error());
            cubeMesh.reset();
        }
        if (rendererPtr->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading GodRays hero meshes; skipping remaining geometry.");
            return;
        }
    }
    if (sphereMesh && sphereMesh->gpuBuffers) {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "GodRays_Sphere");
        auto& xf = m_registry->AddComponent<TransformComponent>(e);
        xf.position = glm::vec3(-1.6f, 0.6f, 4.5f);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = sphereMesh;
        r.albedoColor = glm::vec4(1.0f, 0.98f, 0.95f, 1.0f);
        r.metallic = 1.0f;
        r.roughness = 0.08f;
        r.ao = 1.0f;
        r.presetName = "godrays_chrome_sphere";
    }
    if (cubeMesh && cubeMesh->gpuBuffers) {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "GodRays_GlassCube");
        auto& xf = m_registry->AddComponent<TransformComponent>(e);
        xf.position = glm::vec3(1.8f, 0.7f, 3.5f);
        xf.scale    = glm::vec3(1.2f, 1.2f, 1.2f);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = cubeMesh;
        r.albedoColor = glm::vec4(0.6f, 0.8f, 1.0f, 0.35f);
        r.metallic = 0.0f;
        r.roughness = 0.05f;
        r.ao = 1.0f;
        r.presetName = "godrays_glass_cube";
    }

    // Simple interior light rig: a warm key and a cool rim to complement the
    // sun and provide additional structure in the beams.
    auto makeSpotRotation = [](const glm::vec3& dir) {
        glm::vec3 fwd = glm::normalize(dir);
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(fwd, up)) > 0.99f) {
            up = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        return glm::quatLookAtLH(fwd, up);
    };

    // Warm key light from above-left, angled through the fog.
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "GodRays_KeyLight");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(-4.0f, 6.0f, 2.0f);
        glm::vec3 dir(0.5f, -0.9f, 0.3f);
        t.rotation = makeSpotRotation(dir);

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Spot;
        l.color = glm::vec3(1.0f, 0.92f, 0.85f);
        l.intensity = 9.0f;
        l.range = 30.0f;
        l.innerConeDegrees = 20.0f;
        l.outerConeDegrees = 35.0f;
        l.castsShadows = true;
    }

    // Cool rim light grazing across the back wall and columns.
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "GodRays_RimLight");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(4.0f, 5.0f, 6.0f);
        glm::vec3 dir(-0.4f, -0.7f, -0.6f);
        t.rotation = makeSpotRotation(dir);

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Spot;
        l.color = glm::vec3(0.8f, 0.9f, 1.1f);
        l.intensity = 6.0f;
        l.range = 28.0f;
        l.innerConeDegrees = 22.0f;
        l.outerConeDegrees = 40.0f;
        l.castsShadows = false;
    }
}

void Engine::BuildDragonStudioScene() {
    spdlog::info("Building hero scene: Dragon Over Water Studio");

    // Hero staging scene: "Dragon Over Water Studio"
    //
    // This scene is designed to exercise:
    //  - Planar water rendering (waves, reflections)
    //  - Direct lighting + cascaded sun shadows
    //  - Hybrid SSR / RT reflections and RT GI
    //  - LLM-driven edits on top of a curated layout.
    //
    // Layout (left-handed, +Z forward):
    //  - Large studio floor centered at z = -3
    //  - Square pool and water surface inset into the floor
    //  - Metal dragon hovering above the water
    //  - Chrome sphere opposite the dragon
    //  - Colored cube on the near rim
    //  - Backdrop wall behind the pool
    //  - Three-point studio lighting rig (key / fill / rim).

    const float poolZ = -3.0f;

    // Create a camera
    entt::entity cameraEntity = m_registry->CreateEntity();
    m_registry->AddComponent<Scene::TagComponent>(cameraEntity, "MainCamera");

    auto& cameraTransform = m_registry->AddComponent<Scene::TransformComponent>(cameraEntity);
    // Place camera above and behind the pool, looking toward its center.
    cameraTransform.position = glm::vec3(0.0f, 3.0f, -8.0f);
    glm::vec3 focus(0.0f, 1.0f, poolZ);
    cameraTransform.rotation = glm::quatLookAtLH(
        glm::normalize(focus - cameraTransform.position),
        glm::vec3(0.0f, 1.0f, 0.0f));

    auto& camera = m_registry->AddComponent<Scene::CameraComponent>(cameraEntity);
    camera.fov = 55.0f;  // Slightly wider FOV for full scene framing
    camera.isActive = true;

    if (m_renderer) {
        Graphics::ApplyDragonWaterStudioSunControls(*m_renderer);
    }

    // Initialize the Khronos sample model library so we can spawn the hero
    // dragon mesh by logical name ("DragonAttenuation"). Failures here should
    // not abort scene creation; we fall back to primitives if needed.
    auto sampleLibResult = Utils::InitializeSampleModelLibrary();
    if (sampleLibResult.IsErr()) {
        spdlog::warn("SampleModelLibrary initialization failed: {}", sampleLibResult.Error());
    }

    // Convenience alias for the renderer pointer.
    Graphics::Renderer* renderer = m_renderer.get();

    // Studio floor: large plane under the pool.
    auto floorMesh = Utils::MeshGenerator::CreatePlane(20.0f, 20.0f);
    if (renderer) {
        auto uploadResult = renderer->UploadMesh(floorMesh);
        if (uploadResult.IsErr()) {
            spdlog::warn("Failed to upload floor mesh: {}", uploadResult.Error());
            floorMesh.reset();
        }
        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading floor mesh; aborting Dragon studio geometry build for this run.");
            return;
        }
    }

    if (floorMesh && floorMesh->gpuBuffers) {
        entt::entity floorEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(floorEntity, "StudioFloor");
        auto& floorXform = m_registry->AddComponent<Scene::TransformComponent>(floorEntity);
        floorXform.position = glm::vec3(0.0f, 0.0f, poolZ);
        floorXform.scale = glm::vec3(1.0f);

        auto& floorRenderable = m_registry->AddComponent<Scene::RenderableComponent>(floorEntity);
        floorRenderable.mesh = floorMesh;
        floorRenderable.albedoColor = glm::vec4(0.35f, 0.25f, 0.18f, 1.0f);
        floorRenderable.metallic = 0.0f;
        floorRenderable.roughness = 0.6f;
        floorRenderable.ao = 1.0f;
        floorRenderable.presetName = "wood_floor";
    } else {
        spdlog::warn("Studio floor mesh is unavailable; 'StudioFloor' entity will be skipped.");
    }

    // Pool rim + water share the same underlying plane geometry.
    auto poolMesh = Utils::MeshGenerator::CreatePlane(10.0f, 10.0f);
    if (renderer) {
        auto uploadResult = renderer->UploadMesh(poolMesh);
        if (uploadResult.IsErr()) {
            spdlog::warn("Failed to upload pool mesh: {}", uploadResult.Error());
            poolMesh.reset();
        }
        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading pool mesh; aborting Dragon studio geometry build for this run.");
            return;
        }
    }

    if (poolMesh && poolMesh->gpuBuffers) {
        // Pool rim: bright concrete ring around the water.
        entt::entity rimEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(rimEntity, "PoolRim");
        auto& rimXform = m_registry->AddComponent<Scene::TransformComponent>(rimEntity);
        // Avoid coplanar z-fighting with the studio floor plane.
        rimXform.position = glm::vec3(0.0f, 0.002f, poolZ);
        rimXform.scale = glm::vec3(1.0f);

        auto& rimRenderable = m_registry->AddComponent<Scene::RenderableComponent>(rimEntity);
        rimRenderable.mesh = poolMesh;
        rimRenderable.albedoColor = glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);
        rimRenderable.metallic = 0.0f;
        rimRenderable.roughness = 0.8f;
        rimRenderable.ao = 1.0f;
        rimRenderable.presetName = "concrete";

        // Water surface slightly below the rim so the edge reads clearly.
        entt::entity waterEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(waterEntity, "WaterSurface");
        auto& waterXform = m_registry->AddComponent<Scene::TransformComponent>(waterEntity);
        waterXform.position = glm::vec3(0.0f, -0.02f, poolZ);
        waterXform.scale = glm::vec3(1.0f);

        auto& waterRenderable = m_registry->AddComponent<Scene::RenderableComponent>(waterEntity);
        waterRenderable.mesh = poolMesh;
        waterRenderable.albedoColor = glm::vec4(0.02f, 0.08f, 0.12f, 0.7f);
        waterRenderable.metallic = 0.0f;
        waterRenderable.roughness = 0.08f;
        waterRenderable.ao = 1.0f;
        waterRenderable.presetName = "water";
        m_registry->AddComponent<Scene::WaterSurfaceComponent>(waterEntity, Scene::WaterSurfaceComponent{0.0f});
    } else {
        spdlog::warn("Pool mesh is unavailable; 'PoolRim' and 'WaterSurface' entities will be skipped.");
    }

    // Backdrop wall behind the pool to catch shadows and reflections.
    auto wallMesh = Utils::MeshGenerator::CreatePlane(20.0f, 10.0f);
    if (renderer) {
        auto uploadResult = renderer->UploadMesh(wallMesh);
        if (uploadResult.IsErr()) {
            spdlog::warn("Failed to upload wall mesh: {}", uploadResult.Error());
            wallMesh.reset();
        }
        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading backdrop wall mesh; aborting remaining Dragon studio geometry.");
            return;
        }
    }

    if (wallMesh && wallMesh->gpuBuffers) {
        entt::entity wallEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(wallEntity, "BackdropWall");
        auto& wallXform = m_registry->AddComponent<Scene::TransformComponent>(wallEntity);
        wallXform.position = glm::vec3(0.0f, 5.0f, poolZ + 8.0f);
        // Rotate plane upright so its normal points roughly toward the camera.
        wallXform.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), 0.0f, 0.0f));
        wallXform.scale = glm::vec3(1.0f);

        auto& wallRenderable = m_registry->AddComponent<Scene::RenderableComponent>(wallEntity);
        wallRenderable.mesh = wallMesh;
        wallRenderable.albedoColor = glm::vec4(0.15f, 0.15f, 0.18f, 1.0f);
        wallRenderable.metallic = 0.0f;
        wallRenderable.roughness = 0.85f;
        wallRenderable.ao = 1.0f;
        wallRenderable.presetName = "backdrop";
    } else {
        spdlog::warn("Backdrop wall mesh is unavailable; 'BackdropWall' entity will be skipped.");
    }

    // Hero dragon mesh over the water.
    std::shared_ptr<Scene::MeshData> dragonMesh;
    auto dragonResult = Utils::LoadSampleModelMesh("DragonAttenuation");
    if (dragonResult.IsOk()) {
        dragonMesh = dragonResult.Value();
        if (renderer) {
            auto uploadResult = renderer->UploadMesh(dragonMesh);
            if (uploadResult.IsErr()) {
                spdlog::warn("Failed to upload dragon mesh: {}", uploadResult.Error());
                dragonMesh.reset();
            }
        }
    } else {
        spdlog::warn("Failed to load DragonAttenuation sample mesh: {}", dragonResult.Error());
    }

    if (dragonMesh && dragonMesh->gpuBuffers) {
        entt::entity dragonEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(dragonEntity, "MetalDragon");
        auto& dragonXform = m_registry->AddComponent<Scene::TransformComponent>(dragonEntity);
        dragonXform.position = glm::vec3(1.5f, 1.0f, poolZ);
        dragonXform.scale = glm::vec3(1.0f);

        auto& dragonRenderable = m_registry->AddComponent<Scene::RenderableComponent>(dragonEntity);
        dragonRenderable.mesh = dragonMesh;
        dragonRenderable.albedoColor = glm::vec4(0.75f, 0.75f, 0.8f, 1.0f);
        dragonRenderable.metallic = 1.0f;
        dragonRenderable.roughness = 0.22f;
        dragonRenderable.ao = 1.0f;
        dragonRenderable.presetName = "polished_metal";
    }

    // Chrome test sphere opposite the dragon.
    auto sphereMesh = Utils::MeshGenerator::CreateSphere(0.75f, 32);
    if (renderer) {
        auto uploadResult = renderer->UploadMesh(sphereMesh);
        if (uploadResult.IsErr()) {
            spdlog::warn("Failed to upload sphere mesh: {}", uploadResult.Error());
            sphereMesh.reset();
        }
        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading sphere mesh; remaining Dragon studio geometry will be skipped.");
            return;
        }
    }

    if (sphereMesh && sphereMesh->gpuBuffers) {
        entt::entity sphereEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(sphereEntity, "MetalSphere");
        auto& sphereXform = m_registry->AddComponent<Scene::TransformComponent>(sphereEntity);
        sphereXform.position = glm::vec3(-1.5f, 1.0f, poolZ);
        sphereXform.scale = glm::vec3(1.0f);

        auto& sphereRenderable = m_registry->AddComponent<Scene::RenderableComponent>(sphereEntity);
        sphereRenderable.mesh = sphereMesh;
        sphereRenderable.albedoColor = glm::vec4(0.75f, 0.75f, 0.8f, 1.0f);
        sphereRenderable.metallic = 1.0f;
        sphereRenderable.roughness = 0.05f;
        sphereRenderable.ao = 1.0f;
        sphereRenderable.presetName = "chrome";
    } else {
        spdlog::warn("Sphere mesh is unavailable; 'MetalSphere' entity will be skipped.");
    }

    // Colored cube on the near rim for GI/reflection contrast.
    auto cubeMesh = Utils::MeshGenerator::CreateCube();
    if (renderer) {
        auto uploadResult = renderer->UploadMesh(cubeMesh);
        if (uploadResult.IsErr()) {
            spdlog::warn("Failed to upload cube mesh: {}", uploadResult.Error());
            cubeMesh.reset();
        }
        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading cube mesh; remaining Dragon studio geometry will be skipped.");
            return;
        }
    }

    if (cubeMesh && cubeMesh->gpuBuffers) {
        entt::entity cubeEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(cubeEntity, "ColorCube");
        auto& cubeXform = m_registry->AddComponent<Scene::TransformComponent>(cubeEntity);
        cubeXform.position = glm::vec3(0.0f, 0.5f, poolZ - 1.5f);
        cubeXform.scale = glm::vec3(1.5f, 1.0f, 1.5f);

        auto& cubeRenderable = m_registry->AddComponent<Scene::RenderableComponent>(cubeEntity);
        cubeRenderable.mesh = cubeMesh;
        cubeRenderable.albedoColor = glm::vec4(0.5f, 0.1f, 0.8f, 1.0f);
        cubeRenderable.metallic = 0.0f;
        cubeRenderable.roughness = 0.4f;
        cubeRenderable.ao = 1.0f;
        cubeRenderable.presetName = "painted_plastic";
    } else {
        spdlog::warn("Cube mesh is unavailable; 'ColorCube' entity will be skipped.");
    }

    // Studio lighting rig: warm key, cool rim, and soft fill.
    auto makeSpotRotation = [](const glm::vec3& dir) {
        glm::vec3 fwd = glm::normalize(dir);
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(fwd, up)) > 0.99f) {
            up = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        return glm::quatLookAtLH(fwd, up);
    };

    // Key light
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "KeyLight");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(3.0f, 4.0f, poolZ - 1.0f);
        glm::vec3 dir(-0.6f, -0.8f, 0.7f);
        t.rotation = makeSpotRotation(dir);

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Spot;
        l.color = glm::vec3(1.0f, 0.95f, 0.85f);
        // Slightly reduced intensity and a softer outer cone keep the floor
        // hotspot under the dragon bright but less extreme. We rely on the
        // sun/cascaded shadows for structure and disable key-light shadows
        // entirely so small PCF/PCSS variations do not cause flicker in the
        // patch under the dragon.
        l.intensity = 10.0f;
        l.range = 25.0f;
        l.innerConeDegrees = 22.0f;
        l.outerConeDegrees = 40.0f;
        l.castsShadows = false;
    }

    // Fill light
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "FillLight");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(-3.0f, 2.0f, poolZ - 0.0f);

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Point;
        l.color = glm::vec3(0.8f, 0.85f, 1.0f);
        l.intensity = 4.0f;
        l.range = 20.0f;
        l.castsShadows = false;
    }

    // Rim light
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "RimLight");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(0.0f, 3.0f, poolZ + 7.0f);
        glm::vec3 dir(0.0f, -0.5f, -1.0f);
        t.rotation = makeSpotRotation(dir);

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Spot;
        l.color = glm::vec3(0.9f, 0.9f, 1.0f);
        l.intensity = 6.0f;
        l.range = 25.0f;
        l.innerConeDegrees = 25.0f;
        l.outerConeDegrees = 42.0f;
        l.castsShadows = false;
    }

    // Large softbox-style area light above the pool to produce broad,
    // studio-like highlights on metals and water. This is implemented as a
    // rectangular area light with no dedicated shadow map; it relies on the
    // existing sun shadows and volumetric fog for structure.
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "SoftboxArea");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(0.0f, 6.0f, poolZ - 1.0f);
        glm::vec3 dir(0.0f, -1.0f, 0.1f);
        t.rotation = makeSpotRotation(dir);

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::AreaRect;
        l.color = glm::vec3(1.0f, 0.98f, 0.94f);
        l.intensity = 3.0f;
        l.range = 30.0f;
        l.areaSize = glm::vec2(6.0f, 4.0f);
        l.twoSided = false;
        l.castsShadows = false;
    }

}

void Engine::SetCameraToSceneDefault(Scene::TransformComponent& transform) {
    glm::vec3 pos;
    glm::vec3 target;

    if (m_currentScenePreset == ScenePreset::CornellBox) {
        pos = glm::vec3(0.0f, 1.6f, -3.0f);
        target = glm::vec3(0.0f, 1.2f, 0.0f);
    } else if (m_currentScenePreset == ScenePreset::RTShowcase) {
        pos = glm::vec3(-14.0f, 2.05f, -6.8f);
        target = glm::vec3(-14.0f, 1.05f, 0.25f);
    } else if (m_currentScenePreset == ScenePreset::MaterialLab) {
        pos = glm::vec3(0.0f, 2.45f, -8.2f);
        target = glm::vec3(0.0f, 1.05f, -0.15f);
    } else if (m_currentScenePreset == ScenePreset::TemporalValidation) {
        pos = glm::vec3(0.0f, 2.3f, -6.4f);
        target = glm::vec3(0.0f, 1.0f, 0.1f);
    } else if (m_currentScenePreset == ScenePreset::ProceduralTerrain) {
        pos = glm::vec3(0.0f, 50.0f, -10.0f);
        target = glm::vec3(0.0f, 30.0f, 50.0f);
    } else {
        pos = glm::vec3(0.0f, 3.0f, -8.0f);
        target = glm::vec3(0.0f, 1.0f, kHeroPoolZ);
    }

    glm::vec3 up(0.0f, 1.0f, 0.0f);
    glm::vec3 forward = glm::normalize(target - pos);
    if (std::abs(glm::dot(forward, up)) > 0.99f) {
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    transform.position = pos;
    transform.rotation = glm::quatLookAtLH(forward, up);

    forward = glm::normalize(forward);
    m_cameraYaw = std::atan2(forward.x, forward.z);
    m_cameraPitch = std::asin(glm::clamp(forward.y, -1.0f, 1.0f));
    float pitchLimit = glm::radians(89.0f);
    m_cameraPitch = glm::clamp(m_cameraPitch, -pitchLimit, pitchLimit);
}

// =============================================================================
// Procedural Terrain Scene (appended - does not modify existing code)
// =============================================================================

void Engine::BuildProceduralTerrainScene() {
    // ==========================================================================
    // PROCEDURAL TERRAIN WORLD - Minecraft-style explorable world
    // ==========================================================================

    // Enable terrain system with varied, interesting terrain
    m_terrainEnabled = true;
    m_terrainParams = Scene::TerrainNoiseParams{};
    m_terrainParams.seed = 42;
    m_terrainParams.amplitude = 20.0f;      // Taller mountains
    m_terrainParams.frequency = 0.003f;     // Larger features
    m_terrainParams.octaves = 6;            // More detail
    m_terrainParams.lacunarity = 2.0f;
    m_terrainParams.gain = 0.5f;
    m_terrainParams.warp = 15.0f;           // Domain warping for natural look

    // Simple hash function for procedural placement
    auto hash = [](int x, int z, int seed) -> float {
        uint32_t h = static_cast<uint32_t>(x * 374761393 + z * 668265263 + seed);
        h = (h ^ (h >> 13)) * 1274126177;
        return static_cast<float>(h & 0xFFFF) / 65535.0f;
    };

    // Create camera at a nice starting position
    {
        entt::entity camera = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(camera, "MainCamera");

        // Start at origin, sample terrain height
        float startY = Scene::SampleTerrainHeight(0.0, 0.0, m_terrainParams) + 2.0f;

        auto& transform = m_registry->AddComponent<TransformComponent>(camera);
        transform.position = glm::vec3(0.0f, startY, 0.0f);
        glm::vec3 forward = glm::normalize(glm::vec3(0.0f, 0.0f, 1.0f));
        transform.rotation = glm::quatLookAtLH(forward, glm::vec3(0.0f, 1.0f, 0.0f));

        auto& cam = m_registry->AddComponent<Scene::CameraComponent>(camera);
        cam.fov = 75.0f;  // Wider FOV for exploration
        cam.nearPlane = 0.1f;
        cam.farPlane = 1500.0f;

        m_activeCameraEntity = camera;
    }

    // Create terrain chunks - use same radius as dynamic loading
    const int32_t chunkRadius = CHUNK_LOAD_RADIUS;
    const uint32_t gridDim = 64;
    const float chunkSize = TERRAIN_CHUNK_SIZE;
    int chunkCount = 0;

    // Clear loaded chunks tracking (will be populated below)
    m_loadedChunks.clear();

    for (int32_t cz = -chunkRadius; cz <= chunkRadius; ++cz) {
        for (int32_t cx = -chunkRadius; cx <= chunkRadius; ++cx) {
            entt::entity chunk = m_registry->CreateEntity();

            char tagName[64];
            snprintf(tagName, sizeof(tagName), "TerrainChunk_%d_%d", cx, cz);
            m_registry->AddComponent<Scene::TagComponent>(chunk, tagName);

            auto& transform = m_registry->AddComponent<TransformComponent>(chunk);
            // Position chunk at correct world location - mesh uses local coords
            transform.position = glm::vec3(cx * chunkSize, 0.0f, cz * chunkSize);
            transform.scale = glm::vec3(1.0f);

            auto mesh = Utils::MeshGenerator::CreateTerrainHeightmapChunk(
                gridDim, chunkSize, cx, cz, m_terrainParams);

            auto& renderable = m_registry->AddComponent<Scene::RenderableComponent>(chunk);
            renderable.mesh = mesh;
            renderable.presetName = "terrain";
            renderable.albedoColor = glm::vec4(0.18f, 0.35f, 0.12f, 1.0f);  // Forest green
            renderable.roughness = 0.95f;
            renderable.metallic = 0.0f;

            auto& terrainComp = m_registry->AddComponent<Scene::TerrainChunkComponent>(chunk);
            terrainComp.chunkX = cx;
            terrainComp.chunkZ = cz;
            terrainComp.chunkSize = chunkSize;
            terrainComp.lodLevel = 0;

            // Register chunk in loaded set for dynamic streaming
            m_loadedChunks.insert({cx, cz});

            ++chunkCount;
        }
    }

    // Directional sun light - warm afternoon sun
    {
        entt::entity sun = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(sun, "Sun");

        auto& transform = m_registry->AddComponent<TransformComponent>(sun);
        transform.position = glm::vec3(500.0f, 800.0f, 300.0f);
        glm::vec3 sunDir = glm::normalize(glm::vec3(-0.3f, -0.85f, -0.4f));
        transform.rotation = glm::quatLookAtLH(sunDir, glm::vec3(0.0f, 1.0f, 0.0f));

        auto& light = m_registry->AddComponent<Scene::LightComponent>(sun);
        light.type = Scene::LightType::Directional;
        light.color = glm::vec3(1.0f, 0.95f, 0.8f);  // Warm sunlight
        light.intensity = 4.0f;
        light.castsShadows = true;
    }

    // Helper: Spawn a tree at position
    auto spawnTree = [&](float x, float z, int treeId) {
        float groundY = Scene::SampleTerrainHeight(
            static_cast<double>(x), static_cast<double>(z), m_terrainParams);

        float trunkHeight = 3.0f + hash(static_cast<int>(x), static_cast<int>(z), 100) * 2.0f;
        float trunkRadius = 0.15f + hash(static_cast<int>(x), static_cast<int>(z), 200) * 0.1f;
        float foliageRadius = 1.2f + hash(static_cast<int>(x), static_cast<int>(z), 300) * 0.8f;

        // Trunk
        {
            entt::entity trunk = m_registry->CreateEntity();
            char name[32];
            snprintf(name, sizeof(name), "TreeTrunk_%d", treeId);
            m_registry->AddComponent<Scene::TagComponent>(trunk, name);

            auto& t = m_registry->AddComponent<TransformComponent>(trunk);
            t.position = glm::vec3(x, groundY + trunkHeight * 0.5f, z);
            t.scale = glm::vec3(trunkRadius * 2.0f, trunkHeight, trunkRadius * 2.0f);

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(trunk);
            r.mesh = Utils::MeshGenerator::CreateCylinder(0.5f, 1.0f, 8);
            r.presetName = "wood";
            r.albedoColor = glm::vec4(0.35f, 0.22f, 0.1f, 1.0f);  // Brown bark
            r.roughness = 0.9f;
            r.metallic = 0.0f;
        }

        // Foliage (cone shape for pine tree look)
        {
            entt::entity foliage = m_registry->CreateEntity();
            char name[32];
            snprintf(name, sizeof(name), "TreeFoliage_%d", treeId);
            m_registry->AddComponent<Scene::TagComponent>(foliage, name);

            auto& t = m_registry->AddComponent<TransformComponent>(foliage);
            t.position = glm::vec3(x, groundY + trunkHeight + foliageRadius * 0.5f, z);
            t.scale = glm::vec3(foliageRadius * 2.0f, foliageRadius * 2.5f, foliageRadius * 2.0f);

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(foliage);
            r.mesh = Utils::MeshGenerator::CreateCone(0.5f, 1.0f, 8);
            r.presetName = "leaves";
            r.albedoColor = glm::vec4(0.1f, 0.4f, 0.15f, 1.0f);  // Dark green
            r.roughness = 0.8f;
            r.metallic = 0.0f;
        }
    };

    // Helper: Spawn a rock at position
    auto spawnRock = [&](float x, float z, int rockId) {
        float groundY = Scene::SampleTerrainHeight(
            static_cast<double>(x), static_cast<double>(z), m_terrainParams);

        float size = 0.3f + hash(static_cast<int>(x * 10), static_cast<int>(z * 10), 400) * 0.6f;

        entt::entity rock = m_registry->CreateEntity();
        char name[32];
        snprintf(name, sizeof(name), "Rock_%d", rockId);
        m_registry->AddComponent<Scene::TagComponent>(rock, name);

        auto& t = m_registry->AddComponent<TransformComponent>(rock);
        t.position = glm::vec3(x, groundY + size * 0.3f, z);
        t.scale = glm::vec3(size, size * 0.6f, size);
        // Random rotation
        float yaw = hash(static_cast<int>(x * 7), static_cast<int>(z * 7), 500) * 6.28f;
        t.rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(rock);
        r.mesh = Utils::MeshGenerator::CreateSphere(0.5f, 8);
        r.presetName = "stone";
        r.albedoColor = glm::vec4(0.4f, 0.4f, 0.42f, 1.0f);  // Gray stone
        r.roughness = 0.85f;
        r.metallic = 0.0f;
    };

    // Procedurally place trees and rocks across the terrain
    int treeCount = 0;
    int rockCount = 0;
    const float worldExtent = chunkRadius * chunkSize;

    for (float x = -worldExtent; x < worldExtent; x += 20.0f) {
        for (float z = -worldExtent; z < worldExtent; z += 20.0f) {
            // Add some jitter
            float jx = x + (hash(static_cast<int>(x), static_cast<int>(z), 1) - 0.5f) * 6.0f;
            float jz = z + (hash(static_cast<int>(x), static_cast<int>(z), 2) - 0.5f) * 6.0f;

            float h = Scene::SampleTerrainHeight(
                static_cast<double>(jx), static_cast<double>(jz), m_terrainParams);

            // Trees on mid-height terrain (not too high, not too low)
            if (h > 4.0f && h < 16.0f && hash(static_cast<int>(jx), static_cast<int>(jz), 3) > 0.7f) {
                spawnTree(jx, jz, treeCount++);
            }
            // Rocks scattered more randomly
            else if (hash(static_cast<int>(jx * 2), static_cast<int>(jz * 2), 4) > 0.92f) {
                spawnRock(jx, jz, rockCount++);
            }
        }
    }

    // Spawn interactable objects near spawn
    auto spawnInteractable = [&](const char* name, float x, float z, float radius,
                                  const glm::vec4& color) {
        float groundY = Scene::SampleTerrainHeight(
            static_cast<double>(x), static_cast<double>(z), m_terrainParams);

        entt::entity obj = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(obj, name);

        auto& t = m_registry->AddComponent<TransformComponent>(obj);
        t.position = glm::vec3(x, groundY + radius + 0.1f, z);
        t.scale = glm::vec3(radius * 2.0f);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(obj);
        r.mesh = Utils::MeshGenerator::CreateSphere(0.5f, 16);
        r.presetName = "shiny";
        r.albedoColor = color;
        r.roughness = 0.2f;
        r.metallic = 0.8f;

        auto& i = m_registry->AddComponent<Scene::InteractableComponent>(obj);
        i.type = Scene::InteractionType::Pickup;
        i.highlightColor = glm::vec3(1.0f, 1.0f, 0.5f);
        i.interactionRadius = radius * 2.0f;
        i.isHighlighted = false;

        auto& p = m_registry->AddComponent<Scene::PhysicsBodyComponent>(obj);
        p.velocity = glm::vec3(0.0f);
        p.angularVelocity = glm::vec3(0.0f);
        p.mass = 1.0f;
        p.restitution = 0.5f;
        p.friction = 0.4f;
        p.useGravity = true;
        p.isKinematic = false;
    };

    // Place collectible orbs near spawn
    spawnInteractable("RedOrb", 5.0f, 8.0f, 0.4f, glm::vec4(0.9f, 0.2f, 0.1f, 1.0f));
    spawnInteractable("BlueOrb", -6.0f, 10.0f, 0.35f, glm::vec4(0.1f, 0.3f, 0.9f, 1.0f));
    spawnInteractable("GreenOrb", 8.0f, -5.0f, 0.45f, glm::vec4(0.2f, 0.9f, 0.3f, 1.0f));
    spawnInteractable("GoldOrb", -4.0f, -8.0f, 0.5f, glm::vec4(1.0f, 0.8f, 0.2f, 1.0f));
    spawnInteractable("PurpleOrb", 12.0f, 3.0f, 0.38f, glm::vec4(0.7f, 0.2f, 0.9f, 1.0f));

    if (m_renderer) {
        m_worldState.Update(0.0f);
        Graphics::ApplyOutdoorWorldSceneControls(*m_renderer,
                                                 m_worldState.sunDirection,
                                                 m_worldState.sunColor,
                                                 m_worldState.sunIntensity);
    }

    spdlog::info("=== TERRAIN WORLD READY ===");
    spdlog::info("  {} terrain chunks", chunkCount);
    spdlog::info("  {} trees, {} rocks", treeCount, rockCount);
    spdlog::info("  Time: {:.1f}h - Press ./,/L to control time", m_worldState.timeOfDay);
    spdlog::info("  Press F5 for play mode, WASD to move, E to interact");
    spdlog::info("  Press J to exit terrain world");
}

void Engine::BuildEditorModeTerrainScene() {
    // Minimal scene setup for Engine Editor Mode - EditorWorld handles terrain chunks
    // We just need camera, sun, and basic settings

    // Enable terrain system for player physics
    m_terrainEnabled = true;

    // Get terrain params from EditorWorld if available (so physics matches rendered terrain)
    if (m_editorModeController && m_editorModeController->GetWorld()) {
        m_terrainParams = m_editorModeController->GetWorld()->GetTerrainParams();
    } else {
        // Fallback to default params
        m_terrainParams = Scene::TerrainNoiseParams{};
        m_terrainParams.seed = 42;
        m_terrainParams.amplitude = 20.0f;
        m_terrainParams.frequency = 0.003f;
        m_terrainParams.octaves = 6;
        m_terrainParams.lacunarity = 2.0f;
        m_terrainParams.gain = 0.5f;
        m_terrainParams.warp = 15.0f;
    }

    // Create camera at origin, sample terrain height for starting position
    {
        entt::entity camera = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(camera, "MainCamera");

        float startY = Scene::SampleTerrainHeight(0.0, 0.0, m_terrainParams) + 2.0f;

        auto& transform = m_registry->AddComponent<TransformComponent>(camera);
        transform.position = glm::vec3(0.0f, startY, 0.0f);
        glm::vec3 forward = glm::normalize(glm::vec3(0.0f, 0.0f, 1.0f));
        transform.rotation = glm::quatLookAtLH(forward, glm::vec3(0.0f, 1.0f, 0.0f));

        auto& cam = m_registry->AddComponent<Scene::CameraComponent>(camera);
        cam.fov = 75.0f;
        cam.nearPlane = 0.1f;
        cam.farPlane = 1500.0f;
        cam.isActive = true;

        m_activeCameraEntity = camera;
    }

    // Directional sun light
    {
        entt::entity sun = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(sun, "Sun");

        auto& transform = m_registry->AddComponent<TransformComponent>(sun);
        transform.position = glm::vec3(500.0f, 800.0f, 300.0f);
        glm::vec3 sunDir = glm::normalize(glm::vec3(-0.3f, -0.85f, -0.4f));
        transform.rotation = glm::quatLookAtLH(sunDir, glm::vec3(0.0f, 1.0f, 0.0f));

        auto& light = m_registry->AddComponent<Scene::LightComponent>(sun);
        light.type = Scene::LightType::Directional;
        light.color = glm::vec3(1.0f, 0.95f, 0.8f);
        light.intensity = 4.0f;
        light.castsShadows = true;
    }

    if (m_renderer) {
        m_worldState.Update(0.0f);
        Graphics::ApplyOutdoorWorldSceneControls(*m_renderer,
                                                 m_worldState.sunDirection,
                                                 m_worldState.sunColor,
                                                 m_worldState.sunIntensity);
    }

    spdlog::info("=== EDITOR MODE TERRAIN READY ===");
    spdlog::info("  EditorWorld handles terrain chunks");
    spdlog::info("  Time: {:.1f}h - Press ./,/L to control time", m_worldState.timeOfDay);
    spdlog::info("  Press F5 for play mode, WASD to move, Space to jump");
}

} // namespace Cortex
