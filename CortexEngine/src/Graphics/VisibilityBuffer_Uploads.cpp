#include "VisibilityBuffer.h"
#include "RHI/DX12Device.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace Cortex::Graphics {

Result<void> VisibilityBufferRenderer::UpdateInstances(
    ID3D12GraphicsCommandList* cmdList,
    const std::vector<VBInstanceData>& instances
) {
    (void)cmdList;

    if (instances.empty()) {
        m_instanceCount = 0;
        return Result<void>::Ok();
    }

    if (instances.size() > m_maxInstances) {
        return Result<void>::Err("Too many instances for visibility buffer");
    }

    // Use frame-indexed buffer to prevent race conditions with GPU
    uint32_t frameIdx = m_frameIndex;
    if (!m_instanceBufferMapped[frameIdx]) {
        return Result<void>::Err("Instance buffer " + std::to_string(frameIdx) + " not mapped");
    }

    // Write to the current frame's buffer (GPU reads from previous frame's buffer)
    memcpy(m_instanceBufferMapped[frameIdx], instances.data(), instances.size() * sizeof(VBInstanceData));

    m_instanceCount = static_cast<uint32_t>(instances.size());
    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::UpdateMaterials(
    ID3D12GraphicsCommandList* cmdList,
    const std::vector<VBMaterialConstants>& materials
) {
    (void)cmdList;

    m_materialCount = static_cast<uint32_t>(materials.size());
    if (materials.empty()) {
        return Result<void>::Ok();
    }

    // Use frame-indexed buffer to prevent race conditions
    uint32_t frameIdx = m_frameIndex;

    if (materials.size() > m_maxMaterials) {
        if (m_flushCallback) {
            m_flushCallback();
        }

        // Grow capacity (next power-of-two-ish) to reduce churn.
        uint32_t newCap = m_maxMaterials;
        while (newCap < static_cast<uint32_t>(materials.size())) {
            newCap = std::max(1u, newCap * 2u);
        }
        m_maxMaterials = newCap;

        // Resize all triple-buffered material buffers
        for (uint32_t i = 0; i < kVBFrameCount; ++i) {
            if (m_materialBuffer[i] && m_materialBufferMapped[i]) {
                m_materialBuffer[i]->Unmap(0, nullptr);
                m_materialBufferMapped[i] = nullptr;
            }
            m_materialBuffer[i].Reset();

            D3D12_HEAP_PROPERTIES heapProps{};
            heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

            D3D12_RESOURCE_DESC bufferDesc{};
            bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            bufferDesc.Width = static_cast<UINT64>(m_maxMaterials) * sizeof(VBMaterialConstants);
            bufferDesc.Height = 1;
            bufferDesc.DepthOrArraySize = 1;
            bufferDesc.MipLevels = 1;
            bufferDesc.SampleDesc.Count = 1;
            bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &bufferDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&m_materialBuffer[i])
            );
            if (FAILED(hr)) {
                return Result<void>::Err("Failed to resize VB material constants buffer " + std::to_string(i));
            }
            std::wstring bufferName = L"VB_MaterialBuffer_" + std::to_wstring(i);
            m_materialBuffer[i]->SetName(bufferName.c_str());

            D3D12_RANGE readRange{0, 0};
            hr = m_materialBuffer[i]->Map(0, &readRange, reinterpret_cast<void**>(&m_materialBufferMapped[i]));
            if (FAILED(hr) || !m_materialBufferMapped[i]) {
                return Result<void>::Err("Failed to map resized VB material constants buffer " + std::to_string(i));
            }
        }
    }

    if (!m_materialBufferMapped[frameIdx]) {
        return Result<void>::Err("VB material constants buffer " + std::to_string(frameIdx) + " not mapped");
    }

    memcpy(m_materialBufferMapped[frameIdx], materials.data(), materials.size() * sizeof(VBMaterialConstants));
    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::UpdateReflectionProbes(
    ID3D12GraphicsCommandList* cmdList,
    const std::vector<VBReflectionProbe>& probes
) {
    (void)cmdList;

    m_reflectionProbeCount = static_cast<uint32_t>(probes.size());
    if (probes.empty()) {
        return Result<void>::Ok();
    }

    if (probes.size() > m_maxReflectionProbes) {
        if (m_flushCallback) {
            m_flushCallback();
        }

        uint32_t newCap = m_maxReflectionProbes;
        while (newCap < static_cast<uint32_t>(probes.size())) {
            newCap = std::max(1u, newCap * 2u);
        }
        m_maxReflectionProbes = newCap;

        if (m_reflectionProbeBuffer && m_reflectionProbeBufferMapped) {
            m_reflectionProbeBuffer->Unmap(0, nullptr);
            m_reflectionProbeBufferMapped = nullptr;
        }
        m_reflectionProbeBuffer.Reset();

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = static_cast<UINT64>(m_maxReflectionProbes) * sizeof(VBReflectionProbe);
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_reflectionProbeBuffer)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to resize VB reflection probe buffer");
        }
        m_reflectionProbeBuffer->SetName(L"VB_ReflectionProbeBuffer");

        D3D12_RANGE readRange{0, 0};
        hr = m_reflectionProbeBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_reflectionProbeBufferMapped));
        if (FAILED(hr) || !m_reflectionProbeBufferMapped) {
            return Result<void>::Err("Failed to map resized VB reflection probe buffer");
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = m_maxReflectionProbes;
        srvDesc.Buffer.StructureByteStride = sizeof(VBReflectionProbe);
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        m_device->GetDevice()->CreateShaderResourceView(
            m_reflectionProbeBuffer.Get(), &srvDesc, m_reflectionProbeSRV.cpu
        );
    }

    if (!m_reflectionProbeBufferMapped) {
        return Result<void>::Err("VB reflection probe buffer not mapped");
    }

    memcpy(m_reflectionProbeBufferMapped, probes.data(), probes.size() * sizeof(VBReflectionProbe));
    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::UpdateLocalLights(
    ID3D12GraphicsCommandList* cmdList,
    const std::vector<Light>& localLights
) {
    (void)cmdList;

    m_localLightCount = static_cast<uint32_t>(localLights.size());
    if (localLights.empty()) {
        return Result<void>::Ok();
    }

    if (localLights.size() > m_maxLocalLights) {
        if (m_flushCallback) {
            m_flushCallback();
        }

        uint32_t newCap = m_maxLocalLights;
        while (newCap < static_cast<uint32_t>(localLights.size())) {
            newCap = std::max(1u, newCap * 2u);
        }
        m_maxLocalLights = newCap;

        if (m_localLightsBuffer && m_localLightsBufferMapped) {
            m_localLightsBuffer->Unmap(0, nullptr);
            m_localLightsBufferMapped = nullptr;
        }
        m_localLightsBuffer.Reset();

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = static_cast<UINT64>(m_maxLocalLights) * sizeof(Light);
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_localLightsBuffer)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to resize VB local lights buffer");
        }
        m_localLightsBuffer->SetName(L"VB_LocalLightsBuffer");

        D3D12_RANGE readRange{0, 0};
        hr = m_localLightsBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_localLightsBufferMapped));
        if (FAILED(hr) || !m_localLightsBufferMapped) {
            return Result<void>::Err("Failed to map resized VB local lights buffer");
        }

        if (m_localLightsSRV.IsValid()) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.FirstElement = 0;
            srvDesc.Buffer.NumElements = m_maxLocalLights;
            srvDesc.Buffer.StructureByteStride = sizeof(Light);
            srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            m_device->GetDevice()->CreateShaderResourceView(m_localLightsBuffer.Get(), &srvDesc, m_localLightsSRV.cpu);
        }
    }

    if (!m_localLightsBufferMapped) {
        // First-use allocation (keep consistent with the grow path).
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = static_cast<UINT64>(m_maxLocalLights) * sizeof(Light);
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_localLightsBuffer)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create VB local lights buffer");
        }
        m_localLightsBuffer->SetName(L"VB_LocalLightsBuffer");

        D3D12_RANGE readRange{0, 0};
        hr = m_localLightsBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_localLightsBufferMapped));
        if (FAILED(hr) || !m_localLightsBufferMapped) {
            return Result<void>::Err("Failed to map VB local lights buffer");
        }

        if (m_localLightsSRV.IsValid()) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.FirstElement = 0;
            srvDesc.Buffer.NumElements = m_maxLocalLights;
            srvDesc.Buffer.StructureByteStride = sizeof(Light);
            srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            m_device->GetDevice()->CreateShaderResourceView(m_localLightsBuffer.Get(), &srvDesc, m_localLightsSRV.cpu);
        }
    }

    memcpy(m_localLightsBufferMapped, localLights.data(), localLights.size() * sizeof(Light));
    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::UpdateMeshTable(const std::vector<VBMeshDrawInfo>& meshDraws) {
    const uint32_t meshCount = static_cast<uint32_t>(meshDraws.size());
    const uint32_t frameIdx = m_frameIndex;

    if (meshCount == 0) {
        m_meshCount = 0;
        return Result<void>::Ok();
    }

    if (meshCount > m_maxMeshes) {
        if (m_flushCallback) {
            m_flushCallback();
        }

        uint32_t newCap = m_maxMeshes;
        while (newCap < meshCount) {
            newCap = std::max(1u, newCap * 2u);
        }
        m_maxMeshes = newCap;

        for (uint32_t i = 0; i < kVBFrameCount; ++i) {
            if (m_meshTableBuffer[i] && m_meshTableBufferMapped[i]) {
                m_meshTableBuffer[i]->Unmap(0, nullptr);
                m_meshTableBufferMapped[i] = nullptr;
            }
            m_meshTableBuffer[i].Reset();

            D3D12_HEAP_PROPERTIES heapProps{};
            heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

            D3D12_RESOURCE_DESC bufferDesc{};
            bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            bufferDesc.Width = static_cast<UINT64>(m_maxMeshes) * sizeof(VBMeshTableEntry);
            bufferDesc.Height = 1;
            bufferDesc.DepthOrArraySize = 1;
            bufferDesc.MipLevels = 1;
            bufferDesc.SampleDesc.Count = 1;
            bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &bufferDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&m_meshTableBuffer[i])
            );
            if (FAILED(hr)) {
                return Result<void>::Err("Failed to resize VB mesh table buffer " + std::to_string(i));
            }
            std::wstring meshName = L"VB_MeshTableBuffer_" + std::to_wstring(i);
            m_meshTableBuffer[i]->SetName(meshName.c_str());

            D3D12_RANGE readRange{0, 0};
            hr = m_meshTableBuffer[i]->Map(0, &readRange, reinterpret_cast<void**>(&m_meshTableBufferMapped[i]));
            if (FAILED(hr) || !m_meshTableBufferMapped[i]) {
                return Result<void>::Err("Failed to map resized VB mesh table buffer " + std::to_string(i));
            }
        }
    }

    if (!m_meshTableBufferMapped[frameIdx] || !m_meshTableBuffer[frameIdx]) {
        return Result<void>::Err("VB mesh table buffer " + std::to_string(frameIdx) + " not mapped");
    }

    std::vector<VBMeshTableEntry> meshEntries;
    meshEntries.resize(meshCount);

    for (uint32_t meshIdx = 0; meshIdx < meshCount; ++meshIdx) {
        const auto& draw = meshDraws[meshIdx];

        VBMeshTableEntry entry{};
        entry.vertexBufferIndex = draw.vertexBufferIndex;
        entry.indexBufferIndex = draw.indexBufferIndex;
        entry.vertexStrideBytes = draw.vertexStrideBytes;
        entry.indexFormat = draw.indexFormat;

        if (entry.vertexBufferIndex == 0xFFFFFFFFu || entry.indexBufferIndex == 0xFFFFFFFFu) {
            return Result<void>::Err("VB mesh table missing persistent VB/IB SRV indices (mesh upload must register SRVs)");
        }

        meshEntries[meshIdx] = entry;
    }

    memcpy(m_meshTableBufferMapped[frameIdx], meshEntries.data(), meshEntries.size() * sizeof(VBMeshTableEntry));
    m_meshCount = meshCount;
    return Result<void>::Ok();
}

} // namespace Cortex::Graphics
