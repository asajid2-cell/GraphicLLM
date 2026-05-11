#include "Renderer.h"

#include "Graphics/TextureAdmission.h"
#include "Graphics/TextureUploadTicket.h"

#include <cstddef>
#include <memory>
#include <string>

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

Result<std::shared_ptr<DX12Texture>> Renderer::PublishTexture(TextureUploadTicket ticket) {
    if (!m_services.device || !m_services.descriptorManager) {
        return Result<std::shared_ptr<DX12Texture>>::Err("Renderer is not initialized for texture publication");
    }

    auto srvResult = TextureDescriptorState::CreateStagingSRV(
        m_services.device->GetDevice(),
        m_services.descriptorManager.get(),
        ticket.texture,
        ticket.publishContext);
    if (srvResult.IsErr()) {
        return Result<std::shared_ptr<DX12Texture>>::Err(srvResult.Error());
    }

    auto texPtr = std::make_shared<DX12Texture>(std::move(ticket.texture));
    if (ticket.receipt.residentWidth == 0 ||
        ticket.receipt.residentHeight == 0 ||
        ticket.receipt.residentMipLevels == 0) {
        ticket.receipt.residentWidth = texPtr->GetWidth();
        ticket.receipt.residentHeight = texPtr->GetHeight();
        ticket.receipt.residentMipLevels = texPtr->GetMipLevels();
        ticket.receipt.format = texPtr->GetFormat();
    }
    if (ticket.receipt.residentGpuBytes == 0) {
        ticket.receipt.residentGpuBytes = EstimateTextureBytes(
            texPtr->GetWidth(),
            texPtr->GetHeight(),
            texPtr->GetMipLevels(),
            texPtr->GetFormat());
    }
    if (ticket.receipt.fullGpuBytes == 0) {
        ticket.receipt.fullGpuBytes = ticket.receipt.residentGpuBytes;
    }

    if (ticket.registerAsset && ticket.receipt.residentGpuBytes > 0) {
        m_assetRuntime.registry.RegisterTexture(
            ticket.receipt.key,
            ticket.receipt.residentGpuBytes,
            ticket.receipt.kind);
    }

    if (m_services.bindlessManager && texPtr->GetResource()) {
        auto bindlessResult = texPtr->CreateBindlessSRV(m_services.bindlessManager.get());
        if (bindlessResult.IsErr()) {
            spdlog::warn(
                "Failed to register {} in bindless heap: {}",
                ticket.publishContext,
                bindlessResult.Error());
        } else {
            ticket.receipt.bindlessIndex = texPtr->GetBindlessIndex();
        }
    }

    StoreTextureUploadReceipt(std::move(ticket.receipt));
    return Result<std::shared_ptr<DX12Texture>>::Ok(texPtr);
}

void Renderer::StoreTextureUploadReceipt(TextureUploadReceipt receipt) {
    m_assetRuntime.textureUploads.StoreReceipt(std::move(receipt));
}

} // namespace Cortex::Graphics
