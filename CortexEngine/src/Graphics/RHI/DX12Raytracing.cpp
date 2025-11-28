#include "DX12Raytracing.h"

#include "DX12Device.h"
#include "DescriptorHeap.h"
#include "Scene/ECS_Registry.h"

namespace Cortex::Graphics {

Result<void> DX12RaytracingContext::Initialize(DX12Device* device, DescriptorHeapManager* /*descriptors*/) {
    if (!device) {
        return Result<void>::Err("DX12RaytracingContext::Initialize: device is null");
    }

    ID3D12Device* baseDevice = device->GetDevice();
    if (!baseDevice) {
        return Result<void>::Err("DX12RaytracingContext::Initialize: base D3D12 device is null");
    }

    HRESULT hr = baseDevice->QueryInterface(IID_PPV_ARGS(&m_device5));
    if (FAILED(hr) || !m_device5) {
        return Result<void>::Err("DX12RaytracingContext::Initialize: DXR ID3D12Device5 not available");
    }

    m_rtxWidth = 0;
    m_rtxHeight = 0;

    spdlog::info("DX12RaytracingContext initialized (DXR device detected; no AS builds yet)");
    return Result<void>::Ok();
}

void DX12RaytracingContext::Shutdown() {
    if (m_device5) {
        spdlog::info("DX12RaytracingContext shutdown");
    }

    m_device5.Reset();
    m_rtxWidth = 0;
    m_rtxHeight = 0;
}

void DX12RaytracingContext::OnResize(uint32_t width, uint32_t height) {
    // Avoid redundant work and log noise when the dimensions have not changed.
    if (m_rtxWidth == width && m_rtxHeight == height) {
        return;
    }
    m_rtxWidth = width;
    m_rtxHeight = height;
}

void DX12RaytracingContext::RebuildBLASForMesh(const std::shared_ptr<Scene::MeshData>& /*mesh*/) {
    // Stub in this variant; the extended DXR implementation used by the
    // renderer provides a full BLAS builder.
}

void DX12RaytracingContext::BuildTLAS(Scene::ECS_Registry* /*registry*/,
                                      ID3D12GraphicsCommandList4* /*cmdList*/) {
    // Stub in this variant; the extended DXR implementation used by the
    // renderer provides a full TLAS builder.
}

void DX12RaytracingContext::DispatchRayTracing(ID3D12GraphicsCommandList4* /*cmdList*/) {
    // Intentionally silent to avoid per-frame log spam while the DXR path is
    // still a no-op hook for future ray-traced effects.
}

} // namespace Cortex::Graphics
