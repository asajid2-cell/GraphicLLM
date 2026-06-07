#pragma once

#include "Graphics/Passes/FullSceneShaderV3Passes.h"

namespace Cortex::Graphics {

class FullSceneShaderV3GraphBuilder {
public:
    explicit FullSceneShaderV3GraphBuilder(RenderGraph& graph);

    [[nodiscard]] bool SubmitSceneLocalEnvironment(
        const FullSceneShaderV3Passes::SceneLocalEnvironmentV3Context& context);
    [[nodiscard]] bool SubmitReflectionResolver(
        const FullSceneShaderV3Passes::FullSceneReflectionResolverV3Context& context);
    [[nodiscard]] bool SubmitReflectionHistory(
        const FullSceneShaderV3Passes::FullSceneReflectionHistoryV3Context& context);
    [[nodiscard]] bool SubmitReflectionHistoryCopy(
        const FullSceneShaderV3Passes::FullSceneReflectionHistoryV3CopyContext& context);
    [[nodiscard]] bool SubmitComposite(
        const FullSceneShaderV3Passes::FullSceneCompositeV3Context& context);
    [[nodiscard]] bool SubmitDisplay(
        const FullSceneShaderV3Passes::CandidateBeautyDisplayContext& context);

private:
    RenderGraph& m_graph;
};

} // namespace Cortex::Graphics
