#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <memory>
#include <cstdint>
#include <spdlog/spdlog.h>
#include "Utils/Result.h"

using Microsoft::WRL::ComPtr;

namespace Cortex {
    namespace Scene {
        class ECS_Registry;
        struct MeshData;
    }
}

namespace Cortex::Graphics {

class DX12Device;
class DescriptorHeapManager;

// Lightweight DXR context used as a growth point for future BLAS/TLAS and
// ray tracing work. In this phase it only performs capability checks and
// logs stub calls so that the rest of the engine can safely toggle DXR
// without requiring full acceleration-structure support.
class DX12RaytracingContext {
public:
    DX12RaytracingContext() = default;
    ~DX12RaytracingContext() = default;

    DX12RaytracingContext(const DX12RaytracingContext&) = delete;
    DX12RaytracingContext& operator=(const DX12RaytracingContext&) = delete;
    DX12RaytracingContext(DX12RaytracingContext&&) = default;
    DX12RaytracingContext& operator=(DX12RaytracingContext&&) = default;

    Result<void> Initialize(DX12Device* device, DescriptorHeapManager* descriptors);
    void Shutdown();

    void OnResize(uint32_t width, uint32_t height);

    // Stubs for future BLAS/TLAS integration.
    void RebuildBLASForMesh(const std::shared_ptr<Scene::MeshData>& mesh);
    void BuildTLAS(Scene::ECS_Registry* registry, ID3D12GraphicsCommandList4* cmdList);

    void DispatchRayTracing(ID3D12GraphicsCommandList4* cmdList);

private:
    ComPtr<ID3D12Device5> m_device5;
    uint32_t m_rtxWidth = 0;
    uint32_t m_rtxHeight = 0;
};

} // namespace Cortex::Graphics
