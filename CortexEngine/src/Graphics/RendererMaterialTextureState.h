#pragma once

#include <array>
#include <memory>
#include <string>
#include "Graphics/MaterialState.h"
#include "RHI/DX12Texture.h"
#include "RHI/DescriptorHeap.h"
#include "Utils/Result.h"

namespace Cortex::Graphics {

struct MaterialFallbackTextureState {
    std::shared_ptr<DX12Texture> albedo;
    std::shared_ptr<DX12Texture> normal;
    std::shared_ptr<DX12Texture> metallic;
    std::shared_ptr<DX12Texture> roughness;
    std::array<DescriptorHandle, 4> descriptorTable = {};

    void ResetResources() {
        albedo.reset();
        normal.reset();
        metallic.reset();
        roughness.reset();
        descriptorTable = {};
    }
};

struct TextureDescriptorState {
    static bool WriteTexture2DSRV(ID3D12Device* device,
                                  const std::shared_ptr<DX12Texture>& texture,
                                  D3D12_CPU_DESCRIPTOR_HANDLE dst) {
        if (!device || !texture || !texture->GetResource()) {
            return false;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = texture->GetFormat();
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = texture->GetMipLevels();
        srvDesc.Texture2D.MostDetailedMip = 0;

        device->CreateShaderResourceView(texture->GetResource(), &srvDesc, dst);
        return true;
    }

    [[nodiscard]] static Result<DescriptorHandle> CreateStagingSRV(ID3D12Device* device,
                                                                   DescriptorHeapManager* descriptorManager,
                                                                   DX12Texture& texture,
                                                                   const std::string& context) {
        if (!device || !descriptorManager) {
            return Result<DescriptorHandle>::Err("Renderer is not initialized for " + context);
        }

        auto srvResult = descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (srvResult.IsErr()) {
            return Result<DescriptorHandle>::Err("Failed to allocate staging SRV for " + context + ": " + srvResult.Error());
        }

        auto createResult = texture.CreateSRV(device, srvResult.Value());
        if (createResult.IsErr()) {
            return Result<DescriptorHandle>::Err(createResult.Error());
        }

        return Result<DescriptorHandle>::Ok(srvResult.Value());
    }

    [[nodiscard]] static Result<void> AllocateFallbackDescriptorTable(DescriptorHeapManager* descriptorManager,
                                                                     std::array<DescriptorHandle, 4>& descriptorTable) {
        if (!descriptorManager) {
            return Result<void>::Err("Renderer is not initialized for fallback material descriptors");
        }

        for (int i = 0; i < 4; ++i) {
            auto handleResult = descriptorManager->AllocateCBV_SRV_UAV();
            if (handleResult.IsErr()) {
                return Result<void>::Err("Failed to allocate fallback material descriptor: " + handleResult.Error());
            }
            descriptorTable[i] = handleResult.Value();
        }
        return Result<void>::Ok();
    }

    static void WriteTexture2DSRVTable(ID3D12Device* device,
                                       const std::array<std::shared_ptr<DX12Texture>, 4>& sources,
                                       const std::array<DescriptorHandle, 4>& descriptorTable) {
        for (int i = 0; i < 4; ++i) {
            WriteTexture2DSRV(device, sources[i], descriptorTable[i].cpu);
        }
    }

    static void WriteNullTexture2DSRV(ID3D12Device* device,
                                      DXGI_FORMAT format,
                                      D3D12_CPU_DESCRIPTOR_HANDLE dst) {
        if (!device) {
            return;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = format;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.MostDetailedMip = 0;
        device->CreateShaderResourceView(nullptr, &srvDesc, dst);
    }

    [[nodiscard]] static Result<void> AllocateMaterialDescriptorTable(
        DescriptorHeapManager* descriptorManager,
        MaterialGPUState& state) {
        if (state.descriptorsAllocated) {
            return Result<void>::Ok();
        }
        if (!descriptorManager) {
            return Result<void>::Err("Renderer is not initialized for persistent material descriptors");
        }

        for (uint32_t i = 0; i < MaterialGPUState::kSlotCount; ++i) {
            auto handleResult = descriptorManager->AllocateCBV_SRV_UAV();
            if (handleResult.IsErr()) {
                state.descriptorsReady = false;
                return Result<void>::Err("Failed to allocate persistent material descriptor slot " + std::to_string(i) + ": " + handleResult.Error());
            }
            state.descriptors[i] = handleResult.Value();

            if (i > 0 && state.descriptors[i].index != state.descriptors[i - 1].index + 1) {
                state.descriptorsReady = false;
                return Result<void>::Err("Persistent material descriptor table is not contiguous; material will not be shader-bindable");
            }
        }

        state.descriptorsAllocated = true;
        state.descriptorsReady = false;
        return Result<void>::Ok();
    }

    [[nodiscard]] static Result<bool> RefreshMaterialDescriptorTable(
        ID3D12Device* device,
        DescriptorHeapManager* descriptorManager,
        MaterialGPUState& state,
        const std::array<std::shared_ptr<DX12Texture>, MaterialGPUState::kSlotCount>& sources,
        const std::array<std::shared_ptr<DX12Texture>, MaterialGPUState::kSlotCount>& fallbacks) {
        if (!device || !descriptorManager) {
            return Result<bool>::Err("Renderer is not initialized for persistent material descriptors");
        }

        auto allocResult = AllocateMaterialDescriptorTable(descriptorManager, state);
        if (allocResult.IsErr()) {
            return Result<bool>::Err(allocResult.Error());
        }

        bool descriptorsChanged = !state.descriptorsReady;
        for (size_t i = 0; i < sources.size(); ++i) {
            if (state.sourceTextures[i].lock() != sources[i]) {
                descriptorsChanged = true;
                break;
            }
        }

        if (!descriptorsChanged) {
            return Result<bool>::Ok(false);
        }

        for (size_t i = 0; i < sources.size(); ++i) {
            std::shared_ptr<DX12Texture> texture;
            if (sources[i] && sources[i]->GetResource()) {
                texture = sources[i];
            } else if (fallbacks[i] && fallbacks[i]->GetResource()) {
                texture = fallbacks[i];
            }

            if (!WriteTexture2DSRV(device, texture, state.descriptors[i].cpu)) {
                WriteNullTexture2DSRV(device, DXGI_FORMAT_R8G8B8A8_UNORM, state.descriptors[i].cpu);
            }
        }

        for (size_t i = 0; i < sources.size(); ++i) {
            state.sourceTextures[i] = sources[i];
        }
        state.descriptorsReady = true;
        return Result<bool>::Ok(true);
    }
};

} // namespace Cortex::Graphics
