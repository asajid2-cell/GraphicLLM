#include "Renderer.h"

#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

// VisibilityBufferSubsystem context builder + path forwarder. The VB collect /
// cull / visibility / material-resolve / deferred-lighting passes live in
// Graphics/Subsystems/VisibilityBufferSubsystem; MakeVisibilityBufferContext
// snapshots the per-frame Renderer dependencies they need.
namespace Cortex::Graphics {

VisibilityBufferContext Renderer::MakeVisibilityBufferContext() {
    VisibilityBufferContext ctx{};
    ctx.device = m_services.device;
    ctx.descriptorManager = m_services.descriptorManager.get();
    ctx.window = m_services.window;
    ctx.visibilityBuffer = m_services.visibilityBuffer.get();
    ctx.gpuCulling = m_services.gpuCulling.get();
    ctx.commandList = m_commandResources.graphicsList.Get();

    ctx.frameIndex = m_frameRuntime.frameIndex;
    ctx.renderFrameCounter = m_frameLifecycle.renderFrameCounter;
    ctx.debugViewMode = m_debugViewState.mode;
    ctx.internalRenderWidth = GetInternalRenderWidth();
    ctx.internalRenderHeight = GetInternalRenderHeight();

    ctx.gpuCullingState = &m_gpuCullingState;
    ctx.frameDiagnostics = &m_frameDiagnostics;
    ctx.framePlanning = &m_framePlanning;
    ctx.depthResources = &m_depthResources;
    ctx.mainTargets = &m_mainTargets;
    ctx.constantBuffers = &m_constantBuffers;
    ctx.lightingState = &m_lightingState;
    ctx.environment = &m_environmentState;
    ctx.materialFallbacks = &m_materialFallbacks;
    ctx.hzb = &m_hzb;
    ctx.shadows = &m_shadows;
    ctx.rtGIResource = m_rt.GITargets().color.Get();

    ctx.prepareMaterialResources = [this](Scene::RenderableComponent& renderable) {
        PrepareMaterialResources(renderable);
    };
    ctx.enqueueMeshUpload = [this](const std::shared_ptr<Scene::MeshData>& mesh, const char* label) {
        return EnqueueMeshUpload(mesh, label);
    };
    ctx.ensureEnvironmentBindlessSRVs = [this](EnvironmentMaps& env) {
        EnsureEnvironmentBindlessSRVs(env);
    };
    ctx.buildSceneLocalEnvironmentV3PayloadParams = [this]() {
        return BuildSceneLocalEnvironmentV3PayloadParams();
    };
    ctx.buildCinematicStabilityParams = [this]() {
        return BuildCinematicStabilityParams();
    };
    return ctx;
}

void Renderer::RenderVisibilityBufferPath(Scene::ECS_Registry* registry) {
    m_vb.RenderVisibilityBufferPath(registry, MakeVisibilityBufferContext());
}

} // namespace Cortex::Graphics
