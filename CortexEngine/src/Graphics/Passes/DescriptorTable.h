#pragma once

#include "Graphics/RHI/DescriptorHeap.h"
#include "Utils/Result.h"

#include <cstdint>
#include <span>

namespace Cortex::Graphics::DescriptorTable {

[[nodiscard]] bool BindCBVSRVUAVHeap(ID3D12GraphicsCommandList* commandList,
                                     DescriptorHeapManager* descriptorManager);

bool WriteTexture2DSRV(ID3D12Device* device,
                       DescriptorHandle handle,
                       ID3D12Resource* resource,
                       DXGI_FORMAT format,
                       uint32_t mipLevels = 1);

bool WriteTexture2DRTVAndSRV(ID3D12Device* device,
                             ID3D12Resource* resource,
                             DescriptorHandle rtv,
                             DescriptorHandle srv,
                             DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN);

bool WriteTexture2DUAV(ID3D12Device* device,
                       DescriptorHandle handle,
                       ID3D12Resource* resource,
                       DXGI_FORMAT format);

[[nodiscard]] Result<void> AllocateAndWriteNullSRVTable(ID3D12Device* device,
                                                        DescriptorHeapManager* descriptorManager,
                                                        std::span<DescriptorHandle> table,
                                                        const char* label,
                                                        DXGI_FORMAT format,
                                                        uint32_t mipLevels = 1);

[[nodiscard]] Result<void> AllocateHandleSet(DescriptorHeapManager* descriptorManager,
                                             std::span<DescriptorHandle> handles,
                                             const char* label);

[[nodiscard]] Result<void> EnsureColorTargetViewHandles(DescriptorHeapManager* descriptorManager,
                                                        DescriptorHandle& rtv,
                                                        DescriptorHandle& srv,
                                                        const char* label);

[[nodiscard]] DescriptorHandle Slot(std::span<DescriptorHandle> table,
                                    size_t slot);
[[nodiscard]] bool IsContiguous(std::span<const DescriptorHandle> table);

} // namespace Cortex::Graphics::DescriptorTable
