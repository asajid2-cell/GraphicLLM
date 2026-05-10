#pragma once

#include "BloomPass.h"
#include "Graphics/RenderGraph.h"
#include "Graphics/RHI/DX12Pipeline.h"

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace Cortex::Graphics::BloomGraphPass {

struct FusedBloomContext {
    RGResourceHandle hdr;
    std::span<RGResourceHandle> bloomA;
    std::span<RGResourceHandle> bloomB;
    std::span<ID3D12Resource* const> bloomATemplates;
    std::span<ID3D12Resource* const> bloomBTemplates;
    DescriptorHandle (*graphRtv)[2] = nullptr;
    BloomPass::FullscreenContext fullscreen;
    DX12Pipeline* downsamplePipeline = nullptr;
    DX12Pipeline* blurHPipeline = nullptr;
    DX12Pipeline* blurVPipeline = nullptr;
    DX12Pipeline* compositePipeline = nullptr;
    uint32_t activeLevels = 0;
    uint32_t stageLevels = 0;
    uint32_t baseLevel = 0;
    bool useTransients = true;
    std::function<void()> markHdrShaderResource;
    std::function<void(const char*)> failStage;
    std::function<void()> markBloomRan;
};

struct StandaloneBloomContext {
    RGResourceHandle hdr;
    std::span<RGResourceHandle> bloomA;
    std::span<RGResourceHandle> bloomB;
    uint32_t activeLevels = 0;
    uint32_t baseLevel = 0;
    std::function<void(RGPassBuilder&)> declareTransients;
    std::function<bool(const RenderGraph&)> renderDownsampleBase;
    std::function<bool(uint32_t, const RenderGraph&)> renderDownsampleLevel;
    std::function<bool(uint32_t, const RenderGraph&)> renderBlurHorizontal;
    std::function<bool(uint32_t, const RenderGraph&)> renderBlurVertical;
    std::function<bool(const RenderGraph&)> renderComposite;
    std::function<bool(const RenderGraph&)> copyCombined;
    std::function<void(const char*)> failStage;
};

[[nodiscard]] RGResourceHandle AddFusedBloom(RenderGraph& graph, const FusedBloomContext& context);
[[nodiscard]] RGResourceHandle AddStandaloneBloom(RenderGraph& graph, const StandaloneBloomContext& context);

} // namespace Cortex::Graphics::BloomGraphPass
