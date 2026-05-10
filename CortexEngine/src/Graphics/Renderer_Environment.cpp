#include "Renderer.h"

#include "Graphics/EnvironmentManifest.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

namespace {

struct EnvironmentLoadCandidate {
    std::filesystem::path path;
    std::string name;
    EnvironmentBudgetClass budgetClass = EnvironmentBudgetClass::Small;
    uint32_t maxRuntimeDimension = 2048;
    float defaultDiffuseIntensity = 1.0f;
    float defaultSpecularIntensity = 1.0f;
    bool required = false;
    bool defaultEnvironment = false;
};

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

void WriteNullTexture2DSRV(ID3D12Device* device, DXGI_FORMAT format, D3D12_CPU_DESCRIPTOR_HANDLE dst) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    device->CreateShaderResourceView(nullptr, &srvDesc, dst);
}

} // namespace

bool Renderer::IsIBLLimitEnabled() const {
    return GetFeatureState().iblLimitEnabled;
}

Result<void> Renderer::InitializeEnvironmentMaps() {
    if (!m_services.descriptorManager || !m_services.device) {
        return Result<void>::Err("Renderer not initialized for environment maps");
    }

    // Clear any existing environments and release their registry residency
    // counts. The texture objects are owned by the environment vector; the
    // registry mirrors that ownership for diagnostics and budget warnings.
    for (const auto& env : m_environmentState.maps) {
        if (!env.path.empty()) {
            m_assetRuntime.registry.UnregisterTexture(env.path);
        }
    }
    m_environmentState.ResetMaps();

    namespace fs = std::filesystem;
    std::vector<EnvironmentLoadCandidate> envFiles;

    const fs::path assetsDir = "assets";
    const auto budget = BudgetPlanner::BuildPlan(
        m_services.device ? m_services.device->GetDedicatedVideoMemoryBytes() : 0,
        m_services.window ? std::max(1u, m_services.window->GetWidth()) : 0,
        m_services.window ? std::max(1u, m_services.window->GetHeight()) : 0);

    const fs::path manifestPath = assetsDir / "environments" / "environments.json";
    bool usedManifest = false;
    if (fs::exists(manifestPath)) {
        auto manifestResult = LoadEnvironmentManifest(manifestPath);
        if (manifestResult.IsErr()) {
            spdlog::warn("Environment manifest ignored: {}", manifestResult.Error());
        } else {
            usedManifest = true;
            const auto& manifest = manifestResult.Value();
            for (const auto& entry : manifest.environments) {
                if (!entry.enabled || entry.id == manifest.fallback) {
                    continue;
                }
                if (!IsEnvironmentAllowedForBudget(budget.profile, entry.budgetClass)) {
                    spdlog::info("Environment '{}' skipped for profile '{}' (budget_class={})",
                                 entry.id,
                                 budget.profileName,
                                 ToString(entry.budgetClass));
                    continue;
                }

                const fs::path runtimePath = ResolveEnvironmentAssetPath(manifestPath, entry.runtimePath);
                if (!fs::exists(runtimePath)) {
                    spdlog::warn("Environment manifest {} entry '{}' missing runtime asset '{}'",
                                 entry.required ? "required" : "optional",
                                 entry.id,
                                 runtimePath.string());
                    continue;
                }

                EnvironmentLoadCandidate candidate;
                candidate.path = runtimePath;
                candidate.name = entry.id;
                candidate.budgetClass = entry.budgetClass;
                candidate.maxRuntimeDimension = entry.maxRuntimeDimension;
                candidate.defaultDiffuseIntensity = entry.defaultDiffuseIntensity;
                candidate.defaultSpecularIntensity = entry.defaultSpecularIntensity;
                candidate.required = entry.required;
                candidate.defaultEnvironment = (entry.id == manifest.defaultEnvironment);
                envFiles.push_back(std::move(candidate));
            }

            spdlog::info("Environment manifest loaded from '{}': {} candidates (default='{}', fallback='{}')",
                         manifestPath.string(),
                         envFiles.size(),
                         manifest.defaultEnvironment,
                         manifest.fallback);
        }
    }

    // Legacy fallback: scan assets directory for all HDR and EXR files when no
    // manifest exists or the manifest has no usable runtime entries.
    if (envFiles.empty() && fs::exists(assetsDir) && fs::is_directory(assetsDir)) {
        for (const auto& entry : fs::directory_iterator(assetsDir)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                               [](unsigned char c) { return std::tolower(c); });

                if (ext == ".hdr" || ext == ".exr") {
                    EnvironmentLoadCandidate candidate;
                    candidate.path = entry.path();
                    candidate.name = entry.path().stem().string();
                    candidate.budgetClass = EnvironmentBudgetClass::Medium;
                    candidate.maxRuntimeDimension = 4096;
                    envFiles.push_back(std::move(candidate));
                }
            }
        }
        if (usedManifest) {
            spdlog::warn("Environment manifest had no usable entries; falling back to legacy HDR/EXR scan");
        }
    }

    std::sort(envFiles.begin(),
              envFiles.end(),
              [](const EnvironmentLoadCandidate& a, const EnvironmentLoadCandidate& b) {
                  if (a.defaultEnvironment != b.defaultEnvironment) {
                      return a.defaultEnvironment;
                  }
                  if (a.required != b.required) {
                      return a.required;
                  }
                  return a.name < b.name;
              });
    const size_t maxStartupEnvs =
        std::max<size_t>(1, static_cast<size_t>(budget.iblResidentEnvironmentLimit));

    int successCount = 0;
    bool envBudgetReached = false;
    for (size_t index = 0; index < envFiles.size(); ++index) {
        const auto& candidate = envFiles[index];
        const auto& envPath = candidate.path;
        std::string pathStr = envPath.string();
        std::string name = candidate.name.empty() ? envPath.stem().string() : candidate.name;

        if (!envBudgetReached && successCount < static_cast<int>(maxStartupEnvs)) {
            // Load a limited number of environments synchronously during
            // startup. On 8 GB this is typically just the studio env used
            // by RT showcase; heavier adapters can afford more variety.
            auto texResult = LoadTextureFromFile(pathStr, false, AssetRegistry::TextureKind::Environment);
            if (texResult.IsErr()) {
                spdlog::warn("Failed to load environment from '{}': {}", pathStr, texResult.Error());
                continue;
            }

            auto tex = texResult.Value();

            EnvironmentMaps env;
            env.name = name;
            env.path = pathStr;
            env.budgetClass = ToString(candidate.budgetClass);
            env.maxRuntimeDimension = candidate.maxRuntimeDimension;
            env.defaultDiffuseIntensity = candidate.defaultDiffuseIntensity;
            env.defaultSpecularIntensity = candidate.defaultSpecularIntensity;
            env.diffuseIrradiance = tex;
            env.specularPrefiltered = tex;
            m_environmentState.maps.push_back(env);

            spdlog::info(
                "Environment '{}' loaded at startup from '{}': {}x{}, {} mips",
                name,
                pathStr,
                tex->GetWidth(),
                tex->GetHeight(),
                tex->GetMipLevels());

            ++successCount;

            // Once the environment memory budget has been exceeded, stop
            // eagerly loading additional skyboxes and defer them instead so
            // 8 GB-class GPUs do not spend hundreds of MB on unused IBL.
            if (m_assetRuntime.registry.IsEnvironmentBudgetExceeded()) {
                envBudgetReached = true;
            }
        } else {
            PendingEnvironment pending;
            pending.path = pathStr;
            pending.name = name;
            pending.budgetClass = ToString(candidate.budgetClass);
            pending.maxRuntimeDimension = candidate.maxRuntimeDimension;
            pending.defaultDiffuseIntensity = candidate.defaultDiffuseIntensity;
            pending.defaultSpecularIntensity = candidate.defaultSpecularIntensity;
            m_environmentState.pending.push_back(std::move(pending));
        }
    }

    // If no environments loaded, create a fallback placeholder environment
    if (m_environmentState.maps.empty()) {
        spdlog::warn("No HDR environments loaded; using placeholder");
        EnvironmentMaps fallback;
        fallback.name = "Placeholder";
        fallback.budgetClass = "tiny";
        fallback.maxRuntimeDimension = 0;
        fallback.defaultDiffuseIntensity = 0.75f;
        fallback.defaultSpecularIntensity = 0.35f;

        // The engine's IBL shaders treat environment maps as lat-long 2D
        // textures. Use the existing placeholder 2D texture so SRV dimension
        // matches both forward and deferred/VB sampling.
        fallback.diffuseIrradiance = m_materialFallbacks.albedo;
        fallback.specularPrefiltered = m_materialFallbacks.albedo;

        m_environmentState.maps.push_back(fallback);
    }

    // Ensure current environment index is valid
    m_environmentState.currentIndex = 0;

    // Enable the IBL residency limit for every budget profile so later
    // environment loads cannot silently accumulate above the selected profile.
    SetIBLLimitEnabled(true);

    // If an IBL residency limit is active, trim any excess environments
    // loaded at startup so that we do not immediately exceed the target
    // number of resident skyboxes on 8 GB-class GPUs.
    EnforceIBLResidencyLimit();

    // Allocate persistent descriptors for shadow + IBL + RT mask/history + RT GI
    // (space1, t0-t6) if not already created.
    if (!m_environmentState.shadowAndEnvDescriptors[0].IsValid()) {
        for (int i = 0; i < 7; ++i) {
            auto handleResult = m_services.descriptorManager->AllocateCBV_SRV_UAV();
            if (handleResult.IsErr()) {
                return Result<void>::Err("Failed to allocate SRV table for shadow/environment: " + handleResult.Error());
            }
            m_environmentState.shadowAndEnvDescriptors[i] = handleResult.Value();
        }
    }

    UpdateEnvironmentDescriptorTable();

    spdlog::info(
        "Environment maps initialized: {} loaded eagerly, {} pending for deferred loading (profile={}, resident_limit={})",
        successCount,
        m_environmentState.pending.size(),
        budget.profileName,
        budget.iblResidentEnvironmentLimit);
    return Result<void>::Ok();
}

