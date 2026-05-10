#pragma once

#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/RHI/DescriptorHeap.h"

namespace Cortex::Graphics::SkyboxPass {

struct DrawContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    DX12Pipeline* skyboxPipeline = nullptr;
    DX12Pipeline* proceduralSkyPipeline = nullptr;
    bool iblEnabled = false;
    DescriptorHandle shadowAndEnvironmentTable{};
};

[[nodiscard]] bool Draw(const DrawContext& context);

} // namespace Cortex::Graphics::SkyboxPass
