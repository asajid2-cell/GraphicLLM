#include "VisibilityBuffer.h"

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

Result<void> VisibilityBufferRenderer::CreatePipelines() {
    auto rsResult = CreateRootSignatures();
    if (rsResult.IsErr()) {
        return rsResult;
    }

    auto visibilityResult = CreateVisibilityPassPipelines();
    if (visibilityResult.IsErr()) {
        return visibilityResult;
    }

    auto resolveResult = CreateMaterialResolvePipeline();
    if (resolveResult.IsErr()) {
        return resolveResult;
    }

    auto motionResult = CreateMotionVectorPipeline();
    if (motionResult.IsErr()) {
        return motionResult;
    }

    auto clusterResult = CreateClusteredLightPipeline();
    if (clusterResult.IsErr()) {
        return clusterResult;
    }

    auto brdfResult = CreateBRDFLUTPipeline();
    if (brdfResult.IsErr()) {
        return brdfResult;
    }

    auto debugResult = CreateDebugBlitPipelines();
    if (debugResult.IsErr()) {
        return debugResult;
    }

    auto deferredResult = CreateDeferredLightingPipeline();
    if (deferredResult.IsErr()) {
        return deferredResult;
    }

    spdlog::info("VisibilityBuffer pipelines created successfully");

    return Result<void>::Ok();
}

} // namespace Cortex::Graphics
