#pragma once

#include "Graphics/RenderGraph.h"
#include "Graphics/VisibilityBuffer.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Cortex::Graphics::VisibilityBufferGraphPass {

struct ResourceHandles {
    RGResourceHandle depth;
    RGResourceHandle hdr;
    RGResourceHandle visibility;
    RGResourceHandle albedo;
    RGResourceHandle normalRoughness;
    RGResourceHandle emissiveMetallic;
    RGResourceHandle materialExt0;
    RGResourceHandle materialExt1;
    RGResourceHandle materialExt2;
    RGResourceHandle brdfLut;
    RGResourceHandle clusterRanges;
    RGResourceHandle clusterLightIndices;
    RGResourceHandle shadow;
    RGResourceHandle rtShadow;
    RGResourceHandle rtGI;
    RGResourceHandle debugSource;
};

struct StageFailureContext {
    bool* failed = nullptr;
    std::string* stage = nullptr;
    std::string* error = nullptr;
};

struct ClearContext {
    VisibilityBufferRenderer* renderer = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    StageFailureContext failure;
};

struct VisibilityContext {
    VisibilityBufferRenderer* renderer = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12Resource* depthBuffer = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE depthDSV{};
    const glm::mat4* viewProjection = nullptr;
    const std::vector<VisibilityBufferRenderer::VBMeshDrawInfo>* meshDraws = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS cullMaskAddress = 0;
    D3D12_RESOURCE_STATES* depthState = nullptr;
    uint32_t instanceCount = 0;
    uint32_t* contractInstances = nullptr;
    uint32_t* contractMeshes = nullptr;
    uint32_t* contractDrawBatches = nullptr;
    StageFailureContext failure;
};

struct MaterialResolveContext {
    VisibilityBufferRenderer* renderer = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12Resource* depthBuffer = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE depthSRV{};
    const glm::mat4* viewProjection = nullptr;
    const std::vector<VisibilityBufferRenderer::VBMeshDrawInfo>* meshDraws = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS biomeMaterialsAddress = 0;
    D3D12_RESOURCE_STATES* depthState = nullptr;
    StageFailureContext failure;
};

struct DebugBlitContext {
    VisibilityBufferRenderer* renderer = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12Resource* hdrTarget = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE hdrRTV{};
    ID3D12Resource* depthBuffer = nullptr;
    bool debugVisibility = false;
    bool debugDepth = false;
    bool debugGBuffer = false;
    VisibilityBufferRenderer::DebugBlitBuffer gbufferSource =
        VisibilityBufferRenderer::DebugBlitBuffer::Albedo;
    D3D12_RESOURCE_STATES* hdrState = nullptr;
    D3D12_RESOURCE_STATES* depthState = nullptr;
    bool* renderedThisFrame = nullptr;
    bool* debugOverrideThisFrame = nullptr;
    StageFailureContext failure;
};

struct GraphContext {
    ResourceHandles resources;
    bool needsMaterialResolve = false;
    bool debugPath = false;
    bool debugVisibility = false;
    bool debugDepth = false;
    bool debugGBuffer = false;
    bool brdfGraphOwned = false;
    bool clusterGraphOwned = false;
    ClearContext clear;
    VisibilityContext visibility;
    MaterialResolveContext materialResolve;
    DebugBlitContext debugBlit;
    std::function<void()> brdfLut;
    std::function<void()> clusteredLights;
    std::function<void()> deferredLighting;
    std::function<void(const char*)> failStage;
};

[[nodiscard]] bool Clear(const ClearContext& context);
[[nodiscard]] bool RasterizeVisibility(const VisibilityContext& context);
[[nodiscard]] bool ResolveMaterials(const MaterialResolveContext& context);
[[nodiscard]] bool DebugBlit(const DebugBlitContext& context);
[[nodiscard]] bool AddStagedPath(RenderGraph& graph, const GraphContext& context);

} // namespace Cortex::Graphics::VisibilityBufferGraphPass
