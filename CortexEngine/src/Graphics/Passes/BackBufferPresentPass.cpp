#include "Graphics/Passes/BackBufferPresentPass.h"

namespace Cortex::Graphics::BackBufferPresentPass {

namespace {

D3D12_RESOURCE_BARRIER TransitionBarrier(
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return barrier;
}

bool CaptureBackBuffer(const PresentContext& context) {
    if (!context.device || !context.commandList || !context.backBuffer || !context.visualCapture) {
        return false;
    }

    VisualCaptureResult& capture = *context.visualCapture;
    capture = {};

    const D3D12_RESOURCE_DESC backBufferDesc = context.backBuffer->GetDesc();
    capture.width = static_cast<uint32_t>(backBufferDesc.Width);
    capture.height = backBufferDesc.Height;

    UINT numRows = 0;
    UINT64 rowSizeBytes = 0;
    UINT64 totalBytes = 0;
    context.device->GetCopyableFootprints(
        &backBufferDesc,
        0,
        1,
        0,
        &capture.footprint,
        &numRows,
        &rowSizeBytes,
        &totalBytes);

    D3D12_HEAP_PROPERTIES readbackHeap{};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC readbackDesc{};
    readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDesc.Width = totalBytes;
    readbackDesc.Height = 1;
    readbackDesc.DepthOrArraySize = 1;
    readbackDesc.MipLevels = 1;
    readbackDesc.Format = DXGI_FORMAT_UNKNOWN;
    readbackDesc.SampleDesc.Count = 1;
    readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    const HRESULT hr = context.device->CreateCommittedResource(
        &readbackHeap,
        D3D12_HEAP_FLAG_NONE,
        &readbackDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&capture.readback));
    if (FAILED(hr)) {
        capture = {};
        return false;
    }

    D3D12_RESOURCE_BARRIER copyBarrier = TransitionBarrier(
        context.backBuffer,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    context.commandList->ResourceBarrier(1, &copyBarrier);

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = context.backBuffer;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = capture.readback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = capture.footprint;

    context.commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER presentBarrier = TransitionBarrier(
        context.backBuffer,
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_PRESENT);
    context.commandList->ResourceBarrier(1, &presentBarrier);
    return true;
}

} // namespace

bool TransitionBackBufferForPresent(const PresentContext& context) {
    if (!context.backBufferUsedAsRenderTarget) {
        return true;
    }
    if (!context.commandList || !context.backBuffer) {
        return false;
    }

    if (context.captureVisualValidation && CaptureBackBuffer(context)) {
        return true;
    }

    D3D12_RESOURCE_BARRIER barrier = TransitionBarrier(
        context.backBuffer,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    context.commandList->ResourceBarrier(1, &barrier);
    return true;
}

} // namespace Cortex::Graphics::BackBufferPresentPass
