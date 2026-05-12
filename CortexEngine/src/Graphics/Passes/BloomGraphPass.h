#pragma once

#include "BloomPass.h"
#include "Graphics/RenderGraph.h"
#include "Graphics/RHI/DX12Pipeline.h"

#include <array>
#include <cstdint>
#include <span>

namespace Cortex::Graphics::BloomGraphPass {

struct GraphStatus {
    bool* failed = nullptr;
    const char** stage = nullptr;
};

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
    D3D12_RESOURCE_STATES* hdrResourceState = nullptr;
    D3D12_RESOURCE_STATES hdrShaderResourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    GraphStatus status;
    bool* bloomRan = nullptr;
    const bool* bloomStageFailed = nullptr;
};

struct StandaloneBloomContext {
    RGResourceHandle hdr;
    std::span<RGResourceHandle> bloomA;
    std::span<RGResourceHandle> bloomB;
    std::span<ID3D12Resource* const> bloomATemplates;
    std::span<ID3D12Resource* const> bloomBTemplates;
    DescriptorHandle (*targetRtv)[2] = nullptr;
    BloomPass::FullscreenContext fullscreen;
    DX12Pipeline* downsamplePipeline = nullptr;
    DX12Pipeline* blurHPipeline = nullptr;
    DX12Pipeline* blurVPipeline = nullptr;
    DX12Pipeline* compositePipeline = nullptr;
    uint32_t activeLevels = 0;
    uint32_t stageLevels = 0;
    uint32_t baseLevel = 0;
    bool useTransients = true;
    D3D12_RESOURCE_STATES* hdrResourceState = nullptr;
    D3D12_RESOURCE_STATES hdrShaderResourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    GraphStatus status;
};

[[nodiscard]] RGResourceHandle AddFusedBloom(RenderGraph& graph, const FusedBloomContext& context);
[[nodiscard]] RGResourceHandle AddStandaloneBloom(RenderGraph& graph, const StandaloneBloomContext& context);

} // namespace Cortex::Graphics::BloomGraphPass
