#pragma once

#include <memory>

#ifdef CORTEX_ENABLE_HYPER_EXPERIMENT
#include "Graphics/HyperGeometry/HyperGeometryEngine.h"
#endif

namespace Cortex {
class Window;

namespace Graphics {

class BindlessResourceManager;
class DescriptorHeapManager;
class DX12CommandQueue;
class DX12Device;
class DX12RaytracingContext;
class GPUCullingPipeline;
class RenderGraph;
class RTDenoiser;
class RTReflectionSignalStats;
class TemporalRejectionMask;
class VisibilityBufferRenderer;

#ifndef CORTEX_ENABLE_HYPER_EXPERIMENT
namespace HyperGeometry {
class HyperGeometryEngine;
}
#endif

struct RendererServiceState {
    Window* window = nullptr;
    DX12Device* device = nullptr;

    std::unique_ptr<DX12CommandQueue> commandQueue;
    std::unique_ptr<DX12CommandQueue> uploadQueue;
    std::unique_ptr<DX12CommandQueue> computeQueue;
    std::unique_ptr<DescriptorHeapManager> descriptorManager;
    std::unique_ptr<BindlessResourceManager> bindlessManager;

#ifdef CORTEX_ENABLE_HYPER_EXPERIMENT
    std::unique_ptr<HyperGeometry::HyperGeometryEngine> hyperGeometry;
    bool hyperSceneBuilt = false;
#endif

    std::unique_ptr<DX12RaytracingContext> rayTracingContext;
    std::unique_ptr<RTDenoiser> rtDenoiser;
    std::unique_ptr<RTReflectionSignalStats> rtReflectionSignalStats;
    std::unique_ptr<TemporalRejectionMask> temporalRejectionMask;
    std::unique_ptr<GPUCullingPipeline> gpuCulling;
    std::unique_ptr<RenderGraph> renderGraph;
    std::unique_ptr<VisibilityBufferRenderer> visibilityBuffer;
};

} // namespace Graphics
} // namespace Cortex
