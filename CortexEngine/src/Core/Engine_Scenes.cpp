// Scene construction helpers for Cortex Engine.
// Cornell box + hero "Dragon Over Water Studio" layouts.

#include "Engine.h"

#include "Scene/Components.h"
#include "Scene/TerrainNoise.h"
#include "Utils/MeshGenerator.h"
#include "Utils/GLTFLoader.h"
#include "Graphics/Renderer.h"

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
        BuildProceduralTerrainScene();
        break;
    case ScenePreset::RTShowcase:
    case ScenePreset::GodRays: // currently shares layout with RTShowcase
    default:
        BuildRTShowcaseScene();
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
    case ScenePreset::GodRays:           presetName = "God Rays Atrium"; break;
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
        spdlog::info("Asset memory breakdown after rebuild: tex≈{:.0f} MB env≈{:.0f} MB geom≈{:.0f} MB RT≈{:.0f} MB",
                     texMB, envMB, geomMB, rtMB);

        auto heavyTex = m_renderer->GetAssetRegistry().GetHeaviestTextures(3);
        if (!heavyTex.empty()) {
            spdlog::info("Top textures by estimated GPU bytes:");
            for (const auto& t : heavyTex) {
                const double mb = static_cast<double>(t.bytes) / (1024.0 * 1024.0);
                spdlog::info("  {} ≈ {:.1f} MB", t.key, mb);
            }
        }
        auto heavyMesh = m_renderer->GetAssetRegistry().GetHeaviestMeshes(3);
        if (!heavyMesh.empty()) {
            spdlog::info("Top meshes by estimated GPU bytes:");
            for (const auto& m : heavyMesh) {
                const double mb = static_cast<double>(m.bytes) / (1024.0 * 1024.0);
                spdlog::info("  {} ≈ {:.1f} MB", m.key, mb);
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
        cameraTransform.rotation = glm::quatLookAt(forward, up);
    }

    auto& camera = m_registry->AddComponent<Scene::CameraComponent>(cameraEntity);
    camera.fov = 50.0f;
    camera.isActive = true;

    // Lighting: sun oriented downward plus a simple interior light rig. The
    // interior spots approximate a ceiling area light and a small rim light
    // so reflections and RT GI have strong local contrast.
    if (renderer) {
        renderer->SetSunDirection(glm::normalize(glm::vec3(0.0f, -1.0f, 0.0f)));
        renderer->SetSunColor(glm::vec3(1.0f));
        renderer->SetSunIntensity(2.0f);
        renderer->SetEnvironmentPreset("studio");
        renderer->SetIBLEnabled(true);
        // Subtle volumetric fog and god-rays for the Cornell top light so the
        // interior feels more atmospheric without overwhelming the small box.
        renderer->SetFogEnabled(true);
        renderer->SetFogParams(0.03f, 0.0f, 0.55f);
        renderer->SetGodRayIntensity(0.9f);
        // Keep water parameters gentle; the Cornell "puddle" is a shallow,
        // mostly still surface used for specular highlights and SSR.
        renderer->SetWaterParams(
            0.0f,   // levelY
            0.015f, // amplitude
            4.0f,   // wavelength
            0.5f,   // speed
            1.0f, 0.0f,
            0.01f); // secondaryAmplitude
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
        t.rotation = glm::quatLookAt(glm::normalize(dir), glm::vec3(0.0f, 0.0f, 1.0f));

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
        t.rotation = glm::quatLookAt(glm::normalize(dir), glm::vec3(0.0f, 0.0f, 1.0f));

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
        t.rotation = glm::quatLookAt(glm::normalize(dir), glm::vec3(0.0f, 1.0f, 0.0f));

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

void Engine::BuildRTShowcaseScene() {
    spdlog::info("Building hero scene: RT Showcase Gallery");

    auto* renderer = m_renderer.get();

    // In conservative mode on 8 GB-class GPUs, disable particles for this
    // scene to keep VRAM and per-frame work within a safer envelope.
    if (renderer && m_device) {
        const std::uint64_t bytes = m_device->GetDedicatedVideoMemoryBytes();
        const std::uint64_t mb = bytes / (1024ull * 1024ull);
        if (m_qualityMode == EngineConfig::QualityMode::Conservative &&
            mb > 0 && mb <= 8192ull) {
            renderer->SetParticlesEnabled(false);
        }
    }

    // Global renderer defaults for the RT showcase. IBL and lighting are
    // configured for the gallery in all modes, but heavy quality settings
    // (higher internal resolution, SSR/SSAO/fog, strong bloom/god-rays) are
    // only enabled when the engine was started in a high-quality mode.
    if (renderer) {
        renderer->SetEnvironmentPreset("studio");
        renderer->SetIBLEnabled(true);
        renderer->SetIBLIntensity(0.9f, 1.2f);

        renderer->SetShadowsEnabled(true);
        renderer->SetShadowBias(0.0005f);
        renderer->SetShadowPCFRadius(1.5f);
        renderer->SetCascadeSplitLambda(0.5f);

        // Single sun direction chosen to produce long gallery shadows, glancing
        // pool reflections, and beams through the atrium windows.
        glm::vec3 sunDir = glm::normalize(glm::vec3(0.35f, 0.85f, 0.25f));
        renderer->SetSunDirection(sunDir);
        renderer->SetSunColor(glm::vec3(1.0f));
        renderer->SetSunIntensity(4.5f);

        // Courtyard water tuning: modest waves and clear reflections.
        renderer->SetWaterParams(
            /*levelY*/ 0.0f,
            /*amplitude*/ 0.15f,
            /*waveLength*/ 10.0f,
            /*speed*/ 1.0f,
            /*dirX*/ 1.0f,
            /*dirZ*/ 0.25f,
            /*secondaryAmplitude*/ 0.08f,
            /*steepness*/ 0.6f);

        if (m_qualityMode == EngineConfig::QualityMode::Default) {
            // High-quality RT showcase: request a slightly reduced internal
            // resolution (clamped to ≈0.8 at 1440p with heavy effects), plus
            // full TAA/FXAA, SSR/SSAO, and atmospheric fog/god-rays.
            renderer->SetRenderScale(0.85f);
            renderer->SetExposure(1.2f);
            renderer->SetBloomIntensity(0.35f);

            renderer->SetFXAAEnabled(true);
            renderer->SetTAAEnabled(true);
            renderer->SetSSREnabled(true);
            renderer->SetSSAOEnabled(true);

            renderer->SetFogEnabled(true);
            renderer->SetFogParams(0.03f, 0.0f, 0.45f);
            renderer->SetGodRayIntensity(1.8f);
        } else {
            // Conservative mode: enable all graphics features for maximum quality
            renderer->SetRenderScale(1.0f);
            renderer->SetExposure(1.1f);
            renderer->SetBloomIntensity(0.25f);

            renderer->SetFXAAEnabled(true);
            renderer->SetTAAEnabled(true);
            renderer->SetSSREnabled(true);
            renderer->SetSSAOEnabled(true);
            renderer->SetFogEnabled(true);
            renderer->SetShadowsEnabled(true);
            renderer->SetIBLEnabled(true);
        }

        // Leave ray tracing disabled by default; the user can toggle it
        // explicitly (V key / debug menu) once the scene is up so that any
        // DXR issues do not prevent the engine from becoming interactive.

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

    // Camera positioned at a central hub looking toward the three zones.
    {
        entt::entity camEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(camEntity, "MainCamera");
        auto& t = m_registry->AddComponent<TransformComponent>(camEntity);
        t.position = glm::vec3(0.0f, 3.5f, -13.0f);
        glm::vec3 target(0.0f, 1.5f, 0.0f);
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        t.rotation = glm::quatLookAt(glm::normalize(target - t.position), up);

        auto& cam = m_registry->AddComponent<Scene::CameraComponent>(camEntity);
        cam.fov = 55.0f;
        cam.isActive = true;
        m_activeCameraEntity = camEntity;
    }

    // --------------------
    // Zone A: Reflective gallery (x < 0)
    // --------------------
    const float galleryX = -14.0f;

    if (floorPlane && floorPlane->gpuBuffers) {
        // Floor
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_Floor");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(galleryX, 0.0f, 0.0f);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = floorPlane;
        r.albedoColor = glm::vec4(0.32f, 0.24f, 0.16f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.55f;
        r.ao = 1.0f;
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

    if (wallPlane && wallPlane->gpuBuffers) {
        // Left wall (brick)
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_LeftWall");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(galleryX - 10.0f, 2.0f, 0.0f);
        t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), glm::half_pi<float>(), 0.0f));

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = wallPlane;
        r.albedoColor = glm::vec4(0.4f, 0.4f, 0.42f, 1.0f);
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
        r.albedoColor = glm::vec4(0.86f, 0.86f, 0.9f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.7f;
        r.ao = 1.0f;
        r.presetName = "backdrop";
        r.doubleSided = true;
        r.textures.albedoPath = "assets/textures/rtshowcase/rt_gallery_rightwall_albedo.dds";
        r.textures.normalPath = "assets/textures/rtshowcase/rt_gallery_rightwall_normal_bc5.dds";
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
            r.albedoColor = glm::vec4(0.8f, 0.8f, 0.85f, 1.0f);
            r.metallic = 1.0f;
            r.roughness = 0.03f;
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
            bool allowDragonUpload = true;
            // On 8 GB-class adapters (or any time RT is enabled during init),
            // the RT showcase scene creates extreme memory pressure during the
            // first ~10 frames while BLAS structures are building. Defer the
            // large dragon mesh upload to avoid device-removed errors. The mesh
            // can be loaded later via LLM commands or scene switching.
            //
            // Root cause: ProcessGpuJobsPerFrame() uploads dragon (4.5 MB) while
            // BuildTLAS() allocates BLAS scratch buffers (10s-100s of MB), causing
            // CreateCommittedResource to fail with DEVICE_REMOVED during Present().
            if (m_device) {
                const std::uint64_t bytes = m_device->GetDedicatedVideoMemoryBytes();
                const std::uint64_t mb = bytes / (1024ull * 1024ull);
                // Skip dragon on ≤8GB cards, or if RT is enabled (to avoid init-time OOM)
                if (mb > 0 && mb <= 8192ull) {
                    allowDragonUpload = false;
                    spdlog::info("RTShowcase: skipping dragon mesh upload on 8 GB card to prevent device-removed during RT warm-up");
                }
                // Also skip if ray tracing is active during scene init, regardless of VRAM
                if (renderer && renderer->IsRayTracingEnabled()) {
                    allowDragonUpload = false;
                    spdlog::info("RTShowcase: deferring dragon mesh upload (RT enabled; avoiding init-time memory spike)");
                }
            }

            if (allowDragonUpload) {
                auto enqueue = renderer->EnqueueMeshUpload(dragonMesh, "RTShowcaseDragon");
                if (enqueue.IsErr()) {
                    spdlog::warn("Failed to enqueue RTShowcase dragon mesh upload: {}", enqueue.Error());
                    dragonMesh.reset();
                }
            } else {
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
        dt.scale = glm::vec3(1.0f);

        auto& dr = m_registry->AddComponent<Scene::RenderableComponent>(de);
        dr.mesh = dragonMesh;
        dr.albedoColor = glm::vec4(0.75f, 0.75f, 0.8f, 1.0f);
        dr.metallic = 1.0f;
        dr.roughness = 0.08f;
        dr.ao = 1.0f;
        dr.presetName = "chrome";
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
        sr.albedoColor = glm::vec4(0.9f, 0.9f, 0.95f, 1.0f);
        sr.metallic = 1.0f;
        sr.roughness = 0.04f;
        sr.ao = 1.0f;
        sr.presetName = "chrome";
    }

    // Gallery lights: warm key, cool rim
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_KeyLight");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(galleryX + 3.0f, 3.5f, -3.0f);
        glm::vec3 dir(-0.4f, -0.8f, 0.6f);
        t.rotation = glm::quatLookAt(glm::normalize(dir), glm::vec3(0.0f, 1.0f, 0.0f));

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Spot;
        l.color = glm::vec3(1.0f, 0.95f, 0.85f);
        l.intensity = 12.0f;
        l.range = 30.0f;
        l.innerConeDegrees = 22.0f;
        l.outerConeDegrees = 40.0f;
        l.castsShadows = true;
    }

    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_RimLight");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(galleryX - 6.0f, 3.0f, 3.0f);
        glm::vec3 dir(0.2f, -0.6f, -1.0f);
        t.rotation = glm::quatLookAt(glm::normalize(dir), glm::vec3(0.0f, 1.0f, 0.0f));

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Spot;
        l.color = glm::vec3(0.8f, 0.9f, 1.0f);
        l.intensity = 6.0f;
        l.range = 25.0f;
        l.innerConeDegrees = 24.0f;
        l.outerConeDegrees = 42.0f;
        l.castsShadows = false;
    }

    // Dragon fire emitter near the gallery dragon's mouth. This uses the
    // shared CPU-driven particle system and renders as small emissive
    // billboards that are bright enough to feed bloom and RT reflections.
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_FireEmitter");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        // Positioned slightly above and in front of the dragon plinth so
        // particles travel upward and outward into the gallery space.
        t.position = glm::vec3(galleryX + 0.4f, 1.4f, 2.0f);

        Scene::ParticleEmitterComponent emitter;
        emitter.type = Scene::ParticleEmitterType::Fire;
        emitter.rate = 80.0f;          // steady stream
        emitter.lifetime = 0.8f;       // short, flame-like
        emitter.initialVelocity = glm::vec3(0.0f, 3.0f, 2.0f);
        emitter.velocityRandom  = glm::vec3(0.7f, 0.8f, 0.7f);
        emitter.sizeStart = 0.08f;
        emitter.sizeEnd   = 0.40f;
        // High-intensity warm colors so particles act as emissive sources.
        emitter.colorStart = glm::vec4(5.0f, 2.4f, 0.8f, 0.9f);
        emitter.colorEnd   = glm::vec4(0.6f, 0.15f, 0.0f, 0.0f);
        // Positive gravity here accelerates particles upward, giving a rising
        // flame motion without needing additional forces.
        emitter.gravity = 0.8f;
        emitter.localSpace = true;

        m_registry->AddComponent<Scene::ParticleEmitterComponent>(e, emitter);
    }

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
        r.albedoColor = glm::vec4(0.4f, 0.4f, 0.42f, 1.0f);
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
        wr.albedoColor = glm::vec4(0.02f, 0.09f, 0.13f, 0.7f);
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
        er.albedoColor = glm::vec4(7.0f, 6.0f, 4.0f, 1.0f);
        er.metallic = 0.0f;
        er.roughness = 0.2f;
        er.ao = 1.0f;
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
        l.intensity = 4.0f;
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
        r.albedoColor = glm::vec4(0.28f, 0.28f, 0.3f, 1.0f);
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
            r.albedoColor = glm::vec4(0.12f, 0.12f, 0.14f, 1.0f);
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
            r.albedoColor = glm::vec4(0.2f, 0.2f, 0.24f, 1.0f);
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
        r.albedoColor = glm::vec4(0.4f, 0.4f, 0.42f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.85f;
        r.ao = 1.0f;
        r.presetName = "matte";
    }

    // Dust / mote particle emitter near the light shafts.
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Atrium_DustEmitter");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(atriumX - 2.5f, 2.0f, -0.5f);

        Scene::ParticleEmitterComponent emitter;
        emitter.type = Scene::ParticleEmitterType::Smoke;
        emitter.rate = 40.0f;
        emitter.lifetime = 5.0f;
        emitter.initialVelocity = glm::vec3(0.0f, 0.4f, 0.0f);
        emitter.velocityRandom = glm::vec3(0.15f, 0.2f, 0.15f);
        emitter.sizeStart = 0.06f;
        emitter.sizeEnd = 0.18f;
        emitter.colorStart = glm::vec4(0.9f, 0.9f, 0.9f, 0.25f);
        emitter.colorEnd   = glm::vec4(0.9f, 0.95f, 1.0f, 0.0f);
        emitter.gravity = -0.1f;
        emitter.localSpace = true;
        m_registry->AddComponent<Scene::ParticleEmitterComponent>(e, emitter);
    }

    // Small highlight light in the atrium
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Atrium_SculptureLight");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(atriumX + 2.0f, 3.0f, 0.0f);
        glm::vec3 dir(-0.4f, -1.0f, 0.1f);
        t.rotation = glm::quatLookAt(glm::normalize(dir), glm::vec3(0.0f, 1.0f, 0.0f));

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Spot;
        l.color = glm::vec3(1.0f, 0.95f, 0.9f);
        l.intensity = 5.0f;
        l.range = 15.0f;
        l.innerConeDegrees = 20.0f;
        l.outerConeDegrees = 35.0f;
        l.castsShadows = false;
    }
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
        t.rotation = glm::quatLookAt(glm::normalize(focus - t.position),
                                     glm::vec3(0.0f, 1.0f, 0.0f));

        auto& cam = m_registry->AddComponent<Scene::CameraComponent>(cameraEntity);
        cam.fov = 55.0f;
        cam.isActive = true;
    }

    // Global lighting / environment tuned for strong god rays over a reflective
    // pool. We enable fog and increase god-ray intensity so beams through the
    // atrium windows and across the water surface are clearly visible.
    if (renderer) {
        renderer->SetEnvironmentPreset("studio");
        renderer->SetIBLEnabled(true);
        renderer->SetIBLIntensity(0.75f, 1.1f);

        renderer->SetShadowsEnabled(true);
        renderer->SetShadowBias(0.0005f);
        renderer->SetShadowPCFRadius(1.5f);
        renderer->SetCascadeSplitLambda(0.5f);

        glm::vec3 sunDir = glm::normalize(glm::vec3(0.45f, 0.75f, 0.15f));
        renderer->SetSunDirection(sunDir);
        renderer->SetSunColor(glm::vec3(1.0f));
        renderer->SetSunIntensity(4.0f);

        renderer->SetFogEnabled(true);
        renderer->SetFogParams(
            /*density*/ 0.045f,
            /*baseHeight*/ 0.0f,
            /*falloff*/ 0.65f);
        renderer->SetGodRayIntensity(2.0f);

        // Slow, gentle waves for a shallow indoor pool.
        renderer->SetWaterParams(
            /*levelY*/ 0.0f,
            /*amplitude*/ 0.05f,
            /*waveLength*/ 8.0f,
            /*speed*/ 0.5f,
            /*dirX*/ 1.0f,
            /*dirZ*/ 0.2f,
            /*secondaryAmplitude*/ 0.02f,
            /*steepness*/ 0.5f);
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
        return glm::quatLookAt(fwd, up);
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

void Engine::SetCameraToSceneDefault(Scene::TransformComponent& transform) {
    glm::vec3 pos;
    glm::vec3 target;

    if (m_currentScenePreset == ScenePreset::CornellBox) {
        pos = glm::vec3(0.0f, 1.6f, -3.0f);
        target = glm::vec3(0.0f, 1.2f, 0.0f);
    } else if (m_currentScenePreset == ScenePreset::RTShowcase) {
        pos = glm::vec3(0.0f, 3.5f, -13.0f);
        target = glm::vec3(0.0f, 1.5f, 0.0f);
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
    transform.rotation = glm::quatLookAt(forward, up);

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
        transform.rotation = glm::quatLookAt(forward, glm::vec3(0.0f, 1.0f, 0.0f));

        auto& cam = m_registry->AddComponent<Scene::CameraComponent>(camera);
        cam.fov = 75.0f;  // Wider FOV for exploration
        cam.nearPlane = 0.1f;
        cam.farPlane = 1500.0f;

        m_activeCameraEntity = camera;
    }

    // Create terrain chunks - larger world
    const int chunkRadius = 3;  // Reduced for descriptor budget
    const uint32_t gridDim = 64;
    const float chunkSize = 64.0f;
    int chunkCount = 0;

    for (int cz = -chunkRadius; cz <= chunkRadius; ++cz) {
        for (int cx = -chunkRadius; cx <= chunkRadius; ++cx) {
            entt::entity chunk = m_registry->CreateEntity();

            char tagName[64];
            snprintf(tagName, sizeof(tagName), "TerrainChunk_%d_%d", cx, cz);
            m_registry->AddComponent<Scene::TagComponent>(chunk, tagName);

            auto& transform = m_registry->AddComponent<TransformComponent>(chunk);
            transform.position = glm::vec3(0.0f);
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
        transform.rotation = glm::quatLookAt(sunDir, glm::vec3(0.0f, 1.0f, 0.0f));

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

    // Configure renderer for outdoor world
    if (m_renderer) {
        m_renderer->SetIBLEnabled(false);  // Disable indoor cubemap
        // IBL disabled - use sun lighting only
        m_renderer->SetFogEnabled(true);
        m_renderer->SetExposure(1.0f);
        m_renderer->SetShadowsEnabled(true);
    }

    spdlog::info("=== TERRAIN WORLD READY ===");
    spdlog::info("  {} terrain chunks", chunkCount);
    spdlog::info("  {} trees, {} rocks", treeCount, rockCount);
    spdlog::info("  Press F5 for play mode, WASD to move, E to interact");
    spdlog::info("  Press J to exit terrain world");
}

} // namespace Cortex
