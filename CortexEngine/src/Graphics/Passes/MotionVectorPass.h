#pragma once

#include "Graphics/Passes/MotionVectorTargetPass.h"
#include "Graphics/RenderGraph.h"
#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/RHI/DescriptorHeap.h"
#include "Graphics/VisibilityBuffer.h"

#include <span>
#include <string>
#include <vector>

namespace Cortex::Graphics::MotionVectorPass {

struct GraphStatus {
    bool* failed = nullptr;
    const char** stage = nullptr;
};

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

struct VisibilityBufferMotionContext {
    VisibilityBufferRenderer* renderer = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12Resource* velocityBuffer = nullptr;
    D3D12_RESOURCE_STATES* velocityState = nullptr;
    const std::vector<VisibilityBufferRenderer::VBMeshDrawInfo>* meshDraws = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    D3D12_RESOURCE_STATES visibilityShaderResourceState =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    std::string* error = nullptr;
};

struct GraphContext {
    RGResourceHandle velocity;
    RGResourceHandle depth;
    RGResourceHandle visibility;
    bool useVisibilityBufferMotion = false;
    VisibilityBufferMotionContext visibilityMotion;
    MotionVectorTargetPass::CameraTargetContext cameraTarget;
    DrawContext cameraDraw;
    GraphStatus status;
};

[[nodiscard]] bool Draw(const DrawContext& context);
[[nodiscard]] bool ComputeVisibilityBufferMotion(const VisibilityBufferMotionContext& context);
[[nodiscard]] RGResourceHandle AddToGraph(RenderGraph& graph, const GraphContext& context);

} // namespace Cortex::Graphics::MotionVectorPass
