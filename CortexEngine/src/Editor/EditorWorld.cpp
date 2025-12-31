// EditorWorld.cpp
// Clean world representation implementation for Engine Editor Mode.

#include "EditorWorld.h"
#include "ChunkGenerator.h"
#include "SpatialGrid.h"
#include "Graphics/Renderer.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"
#include "Scene/TerrainNoise.h"
#include "Scene/BiomeMap.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace Cortex {

EditorWorld::EditorWorld() = default;

EditorWorld::~EditorWorld() {
    if (m_initialized) {
        Shutdown();
    }
}

Result<void> EditorWorld::Initialize(Graphics::Renderer* renderer,
                                    Scene::ECS_Registry* registry,
                                    const EditorWorldConfig& config) {
    if (m_initialized) {
        return Result<void>::Err("EditorWorld already initialized");
    }

    if (!renderer) {
        return Result<void>::Err("Renderer pointer is null");
    }

    if (!registry) {
        return Result<void>::Err("Registry pointer is null");
    }

    m_renderer = renderer;
    m_registry = registry;
    m_config = config;

    // Initialize subsystems
    m_spatialGrid = std::make_unique<SpatialGrid>();
    m_spatialGrid->SetChunkSize(config.chunkSize);

    m_chunkGenerator = std::make_unique<ChunkGenerator>();
    m_chunkGenerator->Initialize(config.chunkGeneratorThreads);
    m_chunkGenerator->SetTerrainParams(config.terrainParams);
    m_chunkGenerator->SetChunkSize(config.chunkSize);

    // Set default terrain parameters if not specified
    if (m_config.terrainParams.seed == 0) {
        m_config.terrainParams = Scene::TerrainNoiseParams{
            .seed = 42,
            .amplitude = 20.0f,
            .frequency = 0.003f,
            .octaves = 6,
            .lacunarity = 2.0f,
            .gain = 0.5f,
            .warp = 15.0f
        };
        m_chunkGenerator->SetTerrainParams(m_config.terrainParams);
    }

    // Initialize biome system if enabled
    if (m_config.useBiomes) {
        m_biomeMap = std::make_unique<Scene::BiomeMap>();
        m_biomeMap->Initialize(m_config.biomeParams);

        // Load biome configurations from JSON
        if (!m_config.biomesConfigPath.empty()) {
            if (m_biomeMap->LoadFromJSON(m_config.biomesConfigPath)) {
                spdlog::info("Loaded biome configurations from '{}'", m_config.biomesConfigPath);
            } else {
                spdlog::warn("Failed to load biomes from '{}', using defaults", m_config.biomesConfigPath);
                // Set up default biome configs
                std::vector<Scene::BiomeConfig> defaultBiomes;
                for (uint8_t i = 0; i < static_cast<uint8_t>(Scene::BiomeType::COUNT); ++i) {
                    Scene::BiomeConfig biome;
                    biome.type = static_cast<Scene::BiomeType>(i);
                    biome.name = Scene::BiomeTypeToString(biome.type);
                    // Default colors vary by biome
                    switch (biome.type) {
                        case Scene::BiomeType::Plains:
                            biome.baseColor = glm::vec4(0.3f, 0.5f, 0.2f, 1.0f);
                            break;
                        case Scene::BiomeType::Mountains:
                            biome.baseColor = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
                            biome.heightScale = 2.5f;
                            break;
                        case Scene::BiomeType::Desert:
                            biome.baseColor = glm::vec4(0.8f, 0.7f, 0.5f, 1.0f);
                            break;
                        case Scene::BiomeType::Forest:
                            biome.baseColor = glm::vec4(0.15f, 0.35f, 0.1f, 1.0f);
                            break;
                        case Scene::BiomeType::Tundra:
                            biome.baseColor = glm::vec4(0.85f, 0.9f, 0.95f, 1.0f);
                            break;
                        case Scene::BiomeType::Swamp:
                            biome.baseColor = glm::vec4(0.2f, 0.25f, 0.15f, 1.0f);
                            break;
                        case Scene::BiomeType::Beach:
                            biome.baseColor = glm::vec4(0.9f, 0.85f, 0.7f, 1.0f);
                            break;
                        default:
                            biome.baseColor = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
                            break;
                    }
                    defaultBiomes.push_back(biome);
                }
                m_biomeMap->SetBiomeConfigs(std::move(defaultBiomes));
            }
        }

        // Pass biome map to chunk generator
        m_chunkGenerator->SetBiomeMap(m_biomeMap.get());
        spdlog::info("Biome system enabled (cellSize={}, blendRadius={})",
                     m_config.biomeParams.cellSize, m_config.biomeParams.blendRadius);
    }

    m_initialized = true;
    spdlog::info("EditorWorld initialized (loadRadius={}, maxChunks={}, biomes={})",
                 m_config.loadRadius, m_config.maxLoadedChunks, m_config.useBiomes);

    return Result<void>::Ok();
}