Result<void> Renderer::AddEnvironmentFromTexture(const std::shared_ptr<DX12Texture>& tex, const std::string& name) {
    if (!tex) {
        return Result<void>::Err("AddEnvironmentFromTexture called with null texture");
    }

    EnvironmentMaps env;
    env.name = name.empty() ? "DreamerEnv" : name;
    env.path.clear();
    env.diffuseIrradiance = tex;
    env.specularPrefiltered = tex;

    m_environmentState.maps.push_back(env);
    EnforceIBLResidencyLimit();
    m_environmentState.currentIndex = m_environmentState.maps.size() - 1;

    spdlog::info("Environment '{}' registered from Dreamer texture ({}x{}, {} mips)",
                 env.name, tex->GetWidth(), tex->GetHeight(), tex->GetMipLevels());

    // Ensure descriptor table exists, then refresh bindings.
    if (!m_environmentState.shadowAndEnvDescriptors[0].IsValid() && m_services.descriptorManager) {
        for (int i = 0; i < 7; ++i) {
            auto handleResult = m_services.descriptorManager->AllocateCBV_SRV_UAV();
            if (handleResult.IsErr()) {
                return Result<void>::Err("Failed to allocate SRV table for Dreamer environment: " + handleResult.Error());
            }
            m_environmentState.shadowAndEnvDescriptors[i] = handleResult.Value();
        }
    }

    UpdateEnvironmentDescriptorTable();
    return Result<void>::Ok();
}

