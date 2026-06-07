#include "Graphics/FullSceneShaderV3GraphBuilder.h"

namespace Cortex::Graphics {

FullSceneShaderV3GraphBuilder::FullSceneShaderV3GraphBuilder(RenderGraph& graph)
    : m_graph(graph) {}

bool FullSceneShaderV3GraphBuilder::SubmitSceneLocalEnvironment(
    const FullSceneShaderV3Passes::SceneLocalEnvironmentV3Context& context) {
    return FullSceneShaderV3Passes::AddSceneLocalEnvironmentV3Pass(m_graph, context);
}

bool FullSceneShaderV3GraphBuilder::SubmitSceneLocalEnvironment(
    const SceneLocalEnvironmentCommon& common,
    const SceneLocalEnvironmentSubmission& submission) {
    FullSceneShaderV3Passes::SceneLocalEnvironmentV3Context context{};
    context.sceneLocalEnvironment = submission.sceneLocalEnvironment;
    context.ambientLighting = submission.ambientLighting;
    context.visibleBackground = submission.visibleBackground;
    context.reflectionBackground = submission.reflectionBackground;
    context.atmosphere = submission.atmosphere;
    context.device = common.device;
    context.commandList = common.commandList;
    context.rootSignature = common.rootSignature;
    context.pipeline = common.pipeline;
    context.descriptorManager = common.descriptorManager;
    context.frameConstants = common.frameConstants;
    context.payloadAlbedo = submission.payloadAlbedo;
    context.payloadNormal = submission.payloadNormal;
    context.irradianceProxy = submission.irradianceProxy;
    context.specularProxy = submission.specularProxy;
    context.visibleBackgroundProxy = submission.visibleBackgroundProxy;
    context.outputRTVs = submission.outputRTVs;
    context.width = common.width;
    context.height = common.height;
    context.ran = submission.ran;
    context.failed = common.failed;
    context.stage = common.stage;
    return SubmitSceneLocalEnvironment(context);
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

bool FullSceneShaderV3GraphBuilder::SubmitComposite(
    const CompositeCommon& common,
    const CompositeSubmission& submission) {
    FullSceneShaderV3Passes::FullSceneCompositeV3Context context{};
    context.directLighting = submission.directLighting;
    context.indirectLighting = submission.indirectLighting;
    context.shadowVisibility = submission.shadowVisibility;
    context.legacyHdr = submission.legacyHdr;
    context.localReflectionRadiance = submission.localReflectionRadiance;
    context.reflectionConfidence = submission.reflectionConfidence;
    context.materialAlbedo = submission.materialAlbedo;
    context.sceneLocalEnvironment = submission.sceneLocalEnvironment;
    context.output = submission.output;
    context.energyClampPolicy = submission.energyClampPolicy;
    context.overbrightDiagnostics = submission.overbrightDiagnostics;
    context.compositeContributionMap = submission.compositeContributionMap;
    context.legacyRescueUsage = submission.legacyRescueUsage;
    context.device = common.device;
    context.descriptorManager = common.descriptorManager;
    context.commandList = common.commandList;
    context.rootSignature = common.rootSignature;
    context.pipeline = common.pipeline;
    context.frameConstants = common.frameConstants;
    context.directLightingSRV = submission.directLightingSRV;
    context.indirectLightingSRV = submission.indirectLightingSRV;
    context.shadowVisibilitySRV = submission.shadowVisibilitySRV;
    context.legacyHdrSRV = submission.legacyHdrSRV;
    context.sceneLocalEnvironmentSRV = submission.sceneLocalEnvironmentSRV;
    context.outputRTVs = submission.outputRTVs;
    context.width = common.width;
    context.height = common.height;
    context.ran = submission.ran;
    context.failed = common.failed;
    context.stage = common.stage;
    return SubmitComposite(context);
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
