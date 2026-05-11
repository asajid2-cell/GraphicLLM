#pragma once

#include <array>
#include <memory>
#include <string>
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
};

} // namespace Cortex::Graphics
