#include "Renderer.h"

#include "Graphics/FrameContractResources.h"
#include "Graphics/FrameContractValidation.h"
#include "Graphics/RenderableClassification.h"
#include "Graphics/SurfaceClassification.h"
#include "Scene/Components.h"
#include "Scene/ECS_Registry.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include <glm/geometric.hpp>

namespace Cortex::Graphics {

void Renderer::ValidateFrameContract() {
    const RendererPipelineReadiness pipelineReadiness = m_pipelineState.GetReadiness();

    FrameContractValidationContext context{};
    context.depthOnlyPipelineReady = pipelineReadiness.depthOnly;
    context.alphaDepthOnlyPipelineReady = pipelineReadiness.depthOnlyAlpha;
    context.doubleSidedDepthOnlyPipelineReady = pipelineReadiness.depthOnlyDoubleSided;
    context.doubleSidedAlphaDepthOnlyPipelineReady = pipelineReadiness.depthOnlyAlphaDoubleSided;
    context.transparentPipelineReady = pipelineReadiness.transparent;
    context.waterOverlayPipelineReady = pipelineReadiness.waterOverlay;
    context.overlayPipelineReady = pipelineReadiness.overlay;
    context.readOnlyDepthStencilViewReady = m_depthResources.descriptors.readOnlyDsv.IsValid();
    context.rtReflectionWrittenThisFrame = m_frameLifecycle.rtReflectionWrittenThisFrame;
    context.rtReflectionDenoisedThisFrame = m_rtDenoiseState.reflectionDenoisedThisFrame;

    auto& contract = m_frameDiagnostics.contract.contract;
    if (contract.particles.executed && contract.draws.particleInstances > 0) {
        contract.particles.submittedInstances = contract.draws.particleInstances;
    }

    ValidateFrameContractSnapshot(contract, context);

    const auto& materials = contract.materials;
    if (materials.validationWarnings > 0 || materials.validationErrors > 0) {
        contract.warnings.push_back(
            "material_validation_issues:" +
            std::to_string(materials.validationIssues) +
            " warnings=" + std::to_string(materials.validationWarnings) +
            " errors=" + std::to_string(materials.validationErrors));
    }
    if (materials.blendTransmission > 0) {
        contract.warnings.push_back(
            "material_blend_transmission_count:" +
            std::to_string(materials.blendTransmission));
    }
    if (materials.metallicTransmission > 0) {
        contract.warnings.push_back(
            "material_metallic_transmission_count:" +
            std::to_string(materials.metallicTransmission));
    }

    contract.health.frameWarnings =
        static_cast<uint32_t>(contract.warnings.size());
    if (!contract.warnings.empty()) {
        const std::string& warning = contract.warnings.back();
        contract.health.lastWarningMessage = warning;
        const size_t split = warning.find(':');
        contract.health.lastWarningCode =
            split == std::string::npos ? warning : warning.substr(0, split);
    } else {
        contract.health.lastWarningCode.clear();
        contract.health.lastWarningMessage.clear();
    }
}

} // namespace Cortex::Graphics