void EditorWorld::Shutdown() {
    if (!m_initialized) {
        return;
    }

    spdlog::info("EditorWorld shutting down ({} chunks loaded)", m_loadedChunks.size());

    // Shutdown chunk generator first (stops worker threads)
    if (m_chunkGenerator) {
        m_chunkGenerator->Shutdown();
        m_chunkGenerator.reset();
    }

    // Destroy all chunk entities
    for (const auto& coord : m_loadedChunks) {
        DestroyChunkEntity(coord);
    }
    m_loadedChunks.clear();
    m_pendingChunks.clear();

    // Clear spatial grid
    if (m_spatialGrid) {
        m_spatialGrid->Clear();
        m_spatialGrid.reset();
    }

    // Clear biome map
    m_biomeMap.reset();

    m_renderer = nullptr;
    m_registry = nullptr;
    m_initialized = false;
}

void EditorWorld::Update(const glm::vec3& cameraPosition, float /*deltaTime*/) {
    if (!m_initialized) {
        return;
    }

    // Reset frame stats
    m_stats.chunksLoadedThisFrame = 0;
    m_stats.chunksUnloadedThisFrame = 0;

    // Process any completed chunk generations
    ProcessCompletedChunks();

    // Request new chunks and unload distant ones
    UpdateChunkLoading(cameraPosition);

    // Update stats
    m_stats.loadedChunks = m_loadedChunks.size();
    m_stats.pendingChunks = m_pendingChunks.size();
}

void EditorWorld::SetTerrainParams(const Scene::TerrainNoiseParams& params) {
    m_config.terrainParams = params;
    if (m_chunkGenerator) {
        m_chunkGenerator->SetTerrainParams(params);
    }

    // TODO: Optionally regenerate existing chunks with new params
}

void EditorWorld::SetBiomeParams(const Scene::BiomeMapParams& params) {
    m_config.biomeParams = params;
    if (m_biomeMap) {
        m_biomeMap->Initialize(params);
    }
    // TODO: Optionally regenerate existing chunks with new biome params
}

Scene::BiomeSample EditorWorld::GetBiomeAt(float worldX, float worldZ) const {
    if (m_biomeMap) {
        return m_biomeMap->Sample(worldX, worldZ);
    }
    // Return default sample (plains)
    Scene::BiomeSample sample;
    sample.primary = Scene::BiomeType::Plains;
    sample.secondary = Scene::BiomeType::Plains;
    sample.blendWeight = 0.0f;
    sample.temperature = 0.5f;
    sample.moisture = 0.5f;
    return sample;
}

void EditorWorld::SetBiomesEnabled(bool enabled) {
    m_config.useBiomes = enabled;
    if (m_chunkGenerator) {
        m_chunkGenerator->SetBiomeMap(enabled ? m_biomeMap.get() : nullptr);
    }
}

bool EditorWorld::IsChunkLoaded(const ChunkCoord& coord) const {
    return m_loadedChunks.find(coord) != m_loadedChunks.end();
}

size_t EditorWorld::GetPendingChunkCount() const {
    return m_pendingChunks.size();
}

float EditorWorld::GetTerrainHeight(float worldX, float worldZ) const {
    return Scene::SampleTerrainHeight(
        static_cast<double>(worldX),
        static_cast<double>(worldZ),
        m_config.terrainParams
    );
}

