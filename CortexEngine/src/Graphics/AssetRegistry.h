#pragma once

#include <stdint.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace Cortex::Graphics {

// Lightweight registry that tracks approximate GPU memory usage per asset.
// The goal is visibility and budgeting rather than exact accounting.
class AssetRegistry {
public:
    enum class TextureKind : uint32_t {
        Generic     = 0,
        Environment = 1,
    };

    struct MemoryBreakdown {
        uint64_t textureBytes      = 0; // material / generic textures
        uint64_t environmentBytes  = 0; // HDR/IBL/env maps
        uint64_t geometryBytes     = 0; // vertex + index buffers
        uint64_t rtStructureBytes  = 0; // BLAS + TLAS
    };

    struct HeavyAsset {
        std::string key;
        uint64_t bytes = 0;
    };

    void RegisterTexture(const std::string& key, uint64_t gpuBytes, TextureKind kind);
    void UnregisterTexture(const std::string& key);

    void RegisterMesh(const std::string& key, uint64_t vertexBytes, uint64_t indexBytes);
    void UnregisterMesh(const std::string& key);

    // RT acceleration structures are tracked as a single bucket updated by the
    // DXR context; this folds BLAS/TLAS memory into the inspector.
    void SetRTStructureBytes(uint64_t bytes);

    [[nodiscard]] MemoryBreakdown GetMemoryBreakdown() const;
    [[nodiscard]] std::vector<HeavyAsset> GetHeaviestTextures(size_t maxCount) const;
    [[nodiscard]] std::vector<HeavyAsset> GetHeaviestMeshes(size_t maxCount) const;

    // Ref-count maintenance used during scene rebuilds. These treat the
    // asset keys as canonical identifiers (e.g., file paths for textures,
    // renderer-generated keys for meshes).
    void ResetAllRefCounts();
    void AddRefTextureKey(const std::string& key);
    void AddRefMeshKey(const std::string& key);

    // Collect assets that are currently not referenced by any scene. These
    // lists are intended for cleanup passes (BLAS pruning, cache eviction).
    [[nodiscard]] std::vector<HeavyAsset> CollectUnusedTextures() const;
    [[nodiscard]] std::vector<HeavyAsset> CollectUnusedMeshes() const;

    // Soft budgets (in bytes) for diagnostics and warnings. Budgets are
    // intentionally conservative and can be tuned as needed.
    [[nodiscard]] uint64_t GetTextureBudgetBytes() const { return m_texBudgetBytes; }
    [[nodiscard]] uint64_t GetEnvironmentBudgetBytes() const { return m_envBudgetBytes; }
    [[nodiscard]] uint64_t GetGeometryBudgetBytes() const { return m_geomBudgetBytes; }
    [[nodiscard]] uint64_t GetRTBudgetBytes() const { return m_rtBudgetBytes; }

    [[nodiscard]] bool IsTextureBudgetExceeded() const;
    [[nodiscard]] bool IsEnvironmentBudgetExceeded() const;
    [[nodiscard]] bool IsGeometryBudgetExceeded() const;
    [[nodiscard]] bool IsRTBudgetExceeded() const;

private:
    struct TextureEntry {
        uint64_t    gpuBytes = 0;
        uint32_t    refCount = 0;
        TextureKind kind     = TextureKind::Generic;
    };

    struct MeshEntry {
        uint64_t vertexBytes = 0;
        uint64_t indexBytes  = 0;
        uint32_t refCount    = 0;
    };

    MemoryBreakdown ComputeMemoryBreakdownLocked() const;
    void UpdateBudgetFlagsLocked(const MemoryBreakdown& mem) const;

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, TextureEntry> m_textures;
    std::unordered_map<std::string, MeshEntry>    m_meshes;
    mutable uint64_t m_rtStructureBytes = 0;

    // Per-category soft budgets (bytes).
    uint64_t m_texBudgetBytes  = 3500ull * 1024ull * 1024ull; // ~3.5 GB
    uint64_t m_envBudgetBytes  =  512ull * 1024ull * 1024ull; // ~512 MB
    uint64_t m_geomBudgetBytes = 1500ull * 1024ull * 1024ull; // ~1.5 GB
    uint64_t m_rtBudgetBytes   = 1500ull * 1024ull * 1024ull; // ~1.5 GB

    mutable bool m_texBudgetExceeded  = false;
    mutable bool m_envBudgetExceeded  = false;
    mutable bool m_geomBudgetExceeded = false;
    mutable bool m_rtBudgetExceeded   = false;
};

} // namespace Cortex::Graphics
