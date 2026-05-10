#pragma once

#include <atomic>

#include "Graphics/Renderer_ConstantBuffer.h"
#include "Graphics/ShaderTypes.h"
#include "Scene/BiomeTypes.h"

namespace Cortex::Graphics {

struct RendererConstantBufferState {
    FrameConstants frameCPU{};
    ConstantBuffer<FrameConstants> frame;
    D3D12_GPU_VIRTUAL_ADDRESS currentFrameGPU = 0;
    ConstantBuffer<ObjectConstants> object;
    ConstantBuffer<MaterialConstants> material;
    ConstantBuffer<ShadowConstants> shadow;
    ConstantBuffer<Scene::BiomeMaterialsCBuffer> biomeMaterials;
    std::atomic<bool> biomeMaterialsValid{false};
};

} // namespace Cortex::Graphics
