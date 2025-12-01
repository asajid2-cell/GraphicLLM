// Scene construction helpers for Cortex Engine.
// Cornell box + hero "Dragon Over Water Studio" layouts.

#include "Engine.h"

#include "Scene/Components.h"
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
    // Clear all existing entities/components.
    m_registry->GetRegistry().clear();
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
    case ScenePreset::RTShowcase:
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
    case ScenePreset::CornellBox:      presetName = "Cornell Box"; break;
    case ScenePreset::DragonOverWater: presetName = "Dragon Over Water Studio"; break;
    case ScenePreset::RTShowcase:      presetName = "RT Showcase Gallery"; break;
    default:                           presetName = "Unknown"; break;
    }

    spdlog::info("Scene rebuilt as {}", presetName);
    spdlog::info("{}", m_registry->DescribeScene());
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

    // No hero character mesh in this layout; the Cornell box focuses on
    // spheres, columns, mirrors, and pure lighting/reflection behavior.

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

    // Global renderer defaults for the RT showcase: IBL + RT on, TAA/FXAA,
    // SSR/SSAO/bloom, and moderate fog/god-rays for the atrium.
    if (renderer) {
        renderer->SetEnvironmentPreset("studio");
        // Heavy RT scenes can be rendered at a slightly reduced internal
        // resolution to keep VRAM usage and shading cost in check on 8 GB
        // GPUs while preserving the full window size for the swap chain.
        renderer->SetRenderScale(0.85f);
        renderer->SetIBLEnabled(true);
        renderer->SetIBLIntensity(0.9f, 1.2f);

        renderer->SetExposure(1.2f);
        renderer->SetBloomIntensity(0.35f);

        renderer->SetShadowsEnabled(true);
        renderer->SetShadowBias(0.0005f);
        renderer->SetShadowPCFRadius(1.5f);
        renderer->SetCascadeSplitLambda(0.5f);

        renderer->SetFXAAEnabled(true);
        renderer->SetTAAEnabled(true);
        renderer->SetSSREnabled(true);
        renderer->SetSSAOEnabled(true);

        renderer->SetFogEnabled(true);
        renderer->SetFogParams(0.03f, 0.0f, 0.45f);
        renderer->SetGodRayIntensity(1.8f);
        // Leave ray tracing disabled by default; the user can toggle it
        // explicitly (V key / debug menu) once the scene is up so that any
        // DXR issues do not prevent the engine from becoming interactive.

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
            auto upload = renderer->UploadMesh(dragonMesh);
            if (upload.IsErr()) {
                spdlog::warn("Failed to upload RTShowcase dragon mesh: {}", upload.Error());
                dragonMesh.reset();
            }
        }
    } else {
        spdlog::warn("RTShowcase: failed to load DragonAttenuation: {}", dragonResult.Error());
    }

    if (dragonMesh && dragonMesh->gpuBuffers && cubeMesh && cubeMesh->gpuBuffers) {
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
    }

    if (poolPlane && poolPlane->gpuBuffers) {
        // Pool rim
        entt::entity rim = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(rim, "Courtyard_PoolRim");
        auto& rt = m_registry->AddComponent<Scene::TransformComponent>(rim);
        rt.position = glm::vec3(0.0f, 0.0f, courtyardZ);

        auto& rr = m_registry->AddComponent<Scene::RenderableComponent>(rim);
        rr.mesh = poolPlane;
        rr.albedoColor = glm::vec4(0.9f, 0.9f, 0.92f, 1.0f);
        rr.metallic = 0.0f;
        rr.roughness = 0.75f;
        rr.ao = 1.0f;
        rr.presetName = "concrete";

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

void Engine::SetCameraToSceneDefault(Scene::TransformComponent& transform) {
    glm::vec3 pos;
    glm::vec3 target;

    if (m_currentScenePreset == ScenePreset::CornellBox) {
        pos = glm::vec3(0.0f, 1.6f, -3.0f);
        target = glm::vec3(0.0f, 1.2f, 0.0f);
    } else if (m_currentScenePreset == ScenePreset::RTShowcase) {
        pos = glm::vec3(0.0f, 3.5f, -13.0f);
        target = glm::vec3(0.0f, 1.5f, 0.0f);
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

} // namespace Cortex
