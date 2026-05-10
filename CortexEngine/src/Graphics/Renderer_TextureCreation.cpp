#include "Renderer.h"

#include "Graphics/TextureUploadTicket.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

std::shared_ptr<DX12Texture> Renderer::GetPlaceholderTexture() const {
    return m_materialFallbacks.albedo;
}

std::shared_ptr<DX12Texture> Renderer::GetPlaceholderNormal() const {
    return m_materialFallbacks.normal;
}

std::shared_ptr<DX12Texture> Renderer::GetPlaceholderMetallic() const {
    return m_materialFallbacks.metallic;
}

std::shared_ptr<DX12Texture> Renderer::GetPlaceholderRoughness() const {
    return m_materialFallbacks.roughness;
}

namespace {
bool WriteTexture2DSRV(ID3D12Device* device,
                       const std::shared_ptr<DX12Texture>& texture,
                       D3D12_CPU_DESCRIPTOR_HANDLE dst) {
    if (!device || !texture || !texture->GetResource()) {
        return false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texture->GetFormat();
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = texture->GetMipLevels();
    srvDesc.Texture2D.MostDetailedMip = 0;

    device->CreateShaderResourceView(texture->GetResource(), &srvDesc, dst);
    return true;
}

} // namespace

Result<std::shared_ptr<DX12Texture>> Renderer::CreateTextureFromRGBA(
    const uint8_t* data,
    uint32_t width,
    uint32_t height,
    bool useSRGB,
    const std::string& debugName)
{
    if (!data || width == 0 || height == 0) {
        return Result<std::shared_ptr<DX12Texture>>::Err("Invalid texture data for Dreamer texture");
    }

    if (!m_services.device || !m_services.commandQueue || !m_services.descriptorManager) {
        return Result<std::shared_ptr<DX12Texture>>::Err("Renderer is not initialized");
    }

    DX12Texture texture;
    auto initResult = texture.InitializeFromData(
        m_services.device->GetDevice(),
        nullptr, // use graphics queue for copy + transitions
        m_services.commandQueue->GetCommandQueue(),
        data,
        width,
        height,
        useSRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM,
        debugName
    );
    if (initResult.IsErr()) {
        return Result<std::shared_ptr<DX12Texture>>::Err(initResult.Error());
    }

    TextureUploadReceipt receipt{};
    receipt.key = debugName;
    receipt.sourcePath = debugName;
    receipt.debugName = debugName;
    receipt.kind = AssetRegistry::TextureKind::Generic;
    receipt.sourceEncoding = TextureSourceEncoding::RGBA8;
    receipt.residencyClass = TextureResidencyClass::Generated;
    receipt.format = texture.GetFormat();
    receipt.sourceWidth = width;
    receipt.sourceHeight = height;
    receipt.sourceMipLevels = 1;
    receipt.firstResidentMip = 0;
    receipt.residentWidth = texture.GetWidth();
    receipt.residentHeight = texture.GetHeight();
    receipt.residentMipLevels = texture.GetMipLevels();
    receipt.fullGpuBytes = static_cast<uint64_t>(width) * height * 4u;
    receipt.residentGpuBytes = receipt.fullGpuBytes;
    receipt.textureBudgetBytes = m_framePlanning.budgetPlan.textureBudgetBytes;
    receipt.residentTextureBytesBefore = m_assetRuntime.registry.GetMemoryBreakdown().textureBytes;
    receipt.requestedSRGB = useSRGB;
    receipt.budgetProfile = m_framePlanning.budgetPlan.profileName;

    TextureUploadTicket ticket{};
    ticket.texture = std::move(texture);
    ticket.receipt = std::move(receipt);
    ticket.publishContext = "Dreamer texture '" + debugName + "'";
    ticket.registerAsset = false;

    return PublishTexture(std::move(ticket));
}

Result<void> Renderer::CreatePlaceholderTexture() {
    const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    const float flatNormal[4] = { 0.5f, 0.5f, 1.0f, 1.0f };
    const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    auto createAndBind = [&](const float color[4], std::shared_ptr<DX12Texture>& out) -> Result<void> {
        auto texResult = DX12Texture::CreatePlaceholder(
            m_services.device->GetDevice(),
            m_services.uploadQueue ? m_services.uploadQueue->GetCommandQueue() : nullptr,
            m_services.commandQueue->GetCommandQueue(),
            2,
            2,
            color
        );

        if (texResult.IsErr()) {
            return Result<void>::Err("Failed to create placeholder texture: " + texResult.Error());
        }

        out = std::make_shared<DX12Texture>(std::move(texResult.Value()));

        auto srvResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (srvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate staging SRV for placeholder: " + srvResult.Error());
        }

        auto createSRVResult = out->CreateSRV(m_services.device->GetDevice(), srvResult.Value());
        if (createSRVResult.IsErr()) {
            return createSRVResult;
        }
        return Result<void>::Ok();
    };

    auto albedoResult = createAndBind(white, m_materialFallbacks.albedo);
    if (albedoResult.IsErr()) return albedoResult;

    auto normalResult = createAndBind(flatNormal, m_materialFallbacks.normal);
    if (normalResult.IsErr()) return normalResult;

    auto metallicResult = createAndBind(black, m_materialFallbacks.metallic);
    if (metallicResult.IsErr()) return metallicResult;

    auto roughnessResult = createAndBind(white, m_materialFallbacks.roughness);
    if (roughnessResult.IsErr()) return roughnessResult;

    m_services.commandQueue->Flush();

    if (m_services.descriptorManager && !m_materialFallbacks.descriptorTable[0].IsValid()) {
        std::array<std::shared_ptr<DX12Texture>, 4> sources = {
            m_materialFallbacks.albedo,
            m_materialFallbacks.normal,
            m_materialFallbacks.metallic,
            m_materialFallbacks.roughness
        };

        for (int i = 0; i < 4; ++i) {
            auto handleResult = m_services.descriptorManager->AllocateCBV_SRV_UAV();
            if (handleResult.IsErr()) {
                return Result<void>::Err("Failed to allocate fallback material descriptor: " + handleResult.Error());
            }
            m_materialFallbacks.descriptorTable[i] = handleResult.Value();
        }

        for (int i = 0; i < 4; ++i) {
            WriteTexture2DSRV(m_services.device->GetDevice(), sources[i], m_materialFallbacks.descriptorTable[i].cpu);
        }
    }

    if (m_services.bindlessManager) {
        auto registerPlaceholder = [this](std::shared_ptr<DX12Texture>& tex) {
            if (tex && tex->GetResource()) {
                auto result = tex->CreateBindlessSRV(m_services.bindlessManager.get());
                if (result.IsOk()) {
                    spdlog::debug("Placeholder registered at bindless index {}", tex->GetBindlessIndex());
                } else {
                    spdlog::warn("Failed to register placeholder at bindless index: {}", result.Error());
                }
            }
        };
        registerPlaceholder(m_materialFallbacks.albedo);
        registerPlaceholder(m_materialFallbacks.normal);
        registerPlaceholder(m_materialFallbacks.metallic);
        registerPlaceholder(m_materialFallbacks.roughness);

        auto copyToReserved = [this](const std::shared_ptr<DX12Texture>& tex, uint32_t reservedIndex) {
            D3D12_CPU_DESCRIPTOR_HANDLE dst = m_services.bindlessManager->GetCPUHandle(reservedIndex);
            WriteTexture2DSRV(m_services.device->GetDevice(), tex, dst);
        };
        copyToReserved(m_materialFallbacks.albedo, BindlessResourceManager::kPlaceholderAlbedoIndex);
        copyToReserved(m_materialFallbacks.normal, BindlessResourceManager::kPlaceholderNormalIndex);
        copyToReserved(m_materialFallbacks.metallic, BindlessResourceManager::kPlaceholderMetallicIndex);
        copyToReserved(m_materialFallbacks.roughness, BindlessResourceManager::kPlaceholderRoughnessIndex);
    }

    spdlog::info("Placeholder textures created");
    return Result<void>::Ok();
}

} // namespace Cortex::Graphics