void Renderer::UpdateEnvironmentDescriptorTable() {
    if (!m_services.device || !m_services.descriptorManager) {
        return;
    }
    if (!m_environmentState.shadowAndEnvDescriptors[0].IsValid()) {
        return;
    }

    ID3D12Device* device = m_services.device->GetDevice();

    // Slot 0 (t4): shadow map array, or a neutral placeholder if shadows are unavailable.
    if (m_shadowResources.srv.IsValid()) {
        device->CopyDescriptorsSimple(
            1,
            m_environmentState.shadowAndEnvDescriptors[0].cpu,
            m_shadowResources.srv.cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );
    } else if (!WriteTexture2DSRV(device, m_materialFallbacks.roughness, m_environmentState.shadowAndEnvDescriptors[0].cpu)) {
        WriteNullTexture2DSRV(device, DXGI_FORMAT_R8G8B8A8_UNORM, m_environmentState.shadowAndEnvDescriptors[0].cpu);
    }

    // Environment selection
    std::shared_ptr<DX12Texture> diffuseSrc;
    std::shared_ptr<DX12Texture> specularSrc;

    if (!m_environmentState.maps.empty()) {
        size_t envIndex = m_environmentState.currentIndex;
        if (envIndex >= m_environmentState.maps.size()) {
            envIndex = 0;
        }
        EnvironmentMaps& env = m_environmentState.maps[envIndex];
        EnsureEnvironmentBindlessSRVs(env);

        if (env.diffuseIrradiance && env.diffuseIrradiance->GetResource()) {
            diffuseSrc = env.diffuseIrradiance;
        }
        if (env.specularPrefiltered && env.specularPrefiltered->GetResource()) {
            specularSrc = env.specularPrefiltered;
        }
    }

    // If no environment texture is available, fall back to placeholders when
    // present; otherwise leave the descriptors as null SRVs.
    if (!diffuseSrc && m_materialFallbacks.albedo && m_materialFallbacks.albedo->GetResource()) {
        diffuseSrc = m_materialFallbacks.albedo;
    }
    if (!specularSrc && m_materialFallbacks.albedo && m_materialFallbacks.albedo->GetResource()) {
        specularSrc = m_materialFallbacks.albedo;
    }

    if (!WriteTexture2DSRV(device, diffuseSrc, m_environmentState.shadowAndEnvDescriptors[1].cpu)) {
        WriteNullTexture2DSRV(device, DXGI_FORMAT_R8G8B8A8_UNORM, m_environmentState.shadowAndEnvDescriptors[1].cpu);
    }

    if (!WriteTexture2DSRV(device, specularSrc, m_environmentState.shadowAndEnvDescriptors[2].cpu)) {
        WriteNullTexture2DSRV(device, DXGI_FORMAT_R8G8B8A8_UNORM, m_environmentState.shadowAndEnvDescriptors[2].cpu);
    }

    // Optional RT shadow mask and history (t3, t4). When unavailable the
    // PBR shader simply reads cascaded shadows.
    if (m_rtShadowTargets.maskSRV.IsValid()) {
        device->CopyDescriptorsSimple(
            1,
            m_environmentState.shadowAndEnvDescriptors[3].cpu,
            m_rtShadowTargets.maskSRV.cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );
    }
    if (m_rtShadowTargets.historySRV.IsValid()) {
        device->CopyDescriptorsSimple(
            1,
            m_environmentState.shadowAndEnvDescriptors[4].cpu,
            m_rtShadowTargets.historySRV.cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );
    }

    // Optional RT diffuse GI buffer (t5). When unavailable the PBR shader
    // falls back to SSAO + ambient only.
    if (m_rtGITargets.srv.IsValid()) {
        device->CopyDescriptorsSimple(
            1,
            m_environmentState.shadowAndEnvDescriptors[5].cpu,
            m_rtGITargets.srv.cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
}

void Renderer::EnsureEnvironmentBindlessSRVs(EnvironmentMaps& env) {
    if (!m_services.device || !m_services.descriptorManager) {
        return;
    }

    ID3D12Device* device = m_services.device->GetDevice();
    if (!device) {
        return;
    }

    auto ensureHandle = [&](DescriptorHandle& handle, const char* label) -> bool {
        if (handle.IsValid()) {
            return true;
        }
        auto alloc = m_services.descriptorManager->AllocateCBV_SRV_UAV();
        if (alloc.IsErr()) {
            spdlog::warn("Failed to allocate bindless environment SRV ({}): {}", label, alloc.Error());
            return false;
        }
        handle = alloc.Value();
        return true;
    };

    std::shared_ptr<DX12Texture> diffuseSrc;
    if (env.diffuseIrradiance && env.diffuseIrradiance->GetResource()) {
        diffuseSrc = env.diffuseIrradiance;
    } else if (m_materialFallbacks.albedo && m_materialFallbacks.albedo->GetResource()) {
        diffuseSrc = m_materialFallbacks.albedo;
    }

    std::shared_ptr<DX12Texture> specularSrc;
    if (env.specularPrefiltered && env.specularPrefiltered->GetResource()) {
        specularSrc = env.specularPrefiltered;
    } else if (m_materialFallbacks.albedo && m_materialFallbacks.albedo->GetResource()) {
        specularSrc = m_materialFallbacks.albedo;
    }

    if (diffuseSrc && ensureHandle(env.diffuseIrradianceSRV, "diffuse")) {
        WriteTexture2DSRV(device, diffuseSrc, env.diffuseIrradianceSRV.cpu);
    }
    if (specularSrc && ensureHandle(env.specularPrefilteredSRV, "specular")) {
        WriteTexture2DSRV(device, specularSrc, env.specularPrefilteredSRV.cpu);
    }
}

void Renderer::ProcessPendingEnvironmentMaps(uint32_t maxPerFrame) {
    if (maxPerFrame == 0 || m_environmentState.pending.empty()) {
        return;
    }

    const uint32_t residentLimit = std::max(1u, m_framePlanning.budgetPlan.iblResidentEnvironmentLimit);
    if (m_environmentState.limitEnabled && m_environmentState.maps.size() >= residentLimit) {
        return;
    }

    uint32_t processedThisFrame = 0;
    while (processedThisFrame < maxPerFrame && !m_environmentState.pending.empty()) {
        if (m_environmentState.limitEnabled && m_environmentState.maps.size() >= residentLimit) {
            break;
        }

        PendingEnvironment pending = m_environmentState.pending.back();
        m_environmentState.pending.pop_back();

        auto texResult = LoadTextureFromFile(pending.path, false, AssetRegistry::TextureKind::Environment);
        if (texResult.IsErr()) {
            spdlog::warn(
                "Deferred environment load failed for '{}': {}",
                pending.path,
                texResult.Error());
            continue;
        }

        auto tex = texResult.Value();

        EnvironmentMaps env;
        env.name = pending.name;
        env.path = pending.path;
        env.budgetClass = pending.budgetClass;
        env.maxRuntimeDimension = pending.maxRuntimeDimension;
        env.defaultDiffuseIntensity = pending.defaultDiffuseIntensity;
        env.defaultSpecularIntensity = pending.defaultSpecularIntensity;
        env.diffuseIrradiance = tex;
        env.specularPrefiltered = tex;
        m_environmentState.maps.push_back(env);
        EnforceIBLResidencyLimit();

        spdlog::info(
            "Deferred environment '{}' loaded from '{}': {}x{}, {} mips ({} remaining)",
            env.name,
            pending.path,
            tex->GetWidth(),
            tex->GetHeight(),
            tex->GetMipLevels(),
            m_environmentState.pending.size());

        processedThisFrame++;
    }

    if (m_environmentState.pending.empty()) {
        spdlog::info("All deferred environment maps loaded (total environments: {})", m_environmentState.maps.size());
    }
}

void Renderer::LoadAdditionalEnvironmentMaps(uint32_t maxToLoad) {
    if (maxToLoad == 0) {
        return;
    }
    ProcessPendingEnvironmentMaps(maxToLoad);
}

void Renderer::SetIBLLimitEnabled(bool enabled) {
    if (m_environmentState.limitEnabled == enabled) {
        return;
    }
    m_environmentState.limitEnabled = enabled;
    if (m_environmentState.limitEnabled) {
        EnforceIBLResidencyLimit();
    }
}

void Renderer::EnforceIBLResidencyLimit() {
    if (!m_environmentState.limitEnabled) {
        return;
    }
    const uint32_t residentLimit = std::max(1u, m_framePlanning.budgetPlan.iblResidentEnvironmentLimit);
    if (m_environmentState.maps.size() <= residentLimit) {
        return;
    }

    bool changed = false;
    // Evict oldest environments in FIFO order while keeping the current
    // environment resident whenever possible.
    while (m_environmentState.maps.size() > residentLimit) {
        if (m_environmentState.maps.empty()) {
            break;
        }

        size_t victimIndex = std::numeric_limits<size_t>::max();
        for (size_t i = 0; i < m_environmentState.maps.size(); ++i) {
            if (i != m_environmentState.currentIndex) {
                victimIndex = i;
                break;
            }
        }

        if (victimIndex == std::numeric_limits<size_t>::max()) {
            // Only the current environment is resident; nothing to evict.
            break;
        }

        EnvironmentMaps victim = m_environmentState.maps[victimIndex];
        spdlog::info("IBL residency limit: evicting environment '{}' (path='{}') to keep at most {} loaded",
                     victim.name,
                     victim.path,
                     residentLimit);

        // If we know the source path, push it back into the pending queue so
        // it can be reloaded later if needed.
        if (!victim.path.empty()) {
            PendingEnvironment pending;
            pending.path = victim.path;
            pending.name = victim.name;
            m_environmentState.pending.push_back(std::move(pending));
            m_assetRuntime.registry.UnregisterTexture(victim.path);
        }

        m_environmentState.maps.erase(m_environmentState.maps.begin() + static_cast<std::ptrdiff_t>(victimIndex));
        changed = true;

        if (!m_environmentState.maps.empty()) {
            if (victimIndex < m_environmentState.currentIndex && m_environmentState.currentIndex > 0) {
                --m_environmentState.currentIndex;
            } else if (m_environmentState.currentIndex >= m_environmentState.maps.size()) {
                m_environmentState.currentIndex = m_environmentState.maps.size() - 1;
            }
        } else {
            m_environmentState.currentIndex = 0;
        }
    }

    if (changed && !m_environmentState.maps.empty()) {
        UpdateEnvironmentDescriptorTable();
    }
}

#ifdef CORTEX_ENABLE_HYPER_EXPERIMENT
Result<void> Renderer::EnsureHyperGeometryScene(Scene::ECS_Registry* registry) {
    if (m_services.hyperSceneBuilt || !m_services.hyperGeometry) {
        return Result<void>::Ok();
    }
    if (!registry) {
        return Result<void>::Err("Registry is null; cannot build hyper scene");
    }

    std::vector<std::shared_ptr<Scene::MeshData>> meshes;
    auto view = registry->View<Scene::RenderableComponent>();
    for (auto entity : view) {
        auto& renderable = view.get<Scene::RenderableComponent>(entity);
        if (renderable.mesh) {
            meshes.push_back(renderable.mesh);
        }
    }

    if (meshes.empty()) {
        return Result<void>::Err("No meshes available for Hyper-Geometry scene");
    }

    auto buildResult = m_services.hyperGeometry->BuildScene(meshes);
    if (buildResult.IsErr()) {
        return buildResult;
    }

    m_services.hyperSceneBuilt = true;
    return Result<void>::Ok();
}
#endif

} // namespace Cortex::Graphics
