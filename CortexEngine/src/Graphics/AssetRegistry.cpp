#include "Graphics/AssetRegistry.h"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

AssetRegistry::MemoryBreakdown AssetRegistry::ComputeMemoryBreakdownLocked() const {
    MemoryBreakdown out{};

    for (const auto& [_, tex] : m_textures) {
        if (tex.refCount == 0 || tex.gpuBytes == 0) {
            continue;
        }
        if (tex.kind == TextureKind::Environment) {
            out.environmentBytes += tex.gpuBytes;
        } else {
            out.textureBytes += tex.gpuBytes;
        }
    }

    for (const auto& [_, mesh] : m_meshes) {
        if (mesh.refCount == 0) {
            continue;
        }
        out.geometryBytes += (mesh.vertexBytes + mesh.indexBytes);
    }

    out.rtStructureBytes = m_rtStructureBytes;
    return out;
}

void AssetRegistry::UpdateBudgetFlagsLocked(const MemoryBreakdown& mem) const {
    const bool texOver  = mem.textureBytes     > m_texBudgetBytes;
    const bool envOver  = mem.environmentBytes > m_envBudgetBytes;
    const bool geomOver = mem.geometryBytes    > m_geomBudgetBytes;
    const bool rtOver   = mem.rtStructureBytes > m_rtBudgetBytes;

    if (texOver && !m_texBudgetExceeded) {
        const double usedMB   = static_cast<double>(mem.textureBytes) * (1.0 / (1024.0 * 1024.0));
        const double budgetMB = static_cast<double>(m_texBudgetBytes) * (1.0 / (1024.0 * 1024.0));
        spdlog::warn("Texture budget exceeded: tex≈{:.0f} MB > budget≈{:.0f} MB", usedMB, budgetMB);
    }
    if (envOver && !m_envBudgetExceeded) {
        const double usedMB   = static_cast<double>(mem.environmentBytes) * (1.0 / (1024.0 * 1024.0));
        const double budgetMB = static_cast<double>(m_envBudgetBytes) * (1.0 / (1024.0 * 1024.0));
        spdlog::warn("Environment budget exceeded: env≈{:.0f} MB > budget≈{:.0f} MB", usedMB, budgetMB);
    }
    if (geomOver && !m_geomBudgetExceeded) {
        const double usedMB   = static_cast<double>(mem.geometryBytes) * (1.0 / (1024.0 * 1024.0));
        const double budgetMB = static_cast<double>(m_geomBudgetBytes) * (1.0 / (1024.0 * 1024.0));
        spdlog::warn("Geometry budget exceeded: geom≈{:.0f} MB > budget≈{:.0f} MB", usedMB, budgetMB);
    }
    if (rtOver && !m_rtBudgetExceeded) {
        const double usedMB   = static_cast<double>(mem.rtStructureBytes) * (1.0 / (1024.0 * 1024.0));
        const double budgetMB = static_cast<double>(m_rtBudgetBytes) * (1.0 / (1024.0 * 1024.0));
        spdlog::warn("RT structure budget exceeded: rt≈{:.0f} MB > budget≈{:.0f} MB", usedMB, budgetMB);
    }

    m_texBudgetExceeded  = texOver;
    m_envBudgetExceeded  = envOver;
    m_geomBudgetExceeded = geomOver;
    m_rtBudgetExceeded   = rtOver;
}

void AssetRegistry::RegisterTexture(const std::string& key, uint64_t gpuBytes, TextureKind kind) {
    if (key.empty() || gpuBytes == 0) {
        return;
    }
    std::scoped_lock lock(m_mutex);
    auto& entry = m_textures[key];
    entry.gpuBytes = gpuBytes;
    entry.kind = kind;
    // Simple reference bump so callers can register the same asset multiple
    // times without double-counting; we do not currently track per-caller
    // ownership, only that the texture is in use.
    entry.refCount = entry.refCount + 1;

    auto mem = ComputeMemoryBreakdownLocked();
    UpdateBudgetFlagsLocked(mem);
}

void AssetRegistry::UnregisterTexture(const std::string& key) {
    if (key.empty()) {
        return;
    }
    std::scoped_lock lock(m_mutex);
    auto it = m_textures.find(key);
    if (it == m_textures.end()) {
        return;
    }
    if (it->second.refCount > 1) {
        --it->second.refCount;
    } else {
        m_textures.erase(it);
    }
}

void AssetRegistry::RegisterMesh(const std::string& key,
                                 uint64_t vertexBytes,
                                 uint64_t indexBytes) {
    if (key.empty()) {
        return;
    }
    const uint64_t total = vertexBytes + indexBytes;
    if (total == 0) {
        return;
    }

    std::scoped_lock lock(m_mutex);
    auto& entry = m_meshes[key];
    entry.vertexBytes = vertexBytes;
    entry.indexBytes  = indexBytes;
    entry.refCount    = entry.refCount + 1;

    auto mem = ComputeMemoryBreakdownLocked();
    UpdateBudgetFlagsLocked(mem);
}

