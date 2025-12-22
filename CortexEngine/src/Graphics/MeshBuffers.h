#pragma once

#include "RHI/D3D12Includes.h"
#include <wrl/client.h>
#include <cstdint>

namespace Cortex::Graphics {

// Simple wrapper around the vertex and index buffers used for mesh draws.
// Shared between the raster renderer and the DXR context.
struct MeshBuffers {
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;

    // Indices into the renderer's shader-visible CBV/SRV/UAV heap used for SM6.6 ResourceDescriptorHeap[] access.
    // These are created once when the mesh buffers are uploaded (no per-frame mesh SRV churn).
    static constexpr uint32_t kInvalidDescriptorIndex = 0xFFFFFFFFu;
    uint32_t vbRawSRVIndex = kInvalidDescriptorIndex;
    uint32_t ibRawSRVIndex = kInvalidDescriptorIndex;
    uint32_t vertexStrideBytes = 48u; // sizeof(Vertex)
    uint32_t indexFormat = 0u;        // 0 = R32_UINT, 1 = R16_UINT
};

} // namespace Cortex::Graphics
