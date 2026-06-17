#include "Graphics/Subsystems/ParticleSubsystem.h"

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

void ParticleSubsystem::Render(Scene::ECS_Registry* registry, const ParticleRenderContext& ctx) {
    m_state.ResetFrameStats();
    if (ctx.deviceRemoved || !registry || !ctx.particlePipeline || !ctx.hdrColor || m_state.instanceMapFailed) {
        return;
    }

    static constexpr uint32_t kMaxParticleInstances = 4096;
    const auto& controls = m_state.controls;
    auto& frame = m_state.frame;
    auto& resources = m_state.resources;
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

    const FrustumPlanes frustum = ExtractFrustumPlanesCPU(ctx.viewProjectionNoJitter);

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

    ID3D12Device* device = ctx.device;
    if (!device) {
        return;
    }

    const UINT instanceCount = static_cast<UINT>(particleOffset);
    frame.frameSubmittedInstances = instanceCount;
    const UINT requiredCapacity = std::min<UINT>(scaledMaxInstances, instanceCount + 64u);
    const UINT minCapacity = 256;

    if (resources.NeedsInstanceCapacity(requiredCapacity) && resources.instanceBuffer) {
        if (ctx.waitForGpu) {
            ctx.waitForGpu();
        }
    }
    const HRESULT bufferHr = resources.EnsureInstanceBuffer(device, requiredCapacity, minCapacity);
    if (FAILED(bufferHr)) {
        spdlog::warn("RenderParticles: failed to allocate instance buffer (hr=0x{:08X})",
                     static_cast<unsigned int>(bufferHr));
        if (ctx.reportDeviceRemoved) {
            ctx.reportDeviceRemoved("RenderParticles_CreateInstanceBuffer", bufferHr);
        }
        return;
    }

    UINT bufferSize = instanceCount * sizeof(ParticleInstance);
    resources.gpuPreparedThisFrame = false;

    const HRESULT emitterHr =
        resources.EnsureGpuEmitterBuffer(device, static_cast<UINT>(gpuEmitters.size()), 16);
    if (FAILED(emitterHr)) {
        spdlog::warn("RenderParticles: failed to allocate GPU emitter buffer (hr=0x{:08X})",
                     static_cast<unsigned int>(emitterHr));
        if (ctx.reportDeviceRemoved) {
            ctx.reportDeviceRemoved("RenderParticles_CreateEmitterBuffer", emitterHr);
        }
        return;
    }

    bool gpuLifecycleSucceeded = false;
    if (ctx.particleLifecycleCompute &&
        ctx.compactComputeRootSignature &&
        resources.gpuEmitterBuffer &&
        resources.gpuInstanceBuffer &&
        resources.gpuLifecycleConstantsInitialized) {
        ParticleGpuLifecycleConstants constants{};
        constants.emitterCount = static_cast<uint32_t>(gpuEmitters.size());
        constants.particleCount = instanceCount;
        constants.time = ctx.time;
        constants.bloomContribution = bloomContribution;
        constants.softDepthFade = softDepthFade;
        constants.windInfluence = windInfluence;
        constants.cameraPosition = ctx.cameraPosition;

        ParticleGpuLifecyclePass::DispatchContext lifecycleContext{};
        lifecycleContext.device = device;
        lifecycleContext.commandList = ctx.commandList;
        lifecycleContext.rootSignature = ctx.compactComputeRootSignature;
        lifecycleContext.pipeline = ctx.particleLifecycleCompute;
        lifecycleContext.descriptorManager = ctx.descriptorManager;
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
        if (ctx.reportDeviceRemoved) {
            ctx.reportDeviceRemoved("RenderParticles_PrepareQuadVB", quadHr);
        }
        resources.quadVertexBuffer.Reset();
        return;
    }

    const D3D12_GPU_VIRTUAL_ADDRESS objAddr = ctx.allocObjectConstants ? ctx.allocObjectConstants() : 0;

    ParticleBillboardPass::TargetBindings targets{};
    targets.hdrColor = ctx.hdrColor;
    targets.hdrState = ctx.hdrState;
    targets.hdrRtv = ctx.hdrRtv;
    targets.depthBuffer = ctx.depthBuffer;
    targets.depthState = ctx.depthState;
    targets.depthDsv = ctx.depthDsv;

    ParticleBillboardPass::DrawContext drawContext{};
    drawContext.commandList = ctx.commandList;
    drawContext.rootSignature = ctx.rootSignature;
    drawContext.pipeline = ctx.particlePipeline;
    drawContext.descriptorManager = ctx.descriptorManager;
    drawContext.shadowEnvironmentTable = ctx.shadowEnvironmentTable;
    drawContext.objectConstants = objAddr;
    drawContext.resources = &resources;
    drawContext.instanceCount = instanceCount;
    drawContext.instanceBytes = bufferSize;

    if (ParticleBillboardPass::Draw(drawContext, targets)) {
        frame.frameExecuted = true;
        if (ctx.outParticleDraws) {
            ++(*ctx.outParticleDraws);
        }
        if (ctx.outParticleInstances) {
            *ctx.outParticleInstances += instanceCount;
        }
    }
}

} // namespace Cortex::Graphics
