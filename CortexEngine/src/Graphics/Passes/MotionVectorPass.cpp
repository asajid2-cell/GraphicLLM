#include "MotionVectorPass.h"

#include "DescriptorTable.h"
#include "FullscreenPass.h"

namespace Cortex::Graphics::MotionVectorPass {

namespace {

void Fail(const GraphContext& context, const char* stage) {
    if (context.failStage) {
        context.failStage(stage);
    }
}

[[nodiscard]] bool IsUsable(const GraphContext& context) {
    if (!context.velocity.IsValid()) {
        return false;
    }
    if (context.useVisibilityBufferMotion) {
        return context.visibility.IsValid() && static_cast<bool>(context.computeVisibilityBufferMotion);
    }
    return context.depth.IsValid() &&
           context.cameraTarget.velocity.resource &&
           context.cameraTarget.depth.resource &&
           static_cast<bool>(context.cameraDraw.pipeline);
}

} // namespace

bool Draw(const DrawContext& context) {
    if (!context.device || !context.commandList || !context.pipeline ||
        !context.pipeline->GetPipelineState() || !context.target ||
        !context.targetRtv.IsValid() || !context.depth || context.srvTable.empty()) {
        return false;
    }

    context.commandList->OMSetRenderTargets(1, &context.targetRtv.cpu, FALSE, nullptr);
    FullscreenPass::SetViewportAndScissor(context.commandList, context.target);

    const float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context.commandList->ClearRenderTargetView(context.targetRtv.cpu, clearColor, 0, nullptr);

    if (!FullscreenPass::BindGraphicsState({
            context.commandList,
            context.descriptorManager,
            context.rootSignature,
            context.frameConstants,
        })) {
        return false;
    }

    context.commandList->SetPipelineState(context.pipeline->GetPipelineState());

    if (!DescriptorTable::WriteTexture2DSRV(
            context.device, context.srvTable[0], context.depth, DXGI_FORMAT_R32_FLOAT)) {
        return false;
    }
    for (size_t slot = 1; slot < context.srvTable.size(); ++slot) {
        if (!DescriptorTable::WriteTexture2DSRV(
                context.device, context.srvTable[slot], nullptr, DXGI_FORMAT_R32_FLOAT)) {
            return false;
        }
    }

    context.commandList->SetGraphicsRootDescriptorTable(3, context.srvTable[0].gpu);
    FullscreenPass::DrawTriangle(context.commandList);
    return true;
}

RGResourceHandle AddToGraph(RenderGraph& graph, const GraphContext& context) {
    if (!IsUsable(context)) {
        Fail(context, "motion_vectors_graph_contract");
        return {};
    }

    graph.AddPass(
        "MotionVectors",
        [context](RGPassBuilder& builder) {
            builder.SetType(context.useVisibilityBufferMotion ? RGPassType::Compute : RGPassType::Graphics);
            if (context.useVisibilityBufferMotion) {
                builder.Read(context.visibility, RGResourceUsage::ShaderResource);
                builder.Write(context.velocity, RGResourceUsage::UnorderedAccess);
            } else {
                builder.Read(context.depth, RGResourceUsage::ShaderResource | RGResourceUsage::DepthStencilRead);
                builder.Write(context.velocity, RGResourceUsage::RenderTarget);
            }
        },
        [context](ID3D12GraphicsCommandList*, const RenderGraph&) {
            const bool ok = context.useVisibilityBufferMotion
                ? (context.computeVisibilityBufferMotion && context.computeVisibilityBufferMotion())
                : (MotionVectorTargetPass::TransitionCameraTargets(context.cameraTarget) &&
                   Draw(context.cameraDraw));
            if (!ok) {
                Fail(context, context.useVisibilityBufferMotion ? "visibility_buffer_motion" : "camera_motion");
            }
        });

    return context.velocity;
}

} // namespace Cortex::Graphics::MotionVectorPass
