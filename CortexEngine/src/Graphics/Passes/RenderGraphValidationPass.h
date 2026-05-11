#pragma once

#include "Graphics/RenderGraph.h"
#include "Graphics/RHI/DescriptorHeap.h"
#include "Utils/Result.h"

#include <array>
#include <functional>

namespace Cortex::Graphics::RenderGraphValidationPass {

struct TransientValidationViews {
    DescriptorHandle rtvA;
    DescriptorHandle srvA;
    DescriptorHandle rtvB;
    DescriptorHandle srvB;
};

struct TransientValidationTarget {
    ID3D12Device* device = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    DescriptorHandle rtv;
    DescriptorHandle srv;
    std::array<float, 4> clearColor = {};
    const char* descriptorFailureReason = nullptr;
};

struct TransientValidationContext {
    RGResourceDesc transientDesc;
    TransientValidationTarget passA;
    TransientValidationTarget passB;
    std::function<void(const char*)> failStage;
};

[[nodiscard]] Result<TransientValidationViews> CreateTransientValidationViews(DescriptorHeapManager* descriptorManager);
[[nodiscard]] bool AddTransientValidation(RenderGraph& graph, const TransientValidationContext& context);

} // namespace Cortex::Graphics::RenderGraphValidationPass
