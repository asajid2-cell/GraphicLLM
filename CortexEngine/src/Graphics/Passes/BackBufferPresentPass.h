#pragma once

#include "Graphics/RHI/D3D12Includes.h"

#include <cstdint>
#include <wrl/client.h>

namespace Cortex::Graphics::BackBufferPresentPass {

struct VisualCaptureResult {
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    uint32_t width = 0;
    uint32_t height = 0;
};

struct PresentContext {
    ID3D12Device* device = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12Resource* backBuffer = nullptr;
    bool backBufferUsedAsRenderTarget = false;
    bool captureVisualValidation = false;
    VisualCaptureResult* visualCapture = nullptr;
};

[[nodiscard]] bool TransitionBackBufferForPresent(const PresentContext& context);

} // namespace Cortex::Graphics::BackBufferPresentPass
