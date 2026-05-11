#pragma once

#include "Graphics/RHI/D3D12Includes.h"

namespace Cortex::Graphics::TAACopyPass {

struct ResourceStateRef {
    ID3D12Resource* resource = nullptr;
    D3D12_RESOURCE_STATES* state = nullptr;
};

struct HistoryCopyContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    ResourceStateRef hdrColor;
    ResourceStateRef historyColor;
    ResourceStateRef taaIntermediate;
    bool transitionIntermediateToRenderTarget = false;
    bool skipTransitions = false;
    bool returnHdrAndHistoryToShaderResource = true;
};

struct IntermediateCopyContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    ResourceStateRef taaIntermediate;
    ResourceStateRef hdrColor;
    bool skipTransitions = false;
};

struct ResolveInputsContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    ResourceStateRef taaIntermediate;
    ResourceStateRef hdrColor;
    ResourceStateRef depth;
    ResourceStateRef normalRoughness;
    ResourceStateRef velocity;
    ResourceStateRef historyColor;
    ResourceStateRef temporalMask;
    D3D12_RESOURCE_STATES depthSampleState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    bool skipTransitions = false;
};

[[nodiscard]] bool CopyHdrToHistory(const HistoryCopyContext& context);
[[nodiscard]] bool CopyIntermediateToHdr(const IntermediateCopyContext& context);
[[nodiscard]] bool PrepareResolveInputs(const ResolveInputsContext& context);
[[nodiscard]] bool TransitionToShaderResource(ID3D12GraphicsCommandList* commandList,
                                              const ResourceStateRef& resource,
                                              bool skipTransitions = false);

} // namespace Cortex::Graphics::TAACopyPass
