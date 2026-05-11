#include "Graphics/Passes/MeshUploadCopyPass.h"

namespace Cortex::Graphics::MeshUploadCopyPass {

HRESULT RecordBufferCopies(const CopyContext& context) {
    if (!context.allocator ||
        !context.commandList ||
        !context.vertexDestination ||
        !context.vertexSource ||
        context.vertexBytes == 0 ||
        !context.indexDestination ||
        !context.indexSource ||
        context.indexBytes == 0) {
        return E_INVALIDARG;
    }

    HRESULT hr = context.allocator->Reset();
    if (FAILED(hr)) {
        return hr;
    }

    hr = context.commandList->Reset(context.allocator, nullptr);
    if (FAILED(hr)) {
        return hr;
    }

    context.commandList->CopyBufferRegion(
        context.vertexDestination,
        0,
        context.vertexSource,
        0,
        context.vertexBytes);
    context.commandList->CopyBufferRegion(
        context.indexDestination,
        0,
        context.indexSource,
        0,
        context.indexBytes);

    return context.commandList->Close();
}

} // namespace Cortex::Graphics::MeshUploadCopyPass
