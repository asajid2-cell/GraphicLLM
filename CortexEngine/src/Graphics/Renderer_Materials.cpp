#include "Renderer.h"

#include "Graphics/MaterialState.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <string>

namespace Cortex::Graphics {

bool Renderer::IsBiomeMaterialsValid() const {
    return m_constantBuffers.biomeMaterialsValid.load(std::memory_order_acquire);
}

D3D12_GPU_VIRTUAL_ADDRESS Renderer::GetBiomeMaterialsGPUAddress() const {
    return m_constantBuffers.biomeMaterials.gpuAddress;
}

bool Renderer::AreBiomeMaterialsValid() const {
    return IsBiomeMaterialsValid();
}

void Renderer::EnsureMaterialTextures(Scene::RenderableComponent& renderable) {
    auto tryLoad = [&](std::string& path, std::shared_ptr<DX12Texture>& slot, bool useSRGB, const std::shared_ptr<DX12Texture>& placeholder) {
        const bool isPlaceholder = slot == nullptr || slot == placeholder;
        // Only load from disk when we currently have no texture or a placeholder.
        if (!path.empty() && isPlaceholder) {
            std::shared_ptr<DX12Texture> cachedTexture;
            if (TryGetCachedTexture(path, useSRGB, AssetRegistry::TextureKind::Generic, cachedTexture)) {
                slot = std::move(cachedTexture);
                if (renderable.textures.gpuState) {
                    renderable.textures.gpuState->descriptorsReady = false;
                }
            } else if (IsTextureUploadFailed(path, useSRGB, AssetRegistry::TextureKind::Generic)) {
                spdlog::warn("Texture '{}' was marked failed by the deferred upload queue; using placeholder", path);
                path.clear();
                slot = placeholder;
                if (renderable.textures.gpuState) {
                    renderable.textures.gpuState->descriptorsReady = false;
                }
            } else if (!IsTextureUploadPending(path, useSRGB, AssetRegistry::TextureKind::Generic)) {
                auto queued = QueueTextureUploadFromFile(path, useSRGB, AssetRegistry::TextureKind::Generic);
                if (queued.IsErr()) {
                    spdlog::warn("Failed to queue texture '{}': {}", path, queued.Error());
                    path.clear();
                    slot = placeholder;
                    if (renderable.textures.gpuState) {
                        renderable.textures.gpuState->descriptorsReady = false;
                    }
                } else if (placeholder && slot != placeholder) {
                    slot = placeholder;
                    if (renderable.textures.gpuState) {
                        renderable.textures.gpuState->descriptorsReady = false;
                    }
                }
            } else {
                if (placeholder && slot != placeholder) {
                    slot = placeholder;
                    if (renderable.textures.gpuState) {
                        renderable.textures.gpuState->descriptorsReady = false;
                    }
                }
            }
        } else if (path.empty() && slot && slot != placeholder) {
            slot = placeholder;
            if (renderable.textures.gpuState) {
                renderable.textures.gpuState->descriptorsReady = false;
            }
        }
    };

    tryLoad(renderable.textures.albedoPath, renderable.textures.albedo, true, m_materialFallbacks.albedo);
    tryLoad(renderable.textures.normalPath, renderable.textures.normal, false, m_materialFallbacks.normal);
    tryLoad(renderable.textures.metallicPath, renderable.textures.metallic, false, m_materialFallbacks.metallic);
    tryLoad(renderable.textures.roughnessPath, renderable.textures.roughness, false, m_materialFallbacks.roughness);
    tryLoad(renderable.textures.occlusionPath, renderable.textures.occlusion, false, std::shared_ptr<DX12Texture>{});
    tryLoad(renderable.textures.emissivePath, renderable.textures.emissive, true, std::shared_ptr<DX12Texture>{});

    // glTF extension textures
    tryLoad(renderable.textures.transmissionPath, renderable.textures.transmission, false, std::shared_ptr<DX12Texture>{});
    tryLoad(renderable.textures.clearcoatPath, renderable.textures.clearcoat, false, std::shared_ptr<DX12Texture>{});
    tryLoad(renderable.textures.clearcoatRoughnessPath, renderable.textures.clearcoatRoughness, false, std::shared_ptr<DX12Texture>{});
    tryLoad(renderable.textures.specularPath, renderable.textures.specular, false, std::shared_ptr<DX12Texture>{});
    tryLoad(renderable.textures.specularColorPath, renderable.textures.specularColor, true, std::shared_ptr<DX12Texture>{});

    if (!renderable.textures.albedo) {
        renderable.textures.albedo = m_materialFallbacks.albedo;
    }
    if (!renderable.textures.normal) {
        renderable.textures.normal = m_materialFallbacks.normal;
    }
    if (!renderable.textures.metallic) {
        renderable.textures.metallic = m_materialFallbacks.metallic;
    }
    if (!renderable.textures.roughness) {
        renderable.textures.roughness = m_materialFallbacks.roughness;
    }
}

void Renderer::FillMaterialTextureIndices(const Scene::RenderableComponent& renderable,
                                          MaterialConstants& materialData) const {
    uint32_t texIndices[MaterialGPUState::kSlotCount] = {};
    for (uint32_t i = 0; i < MaterialGPUState::kSlotCount; ++i) {
        texIndices[i] = kInvalidBindlessIndex;
    }

    uint32_t effectiveMapFlags[6] = {
        materialData.mapFlags.x,
        materialData.mapFlags.y,
        materialData.mapFlags.z,
        materialData.mapFlags.w,
        materialData.mapFlags2.x,
        materialData.mapFlags2.y
    };

    if (renderable.textures.gpuState) {
        for (int i = 0; i < 6; ++i) {
            const bool hasMap = (effectiveMapFlags[i] != 0u);
            if (hasMap && renderable.textures.gpuState->descriptors[i].IsValid()) {
                texIndices[i] = renderable.textures.gpuState->descriptors[i].index;
            } else {
                // Descriptor isn't ready (or map missing). Treat as no-map so
                // shaders use constant material values instead of placeholders.
                effectiveMapFlags[i] = 0u;
                texIndices[i] = kInvalidBindlessIndex;
            }
        }

        // Extension textures don't have legacy map flags; treat non-null slots as present.
        const bool hasTransmission = static_cast<bool>(renderable.textures.transmission);
        const bool hasClearcoat = static_cast<bool>(renderable.textures.clearcoat);
        const bool hasClearcoatRoughness = static_cast<bool>(renderable.textures.clearcoatRoughness);
        const bool hasSpecular = static_cast<bool>(renderable.textures.specular);
        const bool hasSpecularColor = static_cast<bool>(renderable.textures.specularColor);

        const auto& desc = renderable.textures.gpuState->descriptors;
        texIndices[6] = (hasTransmission && desc[6].IsValid()) ? desc[6].index : kInvalidBindlessIndex;
        texIndices[7] = (hasClearcoat && desc[7].IsValid()) ? desc[7].index : kInvalidBindlessIndex;
        texIndices[8] = (hasClearcoatRoughness && desc[8].IsValid()) ? desc[8].index : kInvalidBindlessIndex;
        texIndices[9] = (hasSpecular && desc[9].IsValid()) ? desc[9].index : kInvalidBindlessIndex;
        texIndices[10] = (hasSpecularColor && desc[10].IsValid()) ? desc[10].index : kInvalidBindlessIndex;
    } else {
        for (int i = 0; i < 6; ++i) {
            effectiveMapFlags[i] = 0u;
            texIndices[i] = kInvalidBindlessIndex;
        }
    }

    materialData.mapFlags = glm::uvec4(
        effectiveMapFlags[0],
        effectiveMapFlags[1],
        effectiveMapFlags[2],
        effectiveMapFlags[3]
    );
    materialData.mapFlags2 = glm::uvec4(
        effectiveMapFlags[4],
        effectiveMapFlags[5],
        0u,
        0u
    );

    materialData.textureIndices = glm::uvec4(
        texIndices[0],
        texIndices[1],
        texIndices[2],
        texIndices[3]
    );
    materialData.textureIndices2 = glm::uvec4(
        texIndices[4],
        texIndices[5],
        kInvalidBindlessIndex,
        kInvalidBindlessIndex
    );

    materialData.textureIndices3 = glm::uvec4(
        texIndices[6],  // transmission
        texIndices[7],  // clearcoat
        texIndices[8],  // clearcoat roughness
        texIndices[9]   // specular
    );
    materialData.textureIndices4 = glm::uvec4(
        texIndices[10], // specular color
        kInvalidBindlessIndex,
        kInvalidBindlessIndex,
        kInvalidBindlessIndex
    );
}

void Renderer::PrewarmMaterialDescriptors(Scene::ECS_Registry* registry) {
    if (!registry || !m_services.descriptorManager) {
        return;
    }

    auto view = registry->View<Scene::RenderableComponent, Scene::TransformComponent>();
    for (auto entity : view) {
        auto& renderable = view.get<Scene::RenderableComponent>(entity);

        if (!renderable.visible || !renderable.mesh) {
            continue;
        }

        EnsureMaterialTextures(renderable);
        // Material descriptor tables are persistent per material and are
        // rewritten only when their texture sources change.
        RefreshMaterialDescriptors(renderable);
    }
}

void Renderer::RefreshMaterialDescriptors(Scene::RenderableComponent& renderable) {
    auto& tex = renderable.textures;
    if (!tex.gpuState) {
        tex.gpuState = std::make_shared<MaterialGPUState>();
    }
    auto& state = *tex.gpuState;

    ID3D12Device* device = m_services.device ? m_services.device->GetDevice() : nullptr;
    if (!device || !m_services.descriptorManager) {
        return;
    }

    std::array<std::shared_ptr<DX12Texture>, MaterialGPUState::kSlotCount> sources = {
        tex.albedo ? tex.albedo : m_materialFallbacks.albedo,
        tex.normal ? tex.normal : m_materialFallbacks.normal,
        tex.metallic ? tex.metallic : m_materialFallbacks.metallic,
        tex.roughness ? tex.roughness : m_materialFallbacks.roughness,
        tex.occlusion,
        tex.emissive,
        tex.transmission,
        tex.clearcoat,
        tex.clearcoatRoughness,
        tex.specular,
        tex.specularColor
    };

    std::array<std::shared_ptr<DX12Texture>, MaterialGPUState::kSlotCount> fallbacks = {};
    fallbacks[0] = m_materialFallbacks.albedo;
    fallbacks[1] = m_materialFallbacks.normal;
    fallbacks[2] = m_materialFallbacks.metallic;
    fallbacks[3] = m_materialFallbacks.roughness;

    auto refreshResult = TextureDescriptorState::RefreshMaterialDescriptorTable(
        device,
        m_services.descriptorManager.get(),
        state,
        sources,
        fallbacks);
    if (refreshResult.IsErr()) {
        state.descriptorsReady = false;
        spdlog::warn("{}", refreshResult.Error());
        return;
    }
}


void Renderer::UpdateBiomeMaterialsBuffer(const std::vector<Scene::BiomeConfig>& configs) {
    if (configs.empty()) {
        m_constantBuffers.biomeMaterialsValid = false;
        return;
    }

    Scene::BiomeMaterialsCBuffer cbData = {};
    // Find the maximum biome type index to set biomeCount correctly
    uint32_t maxBiomeIndex = 0;
    for (const auto& cfg : configs) {
        uint32_t biomeIdx = static_cast<uint32_t>(cfg.type);
        if (biomeIdx < 16) {
            maxBiomeIndex = std::max(maxBiomeIndex, biomeIdx + 1);
        }
    }
    cbData.biomeCount = maxBiomeIndex;

    // Use BiomeType enum value as the GPU buffer index
    // This ensures CPU-encoded biome indices match GPU array positions
    for (const auto& cfg : configs) {
        uint32_t biomeIdx = static_cast<uint32_t>(cfg.type);
        if (biomeIdx >= 16) continue;  // Skip invalid biome types
        auto& gpu = cbData.biomes[biomeIdx];

        gpu.baseColor = cfg.baseColor;
        gpu.slopeColor = cfg.slopeColor;
        gpu.roughness = cfg.roughness;
        gpu.metallic = cfg.metallic;

        // Initialize height layer arrays to safe defaults
        for (int j = 0; j < 4; ++j) {
            gpu.heightLayerMin[j] = -1000.0f;
            gpu.heightLayerMax[j] = 1000.0f;
            gpu.heightLayerColor[j] = cfg.baseColor;
        }

        // Copy height layers from config (up to 4)
        size_t layerCount = std::min(cfg.heightLayers.size(), size_t(4));
        for (size_t j = 0; j < layerCount; ++j) {
            const auto& layer = cfg.heightLayers[j];
            gpu.heightLayerMin[j] = layer.minHeight;
            gpu.heightLayerMax[j] = layer.maxHeight;
            gpu.heightLayerColor[j] = layer.color;
        }

        // Initialize alignment padding (after metallic, before heightLayerMin)
        gpu._pad0[0] = 0.0f;
        gpu._pad0[1] = 0.0f;
    }

    // Upload to GPU
    m_constantBuffers.biomeMaterials.UpdateData(cbData);
    m_constantBuffers.biomeMaterialsValid = true;

    spdlog::info("Renderer: Updated biome materials buffer with {} biomes", cbData.biomeCount);
}

} // namespace Cortex::Graphics
