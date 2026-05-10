#include "DX12Raytracing.h"

#include "DescriptorHeap.h"

#include <algorithm>

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

namespace {

DXGI_FORMAT SrvFormatForResource(ID3D12Resource* resource, DXGI_FORMAT fallback = DXGI_FORMAT_R8G8B8A8_UNORM) {
    if (!resource) {
        return fallback;
    }
    switch (resource->GetDesc().Format) {
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_TYPELESS:
        return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    default:
        return resource->GetDesc().Format;
    }
}

} // namespace

void DX12RaytracingContext::DispatchRayTracing( 
    ID3D12GraphicsCommandList4* cmdList, 
    const DescriptorHandle& depthSrv, 
    const DescriptorHandle& shadowMaskUav, 
    D3D12_GPU_VIRTUAL_ADDRESS frameCBAddress, 
    const DescriptorHandle& shadowEnvTable) { 
    if (!m_device5 || !cmdList || !m_tlas || !m_rtStateObject || 
        !m_rtStateProps || !m_rtShaderTable || 
        !m_rtGlobalRootSignature || !m_descriptors) { 
        return; 
    } 

    if (!depthSrv.IsValid() || !shadowMaskUav.IsValid()) { 
        return; 
    } 

    ID3D12Device* device = m_device5.Get(); 
    if (!device) { 
        return; 
    } 
    if (!m_descriptors) { 
        return; 
    } 
 
    if (!m_dispatchDescriptorTablesValid) {
        return;
    }
    DescriptorHandle* table = m_shadowDispatchDescriptors[GetDescriptorFrameIndex()];
    if (!table[0].IsValid() || !table[1].IsValid() || !table[2].IsValid()) {
        return;
    }

    const DescriptorHandle& rtTlasSrv = table[0];
    const DescriptorHandle& rtDepthSrv = table[1];
    const DescriptorHandle& rtOutUav = table[2];

    { 
        D3D12_SHADER_RESOURCE_VIEW_DESC asSrvDesc{}; 
        asSrvDesc.Format = DXGI_FORMAT_UNKNOWN; 
        asSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE; 
        asSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; 
        asSrvDesc.RaytracingAccelerationStructure.Location = m_tlas->GetGPUVirtualAddress(); 

        device->CreateShaderResourceView( 
            nullptr, 
            &asSrvDesc, 
            rtTlasSrv.cpu); 
    } 

    device->CopyDescriptorsSimple( 
        1, 
        rtDepthSrv.cpu, 
        depthSrv.cpu, 
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); 

    device->CopyDescriptorsSimple( 
        1, 
        rtOutUav.cpu, 
        shadowMaskUav.cpu, 
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); 

    ID3D12DescriptorHeap* heaps[] = { m_descriptors->GetCBV_SRV_UAV_Heap() }; 
    cmdList->SetDescriptorHeaps(1, heaps); 

    cmdList->SetComputeRootSignature(m_rtGlobalRootSignature.Get()); 
    cmdList->SetPipelineState1(m_rtStateObject.Get()); 

    cmdList->SetComputeRootConstantBufferView(0, frameCBAddress); 
    cmdList->SetComputeRootDescriptorTable(1, rtTlasSrv.gpu); 
    cmdList->SetComputeRootDescriptorTable(2, rtDepthSrv.gpu); 
    cmdList->SetComputeRootDescriptorTable(3, rtOutUav.gpu); 
    if (shadowEnvTable.IsValid()) { 
        cmdList->SetComputeRootDescriptorTable(4, shadowEnvTable.gpu); 
    } 
    if (m_rtMaterialBuffer) {
        cmdList->SetComputeRootShaderResourceView(6, m_rtMaterialBuffer->GetGPUVirtualAddress());
    }

    const D3D12_GPU_VIRTUAL_ADDRESS shaderTableVA = m_rtShaderTable->GetGPUVirtualAddress();

    D3D12_DISPATCH_RAYS_DESC desc{};
    desc.RayGenerationShaderRecord.StartAddress = shaderTableVA + 0 * m_rtShaderTableStride;
    desc.RayGenerationShaderRecord.SizeInBytes = m_rtShaderTableStride;

    desc.MissShaderTable.StartAddress = shaderTableVA + 1 * m_rtShaderTableStride;
    desc.MissShaderTable.SizeInBytes = m_rtShaderTableStride;
    desc.MissShaderTable.StrideInBytes = m_rtShaderTableStride;

    desc.HitGroupTable.StartAddress = shaderTableVA + 2 * m_rtShaderTableStride;
    desc.HitGroupTable.SizeInBytes = m_rtShaderTableStride;
    desc.HitGroupTable.StrideInBytes = m_rtShaderTableStride;

    desc.Width = m_rtxWidth;
    desc.Height = m_rtxHeight;
    desc.Depth = 1;

    cmdList->DispatchRays(&desc);
}

