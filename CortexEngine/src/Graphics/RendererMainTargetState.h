#pragma once

#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"

namespace Cortex::Graphics {

struct MainRenderTargetState {
    ComPtr<ID3D12Resource> hdrColor;
    DescriptorHandle hdrRTV;
    DescriptorHandle hdrSRV;
    D3D12_RESOURCE_STATES hdrState = D3D12_RESOURCE_STATE_COMMON;

    ComPtr<ID3D12Resource> gbufferNormalRoughness;
    DescriptorHandle gbufferNormalRoughnessRTV;
    DescriptorHandle gbufferNormalRoughnessSRV;
    D3D12_RESOURCE_STATES gbufferNormalRoughnessState = D3D12_RESOURCE_STATE_COMMON;

    void ResetHDR() {
        hdrColor.Reset();
        hdrRTV = {};
        hdrSRV = {};
        hdrState = D3D12_RESOURCE_STATE_COMMON;
    }

    void ResetGBufferNormalRoughness() {
        gbufferNormalRoughness.Reset();
        gbufferNormalRoughnessRTV = {};
        gbufferNormalRoughnessSRV = {};
        gbufferNormalRoughnessState = D3D12_RESOURCE_STATE_COMMON;
    }

    void ResetResources() {
        ResetHDR();
        ResetGBufferNormalRoughness();
    }
};

} // namespace Cortex::Graphics
