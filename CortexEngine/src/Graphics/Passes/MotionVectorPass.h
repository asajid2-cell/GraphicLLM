#pragma once

#include "Graphics/Passes/MotionVectorTargetPass.h"
#include "Graphics/RenderGraph.h"
#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/RHI/DescriptorHeap.h"

#include <functional>
#include <span>

namespace Cortex::Graphics::MotionVectorPass {

struct DrawContext {
    ID3D12Device* device = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    DX12Pipeline* pipeline = nullptr;

    ID3D12Resource* target = nullptr;
    DescriptorHandle targetRtv{};
    ID3D12Resource* depth = nullptr;
    std::span<DescriptorHandle> srvTable{};
};

struct GraphContext {
    RGResourceHandle velocity;
    RGResourceHandle depth;
    RGResourceHandle visibility;
    bool useVisibilityBufferMotion = false;
    std::function<bool()> computeVisibilityBufferMotion;
    MotionVectorTargetPass::CameraTargetContext cameraTarget;
    DrawContext cameraDraw;
    std::function<void(const char*)> failStage;
};

[[nodiscard]] bool Draw(const DrawContext& context);
[[nodiscard]] RGResourceHandle AddToGraph(RenderGraph& graph, const GraphContext& context);

} // namespace Cortex::Graphics::MotionVectorPass
