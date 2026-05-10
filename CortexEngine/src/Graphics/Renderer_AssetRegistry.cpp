#include "Renderer.h"

#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <string>

namespace Cortex::Graphics {

void Renderer::RebuildAssetRefsFromScene(Scene::ECS_Registry* registry) {
    if (!registry) {
        return;
    }

    // Reset all ref-counts to zero and then rebuild them from the current
    // ECS graph. This produces an accurate snapshot of which meshes are
    // still referenced after a scene rebuild.
    m_assetRuntime.registry.ResetAllRefCounts();

    auto view = registry->View<Scene::RenderableComponent>();
    for (auto entity : view) {
        auto& renderable = view.get<Scene::RenderableComponent>(entity);

        // Mesh references: map MeshData* to asset key when available.
        if (renderable.mesh) {
            const Scene::MeshData* meshPtr = renderable.mesh.get();
            auto it = m_assetRuntime.meshAssetKeys.find(meshPtr);
            if (it != m_assetRuntime.meshAssetKeys.end()) {
                m_assetRuntime.registry.AddRefMeshKey(it->second);
            }
        }

        // Texture references: paths are used as canonical keys. Dreamer and
        // other non-file sentinel values are ignored for now.
        auto refPath = [&](const std::string& path) {
            if (path.empty()) {
                return;
            }
            if (!path.empty() && path[0] == '[') {
                return;
            }
            m_assetRuntime.registry.AddRefTextureKey(path);
        };

        refPath(renderable.textures.albedoPath);
        refPath(renderable.textures.normalPath);
        refPath(renderable.textures.metallicPath);
        refPath(renderable.textures.roughnessPath);
        refPath(renderable.textures.occlusionPath);
        refPath(renderable.textures.emissivePath);
    }
}

void Renderer::PruneUnusedMeshes(Scene::ECS_Registry* /*registry*/) {
    // Focus on BLAS/geometry cleanup; texture lifetime is primarily tied to
    // scene entities and will be reclaimed when those are destroyed.
    auto unused = m_assetRuntime.registry.CollectUnusedMeshes();
    if (unused.empty()) {
        return;
    }

    uint64_t totalBytes = 0;
    uint32_t count = 0;

    for (const auto& asset : unused) {
        totalBytes += asset.bytes;
        ++count;

        // Locate the MeshData* corresponding to this key so BLAS entries can
        // be released. We expect only a small number of meshes, so a simple
        // linear search over m_assetRuntime.meshAssetKeys is sufficient.
        const Scene::MeshData* meshPtr = nullptr;
        for (const auto& kv : m_assetRuntime.meshAssetKeys) {
            if (kv.second == asset.key) {
                meshPtr = kv.first;
                break;
            }
        }

        if (meshPtr && m_services.rayTracingContext) {
            m_services.rayTracingContext->ReleaseBLASForMesh(meshPtr);
        }

        // Remove from the mesh key map so future ref rebuilds do not consider it.
        if (meshPtr) {
            m_assetRuntime.meshAssetKeys.erase(meshPtr);
        }
    }

    const double mb = static_cast<double>(totalBytes) / (1024.0 * 1024.0);
    spdlog::info("Pruned {} unused meshes (~{:.1f} MB of geometry/BLAS candidates)", count, mb);
}

void Renderer::PruneUnusedTextures() {
    auto unused = m_assetRuntime.registry.CollectUnusedTextures();
    if (unused.empty()) {
        return;
    }

    uint64_t totalBytes = 0;
    uint32_t count = 0;

    for (const auto& asset : unused) {
        totalBytes += asset.bytes;
        ++count;
        // Removing the entry from the registry is sufficient from the
        // diagnostics perspective; the underlying DX12Texture resources are
        // owned by shared_ptrs attached to scene materials and will already
        // have been released when those components were destroyed.
        m_assetRuntime.registry.UnregisterTexture(asset.key);
    }

    const double mb = static_cast<double>(totalBytes) / (1024.0 * 1024.0);
    spdlog::info("Pruned {} unused textures from registry (~{:.1f} MB candidates)", count, mb);
}

} // namespace Cortex::Graphics