void AssetRegistry::UnregisterMesh(const std::string& key) {
    if (key.empty()) {
        return;
    }
    std::scoped_lock lock(m_mutex);
    auto it = m_meshes.find(key);
    if (it == m_meshes.end()) {
        return;
    }
    if (it->second.refCount > 1) {
        --it->second.refCount;
    } else {
        m_meshes.erase(it);
    }
}

void AssetRegistry::SetRTStructureBytes(uint64_t bytes) {
    std::scoped_lock lock(m_mutex);
    m_rtStructureBytes = bytes;
    auto mem = ComputeMemoryBreakdownLocked();
    UpdateBudgetFlagsLocked(mem);
}

AssetRegistry::MemoryBreakdown AssetRegistry::GetMemoryBreakdown() const {
    std::scoped_lock lock(m_mutex);
    return ComputeMemoryBreakdownLocked();
}

std::vector<AssetRegistry::HeavyAsset>
AssetRegistry::GetHeaviestTextures(size_t maxCount) const {
    std::vector<HeavyAsset> out;
    {
        std::scoped_lock lock(m_mutex);
        out.reserve(m_textures.size());
        for (const auto& [key, tex] : m_textures) {
            if (tex.refCount == 0 || tex.gpuBytes == 0) {
                continue;
            }
            out.push_back(HeavyAsset{key, tex.gpuBytes});
        }
    }

    std::sort(out.begin(), out.end(),
              [](const HeavyAsset& a, const HeavyAsset& b) {
                  return a.bytes > b.bytes;
              });
    if (out.size() > maxCount) {
        out.resize(maxCount);
    }
    return out;
}

bool AssetRegistry::IsTextureBudgetExceeded() const {
    std::scoped_lock lock(m_mutex);
    return m_texBudgetExceeded;
}

bool AssetRegistry::IsEnvironmentBudgetExceeded() const {
    std::scoped_lock lock(m_mutex);
    return m_envBudgetExceeded;
}

bool AssetRegistry::IsGeometryBudgetExceeded() const {
    std::scoped_lock lock(m_mutex);
    return m_geomBudgetExceeded;
}

bool AssetRegistry::IsRTBudgetExceeded() const {
    std::scoped_lock lock(m_mutex);
    return m_rtBudgetExceeded;
}

std::vector<AssetRegistry::HeavyAsset>
AssetRegistry::GetHeaviestMeshes(size_t maxCount) const {
    std::vector<HeavyAsset> out;
    {
        std::scoped_lock lock(m_mutex);
        out.reserve(m_meshes.size());
        for (const auto& [key, mesh] : m_meshes) {
            if (mesh.refCount == 0) {
                continue;
            }
            const uint64_t bytes = mesh.vertexBytes + mesh.indexBytes;
            if (bytes == 0) {
                continue;
            }
            out.push_back(HeavyAsset{key, bytes});
        }
    }

    std::sort(out.begin(), out.end(),
              [](const HeavyAsset& a, const HeavyAsset& b) {
                  return a.bytes > b.bytes;
              });
    if (out.size() > maxCount) {
        out.resize(maxCount);
    }
    return out;
}

void AssetRegistry::ResetAllRefCounts() {
    std::scoped_lock lock(m_mutex);
    for (auto& [_, t] : m_textures) {
        t.refCount = 0;
    }
    for (auto& [_, m] : m_meshes) {
        m.refCount = 0;
    }
}

void AssetRegistry::AddRefTextureKey(const std::string& key) {
    if (key.empty()) {
        return;
    }
    std::scoped_lock lock(m_mutex);
    auto it = m_textures.find(key);
    if (it == m_textures.end()) {
        return;
    }
    ++it->second.refCount;
}

void AssetRegistry::AddRefMeshKey(const std::string& key) {
    if (key.empty()) {
        return;
    }
    std::scoped_lock lock(m_mutex);
    auto it = m_meshes.find(key);
    if (it == m_meshes.end()) {
        return;
    }
    ++it->second.refCount;
}

std::vector<AssetRegistry::HeavyAsset> AssetRegistry::CollectUnusedTextures() const {
    std::vector<HeavyAsset> out;
    std::scoped_lock lock(m_mutex);
    out.reserve(m_textures.size());
    for (const auto& [key, tex] : m_textures) {
        // Only consider non-environment textures for pruning; environment
        // maps are treated as long-lived global assets.
        if (tex.kind == TextureKind::Environment) {
            continue;
        }
        if (tex.refCount == 0 && tex.gpuBytes > 0) {
            out.push_back(HeavyAsset{key, tex.gpuBytes});
        }
    }
    return out;
}

std::vector<AssetRegistry::HeavyAsset> AssetRegistry::CollectUnusedMeshes() const {
    std::vector<HeavyAsset> out;
    std::scoped_lock lock(m_mutex);
    out.reserve(m_meshes.size());
    for (const auto& [key, mesh] : m_meshes) {
        const uint64_t bytes = mesh.vertexBytes + mesh.indexBytes;
        if (mesh.refCount == 0 && bytes > 0) {
            out.push_back(HeavyAsset{key, bytes});
        }
    }
    return out;
}

} // namespace Cortex::Graphics
