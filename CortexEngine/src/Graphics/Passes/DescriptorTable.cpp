#include "DescriptorTable.h"

namespace Cortex::Graphics::DescriptorTable {

bool BindCBVSRVUAVHeap(ID3D12GraphicsCommandList* commandList,
                       DescriptorHeapManager* descriptorManager) {
    if (!commandList || !descriptorManager) {
        return false;
    }

    ID3D12DescriptorHeap* heaps[] = { descriptorManager->GetCBV_SRV_UAV_Heap() };
    commandList->SetDescriptorHeaps(1, heaps);
    return true;
}

bool WriteTexture2DSRV(ID3D12Device* device,
                       DescriptorHandle handle,
                       ID3D12Resource* resource,
                       DXGI_FORMAT format,
                       uint32_t mipLevels) {
    if (!device || !handle.IsValid() || mipLevels == 0) {
        return false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = mipLevels;
    device->CreateShaderResourceView(resource, &srvDesc, handle.cpu);
    return true;
}

bool WriteTexture2DUAV(ID3D12Device* device,
                       DescriptorHandle handle,
                       ID3D12Resource* resource,
                       DXGI_FORMAT format) {
    if (!device || !handle.IsValid()) {
        return false;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = format;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(resource, nullptr, &uavDesc, handle.cpu);
    return true;
}

DescriptorHandle Slot(std::span<DescriptorHandle> table, size_t slot) {
    if (slot >= table.size()) {
        return {};
    }
    return table[slot];
}

bool IsContiguous(std::span<const DescriptorHandle> table) {
    if (table.empty() || !table.front().IsValid()) {
        return false;
    }

    const uint32_t base = table.front().index;
    for (size_t i = 1; i < table.size(); ++i) {
        if (!table[i].IsValid() || table[i].index != base + static_cast<uint32_t>(i)) {
            return false;
        }
    }
    return true;
}

} // namespace Cortex::Graphics::DescriptorTable
