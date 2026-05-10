#include "DX12Raytracing.h"

#include "DX12Device.h"
#include "DescriptorHeap.h"

#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Cortex::Graphics {

void DX12RaytracingContext::SetAccelerationStructureBudgets(uint64_t maxBLASBytesTotal,
                                                            uint64_t maxBLASBuildBytesPerFrame) {
    if (maxBLASBytesTotal > 0) {
        m_maxBLASBytesTotal = maxBLASBytesTotal;
    }
    if (maxBLASBuildBytesPerFrame > 0) {
        m_maxBLASBuildBytesPerFrame = maxBLASBuildBytesPerFrame;
    }
}

Result<void> DX12RaytracingContext::Initialize(DX12Device* device, DescriptorHeapManager* descriptors) {
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

    m_descriptors = descriptors;
    m_rtxWidth = 0;
    m_rtxHeight = 0;

    // Allocate persistent per-frame/per-pass descriptors. The renderer passes
    // the current source descriptors every frame; the RT context copies them
    // into these stable slots immediately before DispatchRays.
    if (m_descriptors) {
        auto descriptorResult = AllocateDispatchDescriptorTables();
        if (descriptorResult.IsErr()) {
            spdlog::warn("DX12RaytracingContext: failed to allocate RT dispatch descriptor tables: {}",
                         descriptorResult.Error());
        }
    }

    auto rootResult = CreateGlobalRootSignature();
    if (rootResult.IsErr() || !m_rtGlobalRootSignature) {
        return Result<void>::Ok();
    }

    auto shadowResult = InitializeShadowPipeline();
    if (shadowResult.IsErr() || !m_rtStateObject || !m_rtShaderTable) {
        return Result<void>::Ok();
    }

    auto reflectionResult = InitializeReflectionPipeline();
    if (reflectionResult.IsErr() || !m_rtReflStateObject || !m_rtReflShaderTable) {
        return Result<void>::Ok();
    }

    auto giResult = InitializeGIPipeline();
    if (giResult.IsErr()) {
        return Result<void>::Ok();
    }

    return Result<void>::Ok();
}

Result<void> DX12RaytracingContext::AllocateDispatchDescriptorTables() {
    if (!m_descriptors) {
        return Result<void>::Err("descriptor manager is null");
    }

    auto allocate = [this](DescriptorHandle& out, const char* label) -> Result<void> {
        auto handle = m_descriptors->AllocateCBV_SRV_UAV();
        if (handle.IsErr()) {
            return Result<void>::Err(std::string("failed to allocate ") + label + ": " + handle.Error());
        }
        out = handle.Value();
        return Result<void>::Ok();
    };

    for (uint32_t frame = 0; frame < kRTFrameCount; ++frame) {
        for (uint32_t slot = 0; slot < kShadowDescriptorCount; ++slot) {
            auto result = allocate(m_shadowDispatchDescriptors[frame][slot], "RT shadow dispatch descriptor");
            if (result.IsErr()) {
                return result;
            }
        }
        for (uint32_t slot = 0; slot < kReflectionDescriptorCount; ++slot) {
            auto result = allocate(m_reflectionDispatchDescriptors[frame][slot], "RT reflection dispatch descriptor");
            if (result.IsErr()) {
                return result;
            }
        }
        for (uint32_t slot = 0; slot < kGIDescriptorCount; ++slot) {
            auto result = allocate(m_giDispatchDescriptors[frame][slot], "RT GI dispatch descriptor");
            if (result.IsErr()) {
                return result;
            }
        }
    }

    m_dispatchDescriptorTablesValid = true;
    return Result<void>::Ok();
}

uint32_t DX12RaytracingContext::GetDescriptorFrameIndex() const {
    return static_cast<uint32_t>(m_currentFrameIndex % kRTFrameCount);
}

void DX12RaytracingContext::Shutdown() {
    if (m_device5) {
        spdlog::info("DX12RaytracingContext shutdown");
    }

    m_rtShaderTable.Reset();
    m_rtReflShaderTable.Reset();
    m_rtGIShaderTable.Reset();
    m_rtStateProps.Reset();
    m_rtReflStateProps.Reset();
    m_rtGIStateProps.Reset();
    m_rtStateObject.Reset();
    m_rtReflStateObject.Reset();
    m_rtGIStateObject.Reset();
    m_rtGlobalRootSignature.Reset();

    m_tlasScratch.Reset();
    m_tlas.Reset();
    m_instanceBuffer.Reset();
    m_rtMaterialBuffer.Reset();
    m_blasCache.clear();
    m_totalBLASBytes = 0;
    m_totalTLASBytes = 0;

    m_device5.Reset();
    m_descriptors = nullptr;
    m_rtxWidth = 0;
    m_rtxHeight = 0;
    m_instanceBufferSize = 0;
    m_rtMaterialBufferSize = 0;
    m_rtMaterials.clear();
}

void DX12RaytracingContext::OnResize(uint32_t width, uint32_t height) {
    if (m_rtxWidth == width && m_rtxHeight == height) {
        return;
    }
    m_rtxWidth = width;
    m_rtxHeight = height;
}

void DX12RaytracingContext::SetCameraParams(const glm::vec3& positionWS,
                                            const glm::vec3& forwardWS,
                                            float nearPlane,
                                            float farPlane) {
    m_cameraPosWS     = positionWS;
    float fwdLenSq    = glm::dot(forwardWS, forwardWS);
    m_cameraForwardWS = (fwdLenSq > 0.0f)
        ? glm::normalize(forwardWS)
        : glm::vec3(0.0f, 0.0f, 1.0f);
    m_cameraNearPlane = nearPlane;
    m_cameraFarPlane  = farPlane;
    m_hasCamera       = true;
}

} // namespace Cortex::Graphics
