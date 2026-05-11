#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "RHI/DX12Texture.h"
#include "RHI/DescriptorHeap.h"
#include "Utils/Result.h"

namespace Cortex::Graphics {

struct EnvironmentMaps {
    std::string name;
    std::string path;
    std::string budgetClass = "small";
    uint32_t maxRuntimeDimension = 2048;
    float defaultDiffuseIntensity = 1.0f;
    float defaultSpecularIntensity = 1.0f;
    std::shared_ptr<DX12Texture> diffuseIrradiance;
    std::shared_ptr<DX12Texture> specularPrefiltered;
    DescriptorHandle diffuseIrradianceSRV{};
    DescriptorHandle specularPrefilteredSRV{};
};

struct PendingEnvironment {
    std::string path;
    std::string name;
    std::string budgetClass = "small";
    uint32_t maxRuntimeDimension = 2048;
    float defaultDiffuseIntensity = 1.0f;
    float defaultSpecularIntensity = 1.0f;
};

struct EnvironmentLightingState {
    static constexpr uint32_t kMaxIBLResident = 4;

    // Shadow + environment descriptor table (space1):
    //   t0 = shadow map array
    //   t1 = diffuse IBL
    //   t2 = specular IBL
    //   t3 = RT shadow mask (optional, DXR)
    //   t4 = RT shadow mask history (optional, DXR)
    //   t5 = RT diffuse GI buffer (optional, DXR)
    //   t6 = RT diffuse GI history buffer (optional, DXR)
    std::array<DescriptorHandle, 7> shadowAndEnvDescriptors{};

    std::vector<EnvironmentMaps> maps;
    std::vector<PendingEnvironment> pending;
    size_t currentIndex = 0;

    bool limitEnabled = false;
    float diffuseIntensity = 1.1f;
    float specularIntensity = 1.3f;
    bool enabled = true;
    bool backgroundVisible = true;
    float backgroundExposure = 1.0f;
    float backgroundBlur = 0.0f;
    float rotationDegrees = 0.0f;
    bool selectionFallbackUsed = false;
    std::string requestedEnvironment;
    std::string fallbackReason;

    void ResetMaps() {
        maps.clear();
        pending.clear();
        currentIndex = 0;
    }

    [[nodiscard]] bool HasResidentEnvironment() const {
        return !maps.empty();
    }

    [[nodiscard]] size_t ActiveIndex() const {
        if (maps.empty()) {
            return 0;
        }
        return currentIndex < maps.size() ? currentIndex : 0;
    }

    [[nodiscard]] EnvironmentMaps* ActiveEnvironment() {
        return maps.empty() ? nullptr : &maps[ActiveIndex()];
    }

    [[nodiscard]] const EnvironmentMaps* ActiveEnvironment() const {
        return maps.empty() ? nullptr : &maps[ActiveIndex()];
    }

    [[nodiscard]] std::string ActiveEnvironmentName() const {
        const EnvironmentMaps* env = ActiveEnvironment();
        return env ? env->name : "None";
    }

    [[nodiscard]] bool UsingFallbackEnvironment() const {
        const std::string name = ActiveEnvironmentName();
        return selectionFallbackUsed || name == "Placeholder" || name == "procedural_sky";
    }

    [[nodiscard]] uint32_t ResidentCount() const {
        return static_cast<uint32_t>(maps.size());
    }

    [[nodiscard]] uint32_t PendingCount() const {
        return static_cast<uint32_t>(pending.size());
    }
};

struct EnvironmentDescriptorTableInputs {
    DescriptorHandle shadowMapSRV{};
    DescriptorHandle rtShadowMaskSRV{};
    DescriptorHandle rtShadowHistorySRV{};
    DescriptorHandle rtGISRV{};
    std::shared_ptr<DX12Texture> shadowFallback;
    std::shared_ptr<DX12Texture> diffuseFallback;
    std::shared_ptr<DX12Texture> specularFallback;
};

struct EnvironmentDescriptorState {
    [[nodiscard]] static Result<void> AllocateShadowAndEnvironmentTable(
        DescriptorHeapManager* descriptorManager,
        std::array<DescriptorHandle, 7>& descriptorTable,
        const std::string& context) {
        if (descriptorTable[0].IsValid()) {
            return Result<void>::Ok();
        }
        if (!descriptorManager) {
            return Result<void>::Err("Renderer is not initialized for " + context);
        }

        for (int i = 0; i < 7; ++i) {
            auto handleResult = descriptorManager->AllocateCBV_SRV_UAV();
            if (handleResult.IsErr()) {
                return Result<void>::Err("Failed to allocate SRV table for " + context + ": " + handleResult.Error());
            }
            descriptorTable[i] = handleResult.Value();
        }
        return Result<void>::Ok();
    }

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