std::vector<ChunkCoord> EditorWorld::GetChunksInRadius(const glm::vec3& center, float radius) const {
    if (!m_spatialGrid) {
        return {};
    }
    return m_spatialGrid->GetChunksInRadius(center, radius);
}

std::vector<ChunkCoord> EditorWorld::GetVisibleChunks() const {
    if (!m_spatialGrid) {
        return {};
    }
    return m_spatialGrid->GetAllChunks();
}

void EditorWorld::UpdateChunkLoading(const glm::vec3& cameraPos) {
    // Calculate player chunk coordinate
    int32_t playerChunkX = static_cast<int32_t>(std::floor(cameraPos.x / m_config.chunkSize));
    int32_t playerChunkZ = static_cast<int32_t>(std::floor(cameraPos.z / m_config.chunkSize));

    // Determine desired chunks (square around player)
    std::unordered_set<ChunkCoord, ChunkCoordHash> desiredChunks;
    for (int32_t dz = -m_config.loadRadius; dz <= m_config.loadRadius; ++dz) {
        for (int32_t dx = -m_config.loadRadius; dx <= m_config.loadRadius; ++dx) {
            ChunkCoord coord{playerChunkX + dx, playerChunkZ + dz};
            desiredChunks.insert(coord);
        }
    }

    // Unload distant chunks
    UnloadDistantChunks(cameraPos);

    // Request new chunks that aren't loaded or pending
    uint32_t requestsThisFrame = 0;
    for (const auto& coord : desiredChunks) {
        if (m_loadedChunks.find(coord) == m_loadedChunks.end() &&
            m_pendingChunks.find(coord) == m_pendingChunks.end()) {

            // Calculate priority (closer = higher priority)
            float chunkCenterX = coord.x * m_config.chunkSize + m_config.chunkSize * 0.5f;
            float chunkCenterZ = coord.z * m_config.chunkSize + m_config.chunkSize * 0.5f;
            float dx = chunkCenterX - cameraPos.x;
            float dz = chunkCenterZ - cameraPos.z;
            float distSq = dx * dx + dz * dz;

            // Determine LOD based on distance
            ChunkLOD lod = CalculateLOD(distSq);

            // Priority: inverse of distance (closer = higher)
            float priority = 1.0f / (1.0f + std::sqrt(distSq));

            m_chunkGenerator->RequestChunk(coord, lod, priority);
            m_pendingChunks.insert(coord);

            requestsThisFrame++;

            // Limit requests per frame to avoid overwhelming the queue
            if (requestsThisFrame >= m_config.maxChunksPerFrame * 2) {
                break;
            }
        }
    }
}

void EditorWorld::ProcessCompletedChunks() {
    if (!m_chunkGenerator || !m_chunkGenerator->HasCompletedChunks()) {
        return;
    }

    // Get completed chunks (limited per frame for smooth loading)
    auto completed = m_chunkGenerator->GetCompletedChunks(m_config.maxChunksPerFrame);

    float totalGenTime = 0.0f;

    for (auto& result : completed) {
        // Skip if no longer desired (may have been cancelled)
        if (m_pendingChunks.find(result.coord) == m_pendingChunks.end()) {
            continue;
        }

        // Remove from pending
        m_pendingChunks.erase(result.coord);

        // Check if we're at capacity
        if (m_loadedChunks.size() >= static_cast<size_t>(m_config.maxLoadedChunks)) {
            // Skip this chunk - we're at capacity
            continue;
        }

        // Upload mesh to GPU
        if (result.mesh && m_renderer) {
            auto uploadResult = m_renderer->UploadMesh(result.mesh);
            if (uploadResult.IsErr()) {
                spdlog::warn("Failed to upload chunk ({}, {}): {}",
                            result.coord.x, result.coord.z, uploadResult.Error());
                continue;
            }
        }

        // Create entity
        CreateChunkEntity(result.coord, result.mesh, result.lod);

        // Track in loaded chunks and spatial grid
        m_loadedChunks.insert(result.coord);
        m_spatialGrid->RegisterChunk(result.coord);

        m_stats.chunksLoadedThisFrame++;
        totalGenTime += result.generationTimeMs;
    }

    m_stats.chunkGenerationTimeMs = totalGenTime;
}

