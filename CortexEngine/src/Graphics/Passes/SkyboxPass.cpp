#include "SkyboxPass.h"

#include "FullscreenPass.h"

namespace Cortex::Graphics::SkyboxPass {

bool Draw(const DrawContext& context) {
    if (!context.commandList || !context.rootSignature ||
        !context.rootSignature->GetRootSignature() || !context.frameConstants) {
        return false;
    }

    context.commandList->SetGraphicsRootSignature(context.rootSignature->GetRootSignature());
    context.commandList->SetGraphicsRootConstantBufferView(1, context.frameConstants);

    if (context.iblEnabled && context.skyboxPipeline && context.skyboxPipeline->GetPipelineState()) {
        context.commandList->SetPipelineState(context.skyboxPipeline->GetPipelineState());
        if (context.shadowAndEnvironmentTable.IsValid()) {
            context.commandList->SetGraphicsRootDescriptorTable(4, context.shadowAndEnvironmentTable.gpu);
        }
    } else if (context.proceduralSkyPipeline && context.proceduralSkyPipeline->GetPipelineState()) {
        context.commandList->SetPipelineState(context.proceduralSkyPipeline->GetPipelineState());
    } else {
        return false;
    }

    context.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    FullscreenPass::DrawTriangle(context.commandList);
    return true;
}

} // namespace Cortex::Graphics::SkyboxPass
