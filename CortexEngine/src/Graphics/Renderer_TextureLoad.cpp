#include "Renderer.h"

#include "Graphics/TextureAdmission.h"
#include "Graphics/TextureSourcePlan.h"
#include "Graphics/TextureUploadTicket.h"

#include <memory>
#include <string>

namespace Cortex::Graphics {

namespace {

TextureResidencyClass ResidencyClassFor(AssetRegistry::TextureKind kind) {
    return kind == AssetRegistry::TextureKind::Environment
        ? TextureResidencyClass::Environment
        : TextureResidencyClass::Generic;
}

} // namespace

std::string Renderer::BuildTextureCacheKey(const std::string& path,
                                           bool useSRGB,
                                           AssetRegistry::TextureKind kind) const {
    const bool budgetMaterialMips = kind == AssetRegistry::TextureKind::Generic;

    std::string cacheKey = path + (useSRGB ? "_srgb" : "_linear");
    if (budgetMaterialMips) {
        cacheKey += "_mipcap" + std::to_string(m_framePlanning.budgetPlan.materialTextureMaxDimension);
        cacheKey += "_floor" + std::to_string(m_framePlanning.budgetPlan.materialTextureBudgetFloorDimension);
    }
    return cacheKey;
}

TextureUploadReceipt Renderer::BuildTextureReceiptFromPlan(
    const TextureSourcePlan& plan,
    const std::string& key,
    bool useSRGB,
    AssetRegistry::TextureKind kind,
    uint64_t residentTextureBytesBefore) const {
    TextureUploadReceipt receipt{};
    receipt.key = key;
    receipt.sourcePath = plan.sourcePath;
    receipt.debugName = key;
    receipt.kind = kind;
    receipt.residencyClass = ResidencyClassFor(kind);
    receipt.requestedSRGB = useSRGB;
    receipt.budgetProfile = m_framePlanning.budgetPlan.profileName;
    receipt.residentTextureBytesBefore = residentTextureBytesBefore;
    receipt.textureBudgetBytes =
        (kind == AssetRegistry::TextureKind::Environment)
            ? m_framePlanning.budgetPlan.environmentBudgetBytes
            : m_framePlanning.budgetPlan.textureBudgetBytes;
    receipt.sourceEncoding = plan.encoding;
    receipt.sourceWidth = plan.sourceWidth;
    receipt.sourceHeight = plan.sourceHeight;
    receipt.sourceMipLevels = plan.sourceMipLevels;
    receipt.firstResidentMip = plan.admission.firstMip;
    receipt.residentWidth = plan.admission.width;
    receipt.residentHeight = plan.admission.height;
    receipt.residentMipLevels = plan.sourceMipLevels - plan.admission.firstMip;
    receipt.format = plan.format;
    receipt.fullGpuBytes = plan.admission.fullBytes;
    receipt.residentGpuBytes = plan.admission.admittedBytes;
    receipt.usedCompressedSibling = plan.usedCompressedSibling;
    receipt.budgetTrimmed =
        plan.admission.firstMip > 0 || plan.admission.admittedBytes < plan.admission.fullBytes;
    return receipt;
}

Result<std::shared_ptr<DX12Texture>> Renderer::LoadTextureFromFile(
    const std::string& path,
    bool useSRGB,
    AssetRegistry::TextureKind kind) {
    if (path.empty()) {
        return Result<std::shared_ptr<DX12Texture>>::Err("Empty texture path");
    }

    if (!m_services.device || !m_services.commandQueue || !m_services.descriptorManager) {
        return Result<std::shared_ptr<DX12Texture>>::Err("Renderer is not initialized");
    }

    const bool budgetMaterialMips = kind == AssetRegistry::TextureKind::Generic;

    const std::string cacheKey = BuildTextureCacheKey(path, useSRGB, kind);
    auto cacheIt = m_assetRuntime.textureCache.find(cacheKey);
    if (cacheIt != m_assetRuntime.textureCache.end()) {
        return Result<std::shared_ptr<DX12Texture>>::Ok(cacheIt->second);
    }

    const auto memoryBefore = m_assetRuntime.registry.GetMemoryBreakdown();
    const uint64_t residentBytesBefore =
        (kind == AssetRegistry::TextureKind::Environment)
            ? memoryBefore.environmentBytes
            : memoryBefore.textureBytes;

    constexpr bool kEnableCompressedDDS = true;
    auto planResult = BuildTextureSourcePlan(
        path,
        useSRGB,
        kEnableCompressedDDS,
        budgetMaterialMips,
        m_framePlanning.budgetPlan,
        residentBytesBefore);
    if (planResult.IsErr()) {
        return Result<std::shared_ptr<DX12Texture>>::Err(planResult.Error());
    }

    auto plan = std::move(planResult).Value();
    auto uploadResult = ExecuteTextureUpload(plan);
    if (uploadResult.IsErr()) {
        return Result<std::shared_ptr<DX12Texture>>::Err(uploadResult.Error());
    }

    TextureUploadReceipt receipt =
        BuildTextureReceiptFromPlan(plan, path, useSRGB, kind, residentBytesBefore);

    TextureUploadTicket ticket{};
    ticket.texture = std::move(uploadResult).Value();
    ticket.receipt = std::move(receipt);
    ticket.publishContext = "texture '" + path + "'";
    ticket.registerAsset = true;

    auto publishResult = PublishTexture(std::move(ticket));
    if (publishResult.IsErr()) {
        return publishResult;
    }

    auto texPtr = publishResult.Value();
    m_assetRuntime.textureCache[cacheKey] = texPtr;
    return Result<std::shared_ptr<DX12Texture>>::Ok(texPtr);
}

} // namespace Cortex::Graphics
