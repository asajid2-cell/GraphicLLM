#include "VisibilityBuffer.h"
#include "RHI/DX12Device.h"
#include "RHI/DX12Pipeline.h"
#include "RHI/DescriptorHeap.h"

#include <spdlog/spdlog.h>
#include <d3dcompiler.h>

namespace Cortex::Graphics {
Result<void> VisibilityBufferRenderer::CreateClusteredLightPipeline() {
    ID3D12Device* device = m_device->GetDevice();
    HRESULT hr = S_OK;

    // Clustered Light Culling Pipeline (Compute)
    // ========================================================================
    {
        auto clusterCS = ShaderCompiler::CompileFromFile(
            "assets/shaders/ClusteredLightCulling.hlsl",
            "CSMain",
            "cs_6_6"
        );
        if (clusterCS.IsErr()) {
            return Result<void>::Err("Failed to compile ClusteredLightCulling CS: " + clusterCS.Error());
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC clusterPso{};
        clusterPso.pRootSignature = m_clusterRootSignature.Get();
        clusterPso.CS = { clusterCS.Value().data.data(), clusterCS.Value().data.size() };

        hr = device->CreateComputePipelineState(&clusterPso, IID_PPV_ARGS(&m_clusterPipeline));
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create clustered light culling PSO");
        }

        m_clusterCount = m_clusterCountX * m_clusterCountY * m_clusterCountZ;
        if (!m_clusterRangesBuffer || !m_clusterLightIndicesBuffer) {
            D3D12_HEAP_PROPERTIES heapProps{};
            heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

            D3D12_RESOURCE_DESC rangesDesc{};
            rangesDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rangesDesc.Width = static_cast<UINT64>(m_clusterCount) * sizeof(uint32_t) * 2ull; // uint2
            rangesDesc.Height = 1;
            rangesDesc.DepthOrArraySize = 1;
            rangesDesc.MipLevels = 1;
            rangesDesc.SampleDesc.Count = 1;
            rangesDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            rangesDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            HRESULT hr2 = m_device->GetDevice()->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &rangesDesc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS(&m_clusterRangesBuffer)
            );
            if (FAILED(hr2)) {
                return Result<void>::Err("Failed to create clustered light ranges buffer");
            }
            m_clusterRangesBuffer->SetName(L"VB_ClusterRanges");
            m_clusterRangesState = D3D12_RESOURCE_STATE_COMMON;

            D3D12_RESOURCE_DESC indicesDesc{};
            indicesDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            indicesDesc.Width = static_cast<UINT64>(m_clusterCount) * static_cast<UINT64>(m_maxLightsPerCluster) * sizeof(uint32_t);
            indicesDesc.Height = 1;
            indicesDesc.DepthOrArraySize = 1;
            indicesDesc.MipLevels = 1;
            indicesDesc.SampleDesc.Count = 1;
            indicesDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            indicesDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            hr2 = m_device->GetDevice()->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &indicesDesc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS(&m_clusterLightIndicesBuffer)
            );
            if (FAILED(hr2)) {
                return Result<void>::Err("Failed to create clustered light indices buffer");
            }
            m_clusterLightIndicesBuffer->SetName(L"VB_ClusterLightIndices");
            m_clusterLightIndicesState = D3D12_RESOURCE_STATE_COMMON;
        }

        // Create persistent SRVs for the clustered light lists so forward+ and
        // deferred shaders can sample them via ResourceDescriptorHeap[].
        if (m_clusterRangesSRV.IsValid() && m_clusterRangesBuffer) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.FirstElement = 0;
            srvDesc.Buffer.NumElements = m_clusterCount;
            srvDesc.Buffer.StructureByteStride = sizeof(uint32_t) * 2; // uint2
            srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            m_device->GetDevice()->CreateShaderResourceView(m_clusterRangesBuffer.Get(), &srvDesc, m_clusterRangesSRV.cpu);
        }
        if (m_clusterLightIndicesSRV.IsValid() && m_clusterLightIndicesBuffer) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.FirstElement = 0;
            srvDesc.Buffer.NumElements = m_clusterCount * m_maxLightsPerCluster;
            srvDesc.Buffer.StructureByteStride = sizeof(uint32_t);
            srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            m_device->GetDevice()->CreateShaderResourceView(m_clusterLightIndicesBuffer.Get(), &srvDesc, m_clusterLightIndicesSRV.cpu);
        }

        spdlog::info("VisibilityBuffer: Clustered light culling pipeline created (clusters={}x{}x{}, maxLightsPerCluster={})",
                     m_clusterCountX, m_clusterCountY, m_clusterCountZ, m_maxLightsPerCluster);
    }


    return Result<void>::Ok();
}

} // namespace Cortex::Graphics

