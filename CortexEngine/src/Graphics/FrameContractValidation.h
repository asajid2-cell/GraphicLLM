#pragma once

#include "Graphics/FrameContract.h"

namespace Cortex::Graphics {

struct FrameContractValidationContext {
    bool depthOnlyPipelineReady = false;
    bool alphaDepthOnlyPipelineReady = false;
    bool doubleSidedDepthOnlyPipelineReady = false;
    bool doubleSidedAlphaDepthOnlyPipelineReady = false;
    bool transparentPipelineReady = false;
    bool waterOverlayPipelineReady = false;
    bool overlayPipelineReady = false;
    bool readOnlyDepthStencilViewReady = false;
    bool rtReflectionWrittenThisFrame = false;
    bool rtReflectionDenoisedThisFrame = false;
};

void ValidateFrameContractSnapshot(FrameContract& contract,
                                   const FrameContractValidationContext& context);

} // namespace Cortex::Graphics
