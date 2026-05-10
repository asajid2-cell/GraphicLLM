#include "PostProcessPass.h"

#include "FullscreenPass.h"

namespace Cortex::Graphics::PostProcessPass {

bool Draw(const DrawContext& context) {
    if (!context.commandList || !context.pipeline || !context.pipeline->GetPipelineState() ||
        context.width == 0 || context.height == 0 || !context.targetRtv.ptr ||
        context.srvTable.empty()) {
        return false;
    }

    context.commandList->OMSetRenderTargets(1, &context.targetRtv, FALSE, nullptr);
    FullscreenPass::SetViewportAndScissor(context.commandList, context.width, context.height);

    if (!FullscreenPass::BindGraphicsState({
            context.commandList,
            context.descriptorManager,
            context.rootSignature,
            context.frameConstants,
        })) {
        return false;
    }

    context.commandList->SetPipelineState(context.pipeline->GetPipelineState());
    context.commandList->SetGraphicsRootDescriptorTable(3, context.srvTable[0].gpu);
    if (context.shadowAndEnvironmentTable.IsValid()) {
        context.commandList->SetGraphicsRootDescriptorTable(4, context.shadowAndEnvironmentTable.gpu);
    }

    FullscreenPass::DrawTriangle(context.commandList);
    return true;
}

} // namespace Cortex::Graphics::PostProcessPass