    [[nodiscard]] static Result<void> EnsureEnvironmentBindlessSRVs(
        ID3D12Device* device,
        DescriptorHeapManager* descriptorManager,
        EnvironmentMaps& env,
        const std::shared_ptr<DX12Texture>& fallback) {
        if (!device || !descriptorManager) {
            return Result<void>::Err("Renderer is not initialized for environment bindless SRVs");
        }

        auto ensureHandle = [&](DescriptorHandle& handle, const char* label) -> Result<void> {
            if (handle.IsValid()) {
                return Result<void>::Ok();
            }
            auto alloc = descriptorManager->AllocateCBV_SRV_UAV();
            if (alloc.IsErr()) {
                return Result<void>::Err(std::string("Failed to allocate bindless environment SRV (") + label + "): " + alloc.Error());
            }
            handle = alloc.Value();
            return Result<void>::Ok();
        };

        std::shared_ptr<DX12Texture> diffuseSrc;
        if (env.diffuseIrradiance && env.diffuseIrradiance->GetResource()) {
            diffuseSrc = env.diffuseIrradiance;
        } else if (fallback && fallback->GetResource()) {
            diffuseSrc = fallback;
        }

        std::shared_ptr<DX12Texture> specularSrc;
        if (env.specularPrefiltered && env.specularPrefiltered->GetResource()) {
            specularSrc = env.specularPrefiltered;
        } else if (fallback && fallback->GetResource()) {
            specularSrc = fallback;
        }

        if (diffuseSrc) {
            auto handleResult = ensureHandle(env.diffuseIrradianceSRV, "diffuse");
            if (handleResult.IsErr()) {
                return handleResult;
            }
            WriteTexture2DSRV(device, diffuseSrc, env.diffuseIrradianceSRV.cpu);
        }
        if (specularSrc) {
            auto handleResult = ensureHandle(env.specularPrefilteredSRV, "specular");
            if (handleResult.IsErr()) {
                return handleResult;
            }
            WriteTexture2DSRV(device, specularSrc, env.specularPrefilteredSRV.cpu);
        }

        return Result<void>::Ok();
    }

    static void WriteShadowAndEnvironmentTable(
        ID3D12Device* device,
        EnvironmentLightingState& environment,
        const EnvironmentDescriptorTableInputs& inputs) {
        if (!device || !environment.shadowAndEnvDescriptors[0].IsValid()) {
            return;
        }

        if (inputs.shadowMapSRV.IsValid()) {
            device->CopyDescriptorsSimple(
                1,
                environment.shadowAndEnvDescriptors[0].cpu,
                inputs.shadowMapSRV.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        } else if (!WriteTexture2DSRV(device, inputs.shadowFallback, environment.shadowAndEnvDescriptors[0].cpu)) {
            WriteNullTexture2DSRV(device, DXGI_FORMAT_R8G8B8A8_UNORM, environment.shadowAndEnvDescriptors[0].cpu);
        }

        std::shared_ptr<DX12Texture> diffuseSrc;
        std::shared_ptr<DX12Texture> specularSrc;
        if (EnvironmentMaps* env = environment.ActiveEnvironment()) {
            if (env->diffuseIrradiance && env->diffuseIrradiance->GetResource()) {
                diffuseSrc = env->diffuseIrradiance;
            }
            if (env->specularPrefiltered && env->specularPrefiltered->GetResource()) {
                specularSrc = env->specularPrefiltered;
            }
        }

        if (!diffuseSrc && inputs.diffuseFallback && inputs.diffuseFallback->GetResource()) {
            diffuseSrc = inputs.diffuseFallback;
        }
        if (!specularSrc && inputs.specularFallback && inputs.specularFallback->GetResource()) {
            specularSrc = inputs.specularFallback;
        }

        if (!WriteTexture2DSRV(device, diffuseSrc, environment.shadowAndEnvDescriptors[1].cpu)) {
            WriteNullTexture2DSRV(device, DXGI_FORMAT_R8G8B8A8_UNORM, environment.shadowAndEnvDescriptors[1].cpu);
        }
        if (!WriteTexture2DSRV(device, specularSrc, environment.shadowAndEnvDescriptors[2].cpu)) {
            WriteNullTexture2DSRV(device, DXGI_FORMAT_R8G8B8A8_UNORM, environment.shadowAndEnvDescriptors[2].cpu);
        }

        if (inputs.rtShadowMaskSRV.IsValid()) {
            device->CopyDescriptorsSimple(
                1,
                environment.shadowAndEnvDescriptors[3].cpu,
                inputs.rtShadowMaskSRV.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
        if (inputs.rtShadowHistorySRV.IsValid()) {
            device->CopyDescriptorsSimple(
                1,
                environment.shadowAndEnvDescriptors[4].cpu,
                inputs.rtShadowHistorySRV.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
        if (inputs.rtGISRV.IsValid()) {
            device->CopyDescriptorsSimple(
                1,
                environment.shadowAndEnvDescriptors[5].cpu,
                inputs.rtGISRV.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }
};

} // namespace Cortex::Graphics
