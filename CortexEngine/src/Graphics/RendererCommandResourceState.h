#pragma once

#include <array>

#include <wrl/client.h>

#include "Graphics/Renderer_ConstantBuffer.h"
#include "Graphics/RHI/D3D12Includes.h"

namespace Cortex::Graphics {

struct RendererCommandResourceState {
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, kFrameCount> graphicsAllocators{};
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> graphicsList;
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, kFrameCount> computeAllocators{};
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> computeList;
};

} // namespace Cortex::Graphics
