#pragma once

#include "Graphics/RHI/DescriptorHeap.h"

#include <cstdint>
#include <span>

namespace Cortex::Graphics::DescriptorTable {

bool WriteTexture2DSRV(ID3D12Device* device,
                       DescriptorHandle handle,
                       ID3D12Resource* resource,
                       DXGI_FORMAT format,
                       uint32_t mipLevels = 1);

bool WriteTexture2DUAV(ID3D12Device* device,
                       DescriptorHandle handle,
                       ID3D12Resource* resource,
                       DXGI_FORMAT format);

[[nodiscard]] DescriptorHandle Slot(std::span<DescriptorHandle> table,
                                    size_t slot);
[[nodiscard]] bool IsContiguous(std::span<const DescriptorHandle> table);

} // namespace Cortex::Graphics::DescriptorTable
