#pragma once

#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"

namespace Cortex::Graphics {

struct HDRRenderTargetResources {
    ComPtr<ID3D12Resource> color;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

    void Reset() {
        color.Reset();
        state = D3D12_RESOURCE_STATE_COMMON;
    }
};

struct HDRRenderTargetDescriptors {
    DescriptorHandle rtv;
    DescriptorHandle srv;

    void Reset() {
        rtv = {};
        srv = {};
    }
};

struct GBufferNormalRoughnessResources {
    ComPtr<ID3D12Resource> texture;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

    void Reset() {
        texture.Reset();
        state = D3D12_RESOURCE_STATE_COMMON;
    }
};

struct GBufferNormalRoughnessDescriptors {
    DescriptorHandle rtv;
    DescriptorHandle srv;

    void Reset() {
        rtv = {};
        srv = {};
    }
};

struct HDRRenderTargetState {
    HDRRenderTargetResources resources;
    HDRRenderTargetDescriptors descriptors;

    void Reset() {
        resources.Reset();
        descriptors.Reset();
    }
};

struct GBufferNormalRoughnessTargetState {
    GBufferNormalRoughnessResources resources;
    GBufferNormalRoughnessDescriptors descriptors;

    void Reset() {
        resources.Reset();
        descriptors.Reset();
    }
};

struct MainRenderTargetState {
    HDRRenderTargetState hdr;
    GBufferNormalRoughnessTargetState normalRoughness;

    void ResetHDR() {
        hdr.Reset();
    }

    void ResetGBufferNormalRoughness() {
        normalRoughness.Reset();
    }

    void ResetResources() {
        ResetHDR();
        ResetGBufferNormalRoughness();
    }
};

} // namespace Cortex::Graphics
