#include "GPUCulling.h"
#include "RHI/DX12Device.h"
#include "RHI/DX12Pipeline.h"

namespace Cortex::Graphics {

namespace {

constexpr D3D12_DESCRIPTOR_RANGE_FLAGS kDynamicDescriptorRangeFlags =
    static_cast<D3D12_DESCRIPTOR_RANGE_FLAGS>(
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
        D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);

} // namespace
Result<void> GPUCullingPipeline::CreateRootSignature() {
    // Root signature for compute culling:
    // 0: CBV - Cull constants (view-proj, frustum planes, camera pos)
    // 1: SRV - Instance buffer (input, all instances)
    // 2: SRV - All indirect commands (input)
    // 3: SRV - Occlusion history (input)
    // 4: UAV - Visible command buffer (output)
    // 5: UAV - Command count buffer (atomic append)
    // 6: UAV - Occlusion history (output)
    // 7: UAV - Debug counters/sample (u3)
    // 8: UAV - Visibility mask (u4, one uint32 per instance)
    // 9: SRV table - HZB texture (t2)

    D3D12_ROOT_PARAMETER1 rootParams[10] = {};
    D3D12_DESCRIPTOR_RANGE1 hzbRange{};
    hzbRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    hzbRange.NumDescriptors = 1;
    hzbRange.BaseShaderRegister = 2;
    hzbRange.RegisterSpace = 0;
    // HZB descriptors are refreshed per frame and the data changes between dispatches.
    hzbRange.Flags = kDynamicDescriptorRangeFlags;
    hzbRange.OffsetInDescriptorsFromTableStart = 0;

    // CBV for constants
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].Descriptor.RegisterSpace = 0;
    rootParams[0].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // SRV for instance buffer
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParams[1].Descriptor.ShaderRegister = 0;
    rootParams[1].Descriptor.RegisterSpace = 0;
    rootParams[1].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // SRV for all command buffer
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParams[2].Descriptor.ShaderRegister = 1;
    rootParams[2].Descriptor.RegisterSpace = 0;
    rootParams[2].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // SRV for occlusion history input
    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParams[3].Descriptor.ShaderRegister = 3;
    rootParams[3].Descriptor.RegisterSpace = 0;
    rootParams[3].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
    rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // UAV for visible command buffer
    rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rootParams[4].Descriptor.ShaderRegister = 0;
    rootParams[4].Descriptor.RegisterSpace = 0;
    rootParams[4].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
    rootParams[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // UAV for command count buffer
    rootParams[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rootParams[5].Descriptor.ShaderRegister = 1;
    rootParams[5].Descriptor.RegisterSpace = 0;
    rootParams[5].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
    rootParams[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // UAV for occlusion history output
    rootParams[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rootParams[6].Descriptor.ShaderRegister = 2;
    rootParams[6].Descriptor.RegisterSpace = 0;
    rootParams[6].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
    rootParams[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // UAV for debug counters/sample (raw buffer)
    rootParams[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rootParams[7].Descriptor.ShaderRegister = 3;
    rootParams[7].Descriptor.RegisterSpace = 0;
    rootParams[7].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
    rootParams[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // UAV for per-instance visibility mask (raw buffer)
    rootParams[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rootParams[8].Descriptor.ShaderRegister = 4;
    rootParams[8].Descriptor.RegisterSpace = 0;
    rootParams[8].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
    rootParams[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // SRV descriptor table for HZB texture (t2)
    rootParams[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[9].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[9].DescriptorTable.pDescriptorRanges = &hzbRange;
    rootParams[9].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootSigDesc.Desc_1_1.NumParameters = static_cast<UINT>(_countof(rootParams));
    rootSigDesc.Desc_1_1.pParameters = rootParams;
    rootSigDesc.Desc_1_1.NumStaticSamplers = 0;
    rootSigDesc.Desc_1_1.pStaticSamplers = nullptr;
    rootSigDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3D12SerializeVersionedRootSignature(&rootSigDesc, &serializedRootSig, &errorBlob);
    if (FAILED(hr)) {
        std::string errMsg = "Failed to serialize GPU culling root signature";
        if (errorBlob) {
            errMsg += ": ";
            errMsg += static_cast<const char*>(errorBlob->GetBufferPointer());
        }
        return Result<void>::Err(errMsg);
    }

    hr = m_device->GetDevice()->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(&m_rootSignature)
    );

    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create GPU culling root signature");
    }

    return Result<void>::Ok();
}

Result<void> GPUCullingPipeline::CreateComputePipeline() {
    auto csResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/GPUCulling.hlsl",
        "CSMain",
        "cs_5_1"
    );
    if (csResult.IsErr()) {
        return Result<void>::Err("Failed to compile GPU culling shader: " + csResult.Error());
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSignature.Get();
    auto csBytecode = csResult.Value().GetBytecode();
    psoDesc.CS = csBytecode;

    HRESULT hr = m_device->GetDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_cullPipeline));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create GPU culling pipeline state");
    }

    return Result<void>::Ok();
}

Result<void> GPUCullingPipeline::CreateCommandSignature(ID3D12RootSignature* rootSignature) {
    if (!rootSignature) {
        return Result<void>::Err("CreateCommandSignature requires a valid graphics root signature");
    }

    D3D12_INDIRECT_ARGUMENT_DESC args[5] = {};
    args[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
    args[0].ConstantBufferView.RootParameterIndex = 0;  // ObjectConstants (b0)

    args[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
    args[1].ConstantBufferView.RootParameterIndex = 2;  // MaterialConstants (b2)

    args[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
    args[2].VertexBuffer.Slot = 0;

    args[3].Type = D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW;

    args[4].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

    D3D12_COMMAND_SIGNATURE_DESC cmdSigDesc = {};
    cmdSigDesc.ByteStride = sizeof(IndirectCommand);
    cmdSigDesc.NumArgumentDescs = static_cast<UINT>(sizeof(args) / sizeof(args[0]));
    cmdSigDesc.pArgumentDescs = args;
    cmdSigDesc.NodeMask = 0;

    HRESULT hr = m_device->GetDevice()->CreateCommandSignature(
        &cmdSigDesc,
        rootSignature,
        IID_PPV_ARGS(&m_commandSignature)
    );

    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create command signature for ExecuteIndirect");
    }

    return Result<void>::Ok();
}

Result<void> GPUCullingPipeline::SetGraphicsRootSignature(ID3D12RootSignature* rootSignature) {
    if (m_commandSignature) {
        return Result<void>::Ok();
    }
    return CreateCommandSignature(rootSignature);
}

} // namespace Cortex::Graphics

