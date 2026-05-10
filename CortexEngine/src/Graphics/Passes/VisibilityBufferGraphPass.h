#pragma once

#include "Graphics/RenderGraph.h"

#include <functional>

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

struct GraphContext {
    ResourceHandles resources;
    bool needsMaterialResolve = false;
    bool debugPath = false;
    bool debugVisibility = false;
    bool debugDepth = false;
    bool debugGBuffer = false;
    bool brdfGraphOwned = false;
    bool clusterGraphOwned = false;
    std::function<void()> clear;
    std::function<void()> visibility;
    std::function<void()> materialResolve;
    std::function<void()> debugBlit;
    std::function<void()> brdfLut;
    std::function<void()> clusteredLights;
    std::function<void()> deferredLighting;
    std::function<void(const char*)> failStage;
};

struct LegacyPathContext {
    RGResourceHandle depth;
    RGResourceHandle hdr;
    RGResourceHandle shadow;
    RGResourceHandle rtShadow;
    RGResourceHandle rtGI;
    std::function<void()> execute;
    std::function<void(const char*)> failStage;
};

[[nodiscard]] bool AddStagedPath(RenderGraph& graph, const GraphContext& context);
[[nodiscard]] bool AddLegacyPath(RenderGraph& graph, const LegacyPathContext& context);

} // namespace Cortex::Graphics::VisibilityBufferGraphPass
