#pragma once

#include <cstdint>

#include <wrl/client.h>

#include "Graphics/RHI/D3D12Includes.h"

namespace Cortex::Graphics {

enum class GpuMarker : uint32_t {
    None              = 0,
    BeginFrame        = 1,
    ShadowPass        = 2,
    Skybox            = 3,
    OpaqueGeometry    = 4,
    TransparentGeom   = 5,
    MotionVectors     = 6,
    TAAResolve        = 7,
    SSR               = 8,
    Particles         = 9,
    SSAO              = 10,
    Bloom             = 11,
    PostProcess       = 12,
    DebugLines        = 13,
    EndFrame          = 14,
};

struct RendererBreadcrumbState {
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
    uint32_t* mappedValue = nullptr;
};

} // namespace Cortex::Graphics
