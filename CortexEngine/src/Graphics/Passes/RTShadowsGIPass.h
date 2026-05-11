#pragma once

#include "Graphics/RHI/D3D12Includes.h"

namespace Cortex::Graphics::RTShadowsGIPass {

struct ResourceStateRef {
    ID3D12Resource* resource = nullptr;
    D3D12_RESOURCE_STATES* state = nullptr;
};

struct ShadowInputContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    ResourceStateRef depth;
    ResourceStateRef shadowMask;
};

struct GIOutputContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    ResourceStateRef color;
};

[[nodiscard]] bool PrepareShadowInputs(const ShadowInputContext& context);
[[nodiscard]] bool PrepareGIOutput(const GIOutputContext& context);

} // namespace Cortex::Graphics::RTShadowsGIPass