void DX12RaytracingContext::DispatchReflections( 
    ID3D12GraphicsCommandList4* cmdList, 
    const DescriptorHandle& depthSrv, 
    const DescriptorHandle& reflectionUav, 
    D3D12_GPU_VIRTUAL_ADDRESS frameCBAddress, 
    const DescriptorHandle& shadowEnvTable, 
    const DescriptorHandle& normalRoughnessSrv, 
    const DescriptorHandle& materialExt2Srv, 
    ID3D12Resource* normalRoughnessResource,
    ID3D12Resource* materialExt2Resource,
    uint32_t dispatchWidth, 
    uint32_t dispatchHeight) { 
    if (!m_device5 || !cmdList || !m_tlas || !m_rtMaterialBuffer || !m_rtReflStateObject || 
        !m_rtReflStateProps || !m_rtReflShaderTable || 
        !m_rtGlobalRootSignature || !m_descriptors) { 
        return; 
    } 

    if (!depthSrv.IsValid() || !reflectionUav.IsValid() || !normalRoughnessSrv.IsValid()) { 
        return; 
    } 

    ID3D12Device* device = m_device5.Get(); 
    if (!device) { 
        return; 
    } 
    if (!m_descriptors) { 
        return; 
    } 
 
    if (!m_dispatchDescriptorTablesValid) {
        return;
    }
    DescriptorHandle* table = m_reflectionDispatchDescriptors[GetDescriptorFrameIndex()];
    if (!table[0].IsValid() || !table[1].IsValid() || !table[2].IsValid() ||
        !table[3].IsValid() || !table[4].IsValid()) {
        return;
    }

    const DescriptorHandle& rtTlasSrv = table[0];
    const DescriptorHandle& rtDepthSrv = table[1];
    const DescriptorHandle& rtOutUav = table[2];
    const DescriptorHandle& rtNormalSrv = table[3];
    const DescriptorHandle& rtMaterialExt2Srv = table[4];

    { 
        D3D12_SHADER_RESOURCE_VIEW_DESC asSrvDesc{}; 
        asSrvDesc.Format = DXGI_FORMAT_UNKNOWN; 
        asSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE; 
        asSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; 
        asSrvDesc.RaytracingAccelerationStructure.Location = m_tlas->GetGPUVirtualAddress(); 

        device->CreateShaderResourceView( 
            nullptr, 
            &asSrvDesc, 
            rtTlasSrv.cpu); 
    } 

    device->CopyDescriptorsSimple( 
        1, 
        rtDepthSrv.cpu, 
        depthSrv.cpu, 
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); 

    device->CopyDescriptorsSimple( 
        1, 
        rtOutUav.cpu, 
        reflectionUav.cpu, 
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); 

    if (normalRoughnessResource) {
        D3D12_SHADER_RESOURCE_VIEW_DESC normalSrvDesc{};
        normalSrvDesc.Format = SrvFormatForResource(normalRoughnessResource);
        normalSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        normalSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        normalSrvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(normalRoughnessResource, &normalSrvDesc, rtNormalSrv.cpu);
    } else {
        device->CopyDescriptorsSimple(
            1,
            rtNormalSrv.cpu,
            normalRoughnessSrv.cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    if (materialExt2Srv.IsValid()) {
        if (materialExt2Resource) {
            D3D12_SHADER_RESOURCE_VIEW_DESC materialExt2Desc{};
            materialExt2Desc.Format = SrvFormatForResource(materialExt2Resource);
            materialExt2Desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            materialExt2Desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            materialExt2Desc.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(materialExt2Resource, &materialExt2Desc, rtMaterialExt2Srv.cpu);
        } else {
            device->CopyDescriptorsSimple(
                1,
                rtMaterialExt2Srv.cpu,
                materialExt2Srv.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    } else {
        D3D12_SHADER_RESOURCE_VIEW_DESC nullMaterialExt2{};
        nullMaterialExt2.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        nullMaterialExt2.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        nullMaterialExt2.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        nullMaterialExt2.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(nullptr, &nullMaterialExt2, rtMaterialExt2Srv.cpu);
    }

    ID3D12DescriptorHeap* heaps[] = { m_descriptors->GetCBV_SRV_UAV_Heap() }; 
    cmdList->SetDescriptorHeaps(1, heaps); 

    cmdList->SetComputeRootSignature(m_rtGlobalRootSignature.Get()); 
    cmdList->SetPipelineState1(m_rtReflStateObject.Get()); 

    cmdList->SetComputeRootConstantBufferView(0, frameCBAddress); 
    cmdList->SetComputeRootDescriptorTable(1, rtTlasSrv.gpu); 
    cmdList->SetComputeRootDescriptorTable(2, rtDepthSrv.gpu); 
    cmdList->SetComputeRootDescriptorTable(3, rtOutUav.gpu); 
    if (shadowEnvTable.IsValid()) { 
        cmdList->SetComputeRootDescriptorTable(4, shadowEnvTable.gpu); 
    } 
    cmdList->SetComputeRootDescriptorTable(5, rtNormalSrv.gpu); 
    cmdList->SetComputeRootShaderResourceView(6, m_rtMaterialBuffer->GetGPUVirtualAddress());

    const D3D12_GPU_VIRTUAL_ADDRESS shaderTableVA = m_rtReflShaderTable->GetGPUVirtualAddress();

    D3D12_DISPATCH_RAYS_DESC desc{};
    desc.RayGenerationShaderRecord.StartAddress = shaderTableVA + 0 * m_rtReflShaderTableStride;
    desc.RayGenerationShaderRecord.SizeInBytes = m_rtReflShaderTableStride;

    desc.MissShaderTable.StartAddress = shaderTableVA + 1 * m_rtReflShaderTableStride;
    desc.MissShaderTable.SizeInBytes = m_rtReflShaderTableStride;
    desc.MissShaderTable.StrideInBytes = m_rtReflShaderTableStride;

    desc.HitGroupTable.StartAddress = shaderTableVA + 2 * m_rtReflShaderTableStride;
    desc.HitGroupTable.SizeInBytes = m_rtReflShaderTableStride;
    desc.HitGroupTable.StrideInBytes = m_rtReflShaderTableStride;

    desc.Width = std::max(1u, dispatchWidth);
    desc.Height = std::max(1u, dispatchHeight);
    desc.Depth = 1;

    cmdList->DispatchRays(&desc);
}

void DX12RaytracingContext::DispatchGI( 
    ID3D12GraphicsCommandList4* cmdList, 
    const DescriptorHandle& depthSrv, 
    const DescriptorHandle& giUav, 
    D3D12_GPU_VIRTUAL_ADDRESS frameCBAddress, 
    const DescriptorHandle& shadowEnvTable, 
    uint32_t dispatchWidth, 
    uint32_t dispatchHeight) { 
    if (!m_device5 || !cmdList || !m_tlas || !m_rtGIStateObject || 
        !m_rtGIStateProps || !m_rtGIShaderTable || 
        !m_rtGlobalRootSignature || !m_descriptors) { 
        return; 
    } 

    if (!depthSrv.IsValid() || !giUav.IsValid()) { 
        return; 
    } 

    ID3D12Device* device = m_device5.Get(); 
    if (!device) { 
        return; 
    } 
    if (!m_descriptors) { 
        return; 
    } 
 
    if (!m_dispatchDescriptorTablesValid) {
        return;
    }
    DescriptorHandle* table = m_giDispatchDescriptors[GetDescriptorFrameIndex()];
    if (!table[0].IsValid() || !table[1].IsValid() || !table[2].IsValid()) {
        return;
    }

    const DescriptorHandle& rtTlasSrv = table[0];
    const DescriptorHandle& rtDepthSrv = table[1];
    const DescriptorHandle& rtOutUav = table[2];

    { 
        D3D12_SHADER_RESOURCE_VIEW_DESC asSrvDesc{}; 
        asSrvDesc.Format = DXGI_FORMAT_UNKNOWN; 
        asSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE; 
        asSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; 
        asSrvDesc.RaytracingAccelerationStructure.Location = m_tlas->GetGPUVirtualAddress(); 

        device->CreateShaderResourceView( 
            nullptr, 
            &asSrvDesc, 
            rtTlasSrv.cpu); 
    } 

    device->CopyDescriptorsSimple( 
        1, 
        rtDepthSrv.cpu, 
        depthSrv.cpu, 
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); 

    device->CopyDescriptorsSimple( 
        1, 
        rtOutUav.cpu, 
        giUav.cpu, 
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); 

    ID3D12DescriptorHeap* heaps[] = { m_descriptors->GetCBV_SRV_UAV_Heap() }; 
    cmdList->SetDescriptorHeaps(1, heaps); 

    cmdList->SetComputeRootSignature(m_rtGlobalRootSignature.Get()); 
    cmdList->SetPipelineState1(m_rtGIStateObject.Get()); 

    cmdList->SetComputeRootConstantBufferView(0, frameCBAddress); 
    cmdList->SetComputeRootDescriptorTable(1, rtTlasSrv.gpu); 
    cmdList->SetComputeRootDescriptorTable(2, rtDepthSrv.gpu); 
    cmdList->SetComputeRootDescriptorTable(3, rtOutUav.gpu); 
    if (shadowEnvTable.IsValid()) { 
        cmdList->SetComputeRootDescriptorTable(4, shadowEnvTable.gpu); 
    } 
    if (m_rtMaterialBuffer) {
        cmdList->SetComputeRootShaderResourceView(6, m_rtMaterialBuffer->GetGPUVirtualAddress());
    }

    const D3D12_GPU_VIRTUAL_ADDRESS shaderTableVA = m_rtGIShaderTable->GetGPUVirtualAddress();

    D3D12_DISPATCH_RAYS_DESC desc{};
    desc.RayGenerationShaderRecord.StartAddress = shaderTableVA + 0 * m_rtGIShaderTableStride;
    desc.RayGenerationShaderRecord.SizeInBytes = m_rtGIShaderTableStride;

    desc.MissShaderTable.StartAddress = shaderTableVA + 1 * m_rtGIShaderTableStride;
    desc.MissShaderTable.SizeInBytes = m_rtGIShaderTableStride;
    desc.MissShaderTable.StrideInBytes = m_rtGIShaderTableStride;

    desc.HitGroupTable.StartAddress = shaderTableVA + 2 * m_rtGIShaderTableStride;
    desc.HitGroupTable.SizeInBytes = m_rtGIShaderTableStride;
    desc.HitGroupTable.StrideInBytes = m_rtGIShaderTableStride;

    desc.Width = std::max(1u, dispatchWidth);
    desc.Height = std::max(1u, dispatchHeight);
    desc.Depth = 1;

    cmdList->DispatchRays(&desc);
}

} // namespace Cortex::Graphics
