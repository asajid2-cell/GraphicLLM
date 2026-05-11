#include "Renderer.h"

#include "Graphics/RendererGeometryUtils.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <algorithm>
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

    std::vector<ParticleInstance> instances;
    instances.reserve(1024);

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

        for (const auto& p : emitter.particles) {
            if (p.age >= p.lifetime) {
                continue;
            }
            ++frame.frameLiveParticles;

            if (densityScale < 1.0f) {
                const float samplePhase = static_cast<float>(frame.frameLiveParticles) * densityScale;
                const float previousPhase = static_cast<float>(frame.frameLiveParticles - 1u) * densityScale;
                if (static_cast<uint32_t>(samplePhase) == static_cast<uint32_t>(previousPhase)) {
                    continue;
                }
            }

            if (instances.size() >= scaledMaxInstances) {
                frame.frameCapped = true;
                break;
            }

            ParticleInstance inst{};
            inst.position = emitter.localSpace ? (emitterWorldPos + p.position) : p.position;
            if (windInfluence > 0.0f) {
                const float ageT = (p.lifetime > 0.0f) ? std::clamp(p.age / p.lifetime, 0.0f, 1.0f) : 0.0f;
                inst.position += glm::vec3(windInfluence * ageT * 0.22f, 0.0f, windInfluence * ageT * 0.08f);
            }
            inst.size     = p.size;
            inst.color    = p.color;
            inst.color.r *= bloomContribution;
            inst.color.g *= bloomContribution;
            inst.color.b *= bloomContribution;
            inst.color.a *= std::clamp(1.0f - softDepthFade * 0.18f, 0.55f, 1.0f);

            if (!SphereIntersectsFrustumCPU(frustum, inst.position, glm::max(0.01f, inst.size))) {
                ++frame.frameFrustumCulled;
                continue;
            }

            instances.push_back(inst);
        }

        if (instances.size() >= scaledMaxInstances) {
            break;
        }
    }

    if (instances.empty()) {
        return;
    }

    ID3D12Device* device = m_services.device->GetDevice();
    if (!device) {
        return;
    }

    const UINT instanceCount = static_cast<UINT>(instances.size());
    frame.frameSubmittedInstances = instanceCount;
    const UINT requiredCapacity = instanceCount;
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

    UINT bufferSize = 0;
    const HRESULT mapHr = resources.UploadInstances(instances.data(), instanceCount, bufferSize);
    if (FAILED(mapHr)) {
        spdlog::warn("RenderParticles: failed to map instance buffer (hr=0x{:08X}); disabling particles for this run",
                     static_cast<unsigned int>(mapHr));
        // Map failures are one of the first places a hung device surfaces.
        // Capture rich diagnostics so we can see which pass/frame triggered
        // device removal.
        CORTEX_REPORT_DEVICE_REMOVED("RenderParticles_MapInstanceBuffer", mapHr);
        m_particleState.instanceMapFailed = true;
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

    // --- FIX: Bind render targets with depth buffer BEFORE setting pipeline ---
    // The particle pipeline expects DXGI_FORMAT_D32_FLOAT depth, so we MUST bind the DSV

    // 1. Transition depth buffer to write state if needed
    // 2. NEW FIX: Transition HDR Color to RENDER_TARGET (may be in PIXEL_SHADER_RESOURCE from previous pass)
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    uint32_t barrierCount = 0;

    if (m_depthResources.resources.buffer && m_depthResources.resources.resourceState != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_depthResources.resources.buffer.Get();
        barriers[barrierCount].Transition.StateBefore = m_depthResources.resources.resourceState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_depthResources.resources.resourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }

    if (m_mainTargets.hdr.resources.color && m_mainTargets.hdr.resources.state != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_mainTargets.hdr.resources.color.Get();
        barriers[barrierCount].Transition.StateBefore = m_mainTargets.hdr.resources.state;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_mainTargets.hdr.resources.state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    if (barrierCount > 0) {
        m_commandResources.graphicsList->ResourceBarrier(barrierCount, barriers);
    }

    // 3. Bind render targets (HDR color + depth)
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_mainTargets.hdr.descriptors.rtv.cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_depthResources.descriptors.dsv.cpu;
    m_commandResources.graphicsList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    m_commandResources.graphicsList->SetGraphicsRootSignature(m_pipelineState.rootSignature->GetRootSignature());
    m_commandResources.graphicsList->SetPipelineState(m_pipelineState.particle->GetPipelineState());

    if (m_environmentState.shadowAndEnvDescriptors[0].IsValid()) {
        m_commandResources.graphicsList->SetGraphicsRootDescriptorTable(4, m_environmentState.shadowAndEnvDescriptors[0].gpu);
    }

    ID3D12DescriptorHeap* heaps[] = { m_services.descriptorManager->GetCBV_SRV_UAV_Heap() };
    m_commandResources.graphicsList->SetDescriptorHeaps(1, heaps);

    ObjectConstants obj{};
    obj.modelMatrix  = glm::mat4(1.0f);
    obj.normalMatrix = glm::mat4(1.0f);
    auto objAddr = m_constantBuffers.object.AllocateAndWrite(obj);
    m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(0, objAddr);

    D3D12_VERTEX_BUFFER_VIEW vbViews[2] = {};
    vbViews[0] = resources.QuadVertexBufferView(sizeof(QuadVertex), sizeof(kQuadVertices));
    vbViews[1] = resources.InstanceBufferView(instanceCount, bufferSize);

    m_commandResources.graphicsList->IASetVertexBuffers(0, 2, vbViews);
    m_commandResources.graphicsList->IASetIndexBuffer(nullptr);
    m_commandResources.graphicsList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    m_commandResources.graphicsList->DrawInstanced(4, instanceCount, 0, 0);
    frame.frameExecuted = true;
    ++m_frameDiagnostics.contract.drawCounts.particleDraws;
    m_frameDiagnostics.contract.drawCounts.particleInstances += instanceCount;
}

// ============================================================================
// Vegetation Rendering System
// ============================================================================

#undef CORTEX_REPORT_DEVICE_REMOVED

} // namespace Cortex::Graphics
