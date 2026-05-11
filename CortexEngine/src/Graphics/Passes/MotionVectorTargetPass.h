#pragma once

#include "Graphics/RHI/D3D12Includes.h"

namespace Cortex::Graphics::MotionVectorTargetPass {

struct ResourceStateRef {
    ID3D12Resource* resource = nullptr;
    D3D12_RESOURCE_STATES* state = nullptr;
};

struct VelocityUAVContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    ResourceStateRef velocity;
};

struct CameraTargetContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    ResourceStateRef velocity;
    ResourceStateRef depth;
    D3D12_RESOURCE_STATES depthSampleState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
};

[[nodiscard]] bool TransitionVelocityToUnorderedAccess(const VelocityUAVContext& context);
[[nodiscard]] bool TransitionCameraTargets(const CameraTargetContext& context);

} // namespace Cortex::Graphics::MotionVectorTargetPass
