#include "Renderer.h"

#include "Scene/ECS_Registry.h"

#include <glm/glm.hpp>

// Thin forwarder to ParticleSubsystem. Particle GPU buffers/lifecycle state and
// the billboard draw live in Graphics/Subsystems/ParticleSubsystem.
namespace Cortex::Graphics {

ParticleRenderContext Renderer::MakeParticleRenderContext() {
    ParticleRenderContext ctx{};
    ctx.device = m_services.device ? m_services.device->GetDevice() : nullptr;
    ctx.commandList = m_commandResources.graphicsList.Get();
    ctx.descriptorManager = m_services.descriptorManager.get();
    ctx.deviceRemoved = m_frameLifecycle.deviceRemoved;
    ctx.particlePipeline = m_pipelineState.particle.get();
    ctx.particleLifecycleCompute = m_pipelineState.particleLifecycleCompute.get();
    ctx.compactComputeRootSignature = m_pipelineState.singleSrvUavComputeRootSignature.Get();
    ctx.rootSignature = m_pipelineState.rootSignature.get();
    ctx.viewProjectionNoJitter = m_constantBuffers.frameCPU.viewProjectionNoJitter;
    ctx.time = m_constantBuffers.frameCPU.timeAndExposure.x;
    ctx.cameraPosition = m_constantBuffers.frameCPU.cameraPosition;
    ctx.hdrColor = m_mainTargets.hdr.resources.color.Get();
    ctx.hdrState = &m_mainTargets.hdr.resources.state;
    ctx.hdrRtv = m_mainTargets.hdr.descriptors.rtv.cpu;
    ctx.depthBuffer = m_depthResources.resources.buffer.Get();
    ctx.depthState = &m_depthResources.resources.resourceState;
    ctx.depthDsv = m_depthResources.descriptors.dsv.cpu;
    ctx.shadowEnvironmentTable = m_environmentState.shadowAndEnvDescriptors[0];
    ctx.allocObjectConstants = [this]() -> D3D12_GPU_VIRTUAL_ADDRESS {
        ObjectConstants obj{};
        obj.modelMatrix = glm::mat4(1.0f);
        obj.normalMatrix = glm::mat4(1.0f);
        return m_constantBuffers.object.AllocateAndWrite(obj);
    };
    ctx.waitForGpu = [this]() { WaitForGPU(); };
    ctx.reportDeviceRemoved = [this](const char* label, HRESULT hr) {
        ReportDeviceRemoved(label, hr, __FILE__, __LINE__);
    };
    ctx.outParticleDraws = &m_frameDiagnostics.contract.drawCounts.particleDraws;
    ctx.outParticleInstances = &m_frameDiagnostics.contract.drawCounts.particleInstances;
    return ctx;
}

void Renderer::RenderParticles(Scene::ECS_Registry* registry) {
    m_particles.Render(registry, MakeParticleRenderContext());
}

} // namespace Cortex::Graphics
