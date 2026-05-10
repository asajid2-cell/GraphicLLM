#pragma once

#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DX12Pipeline.h"
#include "RHI/DX12Texture.h"
#include "RHI/DescriptorHeap.h"
#include "Scene/VegetationTypes.h"
#include "ShaderTypes.h"

#include <memory>

namespace Cortex::Graphics {

struct VegetationInstanceBufferState {
    ComPtr<ID3D12Resource> buffer;
    DescriptorHandle srv;
    UINT capacity = 0;
    UINT count = 0;

    void Reset() {
        buffer.Reset();
        srv = {};
        capacity = 0;
        count = 0;
    }
};

struct VegetationRenderState {
    std::unique_ptr<DX12Pipeline> meshPipeline;
    std::unique_ptr<DX12Pipeline> meshShadowPipeline;
    std::unique_ptr<DX12Pipeline> billboardPipeline;
    std::unique_ptr<DX12Pipeline> grassCardPipeline;
    ConstantBuffer<VegetationConstants> constants;
    VegetationInstanceBufferState meshInstances;
    VegetationInstanceBufferState billboardInstances;
    VegetationInstanceBufferState grassInstances;
    std::shared_ptr<DX12Texture> atlas;
    Scene::WindParams wind;
    Scene::VegetationStats stats;
    bool enabled = true;

    void ResetResources() {
        meshPipeline.reset();
        meshShadowPipeline.reset();
        billboardPipeline.reset();
        grassCardPipeline.reset();
        meshInstances.Reset();
        billboardInstances.Reset();
        grassInstances.Reset();
        atlas.reset();
        wind = {};
        stats = {};
        enabled = true;
    }
};

} // namespace Cortex::Graphics
