#include "VisibilityBuffer.h"
#include "RHI/DX12Device.h"
#include "RHI/DX12Pipeline.h"
#include "RHI/DescriptorHeap.h"

#include <spdlog/spdlog.h>
#include <d3dcompiler.h>

namespace Cortex::Graphics {
namespace {

constexpr D3D12_DESCRIPTOR_RANGE_FLAGS kDynamicDescriptorRangeFlags =
    static_cast<D3D12_DESCRIPTOR_RANGE_FLAGS>(
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
        D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);

} // namespace
Result<void> VisibilityBufferRenderer::CreateBRDFLUTPipeline() {
    ID3D12Device* device = m_device->GetDevice();
    HRESULT hr = S_OK;

    // BRDF LUT Generation Pipeline (Compute)
    // ========================================================================
    {
        constexpr uint32_t kBrdfLutSize = 256;

        if (!m_brdfLut) {
            D3D12_HEAP_PROPERTIES heapProps{};
            heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

            D3D12_RESOURCE_DESC texDesc{};
            texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            texDesc.Width = kBrdfLutSize;
            texDesc.Height = kBrdfLutSize;
            texDesc.DepthOrArraySize = 1;
            texDesc.MipLevels = 1;
            texDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
            texDesc.SampleDesc.Count = 1;
            texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            HRESULT hr2 = m_device->GetDevice()->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &texDesc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS(&m_brdfLut)
            );
            if (FAILED(hr2)) {
                return Result<void>::Err("Failed to create BRDF LUT texture");
            }
            m_brdfLut->SetName(L"BRDFLUT");
            m_brdfLutState = D3D12_RESOURCE_STATE_COMMON;

            auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
            if (srvResult.IsErr()) {
                return Result<void>::Err("Failed to allocate BRDF LUT SRV");
            }
            m_brdfLutSRV = srvResult.Value();

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = 1;
            m_device->GetDevice()->CreateShaderResourceView(m_brdfLut.Get(), &srvDesc, m_brdfLutSRV.cpu);

            auto uavResult = m_descriptorManager->AllocateCBV_SRV_UAV();
            if (uavResult.IsErr()) {
                return Result<void>::Err("Failed to allocate BRDF LUT UAV");
            }
            m_brdfLutUAV = uavResult.Value();

            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            m_device->GetDevice()->CreateUnorderedAccessView(m_brdfLut.Get(), nullptr, &uavDesc, m_brdfLutUAV.cpu);

            m_brdfLutReady = false;
        }

        if (!m_brdfLutRootSignature) {
            D3D12_DESCRIPTOR_RANGE1 uavRange{};
            uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            uavRange.NumDescriptors = 1;
            uavRange.BaseShaderRegister = 0;
            uavRange.RegisterSpace = 0;
            uavRange.Flags = kDynamicDescriptorRangeFlags;
            uavRange.OffsetInDescriptorsFromTableStart = 0;

            D3D12_ROOT_PARAMETER1 params[2] = {};

            // b0: {width,height,rcpWidth,rcpHeight}
            params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            params[0].Constants.ShaderRegister = 0;
            params[0].Constants.RegisterSpace = 0;
            params[0].Constants.Num32BitValues = 4;
            params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            // u0: BRDF LUT UAV
            params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[1].DescriptorTable.NumDescriptorRanges = 1;
            params[1].DescriptorTable.pDescriptorRanges = &uavRange;
            params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootDesc{};
            rootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
            rootDesc.Desc_1_1.NumParameters = _countof(params);
            rootDesc.Desc_1_1.pParameters = params;
            rootDesc.Desc_1_1.NumStaticSamplers = 0;
            rootDesc.Desc_1_1.pStaticSamplers = nullptr;
            rootDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

            ComPtr<ID3DBlob> signature, error;
            HRESULT hr2 = D3D12SerializeVersionedRootSignature(&rootDesc, &signature, &error);
            if (FAILED(hr2)) {
                if (error) {
                    return Result<void>::Err(std::string("Failed to serialize BRDF LUT root signature: ") +
                                            static_cast<const char*>(error->GetBufferPointer()));
                }
                return Result<void>::Err("Failed to serialize BRDF LUT root signature");
            }

            hr2 = m_device->GetDevice()->CreateRootSignature(
                0, signature->GetBufferPointer(), signature->GetBufferSize(),
                IID_PPV_ARGS(&m_brdfLutRootSignature)
            );
            if (FAILED(hr2)) {
                return Result<void>::Err("Failed to create BRDF LUT root signature");
            }
        }

        auto brdfCS = ShaderCompiler::CompileFromFile(
            "assets/shaders/BRDFLUT.hlsl",
            "CSMain",
            "cs_6_6"
        );
        if (brdfCS.IsErr()) {
            return Result<void>::Err("Failed to compile BRDF LUT CS: " + brdfCS.Error());
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC brdfPso{};
        brdfPso.pRootSignature = m_brdfLutRootSignature.Get();
        brdfPso.CS = { brdfCS.Value().data.data(), brdfCS.Value().data.size() };

        HRESULT hr2 = device->CreateComputePipelineState(&brdfPso, IID_PPV_ARGS(&m_brdfLutPipeline));
        if (FAILED(hr2)) {
            return Result<void>::Err("Failed to create BRDF LUT PSO");
        }

        spdlog::info("VisibilityBuffer: BRDF LUT pipeline created");
    }


    return Result<void>::Ok();
}

} // namespace Cortex::Graphics

