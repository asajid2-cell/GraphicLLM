#pragma once

#include "Graphics/RHI/D3D12Includes.h"

namespace Cortex::Graphics::MeshUploadCopyPass {

struct CopyContext {
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12Resource* vertexDestination = nullptr;
    ID3D12Resource* vertexSource = nullptr;
    UINT64 vertexBytes = 0;
    ID3D12Resource* indexDestination = nullptr;
    ID3D12Resource* indexSource = nullptr;
    UINT64 indexBytes = 0;
};

[[nodiscard]] HRESULT RecordBufferCopies(const CopyContext& context);

} // namespace Cortex::Graphics::MeshUploadCopyPass
