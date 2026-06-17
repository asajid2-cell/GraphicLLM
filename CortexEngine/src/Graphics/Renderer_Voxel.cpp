#include "Renderer.h"

#include "Core/Window.h"
#include "Scene/ECS_Registry.h"

// Thin forwarders to VoxelSubsystem. Voxel grid state, scene voxelization, and
// the experimental raymarch draw now live in Graphics/Subsystems/VoxelSubsystem.
namespace Cortex::Graphics {

void Renderer::SetVoxelBackendEnabled(bool enabled) {
    m_voxel.SetBackendEnabled(enabled, m_pipelineState.voxel != nullptr);
}

bool Renderer::IsVoxelBackendEnabled() const {
    return m_voxel.IsBackendEnabled();
}

void Renderer::MarkVoxelGridDirty() {
    m_voxel.MarkGridDirty();
}

void Renderer::RenderVoxel(Scene::ECS_Registry* registry) {
    VoxelDrawContext ctx{};
    ctx.device = m_services.device ? m_services.device->GetDevice() : nullptr;
    ctx.commandList = m_commandResources.graphicsList.Get();
    ctx.descriptorManager = m_services.descriptorManager.get();
    ctx.rootSignature = m_pipelineState.rootSignature.get();
    ctx.pipeline = m_pipelineState.voxel.get();
    ctx.frameConstants = m_constantBuffers.currentFrameGPU;
    if (m_services.window) {
        ctx.backBuffer = m_services.window->GetCurrentBackBuffer();
        ctx.backBufferRtv = m_services.window->GetCurrentRTV();
        ctx.width = m_services.window->GetWidth();
        ctx.height = m_services.window->GetHeight();
    }

    if (m_voxel.Render(registry, ctx)) {
        m_frameLifecycle.backBufferUsedAsRTThisFrame = true;
        RecordFramePass("RenderVoxel",
                        true,
                        true,
                        1,
                        {"frame_constants", "voxel_grid"},
                        {"back_buffer"});
    }
}

Result<void> Renderer::BuildVoxelGridFromScene(Scene::ECS_Registry* registry) {
    return m_voxel.BuildGridFromScene(registry,
                                      m_services.device ? m_services.device->GetDevice() : nullptr,
                                      m_services.descriptorManager.get());
}

Result<void> Renderer::UploadVoxelGridToGPU() {
    return m_voxel.UploadGridToGPU(m_services.device ? m_services.device->GetDevice() : nullptr,
                                   m_services.descriptorManager.get());
}

} // namespace Cortex::Graphics