void EditorWorld::UnloadDistantChunks(const glm::vec3& cameraPos) {
    // Calculate unload distance (slightly larger than load radius to prevent thrashing)
    float unloadRadius = (m_config.loadRadius + 2) * m_config.chunkSize;
    float unloadRadiusSq = unloadRadius * unloadRadius;

    std::vector<ChunkCoord> chunksToUnload;

    for (const auto& coord : m_loadedChunks) {
        float chunkCenterX = coord.x * m_config.chunkSize + m_config.chunkSize * 0.5f;
        float chunkCenterZ = coord.z * m_config.chunkSize + m_config.chunkSize * 0.5f;
        float dx = chunkCenterX - cameraPos.x;
        float dz = chunkCenterZ - cameraPos.z;
        float distSq = dx * dx + dz * dz;

        if (distSq > unloadRadiusSq) {
            chunksToUnload.push_back(coord);
        }
    }

    // Limit unloads per frame
    size_t maxUnloads = m_config.maxChunksPerFrame;
    if (chunksToUnload.size() > maxUnloads) {
        chunksToUnload.resize(maxUnloads);
    }

    for (const auto& coord : chunksToUnload) {
        DestroyChunkEntity(coord);
        m_loadedChunks.erase(coord);
        m_spatialGrid->UnregisterChunk(coord);
        m_stats.chunksUnloadedThisFrame++;
    }
}

ChunkLOD EditorWorld::CalculateLOD(float distanceSq) const {
    if (distanceSq > m_config.lodDistance3Sq) {
        return ChunkLOD::Eighth;
    } else if (distanceSq > m_config.lodDistance2Sq) {
        return ChunkLOD::Quarter;
    } else if (distanceSq > m_config.lodDistance1Sq) {
        return ChunkLOD::Half;
    }
    return ChunkLOD::Full;
}

void EditorWorld::CreateChunkEntity(const ChunkCoord& coord,
                                   std::shared_ptr<Scene::MeshData> mesh,
                                   ChunkLOD lod) {
    if (!m_registry || !mesh) {
        return;
    }

    entt::entity entity = m_registry->CreateEntity();

    // Transform component - position at chunk origin
    Scene::TransformComponent transform;
    transform.position = glm::vec3(
        coord.x * m_config.chunkSize,
        0.0f,
        coord.z * m_config.chunkSize
    );
    transform.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    transform.scale = glm::vec3(1.0f);
    m_registry->GetRegistry().emplace<Scene::TransformComponent>(entity, transform);

    // Renderable component
    Scene::RenderableComponent renderable;
    renderable.mesh = mesh;
    renderable.albedoColor = glm::vec4(0.15f, 0.35f, 0.1f, 1.0f);  // Green terrain
    renderable.roughness = 0.95f;
    renderable.metallic = 0.0f;
    m_registry->GetRegistry().emplace<Scene::RenderableComponent>(entity, renderable);

    // Terrain chunk component
    Scene::TerrainChunkComponent chunk;
    chunk.chunkX = coord.x;
    chunk.chunkZ = coord.z;
    chunk.chunkSize = m_config.chunkSize;
    chunk.lodLevel = static_cast<uint32_t>(lod);
    m_registry->GetRegistry().emplace<Scene::TerrainChunkComponent>(entity, chunk);
}

void EditorWorld::DestroyChunkEntity(const ChunkCoord& coord) {
    if (!m_registry) {
        return;
    }

    // Find and destroy the entity with matching coordinates
    auto view = m_registry->GetRegistry().view<Scene::TerrainChunkComponent>();
    for (auto entity : view) {
        const auto& chunk = view.get<Scene::TerrainChunkComponent>(entity);
        if (chunk.chunkX == coord.x && chunk.chunkZ == coord.z) {
            m_registry->DestroyEntity(entity);
            break;
        }
    }
}

} // namespace Cortex
