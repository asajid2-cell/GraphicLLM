#include "Renderer.h"

#include "Graphics/Passes/ParticleBillboardPass.h"
#include "Graphics/Passes/ParticleGpuLifecyclePass.h"
#include "Graphics/RendererGeometryUtils.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

#define CORTEX_REPORT_DEVICE_REMOVED(ctx, hr) \
    ReportDeviceRemoved((ctx), (hr), __FILE__, __LINE__)
void Renderer::RenderParticles(Scene::ECS_Registry* registry) {
    m_particleState.ResetFrameStats();
    if (m_frameLifecycle.deviceRemoved || !registry || !m_pipelineState.particle || !m_mainTargets.hdr.resources.color || m_particleState.instanceMapFailed) {
        return;
    }

    // Cap the number of particles we draw in a single frame to keep the
    // per-frame instance buffer small and avoid pathological memory usage if
    // an emitter accidentally spawns an excessive number of particles.
    static constexpr uint32_t kMaxParticleInstances = 4096;
    const auto& controls = m_particleState.controls;
    auto& frame = m_particleState.frame;
    auto& resources = m_particleState.resources;
    const float densityScale = std::clamp(controls.densityScale, 0.0f, 2.0f);
    const float qualityScale = std::clamp(controls.qualityScale, 0.25f, 2.0f);
    const float bloomContribution = std::clamp(controls.bloomContribution, 0.0f, 2.0f);
    const float softDepthFade = std::clamp(controls.softDepthFade, 0.0f, 1.0f);
    const float windInfluence = std::clamp(controls.windInfluence, 0.0f, 2.0f);
    frame.frameDensityScale = densityScale;
    frame.frameQualityScale = qualityScale;
    frame.frameBloomContribution = bloomContribution;
    frame.frameSoftDepthFade = softDepthFade;
    frame.frameWindInfluence = windInfluence;
    if (densityScale <= 0.0f) {
        frame.frameMaxInstances = 0;
        return;
    }

    const uint32_t scaledMaxInstances = static_cast<uint32_t>(
        std::clamp(densityScale * qualityScale * static_cast<float>(kMaxParticleInstances),
                   1.0f,
                   static_cast<float>(kMaxParticleInstances * 2u)));
    frame.frameMaxInstances = scaledMaxInstances;

    auto view = registry->View<Scene::ParticleEmitterComponent, Scene::TransformComponent>();
    if (view.begin() == view.end()) {
        return;
    }

    std::vector<ParticleGpuEmitter> gpuEmitters;
    gpuEmitters.reserve(32);
    uint32_t particleOffset = 0;

    const FrustumPlanes frustum = ExtractFrustumPlanesCPU(m_constantBuffers.frameCPU.viewProjectionNoJitter);

    for (auto entity : view) {
        auto& emitter   = view.get<Scene::ParticleEmitterComponent>(entity);
        auto& transform = view.get<Scene::TransformComponent>(entity);
        ++frame.frameEmitterCount;
        if (controls.effectPreset == "gallery_mix" ||
            emitter.effectPresetId == controls.effectPreset) {
            ++frame.framePresetMatchedEmitters;
        } else {
            ++frame.framePresetMismatchedEmitters;
        }

        glm::vec3 emitterWorldPos = glm::vec3(transform.worldMatrix[3]);

        // Conservative per-emitter frustum culling. This is most meaningful for
        // local-space emitters; for world-space emitters we still do per-particle
        // culling below.
        if (emitter.localSpace) {
            const float maxSpeed =
                glm::length(emitter.initialVelocity) + glm::length(emitter.velocityRandom);
            const float conservativeRadius =
                glm::max(0.5f, maxSpeed * emitter.lifetime + glm::max(emitter.sizeStart, emitter.sizeEnd));
            if (!SphereIntersectsFrustumCPU(frustum, emitterWorldPos, conservativeRadius)) {
                continue;
            }
        }

        const float emitterBudget = glm::max(1.0f, glm::max(emitter.rate, 0.0f) * glm::max(emitter.lifetime, 0.1f) * qualityScale);
        uint32_t emitterCount = static_cast<uint32_t>(std::ceil(emitterBudget));
        const uint32_t remainingBudget = scaledMaxInstances - particleOffset;
        if (emitterCount > remainingBudget) {
            emitterCount = remainingBudget;
            frame.frameCapped = true;
        }
        if (emitterCount == 0) {
            break;
        }

        ParticleGpuEmitter gpuEmitter{};
        gpuEmitter.positionRate = glm::vec4(emitterWorldPos, glm::max(emitter.rate, 0.001f));
        gpuEmitter.initialVelocityLifetime = glm::vec4(emitter.initialVelocity, glm::max(emitter.lifetime, 0.1f));
        gpuEmitter.velocityRandomGravity = glm::vec4(emitter.velocityRandom, emitter.gravity);
        gpuEmitter.sizeLocalType = glm::vec4(emitter.sizeStart,
                                             emitter.sizeEnd,
                                             emitter.localSpace ? 1.0f : 0.0f,
                                             static_cast<float>(static_cast<uint32_t>(emitter.type)));
        gpuEmitter.colorStart = emitter.colorStart;
        gpuEmitter.colorEnd = emitter.colorEnd;
        gpuEmitter.offsetCountSeed = glm::vec4(static_cast<float>(particleOffset),
                                               static_cast<float>(emitterCount),
                                               static_cast<float>(entt::to_integral(entity) & 0xffffu),
                                               0.0f);
        gpuEmitters.push_back(gpuEmitter);
        particleOffset += emitterCount;
        frame.frameLiveParticles += emitterCount;

        if (particleOffset >= scaledMaxInstances) {
            break;
        }
    }

    if (gpuEmitters.empty() || particleOffset == 0) {
        return;
    }

    ID3D12Device* device = m_services.device->GetDevice();
    if (!device) {
        return;
    }

    const UINT instanceCount = static_cast<UINT>(particleOffset);
    frame.frameSubmittedInstances = instanceCount;
    const UINT requiredCapacity = std::min<UINT>(scaledMaxInstances, instanceCount + 64u);
    const UINT minCapacity = 256;

    if (resources.NeedsInstanceCapacity(requiredCapacity) && resources.instanceBuffer) {
        WaitForGPU();
    }
    const HRESULT bufferHr = resources.EnsureInstanceBuffer(device, requiredCapacity, minCapacity);
    if (FAILED(bufferHr)) {
        spdlog::warn("RenderParticles: failed to allocate instance buffer (hr=0x{:08X})",
                     static_cast<unsigned int>(bufferHr));
        CORTEX_REPORT_DEVICE_REMOVED("RenderParticles_CreateInstanceBuffer", bufferHr);
        return;
    }

    UINT bufferSize = instanceCount * sizeof(ParticleInstance);
    resources.gpuPreparedThisFrame = false;

    const HRESULT emitterHr =
        resources.EnsureGpuEmitterBuffer(device, static_cast<UINT>(gpuEmitters.size()), 16);
    if (FAILED(emitterHr)) {
        spdlog::warn("RenderParticles: failed to allocate GPU emitter buffer (hr=0x{:08X})",
                     static_cast<unsigned int>(emitterHr));
        CORTEX_REPORT_DEVICE_REMOVED("RenderParticles_CreateEmitterBuffer", emitterHr);
        return;
    }

    bool gpuLifecycleSucceeded = false;
    if (m_pipelineState.particleLifecycleCompute &&
        m_pipelineState.singleSrvUavComputeRootSignature &&
        resources.gpuEmitterBuffer &&
        resources.gpuInstanceBuffer &&
        resources.gpuLifecycleConstantsInitialized) {
        ParticleGpuLifecycleConstants constants{};
        constants.emitterCount = static_cast<uint32_t>(gpuEmitters.size());
        constants.particleCount = instanceCount;
        constants.time = m_constantBuffers.frameCPU.timeAndExposure.x;
        constants.bloomContribution = bloomContribution;
        constants.softDepthFade = softDepthFade;
        constants.windInfluence = windInfluence;
        constants.cameraPosition = m_constantBuffers.frameCPU.cameraPosition;

        ParticleGpuLifecyclePass::DispatchContext lifecycleContext{};
        lifecycleContext.device = device;
        lifecycleContext.commandList = m_commandResources.graphicsList.Get();
        lifecycleContext.rootSignature = m_pipelineState.singleSrvUavComputeRootSignature.Get();
        lifecycleContext.pipeline = m_pipelineState.particleLifecycleCompute.get();
        lifecycleContext.descriptorManager = m_services.descriptorManager.get();
        lifecycleContext.resources = &resources;
        lifecycleContext.emitters = gpuEmitters.data();
        lifecycleContext.emitterCount = static_cast<UINT>(gpuEmitters.size());
        lifecycleContext.particleCount = instanceCount;
        lifecycleContext.constants = &constants;

        const ParticleGpuLifecyclePass::DispatchResult lifecycleResult =
            ParticleGpuLifecyclePass::Dispatch(lifecycleContext);
        if (lifecycleResult.executed) {
            frame.frameGpuPrepared = true;
            frame.frameGpuLifecycleDispatched = true;
            frame.frameGpuSimulationDispatched = true;
            frame.frameGpuSortDispatched = true;
            frame.frameGpuDispatchGroups = lifecycleResult.dispatchGroups;
            frame.frameUploadBytes = lifecycleResult.uploadBytes;
            gpuLifecycleSucceeded = true;
        }
    }

    if (!gpuLifecycleSucceeded) {
        spdlog::warn("RenderParticles: GPU lifecycle path unavailable; particles skipped for public path");
        return;
    }

    // Persistent quad vertex buffer in an upload heap; tiny and self-contained.
    struct QuadVertex { float px, py, pz; float u, v; };
    static const QuadVertex kQuadVertices[4] = {
        { -0.5f, -0.5f, 0.0f, 0.0f, 1.0f },
        { -0.5f,  0.5f, 0.0f, 0.0f, 0.0f },
        {  0.5f, -0.5f, 0.0f, 1.0f, 1.0f },
        {  0.5f,  0.5f, 0.0f, 1.0f, 0.0f },
    };

    const HRESULT quadHr = resources.EnsureQuadVertexBuffer(device, kQuadVertices, sizeof(kQuadVertices));
    if (FAILED(quadHr)) {
        spdlog::warn("RenderParticles: failed to prepare quad vertex buffer (hr=0x{:08X})",
                     static_cast<unsigned int>(quadHr));
        CORTEX_REPORT_DEVICE_REMOVED("RenderParticles_PrepareQuadVB", quadHr);
        resources.quadVertexBuffer.Reset();
        return;
    }

    ObjectConstants obj{};
    obj.modelMatrix  = glm::mat4(1.0f);
    obj.normalMatrix = glm::mat4(1.0f);
    const auto objAddr = m_constantBuffers.object.AllocateAndWrite(obj);

    ParticleBillboardPass::TargetBindings targets{};
    targets.hdrColor = m_mainTargets.hdr.resources.color.Get();
    targets.hdrState = &m_mainTargets.hdr.resources.state;
    targets.hdrRtv = m_mainTargets.hdr.descriptors.rtv.cpu;
    targets.depthBuffer = m_depthResources.resources.buffer.Get();
    targets.depthState = &m_depthResources.resources.resourceState;
    targets.depthDsv = m_depthResources.descriptors.dsv.cpu;

    ParticleBillboardPass::DrawContext drawContext{};
    drawContext.commandList = m_commandResources.graphicsList.Get();
    drawContext.rootSignature = m_pipelineState.rootSignature.get();
    drawContext.pipeline = m_pipelineState.particle.get();
    drawContext.descriptorManager = m_services.descriptorManager.get();
    drawContext.shadowEnvironmentTable = m_environmentState.shadowAndEnvDescriptors[0];
    drawContext.objectConstants = objAddr;
    drawContext.resources = &resources;
    drawContext.instanceCount = instanceCount;
    drawContext.instanceBytes = bufferSize;

    if (ParticleBillboardPass::Draw(drawContext, targets)) {
        frame.frameExecuted = true;
        ++m_frameDiagnostics.contract.drawCounts.particleDraws;
        m_frameDiagnostics.contract.drawCounts.particleInstances += instanceCount;
    }
}

// ============================================================================
// Vegetation Rendering System
// ============================================================================

#undef CORTEX_REPORT_DEVICE_REMOVED

} // namespace Cortex::Graphics
