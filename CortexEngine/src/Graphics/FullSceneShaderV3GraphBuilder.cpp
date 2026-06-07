#include "Graphics/FullSceneShaderV3GraphBuilder.h"

namespace Cortex::Graphics {

FullSceneShaderV3GraphBuilder::FullSceneShaderV3GraphBuilder(RenderGraph& graph)
    : m_graph(graph) {}

bool FullSceneShaderV3GraphBuilder::SubmitSceneLocalEnvironment(
    const FullSceneShaderV3Passes::SceneLocalEnvironmentV3Context& context) {
    return FullSceneShaderV3Passes::AddSceneLocalEnvironmentV3Pass(m_graph, context);
}

bool FullSceneShaderV3GraphBuilder::SubmitReflectionResolver(
    const FullSceneShaderV3Passes::FullSceneReflectionResolverV3Context& context) {
    return FullSceneShaderV3Passes::AddFullSceneReflectionResolverV3Pass(m_graph, context);
}

bool FullSceneShaderV3GraphBuilder::SubmitReflectionHistory(
    const FullSceneShaderV3Passes::FullSceneReflectionHistoryV3Context& context) {
    return FullSceneShaderV3Passes::AddFullSceneReflectionHistoryV3Pass(m_graph, context);
}

bool FullSceneShaderV3GraphBuilder::SubmitReflectionHistoryCopy(
    const FullSceneShaderV3Passes::FullSceneReflectionHistoryV3CopyContext& context) {
    return FullSceneShaderV3Passes::AddFullSceneReflectionHistoryV3CopyPass(m_graph, context);
}

bool FullSceneShaderV3GraphBuilder::SubmitComposite(
    const FullSceneShaderV3Passes::FullSceneCompositeV3Context& context) {
    return FullSceneShaderV3Passes::AddFullSceneCompositeV3Pass(m_graph, context);
}

bool FullSceneShaderV3GraphBuilder::SubmitDisplay(
    const FullSceneShaderV3Passes::CandidateBeautyDisplayContext& context) {
    return FullSceneShaderV3Passes::AddCandidateBeautyDisplayPass(m_graph, context);
}

bool FullSceneShaderV3GraphBuilder::SubmitDisplay(
    const DisplayCommon& common,
    const DisplaySubmission& submission) {
    FullSceneShaderV3Passes::CandidateBeautyDisplayContext context{};
    context.passName = submission.passName;
    context.candidate = submission.candidate;
    context.backBuffer = common.backBuffer;
    context.device = common.device;
    context.descriptorManager = common.descriptorManager;
    context.commandList = common.commandList;
    context.rootSignature = common.rootSignature;
    context.pipeline = common.pipeline;
    context.frameConstants = common.frameConstants;
    context.candidateSRV = submission.candidateSRV;
    context.backBufferRTV = common.backBufferRTV;
    context.width = common.width;
    context.height = common.height;
    context.ran = submission.ran;
    context.failed = common.failed;
    context.stage = common.stage;
    return SubmitDisplay(context);
}

} // namespace Cortex::Graphics
