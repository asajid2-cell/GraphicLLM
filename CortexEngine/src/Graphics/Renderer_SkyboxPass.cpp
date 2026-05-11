#include "Renderer.h"
#include "Passes/SkyboxPass.h"

namespace Cortex::Graphics {

void Renderer::RenderSkybox() {
    if (!m_mainTargets.hdr.resources.color || !m_environmentState.backgroundVisible) {
        return;
    }

    (void)SkyboxPass::Draw({
        m_commandResources.graphicsList.Get(),
        m_pipelineState.rootSignature.get(),
        m_constantBuffers.currentFrameGPU,
        m_pipelineState.skybox.get(),
        m_pipelineState.proceduralSky.get(),
        m_environmentState.enabled,
        m_environmentState.shadowAndEnvDescriptors[0],
    });
}

} // namespace Cortex::Graphics
