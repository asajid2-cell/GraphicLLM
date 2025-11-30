#pragma once

#include <d3d12.h>
#include <wrl/client.h>

namespace Cortex::Graphics {

// Simple wrapper around the vertex and index buffers used for mesh draws.
// Shared between the raster renderer and the DXR context.
struct MeshBuffers {
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;
};

} // namespace Cortex::Graphics

