// Scene construction helpers for Cortex Engine.
// Cornell box + hero "Dragon Over Water Studio" layouts.

#include "Engine.h"
#include "EngineEditorMode.h"
#include "Editor/EditorWorld.h"

#include "Scene/Components.h"
#include "Scene/TerrainNoise.h"
#include "LLM/SceneRecipes.h"
#include "LLM/CommandQueue.h"
#include "Utils/MeshGenerator.h"
#include "Utils/GLTFLoader.h"
#include "Graphics/RendererControlApplier.h"
#include "Graphics/Renderer.h"
#include "Graphics/RendererSceneProfile.h"
#include "Scene/ParticleEffectLibrary.h"

#include <cmath>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/geometric.hpp>
#include <nlohmann/json.hpp>

namespace Cortex {

using Graphics::Renderer;
using Scene::TransformComponent;

namespace {
    // Local constants for Cornell box and hero pool layout. These mirror the
    // values used in Engine.cpp but are kept TU-local so scene construction
    // helpers here remain self-contained.
    constexpr float kCornellHalfExtent = 4.0f;
    constexpr float kCornellHeight     = 3.0f;
    constexpr float kHeroPoolZ         = -3.0f;

    void ConfigureShowcaseCameraClip(Scene::CameraComponent& camera, float farPlane) {
        camera.nearPlane = 0.25f;
        camera.farPlane = farPlane;
    }

    entt::entity AddParticleEffect(Scene::ECS_Registry& registry,
                                   const char* tag,
                                   std::string_view effectId,
                                   const glm::vec3& position) {
        if (std::getenv("CORTEX_DISABLE_SCENE_PARTICLES")) {
            return entt::null;
        }
        entt::entity e = registry.CreateEntity();
        registry.AddComponent<Scene::TagComponent>(e, tag);
        auto& t = registry.AddComponent<Scene::TransformComponent>(e);
        t.position = position;

        Scene::ParticleEmitterComponent emitter;
        if (!Scene::ApplyParticleEffectDescriptor(effectId, emitter) &&
            !Scene::ApplyParticleEffectDescriptor("smoke", emitter)) {
            return entt::null;
        }
        emitter.defaultEffectPresetId = emitter.effectPresetId;
        registry.AddComponent<Scene::ParticleEmitterComponent>(e, emitter);
        return e;
    }

    void ScaleParticleEffect(Scene::ECS_Registry& registry,
                             entt::entity entity,
                             float rateScale,
                             float sizeScale,
                             float alphaScale) {
        if (entity == entt::null || !registry.HasComponent<Scene::ParticleEmitterComponent>(entity)) {
            return;
        }

        auto& emitter = registry.GetComponent<Scene::ParticleEmitterComponent>(entity);
        emitter.rate *= rateScale;
        emitter.sizeStart *= sizeScale;
        emitter.sizeEnd *= sizeScale;
        emitter.colorStart.a *= alphaScale;
        emitter.colorEnd.a *= alphaScale;
    }

    struct GenerativeRidgeLayer {
        float distanceM = 55.0f;
        float heightM = 12.0f;
        glm::vec3 color{0.16f, 0.18f, 0.22f};
    };

    struct GenerativeStructure {
        std::string type = "cabin";
        glm::vec3 position{0.0f, 0.0f, 0.0f};
        float yawDeg = 0.0f;
        float widthM = 3.8f;
        float depthM = 3.0f;
        float wallHeightM = 2.0f;
        float roofHeightM = 1.0f;
        bool litWindows = true;
    };

    struct GenerativeContactPatch {
        glm::vec2 position{0.0f, 0.0f};
        float radius = 0.8f;
        float darkness = 0.28f;
        float wetness = 0.24f;
    };

    struct GenerativeWorldGeometry {
        bool enabled = false;
        int foregroundOccluderCount = 0;
        int canyonWallLayers = 0;
        int talusClusterCount = 0;
        int redRockStrataLayers = 0;
        int shorelineSegmentCount = 0;
        int depthBandCount = 0;
        float canyonWidthM = 0.0f;
        float wallHeightM = 0.0f;
    };

    struct GenerativeSurfaceDetail {
        bool enabled = false;
        int pebbleCount = 0;
        int terrainCreaseCount = 0;
        int shoreFoamSegmentCount = 0;
        int wetGlintCount = 0;
        int occlusionRibbonCount = 0;
        float contactShadowStrength = 0.0f;
    };

    struct GenerativeImageContactOcclusion {
        bool enabled = false;
        int deepContactPatchCount = 0;
        float targetDarkContactFraction = 0.002f;
    };

    struct GenerativeSoftOcclusion {
        bool enabled = false;
        int penumbraPatchCount = 0;
        int contactGradientLayerCount = 0;
        int heroAnchorCount = 0;
        float targetSoftContactFraction = 0.010f;
    };

    struct GenerativeWaterShoreIntegration {
        bool enabled = false;
        int foamLaceSegmentCount = 0;
        int shorelineRippleCount = 0;
        int wetlineBandCount = 0;
        int reflectionGlintCount = 0;
        int submergedEdgeRockCount = 0;
    };

    struct GenerativeSurfaceMaterialRichness {
        bool enabled = false;
        int groundDecalCount = 0;
        int rockLichenPatchCount = 0;
        int desertStrataPatchCount = 0;
        int vegetationClusterCount = 0;
        int heroMaterialLineCount = 0;
    };

    struct GenerativeMeshSilhouetteRealism {
        bool enabled = false;
        int cliffMeshVerticalBands = 0;
        int cliffOverhangCount = 0;
        int heroBevelDetailCount = 0;
        int propDepthLayerCount = 0;
    };

    struct GenerativeNaturalisticEcology {
        bool enabled = false;
        int grassClusterCount = 0;
        int bushClusterCount = 0;
        int fernClusterCount = 0;
        int trunkCount = 0;
        int branchCount = 0;
        int stumpCount = 0;
        int mossRockCount = 0;
    };

    struct GenerativeAssetFidelity {
        bool enabled = false;
        int heroDetailCount = 0;
        int campDetailCount = 0;
        int cabinFacadeDetailCount = 0;
        int backdropDetailLayers = 0;
        int foregroundDressingClusters = 0;
    };

    struct GenerativeHeroEnvironmentGeometry {
        bool enabled = false;
        int highDetailCampPieceCount = 0;
        int highDetailCabinPieceCount = 0;
        int mountainMassLayerCount = 0;
        int cliffMassPieceCount = 0;
        int shorelinePropCount = 0;
        int irregularTreeSilhouetteCount = 0;
        int supportPropCount = 0;
    };

    struct GenerativeTextureMaterialFidelity {
        bool enabled = false;
        int textureSetCount = 0;
        int terrainSurfaceCount = 0;
        int rockSurfaceCount = 0;
        int woodSurfaceCount = 0;
        int fabricSurfaceCount = 0;
        int heroSurfaceCount = 0;
        int shoreSurfaceCount = 0;
    };

    struct GenerativeSourceGeometryFidelity {
        bool enabled = false;
        int sourceAssetSetCount = 0;
        int scannedLanternCount = 0;
        int scannedUtilityPropCount = 0;
        int scannedAnchorRockCount = 0;
        int heroAnchorCount = 0;
    };

    struct GenerativeRendererShadowOcclusionBudget {
        bool enabled = false;
        bool rendererSSAO = false;
        bool shadowMaps = false;
        bool dxrRequired = false;
        float ssaoRadius = 0.75f;
        float ssaoBias = 0.018f;
        float ssaoIntensity = 1.35f;
        float shadowBias = 0.0035f;
        float shadowPCFRadius = 2.5f;
        int contactReceiverPatchBudget = 0;
        int softPenumbraPatchBudget = 0;
        float rendererContactBlend = 0.0f;
    };

    struct GenerativeCinematicMaterialLighting {
        bool enabled = false;
        int triplanarDetailLayerCount = 0;
        int terrainReliefPatchCount = 0;
        int shadowCasterCount = 0;
        int contactReceiverCount = 0;
        int localizedLightCount = 0;
        int volumetricLightSliceCount = 0;
        int wetRoughnessVariationCount = 0;
        float sourceTextureWeight = 0.0f;
        float normalDetailScale = 0.0f;
        float roughnessVariation = 0.0f;
    };

    struct GenerativeTextureMaterialRuntimeCounts {
        int terrain = 0;
        int rock = 0;
        int wood = 0;
        int fabric = 0;
        int hero = 0;
        int shore = 0;
        std::unordered_set<std::string> sets;
    };

    struct GenerativeAtmosphereFidelity {
        bool enabled = false;
        bool nightSkyControl = false;
        int stormLayerCount = 0;
        int rainStreakCount = 0;
        int hazeDepthLayers = 0;
        float moonlightExposure = 0.0f;
        float skyBackgroundLift = 1.0f;
    };

    struct GenerativeGeometryRealism {
        bool enabled = false;
        int cliffErosionRidgeCount = 0;
        int strataBreakupCount = 0;
        int foregroundReliefClusters = 0;
        float wallNormalBreakup = 0.0f;
    };

    struct GenerativeAuthoredSceneModule {
        bool enabled = false;
        std::string moduleId;
        int compositionAnchorCount = 0;
        int terrainSetpieceCount = 0;
        int heroClusterCount = 0;
        int foregroundFrameCount = 0;
        int backdropGateCount = 0;
        int lightingZoneCount = 0;
        int materialFamilyCount = 0;
        int waterShapeSegmentCount = 0;
        int practicalLightCount = 0;
        std::string contrastKey;
    };

    float GenerativeTerrainNoise(float x, float z, float phase) {
        const float a = std::sin(x * 0.33f + z * 0.17f + phase) * 0.48f;
        const float b = std::sin(x * 0.91f - z * 0.27f + phase * 1.73f) * 0.22f;
        const float c = std::sin((x + z) * 1.77f + phase * 0.41f) * 0.08f;
        return a + b + c;
    }

    float GenerativeTerrainHeight(float worldX,
                                  float worldZ,
                                  float relief,
                                  float microRelief,
                                  float shoreZ,
                                  bool waterOn) {
        const float heroDist = glm::length(glm::vec2(worldX, worldZ - 1.15f));
        const float heroFlatten = std::clamp((heroDist - 2.8f) / 8.0f, 0.0f, 1.0f);
        const float shoreDist = waterOn ? std::abs(worldZ - shoreZ) : 99.0f;
        const float shoreFlatten = waterOn ? std::clamp((shoreDist - 0.8f) / 5.5f, 0.0f, 1.0f) : 1.0f;
        const float depthBias = std::clamp((worldZ + 1.5f) / 16.0f, -0.35f, 0.65f);
        const float macro = GenerativeTerrainNoise(worldX, worldZ, 2.37f) * relief;
        const float micro = GenerativeTerrainNoise(worldX * 2.7f, worldZ * 2.7f, 8.13f) * microRelief;
        const float mask = std::max(0.16f, heroFlatten) * std::max(0.20f, shoreFlatten);
        return std::clamp((macro + micro) * mask + depthBias * 0.055f, -0.10f, relief * 1.25f);
    }

    std::shared_ptr<Scene::MeshData> CreateGenerativeTerrainMesh(float width,
                                                                 float length,
                                                                 float centerZ,
                                                                 float relief,
                                                                 float microRelief,
                                                                 float shoreZ,
                                                                 bool waterOn,
                                                                 uint32_t gridDim) {
        auto mesh = std::make_shared<Scene::MeshData>();
        mesh->kind = Scene::MeshKind::Procedural;
        gridDim = std::clamp(gridDim, 16u, 128u);

        const uint32_t verts = gridDim + 1u;
        const float halfW = width * 0.5f;
        const float halfL = length * 0.5f;
        mesh->positions.reserve(verts * verts);
        mesh->normals.resize(verts * verts, glm::vec3(0.0f, 1.0f, 0.0f));
        mesh->texCoords.reserve(verts * verts);

        auto idx = [verts](uint32_t x, uint32_t z) { return z * verts + x; };
        for (uint32_t iz = 0; iz <= gridDim; ++iz) {
            const float vz = static_cast<float>(iz) / static_cast<float>(gridDim);
            const float localZ = halfL - vz * length;
            const float worldZ = centerZ + localZ;
            for (uint32_t ix = 0; ix <= gridDim; ++ix) {
                const float vx = static_cast<float>(ix) / static_cast<float>(gridDim);
                const float localX = -halfW + vx * width;
                const float y = GenerativeTerrainHeight(localX, worldZ, relief, microRelief, shoreZ, waterOn);
                mesh->positions.emplace_back(localX, y, localZ);
                mesh->texCoords.emplace_back(vx, vz);
            }
        }

        for (uint32_t iz = 0; iz <= gridDim; ++iz) {
            for (uint32_t ix = 0; ix <= gridDim; ++ix) {
                const uint32_t xl = ix > 0 ? ix - 1u : ix;
                const uint32_t xr = ix < gridDim ? ix + 1u : ix;
                const uint32_t zd = iz > 0 ? iz - 1u : iz;
                const uint32_t zu = iz < gridDim ? iz + 1u : iz;
                const glm::vec3 dx = mesh->positions[idx(xr, iz)] - mesh->positions[idx(xl, iz)];
                const glm::vec3 dz = mesh->positions[idx(ix, zd)] - mesh->positions[idx(ix, zu)];
                glm::vec3 n = glm::cross(dx, dz);
                if (glm::length(n) < 1e-5f) {
                    n = glm::vec3(0.0f, 1.0f, 0.0f);
                } else {
                    n = glm::normalize(n);
                    if (n.y < 0.0f) {
                        n = -n;
                    }
                }
                mesh->normals[idx(ix, iz)] = n;
            }
        }

        for (uint32_t iz = 0; iz < gridDim; ++iz) {
            for (uint32_t ix = 0; ix < gridDim; ++ix) {
                const uint32_t a = idx(ix, iz);
                const uint32_t b = idx(ix + 1u, iz);
                const uint32_t c = idx(ix, iz + 1u);
                const uint32_t d = idx(ix + 1u, iz + 1u);
                mesh->indices.insert(mesh->indices.end(), {a, b, d, a, d, c});
            }
        }
        mesh->UpdateBounds();
        return mesh;
    }

    std::shared_ptr<Scene::MeshData> CreateGenerativeRidgeMesh(float width,
                                                               float height,
                                                               float baseY,
                                                               float phase) {
        auto mesh = std::make_shared<Scene::MeshData>();
        mesh->kind = Scene::MeshKind::Procedural;
        constexpr uint32_t kSegments = 28;
        mesh->positions.reserve((kSegments + 1) * 2);
        mesh->normals.reserve((kSegments + 1) * 2);
        mesh->texCoords.reserve((kSegments + 1) * 2);
        for (uint32_t i = 0; i <= kSegments; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(kSegments);
            const float x = (u - 0.5f) * width;
            const float n0 = std::sin(u * 19.0f + phase) * 0.18f;
            const float n1 = std::sin(u * 43.0f + phase * 1.7f) * 0.10f;
            const float ridge = std::clamp(0.54f + n0 + n1, 0.30f, 0.92f);
            const float yTop = baseY + height * ridge;
            mesh->positions.emplace_back(x, baseY, 0.0f);
            mesh->positions.emplace_back(x, yTop, 0.0f);
            mesh->normals.emplace_back(0.0f, 0.0f, 1.0f);
            mesh->normals.emplace_back(0.0f, 0.0f, 1.0f);
            mesh->texCoords.emplace_back(u, 1.0f);
            mesh->texCoords.emplace_back(u, 0.0f);
        }
        for (uint32_t i = 0; i < kSegments; ++i) {
            const uint32_t b0 = i * 2u;
            const uint32_t t0 = b0 + 1u;
            const uint32_t b1 = b0 + 2u;
            const uint32_t t1 = b0 + 3u;
            mesh->indices.insert(mesh->indices.end(), { b0, b1, t1, b0, t1, t0 });
        }
        mesh->UpdateBounds();
        return mesh;
    }

    std::shared_ptr<Scene::MeshData> CreateGenerativeCliffWallMesh(float length,
                                                                   float height,
                                                                   float roughness,
                                                                   float phase,
                                                                   uint32_t verticalBands = 1u) {
        auto mesh = std::make_shared<Scene::MeshData>();
        mesh->kind = Scene::MeshKind::Procedural;
        constexpr uint32_t kSegments = 36;
        verticalBands = std::clamp(verticalBands, 1u, 10u);
        const uint32_t rows = verticalBands + 1u;
        mesh->positions.reserve((kSegments + 1u) * rows);
        mesh->normals.resize((kSegments + 1u) * rows, glm::vec3(1.0f, 0.0f, 0.0f));
        mesh->texCoords.reserve((kSegments + 1u) * rows);

        auto idx = [rows](uint32_t seg, uint32_t row) {
            return seg * rows + row;
        };

        const float halfLen = length * 0.5f;
        for (uint32_t i = 0; i <= kSegments; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(kSegments);
            const float z = -halfLen + u * length;
            const float ledge = std::sin(u * 23.0f + phase) * 0.35f +
                                std::sin(u * 57.0f + phase * 1.9f) * 0.16f;
            const float top = height * std::clamp(0.72f + std::sin(u * 11.0f + phase * 0.7f) * 0.18f,
                                                  0.48f,
                                                  1.02f);
            const float base = -0.18f + std::sin(u * 17.0f + phase) * 0.08f;
            for (uint32_t row = 0; row < rows; ++row) {
                const float v = static_cast<float>(row) / static_cast<float>(verticalBands);
                const float terrace = std::floor(v * static_cast<float>(verticalBands)) / static_cast<float>(std::max(1u, verticalBands));
                const float shelf = std::sin(v * 31.0f + u * 13.0f + phase) * 0.12f +
                                    std::sin(v * 9.0f - u * 41.0f + phase * 0.33f) * 0.07f;
                const float bandStep = (terrace - 0.5f) * roughness * 0.18f;
                const float undercut = ((row % 2u) == 0u ? -0.08f : 0.06f) * roughness;
                const float x = ledge * roughness + shelf * roughness + bandStep + undercut;
                const float y = glm::mix(base, top, v);
                mesh->positions.emplace_back(x, y, z);
                mesh->texCoords.emplace_back(u * 6.0f, 1.0f - v);
            }
        }

        for (uint32_t i = 0; i < kSegments; ++i) {
            for (uint32_t row = 0; row < verticalBands; ++row) {
                const uint32_t a = idx(i, row);
                const uint32_t b = idx(i + 1u, row);
                const uint32_t c = idx(i, row + 1u);
                const uint32_t d = idx(i + 1u, row + 1u);
                mesh->indices.insert(mesh->indices.end(), { a, b, d, a, d, c });
            }
        }

        for (uint32_t i = 0; i <= kSegments; ++i) {
            for (uint32_t row = 0; row < rows; ++row) {
                const uint32_t il = i > 0 ? i - 1u : i;
                const uint32_t ir = i < kSegments ? i + 1u : i;
                const uint32_t rd = row > 0 ? row - 1u : row;
                const uint32_t ru = row < verticalBands ? row + 1u : row;
                const glm::vec3 dz = mesh->positions[idx(ir, row)] - mesh->positions[idx(il, row)];
                const glm::vec3 dy = mesh->positions[idx(i, ru)] - mesh->positions[idx(i, rd)];
                glm::vec3 n = glm::cross(dz, dy);
                if (glm::length(n) < 1e-5f) {
                    n = glm::vec3(1.0f, 0.0f, 0.0f);
                } else {
                    n = glm::normalize(n);
                }
                mesh->normals[idx(i, row)] = n;
            }
        }
        mesh->UpdateBounds();
        return mesh;
    }

    std::shared_ptr<Scene::MeshData> CreateGenerativeRockShardMesh(float phase) {
        auto mesh = std::make_shared<Scene::MeshData>();
        mesh->kind = Scene::MeshKind::Procedural;
        const glm::vec3 top(0.06f * std::sin(phase), 0.72f, -0.04f);
        const glm::vec3 bottom(0.0f, -0.08f, 0.0f);
        const glm::vec3 left(-0.62f, 0.12f, -0.18f);
        const glm::vec3 right(0.56f, 0.08f, 0.22f);
        const glm::vec3 front(-0.10f, 0.18f, 0.66f);
        const glm::vec3 back(0.16f, 0.10f, -0.58f);

        auto addTri = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
            const uint32_t base = static_cast<uint32_t>(mesh->positions.size());
            glm::vec3 n = glm::cross(b - a, c - a);
            if (glm::length(n) < 1e-5f) {
                n = glm::vec3(0.0f, 1.0f, 0.0f);
            } else {
                n = glm::normalize(n);
            }
            mesh->positions.insert(mesh->positions.end(), {a, b, c});
            mesh->normals.insert(mesh->normals.end(), {n, n, n});
            mesh->texCoords.insert(mesh->texCoords.end(), {
                glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 0.0f), glm::vec2(0.5f, 1.0f)
            });
            mesh->indices.insert(mesh->indices.end(), {base, base + 1u, base + 2u});
        };

        addTri(top, front, right);
        addTri(top, right, back);
        addTri(top, back, left);
        addTri(top, left, front);
        addTri(bottom, right, front);
        addTri(bottom, back, right);
        addTri(bottom, left, back);
        addTri(bottom, front, left);
        mesh->UpdateBounds();
        return mesh;
    }

    std::shared_ptr<Scene::MeshData> CreateGenerativeGableRoofMesh(float width,
                                                                   float depth,
                                                                   float height) {
        auto mesh = std::make_shared<Scene::MeshData>();
        mesh->kind = Scene::MeshKind::Procedural;

        const float hx = width * 0.5f;
        const float hz = depth * 0.5f;
        const glm::vec3 lb(-hx, 0.0f, -hz);
        const glm::vec3 rb( hx, 0.0f, -hz);
        const glm::vec3 lf(-hx, 0.0f,  hz);
        const glm::vec3 rf( hx, 0.0f,  hz);
        const glm::vec3 pb(0.0f, height, -hz);
        const glm::vec3 pf(0.0f, height,  hz);

        auto addTri = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
            const uint32_t base = static_cast<uint32_t>(mesh->positions.size());
            glm::vec3 n = glm::cross(b - a, c - a);
            if (glm::length(n) < 1e-5f) {
                n = glm::vec3(0.0f, 1.0f, 0.0f);
            } else {
                n = glm::normalize(n);
            }
            mesh->positions.insert(mesh->positions.end(), {a, b, c});
            mesh->normals.insert(mesh->normals.end(), {n, n, n});
            mesh->texCoords.insert(mesh->texCoords.end(), {
                glm::vec2(0.0f, 1.0f), glm::vec2(1.0f, 1.0f), glm::vec2(0.5f, 0.0f)
            });
            mesh->indices.insert(mesh->indices.end(), {base, base + 1u, base + 2u});
        };
        auto addQuad = [&](const glm::vec3& a, const glm::vec3& b,
                           const glm::vec3& c, const glm::vec3& d) {
            addTri(a, b, c);
            addTri(a, c, d);
        };

        addTri(lf, rf, pf);       // front gable
        addTri(rb, lb, pb);       // back gable
        addQuad(lb, lf, pf, pb);  // left roof slope
        addQuad(rf, rb, pb, pf);  // right roof slope
        mesh->UpdateBounds();
        return mesh;
    }

    std::shared_ptr<Scene::MeshData> LoadNaturalisticShowcaseMesh(const char* relativeGltf) {
        namespace fs = std::filesystem;
        const fs::path rel = fs::path("assets") / "models" / "naturalistic_showcase" / relativeGltf;
        std::vector<fs::path> candidates;

        fs::path cwd;
        try {
            cwd = fs::current_path();
        } catch (...) {
            cwd = fs::path(".");
        }

        candidates.push_back(cwd / rel);
        candidates.push_back(cwd / ".." / rel);
        candidates.push_back(cwd / ".." / ".." / rel);
        candidates.push_back(cwd / ".." / ".." / ".." / "CortexEngine" / rel);

        for (const auto& candidate : candidates) {
            std::error_code ec;
            if (!fs::exists(candidate, ec)) {
                continue;
            }
            auto result = Utils::LoadGLTFMesh(candidate.string());
            if (result.IsOk()) {
                spdlog::info("Loaded naturalistic showcase mesh '{}'", candidate.string());
                return result.Value();
            }
            spdlog::warn("Failed to load naturalistic showcase mesh '{}': {}", candidate.string(), result.Error());
        }

        spdlog::warn("Naturalistic showcase mesh not found: {}", relativeGltf);
        return nullptr;
    }

    std::shared_ptr<Scene::MeshData> LoadPretrainedGeneratedMesh(const char* relativeGltf) {
        namespace fs = std::filesystem;
        const fs::path rel = fs::path("assets") / "generated" / "pretrained_assets" / relativeGltf;
        std::vector<fs::path> candidates;

        fs::path cwd;
        try {
            cwd = fs::current_path();
        } catch (...) {
            cwd = fs::path(".");
        }

        candidates.push_back(cwd / rel);
        candidates.push_back(cwd / ".." / rel);
        candidates.push_back(cwd / ".." / ".." / rel);
        candidates.push_back(cwd / ".." / ".." / ".." / "CortexEngine" / rel);

        for (const auto& candidate : candidates) {
            std::error_code ec;
            if (!fs::exists(candidate, ec)) {
                continue;
            }
            auto result = Utils::LoadGLTFMesh(candidate.string());
            if (result.IsOk()) {
                spdlog::info("Loaded pretrained generated mesh '{}'", candidate.string());
                return result.Value();
            }
            spdlog::warn("Failed to load pretrained generated mesh '{}': {}", candidate.string(), result.Error());
        }

        spdlog::warn("Pretrained generated mesh not found: {}", relativeGltf);
        return nullptr;
    }

    std::shared_ptr<Scene::MeshData> LoadGeneratedFixtureMesh(const char* relativeGltf) {
        namespace fs = std::filesystem;
        const fs::path rel = fs::path("assets") / "generated" / "final_art_fidelity_meshes" / relativeGltf;
        std::vector<fs::path> candidates;

        fs::path cwd;
        try {
            cwd = fs::current_path();
        } catch (...) {
            cwd = fs::path(".");
        }

        candidates.push_back(cwd / rel);
        candidates.push_back(cwd / ".." / rel);
        candidates.push_back(cwd / ".." / ".." / rel);
        candidates.push_back(cwd / ".." / ".." / ".." / "CortexEngine" / rel);

        for (const auto& candidate : candidates) {
            std::error_code ec;
            if (!fs::exists(candidate, ec)) {
                continue;
            }
            auto result = Utils::LoadGLTFMesh(candidate.string());
            if (result.IsOk()) {
                spdlog::info("Loaded generated fixture mesh '{}'", candidate.string());
                return result.Value();
            }
            spdlog::warn("Failed to load generated fixture mesh '{}': {}", candidate.string(), result.Error());
        }

        spdlog::warn("Generated fixture mesh not found: {}", relativeGltf);
        return nullptr;
    }

    std::shared_ptr<Scene::MeshData> LoadProjectRelativeMesh(const char* relativePath) {
        namespace fs = std::filesystem;
        const fs::path rel = fs::path(relativePath ? relativePath : "");
        std::vector<fs::path> candidates;

        fs::path cwd;
        try {
            cwd = fs::current_path();
        } catch (...) {
            cwd = fs::path(".");
        }

        candidates.push_back(cwd / rel);
        candidates.push_back(cwd / ".." / rel);
        candidates.push_back(cwd / ".." / ".." / rel);
        candidates.push_back(cwd / ".." / ".." / ".." / "CortexEngine" / rel);

        for (const auto& candidate : candidates) {
            std::error_code ec;
            if (!fs::exists(candidate, ec)) {
                continue;
            }
            auto result = Utils::LoadGLTFMesh(candidate.string());
            if (result.IsOk()) {
                spdlog::info("Loaded project-relative mesh '{}'", candidate.string());
                return result.Value();
            }
            spdlog::warn("Failed to load project-relative mesh '{}': {}", candidate.string(), result.Error());
        }

        spdlog::warn("Project-relative mesh not found: {}", relativePath ? relativePath : "");
        return nullptr;
    }

    struct AssetLedMaterialSettings {
        glm::vec4 color{1.0f};
        float metallic = 0.0f;
        float roughness = 0.55f;
        float transmission = 0.0f;
        float ior = 1.5f;
        glm::vec3 emissive{0.0f};
        float emissiveStrength = 1.0f;
        float wetness = 0.0f;
        float proceduralMask = 0.0f;
        bool doubleSided = false;
        Scene::RenderableComponent::AlphaMode alphaMode = Scene::RenderableComponent::AlphaMode::Opaque;
        Scene::RenderableComponent::RenderLayer layer = Scene::RenderableComponent::RenderLayer::Opaque;
        const char* preset = "masonry";
        std::string albedoTexture;
        std::string normalTexture;
        std::string roughnessTexture;
        std::string metallicTexture;
        std::string occlusionTexture;
    };

    struct AssetLedTextureSet {
        const char* albedo = nullptr;
        const char* normal = nullptr;
        const char* arm = nullptr;
    };

    struct RuntimeLayoutTransform {
        glm::vec3 position{0.0f};
        glm::vec3 scale{1.0f};
        glm::vec3 rotation{0.0f};
    };

    struct PretrainedRuntimeLayout {
        bool loaded = false;
        glm::vec3 cameraPosition{-1.12f, 0.84f, 0.15f};
        glm::vec3 cameraTarget{-0.34f, 0.71f, 0.88f};
        float cameraFov = 25.0f;
        std::unordered_map<std::string, RuntimeLayoutTransform> byRole;
        std::unordered_map<std::string, RuntimeLayoutTransform> overrides;
        std::optional<RuntimeLayoutTransform> tabletopVignette;
    };

    bool ReadJsonVec3(const nlohmann::json& value, glm::vec3& out) {
        if (!value.is_array() || value.size() < 3) {
            return false;
        }
        out = glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
        return true;
    }

    RuntimeLayoutTransform TransformFromJson(const nlohmann::json& transform) {
        RuntimeLayoutTransform out;
        if (transform.contains("position")) {
            ReadJsonVec3(transform["position"], out.position);
        }
        if (transform.contains("scale")) {
            ReadJsonVec3(transform["scale"], out.scale);
        }
        if (transform.contains("rotation_degrees")) {
            glm::vec3 degrees{0.0f};
            if (ReadJsonVec3(transform["rotation_degrees"], degrees)) {
                out.rotation = glm::radians(degrees);
            }
        }
        return out;
    }

    std::optional<std::filesystem::path> FindRuntimeAssetFile(const std::filesystem::path& relativePath) {
        namespace fs = std::filesystem;
        std::vector<fs::path> candidates;
        fs::path cwd;
        try {
            cwd = fs::current_path();
        } catch (...) {
            cwd = fs::path(".");
        }
        candidates.push_back(cwd / relativePath);
        candidates.push_back(cwd / ".." / relativePath);
        candidates.push_back(cwd / ".." / ".." / relativePath);
        candidates.push_back(cwd / ".." / ".." / ".." / "CortexEngine" / relativePath);
        std::optional<fs::path> newest;
        fs::file_time_type newestTime{};
        for (const auto& candidate : candidates) {
            std::error_code ec;
            if (fs::exists(candidate, ec)) {
                const auto writeTime = fs::last_write_time(candidate, ec);
                if (!ec && (!newest || writeTime > newestTime)) {
                    newest = candidate;
                    newestTime = writeTime;
                }
            }
        }
        return newest;
    }

    PretrainedRuntimeLayout LoadPretrainedRuntimeLayout() {
        PretrainedRuntimeLayout layout;
        const auto path = FindRuntimeAssetFile(
            std::filesystem::path("assets") / "scenes" / "pretrained_generated" /
            "pretrained_rain_tabletop_shap_e_v1" / "scene_seed.json");
        if (!path) {
            spdlog::warn("Pretrained runtime layout seed not found; using baked fallback transforms.");
            return layout;
        }

        try {
            std::ifstream file(*path);
            const auto root = nlohmann::json::parse(file);
            if (root.contains("camera")) {
                const auto& camera = root["camera"];
                if (camera.contains("position")) { ReadJsonVec3(camera["position"], layout.cameraPosition); }
                if (camera.contains("target")) { ReadJsonVec3(camera["target"], layout.cameraTarget); }
                layout.cameraFov = camera.value("fov", layout.cameraFov);
            }
            for (const auto& asset : root.value("assets", nlohmann::json::array())) {
                if (!asset.contains("role") || !asset.contains("transform")) {
                    continue;
                }
                layout.byRole[asset["role"].get<std::string>()] = TransformFromJson(asset["transform"]);
            }
            if (root.contains("runtime_overrides") &&
                root["runtime_overrides"].contains("tabletop_vignette_screen") &&
                root["runtime_overrides"]["tabletop_vignette_screen"].contains("transform")) {
                layout.tabletopVignette = TransformFromJson(root["runtime_overrides"]["tabletop_vignette_screen"]["transform"]);
            }
            if (root.contains("runtime_overrides") && root["runtime_overrides"].is_object()) {
                for (auto it = root["runtime_overrides"].begin(); it != root["runtime_overrides"].end(); ++it) {
                    if (it.value().contains("transform")) {
                        layout.overrides[it.key()] = TransformFromJson(it.value()["transform"]);
                    }
                }
            }
            layout.loaded = true;
            spdlog::info("Loaded pretrained runtime layout '{}'", path->string());
        } catch (const std::exception& ex) {
            spdlog::warn("Failed to load pretrained runtime layout '{}': {}", path->string(), ex.what());
        }
        return layout;
    }

    RuntimeLayoutTransform GetLayoutTransform(const PretrainedRuntimeLayout& layout,
                                              const char* role,
                                              const glm::vec3& fallbackPosition,
                                              const glm::vec3& fallbackScale,
                                              const glm::vec3& fallbackRotation) {
        const auto it = layout.byRole.find(role ? role : "");
        if (it != layout.byRole.end()) {
            return it->second;
        }
        return RuntimeLayoutTransform{fallbackPosition, fallbackScale, fallbackRotation};
    }

    RuntimeLayoutTransform GetRuntimeOverride(const PretrainedRuntimeLayout& layout,
                                              const char* id,
                                              const glm::vec3& fallbackPosition,
                                              const glm::vec3& fallbackScale,
                                              const glm::vec3& fallbackRotation) {
        const auto it = layout.overrides.find(id ? id : "");
        if (it != layout.overrides.end()) {
            return it->second;
        }
        return RuntimeLayoutTransform{fallbackPosition, fallbackScale, fallbackRotation};
    }

    std::string ToLowerAscii(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    glm::vec4 ReadJsonVec4Or(const nlohmann::json& value, const glm::vec4& fallback) {
        if (!value.is_array() || value.size() < 4) {
            return fallback;
        }
        return glm::vec4(value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>());
    }

    glm::vec3 ReadJsonVec3Or(const nlohmann::json& value, const glm::vec3& fallback) {
        glm::vec3 out = fallback;
        if (ReadJsonVec3(value, out)) {
            return out;
        }
        return fallback;
    }

    AssetLedMaterialSettings ModelAuthoredMaterialFromJson(const nlohmann::json& root,
                                                           const std::string& materialId) {
        AssetLedMaterialSettings material;
        if (!root.contains("materials") || !root["materials"].contains(materialId)) {
            material.color = glm::vec4(0.42f, 0.42f, 0.44f, 1.0f);
            material.preset = "masonry";
            return material;
        }

        const auto& data = root["materials"][materialId];
        material.color = data.contains("color")
            ? ReadJsonVec4Or(data["color"], material.color)
            : material.color;
        material.metallic = data.value("metallic", material.metallic);
        material.roughness = data.value("roughness", material.roughness);
        material.transmission = data.value("transmission", material.transmission);
        material.ior = data.value("ior", material.ior);
        material.emissive = data.contains("emissive")
            ? ReadJsonVec3Or(data["emissive"], material.emissive)
            : material.emissive;
        material.emissiveStrength = data.value("emissive_strength", material.emissiveStrength);
        material.wetness = data.value("wetness", material.wetness);
        material.proceduralMask = data.value("procedural_mask", material.proceduralMask);
        material.albedoTexture = data.value("albedo_texture", std::string{});
        material.normalTexture = data.value("normal_texture", std::string{});
        material.roughnessTexture = data.value("roughness_texture", std::string{});
        material.metallicTexture = data.value("metallic_texture", std::string{});
        material.occlusionTexture = data.value("occlusion_texture", std::string{});
        material.doubleSided = data.value("double_sided", material.transmission > 0.0f || material.color.a < 0.99f);
        if (ToLowerAscii(data.value("alpha", std::string{})) == "blend" || material.color.a < 0.99f) {
            material.alphaMode = Scene::RenderableComponent::AlphaMode::Blend;
        } else if (ToLowerAscii(data.value("alpha", std::string{})) == "mask") {
            material.alphaMode = Scene::RenderableComponent::AlphaMode::Mask;
        }
        if (ToLowerAscii(data.value("layer", std::string{})) == "overlay") {
            material.layer = Scene::RenderableComponent::RenderLayer::Overlay;
        }

        static std::unordered_map<std::string, std::string> presetStorage;
        const std::string preset = data.value("preset", std::string("masonry"));
        presetStorage[materialId] = preset;
        material.preset = presetStorage[materialId].c_str();
        return material;
    }

    std::shared_ptr<Scene::MeshData> CreateModelAuthoredPrimitiveMesh(const std::string& primitive) {
        const std::string shape = ToLowerAscii(primitive);
        if (shape == "plane") {
            return Utils::MeshGenerator::CreatePlane();
        }
        if (shape == "quad") {
            return Utils::MeshGenerator::CreateQuad();
        }
        if (shape == "sphere") {
            return Utils::MeshGenerator::CreateSphere(0.5f, 32);
        }
        if (shape == "cylinder") {
            return Utils::MeshGenerator::CreateCylinder(0.5f, 1.0f, 32);
        }
        if (shape == "cone") {
            return Utils::MeshGenerator::CreateCone(0.5f, 1.0f, 32);
        }
        if (shape == "torus") {
            return Utils::MeshGenerator::CreateTorus(0.5f, 0.16f, 32, 16);
        }
        if (shape == "disk") {
            return Utils::MeshGenerator::CreateDisk(0.5f, 32);
        }
        return Utils::MeshGenerator::CreateCube();
    }

    std::shared_ptr<Scene::MeshData> LoadModelAuthoredObjectMesh(const nlohmann::json& object) {
        const std::string kind = ToLowerAscii(object.value("kind", std::string("primitive")));
        if (kind == "primitive") {
            return CreateModelAuthoredPrimitiveMesh(object.value("primitive", std::string("cube")));
        }

        std::string runtimeAsset = object.value("runtime_asset", std::string{});
        std::replace(runtimeAsset.begin(), runtimeAsset.end(), '\\', '/');
        const std::string generatedPrefix = "assets/generated/pretrained_assets/";
        const std::string generatedFixturePrefix = "assets/generated/final_art_fidelity_meshes/";
        const std::string naturalisticPrefix = "assets/models/naturalistic_showcase/";
        const std::string kenneyPrefix = "assets/models/kenney_furniture_kit/";

        if (runtimeAsset.rfind(generatedPrefix, 0) == 0) {
            const std::string relative = runtimeAsset.substr(generatedPrefix.size());
            return LoadPretrainedGeneratedMesh(relative.c_str());
        }
        if (runtimeAsset.rfind(generatedFixturePrefix, 0) == 0) {
            const std::string relative = runtimeAsset.substr(generatedFixturePrefix.size());
            return LoadGeneratedFixtureMesh(relative.c_str());
        }
        if (runtimeAsset.rfind(naturalisticPrefix, 0) == 0) {
            const std::string relative = runtimeAsset.substr(naturalisticPrefix.size());
            return LoadNaturalisticShowcaseMesh(relative.c_str());
        }
        if (runtimeAsset.rfind(kenneyPrefix, 0) == 0) {
            return LoadProjectRelativeMesh(runtimeAsset.c_str());
        }
        if (kind == "pretrained_mesh") {
            return LoadPretrainedGeneratedMesh(runtimeAsset.c_str());
        }
        if (kind == "generated_mesh") {
            return LoadGeneratedFixtureMesh(runtimeAsset.c_str());
        }
        if (kind == "naturalistic_asset") {
            return LoadNaturalisticShowcaseMesh(runtimeAsset.c_str());
        }
        if (kind == "kenney_asset") {
            return LoadProjectRelativeMesh(runtimeAsset.c_str());
        }
        return nullptr;
    }

    std::optional<std::filesystem::path> FindModelAuthoredSceneSeed() {
        namespace fs = std::filesystem;
        if (const char* env = std::getenv("CORTEX_MODEL_AUTHORED_SCENE_SEED")) {
            if (env[0] != '\0') {
                const fs::path requested(env);
                if (requested.is_absolute()) {
                    std::error_code ec;
                    if (fs::exists(requested, ec)) {
                        return requested;
                    }
                } else if (auto found = FindRuntimeAssetFile(requested)) {
                    return found;
                }
            }
        }
        return FindRuntimeAssetFile(fs::path("assets") / "scenes" / "model_authored" / "latest_scene" / "scene_seed.json");
    }

    std::optional<nlohmann::json> LoadModelAuthoredSceneSeed(std::filesystem::path& outPath) {
        const auto path = FindModelAuthoredSceneSeed();
        if (!path) {
            spdlog::warn("Model-authored scene seed not found. Run tools/author_model_scene.py first.");
            return std::nullopt;
        }
        try {
            std::ifstream file(*path);
            auto root = nlohmann::json::parse(file);
            outPath = *path;
            return root;
        } catch (const std::exception& ex) {
            spdlog::warn("Failed to parse model-authored scene seed '{}': {}", path->string(), ex.what());
            return std::nullopt;
        }
    }

    AssetLedTextureSet GetNaturalisticAssetTextureSet(const char* assetId) {
        const std::string id = assetId ? assetId : "";
        if (id == "boulder_01") {
            return {
                "assets/models/naturalistic_showcase/boulder_01/textures/boulder_01_diff_1k.jpg",
                "assets/models/naturalistic_showcase/boulder_01/textures/boulder_01_nor_gl_1k.jpg",
                "assets/models/naturalistic_showcase/boulder_01/textures/boulder_01_arm_1k.jpg"};
        }
        if (id == "dead_tree_trunk") {
            return {
                "assets/models/naturalistic_showcase/dead_tree_trunk/textures/dead_tree_trunk_diff_1k.jpg",
                "assets/models/naturalistic_showcase/dead_tree_trunk/textures/dead_tree_trunk_nor_gl_1k.jpg",
                "assets/models/naturalistic_showcase/dead_tree_trunk/textures/dead_tree_trunk_arm_1k.jpg"};
        }
        if (id == "dry_branches_medium_01") {
            return {
                "assets/models/naturalistic_showcase/dry_branches_medium_01/textures/dry_branches_medium_01_diff_1k.jpg",
                "assets/models/naturalistic_showcase/dry_branches_medium_01/textures/dry_branches_medium_01_nor_gl_1k.jpg",
                "assets/models/naturalistic_showcase/dry_branches_medium_01/textures/dry_branches_medium_01_arm_1k.jpg"};
        }
        if (id == "fern_02") {
            return {
                "assets/models/naturalistic_showcase/fern_02/textures/fern_02_diff_1k.jpg",
                "assets/models/naturalistic_showcase/fern_02/textures/fern_02_nor_gl_1k.jpg",
                "assets/models/naturalistic_showcase/fern_02/textures/fern_02_arm_1k.jpg"};
        }
        if (id == "grass_bermuda_01") {
            return {
                "assets/models/naturalistic_showcase/grass_bermuda_01/textures/grass_bermuda_01_diff_1k.jpg",
                "assets/models/naturalistic_showcase/grass_bermuda_01/textures/grass_bermuda_01_nor_gl_1k.jpg",
                "assets/models/naturalistic_showcase/grass_bermuda_01/textures/grass_bermuda_01_arm_1k.jpg"};
        }
        if (id == "tree_stump_01") {
            return {
                "assets/models/naturalistic_showcase/tree_stump_01/textures/tree_stump_01_diff_1k.jpg",
                "assets/models/naturalistic_showcase/tree_stump_01/textures/tree_stump_01_nor_gl_1k.jpg",
                "assets/models/naturalistic_showcase/tree_stump_01/textures/tree_stump_01_arm_1k.jpg"};
        }
        if (id == "rock_moss_set_01") {
            return {
                "assets/models/naturalistic_showcase/rock_moss_set_01/textures/rock_moss_set_01_diff_1k.jpg",
                "assets/models/naturalistic_showcase/rock_moss_set_01/textures/rock_moss_set_01_nor_gl_1k.jpg",
                "assets/models/naturalistic_showcase/rock_moss_set_01/textures/rock_moss_set_01_rough_1k.jpg"};
        }
        if (id == "wild_rooibos_bush") {
            return {
                "assets/models/naturalistic_showcase/wild_rooibos_bush/textures/wild_rooibos_bush_diff_1k.jpg",
                "assets/models/naturalistic_showcase/wild_rooibos_bush/textures/wild_rooibos_bush_nor_gl_1k.jpg",
                "assets/models/naturalistic_showcase/wild_rooibos_bush/textures/wild_rooibos_bush_arm_1k.jpg"};
        }
        if (id == "Lantern_01") {
            return {
                "assets/models/naturalistic_showcase/Lantern_01/textures/Lantern_01_brass_diff_1k.jpg",
                "assets/models/naturalistic_showcase/Lantern_01/textures/Lantern_01_brass_nor_gl_1k.jpg",
                "assets/models/naturalistic_showcase/Lantern_01/textures/Lantern_01_brass_arm_1k.jpg"};
        }
        if (id == "WoodenTable_01") {
            return {
                "assets/models/naturalistic_showcase/WoodenTable_01/textures/WoodenTable_01_diff_1k.jpg",
                "assets/models/naturalistic_showcase/WoodenTable_01/textures/WoodenTable_01_nor_gl_1k.jpg",
                "assets/models/naturalistic_showcase/WoodenTable_01/textures/WoodenTable_01_arm_1k.jpg"};
        }
        if (id == "Barrel_01") {
            return {
                "assets/models/naturalistic_showcase/Barrel_01/textures/Barrel_01_explosive_diff_1k.jpg",
                "assets/models/naturalistic_showcase/Barrel_01/textures/Barrel_01_explosive_nor_gl_1k.jpg",
                "assets/models/naturalistic_showcase/Barrel_01/textures/Barrel_01_explosive_arm_1k.jpg"};
        }
        return {};
    }

    void ApplyNaturalisticAssetTextures(Scene::RenderableComponent& renderable, const char* assetId) {
        const AssetLedTextureSet textures = GetNaturalisticAssetTextureSet(assetId);
        if (!textures.albedo || !textures.normal || !textures.arm) {
            return;
        }

        renderable.textures.albedoPath = textures.albedo;
        renderable.textures.normalPath = textures.normal;
        renderable.textures.roughnessPath = textures.arm;
        renderable.textures.metallicPath = textures.arm;
        renderable.textures.occlusionPath = textures.arm;
    }

    bool UploadAssetLedMesh(Renderer* renderer,
                            const std::shared_ptr<Scene::MeshData>& mesh,
                            const char* label) {
        if (!renderer || !mesh) {
            return true;
        }
        auto res = renderer->UploadMesh(mesh);
        if (res.IsErr()) {
            spdlog::warn("Failed to upload asset-led {} mesh: {}", label, res.Error());
            return false;
        }
        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading asset-led {} mesh", label);
            return false;
        }
        return true;
    }

    entt::entity AddAssetLedRenderable(Scene::ECS_Registry& registry,
                                       const char* tag,
                                       const std::shared_ptr<Scene::MeshData>& mesh,
                                       const glm::vec3& position,
                                       const glm::vec3& scale,
                                       const glm::vec3& eulerRadians,
                                       const AssetLedMaterialSettings& material) {
        entt::entity e = registry.CreateEntity();
        registry.AddComponent<Scene::TagComponent>(e, tag);
        auto& t = registry.AddComponent<TransformComponent>(e);
        t.position = position;
        t.scale = scale;
        t.rotation = glm::quat(eulerRadians);

        auto& r = registry.AddComponent<Scene::RenderableComponent>(e);
        r.mesh = mesh;
        r.albedoColor = material.color;
        r.metallic = material.metallic;
        r.roughness = material.roughness;
        r.ao = 1.0f;
        r.transmissionFactor = material.transmission;
        r.ior = material.ior;
        r.emissiveColor = material.emissive;
        r.emissiveStrength = material.emissiveStrength;
        r.emissiveBloomFactor = glm::length(material.emissive) > 0.0f ? 0.55f : 0.0f;
        r.wetnessFactor = material.wetness;
        r.proceduralMaskStrength = material.proceduralMask;
        r.doubleSided = material.doubleSided;
        r.alphaMode = material.alphaMode;
        r.renderLayer = material.layer;
        r.presetName = material.preset;
        if (!material.albedoTexture.empty()) {
            r.textures.albedoPath = material.albedoTexture;
        }
        if (!material.normalTexture.empty()) {
            r.textures.normalPath = material.normalTexture;
        }
        if (!material.roughnessTexture.empty()) {
            r.textures.roughnessPath = material.roughnessTexture;
        }
        if (!material.metallicTexture.empty()) {
            r.textures.metallicPath = material.metallicTexture;
        }
        if (!material.occlusionTexture.empty()) {
            r.textures.occlusionPath = material.occlusionTexture;
        }
        return e;
    }

    entt::entity AddAssetLedNaturalisticRenderable(Scene::ECS_Registry& registry,
                                                   const char* tag,
                                                   const char* assetId,
                                                   const std::shared_ptr<Scene::MeshData>& mesh,
                                                   const glm::vec3& position,
                                                   const glm::vec3& scale,
                                                   const glm::vec3& eulerRadians,
                                                   const AssetLedMaterialSettings& material) {
        entt::entity entity = AddAssetLedRenderable(registry, tag, mesh, position, scale, eulerRadians, material);
        if (registry.HasComponent<Scene::RenderableComponent>(entity)) {
            auto& renderable = registry.GetComponent<Scene::RenderableComponent>(entity);
            ApplyNaturalisticAssetTextures(renderable, assetId);
        }
        return entity;
    }

    entt::entity AddAssetLedCamera(Scene::ECS_Registry& registry,
                                   const glm::vec3& position,
                                   const glm::vec3& target,
                                   float fov,
                                   float farPlane) {
        entt::entity camEntity = registry.CreateEntity();
        registry.AddComponent<Scene::TagComponent>(camEntity, "MainCamera");
        auto& t = registry.AddComponent<TransformComponent>(camEntity);
        t.position = position;
        t.rotation = glm::quatLookAtLH(glm::normalize(target - position), glm::vec3(0.0f, 1.0f, 0.0f));

        auto& cam = registry.AddComponent<Scene::CameraComponent>(camEntity);
        cam.fov = fov;
        ConfigureShowcaseCameraClip(cam, farPlane);
        cam.isActive = true;
        return camEntity;
    }

    bool IsModelAuthoredEnclosedInterior(const std::string& sceneFamily) {
        return sceneFamily == "home_kitchen_lantern" ||
               sceneFamily == "home_office_evening" ||
               sceneFamily == "school_classroom_day" ||
               sceneFamily == "stadium_night_match" ||
               sceneFamily == "basketball_gym_day" ||
               sceneFamily == "red_light_room" ||
               sceneFamily == "neon_streamer_concert";
    }

    void ApplyModelAuthoredLighting(Renderer* renderer, const std::string& sceneFamily) {
        if (!renderer) {
            return;
        }

        const bool enclosedInterior = IsModelAuthoredEnclosedInterior(sceneFamily);
        auto profile = Graphics::BuildSceneLocalCinematicProfile(sceneFamily);
        profile.lighting.source = "model_authored_seed";
        Graphics::ApplySceneCinematicProfile(*renderer, profile);

        if (enclosedInterior) {
            // Enclosed model-authored rooms must not inherit a distinctive HDRI.
            // Even with IBL disabled, keeping a named sky/city environment active
            // makes reflection regressions visually ambiguous and can leak through
            // fallback shader paths. Use a neutral procedural descriptor and rely
            // on authored sun/local scene lights instead.
            renderer->SetBackgroundPresentation(false, 0.0f, 1.0f);
            renderer->SetEnvironmentPreset("neutral_procedural");
            renderer->SetIBLEnabled(false);
            renderer->SetIBLIntensity(0.0f, 0.0f);
        }
    }

    std::string RecipeVisualFamily(const std::string& recipe) {
        return recipe == "garden" ? "recipe_garden" : "recipe_enclosed_room";
    }

    std::string RecipeMaterialPalette(const std::string& recipe) {
        if (recipe == "garden") {
            return "recipe_garden_patio";
        }
        if (recipe == "kitchen") {
            return "recipe_kitchen_enclosed";
        }
        if (recipe == "office") {
            return "recipe_office_enclosed";
        }
        if (recipe == "bathroom") {
            return "recipe_bathroom_enclosed";
        }
        if (recipe == "bedroom") {
            return "recipe_bedroom_enclosed";
        }
        return "recipe_living_room_enclosed";
    }

    struct RecipeLightingBalance {
        const char* policyId = "recipe_scene_lighting_balance_v1";
        float sunScale = 0.86f;
        float ambientScale = 0.90f;
        float localFixtureScale = 0.88f;
        float localProbeDiffuseScale = 0.88f;
        float localProbeSpecularScale = 0.92f;
        float exposureScale = 0.92f;
        float ssaoScale = 1.18f;
    };

    RecipeLightingBalance RecipeLightingBalanceFor(const std::string& recipe) {
        RecipeLightingBalance balance{};
        if (recipe == "garden") {
            balance.policyId = "recipe_garden_lighting_balance_v1";
            balance.sunScale = 0.95f;
            balance.ambientScale = 0.98f;
            balance.localFixtureScale = 0.92f;
            balance.localProbeDiffuseScale = 0.92f;
            balance.localProbeSpecularScale = 0.96f;
            balance.exposureScale = 0.96f;
            balance.ssaoScale = 1.08f;
        } else if (recipe == "kitchen") {
            balance.policyId = "recipe_kitchen_lighting_balance_v1";
            balance.sunScale = 0.88f;
            balance.ambientScale = 0.88f;
            balance.localFixtureScale = 0.84f;
            balance.localProbeDiffuseScale = 0.86f;
            balance.localProbeSpecularScale = 0.92f;
            balance.exposureScale = 0.90f;
            balance.ssaoScale = 1.25f;
        } else if (recipe == "office") {
            balance.policyId = "recipe_office_lighting_balance_v1";
            balance.sunScale = 0.84f;
            balance.ambientScale = 0.88f;
            balance.localFixtureScale = 0.86f;
            balance.localProbeDiffuseScale = 0.86f;
            balance.localProbeSpecularScale = 0.90f;
            balance.exposureScale = 0.90f;
            balance.ssaoScale = 1.24f;
        } else if (recipe == "bathroom") {
            balance.policyId = "recipe_bathroom_lighting_balance_v1";
            balance.sunScale = 0.90f;
            balance.ambientScale = 0.92f;
            balance.localFixtureScale = 0.86f;
            balance.localProbeDiffuseScale = 0.88f;
            balance.localProbeSpecularScale = 0.92f;
            balance.exposureScale = 0.92f;
            balance.ssaoScale = 1.20f;
        }
        return balance;
    }

    struct RecipeMoodGrade {
        float warm = 0.0f;
        float cool = 0.0f;
        float contrast = 1.06f;
        float saturation = 1.04f;
        float vignette = 0.075f;
        float exposureOffset = 0.0f;
    };

    RecipeMoodGrade RecipeMoodGradeFor(const LLM::SceneStyle& style, bool outdoor) {
        const float warmIntent = std::max(0.0f, style.warmth);
        const float coolIntent = std::max(0.0f, -style.warmth);
        const float brightIntent = std::max(0.0f, style.brightness);
        const float moodyIntent = std::max(0.0f, -style.brightness);
        const bool rustic = style.name == "rustic";
        const bool modern = style.name == "modern";

        RecipeMoodGrade grade{};
        grade.warm = warmIntent * 0.40f + (rustic ? 0.035f : 0.0f);
        grade.cool = coolIntent * 0.36f + (modern ? 0.035f : 0.0f) + moodyIntent * 0.055f;

        const float baseContrast = outdoor ? 1.03f : 1.055f;
        const float baseSaturation = outdoor ? 1.02f : 1.035f;
        grade.contrast = baseContrast + coolIntent * 0.020f + moodyIntent * 0.038f + brightIntent * 0.010f;
        grade.saturation = baseSaturation + warmIntent * 0.045f + (rustic ? 0.012f : 0.0f) -
                           coolIntent * 0.010f - brightIntent * 0.006f;
        grade.vignette = (outdoor ? 0.045f : 0.072f) + warmIntent * 0.012f + moodyIntent * 0.030f -
                         brightIntent * 0.020f - (modern ? 0.008f : 0.0f);
        grade.exposureOffset = brightIntent * 0.020f - moodyIntent * 0.030f;

        grade.warm = std::clamp(grade.warm, 0.0f, 0.34f);
        grade.cool = std::clamp(grade.cool, 0.0f, 0.30f);
        grade.contrast = std::clamp(grade.contrast, outdoor ? 1.02f : 1.04f, outdoor ? 1.08f : 1.12f);
        grade.saturation = std::clamp(grade.saturation, outdoor ? 1.00f : 1.01f, outdoor ? 1.06f : 1.09f);
        grade.vignette = std::clamp(grade.vignette, outdoor ? 0.025f : 0.045f, outdoor ? 0.075f : 0.115f);
        return grade;
    }

    void ApplyRecipeVisualContract(Renderer* renderer, const std::string& recipe) {
        if (!renderer) {
            return;
        }

        const bool outdoor = recipe == "garden";
        const std::string palette = RecipeMaterialPalette(recipe);
        const RecipeLightingBalance balance = RecipeLightingBalanceFor(recipe);

        Graphics::FrameContract::SceneVisualInfo visualContract{};
        visualContract.active = true;
        visualContract.profileId = outdoor ? "recipe_garden_rolloff_v1" : "recipe_room_rolloff_v1";
        visualContract.family = RecipeVisualFamily(recipe);
        visualContract.source = "recipe_scene_visual_contract";
        visualContract.enclosedScene = !outdoor;
        visualContract.visibleExternalHDRIAllowed = true;
        visualContract.externalHDRIVisible = false;
        visualContract.environmentOwner = outdoor ? "recipe_neutral_procedural"
                                                  : "recipe_window_daylight_procedural";
        visualContract.reflectionOwner = outdoor ? "recipe_garden_local_probe" : "recipe_room_local_probe";
        visualContract.localReflectionProbeRigId = outdoor ? "recipe_garden_probe" : "recipe_room_probe";
        visualContract.lightRigId = outdoor ? "recipe_garden_daylight" : "recipe_enclosed_room_motivated";
        visualContract.shadowPolicyId = "scene_local_soft_stable_shadows_v1";
        visualContract.exposurePolicyId = "scene_local_manual_exposure_v1";
        visualContract.materialPaletteId = palette;
        visualContract.lightingScriptId = outdoor ? "recipe_garden_daylight" : "recipe_enclosed_practicals";
        visualContract.materialClassSetId = "scene_local_named_material_classes_v1";
        visualContract.materialLayerSetId = "scene_local_cinematic_material_layers_v1";
        visualContract.temporalPolicyId = "stable_recipe_reprojection";
        visualContract.postPolicyId = outdoor ? "recipe_garden_highlight_rolloff" : "recipe_room_highlight_rolloff";
        visualContract.postQualitySetId = "scene_local_cinematic_post_quality_v1";
        visualContract.toneMapperPreset = "filmic_soft";
        visualContract.lightingBalancePolicyId = balance.policyId;
        visualContract.lightingBalancePolicyActive = true;
        visualContract.lightingBalanceSunScale = balance.sunScale;
        visualContract.lightingBalanceAmbientScale = balance.ambientScale;
        visualContract.lightingBalanceLocalFixtureScale = balance.localFixtureScale;
        visualContract.lightingBalanceLocalProbeDiffuseScale = balance.localProbeDiffuseScale;
        visualContract.lightingBalanceLocalProbeSpecularScale = balance.localProbeSpecularScale;
        visualContract.lightingBalanceExposureScale = balance.exposureScale;
        visualContract.lightingBalanceSSAOScale = balance.ssaoScale;
        renderer->SetSceneVisualContract(std::move(visualContract));
        renderer->SetToneMapperPreset("filmic_soft");
        renderer->SetLightingRigContract(outdoor ? "recipe_garden_daylight" : "recipe_enclosed_room_motivated",
                                         "recipe_scene_visual_contract",
                                         false);
        renderer->SetWorldShaderPaletteContract(palette,
                                                outdoor ? "recipe_garden_daylight" : "recipe_enclosed_practicals");
    }

    void AddAssetLedPointLight(Scene::ECS_Registry& registry,
                               const char* tag,
                               const glm::vec3& position,
                               const glm::vec3& color,
                               float intensity,
                               float range) {
        entt::entity e = registry.CreateEntity();
        registry.AddComponent<Scene::TagComponent>(e, tag);
        auto& t = registry.AddComponent<TransformComponent>(e);
        t.position = position;

        auto& l = registry.AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Point;
        l.color = color;
        l.intensity = intensity;
        l.range = range;
        l.castsShadows = false;
    }

    void AddAssetLedSpotLight(Scene::ECS_Registry& registry,
                              const char* tag,
                              const glm::vec3& position,
                              const glm::vec3& target,
                              const glm::vec3& color,
                              float intensity,
                              float range,
                              bool castsShadows) {
        entt::entity e = registry.CreateEntity();
        registry.AddComponent<Scene::TagComponent>(e, tag);
        auto& t = registry.AddComponent<TransformComponent>(e);
        t.position = position;
        t.rotation = glm::quatLookAtLH(glm::normalize(target - position), glm::vec3(0.0f, 1.0f, 0.0f));

        auto& l = registry.AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Spot;
        l.color = color;
        l.intensity = intensity;
        l.range = range;
        l.innerConeDegrees = 26.0f;
        l.outerConeDegrees = 48.0f;
        l.castsShadows = castsShadows;
    }

    size_t AddSceneProfileReflectionProbes(Scene::ECS_Registry& registry,
                                           const Graphics::SceneCinematicProfile& profile) {
        if (!profile.reflections.localProbeEnabled) {
            return 0;
        }

        size_t builtProbes = 0;
        for (const auto& probeSpec : profile.reflectionProbes) {
            if (!probeSpec.enabled) {
                continue;
            }

            entt::entity entity = registry.CreateEntity();
            registry.AddComponent<Scene::TagComponent>(entity, probeSpec.id);
            auto& transform = registry.AddComponent<TransformComponent>(entity);
            transform.position = probeSpec.center;

            Scene::ReflectionProbeComponent probe{};
            probe.extents = glm::max(glm::vec3(0.01f), probeSpec.extents);
            probe.blendDistance = std::max(0.0f, probeSpec.blendDistance);
            probe.environmentIndex = probeSpec.environmentIndex;
            probe.enabled = 1;
            registry.AddComponent<Scene::ReflectionProbeComponent>(entity, probe);
            ++builtProbes;
        }
        return builtProbes;
    }

    size_t AddSceneProfileLights(Scene::ECS_Registry& registry,
                                 const Graphics::SceneCinematicProfile& profile,
                                 const glm::vec3& cameraTarget) {
        auto semanticClassId = [](const std::string& semanticClass) -> uint32_t {
            if (semanticClass.find("softbox") != std::string::npos ||
                semanticClass.find("window") != std::string::npos ||
                semanticClass.find("ceiling_panel") != std::string::npos ||
                semanticClass.find("high_bay") != std::string::npos ||
                semanticClass.find("flood_bank") != std::string::npos) {
                return 1u;
            }
            if (semanticClass.find("neon") != std::string::npos ||
                semanticClass.find("strip") != std::string::npos ||
                semanticClass.find("screen_panel") != std::string::npos) {
                return 2u;
            }
            if (semanticClass.find("stage") != std::string::npos ||
                semanticClass.find("spot") != std::string::npos ||
                semanticClass.find("rim") != std::string::npos ||
                semanticClass.find("wash") != std::string::npos) {
                return 3u;
            }
            if (semanticClass.find("practical") != std::string::npos ||
                semanticClass.find("candle") != std::string::npos ||
                semanticClass.find("lamp") != std::string::npos) {
                return 4u;
            }
            return 0u;
        };

        auto lightLookRotation = [](const glm::vec3& forwardInput) {
            glm::vec3 forward = glm::dot(forwardInput, forwardInput) > 1e-6f
                ? glm::normalize(forwardInput)
                : glm::vec3(0.0f, 0.0f, 1.0f);
            glm::vec3 up(0.0f, 1.0f, 0.0f);
            if (std::abs(glm::dot(forward, up)) > 0.98f) {
                up = glm::vec3(0.0f, 0.0f, 1.0f);
            }
            return glm::quatLookAtLH(forward, up);
        };

        size_t builtLights = 0;
        for (const auto& lightSpec : profile.lightFixtures) {
            if (!lightSpec.enabled) {
                continue;
            }

            entt::entity entity = registry.CreateEntity();
            registry.AddComponent<Scene::TagComponent>(entity, lightSpec.id);

            auto& transform = registry.AddComponent<TransformComponent>(entity);
            transform.position = lightSpec.position;

            auto& light = registry.AddComponent<Scene::LightComponent>(entity);
            if (lightSpec.type == "spot" || lightSpec.type == "area_rect") {
                const glm::vec3 toTarget = lightSpec.target - lightSpec.position;
                const glm::vec3 fallbackTarget = cameraTarget - lightSpec.position;
                const glm::vec3 forward = glm::dot(toTarget, toTarget) > 1e-6f
                    ? glm::normalize(toTarget)
                    : (glm::dot(fallbackTarget, fallbackTarget) > 1e-6f
                        ? glm::normalize(fallbackTarget)
                        : glm::vec3(0.0f, -1.0f, 0.0f));
                transform.rotation = lightLookRotation(forward);
                if (lightSpec.type == "area_rect") {
                    light.type = Scene::LightType::AreaRect;
                    light.areaSize = glm::max(lightSpec.areaSize, glm::vec2(0.01f));
                    light.twoSided = lightSpec.twoSided;
                } else {
                    light.type = Scene::LightType::Spot;
                    light.innerConeDegrees = lightSpec.innerConeDegrees;
                    light.outerConeDegrees = lightSpec.outerConeDegrees;
                }
            } else {
                light.type = Scene::LightType::Point;
            }
            light.color = lightSpec.color;
            light.intensity = lightSpec.intensity;
            light.range = lightSpec.range;
            light.castsShadows = lightSpec.castsShadows;
            light.semanticClassId = semanticClassId(lightSpec.semanticClass);
            ++builtLights;
        }

        if (builtLights == 0) {
            AddAssetLedPointLight(registry,
                                  "ModelAuthored_WarmAccent",
                                  cameraTarget + glm::vec3(-0.55f, 0.40f, -0.10f),
                                  glm::vec3(1.0f, 0.46f, 0.18f),
                                  (profile.family == "neon_alley_material_market" ? 18.0f : 9.0f) *
                                      profile.lightingBalance.localFixtureScale,
                                  4.0f);
            builtLights = 1;
        }
        return builtLights;
    }

    size_t AddModelAuthoredSeedLights(Scene::ECS_Registry& registry,
                                      const nlohmann::json& root,
                                      float lightingBalanceScale = 1.0f) {
        size_t builtLights = 0;
        const auto lightsIt = root.find("lights");
        if (lightsIt == root.end() || !lightsIt->is_array()) {
            return 0;
        }

        for (const auto& light : *lightsIt) {
            if (!light.is_object()) {
                continue;
            }

            const std::string id = light.value("id", std::string("seed_light"));
            const std::string type = ToLowerAscii(light.value("type", std::string("point")));
            const glm::vec3 position = ReadJsonVec3Or(light.value("position", nlohmann::json::array()), glm::vec3(0.0f, 1.5f, 0.0f));
            const glm::vec3 color = ReadJsonVec3Or(light.value("color", nlohmann::json::array()), glm::vec3(1.0f, 0.85f, 0.65f));
            const float intensity =
                glm::clamp(light.value("intensity", 4.0f), 0.0f, 60.0f) *
                glm::clamp(lightingBalanceScale, 0.0f, 2.0f);
            const float range = glm::clamp(light.value("range", 4.0f), 0.1f, 24.0f);
            const bool castsShadows = light.value("casts_shadows", false);
            const std::string tag = std::string("ModelAuthoredSeedLight_") + id;

            entt::entity entity = registry.CreateEntity();
            registry.AddComponent<Scene::TagComponent>(entity, tag);
            auto& transform = registry.AddComponent<TransformComponent>(entity);
            transform.position = position;

            auto& component = registry.AddComponent<Scene::LightComponent>(entity);
            component.color = color;
            component.intensity = intensity;
            component.range = range;
            component.castsShadows = castsShadows;

            if (type == "spot") {
                const glm::vec3 target = ReadJsonVec3Or(light.value("target", nlohmann::json::array()), position + glm::vec3(0.0f, -1.0f, 0.0f));
                glm::vec3 direction = target - position;
                if (glm::length(direction) < 0.0001f) {
                    direction = glm::vec3(0.0f, -1.0f, 0.0f);
                }
                transform.rotation = glm::quatLookAtLH(glm::normalize(direction), glm::vec3(0.0f, 1.0f, 0.0f));
                component.type = Scene::LightType::Spot;
                component.innerConeDegrees = glm::clamp(light.value("inner_cone_degrees", 24.0f), 1.0f, 80.0f);
                component.outerConeDegrees = glm::clamp(light.value("outer_cone_degrees", 48.0f), component.innerConeDegrees + 1.0f, 120.0f);
            } else {
                component.type = Scene::LightType::Point;
            }

            ++builtLights;
        }

        return builtLights;
    }
}

void Engine::RebuildScene(ScenePreset preset) {
    // CRITICAL: Full GPU synchronization before destroying resources.
    // WaitForGPU flushes all command queues (main, upload, compute) and waits
    // for their completion. This is more thorough than WaitForAllFrames which
    // only waits for existing fence values.
    if (m_renderer) {
        m_renderer->WaitForGPU();

        // Reset the command list to clear CPU-side references to resources.
        // This closes the current recording, resets the allocator and command list
        // so they no longer hold references to objects we're about to delete.
        m_renderer->ResetCommandList();

        // CRITICAL: Clear BLAS cache AFTER ResetCommandList() completes.
        // At this point, the command list and allocators have been reset, so no
        // GPU operations reference the BLAS resources anymore. Clearing the cache
        // now prevents #921 OBJECT_DELETED_WHILE_STILL_IN_USE when RT is enabled.
        m_renderer->ClearBLASCache();
    }

    // Exit play mode if active before rebuilding
    if (m_playModeActive) {
        ExitPlayMode();
    }

    // Disable terrain system (will be re-enabled if switching to terrain scene)
    m_terrainEnabled = false;
    m_loadedChunks.clear();

    // Clear EditorWorld's chunk tracking BEFORE clearing the registry.
    // This prevents EditorWorld from trying to access destroyed entities.
    if (m_editorModeController && m_editorModeController->GetWorld()) {
        m_editorModeController->GetWorld()->ClearAllChunks();
    }

    // Clear all existing entities/components. This destroys RenderableComponents
    // which may release GPU resources (mesh buffers, etc.).
    m_registry->GetRegistry().clear();

    // CRITICAL: After clearing the registry, force another full GPU sync to ensure
    // all destructor-triggered resource releases have completed. This prevents
    // D3D12 validation error 921 (OBJECT_DELETED_WHILE_STILL_IN_USE) when rapidly
    // rebuilding scenes with many mesh uploads (e.g., terrain chunks).
    if (m_renderer) {
        m_renderer->WaitForGPU();
    }
    m_activeCameraEntity = entt::null;
    m_selectedEntity = entt::null;
    m_autoDemoEnabled = false;
    m_cameraControllerInitialized = false;

    m_currentScenePreset = preset;

    // Reset renderer temporal history so the new scene starts from a clean
    // state (no TAA or RT afterimages from the previous layout).
    if (m_renderer) {
        m_renderer->ResetTemporalHistoryForSceneChange();
    }

    switch (preset) {
    case ScenePreset::CornellBox:
        BuildCornellScene();
        break;
    case ScenePreset::DragonOverWater:
        BuildDragonStudioScene();
        break;
    case ScenePreset::ProceduralTerrain:
        // Skip old terrain system if Engine Editor Mode is active
        // (EditorWorld handles terrain generation with its own chunk system)
        if (!m_engineEditorMode) {
            BuildProceduralTerrainScene();
        } else {
            // In editor mode, EditorWorld handles terrain chunks but we still need
            // a camera and sun for the scene to work properly
            BuildEditorModeTerrainScene();
        }
        break;
    case ScenePreset::RTShowcase:
    case ScenePreset::IBLGallery:
    case ScenePreset::GodRays: // currently shares layout with RTShowcase
    default:
        BuildRTShowcaseScene();
        break;
    case ScenePreset::MaterialLab:
        BuildMaterialLabScene();
        break;
    case ScenePreset::GlassWaterCourtyard:
        BuildGlassWaterCourtyardScene();
        break;
    case ScenePreset::OutdoorSunsetBeach:
        BuildOutdoorSunsetBeachScene();
        break;
    case ScenePreset::RecipeRoom:
        BuildRecipeScene();
        break;
    case ScenePreset::LiquidGallery:
        BuildLiquidGalleryScene();
        break;
    case ScenePreset::CoastalCliffFoundry:
        BuildCoastalCliffFoundryScene();
        break;
    case ScenePreset::RainGlassPavilion:
        BuildRainGlassPavilionScene();
        break;
    case ScenePreset::DesertRelicGallery:
        BuildDesertRelicGalleryScene();
        break;
    case ScenePreset::NeonAlleyMaterialMarket:
        BuildNeonAlleyMaterialMarketScene();
        break;
    case ScenePreset::ForestCreekShrine:
        BuildForestCreekShrineScene();
        break;
    case ScenePreset::ModelAuthoredScene:
        BuildModelAuthoredScene();
        break;
    case ScenePreset::EffectsShowcase:
        BuildEffectsShowcaseScene();
        break;
    case ScenePreset::TemporalValidation:
        BuildTemporalValidationScene();
        break;
    }

    InitializeCameraController();

    // Refresh LLM scene view so natural-language commands operate on the new layout.
    if (m_commandQueue) {
        m_commandQueue->RefreshLookup(m_registry.get());
    }

    const char* presetName = "Unknown";
    switch (preset) {
    case ScenePreset::CornellBox:        presetName = "Cornell Box"; break;
    case ScenePreset::DragonOverWater:   presetName = "Dragon Over Water Studio"; break;
    case ScenePreset::RTShowcase:        presetName = "RT Showcase Gallery"; break;
    case ScenePreset::IBLGallery:        presetName = "IBL Gallery"; break;
    case ScenePreset::MaterialLab:       presetName = "Material Lab"; break;
    case ScenePreset::GlassWaterCourtyard:presetName = "Glass and Water Courtyard"; break;
    case ScenePreset::OutdoorSunsetBeach:presetName = "Outdoor Sunset Beach"; break;
    case ScenePreset::RecipeRoom:        presetName = "Procedural Recipe"; break;
    case ScenePreset::LiquidGallery:     presetName = "Liquid Gallery"; break;
    case ScenePreset::CoastalCliffFoundry:presetName = "Coastal Cliff Foundry"; break;
    case ScenePreset::RainGlassPavilion: presetName = "Rain Glass Pavilion"; break;
    case ScenePreset::DesertRelicGallery:presetName = "Desert Relic Gallery"; break;
    case ScenePreset::NeonAlleyMaterialMarket:presetName = "Neon Alley Material Market"; break;
    case ScenePreset::ForestCreekShrine: presetName = "Forest Creek Shrine"; break;
    case ScenePreset::ModelAuthoredScene: presetName = "Model Authored Scene"; break;
    case ScenePreset::EffectsShowcase:   presetName = "Effects Showcase"; break;
    case ScenePreset::GodRays:           presetName = "God Rays Atrium"; break;
    case ScenePreset::TemporalValidation:presetName = "Temporal Validation Lab"; break;
    case ScenePreset::ProceduralTerrain: presetName = "Procedural Terrain"; break;
    default:                             presetName = "Unknown"; break;
    }

    spdlog::info("Scene rebuilt as {}", presetName);
    spdlog::info("{}", m_registry->DescribeScene());

    // One-shot asset memory summary to highlight the heaviest categories and
    // assets in the new scene. This complements the frame-level VRAM estimate
    // and helps diagnose oversize textures or geometry.
    if (m_renderer) {
        auto breakdown = m_renderer->GetAssetMemoryBreakdown();
        const double texMB  = static_cast<double>(breakdown.textureBytes) / (1024.0 * 1024.0);
        const double envMB  = static_cast<double>(breakdown.environmentBytes) / (1024.0 * 1024.0);
        const double geomMB = static_cast<double>(breakdown.geometryBytes) / (1024.0 * 1024.0);
        const double rtMB   = static_cast<double>(breakdown.rtStructureBytes) / (1024.0 * 1024.0);
        spdlog::info("Asset memory breakdown after rebuild: tex~{:.0f} MB env~{:.0f} MB geom~{:.0f} MB RT~{:.0f} MB",
                     texMB, envMB, geomMB, rtMB);

        auto heavyTex = m_renderer->GetAssetRegistry().GetHeaviestTextures(3);
        if (!heavyTex.empty()) {
            spdlog::info("Top textures by estimated GPU bytes:");
            for (const auto& t : heavyTex) {
                const double mb = static_cast<double>(t.bytes) / (1024.0 * 1024.0);
                spdlog::info("  {} ~ {:.1f} MB", t.key, mb);
            }
        }
        auto heavyMesh = m_renderer->GetAssetRegistry().GetHeaviestMeshes(3);
        if (!heavyMesh.empty()) {
            spdlog::info("Top meshes by estimated GPU bytes:");
            for (const auto& m : heavyMesh) {
                const double mb = static_cast<double>(m.bytes) / (1024.0 * 1024.0);
                spdlog::info("  {} ~ {:.1f} MB", m.key, mb);
            }
        }
    }

    // Rebuild asset ref-counts from the new ECS graph and prune any meshes
    // that are no longer referenced so BLAS/geometry memory does not
    // accumulate across scene changes. Then prune unused textures from the
    // registry so diagnostics do not track stale entries.
    if (m_renderer) {
        // Mark the voxel volume as dirty so the next voxel render pass
        // rebuilds it from the new ECS layout instead of reusing geometry
        // from the previous scene.
        m_renderer->MarkVoxelGridDirty();
        m_renderer->RebuildAssetRefsFromScene(m_registry.get());

        // CRITICAL: Wait for ALL in-flight frames before pruning old assets.
        // This prevents OBJECT_DELETED_WHILE_STILL_IN_USE error #921 during scene switches.
        m_renderer->WaitForAllFrames();

        m_renderer->PruneUnusedMeshes(m_registry.get());
        m_renderer->PruneUnusedTextures();
    }

    // Apply VRAM-aware quality clamping after large scene rebuilds so that
    // heavy layouts automatically fall back to safe presets when the
    // estimated GPU memory footprint is close to the adapter limit.
    // DISABLED: Keep all graphics features enabled
    // ApplyVRAMQualityGovernor();
}

void Engine::BuildCornellScene() {
    spdlog::info("Building hero scene: Cornell Box with mirror");

    auto* renderer = m_renderer.get();

    // Camera starting inside the box near the front wall, looking toward the
    // center so all mirrored surfaces are visible.
    entt::entity cameraEntity = m_registry->CreateEntity();
    m_registry->AddComponent<Scene::TagComponent>(cameraEntity, "MainCamera");

    auto& cameraTransform = m_registry->AddComponent<TransformComponent>(cameraEntity);
    cameraTransform.position = glm::vec3(0.0f, 1.6f, -3.0f);
    {
        glm::vec3 target(0.0f, 1.2f, 0.0f);
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        glm::vec3 forward = glm::normalize(target - cameraTransform.position);
        cameraTransform.rotation = glm::quatLookAtLH(forward, up);
    }

    auto& camera = m_registry->AddComponent<Scene::CameraComponent>(cameraEntity);
    camera.fov = 50.0f;
    ConfigureShowcaseCameraClip(camera, 80.0f);
    camera.isActive = true;

    if (renderer) {
        Graphics::ApplyCornellSceneControls(*renderer);
    }

    // Shared plane meshes
    auto floorMesh = Utils::MeshGenerator::CreatePlane(2.0f * kCornellHalfExtent,
                                                       2.0f * kCornellHalfExtent);
    auto wallMesh = Utils::MeshGenerator::CreatePlane(2.0f * kCornellHalfExtent,
                                                      kCornellHeight);
    if (renderer) {
        auto floorResult = renderer->UploadMesh(floorMesh);
        if (floorResult.IsErr()) {
            spdlog::warn("Failed to upload Cornell floor mesh: {}", floorResult.Error());
            floorMesh.reset();
        }
        auto wallResult = renderer->UploadMesh(wallMesh);
        if (wallResult.IsErr()) {
            spdlog::warn("Failed to upload Cornell wall mesh: {}", wallResult.Error());
            wallMesh.reset();
        }

        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while building Cornell scene; aborting geometry creation for this run.");
            return;
        }
    }

    if (!floorMesh || !floorMesh->gpuBuffers || !wallMesh || !wallMesh->gpuBuffers) {
        spdlog::warn("Cornell scene meshes are not available; skipping Cornell box geometry.");
        return;
    }

    // Floor
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_Floor");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(0.0f, 0.0f, 0.0f);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = floorMesh;
        r.albedoColor = glm::vec4(0.92f, 0.92f, 0.96f, 1.0f);
        r.metallic = 1.0f;
        r.roughness = 0.03f;
        r.ao = 1.0f;
        r.presetName = "cornell_floor";
        r.doubleSided = true;
        // Reuse the RT showcase wood floor textures so the Cornell floor
        // participates in the same BC7/BC5 material pipeline.
        r.textures.albedoPath = "assets/textures/rtshowcase/rt_gallery_floor_albedo.dds";
        r.textures.normalPath = "assets/textures/rtshowcase/rt_gallery_floor_normal_bc5.dds";
    }

    // Ceiling
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_Ceiling");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(0.0f, kCornellHeight, 0.0f);
        t.rotation = glm::quat(glm::vec3(glm::pi<float>(), 0.0f, 0.0f));

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = floorMesh;
        r.albedoColor = glm::vec4(0.9f, 0.9f, 0.95f, 1.0f);
        r.metallic = 1.0f;
        r.roughness = 0.035f;
        r.ao = 1.0f;
        r.presetName = "cornell_ceiling";
        r.doubleSided = true;
        r.textures.albedoPath = "assets/textures/rtshowcase/rt_gallery_rightwall_albedo.dds";
        r.textures.normalPath = "assets/textures/rtshowcase/rt_gallery_rightwall_normal_bc5.dds";
    }

    // Back wall
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_BackWall");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(0.0f, kCornellHeight * 0.5f, kCornellHalfExtent);
        t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), 0.0f, 0.0f));

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = wallMesh;
        r.albedoColor = glm::vec4(0.9f, 0.9f, 0.93f, 1.0f);
        r.metallic = 1.0f;
        r.roughness = 0.03f;
        r.ao = 1.0f;
        r.presetName = "cornell_back";
        r.doubleSided = true;
        r.textures.albedoPath = "assets/textures/rtshowcase/rt_gallery_rightwall_albedo.dds";
        r.textures.normalPath = "assets/textures/rtshowcase/rt_gallery_rightwall_normal_bc5.dds";
    }

    // Left wall (green)
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_LeftWall");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(-kCornellHalfExtent, kCornellHeight * 0.5f, 0.0f);
        t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), glm::half_pi<float>(), 0.0f));

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = wallMesh;
        r.albedoColor = glm::vec4(0.3f, 0.9f, 0.3f, 1.0f);
        r.metallic = 1.0f;
        r.roughness = 0.035f;
        r.ao = 1.0f;
        r.presetName = "cornell_green";
        r.doubleSided = true;
        // No albedo texture - use pure base color for classic Cornell Box look
    }

    // Right wall (red)
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_RightWall");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(kCornellHalfExtent, kCornellHeight * 0.5f, 0.0f);
        t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), -glm::half_pi<float>(), 0.0f));

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = wallMesh;
        r.albedoColor = glm::vec4(0.9f, 0.25f, 0.25f, 1.0f);
        r.metallic = 1.0f;
        r.roughness = 0.035f;
        r.ao = 1.0f;
        r.presetName = "cornell_red";
        r.doubleSided = true;
        // No albedo texture - use pure base color for classic Cornell Box look
    }

    // Front wall (mirror) closing the box toward -Z so that the interior is
    // fully enclosed and mirror reflections can bounce between back and front.
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_FrontWall");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(0.0f, kCornellHeight * 0.5f, -kCornellHalfExtent);
        t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), glm::pi<float>(), 0.0f));

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = wallMesh;
        r.albedoColor = glm::vec4(0.95f, 0.95f, 0.98f, 1.0f);
        r.metallic = 1.0f;
        r.roughness = 0.025f;
        r.ao = 1.0f;
        r.presetName = "cornell_front_mirror";
        r.doubleSided = true;
    }

    // Mirror panel on the back wall.
    auto mirrorMesh = Utils::MeshGenerator::CreatePlane(1.5f, 1.5f);
    if (renderer) {
        auto mirrorResult = renderer->UploadMesh(mirrorMesh);
        if (mirrorResult.IsErr()) {
            spdlog::warn("Failed to upload Cornell mirror mesh: {}", mirrorResult.Error());
            mirrorMesh.reset();
        }
        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading Cornell mirror mesh; skipping remaining Cornell geometry.");
            return;
        }
    }
    if (mirrorMesh && mirrorMesh->gpuBuffers) {
        // Primary mirror on the back wall.
        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_Mirror");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(0.0f, 1.0f, kCornellHalfExtent - 0.01f);
            t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), 0.0f, 0.0f));

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = mirrorMesh;
            r.albedoColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            r.metallic = 1.0f;
            r.roughness = 0.02f;
            r.ao = 1.0f;
            r.presetName = "mirror";
            r.doubleSided = true;
        }

        // Interior mirror panel facing the back-wall mirror to create a simple
        // "infinity mirror" effect when reflections are enabled. This is placed
        // slightly in front of the back wall so repeated bounces between the
        // two mirrors create a tunnel-like illusion in RT/SSR.
        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_InfinityPanel");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(0.0f, 1.0f, 0.0f);
            t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), 0.0f, 0.0f));

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = mirrorMesh;
            r.albedoColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            r.metallic = 1.0f;
            r.roughness = 0.02f;
            r.ao = 1.0f;
            r.presetName = "infinity_mirror";
            r.doubleSided = true;
        }
    }

    // Test spheres inside the box (re-used for multiple entities).
    auto sphereMesh = Utils::MeshGenerator::CreateSphere(0.25f, 32);
    if (renderer) {
        auto sphereResult = renderer->UploadMesh(sphereMesh);
        if (sphereResult.IsErr()) {
            spdlog::warn("Failed to upload Cornell sphere mesh: {}", sphereResult.Error());
            sphereMesh.reset();
        }
        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading Cornell sphere mesh; remaining geometry will be skipped.");
            return;
        }
    }
    if (sphereMesh && sphereMesh->gpuBuffers) {
        // Polished chrome sphere on the right side.
        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_SphereChrome");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(0.8f, 0.4f, 0.2f);

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = sphereMesh;
            r.albedoColor = glm::vec4(0.75f, 0.75f, 0.8f, 1.0f);
            r.metallic = 1.0f;
            r.roughness = 0.05f;
            r.ao = 1.0f;
            r.presetName = "chrome";
        }

        // Rough painted sphere on the left for GI and diffuse reflection
        // comparison against the polished metal sphere.
        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_SphereRough");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(-0.8f, 0.4f, 0.3f);

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = sphereMesh;
            r.albedoColor = glm::vec4(0.9f, 0.35f, 0.15f, 1.0f);
            r.metallic = 0.0f;
            r.roughness = 0.8f;
            r.ao = 1.0f;
            r.presetName = "cornell_rough_sphere";
        }
    }

    // Tall glossy box column near the back-left corner.
    auto boxMesh = Utils::MeshGenerator::CreateCube();
    if (renderer) {
        auto boxResult = renderer->UploadMesh(boxMesh);
        if (boxResult.IsErr()) {
            spdlog::warn("Failed to upload Cornell box mesh: {}", boxResult.Error());
            boxMesh.reset();
        }
        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading Cornell box mesh; skipping remaining Cornell geometry.");
            return;
        }
    }
    if (boxMesh && boxMesh->gpuBuffers) {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_BoxColumn");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(-0.9f, 0.75f, -0.4f);
        t.scale = glm::vec3(0.6f, 1.5f, 0.6f);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = boxMesh;
        r.albedoColor = glm::vec4(0.55f, 0.28f, 0.18f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.8f;
        r.ao = 1.0f;
        r.presetName = "brick";
        r.textures.albedoPath = "assets/textures/rtshowcase/rt_gallery_leftwall_albedo.dds";
        r.textures.normalPath = "assets/textures/rtshowcase/rt_gallery_leftwall_normal_bc5.dds";
    }

    // Low plinth in the center made from a cylinder for additional curved
    // geometry and self-shadowing.
    auto cylinderMesh = Utils::MeshGenerator::CreateCylinder(0.4f, 0.4f, 32);
    if (renderer) {
        auto cylResult = renderer->UploadMesh(cylinderMesh);
        if (cylResult.IsErr()) {
            spdlog::warn("Failed to upload Cornell cylinder mesh: {}", cylResult.Error());
            cylinderMesh.reset();
        }
        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading Cornell cylinder mesh; remaining extra Cornell geometry will be skipped.");
            return;
        }
    }
    if (cylinderMesh && cylinderMesh->gpuBuffers) {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_Plinth");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(-0.1f, 0.2f, 0.7f);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = cylinderMesh;
        r.albedoColor = glm::vec4(0.25f, 0.3f, 0.85f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.4f;
        r.ao = 1.0f;
        r.presetName = "plastic";
    }

    // Shallow water puddle in the center of the floor so the Cornell
    // layout exercises the same liquid shading path as the hero pool and
    // RT showcase courtyard. The global water function is tuned above.
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_WaterPuddle");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(0.0f, 0.0f, 0.4f);
        t.scale = glm::vec3(0.35f, 1.0f, 0.35f);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = floorMesh;
        r.albedoColor = glm::vec4(0.02f, 0.08f, 0.12f, 0.7f);
        r.metallic = 0.0f;
        r.roughness = 0.06f;
        r.ao = 1.0f;
        r.presetName = "water";
        m_registry->AddComponent<Scene::WaterSurfaceComponent>(e, Scene::WaterSurfaceComponent{0.0f});
    }

    // No hero character mesh in this layout; the Cornell box focuses on
    // spheres, columns, mirrors, liquids, and pure lighting/reflection behavior.

    // Secondary mirror panel on the right wall to create more complex
    // multi-bounce reflections.
    auto sideMirrorMesh = Utils::MeshGenerator::CreatePlane(1.0f, 1.5f);
    if (renderer) {
        auto sideResult = renderer->UploadMesh(sideMirrorMesh);
        if (sideResult.IsErr()) {
            spdlog::warn("Failed to upload Cornell side mirror mesh: {}", sideResult.Error());
            sideMirrorMesh.reset();
        }
        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading Cornell side mirror mesh; stopping additional mirror creation.");
            return;
        }
    }
    if (sideMirrorMesh && sideMirrorMesh->gpuBuffers) {
        // Pure mirror panel on the right wall.
        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_SideMirror");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(kCornellHalfExtent - 0.01f, 1.0f, -0.4f);
            t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), -glm::half_pi<float>(), 0.0f));

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = sideMirrorMesh;
            r.albedoColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            r.metallic = 1.0f;
            r.roughness = 0.03f;
            r.ao = 1.0f;
            r.presetName = "mirror";
            r.doubleSided = true;
        }

        // Small "glass brick" tiles near the side mirror using the same
        // geometry but with glass-like material parameters.
        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_GlassBrick1");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(kCornellHalfExtent - 0.015f, 0.7f, 0.3f);
            t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), -glm::half_pi<float>(), 0.0f));
            t.scale = glm::vec3(0.4f, 0.5f, 1.0f);

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = sideMirrorMesh;
            r.albedoColor = glm::vec4(0.6f, 0.8f, 1.0f, 1.0f);
            r.metallic = 0.0f;
            r.roughness = 0.05f;
            r.ao = 1.0f;
            r.presetName = "glass";
            r.doubleSided = true;
        }

        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_GlassBrick2");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(kCornellHalfExtent - 0.015f, 1.4f, 0.3f);
            t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), -glm::half_pi<float>(), 0.0f));
            t.scale = glm::vec3(0.4f, 0.5f, 1.0f);

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = sideMirrorMesh;
            r.albedoColor = glm::vec4(0.7f, 0.9f, 1.0f, 1.0f);
            r.metallic = 0.0f;
            r.roughness = 0.04f;
            r.ao = 1.0f;
            r.presetName = "glass";
            r.doubleSided = true;
        }
    }

    // Simple interior light rig: a ceiling spot approximating the classic
    // Cornell top light, a large softbox area light, and a small rim light
    // to add specular interest.
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_SoftboxArea");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(0.0f, kCornellHeight - 0.05f, 0.0f);
        glm::vec3 dir(0.0f, -1.0f, 0.0f);
        t.rotation = glm::quatLookAtLH(glm::normalize(dir), glm::vec3(0.0f, 0.0f, 1.0f));

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::AreaRect;
        l.color = glm::vec3(1.0f, 0.98f, 0.96f);
        l.intensity = 2.5f;
        l.range = 10.0f;
        l.areaSize = glm::vec2(3.0f, 2.0f);
        l.twoSided = false;
        l.castsShadows = false;
    }

    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_CeilingLight");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(0.0f, kCornellHeight - 0.1f, 0.0f);
        glm::vec3 dir(0.0f, -1.0f, 0.0f);
        t.rotation = glm::quatLookAtLH(glm::normalize(dir), glm::vec3(0.0f, 0.0f, 1.0f));

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Spot;
        l.color = glm::vec3(1.0f, 0.98f, 0.9f);
        l.intensity = 10.0f;
        l.range = 12.0f;
        l.innerConeDegrees = 35.0f;
        l.outerConeDegrees = 55.0f;
        l.castsShadows = true;
    }

    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Cornell_RimLight");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(-kCornellHalfExtent + 0.3f, 1.8f, -1.5f);
        glm::vec3 dir(0.4f, -0.5f, 1.0f);
        t.rotation = glm::quatLookAtLH(glm::normalize(dir), glm::vec3(0.0f, 1.0f, 0.0f));

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Spot;
        l.color = glm::vec3(0.8f, 0.9f, 1.0f);
        l.intensity = 4.0f;
        l.range = 10.0f;
        l.innerConeDegrees = 25.0f;
        l.outerConeDegrees = 40.0f;
        l.castsShadows = false;
    }
}

void Engine::BuildMaterialLabScene() {
    spdlog::info("Building public scene: Material Lab");

    auto* renderer = m_renderer.get();
    if (renderer) {
        Graphics::ApplyMaterialLabSceneControls(*renderer);
    }

    auto floorPlane = Utils::MeshGenerator::CreatePlane(20.0f, 11.0f);
    auto wallPlane = Utils::MeshGenerator::CreatePlane(20.0f, 6.5f);
    auto sphereMesh = Utils::MeshGenerator::CreateSphere(0.5f, 32);
    auto cubeMesh = Utils::MeshGenerator::CreateCube();
    auto cylinderMesh = Utils::MeshGenerator::CreateCylinder(0.32f, 1.3f, 32);
    auto torusMesh = Utils::MeshGenerator::CreateTorus(0.52f, 0.16f, 32, 16);
    auto scannedLanternMesh = LoadNaturalisticShowcaseMesh("Lantern_01/Lantern_01_1k.gltf");
    auto scannedTableMesh = LoadNaturalisticShowcaseMesh("WoodenTable_01/WoodenTable_01_1k.gltf");
    auto scannedFernMesh = LoadNaturalisticShowcaseMesh("fern_02/fern_02_1k.gltf");

    if (renderer) {
        auto uploadMesh = [&](const std::shared_ptr<Scene::MeshData>& mesh, const char* label) {
            if (!mesh) return true;
            auto res = renderer->UploadMesh(mesh);
            if (res.IsErr()) {
                spdlog::warn("Failed to upload MaterialLab {} mesh: {}", label, res.Error());
                return false;
            }
            if (renderer->IsDeviceRemoved()) {
                spdlog::error("DX12 device was removed while uploading MaterialLab {} mesh", label);
                return false;
            }
            return true;
        };

        if (!uploadMesh(floorPlane, "floor") ||
            !uploadMesh(wallPlane, "wall") ||
            !uploadMesh(sphereMesh, "sphere") ||
            !uploadMesh(cubeMesh, "cube") ||
            !uploadMesh(cylinderMesh, "cylinder") ||
            !uploadMesh(torusMesh, "torus") ||
            !uploadMesh(scannedLanternMesh, "naturalistic Lantern_01") ||
            !uploadMesh(scannedTableMesh, "naturalistic WoodenTable_01") ||
            !uploadMesh(scannedFernMesh, "naturalistic fern_02")) {
            return;
        }
    }

    {
        entt::entity camEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(camEntity, "MainCamera");
        auto& t = m_registry->AddComponent<TransformComponent>(camEntity);
        t.position = glm::vec3(0.0f, 2.45f, -8.2f);
        const glm::vec3 target(0.0f, 1.05f, -0.15f);
        t.rotation = glm::quatLookAtLH(glm::normalize(target - t.position), glm::vec3(0.0f, 1.0f, 0.0f));

        auto& cam = m_registry->AddComponent<Scene::CameraComponent>(camEntity);
        cam.fov = 54.0f;
        ConfigureShowcaseCameraClip(cam, 120.0f);
        cam.isActive = true;
        m_activeCameraEntity = camEntity;
    }

    auto addRenderable = [&](const char* tag,
                             const std::shared_ptr<Scene::MeshData>& mesh,
                             const glm::vec3& position,
                             const glm::vec3& scale,
                             const glm::vec3& euler,
                             const glm::vec4& color,
                             float metallic,
                             float roughness,
                             const char* preset) -> entt::entity {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, tag);
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = position;
        t.scale = scale;
        t.rotation = glm::quat(euler);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = mesh;
        r.albedoColor = color;
        r.metallic = metallic;
        r.roughness = roughness;
        r.ao = 1.0f;
        r.presetName = preset;
        return e;
    };

    if (floorPlane && floorPlane->gpuBuffers) {
        auto floor = addRenderable("MaterialLab_Floor", floorPlane,
                                   glm::vec3(0.0f, 0.0f, 0.0f),
                                   glm::vec3(1.0f),
                                   glm::vec3(0.0f),
                                   glm::vec4(0.56f, 0.58f, 0.58f, 1.0f),
                                   0.0f, 0.78f, "masonry");
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(floor);
        r.doubleSided = true;
        r.normalScale = 0.2f;
    }

    if (wallPlane && wallPlane->gpuBuffers) {
        auto back = addRenderable("MaterialLab_Backdrop", wallPlane,
                                  glm::vec3(0.0f, 3.0f, 4.2f),
                                  glm::vec3(1.0f),
                                  glm::vec3(-glm::half_pi<float>(), 0.0f, 0.0f),
                                  glm::vec4(0.74f, 0.75f, 0.73f, 1.0f),
                                  0.0f, 0.68f, "backdrop");
        m_registry->GetComponent<Scene::RenderableComponent>(back).doubleSided = true;
    }

    if (cubeMesh && cubeMesh->gpuBuffers) {
        const struct LabContextBlock {
            const char* tag;
            glm::vec3 position;
            glm::vec3 scale;
            glm::vec4 color;
            const char* preset;
            float roughness;
        } contextBlocks[] = {
            {"MaterialLab_BackdropBaseRail", glm::vec3(0.0f, 0.54f, 4.02f), glm::vec3(9.35f, 0.16f, 0.10f), glm::vec4(0.45f, 0.46f, 0.44f, 1.0f), "masonry", 0.62f},
            {"MaterialLab_BackdropTopRail", glm::vec3(0.0f, 2.70f, 4.00f), glm::vec3(8.90f, 0.10f, 0.10f), glm::vec4(0.48f, 0.49f, 0.47f, 1.0f), "masonry", 0.60f},
            {"MaterialLab_LeftSideReturn", glm::vec3(-8.95f, 1.55f, 0.50f), glm::vec3(0.12f, 1.55f, 3.65f), glm::vec4(0.53f, 0.54f, 0.52f, 1.0f), "masonry", 0.70f},
            {"MaterialLab_RightSideReturn", glm::vec3(8.95f, 1.55f, 0.50f), glm::vec3(0.12f, 1.55f, 3.65f), glm::vec4(0.53f, 0.54f, 0.52f, 1.0f), "masonry", 0.70f},
            {"MaterialLab_CenterFloorRunway", glm::vec3(0.0f, 0.035f, -0.22f), glm::vec3(7.70f, 0.028f, 0.16f), glm::vec4(0.36f, 0.37f, 0.36f, 1.0f), "stone", 0.54f},
            {"MaterialLab_RightPropPlatform", glm::vec3(3.65f, 0.18f, -0.10f), glm::vec3(1.75f, 0.20f, 1.25f), glm::vec4(0.55f, 0.50f, 0.44f, 1.0f), "wood", 0.58f}
        };
        for (const auto& block : contextBlocks) {
            addRenderable(block.tag, cubeMesh, block.position, block.scale, glm::vec3(0.0f),
                          block.color, 0.0f, block.roughness, block.preset);
        }
    }

    struct Swatch {
        const char* tag;
        const char* preset;
        glm::vec4 color;
        float metallic;
        float roughness;
        const std::shared_ptr<Scene::MeshData>* mesh;
        glm::vec3 scale;
        glm::vec3 euler;
    };

    const Swatch swatches[] = {
        {"MaterialLab_MirrorSphere", "mirror", glm::vec4(1.0f), 1.0f, 0.02f, &sphereMesh, glm::vec3(1.0f), glm::vec3(0.0f)},
        {"MaterialLab_ChromeSphere", "chrome", glm::vec4(0.76f, 0.78f, 0.84f, 1.0f), 1.0f, 0.055f, &sphereMesh, glm::vec3(1.0f), glm::vec3(0.0f)},
        {"MaterialLab_BrushedCylinder", "brushed_metal", glm::vec4(0.72f, 0.72f, 0.76f, 1.0f), 1.0f, 0.32f, &cylinderMesh, glm::vec3(1.0f), glm::vec3(0.0f)},
        {"MaterialLab_GoldTorus", "gold", glm::vec4(1.0f, 0.76f, 0.35f, 1.0f), 1.0f, 0.20f, &torusMesh, glm::vec3(1.0f), glm::vec3(glm::half_pi<float>(), 0.0f, 0.0f)},
        {"MaterialLab_ClearcoatCube", "clearcoat", glm::vec4(0.12f, 0.26f, 0.75f, 1.0f), 0.0f, 0.24f, &cubeMesh, glm::vec3(0.9f), glm::vec3(0.0f, 0.42f, 0.0f)},
        {"MaterialLab_PlasticSphere", "plastic", glm::vec4(0.85f, 0.12f, 0.17f, 1.0f), 0.0f, 0.38f, &sphereMesh, glm::vec3(1.0f), glm::vec3(0.0f)},
        {"MaterialLab_GlassCube", "glass", glm::vec4(0.72f, 0.92f, 1.0f, 1.0f), 0.0f, 0.05f, &cubeMesh, glm::vec3(0.9f), glm::vec3(0.0f, -0.35f, 0.0f)},
        {"MaterialLab_EmissiveTorus", "emissive_panel", glm::vec4(1.0f, 0.72f, 0.28f, 1.0f), 0.0f, 0.25f, &torusMesh, glm::vec3(1.0f), glm::vec3(glm::half_pi<float>(), 0.0f, 0.0f)},
        {"MaterialLab_VelvetSphere", "velvet", glm::vec4(0.55f, 0.08f, 0.22f, 1.0f), 0.0f, 0.82f, &sphereMesh, glm::vec3(1.0f), glm::vec3(0.0f)},
        {"MaterialLab_SubsurfaceCube", "skin_ish_wax", glm::vec4(0.92f, 0.54f, 0.42f, 1.0f), 0.0f, 0.46f, &cubeMesh, glm::vec3(0.9f), glm::vec3(0.0f, -0.28f, 0.0f)}
    };

    constexpr int swatchCount = static_cast<int>(sizeof(swatches) / sizeof(swatches[0]));
    for (int i = 0; i < swatchCount; ++i) {
        const int col = i % 5;
        const int row = i / 5;
        const float x = -6.4f + static_cast<float>(col) * 3.2f;
        const float z = -1.9f + static_cast<float>(row) * 2.6f;
        const auto& s = swatches[i];
        if (!s.mesh || !(*s.mesh) || !(*s.mesh)->gpuBuffers) {
            continue;
        }

        entt::entity e = addRenderable(s.tag, *s.mesh, glm::vec3(x, 0.78f, z), s.scale, s.euler,
                                       s.color, s.metallic, s.roughness, s.preset);
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(e);
        if (std::string(s.preset) == "glass") {
            r.transmissionFactor = 0.74f;
            r.ior = 1.50f;
            r.specularFactor = 1.35f;
        } else if (std::string(s.preset) == "chrome") {
            r.clearcoatFactor = 0.72f;
            r.clearcoatRoughnessFactor = 0.035f;
            r.specularFactor = 1.30f;
        } else if (std::string(s.preset) == "clearcoat") {
            r.clearcoatFactor = 0.9f;
            r.clearcoatRoughnessFactor = 0.08f;
            r.wetnessFactor = 0.55f;
        } else if (std::string(s.preset) == "brushed_metal") {
            r.anisotropyStrength = 0.75f;
            r.proceduralMaskStrength = 0.42f;
        } else if (std::string(s.preset) == "emissive_panel") {
            r.emissiveColor = glm::vec3(1.0f, 0.58f, 0.22f);
            r.emissiveStrength = 3.4f;
            r.emissiveBloomFactor = 0.6f;
        }
    }

    if (scannedTableMesh && scannedTableMesh->gpuBuffers) {
        auto table = addRenderable("MaterialLab_ScannedWoodenTable", scannedTableMesh,
                                   glm::vec3(3.62f, 0.20f, -0.20f),
                                   glm::vec3(1.00f),
                                   glm::vec3(0.0f, -0.38f, 0.0f),
                                   glm::vec4(0.43f, 0.25f, 0.12f, 1.0f),
                                   0.0f, 0.62f, "wood");
        ApplyNaturalisticAssetTextures(m_registry->GetComponent<Scene::RenderableComponent>(table), "WoodenTable_01");
    }

    if (scannedLanternMesh && scannedLanternMesh->gpuBuffers) {
        auto lantern = addRenderable("MaterialLab_ScannedLantern", scannedLanternMesh,
                                     glm::vec3(3.58f, 0.84f, -0.22f),
                                     glm::vec3(2.62f),
                                     glm::vec3(0.0f, -0.20f, 0.0f),
                                     glm::vec4(0.78f, 0.55f, 0.30f, 1.0f),
                                     1.0f, 0.24f, "brushed_metal");
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(lantern);
        ApplyNaturalisticAssetTextures(r, "Lantern_01");
        r.clearcoatFactor = 0.35f;
        r.specularFactor = 1.22f;
    }

    if (scannedFernMesh && scannedFernMesh->gpuBuffers) {
        for (int i = 0; i < 2; ++i) {
            auto fern = addRenderable(("MaterialLab_ScannedFern_" + std::to_string(i)).c_str(),
                                      scannedFernMesh,
                                      glm::vec3(2.76f + 1.52f * static_cast<float>(i), 0.08f, 0.72f),
                                      glm::vec3(0.66f),
                                      glm::vec3(0.0f, 0.45f - 0.8f * static_cast<float>(i), 0.0f),
                                      glm::vec4(0.08f, 0.28f, 0.12f, 1.0f),
                                      0.0f, 0.58f, "wood");
            auto& r = m_registry->GetComponent<Scene::RenderableComponent>(fern);
            ApplyNaturalisticAssetTextures(r, "fern_02");
            r.doubleSided = true;
        }
    }

    // High-contrast strips behind the glass swatch make the transparent pass'
    // refraction/tint behavior visible in screenshots and smoke captures.
    if (cubeMesh && cubeMesh->gpuBuffers) {
        struct GlassStrip {
            const char* tag;
            glm::vec3 offset;
            glm::vec4 color;
        };
        const GlassStrip strips[] = {
            {"MaterialLab_GlassRefractionStrip_Cyan",  {-3.72f, 1.08f, 2.05f}, glm::vec4(0.10f, 0.90f, 1.00f, 1.0f)},
            {"MaterialLab_GlassRefractionStrip_Amber", {-3.20f, 1.08f, 2.05f}, glm::vec4(1.00f, 0.58f, 0.14f, 1.0f)},
            {"MaterialLab_GlassRefractionStrip_Red",   {-2.68f, 1.08f, 2.05f}, glm::vec4(1.00f, 0.12f, 0.18f, 1.0f)}
        };
        for (const GlassStrip& strip : strips) {
            auto e = addRenderable(strip.tag, cubeMesh, strip.offset,
                                   glm::vec3(0.18f, 0.82f, 0.10f),
                                   glm::vec3(0.0f),
                                   strip.color,
                                   0.0f, 0.18f, "emissive_panel");
            auto& r = m_registry->GetComponent<Scene::RenderableComponent>(e);
            r.emissiveColor = glm::vec3(strip.color);
            r.emissiveStrength = 2.1f;
            r.emissiveBloomFactor = 0.35f;
        }
    }

    // Neutral plinths make reflections and contact shadows easier to inspect.
    if (cubeMesh && cubeMesh->gpuBuffers) {
        for (int i = 0; i < swatchCount; ++i) {
            const int col = i % 5;
            const int row = i / 5;
            const float x = -6.4f + static_cast<float>(col) * 3.2f;
            const float z = -1.9f + static_cast<float>(row) * 2.6f;
            addRenderable(("MaterialLab_Plinth_" + std::to_string(i)).c_str(),
                          cubeMesh,
                          glm::vec3(x, 0.25f, z),
                          glm::vec3(1.35f, 0.5f, 1.35f),
                          glm::vec3(0.0f),
                          glm::vec4(0.66f, 0.66f, 0.64f, 1.0f),
                          0.0f, 0.62f, "backdrop");
        }
    }

    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "MaterialLab_KeySoftbox");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(-2.8f, 5.0f, -3.2f);
        t.rotation = glm::quatLookAtLH(glm::normalize(glm::vec3(0.4f, -1.0f, 0.35f)),
                                       glm::vec3(0.0f, 1.0f, 0.0f));

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::AreaRect;
        l.color = glm::vec3(1.0f, 0.96f, 0.9f);
        l.intensity = 3.2f;
        l.range = 18.0f;
        l.areaSize = glm::vec2(4.5f, 2.2f);
        l.castsShadows = true;
    }

    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "MaterialLab_CoolRim");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(4.8f, 2.7f, 1.9f);

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Point;
        l.color = glm::vec3(0.58f, 0.72f, 1.0f);
        l.intensity = 2.4f;
        l.range = 8.0f;
        l.castsShadows = false;
    }
}

void Engine::BuildGlassWaterCourtyardScene() {
    spdlog::info("Building public scene: Glass and Water Courtyard");

    auto* renderer = m_renderer.get();
    if (renderer) {
        Graphics::ApplyGlassWaterCourtyardSceneControls(*renderer);
    }

    auto floorPlane = Utils::MeshGenerator::CreatePlane(18.0f, 14.0f);
    auto wallPlane = Utils::MeshGenerator::CreatePlane(18.0f, 7.0f);
    auto sideWallPlane = Utils::MeshGenerator::CreatePlane(14.0f, 7.0f);
    auto poolPlane = Utils::MeshGenerator::CreatePlane(7.2f, 5.2f);
    auto quadMesh = Utils::MeshGenerator::CreateQuad(1.0f, 1.0f);
    auto cubeMesh = Utils::MeshGenerator::CreateCube();
    auto sphereMesh = Utils::MeshGenerator::CreateSphere(0.5f, 32);
    auto columnMesh = Utils::MeshGenerator::CreateCylinder(0.28f, 3.2f, 32);
    auto scannedFernMesh = LoadNaturalisticShowcaseMesh("fern_02/fern_02_1k.gltf");
    auto scannedLanternMesh = LoadNaturalisticShowcaseMesh("Lantern_01/Lantern_01_1k.gltf");

    if (renderer) {
        auto uploadMesh = [&](const std::shared_ptr<Scene::MeshData>& mesh, const char* label) {
            if (!mesh) return true;
            auto res = renderer->UploadMesh(mesh);
            if (res.IsErr()) {
                spdlog::warn("Failed to upload GlassWaterCourtyard {} mesh: {}", label, res.Error());
                return false;
            }
            if (renderer->IsDeviceRemoved()) {
                spdlog::error("DX12 device was removed while uploading GlassWaterCourtyard {} mesh", label);
                return false;
            }
            return true;
        };

        if (!uploadMesh(floorPlane, "floor") ||
            !uploadMesh(wallPlane, "wall") ||
            !uploadMesh(sideWallPlane, "side wall") ||
            !uploadMesh(poolPlane, "pool") ||
            !uploadMesh(quadMesh, "quad") ||
            !uploadMesh(cubeMesh, "cube") ||
            !uploadMesh(sphereMesh, "sphere") ||
            !uploadMesh(columnMesh, "column") ||
            !uploadMesh(scannedFernMesh, "naturalistic fern_02") ||
            !uploadMesh(scannedLanternMesh, "naturalistic Lantern_01")) {
            return;
        }
    }

    {
        entt::entity camEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(camEntity, "MainCamera");
        auto& t = m_registry->AddComponent<TransformComponent>(camEntity);
        t.position = glm::vec3(-2.95f, 1.45f, -5.40f);
        const glm::vec3 target(0.0f, 0.55f, -0.70f);
        t.rotation = glm::quatLookAtLH(glm::normalize(target - t.position), glm::vec3(0.0f, 1.0f, 0.0f));

        auto& cam = m_registry->AddComponent<Scene::CameraComponent>(camEntity);
        cam.fov = 42.0f;
        ConfigureShowcaseCameraClip(cam, 140.0f);
        cam.isActive = true;
        m_activeCameraEntity = camEntity;
    }

    auto addRenderable = [&](const char* tag,
                             const std::shared_ptr<Scene::MeshData>& mesh,
                             const glm::vec3& position,
                             const glm::vec3& scale,
                             const glm::vec3& euler,
                             const glm::vec4& color,
                             float metallic,
                             float roughness,
                             const char* preset) -> entt::entity {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, tag);
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = position;
        t.scale = scale;
        t.rotation = glm::quat(euler);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = mesh;
        r.albedoColor = color;
        r.metallic = metallic;
        r.roughness = roughness;
        r.ao = 1.0f;
        r.presetName = preset;
        return e;
    };

    if (floorPlane && floorPlane->gpuBuffers) {
        auto floor = addRenderable("GlassWaterCourtyard_Floor", floorPlane,
                                   glm::vec3(0.0f, 0.0f, 0.0f),
                                   glm::vec3(1.0f),
                                   glm::vec3(0.0f),
                                   glm::vec4(0.34f, 0.31f, 0.28f, 1.0f),
                                   0.0f, 0.78f, "masonry");
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(floor);
        r.doubleSided = true;
        r.normalScale = 0.22f;
    }

    if (wallPlane && wallPlane->gpuBuffers) {
        auto back = addRenderable("GlassWaterCourtyard_BackWall", wallPlane,
                                  glm::vec3(0.0f, 3.5f, 5.8f),
                                  glm::vec3(1.0f),
                                  glm::vec3(-glm::half_pi<float>(), 0.0f, 0.0f),
                                  glm::vec4(0.44f, 0.39f, 0.34f, 1.0f),
                                  0.0f, 0.72f, "masonry");
        m_registry->GetComponent<Scene::RenderableComponent>(back).doubleSided = true;
    }

    if (sideWallPlane && sideWallPlane->gpuBuffers) {
        auto left = addRenderable("GlassWaterCourtyard_LeftWall", sideWallPlane,
                                  glm::vec3(-9.0f, 3.5f, -0.2f),
                                  glm::vec3(1.0f),
                                  glm::vec3(-glm::half_pi<float>(), glm::half_pi<float>(), 0.0f),
                                  glm::vec4(0.40f, 0.36f, 0.32f, 1.0f),
                                  0.0f, 0.70f, "masonry");
        auto right = addRenderable("GlassWaterCourtyard_RightWall", sideWallPlane,
                                   glm::vec3(9.0f, 3.5f, -0.2f),
                                   glm::vec3(1.0f),
                                   glm::vec3(-glm::half_pi<float>(), -glm::half_pi<float>(), 0.0f),
                                   glm::vec4(0.40f, 0.36f, 0.32f, 1.0f),
                                   0.0f, 0.70f, "masonry");
        m_registry->GetComponent<Scene::RenderableComponent>(left).doubleSided = true;
        m_registry->GetComponent<Scene::RenderableComponent>(right).doubleSided = true;
    }

    if (poolPlane && poolPlane->gpuBuffers) {
        auto water = addRenderable("GlassWaterCourtyard_WaterSurface", poolPlane,
                                   glm::vec3(0.0f, -0.02f, -0.25f),
                                   glm::vec3(0.92f),
                                   glm::vec3(0.0f),
                                   glm::vec4(0.08f, 0.42f, 0.72f, 0.74f),
                                   0.0f, 0.045f, "water");
        Scene::WaterSurfaceComponent waterSurface{};
        waterSurface.absorption = 0.40f;
        waterSurface.foamStrength = 0.92f;
        waterSurface.viscosity = 0.12f;
        waterSurface.bodyThickness = 0.52f;
        waterSurface.sloshStrength = 0.34f;
        waterSurface.meniscusStrength = 0.45f;
        waterSurface.flowSpeed = 1.15f;
        waterSurface.shallowTint = glm::vec3(0.10f, 0.55f, 0.85f);
        waterSurface.deepTint = glm::vec3(0.005f, 0.065f, 0.22f);
        m_registry->AddComponent<Scene::WaterSurfaceComponent>(water, waterSurface);
    }

    if (cubeMesh && cubeMesh->gpuBuffers) {
        const struct CourtyardBlock {
            const char* tag;
            glm::vec3 position;
            glm::vec3 scale;
            glm::vec4 color;
            const char* preset;
            float roughness;
        } blocks[] = {
            {"GlassWaterCourtyard_PoolCoping_North", glm::vec3(0.0f, 0.10f,  2.78f), glm::vec3(4.35f, 0.20f, 0.18f), glm::vec4(0.74f, 0.69f, 0.61f, 1.0f), "masonry", 0.62f},
            {"GlassWaterCourtyard_PoolCoping_South", glm::vec3(0.0f, 0.10f, -3.28f), glm::vec3(4.35f, 0.20f, 0.18f), glm::vec4(0.74f, 0.69f, 0.61f, 1.0f), "masonry", 0.62f},
            {"GlassWaterCourtyard_PoolCoping_West",  glm::vec3(-4.25f, 0.10f, -0.25f), glm::vec3(0.18f, 0.20f, 3.18f), glm::vec4(0.74f, 0.69f, 0.61f, 1.0f), "masonry", 0.62f},
            {"GlassWaterCourtyard_PoolCoping_East",  glm::vec3( 4.25f, 0.10f, -0.25f), glm::vec3(0.18f, 0.20f, 3.18f), glm::vec4(0.74f, 0.69f, 0.61f, 1.0f), "masonry", 0.62f},
            {"GlassWaterCourtyard_BackWall_LowerTrim", glm::vec3(0.0f, 0.62f, 5.55f), glm::vec3(8.2f, 0.12f, 0.10f), glm::vec4(0.44f, 0.37f, 0.32f, 1.0f), "masonry", 0.68f},
            {"GlassWaterCourtyard_BackWall_UpperTrim", glm::vec3(0.0f, 2.85f, 5.54f), glm::vec3(7.4f, 0.10f, 0.10f), glm::vec4(0.43f, 0.36f, 0.31f, 1.0f), "masonry", 0.70f},
            {"GlassWaterCourtyard_FloorInlay_Left", glm::vec3(-3.65f, 0.025f, -0.25f), glm::vec3(0.10f, 0.035f, 3.60f), glm::vec4(0.25f, 0.23f, 0.22f, 1.0f), "stone", 0.54f},
            {"GlassWaterCourtyard_FloorInlay_Right", glm::vec3( 3.65f, 0.025f, -0.25f), glm::vec3(0.10f, 0.035f, 3.60f), glm::vec4(0.25f, 0.23f, 0.22f, 1.0f), "stone", 0.54f},
            {"GlassWaterCourtyard_PoolStep_ShallowA", glm::vec3(-2.15f, 0.045f, -2.42f), glm::vec3(1.35f, 0.055f, 0.22f), glm::vec4(0.60f, 0.67f, 0.66f, 1.0f), "wet_stone", 0.36f},
            {"GlassWaterCourtyard_PoolStep_ShallowB", glm::vec3(-2.15f, 0.075f, -2.08f), glm::vec3(1.02f, 0.050f, 0.18f), glm::vec4(0.57f, 0.64f, 0.64f, 1.0f), "wet_stone", 0.34f},
            {"GlassWaterCourtyard_WaterlineTile_North", glm::vec3(0.0f, 0.035f, 2.36f), glm::vec3(3.30f, 0.035f, 0.055f), glm::vec4(0.58f, 0.68f, 0.70f, 1.0f), "wet_stone", 0.32f},
            {"GlassWaterCourtyard_WaterlineTile_South", glm::vec3(0.0f, 0.035f, -2.88f), glm::vec3(3.30f, 0.035f, 0.055f), glm::vec4(0.58f, 0.68f, 0.70f, 1.0f), "wet_stone", 0.32f},
            {"GlassWaterCourtyard_PoolCorner_NW", glm::vec3(-4.25f, 0.16f, 2.78f), glm::vec3(0.34f, 0.28f, 0.34f), glm::vec4(0.66f, 0.60f, 0.52f, 1.0f), "masonry", 0.50f},
            {"GlassWaterCourtyard_PoolCorner_NE", glm::vec3( 4.25f, 0.16f, 2.78f), glm::vec3(0.34f, 0.28f, 0.34f), glm::vec4(0.66f, 0.60f, 0.52f, 1.0f), "masonry", 0.50f},
            {"GlassWaterCourtyard_PoolCorner_SW", glm::vec3(-4.25f, 0.16f, -3.28f), glm::vec3(0.34f, 0.28f, 0.34f), glm::vec4(0.66f, 0.60f, 0.52f, 1.0f), "masonry", 0.50f},
            {"GlassWaterCourtyard_PoolCorner_SE", glm::vec3( 4.25f, 0.16f, -3.28f), glm::vec3(0.34f, 0.28f, 0.34f), glm::vec4(0.66f, 0.60f, 0.52f, 1.0f), "masonry", 0.50f},
            {"GlassWaterCourtyard_CourtyardSkirt_Front", glm::vec3(0.0f, 0.08f, -4.82f), glm::vec3(7.4f, 0.12f, 0.16f), glm::vec4(0.48f, 0.43f, 0.37f, 1.0f), "masonry", 0.56f},
            {"GlassWaterCourtyard_CourtyardSkirt_Left", glm::vec3(-5.15f, 0.08f, -0.25f), glm::vec3(0.15f, 0.12f, 4.45f), glm::vec4(0.48f, 0.43f, 0.37f, 1.0f), "masonry", 0.56f},
            {"GlassWaterCourtyard_CourtyardSkirt_Right", glm::vec3(5.15f, 0.08f, -0.25f), glm::vec3(0.15f, 0.12f, 4.45f), glm::vec4(0.48f, 0.43f, 0.37f, 1.0f), "masonry", 0.56f},
            {"GlassWaterCourtyard_BackWall_Niche_Dark", glm::vec3(0.0f, 1.72f, 5.48f), glm::vec3(3.50f, 1.35f, 0.08f), glm::vec4(0.18f, 0.17f, 0.16f, 1.0f), "wet_stone", 0.42f},
            {"GlassWaterCourtyard_BackWall_Niche_Lintel", glm::vec3(0.0f, 2.48f, 5.38f), glm::vec3(3.95f, 0.14f, 0.18f), glm::vec4(0.56f, 0.50f, 0.44f, 1.0f), "masonry", 0.58f},
            {"GlassWaterCourtyard_BackWall_Niche_LeftJamb", glm::vec3(-2.08f, 1.58f, 5.38f), glm::vec3(0.14f, 1.12f, 0.18f), glm::vec4(0.54f, 0.48f, 0.42f, 1.0f), "masonry", 0.58f},
            {"GlassWaterCourtyard_BackWall_Niche_RightJamb", glm::vec3(2.08f, 1.58f, 5.38f), glm::vec3(0.14f, 1.12f, 0.18f), glm::vec4(0.54f, 0.48f, 0.42f, 1.0f), "masonry", 0.58f},
            {"GlassWaterCourtyard_Planter_Left", glm::vec3(-5.60f, 0.34f, 1.72f), glm::vec3(0.78f, 0.34f, 1.12f), glm::vec4(0.30f, 0.25f, 0.20f, 1.0f), "wet_stone", 0.38f},
            {"GlassWaterCourtyard_Planter_Right", glm::vec3(5.60f, 0.34f, 1.72f), glm::vec3(0.78f, 0.34f, 1.12f), glm::vec4(0.30f, 0.25f, 0.20f, 1.0f), "wet_stone", 0.38f},
            {"GlassWaterCourtyard_CanopyShadowBaffle_Left", glm::vec3(-3.30f, 2.70f, -0.35f), glm::vec3(0.10f, 0.55f, 4.10f), glm::vec4(0.12f, 0.13f, 0.14f, 1.0f), "brushed_metal", 0.30f},
            {"GlassWaterCourtyard_CanopyShadowBaffle_Right", glm::vec3(3.30f, 2.70f, -0.35f), glm::vec3(0.10f, 0.55f, 4.10f), glm::vec4(0.12f, 0.13f, 0.14f, 1.0f), "brushed_metal", 0.30f}
        };
        for (const auto& block : blocks) {
            auto e = addRenderable(block.tag, cubeMesh, block.position, block.scale, glm::vec3(0.0f),
                                   block.color, 0.0f, block.roughness, block.preset);
            if (std::string(block.preset) == "wet_stone") {
                m_registry->GetComponent<Scene::RenderableComponent>(e).wetnessFactor = 0.50f;
            }
        }
    }

    if (columnMesh && columnMesh->gpuBuffers) {
        const glm::vec3 columnPositions[] = {
            {-4.8f, 1.6f, -2.8f},
            { 4.8f, 1.6f, -2.8f},
            {-4.8f, 1.6f,  2.8f},
            { 4.8f, 1.6f,  2.8f}
        };
        for (int i = 0; i < 4; ++i) {
            addRenderable(("GlassWaterCourtyard_Column_" + std::to_string(i)).c_str(),
                          columnMesh,
                          columnPositions[i],
                          glm::vec3(1.0f),
                          glm::vec3(0.0f),
                          glm::vec4(0.70f, 0.64f, 0.56f, 1.0f),
                          0.0f, 0.42f, "masonry");
            if (cubeMesh && cubeMesh->gpuBuffers) {
                addRenderable(("GlassWaterCourtyard_ColumnBase_" + std::to_string(i)).c_str(),
                              cubeMesh,
                              glm::vec3(columnPositions[i].x, 0.16f, columnPositions[i].z),
                              glm::vec3(0.74f, 0.32f, 0.74f),
                              glm::vec3(0.0f),
                              glm::vec4(0.62f, 0.55f, 0.48f, 1.0f),
                              0.0f, 0.52f, "masonry");
                addRenderable(("GlassWaterCourtyard_ColumnCap_" + std::to_string(i)).c_str(),
                              cubeMesh,
                              glm::vec3(columnPositions[i].x, 3.12f, columnPositions[i].z),
                              glm::vec3(0.82f, 0.22f, 0.82f),
                              glm::vec3(0.0f),
                              glm::vec4(0.66f, 0.59f, 0.50f, 1.0f),
                              0.0f, 0.48f, "masonry");
            }
        }
    }

    if (quadMesh && quadMesh->gpuBuffers) {
        auto roof = addRenderable("GlassWaterCourtyard_GlassCanopy", quadMesh,
                                  glm::vec3(0.0f, 3.15f, -0.25f),
                                  glm::vec3(6.4f, 4.6f, 1.0f),
                                  glm::vec3(glm::half_pi<float>(), 0.0f, 0.0f),
                                  glm::vec4(0.72f, 0.90f, 1.0f, 1.0f),
                                  0.0f, 0.035f, "glass_panel");
        auto& roofR = m_registry->GetComponent<Scene::RenderableComponent>(roof);
        roofR.transmissionFactor = 0.66f;
        roofR.ior = 1.50f;
        roofR.specularFactor = 1.35f;
        roofR.doubleSided = true;

        if (cubeMesh && cubeMesh->gpuBuffers) {
            const struct CanopyFrame {
                const char* tag;
                glm::vec3 position;
                glm::vec3 scale;
            } framePieces[] = {
                {"GlassWaterCourtyard_CanopyFrame_North", glm::vec3(0.0f, 3.18f, 2.10f), glm::vec3(6.8f, 0.06f, 0.08f)},
                {"GlassWaterCourtyard_CanopyFrame_South", glm::vec3(0.0f, 3.18f, -2.60f), glm::vec3(6.8f, 0.06f, 0.08f)},
                {"GlassWaterCourtyard_CanopyFrame_West",  glm::vec3(-3.35f, 3.18f, -0.25f), glm::vec3(0.08f, 0.06f, 4.7f)},
                {"GlassWaterCourtyard_CanopyFrame_East",  glm::vec3( 3.35f, 3.18f, -0.25f), glm::vec3(0.08f, 0.06f, 4.7f)},
                {"GlassWaterCourtyard_CanopyFrame_CenterA", glm::vec3(0.0f, 3.19f, -0.25f), glm::vec3(6.5f, 0.045f, 0.055f)},
                {"GlassWaterCourtyard_CanopyFrame_CenterB", glm::vec3(0.0f, 3.20f, -0.25f), glm::vec3(0.055f, 0.045f, 4.4f)}
            };
            for (const auto& piece : framePieces) {
                addRenderable(piece.tag, cubeMesh, piece.position, piece.scale, glm::vec3(0.0f),
                              glm::vec4(0.22f, 0.24f, 0.25f, 1.0f),
                              1.0f, 0.24f, "brushed_metal");
            }
        }

        auto warmPanel = addRenderable("GlassWaterCourtyard_SunsetPanel", quadMesh,
                                       glm::vec3(-3.2f, 2.5f, 5.65f),
                                       glm::vec3(2.4f, 0.65f, 1.0f),
                                       glm::vec3(0.0f),
                                       glm::vec4(1.0f, 0.58f, 0.28f, 1.0f),
                                       0.0f, 0.2f, "emissive_panel");
        auto& panelR = m_registry->GetComponent<Scene::RenderableComponent>(warmPanel);
        panelR.emissiveColor = glm::vec3(1.0f, 0.48f, 0.20f);
        panelR.emissiveStrength = 2.6f;
        panelR.doubleSided = true;
    }

    if (cubeMesh && cubeMesh->gpuBuffers) {
        auto glassBlock = addRenderable("GlassWaterCourtyard_GlassBlock", cubeMesh,
                                        glm::vec3(2.8f, 0.72f, -1.3f),
                                        glm::vec3(0.85f, 1.1f, 0.85f),
                                        glm::vec3(0.0f, 0.55f, 0.0f),
                                        glm::vec4(0.64f, 0.86f, 1.0f, 1.0f),
                                        0.0f, 0.04f, "glass");
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(glassBlock);
        r.transmissionFactor = 0.76f;
        r.ior = 1.52f;
        r.specularFactor = 1.38f;

        const struct GlassScreen {
            const char* tag;
            glm::vec3 position;
            glm::vec3 scale;
            float yaw;
        } screens[] = {
            {"GlassWaterCourtyard_GlassScreen_Left", glm::vec3(-2.8f, 0.95f, 1.55f), glm::vec3(0.08f, 1.25f, 1.15f), 0.18f},
            {"GlassWaterCourtyard_GlassScreen_Right", glm::vec3(2.25f, 0.95f, 1.45f), glm::vec3(0.08f, 1.20f, 1.05f), -0.24f}
        };
        for (const auto& screen : screens) {
            auto screenEntity = addRenderable(screen.tag, cubeMesh,
                                              screen.position,
                                              screen.scale,
                                              glm::vec3(0.0f, screen.yaw, 0.0f),
                                              glm::vec4(0.56f, 0.82f, 1.0f, 1.0f),
                                              0.0f, 0.035f, "glass_panel");
            auto& screenR = m_registry->GetComponent<Scene::RenderableComponent>(screenEntity);
            screenR.transmissionFactor = 0.72f;
            screenR.ior = 1.49f;
            screenR.specularFactor = 1.32f;
        }
    }

    if (sphereMesh && sphereMesh->gpuBuffers) {
        addRenderable("GlassWaterCourtyard_MirrorSphere", sphereMesh,
                      glm::vec3(-3.25f, 0.68f, -0.55f),
                      glm::vec3(0.82f),
                      glm::vec3(0.0f),
                      glm::vec4(0.82f, 0.78f, 0.72f, 1.0f),
                      1.0f, 0.08f, "mirror");

        const struct CourtyardPlant {
            const char* tag;
            glm::vec3 position;
            glm::vec3 scale;
            glm::vec4 color;
        } plants[] = {
            {"GlassWaterCourtyard_PlanterFoliage_Left_A", glm::vec3(-5.70f, 0.86f, 1.25f), glm::vec3(0.54f, 0.38f, 0.42f), glm::vec4(0.09f, 0.22f, 0.13f, 1.0f)},
            {"GlassWaterCourtyard_PlanterFoliage_Left_B", glm::vec3(-5.44f, 1.04f, 1.82f), glm::vec3(0.44f, 0.46f, 0.36f), glm::vec4(0.12f, 0.29f, 0.16f, 1.0f)},
            {"GlassWaterCourtyard_PlanterFoliage_Right_A", glm::vec3(5.70f, 0.86f, 1.25f), glm::vec3(0.54f, 0.38f, 0.42f), glm::vec4(0.08f, 0.20f, 0.12f, 1.0f)},
            {"GlassWaterCourtyard_PlanterFoliage_Right_B", glm::vec3(5.42f, 1.02f, 1.88f), glm::vec3(0.40f, 0.44f, 0.34f), glm::vec4(0.13f, 0.31f, 0.17f, 1.0f)}
        };
        for (const auto& plant : plants) {
            auto foliage = addRenderable(plant.tag, sphereMesh, plant.position, plant.scale, glm::vec3(0.0f),
                                         plant.color, 0.0f, 0.68f, "wood");
            m_registry->GetComponent<Scene::RenderableComponent>(foliage).doubleSided = true;
        }
    }

    if (scannedFernMesh && scannedFernMesh->gpuBuffers) {
        const struct CourtyardFernAsset {
            const char* tag;
            glm::vec3 position;
            glm::vec3 scale;
            float yaw;
        } ferns[] = {
            {"GlassWaterCourtyard_ScannedFern_LeftA", glm::vec3(-5.78f, 0.48f, 1.28f), glm::vec3(0.58f), 0.35f},
            {"GlassWaterCourtyard_ScannedFern_LeftB", glm::vec3(-5.38f, 0.50f, 1.96f), glm::vec3(0.46f), -0.22f},
            {"GlassWaterCourtyard_ScannedFern_RightA", glm::vec3(5.72f, 0.48f, 1.30f), glm::vec3(0.56f), -0.38f},
            {"GlassWaterCourtyard_ScannedFern_RightB", glm::vec3(5.34f, 0.50f, 1.98f), glm::vec3(0.44f), 0.18f}
        };
        for (const auto& fern : ferns) {
            auto e = addRenderable(fern.tag, scannedFernMesh, fern.position, fern.scale,
                                   glm::vec3(0.0f, fern.yaw, 0.0f),
                                   glm::vec4(0.12f, 0.32f, 0.15f, 1.0f),
                                   0.0f, 0.62f, "wood");
            auto& r = m_registry->GetComponent<Scene::RenderableComponent>(e);
            ApplyNaturalisticAssetTextures(r, "fern_02");
            r.doubleSided = true;
        }
    }

    if (scannedLanternMesh && scannedLanternMesh->gpuBuffers) {
        const struct CourtyardLanternAsset {
            const char* tag;
            glm::vec3 position;
            glm::vec3 scale;
            float yaw;
        } lanterns[] = {
            {"GlassWaterCourtyard_ScannedLantern_Left", glm::vec3(-2.95f, 0.24f, 2.45f), glm::vec3(0.34f), 0.28f},
            {"GlassWaterCourtyard_ScannedLantern_Right", glm::vec3(3.05f, 0.24f, 2.34f), glm::vec3(0.30f), -0.36f}
        };
        for (const auto& lantern : lanterns) {
            auto e = addRenderable(lantern.tag, scannedLanternMesh, lantern.position, lantern.scale,
                                   glm::vec3(0.0f, lantern.yaw, 0.0f),
                                   glm::vec4(0.92f, 0.68f, 0.36f, 1.0f),
                                   1.0f, 0.24f, "brushed_gold");
            ApplyNaturalisticAssetTextures(m_registry->GetComponent<Scene::RenderableComponent>(e), "Lantern_01");
        }
    }

    auto addLight = [&](const char* tag,
                        Scene::LightType type,
                        const glm::vec3& position,
                        const glm::vec3& direction,
                        const glm::vec3& color,
                        float intensity,
                        float range) {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, tag);
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = position;
        if (glm::length(direction) > 0.001f) {
            t.rotation = glm::quatLookAtLH(glm::normalize(direction), glm::vec3(0.0f, 1.0f, 0.0f));
        }
        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = type;
        l.color = color;
        l.intensity = intensity;
        l.range = range;
        l.castsShadows = type != Scene::LightType::Point;
        if (type == Scene::LightType::Spot) {
            l.innerConeDegrees = 24.0f;
            l.outerConeDegrees = 42.0f;
        } else if (type == Scene::LightType::AreaRect) {
            l.areaSize = glm::vec2(4.5f, 2.0f);
        }
    };

    addLight("GlassWaterCourtyard_SunsetKey", Scene::LightType::Spot,
             glm::vec3(-4.2f, 4.8f, -4.4f), glm::vec3(0.58f, -0.85f, 0.72f),
             glm::vec3(1.0f, 0.70f, 0.44f), 5.4f, 24.0f);
    addLight("GlassWaterCourtyard_CoolRim", Scene::LightType::AreaRect,
             glm::vec3(4.6f, 3.2f, -3.8f), glm::vec3(-0.6f, -0.45f, 0.8f),
             glm::vec3(0.52f, 0.72f, 1.0f), 2.5f, 14.0f);
    addLight("GlassWaterCourtyard_UnderwaterFill", Scene::LightType::Point,
             glm::vec3(0.0f, -0.35f, -0.25f), glm::vec3(0.0f),
             glm::vec3(0.16f, 0.42f, 0.95f), 2.4f, 9.0f);
}

void Engine::BuildOutdoorSunsetBeachScene() {
    spdlog::info("Building public scene: Outdoor Sunset Beach");

    auto* renderer = m_renderer.get();
    if (renderer) {
        Graphics::ApplyGlassWaterCourtyardSceneControls(*renderer);
        renderer->SetEnvironmentPreset("neutral_procedural");
        renderer->SetIBLEnabled(false);
        renderer->SetIBLIntensity(0.0f, 0.0f);
        renderer->SetBackgroundPresentation(true, 0.94f, 0.0f);
        renderer->SetAmbientLighting(glm::vec3(0.20f, 0.135f, 0.10f), 1.0f); // warm golden-hour fill
        renderer->SetExposure(1.02f);
        renderer->SetBloomIntensity(0.18f);
        renderer->SetBloomShape(0.82f, 0.70f, 2.0f);
        renderer->SetColorGrade(0.48f, 0.05f); // warm sunset grade without orange crush
        renderer->SetToneGrade(1.045f, 1.035f);
        renderer->SetCinematicPost(0.085f, 0.10f);
        renderer->SetSunDirection(glm::normalize(glm::vec3(0.30f, 0.10f, 0.95f))); // low sun over the water
        renderer->SetSunColor(glm::vec3(1.0f, 0.56f, 0.30f));                        // golden-hour sun
        renderer->SetSunIntensity(2.7f);
        renderer->SetWaterParams(-0.02f, 0.046f, 9.8f, 0.42f, 0.88f, 0.30f, 0.020f, 0.44f);
        renderer->SetWaterOptics(0.050f, 1.48f);
        renderer->SetFogEnabled(true);
        renderer->SetFogParams(0.018f, 0.0f, 0.34f, 4.0f);
        renderer->SetGodRayIntensity(0.34f);
        renderer->SetShadowBias(0.0035f);
        renderer->SetShadowPCFRadius(3.25f);
        renderer->SetParticlesEnabled(true);
        renderer->SetParticleDensityScale(0.90f);
        renderer->SetParticleTuning(1.05f, 1.65f, 0.64f, 0.46f);
    }

    auto sandPlane = Utils::MeshGenerator::CreatePlane(26.0f, 18.0f);
    for (auto& uv : sandPlane->texCoords) {
        uv *= glm::vec2(18.0f, 12.0f);
    }
    auto waterPlane = Utils::MeshGenerator::CreatePlane(26.0f, 12.0f);
    auto cubeMesh = Utils::MeshGenerator::CreateCube();
    auto sphereMesh = Utils::MeshGenerator::CreateSphere(0.5f, 32);
    auto trunkMesh = Utils::MeshGenerator::CreateCylinder(0.18f, 3.2f, 18);
    auto coneMesh = Utils::MeshGenerator::CreateCone(0.5f, 1.0f, 32);
    auto leafMesh = Utils::MeshGenerator::CreateQuad(1.0f, 1.0f);
    auto lilyPadMesh = Utils::MeshGenerator::CreateDisk(0.5f, 48);
    auto scannedBoulderMesh = LoadNaturalisticShowcaseMesh("boulder_01/boulder_01_1k.gltf");
    auto scannedTrunkMesh = LoadNaturalisticShowcaseMesh("dead_tree_trunk/dead_tree_trunk_1k.gltf");
    auto scannedBranchesMesh = LoadNaturalisticShowcaseMesh("dry_branches_medium_01/dry_branches_medium_01_1k.gltf");
    auto scannedFernMesh = LoadNaturalisticShowcaseMesh("fern_02/fern_02_1k.gltf");
    auto scannedGrassMesh = LoadNaturalisticShowcaseMesh("grass_bermuda_01/grass_bermuda_01_1k.gltf");
    auto scannedStumpMesh = LoadNaturalisticShowcaseMesh("tree_stump_01/tree_stump_01_1k.gltf");
    auto scannedMossRockMesh = LoadNaturalisticShowcaseMesh("rock_moss_set_01/rock_moss_set_01_1k.gltf");
    auto scannedBushMesh = LoadNaturalisticShowcaseMesh("wild_rooibos_bush/wild_rooibos_bush_1k.gltf");

    if (renderer) {
        auto uploadMesh = [&](const std::shared_ptr<Scene::MeshData>& mesh, const char* label) {
            if (!mesh) return true;
            auto res = renderer->UploadMesh(mesh);
            if (res.IsErr()) {
                spdlog::warn("Failed to upload OutdoorSunsetBeach {} mesh: {}", label, res.Error());
                return false;
            }
            if (renderer->IsDeviceRemoved()) {
                spdlog::error("DX12 device was removed while uploading OutdoorSunsetBeach {} mesh", label);
                return false;
            }
            return true;
        };

        if (!uploadMesh(sandPlane, "sand") ||
            !uploadMesh(waterPlane, "water") ||
            !uploadMesh(cubeMesh, "cube") ||
            !uploadMesh(sphereMesh, "sphere") ||
            !uploadMesh(trunkMesh, "trunk") ||
            !uploadMesh(coneMesh, "cone") ||
            !uploadMesh(leafMesh, "leaf") ||
            !uploadMesh(lilyPadMesh, "lily pad disk") ||
            !uploadMesh(scannedBoulderMesh, "naturalistic boulder_01") ||
            !uploadMesh(scannedTrunkMesh, "naturalistic dead_tree_trunk") ||
            !uploadMesh(scannedBranchesMesh, "naturalistic dry_branches_medium_01") ||
            !uploadMesh(scannedFernMesh, "naturalistic fern_02") ||
            !uploadMesh(scannedGrassMesh, "naturalistic grass_bermuda_01") ||
            !uploadMesh(scannedStumpMesh, "naturalistic tree_stump_01") ||
            !uploadMesh(scannedMossRockMesh, "naturalistic rock_moss_set_01") ||
            !uploadMesh(scannedBushMesh, "naturalistic wild_rooibos_bush")) {
            return;
        }
    }

    {
        entt::entity camEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(camEntity, "MainCamera");
        auto& t = m_registry->AddComponent<TransformComponent>(camEntity);
        t.position = glm::vec3(-4.35f, 0.92f, -2.78f);
        const glm::vec3 target(0.55f, 0.22f, 2.05f);
        t.rotation = glm::quatLookAtLH(glm::normalize(target - t.position), glm::vec3(0.0f, 1.0f, 0.0f));

        auto& cam = m_registry->AddComponent<Scene::CameraComponent>(camEntity);
        cam.fov = 39.0f;
        ConfigureShowcaseCameraClip(cam, 180.0f);
        cam.isActive = true;
        m_activeCameraEntity = camEntity;
    }

    auto addRenderable = [&](const std::string& tag,
                             const std::shared_ptr<Scene::MeshData>& mesh,
                             const glm::vec3& position,
                             const glm::vec3& scale,
                             const glm::vec3& euler,
                             const glm::vec4& color,
                             float metallic,
                             float roughness,
                             const char* preset) -> entt::entity {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, tag);
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = position;
        t.scale = scale;
        t.rotation = glm::quat(euler);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = mesh;
        r.albedoColor = color;
        r.metallic = metallic;
        r.roughness = roughness;
        r.ao = 1.0f;
        r.presetName = preset;
        return e;
    };

    auto addOrganicPatch = [&](const std::string& tag,
                               const glm::vec3& position,
                               const glm::vec3& scale,
                               float yaw,
                               const glm::vec4& color,
                               float roughness,
                               const char* preset) -> entt::entity {
        auto patchMesh = (lilyPadMesh && lilyPadMesh->gpuBuffers) ? lilyPadMesh : cubeMesh;
        const bool diskPatch = (patchMesh == lilyPadMesh);
        const glm::vec3 patchScale = diskPatch ? glm::vec3(scale.x, 1.0f, scale.z) : scale;
        auto e = addRenderable(tag, patchMesh, position, patchScale,
                               glm::vec3(0.0f, yaw, 0.0f),
                               color, 0.0f, roughness, preset);
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(e);
        r.doubleSided = true;
        return e;
    };

    if (sandPlane && sandPlane->gpuBuffers) {
        auto sand = addRenderable("OutdoorBeach_SandShelf", sandPlane,
                                  glm::vec3(0.0f, 0.0f, -2.55f),
                                  glm::vec3(1.0f),
                                  glm::vec3(0.0f),
                                  glm::vec4(0.43f, 0.36f, 0.24f, 1.0f),
                                  0.0f, 0.88f, "sand");
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(sand);
        r.doubleSided = true;
        r.textures.albedoPath = "assets/textures/polyhaven/coast_sand_05/coast_sand_05_diff_1k.jpg";
        r.textures.normalPath = "assets/textures/polyhaven/coast_sand_05/coast_sand_05_nor_gl_1k.jpg";
        r.textures.roughnessPath = "assets/textures/polyhaven/coast_sand_05/coast_sand_05_rough_1k.jpg";
        r.metallic = 0.0f;
        r.normalScale = 0.65f;
        r.wetnessFactor = 0.10f;
        r.proceduralMaskStrength = 0.0f;
    }

    if (waterPlane && waterPlane->gpuBuffers) {
        auto water = addRenderable("OutdoorBeach_TideWater", waterPlane,
                                   glm::vec3(0.0f, -0.035f, 4.45f),
                                   glm::vec3(1.0f),
                                   glm::vec3(0.0f),
                                   glm::vec4(0.025f, 0.22f, 0.18f, 0.94f),
                                   0.0f, 0.045f, "water");
        Scene::WaterSurfaceComponent tide{};
        tide.absorption = 0.56f;
        tide.foamStrength = 0.68f;
        tide.viscosity = 0.16f;
        tide.bodyThickness = 0.44f;
        tide.meniscusStrength = 0.38f;
        tide.flowSpeed = 0.46f;
        tide.shallowTint = glm::vec3(0.15f, 0.38f, 0.25f);
        tide.deepTint = glm::vec3(0.014f, 0.080f, 0.074f);
        m_registry->AddComponent<Scene::WaterSurfaceComponent>(water, tide);
    }


    // --- Real-asset beach dressing -----------------------------------------
    // Rebuilt to use only the loaded naturalistic meshes (no primitive stand-ins).
    // Each prop is measured and ground-snapped so its base sits on the sand
    // (y=0, no floating), kept out of the water (props stay below waterlineZ),
    // and arranged in loose clusters with a clear central sightline to the sea.
    {
        const float waterlineZ = 0.6f; // dry sand below this z; tide water beyond
        (void)waterlineZ;
        auto placeNature = [&](const std::string& tag,
                               const std::shared_ptr<Scene::MeshData>& mesh,
                               float targetSize, float x, float z, float yawDeg,
                               const glm::vec4& color, float roughness) {
            if (!mesh || !mesh->gpuBuffers) {
                return;
            }
            if (!mesh->hasBounds) {
                mesh->UpdateBounds();
            }
            const glm::vec3 ext = glm::max(mesh->boundsMax - mesh->boundsMin, glm::vec3(1e-3f));
            const float horiz = std::max(ext.x, ext.z);
            const float s = (horiz > 1e-3f && targetSize > 0.0f) ? targetSize / horiz : 1.0f;
            const float y = -mesh->boundsMin.y * s; // ground-snap base onto the sand
            addRenderable(tag, mesh, glm::vec3(x, y, z), glm::vec3(s),
                          glm::vec3(0.0f, glm::radians(yawDeg), 0.0f), color, 0.0f, roughness,
                          "naturalistic");
        };

        const glm::vec4 rockCol(0.52f, 0.50f, 0.47f, 1.0f);
        const glm::vec4 woodCol(0.40f, 0.28f, 0.17f, 1.0f);
        const glm::vec4 leafCol(0.28f, 0.40f, 0.19f, 1.0f);
        const glm::vec4 dryCol(0.55f, 0.50f, 0.30f, 1.0f);

        // Rock clusters on the open sand, off to the sides of the sightline.
        placeNature("Beach_Boulder_A", scannedBoulderMesh, 1.7f, -4.7f, -0.3f, 20.0f, rockCol, 0.85f);
        placeNature("Beach_Boulder_B", scannedBoulderMesh, 1.1f, -3.9f, 0.2f, 145.0f, rockCol, 0.85f);
        placeNature("Beach_MossRock_A", scannedMossRockMesh, 1.4f, 4.5f, -0.2f, -30.0f, rockCol, 0.8f);
        placeNature("Beach_MossRock_B", scannedMossRockMesh, 0.9f, 3.6f, 0.4f, 60.0f, rockCol, 0.8f);

        // Driftwood + a stump near the waterline / mid sand.
        placeNature("Beach_Driftwood_A", scannedTrunkMesh, 2.6f, -1.7f, 0.45f, 78.0f, woodCol, 0.8f);
        placeNature("Beach_Branches_A", scannedBranchesMesh, 1.7f, 1.7f, 0.25f, -52.0f, dryCol, 0.85f);
        placeNature("Beach_Stump_A", scannedStumpMesh, 1.0f, -5.6f, -1.5f, 0.0f, woodCol, 0.8f);

        // Foreground pebbles + a driftwood twig near the camera give the empty
        // near-sand depth and a sense of scale leading the eye to the water.
        placeNature("Beach_Pebble_A", scannedMossRockMesh, 0.5f, -3.6f, -1.9f, 30.0f, rockCol, 0.8f);
        placeNature("Beach_Pebble_B", scannedBoulderMesh, 0.38f, -2.9f, -1.4f, 110.0f, rockCol, 0.85f);
        placeNature("Beach_Pebble_C", scannedMossRockMesh, 0.32f, -2.3f, -2.0f, 200.0f, rockCol, 0.8f);
        placeNature("Beach_Driftwood_F", scannedBranchesMesh, 0.85f, -3.9f, -0.9f, 65.0f, dryCol, 0.85f);

        // Dune vegetation in TIGHT CLUMPS (grass + bush overlapping) toward the
        // back/sides so the billboard-ish meshes read as planted bushes rather
        // than a fence of single cards; central foreground stays clear (sightline).
        struct Clump { float x; float z; };
        const Clump clumps[] = {{-5.6f, -1.9f}, {-3.3f, -2.5f}, {3.2f, -2.5f}, {5.6f, -1.8f}};
        int gi = 0;
        for (const auto& cl : clumps) {
            placeNature("Beach_Bush_" + std::to_string(gi++), scannedBushMesh, 1.4f, cl.x, cl.z + 0.15f, 60.0f,
                        leafCol, 0.72f);
            placeNature("Beach_Grass_" + std::to_string(gi++), scannedGrassMesh, 1.0f, cl.x - 0.35f, cl.z, 25.0f,
                        leafCol, 0.7f);
            placeNature("Beach_Grass_" + std::to_string(gi++), scannedGrassMesh, 0.85f, cl.x + 0.4f, cl.z - 0.3f,
                        135.0f, leafCol, 0.7f);
        }
        placeNature("Beach_Fern_A", scannedFernMesh, 1.0f, -4.5f, -2.6f, 25.0f, leafCol, 0.65f);
        placeNature("Beach_Fern_B", scannedFernMesh, 0.9f, 4.4f, -2.6f, -40.0f, leafCol, 0.65f);
    }

    const entt::entity waterDust =
        AddParticleEffect(*m_registry, "Beach_Sunset_WaterDust", "dust", glm::vec3(0.25f, 1.28f, -2.55f));
    ScaleParticleEffect(*m_registry, waterDust, 0.62f, 1.00f, 0.92f);
    const entt::entity skyDust =
        AddParticleEffect(*m_registry, "Beach_Sunset_SkyDust", "dust", glm::vec3(3.35f, 2.70f, 0.60f));
    ScaleParticleEffect(*m_registry, skyDust, 0.42f, 0.92f, 0.82f);
}

void Engine::BuildRecipeScene() {
    std::string recipe = m_recipeName;
    if (const char* env = std::getenv("CORTEX_SCENE_RECIPE"); env && *env) {
        recipe = env;
    }
    spdlog::info("Building scene: procedural recipe '{}'", recipe);

    // Style read from descriptive words in the prompt (modern/rustic/cozy/bright/
    // moody...) modulates the mood so the same room recipe isn't always identical.
    LLM::SceneStyle style;
    if (const char* pr = std::getenv("CORTEX_SCENE_PROMPT"); pr && *pr) {
        style = LLM::ParseSceneStyle(pr);
    }
    if (!style.name.empty() || style.brightness != 0.0f) {
        spdlog::info("Recipe style '{}' warmth={:.2f} brightness={:.2f}",
                     style.name.empty() ? "(brightness)" : style.name, style.warmth, style.brightness);
    }

    const bool outdoor = (recipe == "garden") || (recipe == "generative_exterior");

    // Generative exterior: the IR's "environment" block drives renderer state that
    // scene commands cannot reach (sun, fog, exposure, the animated water surface).
    // Parsed once here; applied after the outdoor defaults below so the IR wins.
    struct GenExtEnv {
        bool valid = false;
        float sunAz = 160.0f, sunEl = 32.0f, sunInt = 2.4f;
        glm::vec3 sunColor{1.0f, 0.94f, 0.82f};
        float fogDensity = 0.0075f, fogStart = 4.0f;
        float exposure = 1.0f;
        bool waterOn = false;
        float waterLevel = 0.05f, waterFromZ = -6.0f, waterRough = 0.055f, waterWave = 0.045f;
        float waterAbsorption = 0.72f, waterFoam = 0.62f, waterFresnel = 0.55f;
        float waterViscosity = 0.48f, waterBodyThickness = 0.80f;
        float waterColorStrength = 0.0f;
        glm::vec3 waterShallow{0.15f, 0.42f, 0.30f}, waterDeep{0.020f, 0.10f, 0.09f};
        float extent = 30.0f;
        std::string groundKind = "grass";
        glm::vec3 groundColor{0.24f, 0.34f, 0.16f};
        bool groundColorSet = false;
        bool terrainHeightfield = false;
        float terrainRelief = 0.0f;
        float terrainMicroRelief = 0.0f;
        uint32_t terrainGrid = 48;
        bool graphicsMaterials = false;
        float groundNormalScale = 0.75f;
        float groundWetness = 0.0f;
        float groundProceduralMask = 0.20f;
        float groundRoughness = 0.92f;
        float graphicsSSAORadius = 0.75f;
        float graphicsSSAOBias = 0.018f;
        float graphicsSSAOIntensity = 1.35f;
        float graphicsShadowBias = 0.0035f;
        float graphicsShadowPCF = 2.5f;
        std::string skyPreset;   // optional IR override: sky_day | sky_sunset | sky_partly_cloudy
        std::string lookTime;
        std::string lookGrade;
        std::vector<GenerativeRidgeLayer> ridgeLayers;
        std::vector<GenerativeStructure> structures;
        std::vector<GenerativeContactPatch> contactPatches;
        GenerativeWorldGeometry worldGeometry;
        GenerativeSurfaceDetail surfaceDetail;
        GenerativeImageContactOcclusion imageContactOcclusion;
        GenerativeSoftOcclusion softOcclusion;
        GenerativeWaterShoreIntegration waterShoreIntegration;
        GenerativeSurfaceMaterialRichness surfaceMaterialRichness;
        GenerativeMeshSilhouetteRealism meshSilhouetteRealism;
        GenerativeNaturalisticEcology naturalisticEcology;
        GenerativeAssetFidelity assetFidelity;
        GenerativeHeroEnvironmentGeometry heroEnvironmentGeometry;
        GenerativeTextureMaterialFidelity textureMaterialFidelity;
        GenerativeSourceGeometryFidelity sourceGeometryFidelity;
        GenerativeRendererShadowOcclusionBudget rendererShadowOcclusionBudget;
        GenerativeCinematicMaterialLighting cinematicMaterialLighting;
        GenerativeAtmosphereFidelity atmosphereFidelity;
        GenerativeGeometryRealism geometryRealism;
        GenerativeAuthoredSceneModule authoredSceneModule;
        int shoreLayerCount = 0;
        int rimLightCount = 0;
        int advancedShaderTermCount = 0;
    } genExt;
    if (recipe == "generative_exterior") {
        if (const char* raw = std::getenv("CORTEX_SCENE_IR_JSON"); raw && *raw) {
            try {
                const nlohmann::json ir = nlohmann::json::parse(raw);
                const nlohmann::json env = ir.value("environment", nlohmann::json::object());
                auto num = [](const nlohmann::json& j, const char* k, float d) -> float {
                    return (j.contains(k) && j[k].is_number()) ? (float)j[k].get<double>() : d;
                };
                auto vec3Of = [](const nlohmann::json& j, const char* k, const glm::vec3& d) -> glm::vec3 {
                    if (!j.contains(k) || !j[k].is_array() || j[k].size() < 3) return d;
                    return glm::vec3((float)j[k][0], (float)j[k][1], (float)j[k][2]);
                };
                const nlohmann::json sun = env.value("sun", nlohmann::json::object());
                genExt.sunAz = num(sun, "azimuth_deg", genExt.sunAz);
                genExt.sunEl = std::clamp(num(sun, "elevation_deg", genExt.sunEl), 3.0f, 80.0f);
                genExt.sunColor = vec3Of(sun, "color", genExt.sunColor);
                genExt.sunInt = std::clamp(num(sun, "intensity", genExt.sunInt), 0.3f, 6.0f);
                const nlohmann::json fog = env.value("fog", nlohmann::json::object());
                genExt.fogDensity = std::clamp(num(fog, "density", genExt.fogDensity), 0.0f, 0.06f);
                genExt.fogStart = std::clamp(num(fog, "start", genExt.fogStart), 0.0f, 30.0f);
                genExt.exposure = std::clamp(num(env, "exposure", 1.0f), 0.4f, 1.8f);
                genExt.skyPreset = env.value("sky", std::string());
                const nlohmann::json look = env.value("look", nlohmann::json::object());
                genExt.lookTime = look.value("time", std::string());
                genExt.lookGrade = look.value("grade", std::string());
                const nlohmann::json ground = env.value("ground", nlohmann::json::object());
                genExt.extent = std::clamp(num(ground, "extent", genExt.extent), 12.0f, 80.0f);
                genExt.groundKind = ground.value("kind", genExt.groundKind);
                if (ground.contains("color") && ground["color"].is_array() && ground["color"].size() >= 3) {
                    genExt.groundColor = vec3Of(ground, "color", genExt.groundColor);
                    genExt.groundColorSet = true;
                }
                const nlohmann::json terrain = ground.value("terrain", nlohmann::json::object());
                genExt.terrainHeightfield = terrain.value("mode", std::string()) == "heightfield";
                genExt.terrainRelief = std::clamp(num(terrain, "relief_m", genExt.terrainRelief), 0.0f, 1.4f);
                genExt.terrainMicroRelief = std::clamp(num(terrain, "micro_relief_m", genExt.terrainMicroRelief), 0.0f, 0.25f);
                genExt.terrainGrid = static_cast<uint32_t>(std::clamp(num(terrain, "grid", static_cast<float>(genExt.terrainGrid)),
                                                                       16.0f, 128.0f));
                const nlohmann::json water = env.value("water", nlohmann::json::object());
                genExt.waterOn = water.value("enabled", false);
                genExt.waterLevel = std::clamp(num(water, "level", genExt.waterLevel), 0.02f, 0.4f);
                genExt.waterFromZ = std::clamp(num(water, "from_z", genExt.waterFromZ),
                                               -genExt.extent * 0.5f, genExt.extent * 0.35f);
                genExt.waterRough = std::clamp(num(water, "roughness", genExt.waterRough), 0.02f, 0.4f);
                genExt.waterWave = std::clamp(num(water, "wave", genExt.waterWave), 0.005f, 0.09f);
                genExt.waterAbsorption = std::clamp(num(water, "absorption", genExt.waterAbsorption), 0.05f, 1.5f);
                genExt.waterFoam = std::clamp(num(water, "foam", genExt.waterFoam), 0.0f, 1.0f);
                genExt.waterFresnel = std::clamp(num(water, "fresnel", genExt.waterFresnel), 0.02f, 1.5f);
                genExt.waterViscosity = std::clamp(num(water, "viscosity", genExt.waterViscosity), 0.0f, 1.0f);
                genExt.waterBodyThickness = std::clamp(num(water, "body_thickness", genExt.waterBodyThickness), 0.10f, 2.0f);
                genExt.waterColorStrength = std::clamp(num(water, "color_strength", genExt.waterColorStrength), 0.0f, 1.0f);
                genExt.waterShallow = vec3Of(water, "shallow", genExt.waterShallow);
                genExt.waterDeep = vec3Of(water, "deep", genExt.waterDeep);
                const nlohmann::json graphics = env.value("graphics_pass", nlohmann::json::object());
                const nlohmann::json graphicsMaterials = graphics.value("materials", nlohmann::json::object());
                genExt.graphicsMaterials = graphicsMaterials.value("enabled", false);
                genExt.groundNormalScale = std::clamp(num(graphicsMaterials, "ground_normal_scale", genExt.groundNormalScale), 0.0f, 1.5f);
                genExt.groundWetness = std::clamp(num(graphicsMaterials, "ground_wetness", genExt.groundWetness), 0.0f, 1.0f);
                genExt.groundProceduralMask = std::clamp(num(graphicsMaterials, "procedural_mask", genExt.groundProceduralMask), 0.0f, 1.0f);
                genExt.groundRoughness = std::clamp(num(graphicsMaterials, "roughness", genExt.groundRoughness), 0.15f, 1.0f);
                const nlohmann::json advancedTerms = graphicsMaterials.value("advanced_shader_terms", nlohmann::json::object());
                genExt.advancedShaderTermCount = 0;
                if (advancedTerms.is_object()) {
                    for (auto it = advancedTerms.begin(); it != advancedTerms.end(); ++it) {
                        if (it.value().is_boolean() && it.value().get<bool>()) {
                            genExt.advancedShaderTermCount++;
                        }
                    }
                }
                const nlohmann::json graphicsRenderer = graphics.value("renderer", nlohmann::json::object());
                genExt.graphicsSSAORadius = std::clamp(num(graphicsRenderer, "ssao_radius", genExt.graphicsSSAORadius), 0.20f, 2.5f);
                genExt.graphicsSSAOIntensity = std::clamp(num(graphicsRenderer, "ssao_intensity", genExt.graphicsSSAOIntensity), 0.5f, 4.5f);
                genExt.graphicsShadowBias = std::clamp(num(graphicsRenderer, "shadow_bias", genExt.graphicsShadowBias), 0.0003f, 0.010f);
                genExt.graphicsShadowPCF = std::clamp(num(graphicsRenderer, "shadow_pcf_radius", genExt.graphicsShadowPCF), 0.25f, 5.0f);
                const nlohmann::json rendererShadowBudget =
                    graphics.value("renderer_shadow_occlusion_budget", nlohmann::json::object());
                genExt.rendererShadowOcclusionBudget.enabled = rendererShadowBudget.value("enabled", false);
                genExt.rendererShadowOcclusionBudget.rendererSSAO = rendererShadowBudget.value("renderer_ssao", false);
                genExt.rendererShadowOcclusionBudget.shadowMaps = rendererShadowBudget.value("shadow_maps", false);
                genExt.rendererShadowOcclusionBudget.dxrRequired = rendererShadowBudget.value("dxr_required", false);
                genExt.rendererShadowOcclusionBudget.ssaoRadius =
                    std::clamp(num(rendererShadowBudget, "ssao_radius", genExt.graphicsSSAORadius), 0.20f, 2.5f);
                genExt.rendererShadowOcclusionBudget.ssaoBias =
                    std::clamp(num(rendererShadowBudget, "ssao_bias", genExt.graphicsSSAOBias), 0.0005f, 0.05f);
                genExt.rendererShadowOcclusionBudget.ssaoIntensity =
                    std::clamp(num(rendererShadowBudget, "ssao_intensity", genExt.graphicsSSAOIntensity), 0.5f, 4.5f);
                genExt.rendererShadowOcclusionBudget.shadowBias =
                    std::clamp(num(rendererShadowBudget, "shadow_bias", genExt.graphicsShadowBias), 0.0003f, 0.010f);
                genExt.rendererShadowOcclusionBudget.shadowPCFRadius =
                    std::clamp(num(rendererShadowBudget, "shadow_pcf_radius", genExt.graphicsShadowPCF), 0.25f, 5.0f);
                genExt.rendererShadowOcclusionBudget.contactReceiverPatchBudget =
                    static_cast<int>(std::clamp(num(rendererShadowBudget, "contact_receiver_patch_budget", 0.0f), 0.0f, 96.0f));
                genExt.rendererShadowOcclusionBudget.softPenumbraPatchBudget =
                    static_cast<int>(std::clamp(num(rendererShadowBudget, "soft_penumbra_patch_budget", 0.0f), 0.0f, 64.0f));
                genExt.rendererShadowOcclusionBudget.rendererContactBlend =
                    std::clamp(num(rendererShadowBudget, "renderer_contact_blend", 0.0f), 0.0f, 1.0f);
                if (genExt.rendererShadowOcclusionBudget.enabled) {
                    genExt.graphicsSSAORadius = genExt.rendererShadowOcclusionBudget.ssaoRadius;
                    genExt.graphicsSSAOBias = genExt.rendererShadowOcclusionBudget.ssaoBias;
                    genExt.graphicsSSAOIntensity = genExt.rendererShadowOcclusionBudget.ssaoIntensity;
                    genExt.graphicsShadowBias = genExt.rendererShadowOcclusionBudget.shadowBias;
                    genExt.graphicsShadowPCF = genExt.rendererShadowOcclusionBudget.shadowPCFRadius;
                }
                const nlohmann::json cinematicMaterialLighting =
                    graphics.value("cinematic_material_lighting", nlohmann::json::object());
                genExt.cinematicMaterialLighting.enabled = cinematicMaterialLighting.value("enabled", false);
                genExt.cinematicMaterialLighting.triplanarDetailLayerCount =
                    static_cast<int>(std::clamp(num(cinematicMaterialLighting, "triplanar_detail_layer_count", 0.0f), 0.0f, 16.0f));
                genExt.cinematicMaterialLighting.terrainReliefPatchCount =
                    static_cast<int>(std::clamp(num(cinematicMaterialLighting, "terrain_relief_patch_count", 0.0f), 0.0f, 48.0f));
                genExt.cinematicMaterialLighting.shadowCasterCount =
                    static_cast<int>(std::clamp(num(cinematicMaterialLighting, "shadow_caster_count", 0.0f), 0.0f, 24.0f));
                genExt.cinematicMaterialLighting.contactReceiverCount =
                    static_cast<int>(std::clamp(num(cinematicMaterialLighting, "contact_receiver_count", 0.0f), 0.0f, 48.0f));
                genExt.cinematicMaterialLighting.localizedLightCount =
                    static_cast<int>(std::clamp(num(cinematicMaterialLighting, "localized_light_count", 0.0f), 0.0f, 6.0f));
                genExt.cinematicMaterialLighting.volumetricLightSliceCount =
                    static_cast<int>(std::clamp(num(cinematicMaterialLighting, "volumetric_light_slice_count", 0.0f), 0.0f, 10.0f));
                genExt.cinematicMaterialLighting.wetRoughnessVariationCount =
                    static_cast<int>(std::clamp(num(cinematicMaterialLighting, "wet_roughness_variation_count", 0.0f), 0.0f, 20.0f));
                genExt.cinematicMaterialLighting.sourceTextureWeight =
                    std::clamp(num(cinematicMaterialLighting, "source_texture_weight", 0.0f), 0.0f, 1.0f);
                genExt.cinematicMaterialLighting.normalDetailScale =
                    std::clamp(num(cinematicMaterialLighting, "normal_detail_scale", 0.0f), 0.0f, 1.5f);
                genExt.cinematicMaterialLighting.roughnessVariation =
                    std::clamp(num(cinematicMaterialLighting, "roughness_variation", 0.0f), 0.0f, 1.0f);
                const nlohmann::json authoredSceneModule =
                    graphics.value("authored_scene_module", nlohmann::json::object());
                genExt.authoredSceneModule.enabled = authoredSceneModule.value("enabled", false);
                genExt.authoredSceneModule.moduleId = authoredSceneModule.value("module_id", std::string());
                genExt.authoredSceneModule.contrastKey = authoredSceneModule.value("contrast_key", std::string());
                genExt.authoredSceneModule.compositionAnchorCount =
                    static_cast<int>(std::clamp(num(authoredSceneModule, "composition_anchor_count", 0.0f), 0.0f, 16.0f));
                genExt.authoredSceneModule.terrainSetpieceCount =
                    static_cast<int>(std::clamp(num(authoredSceneModule, "terrain_setpiece_count", 0.0f), 0.0f, 14.0f));
                genExt.authoredSceneModule.heroClusterCount =
                    static_cast<int>(std::clamp(num(authoredSceneModule, "hero_cluster_count", 0.0f), 0.0f, 8.0f));
                genExt.authoredSceneModule.foregroundFrameCount =
                    static_cast<int>(std::clamp(num(authoredSceneModule, "foreground_frame_count", 0.0f), 0.0f, 10.0f));
                genExt.authoredSceneModule.backdropGateCount =
                    static_cast<int>(std::clamp(num(authoredSceneModule, "backdrop_gate_count", 0.0f), 0.0f, 8.0f));
                genExt.authoredSceneModule.lightingZoneCount =
                    static_cast<int>(std::clamp(num(authoredSceneModule, "lighting_zone_count", 0.0f), 0.0f, 8.0f));
                genExt.authoredSceneModule.materialFamilyCount =
                    static_cast<int>(std::clamp(num(authoredSceneModule, "material_family_count", 0.0f), 0.0f, 12.0f));
                genExt.authoredSceneModule.waterShapeSegmentCount =
                    static_cast<int>(std::clamp(num(authoredSceneModule, "water_shape_segment_count", 0.0f), 0.0f, 18.0f));
                genExt.authoredSceneModule.practicalLightCount =
                    static_cast<int>(std::clamp(num(authoredSceneModule, "practical_light_count", 0.0f), 0.0f, 8.0f));
                const nlohmann::json worldGeometry = graphics.value("world_geometry", nlohmann::json::object());
                genExt.worldGeometry.enabled = worldGeometry.value("enabled", false);
                genExt.worldGeometry.foregroundOccluderCount =
                    static_cast<int>(std::clamp(num(worldGeometry, "foreground_occluder_count", 0.0f), 0.0f, 12.0f));
                genExt.worldGeometry.canyonWallLayers =
                    static_cast<int>(std::clamp(num(worldGeometry, "canyon_wall_layers", 0.0f), 0.0f, 12.0f));
                genExt.worldGeometry.talusClusterCount =
                    static_cast<int>(std::clamp(num(worldGeometry, "talus_cluster_count", 0.0f), 0.0f, 36.0f));
                genExt.worldGeometry.redRockStrataLayers =
                    static_cast<int>(std::clamp(num(worldGeometry, "red_rock_strata_layers", 0.0f), 0.0f, 20.0f));
                genExt.worldGeometry.shorelineSegmentCount =
                    static_cast<int>(std::clamp(num(worldGeometry, "shoreline_segment_count", 0.0f), 0.0f, 8.0f));
                genExt.worldGeometry.depthBandCount =
                    static_cast<int>(std::clamp(num(worldGeometry, "depth_band_count", 0.0f), 0.0f, 8.0f));
                genExt.worldGeometry.canyonWidthM = std::clamp(num(worldGeometry, "canyon_width_m", 0.0f), 0.0f, 42.0f);
                genExt.worldGeometry.wallHeightM = std::clamp(num(worldGeometry, "wall_height_m", 0.0f), 0.0f, 22.0f);
                const nlohmann::json graphicsLighting = graphics.value("lighting", nlohmann::json::object());
                genExt.rimLightCount = static_cast<int>(std::clamp(num(graphicsLighting, "rim_light_count", 0.0f), 0.0f, 4.0f));
                const nlohmann::json surfaceDetail = graphics.value("surface_detail", nlohmann::json::object());
                genExt.surfaceDetail.enabled = surfaceDetail.value("enabled", false);
                genExt.surfaceDetail.pebbleCount =
                    static_cast<int>(std::clamp(num(surfaceDetail, "pebble_count", 0.0f), 0.0f, 80.0f));
                genExt.surfaceDetail.terrainCreaseCount =
                    static_cast<int>(std::clamp(num(surfaceDetail, "terrain_crease_count", 0.0f), 0.0f, 18.0f));
                genExt.surfaceDetail.shoreFoamSegmentCount =
                    static_cast<int>(std::clamp(num(surfaceDetail, "shore_foam_segment_count", 0.0f), 0.0f, 16.0f));
                genExt.surfaceDetail.wetGlintCount =
                    static_cast<int>(std::clamp(num(surfaceDetail, "wet_glint_count", 0.0f), 0.0f, 16.0f));
                const nlohmann::json surfaceMaterialRichness = graphics.value("surface_material_richness", nlohmann::json::object());
                genExt.surfaceMaterialRichness.enabled = surfaceMaterialRichness.value("enabled", false);
                genExt.surfaceMaterialRichness.groundDecalCount =
                    static_cast<int>(std::clamp(num(surfaceMaterialRichness, "ground_decal_count", 0.0f), 0.0f, 36.0f));
                genExt.surfaceMaterialRichness.rockLichenPatchCount =
                    static_cast<int>(std::clamp(num(surfaceMaterialRichness, "rock_lichen_patch_count", 0.0f), 0.0f, 28.0f));
                genExt.surfaceMaterialRichness.desertStrataPatchCount =
                    static_cast<int>(std::clamp(num(surfaceMaterialRichness, "desert_strata_patch_count", 0.0f), 0.0f, 32.0f));
                genExt.surfaceMaterialRichness.vegetationClusterCount =
                    static_cast<int>(std::clamp(num(surfaceMaterialRichness, "vegetation_cluster_count", 0.0f), 0.0f, 32.0f));
                genExt.surfaceMaterialRichness.heroMaterialLineCount =
                    static_cast<int>(std::clamp(num(surfaceMaterialRichness, "hero_material_line_count", 0.0f), 0.0f, 48.0f));
                const nlohmann::json meshSilhouetteRealism = graphics.value("mesh_silhouette_realism", nlohmann::json::object());
                genExt.meshSilhouetteRealism.enabled = meshSilhouetteRealism.value("enabled", false);
                genExt.meshSilhouetteRealism.cliffMeshVerticalBands =
                    static_cast<int>(std::clamp(num(meshSilhouetteRealism, "cliff_mesh_vertical_bands", 0.0f), 0.0f, 10.0f));
                genExt.meshSilhouetteRealism.cliffOverhangCount =
                    static_cast<int>(std::clamp(num(meshSilhouetteRealism, "cliff_overhang_count", 0.0f), 0.0f, 24.0f));
                genExt.meshSilhouetteRealism.heroBevelDetailCount =
                    static_cast<int>(std::clamp(num(meshSilhouetteRealism, "hero_bevel_detail_count", 0.0f), 0.0f, 48.0f));
                genExt.meshSilhouetteRealism.propDepthLayerCount =
                    static_cast<int>(std::clamp(num(meshSilhouetteRealism, "prop_depth_layer_count", 0.0f), 0.0f, 24.0f));
                const nlohmann::json naturalisticEcology = graphics.value("naturalistic_ecology", nlohmann::json::object());
                genExt.naturalisticEcology.enabled = naturalisticEcology.value("enabled", false);
                genExt.naturalisticEcology.grassClusterCount =
                    static_cast<int>(std::clamp(num(naturalisticEcology, "grass_cluster_count", 0.0f), 0.0f, 24.0f));
                genExt.naturalisticEcology.bushClusterCount =
                    static_cast<int>(std::clamp(num(naturalisticEcology, "bush_cluster_count", 0.0f), 0.0f, 16.0f));
                genExt.naturalisticEcology.fernClusterCount =
                    static_cast<int>(std::clamp(num(naturalisticEcology, "fern_cluster_count", 0.0f), 0.0f, 16.0f));
                genExt.naturalisticEcology.trunkCount =
                    static_cast<int>(std::clamp(num(naturalisticEcology, "trunk_count", 0.0f), 0.0f, 10.0f));
                genExt.naturalisticEcology.branchCount =
                    static_cast<int>(std::clamp(num(naturalisticEcology, "branch_count", 0.0f), 0.0f, 14.0f));
                genExt.naturalisticEcology.stumpCount =
                    static_cast<int>(std::clamp(num(naturalisticEcology, "stump_count", 0.0f), 0.0f, 8.0f));
                genExt.naturalisticEcology.mossRockCount =
                    static_cast<int>(std::clamp(num(naturalisticEcology, "moss_rock_count", 0.0f), 0.0f, 12.0f));
                const nlohmann::json assetFidelity = graphics.value("asset_fidelity", nlohmann::json::object());
                genExt.assetFidelity.enabled = assetFidelity.value("enabled", false);
                genExt.assetFidelity.heroDetailCount =
                    static_cast<int>(std::clamp(num(assetFidelity, "hero_detail_count", 0.0f), 0.0f, 80.0f));
                genExt.assetFidelity.campDetailCount =
                    static_cast<int>(std::clamp(num(assetFidelity, "camp_detail_count", 0.0f), 0.0f, 48.0f));
                genExt.assetFidelity.cabinFacadeDetailCount =
                    static_cast<int>(std::clamp(num(assetFidelity, "cabin_facade_detail_count", 0.0f), 0.0f, 64.0f));
                genExt.assetFidelity.backdropDetailLayers =
                    static_cast<int>(std::clamp(num(assetFidelity, "backdrop_detail_layers", 0.0f), 0.0f, 10.0f));
                genExt.assetFidelity.foregroundDressingClusters =
                    static_cast<int>(std::clamp(num(assetFidelity, "foreground_dressing_clusters", 0.0f), 0.0f, 16.0f));
                const nlohmann::json heroEnvironmentGeometry = graphics.value("hero_environment_geometry", nlohmann::json::object());
                genExt.heroEnvironmentGeometry.enabled = heroEnvironmentGeometry.value("enabled", false);
                genExt.heroEnvironmentGeometry.highDetailCampPieceCount =
                    static_cast<int>(std::clamp(num(heroEnvironmentGeometry, "high_detail_camp_piece_count", 0.0f), 0.0f, 80.0f));
                genExt.heroEnvironmentGeometry.highDetailCabinPieceCount =
                    static_cast<int>(std::clamp(num(heroEnvironmentGeometry, "high_detail_cabin_piece_count", 0.0f), 0.0f, 80.0f));
                genExt.heroEnvironmentGeometry.mountainMassLayerCount =
                    static_cast<int>(std::clamp(num(heroEnvironmentGeometry, "mountain_mass_layer_count", 0.0f), 0.0f, 12.0f));
                genExt.heroEnvironmentGeometry.cliffMassPieceCount =
                    static_cast<int>(std::clamp(num(heroEnvironmentGeometry, "cliff_mass_piece_count", 0.0f), 0.0f, 32.0f));
                genExt.heroEnvironmentGeometry.shorelinePropCount =
                    static_cast<int>(std::clamp(num(heroEnvironmentGeometry, "shoreline_prop_count", 0.0f), 0.0f, 28.0f));
                genExt.heroEnvironmentGeometry.irregularTreeSilhouetteCount =
                    static_cast<int>(std::clamp(num(heroEnvironmentGeometry, "irregular_tree_silhouette_count", 0.0f), 0.0f, 24.0f));
                genExt.heroEnvironmentGeometry.supportPropCount =
                    static_cast<int>(std::clamp(num(heroEnvironmentGeometry, "support_prop_count", 0.0f), 0.0f, 24.0f));
                const nlohmann::json textureMaterialFidelity = graphics.value("texture_material_fidelity", nlohmann::json::object());
                genExt.textureMaterialFidelity.enabled = textureMaterialFidelity.value("enabled", false);
                genExt.textureMaterialFidelity.textureSetCount =
                    static_cast<int>(std::clamp(num(textureMaterialFidelity, "texture_set_count", 0.0f), 0.0f, 16.0f));
                genExt.textureMaterialFidelity.terrainSurfaceCount =
                    static_cast<int>(std::clamp(num(textureMaterialFidelity, "terrain_surface_count", 0.0f), 0.0f, 64.0f));
                genExt.textureMaterialFidelity.rockSurfaceCount =
                    static_cast<int>(std::clamp(num(textureMaterialFidelity, "rock_surface_count", 0.0f), 0.0f, 128.0f));
                genExt.textureMaterialFidelity.woodSurfaceCount =
                    static_cast<int>(std::clamp(num(textureMaterialFidelity, "wood_surface_count", 0.0f), 0.0f, 128.0f));
                genExt.textureMaterialFidelity.fabricSurfaceCount =
                    static_cast<int>(std::clamp(num(textureMaterialFidelity, "fabric_surface_count", 0.0f), 0.0f, 96.0f));
                genExt.textureMaterialFidelity.heroSurfaceCount =
                    static_cast<int>(std::clamp(num(textureMaterialFidelity, "hero_surface_count", 0.0f), 0.0f, 160.0f));
                genExt.textureMaterialFidelity.shoreSurfaceCount =
                    static_cast<int>(std::clamp(num(textureMaterialFidelity, "shore_surface_count", 0.0f), 0.0f, 64.0f));
                const nlohmann::json sourceGeometryFidelity = graphics.value("source_geometry_fidelity", nlohmann::json::object());
                genExt.sourceGeometryFidelity.enabled = sourceGeometryFidelity.value("enabled", false);
                genExt.sourceGeometryFidelity.sourceAssetSetCount =
                    static_cast<int>(std::clamp(num(sourceGeometryFidelity, "source_asset_set_count", 0.0f), 0.0f, 12.0f));
                genExt.sourceGeometryFidelity.scannedLanternCount =
                    static_cast<int>(std::clamp(num(sourceGeometryFidelity, "scanned_lantern_count", 0.0f), 0.0f, 6.0f));
                genExt.sourceGeometryFidelity.scannedUtilityPropCount =
                    static_cast<int>(std::clamp(num(sourceGeometryFidelity, "scanned_utility_prop_count", 0.0f), 0.0f, 8.0f));
                genExt.sourceGeometryFidelity.scannedAnchorRockCount =
                    static_cast<int>(std::clamp(num(sourceGeometryFidelity, "scanned_anchor_rock_count", 0.0f), 0.0f, 10.0f));
                genExt.sourceGeometryFidelity.heroAnchorCount =
                    static_cast<int>(std::clamp(num(sourceGeometryFidelity, "hero_anchor_count", 0.0f), 0.0f, 16.0f));
                const nlohmann::json atmosphereFidelity = graphics.value("atmosphere_fidelity", nlohmann::json::object());
                genExt.atmosphereFidelity.enabled = atmosphereFidelity.value("enabled", false);
                genExt.atmosphereFidelity.nightSkyControl = atmosphereFidelity.value("night_sky_control", false);
                genExt.atmosphereFidelity.stormLayerCount =
                    static_cast<int>(std::clamp(num(atmosphereFidelity, "storm_layer_count", 0.0f), 0.0f, 8.0f));
                genExt.atmosphereFidelity.rainStreakCount =
                    static_cast<int>(std::clamp(num(atmosphereFidelity, "rain_streak_count", 0.0f), 0.0f, 80.0f));
                genExt.atmosphereFidelity.hazeDepthLayers =
                    static_cast<int>(std::clamp(num(atmosphereFidelity, "haze_depth_layers", 0.0f), 0.0f, 8.0f));
                genExt.atmosphereFidelity.moonlightExposure =
                    std::clamp(num(atmosphereFidelity, "moonlight_exposure", 0.0f), 0.0f, 1.5f);
                genExt.atmosphereFidelity.skyBackgroundLift =
                    std::clamp(num(atmosphereFidelity, "sky_background_lift", 1.0f), 0.05f, 4.0f);
                const nlohmann::json geometryRealism = graphics.value("geometry_realism", nlohmann::json::object());
                genExt.geometryRealism.enabled = geometryRealism.value("enabled", false);
                genExt.geometryRealism.cliffErosionRidgeCount =
                    static_cast<int>(std::clamp(num(geometryRealism, "cliff_erosion_ridge_count", 0.0f), 0.0f, 64.0f));
                genExt.geometryRealism.strataBreakupCount =
                    static_cast<int>(std::clamp(num(geometryRealism, "strata_breakup_count", 0.0f), 0.0f, 48.0f));
                genExt.geometryRealism.foregroundReliefClusters =
                    static_cast<int>(std::clamp(num(geometryRealism, "foreground_relief_clusters", 0.0f), 0.0f, 24.0f));
                genExt.geometryRealism.wallNormalBreakup =
                    std::clamp(num(geometryRealism, "wall_normal_breakup", 0.0f), 0.0f, 1.0f);
                const nlohmann::json occlusion = graphics.value("occlusion", nlohmann::json::object());
                genExt.surfaceDetail.occlusionRibbonCount =
                    static_cast<int>(std::clamp(num(occlusion, "ground_shadow_ribbon_count", 0.0f), 0.0f, 18.0f));
                genExt.surfaceDetail.contactShadowStrength =
                    std::clamp(num(occlusion, "contact_shadow_strength", 0.0f), 0.0f, 1.0f);
                const nlohmann::json imageContactOcclusion = graphics.value("image_contact_occlusion", nlohmann::json::object());
                genExt.imageContactOcclusion.enabled = imageContactOcclusion.value("enabled", false);
                genExt.imageContactOcclusion.deepContactPatchCount =
                    static_cast<int>(std::clamp(num(imageContactOcclusion, "deep_contact_patch_count", 0.0f), 0.0f, 48.0f));
                genExt.imageContactOcclusion.targetDarkContactFraction =
                    std::clamp(num(imageContactOcclusion, "target_dark_contact_fraction", 0.002f), 0.0f, 0.05f);
                const nlohmann::json softOcclusion = graphics.value("soft_occlusion", nlohmann::json::object());
                genExt.softOcclusion.enabled = softOcclusion.value("enabled", false);
                genExt.softOcclusion.penumbraPatchCount =
                    static_cast<int>(std::clamp(num(softOcclusion, "penumbra_patch_count", 0.0f), 0.0f, 48.0f));
                genExt.softOcclusion.contactGradientLayerCount =
                    static_cast<int>(std::clamp(num(softOcclusion, "contact_gradient_layer_count", 0.0f), 0.0f, 5.0f));
                genExt.softOcclusion.heroAnchorCount =
                    static_cast<int>(std::clamp(num(softOcclusion, "hero_anchor_count", 0.0f), 0.0f, 20.0f));
                genExt.softOcclusion.targetSoftContactFraction =
                    std::clamp(num(softOcclusion, "target_soft_contact_fraction", 0.010f), 0.0f, 0.08f);
                const nlohmann::json waterShoreIntegration = graphics.value("water_shore_integration", nlohmann::json::object());
                genExt.waterShoreIntegration.enabled = waterShoreIntegration.value("enabled", false);
                genExt.waterShoreIntegration.foamLaceSegmentCount =
                    static_cast<int>(std::clamp(num(waterShoreIntegration, "foam_lace_segment_count", 0.0f), 0.0f, 32.0f));
                genExt.waterShoreIntegration.shorelineRippleCount =
                    static_cast<int>(std::clamp(num(waterShoreIntegration, "shoreline_ripple_count", 0.0f), 0.0f, 32.0f));
                genExt.waterShoreIntegration.wetlineBandCount =
                    static_cast<int>(std::clamp(num(waterShoreIntegration, "wetline_band_count", 0.0f), 0.0f, 8.0f));
                genExt.waterShoreIntegration.reflectionGlintCount =
                    static_cast<int>(std::clamp(num(waterShoreIntegration, "reflection_glint_count", 0.0f), 0.0f, 24.0f));
                genExt.waterShoreIntegration.submergedEdgeRockCount =
                    static_cast<int>(std::clamp(num(waterShoreIntegration, "submerged_edge_rock_count", 0.0f), 0.0f, 16.0f));
                const nlohmann::json contact = graphics.value("contact", nlohmann::json::object());
                genExt.shoreLayerCount = static_cast<int>(std::clamp(num(contact, "shore_layer_count", 0.0f), 0.0f, 8.0f));
                for (const auto& patchJson : contact.value("patches", nlohmann::json::array())) {
                    if (!patchJson.is_object()) {
                        continue;
                    }
                    GenerativeContactPatch patch{};
                    patch.position.x = std::clamp(num(patchJson, "x", 0.0f), -genExt.extent * 0.55f, genExt.extent * 0.55f);
                    patch.position.y = std::clamp(num(patchJson, "z", 0.0f), -genExt.extent * 0.6f, genExt.extent * 0.35f);
                    patch.radius = std::clamp(num(patchJson, "radius", patch.radius), 0.2f, 3.2f);
                    patch.darkness = std::clamp(num(patchJson, "darkness", patch.darkness), 0.0f, 1.0f);
                    patch.wetness = std::clamp(num(patchJson, "wetness", patch.wetness), 0.0f, 1.0f);
                    genExt.contactPatches.push_back(patch);
                    if (genExt.contactPatches.size() >= 40) {
                        break;
                    }
                }
                const nlohmann::json background = env.value("background", nlohmann::json::object());
                const nlohmann::json ridgeLayers = background.value("ridge_layers", nlohmann::json::array());
                for (const auto& layer : ridgeLayers) {
                    if (!layer.is_object()) {
                        continue;
                    }
                    GenerativeRidgeLayer ridge{};
                    ridge.distanceM = std::clamp(num(layer, "distance_m", ridge.distanceM), 24.0f, 180.0f);
                    ridge.heightM = std::clamp(num(layer, "height_m", ridge.heightM), 4.0f, 42.0f);
                    ridge.color = vec3Of(layer, "color", ridge.color);
                    genExt.ridgeLayers.push_back(ridge);
                }
                const nlohmann::json structures = env.value("structures", nlohmann::json::array());
                for (const auto& item : structures) {
                    if (!item.is_object()) {
                        continue;
                    }
                    GenerativeStructure structure{};
                    structure.type = item.value("type", structure.type);
                    if (structure.type != "cabin") {
                        continue;
                    }
                    structure.position.x = std::clamp(num(item, "x", structure.position.x),
                                                      -genExt.extent * 0.45f, genExt.extent * 0.45f);
                    structure.position.y = std::clamp(num(item, "y", structure.position.y), -0.2f, 2.0f);
                    structure.position.z = std::clamp(num(item, "z", structure.position.z),
                                                      -genExt.extent * 0.28f, genExt.extent * 0.35f);
                    structure.yawDeg = num(item, "yaw_deg", structure.yawDeg);
                    structure.widthM = std::clamp(num(item, "width_m", structure.widthM), 2.0f, 6.0f);
                    structure.depthM = std::clamp(num(item, "depth_m", structure.depthM), 1.8f, 5.0f);
                    structure.wallHeightM = std::clamp(num(item, "wall_height_m", structure.wallHeightM), 1.4f, 3.0f);
                    structure.roofHeightM = std::clamp(num(item, "roof_height_m", structure.roofHeightM), 0.45f, 1.8f);
                    structure.litWindows = item.value("lit_windows", structure.litWindows);
                    genExt.structures.push_back(structure);
                }
                genExt.valid = true;
            } catch (const std::exception& e) {
                spdlog::warn("generative_exterior: bad IR json for environment: {}", e.what());
            }
        }
    }
    GenerativeTextureMaterialRuntimeCounts textureMaterialCounts;
    auto applyGeneratedTextureMaterial = [&](Scene::RenderableComponent& r,
                                             const char* materialClass,
                                             bool heroSurface = false,
                                             bool shoreSurface = false) {
        const std::string cls = materialClass ? materialClass : "";
        auto applySet = [&](const char* setName,
                            const char* albedo,
                            const char* normal,
                            const char* roughness,
                            float normalScale,
                            float occlusion = 0.74f) {
            textureMaterialCounts.sets.insert(setName);
            r.textures.albedoPath = albedo;
            r.textures.normalPath = normal;
            r.textures.roughnessPath = roughness;
            r.normalScale = std::max(r.normalScale, normalScale);
            r.occlusionStrength = std::min(r.occlusionStrength, occlusion);
            r.proceduralMaskStrength = std::max(r.proceduralMaskStrength, 0.36f);
        };

        if (cls == "terrain_sand") {
            applySet("polyhaven/aerial_beach_01",
                     "assets/textures/polyhaven/aerial_beach_01/aerial_beach_01_diff_1k.jpg",
                     "assets/textures/polyhaven/aerial_beach_01/aerial_beach_01_nor_gl_1k.jpg",
                     "assets/textures/polyhaven/aerial_beach_01/aerial_beach_01_rough_1k.jpg",
                     0.66f);
            textureMaterialCounts.terrain++;
        } else if (cls == "terrain_shore") {
            applySet("polyhaven/coast_sand_05",
                     "assets/textures/polyhaven/coast_sand_05/coast_sand_05_diff_1k.jpg",
                     "assets/textures/polyhaven/coast_sand_05/coast_sand_05_nor_gl_1k.jpg",
                     "assets/textures/polyhaven/coast_sand_05/coast_sand_05_rough_1k.jpg",
                     0.72f,
                     0.66f);
            textureMaterialCounts.terrain++;
            textureMaterialCounts.shore++;
        } else if (cls == "terrain_grass" || cls == "terrain_rock") {
            applySet("polyhaven/aerial_grass_rock",
                     "assets/textures/polyhaven/aerial_grass_rock/aerial_grass_rock_diff_1k.jpg",
                     "assets/textures/polyhaven/aerial_grass_rock/aerial_grass_rock_nor_gl_1k.jpg",
                     "assets/textures/polyhaven/aerial_grass_rock/aerial_grass_rock_rough_1k.jpg",
                     cls == "terrain_rock" ? 0.78f : 0.74f);
            textureMaterialCounts.terrain++;
        } else if (cls == "rock" || cls == "rock_cliff") {
            const bool mossy = cls == "rock";
            applySet(mossy ? "naturalistic/rock_moss_set_01" : "naturalistic/boulder_01",
                     mossy
                         ? "assets/models/naturalistic_showcase/rock_moss_set_01/textures/rock_moss_set_01_diff_1k.jpg"
                         : "assets/models/naturalistic_showcase/boulder_01/textures/boulder_01_diff_1k.jpg",
                     mossy
                         ? "assets/models/naturalistic_showcase/rock_moss_set_01/textures/rock_moss_set_01_nor_gl_1k.jpg"
                         : "assets/models/naturalistic_showcase/boulder_01/textures/boulder_01_nor_gl_1k.jpg",
                     mossy
                         ? "assets/models/naturalistic_showcase/rock_moss_set_01/textures/rock_moss_set_01_rough_1k.jpg"
                         : "assets/models/naturalistic_showcase/boulder_01/textures/boulder_01_arm_1k.jpg",
                     cls == "rock_cliff" ? 0.86f : 0.72f,
                     0.60f);
            if (!mossy) {
                r.textures.metallicPath = "assets/models/naturalistic_showcase/boulder_01/textures/boulder_01_arm_1k.jpg";
                r.textures.occlusionPath = "assets/models/naturalistic_showcase/boulder_01/textures/boulder_01_arm_1k.jpg";
            }
            textureMaterialCounts.rock++;
        } else if (cls == "wood" || cls == "driftwood") {
            if (cls == "driftwood") {
                applySet("naturalistic/dead_tree_trunk",
                         "assets/models/naturalistic_showcase/dead_tree_trunk/textures/dead_tree_trunk_diff_1k.jpg",
                         "assets/models/naturalistic_showcase/dead_tree_trunk/textures/dead_tree_trunk_nor_gl_1k.jpg",
                         "assets/models/naturalistic_showcase/dead_tree_trunk/textures/dead_tree_trunk_arm_1k.jpg",
                         0.64f,
                         0.64f);
                r.textures.metallicPath = "assets/models/naturalistic_showcase/dead_tree_trunk/textures/dead_tree_trunk_arm_1k.jpg";
                r.textures.occlusionPath = "assets/models/naturalistic_showcase/dead_tree_trunk/textures/dead_tree_trunk_arm_1k.jpg";
            } else {
                applySet("polyhaven/wood_floor_deck",
                         "assets/textures/polyhaven/wood_floor_deck/wood_floor_deck_diff_1k.jpg",
                         "assets/textures/polyhaven/wood_floor_deck/wood_floor_deck_nor_gl_1k.jpg",
                         "assets/textures/polyhaven/wood_floor_deck/wood_floor_deck_rough_1k.jpg",
                         0.58f,
                         0.68f);
            }
            textureMaterialCounts.wood++;
        } else if (cls == "fabric") {
            applySet("polyhaven/plastered_wall",
                     "assets/textures/polyhaven/plastered_wall/plastered_wall_diff_1k.jpg",
                     "assets/textures/polyhaven/plastered_wall/plastered_wall_nor_gl_1k.jpg",
                     "assets/textures/polyhaven/plastered_wall/plastered_wall_rough_1k.jpg",
                     0.46f,
                     0.72f);
            textureMaterialCounts.fabric++;
            r.sheenWeight = std::max(r.sheenWeight, 0.18f);
        }

        if (heroSurface) {
            textureMaterialCounts.hero++;
        }
        if (shoreSurface) {
            textureMaterialCounts.shore++;
        }
    };
    const RecipeLightingBalance lightingBalance = RecipeLightingBalanceFor(recipe);
    if (auto* renderer = m_renderer.get()) {
        ApplyRecipeVisualContract(renderer, recipe);
        renderer->SetEnvironmentPreset("neutral_procedural");
        renderer->SetIBLEnabled(true);
        // Outdoor: the SUN is the key light; keep the (gray neutral) IBL modest so
        // it doesn't flood the scene flat-gray, and use a gentle sky-blue fill.
        const float iblBase = outdoor ? 0.45f : 0.30f;
        const float ibl = outdoor
            ? std::clamp(iblBase + style.brightness * 0.25f, 0.2f, 1.3f)
            : std::clamp(iblBase + style.brightness * 0.08f, 0.16f, 0.45f);
        renderer->SetIBLIntensity(ibl, outdoor ? ibl : ibl * 0.62f);
        renderer->SetLocalReflectionProbeRadiance(true,
                                                   (outdoor ? 0.16f : 0.16f) * lightingBalance.localProbeDiffuseScale,
                                                   (outdoor ? 0.16f : 0.15f) * lightingBalance.localProbeSpecularScale);
        if (outdoor) {
            renderer->SetBackgroundPresentation(true, 0.95f, 0.0f);
        } else {
            renderer->SetBackgroundPresentation(true, 0.58f, 0.02f);
        }
        const RecipeMoodGrade moodGrade = RecipeMoodGradeFor(style, outdoor);
        // Warmth shifts the ambient toward warm/cool; brightness scales the fill.
        glm::vec3 amb = outdoor ? glm::vec3(0.24f, 0.27f, 0.31f) : glm::vec3(0.19f, 0.18f, 0.16f);
        amb.r += style.warmth * 0.06f;
        amb.b -= style.warmth * 0.06f;
        amb *= (1.0f + style.brightness * (outdoor ? 0.35f : 0.12f));
        renderer->SetAmbientLighting(glm::max(amb, glm::vec3(outdoor ? 0.05f : 0.06f)),
                                     (outdoor ? 1.0f : 0.76f) * lightingBalance.ambientScale);
        const float recipeExposure =
            std::clamp((outdoor ? 0.68f : 0.78f) + style.brightness * 0.05f + moodGrade.exposureOffset,
                       0.52f,
                       outdoor ? 1.0f : 0.98f);
        renderer->SetExposure(std::clamp(recipeExposure * lightingBalance.exposureScale,
                                         0.45f,
                                         outdoor ? 1.0f : 0.98f));
        renderer->SetAutoExposureEnabled(true); // recipe default; the night block disables it for a fixed dark look
        renderer->SetColorGrade(moodGrade.warm, moodGrade.cool);
        renderer->SetToneGrade(moodGrade.contrast, moodGrade.saturation);
        renderer->SetCinematicPostEnabled(true);
        renderer->SetCinematicPost(moodGrade.vignette, 0.0f);
        // Showcase variant (CORTEX_SHOWCASE): a low golden-hour sun rakes through the
        // big window for dramatic long shadows + real volumetric shafts.
        const bool showcase = []{ const char* v = std::getenv("CORTEX_SHOWCASE"); return v && v[0] && v[0] != '0'; }();
        const bool night = showcase && []{ const char* v = std::getenv("CORTEX_SHOWCASE_NIGHT"); return v && v[0] && v[0] != '0'; }();
        const glm::vec3 interiorSunDir = showcase ? glm::vec3(-0.62f, 0.26f, -0.44f)  // low, raking through the window
                                                  : glm::vec3(-0.42f, 0.74f, -0.52f); // high overhead key
        renderer->SetSunDirection(glm::normalize(outdoor ? glm::vec3(-0.59f, 0.05f, -0.79f) : interiorSunDir));
        if (outdoor) {
            renderer->SetSunColor(glm::vec3(1.0f, 0.95f, 0.86f)); // warm daylight
            renderer->SetSunIntensity(1.9f * lightingBalance.sunScale);
        } else {
            renderer->SetSunColor(showcase ? glm::vec3(1.0f, 0.80f, 0.52f)   // warm golden-hour
                                           : glm::vec3(1.0f, 0.88f, 0.68f));
            renderer->SetSunIntensity(std::clamp((showcase ? 4.7f : 4.25f) + style.brightness * 0.20f, 3.3f, 5.2f) *
                                      lightingBalance.sunScale);
        }
        renderer->SetShadowBias(outdoor ? 0.0035f : 0.0008f);
        renderer->SetShadowPCFRadius(outdoor ? 2.5f : 0.82f);
        // Run the recipe scenes through the FULL-quality path (scene presets
        // otherwise default to 0.85 render scale + IBL off): full-res, TAA,
        // screen-space reflections + AO. Re-asserted last so no profile undoes it.
        // Showcase stills supersample at 1.5x (internal 1920x1080 -> resolved to the
        // swapchain = SSAA): sharper geometry edges + reduced aliasing on the hero
        // capture. The 1.5 cap is the engine's existing render-scale ceiling; the
        // budget planner still clamps down if VRAM-limited. Perf is a non-issue for a
        // one-frame still. Interactive/standard scenes stay at native 1.0.
        // CORTEX_RENDER_SCALE overrides the showcase 1.5x SSAA -- interactive runs set
        // 1.0 (via render_ir.ps1 -Fast) so a generative render doesn't freeze the
        // desktop; battery/hero captures keep the full-quality default.
        float showcaseScale = 1.5f;
        if (const char* rs = std::getenv("CORTEX_RENDER_SCALE"); rs && *rs) {
            char* end = nullptr;
            const float v = std::strtof(rs, &end);
            if (end != rs && std::isfinite(v)) {
                showcaseScale = std::clamp(v, 0.5f, 1.5f);
            }
        }
        renderer->SetRenderScale(showcase ? showcaseScale : 1.0f);
        renderer->SetTAAEnabled(true);
        renderer->SetSSREnabled(true);
        renderer->SetSSAOEnabled(true);
        renderer->SetSSAOParams((outdoor ? 0.75f : 1.18f) * lightingBalance.ssaoScale,
                                outdoor ? 0.020f : 0.012f,
                                outdoor ? 1.35f : 3.10f);
        renderer->SetShadowsEnabled(true);
        renderer->SetFogEnabled(true);
        renderer->SetFogParams(outdoor ? 0.0075f : (showcase ? 0.085f : 0.016f), outdoor ? 0.05f : 0.15f, outdoor ? 0.34f : 0.42f, outdoor ? 4.0f : 0.0f);
        renderer->SetGodRayIntensity(outdoor ? 0.0f : (showcase ? 0.78f : 0.40f));
        if (night) {
            // NIGHT showcase: kill the daytime key and let the warm lamps (point lights)
            // be the only real light. Dark cool ambient + faint moonlight fill keep the
            // room moody; exposure lifts the warm lamp pools without flooding it. Fog stays
            // so the lamps throw volumetric warm glow/halos; no sun god-ray. Asserted last
            // so it overrides the daytime sun/ambient/exposure set above. The reflective
            // PolishedWood floor mirrors the warm pools for the AAA night look.
            renderer->SetSunIntensity(0.12f);                                          // faint moonlight only
            renderer->SetSunColor(glm::vec3(0.55f, 0.62f, 0.85f));                      // cool
            renderer->SetSunDirection(glm::normalize(glm::vec3(-0.30f, 0.42f, -0.42f)));
            renderer->SetAmbientLighting(glm::vec3(0.010f, 0.013f, 0.022f), 0.30f);     // near-black cool fill
            // Kill the diffuse IBL flood (it was lighting the room like day); keep a faint
            // specular env so the polished floor still has something to reflect.
            renderer->SetIBLIntensity(0.015f, 0.10f);
            // Auto-exposure meters the dark scene back toward mid-grey (it "sees in the
            // dark"), washing the moody night to day. Switch to FIXED exposure so the
            // dark stays dark and the bright warm lamps read as glowing pools.
            renderer->SetAutoExposureEnabled(false);
            renderer->SetExposure(0.55f);                                              // fixed night exposure
            renderer->SetGodRayIntensity(0.0f);                                        // no sun shafts at night
            renderer->SetFogParams(0.115f, 0.15f, 0.42f, 0.0f);                        // denser air so the warm lamp light visibly scatters (halos/glow)
        }
        if (genExt.valid) {
            // IR-driven exterior environment (overrides the generic outdoor defaults).
            // The deferred (visibility-buffer) path paints the ENVIRONMENT equirect as
            // the sky background -- the forward procedural-sky pass never runs here --
            // so the sky is a real HDRI: picked by IR override or sun elevation, and
            // rotated so its baked sun sits at the IR sun azimuth. Its IBL doubles as
            // physically-plausible sky ambient (blue fill in shade).
            std::string skyPreset = genExt.skyPreset;
            if (skyPreset.empty()) {
                skyPreset = genExt.sunEl < 18.0f ? "sky_sunset" : "sky_day";
            }
            auto lowercase = [](std::string s) {
                std::transform(s.begin(), s.end(), s.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return s;
            };
            const std::string skyLower = lowercase(skyPreset);
            const std::string timeLower = lowercase(genExt.lookTime);
            const std::string gradeLower = lowercase(genExt.lookGrade);
            const bool moonlightLook = timeLower == "moonlight" ||
                                       gradeLower.find("moon") != std::string::npos ||
                                       skyLower.find("night") != std::string::npos;
            const bool authoredNightAtmosphere = moonlightLook &&
                                                 genExt.atmosphereFidelity.enabled &&
                                                 genExt.atmosphereFidelity.nightSkyControl;
            renderer->SetEnvironmentPreset(skyPreset);
            renderer->SetIBLEnabled(true);
            // Poly Haven pure-sky HDRIs sit around 0.1 median linear luminance (vs the
            // ~1.0 sun-lit ground), so both the visible background and the IBL need a
            // strong lift to read as a bright day. The sunset HDRI is brighter and
            // saturated -- lifting it 4x washes it to pastel, so it gets its own curve.
            const bool sunsetSky = skyPreset == "sky_sunset";
            renderer->SetIBLIntensity(authoredNightAtmosphere ? 0.34f : (moonlightLook ? 0.72f : (sunsetSky ? 2.1f : 3.2f)),
                                      authoredNightAtmosphere ? 0.58f : (moonlightLook ? 1.15f : (sunsetSky ? 1.4f : 1.8f))); // NOTE: specular also drives the visible sky-background brightness
            renderer->SetBackgroundPresentation(true,
                                                authoredNightAtmosphere ? genExt.atmosphereFidelity.skyBackgroundLift : (moonlightLook ? 1.05f : (sunsetSky ? 2.2f : 4.0f)),
                                                moonlightLook ? 0.14f : 0.0f);
            // Each HDRI's baked sun sits at its own azimuth in the file; the offset
            // aligns the visible glow with the IR sun light/shadows (calibrated
            // empirically: the sunset glow centres at rotation sunAz+150).
            renderer->SetEnvironmentRotation(genExt.sunAz + (sunsetSky ? 150.0f : 0.0f));
            // Sun direction points TO the light: azimuth 0 = +Z (over the camera's
            // shoulder), 180 = -Z (backlit, over the water); elevation above horizon.
            const float az = glm::radians(genExt.sunAz);
            const float el = glm::radians(genExt.sunEl);
            const glm::vec3 sunDir(std::sin(az) * std::cos(el), std::sin(el), std::cos(az) * std::cos(el));
            renderer->SetSunDirection(glm::normalize(sunDir));
            renderer->SetSunColor(genExt.sunColor);
            // golden hour is a LOOK, not a dim field: the low warm key keeps enough
            // punch to gild the scene
            renderer->SetSunIntensity((sunsetSky ? std::max(genExt.sunInt, 2.9f) : genExt.sunInt) *
                                      lightingBalance.sunScale);
            // Ambient = a sky-blue fill nudged toward the sun's colour so shade zones
            // read plausibly under any time-of-day the composer picks.
            const glm::vec3 skyFill = moonlightLook
                ? glm::vec3(0.035f, 0.050f, 0.095f)
                : glm::mix(glm::vec3(0.16f, 0.19f, 0.24f),
                           genExt.sunColor * 0.22f, 0.35f);
            renderer->SetAmbientLighting(skyFill, authoredNightAtmosphere ? 0.58f : (moonlightLook ? 0.92f : 1.0f));
            const float authoredFog = genExt.atmosphereFidelity.enabled
                ? std::max(genExt.fogDensity,
                           genExt.atmosphereFidelity.stormLayerCount >= 2 ? 0.024f : 0.016f)
                : genExt.fogDensity;
            renderer->SetFogParams(authoredFog, 0.05f, 0.34f, genExt.fogStart);
            if (moonlightLook) {
                renderer->SetColorGrade(0.02f, 0.30f);
                renderer->SetToneGrade(1.085f, 0.96f);
                renderer->SetCinematicPost(0.105f, 0.0f);
            }
            // FIXED exposure: auto-adaptation meters the bright sky (half the frame)
            // and crushes the ground into mud. Deterministic base; the critique loop
            // adjusts via CORTEX_AUTOEXPOSURE_MULT.
            renderer->SetAutoExposureEnabled(false);
            const float exteriorExposure = authoredNightAtmosphere
                ? std::max(0.48f, genExt.atmosphereFidelity.moonlightExposure)
                : (moonlightLook ? 0.98f : 0.80f);
            renderer->SetExposure(std::clamp(exteriorExposure * genExt.exposure, 0.35f, 1.45f));
            renderer->SetSSAOEnabled(true);
            renderer->SetSSAOParams(genExt.graphicsSSAORadius,
                                    genExt.graphicsSSAOBias,
                                    genExt.graphicsSSAOIntensity);
            renderer->SetSSREnabled(true);
            renderer->SetSSRParams(48.0f, 0.18f, 0.66f);
            renderer->SetShadowsEnabled(true);
            renderer->SetShadowBias(genExt.graphicsShadowBias);
            renderer->SetShadowPCFRadius(genExt.graphicsShadowPCF);
            spdlog::info("generative_exterior: graphics renderer quality ssao_radius={:.2f} ssao_intensity={:.2f} shadow_pcf={:.2f} ssr=on shadows=on",
                         genExt.graphicsSSAORadius,
                         genExt.graphicsSSAOIntensity,
                         genExt.graphicsShadowPCF);
            if (genExt.waterOn) {
                renderer->SetWaterParams(genExt.waterLevel, genExt.waterWave, 9.5f, 0.42f,
                                         0.12f, 0.92f, 0.020f, 0.44f); // gentle swell rolling toward the shore (+Z)
                renderer->SetWaterOptics(genExt.waterRough, genExt.waterFresnel); // v3 color intent can damp sky-mirror washout
            }
            if (genExt.authoredSceneModule.enabled) {
                renderer->SetLightingRigContract("generative_authored_scene_module", "generated_scene_module", false);
                renderer->SetWorldShaderPaletteContract(genExt.authoredSceneModule.moduleId.empty()
                                                            ? "generative_authored_exterior"
                                                            : genExt.authoredSceneModule.moduleId,
                                                        "generated_scene_module");
                const std::string module = genExt.authoredSceneModule.moduleId;
                if (module == "campsite_lake_dawn") {
                    renderer->SetSunDirection(glm::normalize(glm::vec3(0.74f, 0.17f, -0.64f)));
                    renderer->SetSunColor(glm::vec3(1.0f, 0.45f, 0.22f));
                    renderer->SetSunIntensity(4.25f * lightingBalance.sunScale);
                    renderer->SetAmbientLighting(glm::vec3(0.030f, 0.038f, 0.058f), 0.64f);
                    renderer->SetIBLIntensity(1.05f, 0.96f);
                    renderer->SetColorGrade(0.18f, 0.055f);
                    renderer->SetToneGrade(1.24f, 1.13f);
                    renderer->SetExposure(std::min(0.88f, genExt.exposure));
                    renderer->SetBloomIntensity(0.24f);
                    renderer->SetFogParams(std::max(genExt.fogDensity, 0.026f), 0.05f, 0.42f, 4.0f);
                    renderer->SetGodRayIntensity(0.18f);
                } else if (module == "desert_canyon_river") {
                    renderer->SetSunDirection(glm::normalize(glm::vec3(0.82f, 0.22f, -0.52f)));
                    renderer->SetSunColor(glm::vec3(1.0f, 0.58f, 0.30f));
                    renderer->SetSunIntensity(4.05f * lightingBalance.sunScale);
                    renderer->SetAmbientLighting(glm::vec3(0.055f, 0.043f, 0.034f), 0.70f);
                    renderer->SetIBLIntensity(1.35f, 1.05f);
                    renderer->SetColorGrade(0.20f, 0.020f);
                    renderer->SetToneGrade(1.22f, 1.10f);
                    renderer->SetExposure(std::min(0.86f, genExt.exposure));
                    renderer->SetBloomIntensity(0.12f);
                    renderer->SetFogParams(std::max(genExt.fogDensity, 0.012f), 0.05f, 0.40f, 8.0f);
                } else if (module == "alpine_cabin_lake") {
                    renderer->SetSunDirection(glm::normalize(glm::vec3(-0.36f, 0.32f, -0.88f)));
                    renderer->SetSunColor(glm::vec3(0.50f, 0.62f, 1.0f));
                    renderer->SetSunIntensity(1.05f * lightingBalance.sunScale);
                    renderer->SetAmbientLighting(glm::vec3(0.014f, 0.020f, 0.052f), 0.48f);
                    renderer->SetIBLIntensity(0.30f, 0.74f);
                    renderer->SetColorGrade(0.015f, 0.40f);
                    renderer->SetToneGrade(1.18f, 1.06f);
                    renderer->SetExposure(std::min(0.74f, genExt.exposure));
                    renderer->SetBloomIntensity(0.22f);
                    renderer->SetFogParams(std::max(genExt.fogDensity, 0.024f), 0.05f, 0.42f, 5.0f);
                } else {
                    renderer->SetAmbientLighting(glm::vec3(0.060f, 0.070f, 0.078f), 0.72f);
                    renderer->SetToneGrade(1.16f, 1.08f);
                    renderer->SetExposure(std::min(0.90f, genExt.exposure));
                }
                renderer->SetSSAOParams(std::max(genExt.graphicsSSAORadius, 1.24f),
                                        std::min(genExt.graphicsSSAOBias, 0.018f),
                                        std::max(genExt.graphicsSSAOIntensity, 2.65f));
                renderer->SetShadowBias(std::min(genExt.graphicsShadowBias, 0.0018f));
                renderer->SetShadowPCFRadius(std::max(genExt.graphicsShadowPCF, 3.0f));
                spdlog::info("generative_exterior: authored module lighting module={} contrast_key={} zones={}",
                             module.empty() ? "unknown" : module,
                             genExt.authoredSceneModule.contrastKey.empty() ? "default" : genExt.authoredSceneModule.contrastKey,
                             genExt.authoredSceneModule.lightingZoneCount);
            }
        }
        renderer->SetBloomShape(outdoor ? 1.05f : 1.02f, outdoor ? 0.45f : 0.50f, outdoor ? 2.0f : 0.82f);
        renderer->SetParticlesEnabled(true);
        renderer->SetParticleDensityScale(outdoor ? 0.90f : 1.05f);
        renderer->SetParticleTuning(outdoor ? 1.05f : 1.22f,
                                    outdoor ? 1.55f : 1.70f,
                                    outdoor ? 0.66f : 0.58f,
                                    outdoor ? 0.46f : 0.68f);
    }

    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, outdoor ? "Recipe_Garden_LocalReflectionProbe"
                                                                 : "Recipe_Room_LocalReflectionProbe");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = outdoor ? glm::vec3(0.0f, 1.45f, 0.0f) : glm::vec3(0.0f, 1.40f, -0.15f);
        Scene::ReflectionProbeComponent probe{};
        probe.extents = outdoor ? glm::vec3(5.25f, 2.50f, 5.25f) : glm::vec3(4.25f, 2.25f, 4.25f);
        probe.blendDistance = outdoor ? 2.25f : 1.75f;
        probe.environmentIndex = 0;
        probe.enabled = 1;
        m_registry->AddComponent<Scene::ReflectionProbeComponent>(e, probe);
    }

    // Generative exterior ground + water: created DIRECTLY (the command executor's
    // anti-z-fight rule lifts primitives to y>=0.5, which would raise the ground and
    // bury a sea-level water plane). Mirrors BuildOutdoorSunsetBeachScene: a UV-tiled
    // textured ground plane with its surface at exactly y=0 -- Place()'d objects
    // ground-snap their bases to y=0, so everything rests on it -- plus an animated
    // Gerstner sea covering z <= from_z, slightly ABOVE the ground so the seabed shows
    // through the shallows and solver-placed rocks rise through the surface naturally.
    if (genExt.valid) {
        if (auto* renderer = m_renderer.get()) {
            const float groundNear = genExt.extent * 0.5f + 10.0f;   // land runs behind the camera
            const float groundFar = -(genExt.extent * 1.9f + 10.0f); // seabed past the water, into the fog
            const float groundW = genExt.extent * 2.0f;
            const float shoreZ = genExt.waterOn ? (genExt.waterFromZ + 0.5f) : groundFar;
            // Land = flat plane with its surface at y=0. When there is water, a second
            // gently TILTED plane continues from the shoreline down to -2.5 m at the far
            // edge, so the water gains real depth with distance -- shallow green at the
            // shore, deep tint further out -- instead of a uniform 5 cm film over sand.
            const float landLen = groundNear - shoreZ;
            auto groundPlane = genExt.terrainHeightfield
                ? CreateGenerativeTerrainMesh(groundW,
                                              landLen,
                                              (groundNear + shoreZ) * 0.5f,
                                              genExt.terrainRelief,
                                              genExt.terrainMicroRelief,
                                              shoreZ,
                                              genExt.waterOn,
                                              genExt.terrainGrid)
                : Utils::MeshGenerator::CreatePlane(groundW, landLen);
            const float uvTile = genExt.extent / 2.5f;
            for (auto& uv : groundPlane->texCoords) {
                uv *= glm::vec2(uvTile, uvTile * (landLen / groundW));
            }
            glm::vec3 gcol = genExt.groundColor;
            if (!genExt.groundColorSet) {
                gcol = genExt.groundKind == "sand" ? glm::vec3(0.85f, 0.77f, 0.58f)
                     : genExt.groundKind == "dirt" ? glm::vec3(0.40f, 0.30f, 0.20f)
                     : genExt.groundKind == "rock" ? glm::vec3(0.47f, 0.46f, 0.44f)
                     : genExt.groundKind == "snow" ? glm::vec3(0.92f, 0.93f, 0.96f)
                                                   : glm::vec3(0.30f, 0.42f, 0.20f); // grass
            }
            const glm::vec3 receiverBase = genExt.groundKind == "dirt"
                ? glm::vec3(0.155f, 0.105f, 0.080f)
                : genExt.groundKind == "sand"
                    ? glm::vec3(0.185f, 0.165f, 0.125f)
                    : glm::vec3(0.105f, 0.118f, 0.105f);
            auto dressGround = [&](Scene::RenderableComponent& r) {
                r.albedoColor = glm::vec4(gcol, 1.0f);
                r.metallic = 0.0f;
                r.roughness = genExt.graphicsMaterials ? genExt.groundRoughness : 0.92f;
                r.ao = 1.0f;
                r.occlusionStrength = genExt.surfaceDetail.enabled ? 0.72f : 1.0f;
                r.doubleSided = true;
                r.presetName = genExt.groundKind == "sand" ? "sand" : "naturalistic";
                r.normalScale = genExt.graphicsMaterials ? genExt.groundNormalScale : r.normalScale;
                r.wetnessFactor = genExt.graphicsMaterials ? genExt.groundWetness : r.wetnessFactor;
                r.proceduralMaskStrength = genExt.graphicsMaterials ? genExt.groundProceduralMask : r.proceduralMaskStrength;
                r.specularFactor = genExt.graphicsMaterials ? 0.72f : r.specularFactor;
                r.clearcoatFactor = genExt.graphicsMaterials ? std::min(genExt.groundWetness * 0.38f, 0.22f) : r.clearcoatFactor;
                r.clearcoatRoughnessFactor = 0.72f;
                r.sheenWeight = genExt.surfaceDetail.enabled ? 0.055f : r.sheenWeight;
                r.anisotropyStrength = genExt.surfaceDetail.enabled ? 0.12f : r.anisotropyStrength;
                // Explicitly requested SATURATED ground colours ("red sand") must
                // actually paint the terrain: tint weight scales with how far the
                // colour sits from neutral, so natural palettes stay texture-led.
                const float maxc = std::max(gcol.r, std::max(gcol.g, gcol.b));
                const float minc = std::min(gcol.r, std::min(gcol.g, gcol.b));
                const float saturation = maxc > 1e-4f ? (maxc - minc) / maxc : 0.0f;
                const float tintW = genExt.groundColorSet ? std::min(0.3f + saturation * 0.9f, 0.85f) : 0.28f;
                if (genExt.groundKind == "sand") {
                    // aerial_beach_01 = BRIGHT dry sand (linear albedo ~0.25); the older
                    // coast_sand_05 set is dark wet shore (~0.05) and reads as mud.
                    applyGeneratedTextureMaterial(r, "terrain_sand");
                    r.albedoColor = glm::vec4(glm::mix(glm::vec3(1.0f), gcol, tintW), 1.0f);
                    r.normalScale = 0.65f;
                } else if (genExt.groundKind == "grass") {
                    applyGeneratedTextureMaterial(r, "terrain_grass");
                    r.albedoColor = glm::vec4(glm::mix(glm::vec3(1.0f), gcol, std::max(tintW, 0.30f)), 1.0f);
                    r.normalScale = 0.75f;
                } else if (genExt.groundKind == "rock") {
                    applyGeneratedTextureMaterial(r, "terrain_rock");
                } else if (genExt.groundKind == "dirt") {
                    applyGeneratedTextureMaterial(r, "terrain_sand");
                } else {
                    applyGeneratedTextureMaterial(r, "terrain_grass");
                }
            };
            auto upG = renderer->UploadMesh(groundPlane);
            if (upG.IsErr()) {
                spdlog::warn("generative_exterior: ground mesh upload failed: {}", upG.Error());
            } else {
                entt::entity groundE = m_registry->CreateEntity();
                m_registry->AddComponent<Scene::TagComponent>(groundE, "GenerativeExterior_Ground");
                auto& t = m_registry->AddComponent<TransformComponent>(groundE);
                t.position = glm::vec3(0.0f, 0.0f, (groundNear + shoreZ) * 0.5f);
                auto& r = m_registry->AddComponent<Scene::RenderableComponent>(groundE);
                r.mesh = groundPlane;
                dressGround(r);
                if (genExt.terrainHeightfield) {
                    spdlog::info("generative_exterior: created procedural terrain heightfield grid={} relief={:.2f} micro={:.2f}",
                                 genExt.terrainGrid,
                                 genExt.terrainRelief,
                                 genExt.terrainMicroRelief);
                }
            }
            if (genExt.waterOn) {
                // The seabed: a gently tilted plane running from the shoreline (y=0)
                // down to -2.5 m at the far edge.
                const float seaLen = shoreZ - groundFar;
                const float depth = 2.5f;
                auto seabedPlane = Utils::MeshGenerator::CreatePlane(groundW, seaLen);
                for (auto& uv : seabedPlane->texCoords) {
                    uv *= glm::vec2(uvTile, uvTile * (seaLen / groundW));
                }
                auto upS = renderer->UploadMesh(seabedPlane);
                if (upS.IsErr()) {
                    spdlog::warn("generative_exterior: seabed mesh upload failed: {}", upS.Error());
                } else {
                    const float alpha = std::asin(std::clamp(depth / seaLen, 0.0f, 0.5f));
                    entt::entity seabedE = m_registry->CreateEntity();
                    m_registry->AddComponent<Scene::TagComponent>(seabedE, "GenerativeExterior_Seabed");
                    auto& t = m_registry->AddComponent<TransformComponent>(seabedE);
                    t.position = glm::vec3(0.0f, -depth * 0.5f, (shoreZ + groundFar) * 0.5f);
                    t.rotation = glm::quat(glm::vec3(-alpha, 0.0f, 0.0f)); // shore edge up to y=0, far edge down to -depth
                    auto& r = m_registry->AddComponent<Scene::RenderableComponent>(seabedE);
                    r.mesh = seabedPlane;
                    dressGround(r);
                    applyGeneratedTextureMaterial(r, "terrain_shore", false, true);
                }
            }
            if (genExt.graphicsMaterials && genExt.waterOn && genExt.shoreLayerCount > 0) {
                int shoreLayers = 0;
                const float bandWidths[] = {0.62f, 0.26f};
                const glm::vec4 bandColors[] = {
                    glm::vec4(glm::max(gcol * 0.34f, glm::vec3(0.020f)), 0.34f),
                    glm::vec4(glm::mix(genExt.waterShallow, glm::vec3(0.55f, 0.58f, 0.62f), 0.30f), 0.18f),
                };
                for (int i = 0; i < std::min(genExt.shoreLayerCount, 2); ++i) {
                    auto shoreMesh = Utils::MeshGenerator::CreatePlane(groundW, bandWidths[i]);
                    auto upShore = renderer->UploadMesh(shoreMesh);
                    if (upShore.IsErr()) {
                        spdlog::warn("generative_exterior: shore layer mesh upload failed: {}", upShore.Error());
                        continue;
                    }
                    entt::entity shore = m_registry->CreateEntity();
                    m_registry->AddComponent<Scene::TagComponent>(
                        shore, "GenerativeExterior_ShoreGrounding" + std::to_string(i));
                    auto& t = m_registry->AddComponent<TransformComponent>(shore);
                    t.position = glm::vec3(0.0f, 0.026f + i * 0.006f, shoreZ + 0.32f + i * 0.40f);
                    auto& r = m_registry->AddComponent<Scene::RenderableComponent>(shore);
                    r.mesh = shoreMesh;
                    r.albedoColor = bandColors[i];
                    r.metallic = 0.0f;
                    r.roughness = i == 0 ? 0.78f : 0.86f;
                    r.ao = 0.72f;
                    r.occlusionStrength = 0.70f;
                    r.normalScale = 0.22f;
                    r.wetnessFactor = i == 0 ? 0.28f : 0.10f;
                    r.proceduralMaskStrength = 0.16f;
                    r.specularFactor = i == 0 ? 0.20f : 0.12f;
                    r.clearcoatFactor = i == 0 ? 0.18f : 0.06f;
                    r.clearcoatRoughnessFactor = 0.66f;
                    r.doubleSided = true;
                    r.alphaMode = Scene::RenderableComponent::AlphaMode::Blend;
                    r.renderLayer = Scene::RenderableComponent::RenderLayer::Overlay;
                    r.presetName = "naturalistic";
                    shoreLayers++;
                }
                if (shoreLayers > 0) {
                    spdlog::info("generative_exterior: created {} shore grounding layer(s)", shoreLayers);
                }
            }
            if (genExt.graphicsMaterials && !genExt.contactPatches.empty()) {
                auto contactMesh = Utils::MeshGenerator::CreateDisk(1.0f, 36);
                auto upContact = renderer->UploadMesh(contactMesh);
                if (upContact.IsErr()) {
                    spdlog::warn("generative_exterior: contact patch mesh upload failed: {}", upContact.Error());
                } else {
                    int contactCount = 0;
                    for (const auto& patch : genExt.contactPatches) {
                        entt::entity contact = m_registry->CreateEntity();
                        m_registry->AddComponent<Scene::TagComponent>(
                            contact, "GenerativeExterior_ContactGrounding" + std::to_string(contactCount));
                        auto& t = m_registry->AddComponent<TransformComponent>(contact);
                        t.position = glm::vec3(patch.position.x,
                                               0.018f + static_cast<float>(contactCount % 5) * 0.0015f,
                                               patch.position.y);
                        const float squash = 0.56f + 0.18f * std::sin(patch.position.x * 1.7f + patch.position.y * 0.6f);
                        t.scale = glm::vec3(patch.radius * 0.26f, 1.0f, patch.radius * squash * 0.20f);
                        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(contact);
                        r.mesh = contactMesh;
                        const glm::vec3 receiver = glm::max(receiverBase * (0.90f - patch.darkness * 0.08f), glm::vec3(0.020f));
                        r.albedoColor = glm::vec4(receiver, 0.055f);
                        r.metallic = 0.0f;
                        r.roughness = 0.88f;
                        r.ao = 0.52f;
                        r.occlusionStrength = 0.46f;
                        r.normalScale = 0.12f;
                        r.wetnessFactor = patch.wetness * 0.42f;
                        r.proceduralMaskStrength = 0.10f;
                        r.specularFactor = 0.16f;
                        r.clearcoatFactor = patch.wetness * 0.10f;
                        r.clearcoatRoughnessFactor = 0.80f;
                        r.doubleSided = true;
                        r.alphaMode = Scene::RenderableComponent::AlphaMode::Blend;
                        r.renderLayer = Scene::RenderableComponent::RenderLayer::Overlay;
                        r.presetName = "naturalistic";
                        contactCount++;
                    }
                    spdlog::info("generative_exterior: created contact grounding {} patch(es)", contactCount);
                }
            }
            int rendererBudgetContactPatches = 0;
            int rendererBudgetSoftPenumbra = 0;
            if (genExt.graphicsMaterials && genExt.imageContactOcclusion.enabled && !genExt.contactPatches.empty()) {
                auto deepContactMesh = Utils::MeshGenerator::CreateDisk(1.0f, 32);
                auto upDeepContact = renderer->UploadMesh(deepContactMesh);
                if (upDeepContact.IsErr()) {
                    spdlog::warn("generative_exterior: image contact occluder mesh upload failed: {}", upDeepContact.Error());
                } else {
                    auto pseudo = [](int i, float salt) {
                        const float n = std::sin(static_cast<float>(i) * 12.9898f + salt * 78.233f) * 43758.5453f;
                        return n - std::floor(n);
                    };
                    int deepContactCount = 0;
                    const int patchLimit = std::min(genExt.imageContactOcclusion.deepContactPatchCount, 48);
                    for (int i = 0; i < patchLimit; ++i) {
                        const auto& patch = genExt.contactPatches[static_cast<size_t>(i) % genExt.contactPatches.size()];
                        const float px = patch.position.x + (pseudo(i + 907, 1.41f) - 0.5f) * patch.radius * 0.30f;
                        const float pz = patch.position.y + (pseudo(i + 919, 1.83f) - 0.5f) * patch.radius * 0.24f;
                        entt::entity contact = m_registry->CreateEntity();
                        m_registry->AddComponent<Scene::TagComponent>(
                            contact, "GenerativeExterior_ImageContactOccluder" + std::to_string(i));
                        auto& t = m_registry->AddComponent<TransformComponent>(contact);
                        t.position = glm::vec3(px,
                                               0.064f + static_cast<float>(i % 7) * 0.0010f,
                                               pz);
                        t.rotation = glm::quat(glm::vec3(0.0f, glm::radians(17.0f * static_cast<float>(i)), 0.0f));
                        const float radius = std::clamp(patch.radius, 0.26f, 1.45f);
                        t.scale = glm::vec3(radius * (0.26f + 0.03f * static_cast<float>(i % 3)),
                                            1.0f,
                                            radius * (0.095f + 0.014f * static_cast<float>((i + 1) % 3)));
                        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(contact);
                        r.mesh = deepContactMesh;
                        const glm::vec3 contactColor = glm::max(receiverBase * 0.72f, glm::vec3(0.014f));
                        r.albedoColor = glm::vec4(contactColor, 1.0f);
                        r.metallic = 0.0f;
                        r.roughness = 0.96f;
                        r.ao = 0.22f;
                        r.occlusionStrength = 0.92f;
                        r.normalScale = 0.05f;
                        r.wetnessFactor = patch.wetness * 0.12f;
                        r.proceduralMaskStrength = 0.04f;
                        r.specularFactor = 0.02f;
                        r.clearcoatFactor = 0.0f;
                        r.doubleSided = true;
                        r.alphaMode = Scene::RenderableComponent::AlphaMode::Opaque;
                        r.renderLayer = Scene::RenderableComponent::RenderLayer::Opaque;
                        r.presetName = "shadow";
                        deepContactCount++;
                    }
                    if (genExt.structures.empty()) {
                        const float anchorData[][4] = {
                            { 2.90f, 0.90f, 1.18f, 0.18f },
                            { -0.35f, 0.35f, 0.78f, 0.12f },
                            { -1.05f, 0.86f, 0.72f, 0.12f },
                            { 0.48f, -0.18f, 0.74f, 0.11f },
                            { -2.25f, 1.35f, 0.92f, 0.14f },
                            { 1.25f, 2.35f, 0.88f, 0.13f },
                            { -0.35f, 2.85f, 1.06f, 0.15f },
                            { 3.85f, 1.55f, 0.90f, 0.12f },
                            { -3.70f, 2.05f, 0.84f, 0.12f },
                            { 5.60f, 2.65f, 0.92f, 0.13f },
                            { -5.30f, 3.10f, 0.88f, 0.12f },
                            { 0.90f, 3.35f, 0.96f, 0.14f },
                        };
                        constexpr int anchorCount = static_cast<int>(sizeof(anchorData) / sizeof(anchorData[0]));
                        for (int i = 0; i < anchorCount; ++i) {
                            entt::entity contact = m_registry->CreateEntity();
                            m_registry->AddComponent<Scene::TagComponent>(
                                contact, "GenerativeExterior_ImageContactHeroAnchor" + std::to_string(i));
                            auto& t = m_registry->AddComponent<TransformComponent>(contact);
                            t.position = glm::vec3(anchorData[i][0], 0.074f + static_cast<float>(i % 5) * 0.001f, anchorData[i][1]);
                            t.rotation = glm::quat(glm::vec3(0.0f, glm::radians(-18.0f + 13.0f * static_cast<float>(i)), 0.0f));
                            t.scale = glm::vec3(anchorData[i][2] * 0.26f, 1.0f, anchorData[i][3] * 0.52f);
                            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(contact);
                            r.mesh = deepContactMesh;
                            const glm::vec3 anchorColor = glm::max(receiverBase * 0.66f, glm::vec3(0.013f));
                            r.albedoColor = glm::vec4(anchorColor, 1.0f);
                            r.metallic = 0.0f;
                            r.roughness = 0.96f;
                            r.ao = 0.18f;
                            r.occlusionStrength = 0.94f;
                            r.normalScale = 0.04f;
                            r.wetnessFactor = genExt.groundWetness * 0.08f;
                            r.proceduralMaskStrength = 0.02f;
                            r.specularFactor = 0.01f;
                            r.clearcoatFactor = 0.0f;
                            r.doubleSided = true;
                            r.alphaMode = Scene::RenderableComponent::AlphaMode::Opaque;
                            r.renderLayer = Scene::RenderableComponent::RenderLayer::Opaque;
                            r.presetName = "shadow";
                            deepContactCount++;
                        }
                    }
                    spdlog::info("generative_exterior: created image contact occluders patches={} target_dark_contact={:.4f}",
                                 deepContactCount,
                                 genExt.imageContactOcclusion.targetDarkContactFraction);
                    rendererBudgetContactPatches = deepContactCount;
                }
            }
            if (genExt.graphicsMaterials && genExt.softOcclusion.enabled && !genExt.contactPatches.empty()) {
                auto softMesh = Utils::MeshGenerator::CreateDisk(1.0f, 48);
                auto upSoft = renderer->UploadMesh(softMesh);
                if (upSoft.IsErr()) {
                    spdlog::warn("generative_exterior: soft contact occlusion mesh upload failed: {}", upSoft.Error());
                } else {
                    auto pseudo = [](int i, float salt) {
                        const float n = std::sin(static_cast<float>(i) * 12.9898f + salt * 78.233f) * 43758.5453f;
                        return n - std::floor(n);
                    };
                    auto addSoftPatch = [&](const std::string& tag,
                                            float x,
                                            float z,
                                            float sx,
                                            float sz,
                                            float yawDeg,
                                            float alpha,
                                            int index) {
                        entt::entity soft = m_registry->CreateEntity();
                        m_registry->AddComponent<Scene::TagComponent>(soft, tag);
                        auto& t = m_registry->AddComponent<TransformComponent>(soft);
                        t.position = glm::vec3(x, 0.043f + static_cast<float>(index % 11) * 0.0006f, z);
                        t.rotation = glm::quat(glm::vec3(0.0f, glm::radians(yawDeg), 0.0f));
                        t.scale = glm::vec3(sx, 1.0f, sz);
                        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(soft);
                        r.mesh = softMesh;
                        const glm::vec3 softColor = glm::max(glm::mix(receiverBase, glm::vec3(0.030f), 0.10f), glm::vec3(0.020f));
                        r.albedoColor = glm::vec4(softColor, alpha);
                        r.metallic = 0.0f;
                        r.roughness = 0.98f;
                        r.ao = 0.38f;
                        r.occlusionStrength = 0.86f;
                        r.normalScale = 0.03f;
                        r.wetnessFactor = genExt.groundWetness * 0.10f;
                        r.proceduralMaskStrength = 0.03f;
                        r.specularFactor = 0.02f;
                        r.clearcoatFactor = 0.0f;
                        r.doubleSided = true;
                        r.alphaMode = Scene::RenderableComponent::AlphaMode::Blend;
                        r.renderLayer = Scene::RenderableComponent::RenderLayer::Overlay;
                        r.presetName = "shadow";
                    };

                    int penumbraCount = 0;
                    const int layerCount = std::max(1, genExt.softOcclusion.contactGradientLayerCount);
                    const int patchLimit = std::min(genExt.softOcclusion.penumbraPatchCount, 48);
                    for (int i = 0; i < patchLimit; ++i) {
                        const int layer = i % layerCount;
                        const auto& patch = genExt.contactPatches[static_cast<size_t>(i / layerCount) % genExt.contactPatches.size()];
                        const float spread = 1.0f + static_cast<float>(layer) * 0.24f;
                        const float radius = std::clamp(patch.radius * (0.34f + 0.06f * pseudo(i + 1001, 1.37f)), 0.14f, 0.70f);
                        const float alpha = std::clamp(0.014f - static_cast<float>(layer) * 0.003f +
                                                       genExt.softOcclusion.targetSoftContactFraction * 0.05f,
                                                       0.006f,
                                                       0.018f);
                        addSoftPatch("GenerativeExterior_SoftContactPenumbra" + std::to_string(i),
                                     patch.position.x + (pseudo(i + 1013, 2.17f) - 0.5f) * patch.radius * 0.18f,
                                     patch.position.y + (pseudo(i + 1021, 2.61f) - 0.5f) * patch.radius * 0.16f,
                                     radius * (0.42f + 0.08f * pseudo(i, 1.71f)) * spread,
                                     radius * (0.18f + 0.04f * pseudo(i, 1.93f)) * spread,
                                     -18.0f + 57.0f * pseudo(i, 2.37f),
                                     alpha,
                                     i);
                        penumbraCount++;
                    }

                    int heroAnchorCount = 0;
                    if (!genExt.structures.empty()) {
                        for (const auto& structure : genExt.structures) {
                            const float yawRad = glm::radians(structure.yawDeg);
                            const float cs = std::cos(yawRad);
                            const float sn = std::sin(yawRad);
                            auto place = [&](float x, float z) -> glm::vec2 {
                                const glm::vec3 p = structure.position + glm::vec3(cs * x + sn * z,
                                                                                  0.0f,
                                                                                  -sn * x + cs * z);
                                return glm::vec2(p.x, p.z);
                            };
                            const std::array<glm::vec4, 8> cabinAnchors = {
                                glm::vec4(0.0f, 0.0f, structure.widthM * 0.22f, structure.depthM * 0.15f),
                                glm::vec4(0.0f, structure.depthM * 0.54f, structure.widthM * 0.18f, 0.14f),
                                glm::vec4(-structure.widthM * 0.34f, structure.depthM * 0.34f, 0.13f, 0.12f),
                                glm::vec4(structure.widthM * 0.34f, structure.depthM * 0.34f, 0.13f, 0.12f),
                                glm::vec4(-structure.widthM * 0.48f, -structure.depthM * 0.32f, 0.12f, 0.11f),
                                glm::vec4(structure.widthM * 0.48f, -structure.depthM * 0.32f, 0.12f, 0.11f),
                                glm::vec4(-structure.widthM * 0.18f, structure.depthM * 0.62f, 0.11f, 0.10f),
                                glm::vec4(structure.widthM * 0.18f, structure.depthM * 0.62f, 0.11f, 0.10f),
                            };
                            for (size_t i = 0; i < cabinAnchors.size() && heroAnchorCount < genExt.softOcclusion.heroAnchorCount; ++i) {
                                const glm::vec2 p = place(cabinAnchors[i].x, cabinAnchors[i].y);
                                addSoftPatch("GenerativeExterior_SoftHeroAnchor_Cabin" + std::to_string(heroAnchorCount),
                                             p.x,
                                             p.y,
                                             cabinAnchors[i].z,
                                             cabinAnchors[i].w,
                                             structure.yawDeg,
                                             0.014f,
                                             100 + heroAnchorCount);
                                heroAnchorCount++;
                            }
                        }
                    } else {
                        const float anchorData[][4] = {
                            { 2.90f, 0.90f, 0.40f, 0.16f },
                            { -0.35f, 0.35f, 0.26f, 0.14f },
                            { -1.05f, 0.86f, 0.22f, 0.12f },
                            { 0.48f, -0.18f, 0.22f, 0.12f },
                            { -2.25f, 1.35f, 0.26f, 0.13f },
                            { 1.25f, 2.35f, 0.25f, 0.12f },
                            { -0.35f, 2.85f, 0.25f, 0.12f },
                            { 3.85f, 1.55f, 0.23f, 0.11f },
                            { -3.70f, 2.05f, 0.23f, 0.11f },
                            { 5.60f, 2.65f, 0.22f, 0.11f },
                            { -5.30f, 3.10f, 0.22f, 0.10f },
                            { 0.90f, 3.35f, 0.22f, 0.10f },
                        };
                        constexpr int anchorCount = static_cast<int>(sizeof(anchorData) / sizeof(anchorData[0]));
                        for (int i = 0; i < std::min(anchorCount, genExt.softOcclusion.heroAnchorCount); ++i) {
                            addSoftPatch("GenerativeExterior_SoftHeroAnchor_Camp" + std::to_string(i),
                                         anchorData[i][0],
                                         anchorData[i][1],
                                         anchorData[i][2],
                                         anchorData[i][3],
                                         -20.0f + 17.0f * static_cast<float>(i),
                                         0.022f,
                                         140 + i);
                            heroAnchorCount++;
                        }
                    }
                    spdlog::info("generative_exterior: created soft contact occlusion penumbra={} gradient_layers={} hero_anchors={} target_soft_contact={:.4f}",
                                 penumbraCount,
                                 layerCount,
                                 heroAnchorCount,
                                 genExt.softOcclusion.targetSoftContactFraction);
                    rendererBudgetSoftPenumbra = penumbraCount + heroAnchorCount;
                }
            }
            if (genExt.rendererShadowOcclusionBudget.enabled) {
                const auto features = renderer->GetFeatureState();
                const auto quality = renderer->GetQualityState();
                spdlog::info("generative_exterior: renderer shadow occlusion budget ssao={} shadows={} ssao_radius={:.2f} ssao_bias={:.3f} ssao_intensity={:.2f} shadow_bias={:.4f} shadow_pcf={:.2f} contact_patches={} soft_penumbra={} overlay_budget={} dxr_required={}",
                             features.ssaoEnabled ? "on" : "off",
                             quality.shadowsEnabled ? "on" : "off",
                             features.ssaoRadius,
                             features.ssaoBias,
                             features.ssaoIntensity,
                             quality.shadowBias,
                             quality.shadowPCFRadius,
                             rendererBudgetContactPatches,
                             rendererBudgetSoftPenumbra,
                             rendererBudgetContactPatches + rendererBudgetSoftPenumbra,
                             genExt.rendererShadowOcclusionBudget.dxrRequired ? 1 : 0);
            }
            if (genExt.waterOn && genExt.waterShoreIntegration.enabled) {
                auto ribbonMesh = Utils::MeshGenerator::CreatePlane(1.0f, 1.0f);
                auto edgeRockMesh = CreateGenerativeRockShardMesh(8.91f);
                const auto upRibbon = renderer->UploadMesh(ribbonMesh);
                const auto upEdgeRock = renderer->UploadMesh(edgeRockMesh);
                if (upRibbon.IsErr() || upEdgeRock.IsErr()) {
                    spdlog::warn("generative_exterior: water shore integration mesh upload failed ribbon='{}' rock='{}'",
                                 upRibbon.IsErr() ? upRibbon.Error() : "ok",
                                 upEdgeRock.IsErr() ? upEdgeRock.Error() : "ok");
                } else {
                    auto pseudo = [](int i, float salt) {
                        const float n = std::sin(static_cast<float>(i) * 12.9898f + salt * 78.233f) * 43758.5453f;
                        return n - std::floor(n);
                    };
                    auto dressWaterOverlay = [&](Scene::RenderableComponent& r,
                                                 const glm::vec4& color,
                                                 float roughness,
                                                 float wetness,
                                                 float clearcoat,
                                                 const char* preset) {
                        r.albedoColor = color;
                        r.metallic = 0.0f;
                        r.roughness = roughness;
                        r.ao = 0.72f;
                        r.occlusionStrength = 0.58f;
                        r.normalScale = 0.10f;
                        r.wetnessFactor = wetness;
                        r.proceduralMaskStrength = 0.18f;
                        r.specularFactor = 0.34f + clearcoat * 0.80f;
                        r.clearcoatFactor = clearcoat;
                        r.clearcoatRoughnessFactor = 0.48f;
                        r.anisotropyStrength = 0.20f;
                        r.doubleSided = true;
                        r.alphaMode = Scene::RenderableComponent::AlphaMode::Blend;
                        r.renderLayer = Scene::RenderableComponent::RenderLayer::Overlay;
                        r.presetName = preset;
                    };
                    auto addRibbon = [&](const std::string& tag,
                                         const glm::vec3& position,
                                         const glm::vec3& scale,
                                         float yawDeg,
                                         const glm::vec4& color,
                                         float roughness,
                                         float wetness,
                                         float clearcoat,
                                         const char* preset) {
                        entt::entity e = m_registry->CreateEntity();
                        m_registry->AddComponent<Scene::TagComponent>(e, tag);
                        auto& t = m_registry->AddComponent<TransformComponent>(e);
                        t.position = position;
                        t.rotation = glm::quat(glm::vec3(0.0f, glm::radians(yawDeg), 0.0f));
                        t.scale = scale;
                        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
                        r.mesh = ribbonMesh;
                        dressWaterOverlay(r, color, roughness, wetness, clearcoat, preset);
                    };

                    int wetlineBands = 0;
                    for (int i = 0; i < genExt.waterShoreIntegration.wetlineBandCount; ++i) {
                        const float bandZ = shoreZ + 0.16f + static_cast<float>(i) * 0.22f;
                        const float alpha = 0.16f - static_cast<float>(i) * 0.018f;
                        addRibbon("GenerativeExterior_WaterWetlineGradient" + std::to_string(i),
                                  glm::vec3(0.0f, 0.061f + static_cast<float>(i) * 0.0012f, bandZ),
                                  glm::vec3(groundW * (0.96f - i * 0.035f), 1.0f, 0.18f + i * 0.06f),
                                  -1.5f + 0.8f * static_cast<float>(i),
                                  glm::vec4(glm::mix(gcol * 0.30f, genExt.waterShallow, 0.34f), std::max(alpha, 0.08f)),
                                  0.70f,
                                  0.62f,
                                  0.18f,
                                  "naturalistic");
                        wetlineBands++;
                    }

                    int foamLace = 0;
                    for (int i = 0; i < genExt.waterShoreIntegration.foamLaceSegmentCount; ++i) {
                        const float x = (pseudo(i + 1201, 1.23f) - 0.5f) * groundW * 0.84f;
                        const float z = shoreZ - 0.08f + (pseudo(i + 1211, 1.91f) - 0.5f) * 0.36f;
                        const glm::vec3 foam = glm::mix(glm::vec3(0.92f, 0.94f, 0.96f), genExt.waterShallow, 0.24f);
                        addRibbon("GenerativeExterior_WaterFoamLace" + std::to_string(i),
                                  glm::vec3(x, genExt.waterLevel + 0.034f + static_cast<float>(i % 7) * 0.0009f, z),
                                  glm::vec3(1.10f + pseudo(i, 2.11f) * 1.80f,
                                            1.0f,
                                            0.035f + pseudo(i, 2.73f) * 0.050f),
                                  -8.0f + pseudo(i, 3.01f) * 16.0f,
                                  glm::vec4(foam, 0.26f),
                                  0.30f,
                                  0.78f,
                                  0.28f,
                                  "water");
                        foamLace++;
                    }

                    int rippleCount = 0;
                    for (int i = 0; i < genExt.waterShoreIntegration.shorelineRippleCount; ++i) {
                        const float lane = static_cast<float>(i % 5);
                        const float x = (pseudo(i + 1301, 1.47f) - 0.5f) * groundW * 0.76f;
                        const float z = shoreZ - 0.52f - lane * 0.32f - pseudo(i + 1319, 2.29f) * 0.20f;
                        const glm::vec3 ripple = glm::mix(genExt.waterShallow, glm::vec3(1.0f), 0.16f + 0.04f * pseudo(i, 2.91f));
                        addRibbon("GenerativeExterior_WaterShoreRipple" + std::to_string(i),
                                  glm::vec3(x, genExt.waterLevel + 0.039f + static_cast<float>(i % 5) * 0.0008f, z),
                                  glm::vec3(0.80f + pseudo(i, 1.69f) * 1.55f,
                                            1.0f,
                                            0.025f + pseudo(i, 2.41f) * 0.035f),
                                  -4.0f + pseudo(i, 3.17f) * 8.0f,
                                  glm::vec4(ripple, 0.12f),
                                  0.24f,
                                  0.62f,
                                  0.34f,
                                  "water");
                        rippleCount++;
                    }

                    int glintCount = 0;
                    for (int i = 0; i < genExt.waterShoreIntegration.reflectionGlintCount; ++i) {
                        const float x = (pseudo(i + 1409, 1.83f) - 0.5f) * groundW * 0.58f;
                        const float z = shoreZ - 0.88f - pseudo(i + 1423, 2.67f) * 3.4f;
                        const glm::vec3 glint = glm::mix(genExt.waterShallow, glm::vec3(1.0f), 0.34f);
                        addRibbon("GenerativeExterior_WaterReflectionGlint" + std::to_string(i),
                                  glm::vec3(x, genExt.waterLevel + 0.046f + static_cast<float>(i % 3) * 0.0007f, z),
                                  glm::vec3(0.42f + pseudo(i, 2.01f) * 0.76f,
                                            1.0f,
                                            0.018f + pseudo(i, 2.89f) * 0.025f),
                                  8.0f + pseudo(i, 3.31f) * 148.0f,
                                  glm::vec4(glint, 0.16f),
                                  0.18f,
                                  0.72f,
                                  0.42f,
                                  "water");
                        glintCount++;
                    }

                    int submergedRocks = 0;
                    for (int i = 0; i < genExt.waterShoreIntegration.submergedEdgeRockCount; ++i) {
                        const float x = (pseudo(i + 1501, 2.07f) - 0.5f) * groundW * 0.82f;
                        const float z = shoreZ - 0.24f - pseudo(i + 1511, 2.53f) * 1.25f;
                        entt::entity rock = m_registry->CreateEntity();
                        m_registry->AddComponent<Scene::TagComponent>(rock, "GenerativeExterior_SubmergedEdgeRock" + std::to_string(i));
                        auto& t = m_registry->AddComponent<TransformComponent>(rock);
                        t.position = glm::vec3(x, genExt.waterLevel + 0.018f + static_cast<float>(i % 4) * 0.001f, z);
                        t.rotation = glm::quat(glm::vec3(glm::radians(-3.0f + pseudo(i, 2.19f) * 6.0f),
                                                         glm::radians(pseudo(i, 2.71f) * 360.0f),
                                                         glm::radians(-5.0f + pseudo(i, 3.13f) * 10.0f)));
                        const float s = 0.34f + pseudo(i, 3.77f) * 0.34f;
                        t.scale = glm::vec3(s * 1.35f, s * 0.34f, s * 0.78f);
                        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(rock);
                        r.mesh = edgeRockMesh;
                        const glm::vec3 rockColor = glm::mix(gcol, glm::vec3(0.08f, 0.075f, 0.070f), 0.56f);
                        dressWaterOverlay(r,
                                          glm::vec4(glm::max(rockColor, glm::vec3(0.012f)), 0.72f),
                                          0.66f,
                                          0.82f,
                                          0.20f,
                                          "wet_stone");
                        submergedRocks++;
                    }

                    spdlog::info("generative_exterior: created water shore integration foam_lace={} ripples={} wetline_bands={} reflection_glints={} submerged_edge_rocks={}",
                                 foamLace,
                                 rippleCount,
                                 wetlineBands,
                                 glintCount,
                                 submergedRocks);
                }
            }
            if (genExt.cinematicMaterialLighting.enabled) {
                auto planeMesh = Utils::MeshGenerator::CreatePlane(1.0f, 1.0f);
                auto receiverMesh = Utils::MeshGenerator::CreateDisk(1.0f, 36);
                auto casterMesh = CreateGenerativeRockShardMesh(37.21f);
                auto cubeMesh = Utils::MeshGenerator::CreateCube();
                auto cylinderMesh = Utils::MeshGenerator::CreateCylinder(0.5f, 1.0f, 16);
                const auto upPlane = renderer->UploadMesh(planeMesh);
                const auto upReceiver = renderer->UploadMesh(receiverMesh);
                const auto upCaster = renderer->UploadMesh(casterMesh);
                const auto upCube = renderer->UploadMesh(cubeMesh);
                const auto upCylinder = renderer->UploadMesh(cylinderMesh);
                if (upPlane.IsErr() || upReceiver.IsErr() || upCaster.IsErr() || upCube.IsErr() || upCylinder.IsErr()) {
                    spdlog::warn("generative_exterior: cinematic material lighting mesh upload failed plane='{}' receiver='{}' caster='{}' cube='{}' cylinder='{}'",
                                 upPlane.IsErr() ? upPlane.Error() : "ok",
                                 upReceiver.IsErr() ? upReceiver.Error() : "ok",
                                 upCaster.IsErr() ? upCaster.Error() : "ok",
                                 upCube.IsErr() ? upCube.Error() : "ok",
                                 upCylinder.IsErr() ? upCylinder.Error() : "ok");
                } else {
                    auto pseudo = [](int i, float salt) {
                        const float n = std::sin(static_cast<float>(i) * 12.9898f + salt * 78.233f) * 43758.5453f;
                        return n - std::floor(n);
                    };
                    const std::string module = genExt.authoredSceneModule.moduleId;
                    const bool canyonModule = module == "desert_canyon_river";
                    const bool alpineModule = module == "alpine_cabin_lake";
                    const bool campsiteModule = module == "campsite_lake_dawn" || (!alpineModule && !canyonModule);
                    const glm::vec3 baseRock = canyonModule
                        ? glm::vec3(0.55f, 0.22f, 0.12f)
                        : glm::mix(gcol, glm::vec3(0.18f, 0.17f, 0.15f), 0.42f);
                    const glm::vec3 shadowTone = glm::max(receiverBase * (alpineModule ? 0.58f : 0.70f), glm::vec3(0.014f));
                    const float landSpan = std::max(4.0f, groundNear - shoreZ);
                    const float sourceTextureWeight = genExt.cinematicMaterialLighting.sourceTextureWeight;
                    const float normalScale = std::max(genExt.cinematicMaterialLighting.normalDetailScale, 0.78f);

                    auto dressTerrainOverlay = [&](Scene::RenderableComponent& r,
                                                   const glm::vec4& color,
                                                   const char* preset,
                                                   float roughness,
                                                   float wetness,
                                                   float clearcoat,
                                                   float alpha) {
                        r.albedoColor = color;
                        r.metallic = 0.0f;
                        r.roughness = roughness;
                        r.ao = 0.58f;
                        r.occlusionStrength = 0.78f;
                        r.normalScale = normalScale;
                        r.wetnessFactor = wetness;
                        r.proceduralMaskStrength = 0.52f + 0.20f * sourceTextureWeight;
                        r.specularFactor = 0.10f + clearcoat * 0.44f;
                        r.clearcoatFactor = clearcoat;
                        r.clearcoatRoughnessFactor = 0.58f;
                        r.sheenWeight = 0.05f;
                        r.anisotropyStrength = 0.16f;
                        r.doubleSided = true;
                        r.alphaMode = alpha < 0.99f
                            ? Scene::RenderableComponent::AlphaMode::Blend
                            : Scene::RenderableComponent::AlphaMode::Opaque;
                        r.renderLayer = alpha < 0.99f
                            ? Scene::RenderableComponent::RenderLayer::Overlay
                            : Scene::RenderableComponent::RenderLayer::Opaque;
                        r.presetName = preset;
                    };

                    auto addPatch = [&](const std::string& tag,
                                        const std::shared_ptr<Scene::MeshData>& mesh,
                                        const glm::vec3& position,
                                        const glm::vec3& scale,
                                        const glm::vec3& euler,
                                        const glm::vec4& color,
                                        const char* preset,
                                        float roughness,
                                        float wetness,
                                        float clearcoat,
                                        float alpha,
                                        const char* textureId,
                                        bool heroSurface = false,
                                        bool shoreSurface = false) {
                        entt::entity e = m_registry->CreateEntity();
                        m_registry->AddComponent<Scene::TagComponent>(e, tag);
                        auto& t = m_registry->AddComponent<TransformComponent>(e);
                        t.position = position;
                        t.scale = scale;
                        t.rotation = glm::quat(euler);
                        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
                        r.mesh = mesh;
                        dressTerrainOverlay(r, color, preset, roughness, wetness, clearcoat, alpha);
                        if (textureId && textureId[0]) {
                            applyGeneratedTextureMaterial(r, textureId, heroSurface, shoreSurface);
                        }
                    };

                    int triplanarLayers = 0;
                    for (int i = 0; i < genExt.cinematicMaterialLighting.triplanarDetailLayerCount; ++i) {
                        const float lane = static_cast<float>(i % 4);
                        const float z = shoreZ + 0.92f + lane * 0.82f + pseudo(i, 1.27f) * 0.36f;
                        const float x = (pseudo(i + 11, 1.83f) - 0.5f) * groundW * 0.42f;
                        const glm::vec3 detailColor = glm::mix(gcol * 0.72f, baseRock, canyonModule ? 0.46f : 0.24f + 0.04f * static_cast<float>(i % 3));
                        addPatch("GenerativeExterior_CinematicTriplanarLayer" + std::to_string(i),
                                 planeMesh,
                                 glm::vec3(x, 0.078f + static_cast<float>(i % 5) * 0.0010f, z),
                                 glm::vec3(1.10f + pseudo(i, 2.31f) * 1.35f,
                                           1.0f,
                                           0.08f + pseudo(i, 2.91f) * 0.075f),
                                 glm::vec3(0.0f, glm::radians(-18.0f + pseudo(i, 3.19f) * 36.0f), 0.0f),
                                 glm::vec4(glm::max(detailColor, glm::vec3(0.018f)), 0.042f),
                                 canyonModule ? "masonry" : "naturalistic",
                                 0.78f,
                                 genExt.groundWetness * 0.42f,
                                 0.08f,
                                 0.042f,
                                 "",
                                 false,
                                 false);
                        triplanarLayers++;
                    }

                    int reliefPatches = 0;
                    for (int i = 0; i < genExt.cinematicMaterialLighting.terrainReliefPatchCount; ++i) {
                        const float sideBias = (i % 2 == 0) ? -1.0f : 1.0f;
                        const float x = sideBias * (1.4f + pseudo(i + 101, 1.59f) * groundW * 0.42f);
                        const float z = shoreZ + 0.55f + pseudo(i + 113, 2.47f) * std::min(landSpan, 9.5f);
                        const float s = 0.18f + pseudo(i, 3.07f) * (canyonModule ? 0.24f : 0.20f);
                        const glm::vec3 patchColor = glm::mix(baseRock, gcol, canyonModule ? 0.30f : 0.56f + 0.10f * pseudo(i, 4.11f));
                        addPatch("GenerativeExterior_CinematicReliefPatch" + std::to_string(i),
                                 casterMesh,
                                 glm::vec3(x, 0.075f + 0.010f * pseudo(i, 2.03f), z),
                                 glm::vec3(s * (1.35f + pseudo(i, 1.21f) * 0.70f),
                                           s * 0.13f,
                                           s * (0.55f + pseudo(i, 1.61f) * 0.48f)),
                                 glm::vec3(glm::radians(-3.0f + pseudo(i, 1.07f) * 6.0f),
                                           glm::radians(pseudo(i, 1.91f) * 360.0f),
                                           glm::radians(-4.0f + pseudo(i, 2.23f) * 8.0f)),
                                 glm::vec4(glm::max(patchColor, glm::vec3(0.016f)), 1.0f),
                                 canyonModule ? "masonry" : "naturalistic",
                                 0.86f,
                                 genExt.groundWetness * 0.34f,
                                 0.05f,
                                 1.0f,
                                 "");
                        reliefPatches++;
                    }

                    int shadowCasters = 0;
                    for (int i = 0; i < genExt.cinematicMaterialLighting.shadowCasterCount; ++i) {
                        const bool nearHero = i < std::max(4, genExt.cinematicMaterialLighting.shadowCasterCount / 2);
                        const float angle = glm::radians(-35.0f + static_cast<float>(i) * 18.0f);
                        const float radius = nearHero ? (2.45f + 0.22f * static_cast<float>(i % 4)) : (5.6f + 0.38f * static_cast<float>(i % 5));
                        const float x = std::cos(angle) * radius + (canyonModule ? ((i % 2 == 0) ? -4.6f : 4.6f) : 0.0f);
                        const float z = (nearHero ? 1.28f : shoreZ + 1.55f) + std::sin(angle) * (nearHero ? 1.25f : 1.8f);
                        const float h = 0.12f + 0.035f * static_cast<float>(i % 4);
                        addPatch("GenerativeExterior_CinematicShadowCaster" + std::to_string(i),
                                 (i % 3 == 0) ? cylinderMesh : casterMesh,
                                 glm::vec3(x, 0.050f + h * 0.42f, z),
                                 glm::vec3(0.070f + 0.020f * static_cast<float>(i % 3),
                                           h,
                                           0.12f + 0.025f * static_cast<float>((i + 1) % 3)),
                                 glm::vec3(glm::radians((i % 3 == 0) ? 86.0f : (-2.0f + 1.2f * static_cast<float>(i % 4))),
                                           glm::radians(-24.0f + 19.0f * static_cast<float>(i)),
                                           glm::radians(3.0f * static_cast<float>(i % 3 - 1))),
                                 glm::vec4(glm::max(glm::mix(baseRock, gcol, 0.42f), glm::vec3(0.040f)), 1.0f),
                                 (i % 3 == 0) ? "wood" : "masonry",
                                 0.82f,
                                 genExt.groundWetness * 0.16f,
                                 0.02f,
                                 1.0f,
                                 "",
                                 true,
                                 false);
                        shadowCasters++;
                    }

                    int contactReceivers = 0;
                    for (int i = 0; i < genExt.cinematicMaterialLighting.contactReceiverCount; ++i) {
                        const float x = (pseudo(i + 307, 1.37f) - 0.5f) * groundW * 0.64f;
                        const float z = shoreZ + 0.52f + pseudo(i + 313, 2.11f) * std::min(landSpan, 6.8f);
                        addPatch("GenerativeExterior_CinematicContactReceiver" + std::to_string(i),
                                 receiverMesh,
                                 glm::vec3(x, 0.091f + static_cast<float>(i % 6) * 0.001f, z),
                                 glm::vec3(0.30f + pseudo(i, 2.73f) * 0.42f,
                                           1.0f,
                                           0.08f + pseudo(i, 3.49f) * 0.12f),
                                 glm::vec3(0.0f, glm::radians(pseudo(i, 4.03f) * 180.0f), 0.0f),
                                 glm::vec4(shadowTone, 0.070f),
                                 "shadow",
                                 0.94f,
                                 genExt.groundWetness * 0.10f,
                                 0.0f,
                                 0.070f,
                                 "");
                        contactReceivers++;
                    }

                    int wetVariation = 0;
                    if (genExt.waterOn) {
                        for (int i = 0; i < genExt.cinematicMaterialLighting.wetRoughnessVariationCount; ++i) {
                            const float x = (pseudo(i + 409, 1.67f) - 0.5f) * groundW * 0.76f;
                            const float z = shoreZ + 0.10f + pseudo(i + 421, 2.37f) * 1.35f;
                            const glm::vec3 wetColor = glm::mix(gcol * 0.42f, genExt.waterShallow * 0.62f, 0.22f + 0.08f * pseudo(i, 2.01f));
                            addPatch("GenerativeExterior_CinematicWetRoughness" + std::to_string(i),
                                     planeMesh,
                                     glm::vec3(x, 0.094f + static_cast<float>(i % 4) * 0.001f, z),
                                     glm::vec3(0.82f + pseudo(i, 2.71f) * 1.15f,
                                               1.0f,
                                               0.055f + pseudo(i, 3.13f) * 0.060f),
                                     glm::vec3(0.0f, glm::radians(-9.0f + pseudo(i, 3.71f) * 18.0f), 0.0f),
                                     glm::vec4(glm::max(wetColor, glm::vec3(0.014f)), 0.045f),
                                     "wet_masonry",
                                     0.34f + 0.18f * pseudo(i, 4.41f),
                                     0.82f,
                                     0.32f,
                                     0.045f,
                                     "",
                                     false,
                                     true);
                            wetVariation++;
                        }
                    }

                    int volumetricSlices = 0;
                    const glm::vec3 sliceColor = alpineModule
                        ? glm::vec3(0.34f, 0.46f, 0.86f)
                        : (canyonModule ? glm::vec3(1.0f, 0.56f, 0.28f) : glm::vec3(1.0f, 0.48f, 0.22f));
                    for (int i = 0; i < genExt.cinematicMaterialLighting.volumetricLightSliceCount; ++i) {
                        const float side = (i % 2 == 0) ? -1.0f : 1.0f;
                        const float z = shoreZ - 1.10f - 0.48f * static_cast<float>(i % 4);
                        addPatch("GenerativeExterior_CinematicVolumetricSlice" + std::to_string(i),
                                 cubeMesh,
                                 glm::vec3(side * (groundW * 0.34f + 0.52f * static_cast<float>(i % 3)),
                                           1.55f + 0.14f * static_cast<float>(i / 2),
                                           z),
                                 glm::vec3(0.018f,
                                           0.70f + 0.10f * static_cast<float>(i % 3),
                                           0.30f + 0.05f * static_cast<float>(i % 2)),
                                 glm::vec3(glm::radians(-10.0f + 3.0f * static_cast<float>(i % 3)),
                                           glm::radians(side * (18.0f + 3.0f * static_cast<float>(i))),
                                           glm::radians(side * 4.0f)),
                                 glm::vec4(glm::mix(sliceColor, glm::vec3(0.42f, 0.44f, 0.46f), 0.78f),
                                           alpineModule ? 0.006f : 0.005f),
                                 "naturalistic",
                                 0.92f,
                                 0.0f,
                                 0.0f,
                                 alpineModule ? 0.006f : 0.005f,
                                 "");
                        volumetricSlices++;
                    }

                    int localizedLights = 0;
                    if (campsiteModule) {
                        AddAssetLedSpotLight(*m_registry, "GenerativeExterior_CinematicCampfireShadowKey",
                                             glm::vec3(-2.4f, 2.0f, 2.65f), glm::vec3(1.2f, 0.35f, 0.62f),
                                             glm::vec3(1.0f, 0.42f, 0.16f), 4.8f, 13.0f, true);
                        AddAssetLedSpotLight(*m_registry, "GenerativeExterior_CinematicDawnGroundRake",
                                             glm::vec3(5.8f, 3.2f, -2.4f), glm::vec3(0.2f, 0.18f, 1.2f),
                                             glm::vec3(1.0f, 0.56f, 0.24f), 4.2f, 18.0f, true);
                        AddAssetLedPointLight(*m_registry, "GenerativeExterior_CinematicTentWarmBounce",
                                              glm::vec3(2.35f, 0.70f, 0.86f), glm::vec3(1.0f, 0.32f, 0.16f), 2.2f, 4.2f);
                        localizedLights = 3;
                    } else if (canyonModule) {
                        AddAssetLedSpotLight(*m_registry, "GenerativeExterior_CinematicCanyonWallShadowKey",
                                             glm::vec3(-6.5f, 4.1f, -2.2f), glm::vec3(2.0f, 0.62f, -7.0f),
                                             glm::vec3(1.0f, 0.62f, 0.30f), 5.2f, 22.0f, true);
                        AddAssetLedSpotLight(*m_registry, "GenerativeExterior_CinematicRiverRim",
                                             glm::vec3(4.6f, 2.6f, 0.4f), glm::vec3(0.0f, 0.22f, shoreZ - 1.5f),
                                             glm::vec3(0.36f, 0.82f, 0.92f), 2.8f, 15.0f, true);
                        localizedLights = 2;
                    } else {
                        AddAssetLedSpotLight(*m_registry, "GenerativeExterior_CinematicMoonShadowKey",
                                             glm::vec3(-4.8f, 4.4f, -1.8f), glm::vec3(0.8f, 0.42f, 1.5f),
                                             glm::vec3(0.38f, 0.52f, 1.0f), 4.2f, 18.0f, true);
                        AddAssetLedPointLight(*m_registry, "GenerativeExterior_CinematicCabinBounce",
                                              glm::vec3(0.2f, 0.78f, 2.1f), glm::vec3(1.0f, 0.54f, 0.24f), 2.5f, 4.6f);
                        localizedLights = 2;
                    }

                    renderer->SetSSAOParams(std::max(genExt.graphicsSSAORadius, 1.32f),
                                            std::min(genExt.graphicsSSAOBias, 0.016f),
                                            std::max(genExt.graphicsSSAOIntensity, alpineModule ? 2.72f : 2.95f));
                    renderer->SetShadowBias(std::min(genExt.graphicsShadowBias, 0.0015f));
                    renderer->SetShadowPCFRadius(std::max(genExt.graphicsShadowPCF, 3.25f));
                    renderer->SetGodRayIntensity(std::max(canyonModule ? 0.12f : 0.24f, alpineModule ? 0.16f : 0.20f));

                    spdlog::info("generative_exterior: cinematic material lighting triplanar_layers={} relief_patches={} shadow_casters={} contact_receivers={} localized_lights={} volumetric_slices={} wet_variation={}",
                                 triplanarLayers,
                                 reliefPatches,
                                 shadowCasters,
                                 contactReceivers,
                                 localizedLights,
                                 volumetricSlices,
                                 wetVariation);
                }
            }
            if (genExt.worldGeometry.enabled) {
                const float canyonHalfWidth = genExt.worldGeometry.canyonWidthM > 1.0f
                    ? genExt.worldGeometry.canyonWidthM * 0.5f
                    : genExt.extent * 0.36f;
                const float wallHeight = genExt.worldGeometry.wallHeightM > 1.0f
                    ? genExt.worldGeometry.wallHeightM
                    : std::max(5.5f, genExt.terrainRelief * 10.0f);
                const uint32_t cliffBands = genExt.meshSilhouetteRealism.enabled
                    ? static_cast<uint32_t>(std::max(1, genExt.meshSilhouetteRealism.cliffMeshVerticalBands))
                    : 1u;
                auto cliffMesh = CreateGenerativeCliffWallMesh(genExt.extent * 1.08f,
                                                               wallHeight,
                                                               genExt.meshSilhouetteRealism.enabled ? 1.24f : 0.95f,
                                                               4.31f,
                                                               cliffBands);
                auto shardMesh = CreateGenerativeRockShardMesh(2.17f);
                auto cubeMesh = Utils::MeshGenerator::CreateCube();
                const auto upCliff = renderer->UploadMesh(cliffMesh);
                const auto upShard = renderer->UploadMesh(shardMesh);
                const auto upCube = renderer->UploadMesh(cubeMesh);
                if (upCliff.IsErr() || upShard.IsErr() || upCube.IsErr()) {
                    spdlog::warn("generative_exterior: world geometry mesh upload failed cliff='{}' shard='{}' cube='{}'",
                                 upCliff.IsErr() ? upCliff.Error() : "ok",
                                 upShard.IsErr() ? upShard.Error() : "ok",
                                 upCube.IsErr() ? upCube.Error() : "ok");
                } else {
                    const glm::vec3 rockBase = genExt.groundKind == "dirt"
                        ? glm::vec3(0.58f, 0.24f, 0.14f)
                        : glm::mix(gcol, glm::vec3(0.34f, 0.34f, 0.32f), 0.55f);
                    auto dressRock = [&](Scene::RenderableComponent& r,
                                         const glm::vec3& color,
                                         float roughness,
                                         float normalScale,
                                         float proceduralMask) {
                        r.albedoColor = glm::vec4(glm::max(color, glm::vec3(0.018f)), 1.0f);
                        r.metallic = 0.0f;
                        r.roughness = roughness;
                        r.ao = 0.88f;
                        r.occlusionStrength = 0.76f;
                        r.normalScale = normalScale;
                        r.wetnessFactor = genExt.groundWetness * 0.35f;
                        r.proceduralMaskStrength = proceduralMask;
                        r.specularFactor = 0.24f;
                        r.clearcoatFactor = std::min(genExt.groundWetness * 0.24f, 0.18f);
                        r.clearcoatRoughnessFactor = 0.72f;
                        r.anisotropyStrength = 0.10f;
                        r.doubleSided = true;
                        r.presetName = "masonry";
                        applyGeneratedTextureMaterial(r, "rock_cliff");
                    };

                    int canyonWalls = 0;
                    for (int i = 0; i < genExt.worldGeometry.canyonWallLayers; ++i) {
                        const int row = i / 2;
                        const float side = (i % 2 == 0) ? -1.0f : 1.0f;
                        entt::entity wall = m_registry->CreateEntity();
                        m_registry->AddComponent<Scene::TagComponent>(
                            wall, "GenerativeExterior_CanyonWall" + std::to_string(i));
                        auto& t = m_registry->AddComponent<TransformComponent>(wall);
                        t.position = glm::vec3(side * (canyonHalfWidth + row * 2.7f),
                                               0.0f,
                                               -12.0f - static_cast<float>(row) * 8.5f);
                        t.rotation = glm::quat(glm::vec3(0.0f, side > 0.0f ? glm::pi<float>() : 0.0f, 0.0f));
                        t.scale = glm::vec3(1.0f, std::max(0.55f, 1.0f - row * 0.08f), 1.0f + row * 0.12f);
                        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(wall);
                        r.mesh = cliffMesh;
                        const glm::vec3 color = glm::mix(rockBase, glm::vec3(0.18f, 0.09f, 0.065f), row * 0.11f);
                        dressRock(r, color, 0.88f, 0.82f, 0.72f);
                        canyonWalls++;
                    }

                    int strataCount = 0;
                    for (int i = 0; i < genExt.worldGeometry.redRockStrataLayers; ++i) {
                        const int band = i / 2;
                        const float side = (i % 2 == 0) ? -1.0f : 1.0f;
                        entt::entity strata = m_registry->CreateEntity();
                        m_registry->AddComponent<Scene::TagComponent>(
                            strata, "GenerativeExterior_RedRockStrata" + std::to_string(i));
                        auto& t = m_registry->AddComponent<TransformComponent>(strata);
                        t.position = glm::vec3(side * (canyonHalfWidth - 0.18f),
                                               1.05f + band * 0.86f,
                                               -11.0f - band * 4.25f);
                        t.scale = glm::vec3(0.12f, 0.045f, genExt.extent * (0.30f + band * 0.025f));
                        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(strata);
                        r.mesh = cubeMesh;
                        const glm::vec3 stripe = (band % 2 == 0)
                            ? glm::vec3(0.78f, 0.40f, 0.18f)
                            : glm::vec3(0.36f, 0.13f, 0.09f);
                        dressRock(r, stripe, 0.82f, 0.45f, 0.56f);
                        strataCount++;
                    }

                    int overhangCount = 0;
                    if (genExt.meshSilhouetteRealism.enabled && genExt.meshSilhouetteRealism.cliffOverhangCount > 0) {
                        for (int i = 0; i < genExt.meshSilhouetteRealism.cliffOverhangCount; ++i) {
                            const float side = (i % 2 == 0) ? -1.0f : 1.0f;
                            const int band = (i / 2) % std::max(1, genExt.meshSilhouetteRealism.cliffMeshVerticalBands);
                            entt::entity ledge = m_registry->CreateEntity();
                            m_registry->AddComponent<Scene::TagComponent>(
                                ledge, "GenerativeExterior_CliffSilhouetteOverhang" + std::to_string(i));
                            auto& t = m_registry->AddComponent<TransformComponent>(ledge);
                            t.position = glm::vec3(side * (canyonHalfWidth - 0.32f - 0.10f * static_cast<float>(i % 3)),
                                                   0.95f + band * 0.54f,
                                                   -5.2f - static_cast<float>((i * 4) % 24));
                            t.rotation = glm::quat(glm::vec3(glm::radians(-3.0f + (i % 5) * 1.4f),
                                                             side > 0.0f ? glm::pi<float>() : 0.0f,
                                                             glm::radians(-5.0f + (i % 4) * 2.5f)));
                            t.scale = glm::vec3(0.34f + 0.05f * static_cast<float>(i % 4),
                                                0.095f + 0.014f * static_cast<float>(i % 3),
                                                0.70f + 0.13f * static_cast<float>(i % 5));
                            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(ledge);
                            r.mesh = cubeMesh;
                            dressRock(r,
                                      glm::mix(rockBase, glm::vec3(0.20f, 0.075f, 0.045f), 0.36f + 0.06f * (i % 4)),
                                      0.88f,
                                      0.64f,
                                      0.66f);
                            overhangCount++;
                        }
                    }

                    int erosionCount = 0;
                    int breakupCount = 0;
                    if (genExt.geometryRealism.enabled &&
                        (genExt.geometryRealism.cliffErosionRidgeCount > 0 ||
                         genExt.geometryRealism.strataBreakupCount > 0)) {
                        for (int i = 0; i < genExt.geometryRealism.cliffErosionRidgeCount; ++i) {
                            const float side = (i % 2 == 0) ? -1.0f : 1.0f;
                            const int band = (i / 2) % 6;
                            entt::entity ridge = m_registry->CreateEntity();
                            m_registry->AddComponent<Scene::TagComponent>(
                                ridge, "GenerativeExterior_CliffErosionRidge" + std::to_string(i));
                            auto& t = m_registry->AddComponent<TransformComponent>(ridge);
                            t.position = glm::vec3(side * (canyonHalfWidth - 0.08f - 0.04f * (i % 3)),
                                                   0.72f + band * (wallHeight * 0.105f),
                                                   -7.0f - static_cast<float>((i * 7) % 19));
                            t.rotation = glm::quat(glm::vec3(glm::radians(-2.0f + (i % 5) * 0.8f),
                                                             side > 0.0f ? glm::pi<float>() : 0.0f,
                                                             glm::radians(-3.0f + (i % 4) * 1.5f)));
                            t.scale = glm::vec3(0.070f,
                                                0.035f + 0.006f * static_cast<float>(i % 3),
                                                2.1f + 0.22f * static_cast<float>(i % 5));
                            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(ridge);
                            r.mesh = cubeMesh;
                            const glm::vec3 ridgeColor = (i % 2 == 0)
                                ? glm::vec3(0.80f, 0.42f, 0.20f)
                                : glm::vec3(0.30f, 0.11f, 0.075f);
                            dressRock(r,
                                      glm::mix(rockBase, ridgeColor, 0.55f),
                                      0.86f,
                                      0.50f + genExt.geometryRealism.wallNormalBreakup * 0.28f,
                                      0.64f);
                            erosionCount++;
                        }
                        for (int i = 0; i < genExt.geometryRealism.strataBreakupCount; ++i) {
                            const float side = (i % 2 == 0) ? -1.0f : 1.0f;
                            entt::entity crack = m_registry->CreateEntity();
                            m_registry->AddComponent<Scene::TagComponent>(
                                crack, "GenerativeExterior_CliffVerticalCrack" + std::to_string(i));
                            auto& t = m_registry->AddComponent<TransformComponent>(crack);
                            t.position = glm::vec3(side * (canyonHalfWidth - 0.055f),
                                                   1.05f + static_cast<float>(i % 5) * 0.72f,
                                                   -5.8f - static_cast<float>((i * 5) % 23));
                            t.rotation = glm::quat(glm::vec3(glm::radians(1.5f * (i % 3 - 1)),
                                                             side > 0.0f ? glm::pi<float>() : 0.0f,
                                                             glm::radians(-4.0f + (i % 5) * 2.0f)));
                            t.scale = glm::vec3(0.055f,
                                                0.48f + 0.08f * static_cast<float>(i % 4),
                                                0.030f);
                            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(crack);
                            r.mesh = cubeMesh;
                            dressRock(r,
                                      glm::vec3(0.11f, 0.045f, 0.035f),
                                      0.95f,
                                      0.28f,
                                      0.48f);
                            breakupCount++;
                        }
                    }

                    int talusCount = 0;
                    for (int i = 0; i < genExt.worldGeometry.talusClusterCount; ++i) {
                        const float side = (i % 2 == 0) ? -1.0f : 1.0f;
                        const int lane = (i / 2) % 4;
                        entt::entity talus = m_registry->CreateEntity();
                        m_registry->AddComponent<Scene::TagComponent>(
                            talus, "GenerativeExterior_TalusRock" + std::to_string(i));
                        auto& t = m_registry->AddComponent<TransformComponent>(talus);
                        t.position = glm::vec3(side * (canyonHalfWidth - 2.1f - lane * 0.65f),
                                               0.12f,
                                               -3.5f - static_cast<float>(i % 9) * 2.35f);
                        t.rotation = glm::quat(glm::vec3(glm::radians((i % 3) * 4.0f),
                                                         glm::radians(31.0f * i),
                                                         glm::radians((i % 5 - 2) * 5.0f)));
                        const float s = 0.72f + 0.13f * static_cast<float>(i % 5);
                        t.scale = glm::vec3(s * 1.15f, s * 0.62f, s * 0.90f);
                        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(talus);
                        r.mesh = shardMesh;
                        dressRock(r, glm::mix(rockBase, glm::vec3(0.24f, 0.10f, 0.07f), 0.25f + (i % 4) * 0.08f),
                                  0.90f,
                                  0.70f,
                                  0.62f);
                        talusCount++;
                    }

                    int foregroundCount = 0;
                    for (int i = 0; i < genExt.worldGeometry.foregroundOccluderCount; ++i) {
                        const float side = (i % 2 == 0) ? -1.0f : 1.0f;
                        entt::entity fg = m_registry->CreateEntity();
                        m_registry->AddComponent<Scene::TagComponent>(
                            fg, "GenerativeExterior_ForegroundOccluder" + std::to_string(i));
                        auto& t = m_registry->AddComponent<TransformComponent>(fg);
                        t.position = glm::vec3(side * (5.4f + (i % 3) * 1.35f),
                                               0.18f,
                                               5.3f - static_cast<float>(i / 2) * 1.15f);
                        t.rotation = glm::quat(glm::vec3(glm::radians(-5.0f + i * 3.0f),
                                                         glm::radians(47.0f * i),
                                                         glm::radians(6.0f - i * 2.0f)));
                        const float s = 1.28f + 0.22f * static_cast<float>(i % 3);
                        t.scale = glm::vec3(s * 1.25f, s * 0.82f, s);
                        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(fg);
                        r.mesh = shardMesh;
                        dressRock(r, glm::mix(rockBase, glm::vec3(0.06f, 0.045f, 0.040f), 0.38f),
                                  0.93f,
                                  0.74f,
                                  0.66f);
                        foregroundCount++;
                    }

                    spdlog::info("generative_exterior: created world geometry depth_bands={} foreground={} ridge_layers={} shoreline={} talus={} strata={}",
                                 genExt.worldGeometry.depthBandCount,
                                 foregroundCount,
                                 genExt.ridgeLayers.size(),
                                 genExt.worldGeometry.shorelineSegmentCount,
                                 talusCount,
                                 strataCount);
                    if (canyonWalls > 0) {
                        spdlog::info("generative_exterior: created canyon wall layer(s) {}", canyonWalls);
                    }
                    if (canyonWalls > 0 && genExt.meshSilhouetteRealism.enabled) {
                        spdlog::info("generative_exterior: created faceted cliff mesh vertical_bands={} overhangs={}",
                                     cliffBands,
                                     overhangCount);
                    }
                    if (foregroundCount > 0) {
                        spdlog::info("generative_exterior: created foreground occluder(s) {}", foregroundCount);
                    }
                    if (erosionCount > 0 || breakupCount > 0) {
                        spdlog::info("generative_exterior: created cliff erosion detail ridges={} cracks={}",
                                     erosionCount,
                                     breakupCount);
                    }
                }
            }
            if (genExt.surfaceDetail.enabled) {
                auto detailShardMesh = CreateGenerativeRockShardMesh(5.73f);
                auto ribbonMesh = Utils::MeshGenerator::CreatePlane(1.0f, 1.0f);
                auto diskMesh = Utils::MeshGenerator::CreateDisk(1.0f, 32);
                const auto upDetailShard = renderer->UploadMesh(detailShardMesh);
                const auto upRibbon = renderer->UploadMesh(ribbonMesh);
                const auto upDisk = renderer->UploadMesh(diskMesh);
                if (upDetailShard.IsErr() || upRibbon.IsErr() || upDisk.IsErr()) {
                    spdlog::warn("generative_exterior: surface detail mesh upload failed shard='{}' ribbon='{}' disk='{}'",
                                 upDetailShard.IsErr() ? upDetailShard.Error() : "ok",
                                 upRibbon.IsErr() ? upRibbon.Error() : "ok",
                                 upDisk.IsErr() ? upDisk.Error() : "ok");
                } else {
                    auto pseudo = [](int i, float f) -> float {
                        return std::sin(static_cast<float>(i) * f) * 0.5f + 0.5f;
                    };
                    auto dressOverlay = [&](Scene::RenderableComponent& r,
                                            const glm::vec4& color,
                                            float roughness,
                                            float normalScale,
                                            float wetness,
                                            float clearcoat) {
                        r.albedoColor = color;
                        r.metallic = 0.0f;
                        r.roughness = roughness;
                        r.ao = 0.60f;
                        r.occlusionStrength = 0.52f;
                        r.normalScale = normalScale;
                        r.wetnessFactor = wetness;
                        r.proceduralMaskStrength = 0.24f;
                        r.specularFactor = 0.22f + clearcoat * 0.55f;
                        r.clearcoatFactor = clearcoat;
                        r.clearcoatRoughnessFactor = 0.62f;
                        r.doubleSided = true;
                        r.alphaMode = Scene::RenderableComponent::AlphaMode::Blend;
                        r.renderLayer = Scene::RenderableComponent::RenderLayer::Overlay;
                        r.presetName = "naturalistic";
                    };

                    int ribbonCount = 0;
                    const float landSpan = std::max(groundNear - shoreZ, 8.0f);
                    for (int i = 0; i < genExt.surfaceDetail.occlusionRibbonCount; ++i) {
                        entt::entity ribbon = m_registry->CreateEntity();
                        m_registry->AddComponent<Scene::TagComponent>(
                            ribbon, "GenerativeExterior_OcclusionRibbon" + std::to_string(i));
                        auto& t = m_registry->AddComponent<TransformComponent>(ribbon);
                        const float x = (pseudo(i, 1.91f) - 0.5f) * groundW * 0.62f;
                        const float z = shoreZ + 1.1f + pseudo(i + 3, 2.17f) * (landSpan * 0.72f);
                        t.position = glm::vec3(x, 0.036f + i * 0.0007f, z);
                        t.rotation = glm::quat(glm::vec3(0.0f, glm::radians(-34.0f + pseudo(i, 0.77f) * 68.0f), 0.0f));
                        t.scale = glm::vec3(2.2f + pseudo(i, 1.31f) * 2.1f,
                                            1.0f,
                                            0.20f + pseudo(i + 7, 1.13f) * 0.20f);
                        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(ribbon);
                        r.mesh = ribbonMesh;
                        const float alpha = 0.12f + genExt.surfaceDetail.contactShadowStrength * 0.20f;
                        dressOverlay(r, glm::vec4(glm::max(gcol * 0.18f, glm::vec3(0.012f)), alpha),
                                     0.92f,
                                     0.10f,
                                     0.06f,
                                     0.02f);
                        ribbonCount++;
                    }

                    int creaseCount = 0;
                    for (int i = 0; i < genExt.surfaceDetail.terrainCreaseCount; ++i) {
                        entt::entity crease = m_registry->CreateEntity();
                        m_registry->AddComponent<Scene::TagComponent>(
                            crease, "GenerativeExterior_TerrainCrease" + std::to_string(i));
                        auto& t = m_registry->AddComponent<TransformComponent>(crease);
                        const float x = (pseudo(i + 11, 2.03f) - 0.5f) * groundW * 0.74f;
                        const float z = shoreZ + 0.5f + pseudo(i + 5, 1.57f) * (landSpan * 0.58f);
                        t.position = glm::vec3(x, 0.041f + i * 0.0009f, z);
                        t.rotation = glm::quat(glm::vec3(0.0f, glm::radians(18.0f + pseudo(i, 2.41f) * 144.0f), 0.0f));
                        t.scale = glm::vec3(1.0f + pseudo(i, 0.97f) * 1.3f,
                                            1.0f,
                                            0.055f + pseudo(i, 1.71f) * 0.065f);
                        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(crease);
                        r.mesh = ribbonMesh;
                        dressOverlay(r, glm::vec4(glm::max(gcol * 0.20f, glm::vec3(0.012f)), 0.18f),
                                     0.90f,
                                     0.18f,
                                     genExt.groundWetness * 0.22f,
                                     0.04f);
                        creaseCount++;
                    }

                    int pebbleCount = 0;
                    const glm::vec3 pebbleBase = genExt.groundKind == "dirt"
                        ? glm::vec3(0.42f, 0.17f, 0.10f)
                        : glm::mix(gcol, glm::vec3(0.30f, 0.31f, 0.29f), 0.55f);
                    for (int i = 0; i < genExt.surfaceDetail.pebbleCount; ++i) {
                        entt::entity pebble = m_registry->CreateEntity();
                        m_registry->AddComponent<Scene::TagComponent>(
                            pebble, "GenerativeExterior_MicroPebble" + std::to_string(i));
                        auto& t = m_registry->AddComponent<TransformComponent>(pebble);
                        const float x = (pseudo(i + 19, 1.67f) - 0.5f) * groundW * 0.76f;
                        const float z = shoreZ + 0.9f + pseudo(i + 23, 2.29f) * (landSpan * 0.66f);
                        t.position = glm::vec3(x, 0.050f, z);
                        t.rotation = glm::quat(glm::vec3(glm::radians(-3.0f + pseudo(i, 2.11f) * 6.0f),
                                                         glm::radians(pseudo(i, 3.03f) * 360.0f),
                                                         glm::radians(-4.0f + pseudo(i, 1.23f) * 8.0f)));
                        const float s = 0.10f + pseudo(i, 0.83f) * 0.18f;
                        t.scale = glm::vec3(s * (1.0f + pseudo(i, 1.17f) * 0.8f), s * 0.42f, s);
                        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(pebble);
                        r.mesh = detailShardMesh;
                        const glm::vec3 color = glm::mix(pebbleBase, glm::vec3(0.10f, 0.09f, 0.08f), pseudo(i, 1.49f) * 0.38f);
                        r.albedoColor = glm::vec4(glm::max(color, glm::vec3(0.018f)), 1.0f);
                        r.metallic = 0.0f;
                        r.roughness = 0.86f;
                        r.ao = 0.78f;
                        r.occlusionStrength = 0.70f;
                        r.normalScale = 0.36f;
                        r.wetnessFactor = genExt.groundWetness * (0.25f + pseudo(i, 2.71f) * 0.30f);
                        r.proceduralMaskStrength = 0.38f;
                        r.specularFactor = 0.20f;
                        r.clearcoatFactor = std::min(r.wetnessFactor * 0.24f, 0.12f);
                        r.clearcoatRoughnessFactor = 0.74f;
                        r.doubleSided = true;
                        r.presetName = "stone";
                        pebbleCount++;
                    }

                    int shoreFoamCount = 0;
                    if (genExt.waterOn) {
                        for (int i = 0; i < genExt.surfaceDetail.shoreFoamSegmentCount; ++i) {
                            entt::entity foam = m_registry->CreateEntity();
                            m_registry->AddComponent<Scene::TagComponent>(
                                foam, "GenerativeExterior_ShoreFoamWetline" + std::to_string(i));
                            auto& t = m_registry->AddComponent<TransformComponent>(foam);
                            const float x = (pseudo(i + 31, 1.43f) - 0.5f) * groundW * 0.78f;
                            t.position = glm::vec3(x, genExt.waterLevel + 0.018f + i * 0.0006f, shoreZ - 0.08f + pseudo(i, 1.77f) * 0.34f);
                            t.rotation = glm::quat(glm::vec3(0.0f, glm::radians(-7.0f + pseudo(i, 1.01f) * 14.0f), 0.0f));
                            t.scale = glm::vec3(1.7f + pseudo(i, 1.91f) * 1.4f,
                                                1.0f,
                                                0.055f + pseudo(i, 2.73f) * 0.035f);
                            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(foam);
                            r.mesh = ribbonMesh;
                            const glm::vec3 foamColor = glm::mix(glm::vec3(0.92f), genExt.waterShallow, 0.28f);
                            dressOverlay(r, glm::vec4(foamColor, 0.18f),
                                         0.38f,
                                         0.08f,
                                         0.52f,
                                         0.22f);
                            shoreFoamCount++;
                        }
                    }

                    int wetGlintCount = 0;
                    for (int i = 0; i < genExt.surfaceDetail.wetGlintCount; ++i) {
                        entt::entity glint = m_registry->CreateEntity();
                        m_registry->AddComponent<Scene::TagComponent>(
                            glint, "GenerativeExterior_WetSpecularGlint" + std::to_string(i));
                        auto& t = m_registry->AddComponent<TransformComponent>(glint);
                        const float x = (pseudo(i + 43, 2.19f) - 0.5f) * groundW * 0.60f;
                        const float z = genExt.waterOn
                            ? shoreZ + 0.45f + pseudo(i, 1.29f) * 3.2f
                            : shoreZ + 2.0f + pseudo(i, 1.29f) * (landSpan * 0.28f);
                        t.position = glm::vec3(x, 0.045f + i * 0.0005f, z);
                        t.rotation = glm::quat(glm::vec3(0.0f, glm::radians(10.0f + pseudo(i, 2.63f) * 160.0f), 0.0f));
                        t.scale = glm::vec3(0.60f + pseudo(i, 1.49f) * 0.95f,
                                            1.0f,
                                            0.035f + pseudo(i, 0.91f) * 0.055f);
                        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(glint);
                        r.mesh = ribbonMesh;
                        const glm::vec3 glintColor = genExt.waterOn
                            ? glm::mix(genExt.waterShallow, glm::vec3(1.0f), 0.18f)
                            : glm::mix(gcol, glm::vec3(1.0f), 0.08f);
                        dressOverlay(r, glm::vec4(glintColor, 0.055f),
                                     0.30f,
                                     0.05f,
                                     0.46f,
                                     0.24f);
                        wetGlintCount++;
                    }

                    spdlog::info("generative_exterior: created occlusion layering ribbons={} creases={} strength={:.2f}",
                                 ribbonCount,
                                 creaseCount,
                                 genExt.surfaceDetail.contactShadowStrength);
                    spdlog::info("generative_exterior: created surface detail pebbles={} shore_foam={} wet_glints={}",
                                 pebbleCount,
                                 shoreFoamCount,
                                 wetGlintCount);
                }
            }
            if (genExt.graphicsMaterials) {
                spdlog::info("generative_exterior: graphics material pass ground_normal={:.2f} ground_wetness={:.2f} procedural={:.2f} contacts={}",
                             genExt.groundNormalScale,
                             genExt.groundWetness,
                             genExt.groundProceduralMask,
                             genExt.contactPatches.size());
                spdlog::info("generative_exterior: graphics shader material pass advanced_terms={} occlusion_ribbons={} pebbles={}",
                             genExt.advancedShaderTermCount,
                             genExt.surfaceDetail.occlusionRibbonCount,
                             genExt.surfaceDetail.pebbleCount);
            }
            if (genExt.surfaceMaterialRichness.enabled) {
                auto decalMesh = Utils::MeshGenerator::CreatePlane(1.0f, 1.0f);
                auto cubeMesh = Utils::MeshGenerator::CreateCube();
                const auto upDecal = renderer->UploadMesh(decalMesh);
                const auto upCube = renderer->UploadMesh(cubeMesh);
                if (upDecal.IsErr() || upCube.IsErr()) {
                    spdlog::warn("generative_exterior: material richness mesh upload failed decal='{}' cube='{}'",
                                 upDecal.IsErr() ? upDecal.Error() : "ok",
                                 upCube.IsErr() ? upCube.Error() : "ok");
                } else {
                    auto pseudo = [](int i, float f) -> float {
                        return std::sin(static_cast<float>(i) * f) * 0.5f + 0.5f;
                    };
                    auto dressOverlay = [&](Scene::RenderableComponent& r,
                                            const glm::vec4& color,
                                            const char* preset,
                                            float roughness,
                                            float normalScale,
                                            float wetness,
                                            float clearcoat) {
                        r.albedoColor = color;
                        r.metallic = 0.0f;
                        r.roughness = roughness;
                        r.ao = 0.70f;
                        r.occlusionStrength = 0.58f;
                        r.normalScale = normalScale;
                        r.wetnessFactor = wetness;
                        r.proceduralMaskStrength = 0.42f;
                        r.specularFactor = 0.18f + clearcoat * 0.48f;
                        r.clearcoatFactor = clearcoat;
                        r.clearcoatRoughnessFactor = 0.70f;
                        r.anisotropyStrength = 0.16f;
                        r.doubleSided = true;
                        r.alphaMode = Scene::RenderableComponent::AlphaMode::Blend;
                        r.renderLayer = Scene::RenderableComponent::RenderLayer::Overlay;
                        r.presetName = preset;
                    };
                    auto addDecal = [&](const std::string& tag,
                                        const glm::vec3& position,
                                        const glm::vec3& scale,
                                        float yaw,
                                        const glm::vec4& color,
                                        const char* preset,
                                        float roughness,
                                        float normalScale,
                                        float wetness,
                                        float clearcoat) {
                        entt::entity decal = m_registry->CreateEntity();
                        m_registry->AddComponent<Scene::TagComponent>(decal, tag);
                        auto& t = m_registry->AddComponent<TransformComponent>(decal);
                        t.position = position;
                        t.rotation = glm::quat(glm::vec3(0.0f, yaw, 0.0f));
                        t.scale = scale;
                        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(decal);
                        r.mesh = decalMesh;
                        dressOverlay(r, color, preset, roughness, normalScale, wetness, clearcoat);
                    };
                    auto addCubeMark = [&](const std::string& tag,
                                           const glm::vec3& position,
                                           const glm::vec3& scale,
                                           const glm::vec3& euler,
                                           const glm::vec4& color,
                                           const char* preset,
                                           float roughness,
                                           float normalScale) {
                        entt::entity mark = m_registry->CreateEntity();
                        m_registry->AddComponent<Scene::TagComponent>(mark, tag);
                        auto& t = m_registry->AddComponent<TransformComponent>(mark);
                        t.position = position;
                        t.rotation = glm::quat(euler);
                        t.scale = scale;
                        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(mark);
                        r.mesh = cubeMesh;
                        dressOverlay(r, color, preset, roughness, normalScale, genExt.groundWetness * 0.22f, 0.06f);
                    };

                    const bool desertSurface = genExt.groundKind == "dirt";
                    const float landSpan = std::max(groundNear - shoreZ, 8.0f);
                    int groundDecals = 0;
                    for (int i = 0; i < genExt.surfaceMaterialRichness.groundDecalCount; ++i) {
                        const float x = (pseudo(i + 101, 1.73f) - 0.5f) * groundW * 0.72f;
                        const float z = shoreZ + 0.85f + pseudo(i + 109, 2.21f) * landSpan * 0.70f;
                        const glm::vec3 base = desertSurface
                            ? glm::mix(gcol, glm::vec3(0.25f, 0.10f, 0.055f), 0.44f + 0.18f * pseudo(i, 0.91f))
                            : glm::mix(gcol, glm::vec3(0.11f, 0.15f, 0.10f), 0.34f + 0.22f * pseudo(i, 0.91f));
                        addDecal("GenerativeExterior_MaterialBreakup_Ground" + std::to_string(i),
                                 glm::vec3(x, 0.057f + i * 0.0005f, z),
                                 glm::vec3(0.52f + pseudo(i, 1.31f) * 1.10f,
                                           1.0f,
                                           0.22f + pseudo(i, 1.97f) * 0.52f),
                                 glm::radians(pseudo(i, 2.63f) * 180.0f),
                                 glm::vec4(glm::max(base, glm::vec3(0.018f)), desertSurface ? 0.20f : 0.16f),
                                 "naturalistic",
                                 0.92f,
                                 0.24f,
                                 genExt.groundWetness * 0.30f,
                                 0.04f);
                        groundDecals++;
                    }

                    int rockPatches = 0;
                    const int patchCount = genExt.surfaceMaterialRichness.rockLichenPatchCount;
                    for (int i = 0; i < patchCount; ++i) {
                        const float x = (pseudo(i + 151, 2.09f) - 0.5f) * groundW * 0.76f;
                        const float z = shoreZ + 0.45f + pseudo(i + 157, 1.61f) * landSpan * 0.58f;
                        const glm::vec3 lichen = desertSurface
                            ? glm::vec3(0.34f, 0.18f, 0.09f)
                            : glm::mix(glm::vec3(0.16f, 0.30f, 0.14f), glm::vec3(0.08f, 0.18f, 0.20f), pseudo(i, 0.77f));
                        addDecal("GenerativeExterior_MaterialBreakup_RockLichen" + std::to_string(i),
                                 glm::vec3(x, 0.065f + i * 0.0005f, z),
                                 glm::vec3(0.30f + pseudo(i, 1.13f) * 0.46f,
                                           1.0f,
                                           0.18f + pseudo(i, 1.79f) * 0.34f),
                                 glm::radians(20.0f + pseudo(i, 2.83f) * 140.0f),
                                 glm::vec4(lichen, desertSurface ? 0.15f : 0.19f),
                                 desertSurface ? "stone" : "foliage",
                                 desertSurface ? 0.90f : 0.78f,
                                 0.30f,
                                 genExt.groundWetness * 0.20f,
                                 desertSurface ? 0.03f : 0.08f);
                        rockPatches++;
                    }

                    int desertPatches = 0;
                    if (genExt.surfaceMaterialRichness.desertStrataPatchCount > 0) {
                        const float canyonHalfWidth = genExt.worldGeometry.canyonWidthM > 1.0f
                            ? genExt.worldGeometry.canyonWidthM * 0.5f
                            : genExt.extent * 0.36f;
                        for (int i = 0; i < genExt.surfaceMaterialRichness.desertStrataPatchCount; ++i) {
                            const float side = (i % 2 == 0) ? -1.0f : 1.0f;
                            const float y = 0.72f + static_cast<float>(i % 7) * 0.44f;
                            const float z = -4.4f - static_cast<float>((i * 5) % 25);
                            const glm::vec3 stripe = (i % 3 == 0)
                                ? glm::vec3(0.82f, 0.42f, 0.20f)
                                : glm::vec3(0.24f, 0.080f, 0.050f);
                            addCubeMark("GenerativeExterior_MaterialBreakup_DesertStrata" + std::to_string(i),
                                        glm::vec3(side * (canyonHalfWidth - 0.045f), y, z),
                                        glm::vec3(0.052f,
                                                  0.026f + 0.008f * static_cast<float>(i % 3),
                                                  0.72f + 0.16f * static_cast<float>(i % 4)),
                                        glm::vec3(glm::radians(-2.0f + static_cast<float>(i % 5)),
                                                  side > 0.0f ? glm::pi<float>() : 0.0f,
                                                  glm::radians(-5.0f + static_cast<float>(i % 4) * 2.0f)),
                                        glm::vec4(stripe, 0.72f),
                                        "masonry",
                                        0.86f,
                                        0.56f);
                            desertPatches++;
                        }
                    }

                    int vegetationClusters = 0;
                    int vegetationBlades = 0;
                    const glm::vec4 vegColor = desertSurface
                        ? glm::vec4(0.48f, 0.40f, 0.20f, 1.0f)
                        : glm::vec4(0.15f, 0.38f, 0.18f, 1.0f);
                    for (int i = 0; i < genExt.surfaceMaterialRichness.vegetationClusterCount; ++i) {
                        const float x = (pseudo(i + 211, 1.47f) - 0.5f) * groundW * 0.70f;
                        const float z = shoreZ + 1.2f + pseudo(i + 217, 1.89f) * landSpan * 0.62f;
                        const float clusterScale = desertSurface ? 0.76f : 1.0f;
                        for (int b = 0; b < 3; ++b) {
                            const float bx = x + (static_cast<float>(b) - 1.0f) * 0.055f;
                            const float bz = z + (pseudo(i + b + 229, 2.37f) - 0.5f) * 0.12f;
                            addCubeMark("GenerativeExterior_VegetationSurfaceCluster" + std::to_string(i) + "_" + std::to_string(b),
                                        glm::vec3(bx, 0.12f + 0.015f * b, bz),
                                        glm::vec3(0.035f,
                                                  (0.22f + 0.060f * b) * clusterScale,
                                                  0.030f),
                                        glm::vec3(glm::radians(-10.0f + 9.0f * static_cast<float>(b)),
                                                  glm::radians(35.0f * static_cast<float>(i + b)),
                                                  glm::radians(-6.0f + 6.0f * static_cast<float>(b))),
                                        vegColor,
                                        "foliage",
                                        desertSurface ? 0.84f : 0.64f,
                                        0.34f);
                            vegetationBlades++;
                        }
                        vegetationClusters++;
                    }

                    int heroLines = 0;
                    const int desiredHeroLines = genExt.surfaceMaterialRichness.heroMaterialLineCount;
                    if (!genExt.structures.empty()) {
                        const auto& structure = genExt.structures.front();
                        const float yawRad = glm::radians(structure.yawDeg);
                        const float cs = std::cos(yawRad);
                        const float sn = std::sin(yawRad);
                        auto place = [&](float x, float y, float z) -> glm::vec3 {
                            return structure.position + glm::vec3(cs * x + sn * z,
                                                                  y,
                                                                  -sn * x + cs * z);
                        };
                        const float frontZ = structure.depthM * 0.5f + 0.16f;
                        for (int i = 0; i < desiredHeroLines; ++i) {
                            const float row = static_cast<float>(i / 6);
                            const float col = static_cast<float>(i % 6);
                            addCubeMark("GenerativeExterior_HeroMaterialLine_Cabin" + std::to_string(i),
                                        place(-structure.widthM * 0.38f + col * structure.widthM * 0.15f,
                                              0.36f + row * 0.18f,
                                              frontZ),
                                        glm::vec3(structure.widthM * (0.060f + 0.012f * static_cast<float>(i % 3)),
                                                  0.018f,
                                                  0.024f),
                                        glm::vec3(0.0f, yawRad, glm::radians(-1.5f + static_cast<float>(i % 4))),
                                        glm::vec4(0.10f, 0.060f, 0.032f, 0.74f),
                                        "wood",
                                        0.82f,
                                        0.40f);
                            heroLines++;
                        }
                    } else {
                        const glm::vec3 tentCenter(2.9f, 0.0f, 0.9f);
                        const float tentYaw = glm::radians(-18.0f);
                        const float cs = std::cos(tentYaw);
                        const float sn = std::sin(tentYaw);
                        auto tentPlace = [&](float x, float y, float z) -> glm::vec3 {
                            return tentCenter + glm::vec3(cs * x + sn * z, y, -sn * x + cs * z);
                        };
                        for (int i = 0; i < desiredHeroLines; ++i) {
                            const float row = static_cast<float>(i / 6);
                            const float col = static_cast<float>(i % 6);
                            const float sideZ = (i % 2 == 0) ? -0.62f : 0.62f;
                            addCubeMark("GenerativeExterior_HeroMaterialLine_Tent" + std::to_string(i),
                                        tentPlace(-0.92f + col * 0.36f,
                                                  0.42f + row * 0.11f,
                                                  sideZ),
                                        glm::vec3(0.25f,
                                                  0.020f,
                                                  0.022f),
                                        glm::vec3(glm::radians(8.0f),
                                                  tentYaw,
                                                  glm::radians(-4.0f + static_cast<float>(i % 5) * 2.0f)),
                                        glm::vec4(0.070f, 0.040f, 0.050f, 0.72f),
                                        "fabric",
                                        0.76f,
                                        0.32f);
                            heroLines++;
                        }
                    }

                    spdlog::info("generative_exterior: created material breakup decals ground={} rock_lichen={} desert_strata={} hero_lines={}",
                                 groundDecals,
                                 rockPatches,
                                 desertPatches,
                                 heroLines);
                    spdlog::info("generative_exterior: created vegetation surface clusters clusters={} blades={}",
                                 vegetationClusters,
                                 vegetationBlades);
                }
            }
        }
    }

    if (genExt.valid && genExt.naturalisticEcology.enabled) {
        if (auto* renderer = m_renderer.get()) {
            auto grassMesh = LoadNaturalisticShowcaseMesh("grass_bermuda_01/grass_bermuda_01_1k.gltf");
            auto bushMesh = LoadNaturalisticShowcaseMesh("wild_rooibos_bush/wild_rooibos_bush_1k.gltf");
            auto fernMesh = LoadNaturalisticShowcaseMesh("fern_02/fern_02_1k.gltf");
            auto trunkMesh = LoadNaturalisticShowcaseMesh("dead_tree_trunk/dead_tree_trunk_1k.gltf");
            auto branchMesh = LoadNaturalisticShowcaseMesh("dry_branches_medium_01/dry_branches_medium_01_1k.gltf");
            auto stumpMesh = LoadNaturalisticShowcaseMesh("tree_stump_01/tree_stump_01_1k.gltf");
            auto mossRockMesh = LoadNaturalisticShowcaseMesh("rock_moss_set_01/rock_moss_set_01_1k.gltf");
            auto boulderMesh = LoadNaturalisticShowcaseMesh("boulder_01/boulder_01_1k.gltf");

            const bool uploadsOk =
                UploadAssetLedMesh(renderer, grassMesh, "grass_bermuda_01") &&
                UploadAssetLedMesh(renderer, bushMesh, "wild_rooibos_bush") &&
                UploadAssetLedMesh(renderer, fernMesh, "fern_02") &&
                UploadAssetLedMesh(renderer, trunkMesh, "dead_tree_trunk") &&
                UploadAssetLedMesh(renderer, branchMesh, "dry_branches_medium_01") &&
                UploadAssetLedMesh(renderer, stumpMesh, "tree_stump_01") &&
                UploadAssetLedMesh(renderer, mossRockMesh, "rock_moss_set_01") &&
                UploadAssetLedMesh(renderer, boulderMesh, "boulder_01");
            if (!uploadsOk) {
                spdlog::warn("generative_exterior: naturalistic ecology mesh upload failed");
            } else {
                const bool desertSurface = genExt.groundKind.find("dirt") != std::string::npos ||
                                           genExt.worldGeometry.canyonWallLayers > 0;
                const float groundW = genExt.extent * 1.82f;
                const float shoreZ = genExt.waterOn ? genExt.waterFromZ : -genExt.extent * 0.30f;
                const float landSpan = genExt.extent * 0.54f;
                auto pseudo = [](int i, float salt) {
                    const float n = std::sin(static_cast<float>(i) * 12.9898f + salt * 78.233f) * 43758.5453f;
                    return n - std::floor(n);
                };

                const AssetLedMaterialSettings wetBark{
                    glm::vec4(desertSurface ? glm::vec3(0.36f, 0.24f, 0.14f) : glm::vec3(0.18f, 0.11f, 0.070f), 1.0f),
                    0.0f,
                    desertSurface ? 0.78f : 0.58f,
                    0.0f,
                    1.5f,
                    glm::vec3(0.0f),
                    1.0f,
                    desertSurface ? 0.08f : std::min(0.62f, genExt.groundWetness + 0.20f),
                    0.42f,
                    false,
                    Scene::RenderableComponent::AlphaMode::Opaque,
                    Scene::RenderableComponent::RenderLayer::Opaque,
                    "wood"
                };
                const AssetLedMaterialSettings vegetation{
                    glm::vec4(desertSurface ? glm::vec3(0.45f, 0.36f, 0.18f) : glm::vec3(0.10f, 0.23f, 0.12f), 1.0f),
                    0.0f,
                    desertSurface ? 0.86f : 0.72f,
                    0.0f,
                    1.5f,
                    glm::vec3(0.0f),
                    1.0f,
                    desertSurface ? 0.03f : std::min(0.48f, genExt.groundWetness + 0.12f),
                    desertSurface ? 0.24f : 0.46f,
                    true,
                    Scene::RenderableComponent::AlphaMode::Opaque,
                    Scene::RenderableComponent::RenderLayer::Opaque,
                    "vegetation"
                };
                const AssetLedMaterialSettings stone{
                    glm::vec4(desertSurface ? glm::vec3(0.54f, 0.32f, 0.20f) : glm::vec3(0.17f, 0.20f, 0.16f), 1.0f),
                    0.0f,
                    desertSurface ? 0.76f : 0.62f,
                    0.0f,
                    1.5f,
                    glm::vec3(0.0f),
                    1.0f,
                    desertSurface ? 0.04f : std::min(0.70f, genExt.groundWetness + 0.22f),
                    desertSurface ? 0.42f : 0.66f,
                    false,
                    Scene::RenderableComponent::AlphaMode::Opaque,
                    Scene::RenderableComponent::RenderLayer::Opaque,
                    "mossy_masonry"
                };

                auto addNatural = [&](const std::string& tag,
                                      const char* assetId,
                                      const std::shared_ptr<Scene::MeshData>& mesh,
                                      const glm::vec3& position,
                                      const glm::vec3& scale,
                                      const glm::vec3& euler,
                                      const AssetLedMaterialSettings& material,
                                      int& counter) {
                    if (!mesh || !mesh->gpuBuffers) {
                        return;
                    }
                    AddAssetLedNaturalisticRenderable(*m_registry,
                                                      tag.c_str(),
                                                      assetId,
                                                      mesh,
                                                      position,
                                                      scale,
                                                      euler,
                                                      material);
                    counter++;
                };

                int grassInstances = 0;
                int bushInstances = 0;
                int fernInstances = 0;
                int trunkInstances = 0;
                int branchInstances = 0;
                int stumpInstances = 0;
                int rockInstances = 0;

                for (int i = 0; i < genExt.naturalisticEcology.grassClusterCount; ++i) {
                    float x = (pseudo(i + 701, 1.13f) - 0.5f) * groundW * 0.72f;
                    float z = shoreZ + 0.84f + pseudo(i + 709, 1.61f) * landSpan;
                    if (!desertSurface && i < 6) {
                        static const float anchorX[6] = { -12.4f, -9.2f, 10.6f, 13.0f, -5.6f, 6.8f };
                        static const float anchorZ[6] = { 3.6f, 5.1f, 3.1f, 5.4f, 2.4f, 2.8f };
                        x = anchorX[i];
                        z = anchorZ[i];
                    }
                    const float s = desertSurface
                        ? 0.12f + 0.030f * static_cast<float>(i % 3)
                        : 0.40f + 0.060f * static_cast<float>(i % 4);
                    addNatural("GenerativeExterior_NaturalisticGrass" + std::to_string(i),
                               "grass_bermuda_01",
                               grassMesh,
                               glm::vec3(x, 0.055f, z),
                               glm::vec3(s),
                               glm::vec3(0.0f, glm::radians(37.0f * static_cast<float>(i)), 0.0f),
                               vegetation,
                               grassInstances);
                }

                for (int i = 0; i < genExt.naturalisticEcology.bushClusterCount; ++i) {
                    const float side = (i % 2 == 0) ? -1.0f : 1.0f;
                    float x = side * (5.2f + pseudo(i + 733, 2.03f) * genExt.extent * 0.30f);
                    float z = shoreZ + 1.2f + pseudo(i + 739, 2.41f) * landSpan * 0.84f;
                    if (!desertSurface && i < 4) {
                        static const float anchorX[4] = { -14.2f, 14.0f, -8.4f, 8.8f };
                        static const float anchorZ[4] = { 2.9f, 3.2f, 5.7f, 5.2f };
                        x = anchorX[i];
                        z = anchorZ[i];
                    }
                    const float s = desertSurface ? 0.30f : 0.62f + 0.070f * static_cast<float>(i % 3);
                    addNatural("GenerativeExterior_NaturalisticBush" + std::to_string(i),
                               "wild_rooibos_bush",
                               bushMesh,
                               glm::vec3(x, 0.18f, z),
                               glm::vec3(s),
                               glm::vec3(0.0f, glm::radians(-24.0f + 31.0f * static_cast<float>(i)), 0.0f),
                               vegetation,
                               bushInstances);
                }

                for (int i = 0; i < genExt.naturalisticEcology.fernClusterCount; ++i) {
                    const float side = (i % 2 == 0) ? -1.0f : 1.0f;
                    const float x = side * (1.4f + pseudo(i + 751, 2.77f) * 7.2f);
                    const float z = shoreZ + 1.5f + pseudo(i + 757, 3.19f) * landSpan * 0.72f;
                    addNatural("GenerativeExterior_NaturalisticFern" + std::to_string(i),
                               "fern_02",
                               fernMesh,
                               glm::vec3(x, 0.060f, z),
                               glm::vec3(0.46f + 0.055f * static_cast<float>(i % 4)),
                               glm::vec3(0.0f, glm::radians(22.0f * static_cast<float>(i)), 0.0f),
                               vegetation,
                               fernInstances);
                }

                for (int i = 0; i < genExt.naturalisticEcology.trunkCount; ++i) {
                    const float x = -7.2f + static_cast<float>(i) * (14.4f / static_cast<float>(std::max(1, genExt.naturalisticEcology.trunkCount)));
                    const float z = shoreZ + 2.4f + pseudo(i + 769, 3.47f) * landSpan * 0.54f;
                    addNatural("GenerativeExterior_NaturalisticTrunk" + std::to_string(i),
                               "dead_tree_trunk",
                               trunkMesh,
                               glm::vec3(x, 0.10f, z),
                               glm::vec3(desertSurface ? 0.28f : 0.38f),
                               glm::vec3(glm::radians(-3.0f + 5.0f * pseudo(i, 4.1f)),
                                         glm::radians(18.0f + 53.0f * static_cast<float>(i)),
                                         glm::radians(-5.0f + 7.0f * pseudo(i, 4.7f))),
                               wetBark,
                               trunkInstances);
                }

                for (int i = 0; i < genExt.naturalisticEcology.branchCount; ++i) {
                    const float x = (pseudo(i + 787, 4.23f) - 0.5f) * groundW * 0.62f;
                    const float z = shoreZ + 1.0f + pseudo(i + 797, 4.67f) * landSpan * 0.84f;
                    addNatural("GenerativeExterior_NaturalisticBranch" + std::to_string(i),
                               "dry_branches_medium_01",
                               branchMesh,
                               glm::vec3(x, 0.075f + 0.020f * static_cast<float>(i % 2), z),
                               glm::vec3(desertSurface ? 0.48f : 0.54f),
                               glm::vec3(glm::radians(2.0f * static_cast<float>(i % 3)),
                                         glm::radians(-42.0f + 29.0f * static_cast<float>(i)),
                                         glm::radians(-6.0f + 4.0f * static_cast<float>(i % 4))),
                               wetBark,
                               branchInstances);
                }

                for (int i = 0; i < genExt.naturalisticEcology.stumpCount; ++i) {
                    const float side = (i % 2 == 0) ? -1.0f : 1.0f;
                    const float x = side * (2.2f + pseudo(i + 811, 5.03f) * 8.6f);
                    const float z = shoreZ + 2.0f + pseudo(i + 821, 5.41f) * landSpan * 0.62f;
                    addNatural("GenerativeExterior_NaturalisticStump" + std::to_string(i),
                               "tree_stump_01",
                               stumpMesh,
                               glm::vec3(x, 0.055f, z),
                               glm::vec3(desertSurface ? 0.24f : 0.32f),
                               glm::vec3(0.0f, glm::radians(34.0f * static_cast<float>(i)), glm::radians(-2.0f + 3.0f * static_cast<float>(i % 2))),
                               wetBark,
                               stumpInstances);
                }

                for (int i = 0; i < genExt.naturalisticEcology.mossRockCount; ++i) {
                    const bool useBoulder = desertSurface || (i % 2 == 0);
                    const auto& rockMesh = useBoulder ? boulderMesh : mossRockMesh;
                    const char* rockId = useBoulder ? "boulder_01" : "rock_moss_set_01";
                    const float x = (pseudo(i + 839, 5.89f) - 0.5f) * groundW * 0.78f;
                    const float z = shoreZ + 0.55f + pseudo(i + 853, 6.11f) * landSpan * 0.72f;
                    addNatural("GenerativeExterior_NaturalisticMossRock" + std::to_string(i),
                               rockId,
                               rockMesh,
                               glm::vec3(x, 0.040f, z),
                               glm::vec3(desertSurface ? 0.22f : 0.18f + 0.030f * static_cast<float>(i % 3)),
                               glm::vec3(glm::radians(-2.0f + 4.0f * pseudo(i, 6.7f)),
                                         glm::radians(27.0f * static_cast<float>(i)),
                                         glm::radians(-4.0f + 5.0f * pseudo(i, 7.1f))),
                               stone,
                               rockInstances);
                }

                const int totalNaturalistic = grassInstances + bushInstances + fernInstances +
                    trunkInstances + branchInstances + stumpInstances + rockInstances;
                if (totalNaturalistic > 0) {
                    spdlog::info("generative_exterior: created naturalistic ecology assets grass={} bush={} fern={} trunks={} branches={} stumps={} moss_rocks={}",
                                 grassInstances,
                                 bushInstances,
                                 fernInstances,
                                 trunkInstances,
                                 branchInstances,
                                 stumpInstances,
                                 rockInstances);
                } else {
                    spdlog::warn("generative_exterior: naturalistic ecology assets requested but no scanned instances were created");
                }
            }
        }
    }

    if (genExt.valid && genExt.sourceGeometryFidelity.enabled) {
        if (auto* renderer = m_renderer.get()) {
            auto lanternMesh = LoadNaturalisticShowcaseMesh("Lantern_01/Lantern_01_1k.gltf");
            auto tableMesh = LoadNaturalisticShowcaseMesh("WoodenTable_01/WoodenTable_01_1k.gltf");
            auto barrelMesh = LoadNaturalisticShowcaseMesh("Barrel_01/Barrel_01_1k.gltf");
            auto boulderMesh = LoadNaturalisticShowcaseMesh("boulder_01/boulder_01_1k.gltf");

            const bool uploadsOk =
                UploadAssetLedMesh(renderer, lanternMesh, "source-bound Lantern_01") &&
                UploadAssetLedMesh(renderer, tableMesh, "source-bound WoodenTable_01") &&
                UploadAssetLedMesh(renderer, barrelMesh, "source-bound Barrel_01") &&
                UploadAssetLedMesh(renderer, boulderMesh, "source-bound boulder_01");
            if (!uploadsOk) {
                spdlog::warn("generative_exterior: source-bound hero geometry mesh upload failed");
            } else {
                const bool hasCabin = !genExt.structures.empty();
                const float shoreZ = genExt.waterOn ? genExt.waterFromZ : -genExt.extent * 0.30f;
                const float wetness = std::clamp(genExt.groundWetness, 0.0f, 1.0f);
                const bool desertSurface = genExt.groundKind.find("dirt") != std::string::npos ||
                                           genExt.worldGeometry.canyonWallLayers > 0;

                const AssetLedMaterialSettings warmBrass{
                    glm::vec4(0.54f, 0.38f, 0.20f, 1.0f),
                    0.26f,
                    0.30f,
                    0.0f,
                    1.5f,
                    glm::vec3(1.0f, 0.58f, 0.26f),
                    0.70f,
                    wetness * 0.18f,
                    0.42f,
                    false,
                    Scene::RenderableComponent::AlphaMode::Opaque,
                    Scene::RenderableComponent::RenderLayer::Opaque,
                    "brass"
                };
                const AssetLedMaterialSettings wetWood{
                    glm::vec4(0.26f, 0.15f, 0.070f, 1.0f),
                    0.0f,
                    0.50f,
                    0.0f,
                    1.5f,
                    glm::vec3(0.0f),
                    1.0f,
                    std::min(0.64f, wetness + 0.12f),
                    0.48f,
                    false,
                    Scene::RenderableComponent::AlphaMode::Opaque,
                    Scene::RenderableComponent::RenderLayer::Opaque,
                    "wood"
                };
                const AssetLedMaterialSettings darkUtility{
                    glm::vec4(0.24f, 0.20f, 0.16f, 1.0f),
                    0.16f,
                    0.42f,
                    0.0f,
                    1.5f,
                    glm::vec3(0.0f),
                    1.0f,
                    wetness * 0.18f,
                    0.36f,
                    false,
                    Scene::RenderableComponent::AlphaMode::Opaque,
                    Scene::RenderableComponent::RenderLayer::Opaque,
                    "masonry"
                };
                const AssetLedMaterialSettings anchorStone{
                    glm::vec4(desertSurface ? glm::vec3(0.50f, 0.31f, 0.20f) : glm::vec3(0.16f, 0.19f, 0.15f), 1.0f),
                    0.0f,
                    desertSurface ? 0.74f : 0.58f,
                    0.0f,
                    1.5f,
                    glm::vec3(0.0f),
                    1.0f,
                    desertSurface ? 0.04f : std::min(0.68f, wetness + 0.18f),
                    desertSurface ? 0.38f : 0.62f,
                    false,
                    Scene::RenderableComponent::AlphaMode::Opaque,
                    Scene::RenderableComponent::RenderLayer::Opaque,
                    "mossy_masonry"
                };

                auto sourceSets = 0;
                sourceSets += (lanternMesh && lanternMesh->gpuBuffers) ? 1 : 0;
                sourceSets += (tableMesh && tableMesh->gpuBuffers) ? 1 : 0;
                sourceSets += (barrelMesh && barrelMesh->gpuBuffers) ? 1 : 0;
                sourceSets += (boulderMesh && boulderMesh->gpuBuffers) ? 1 : 0;

                auto addSource = [&](const std::string& tag,
                                     const char* assetId,
                                     const std::shared_ptr<Scene::MeshData>& mesh,
                                     const glm::vec3& position,
                                     const glm::vec3& scale,
                                     const glm::vec3& euler,
                                     const AssetLedMaterialSettings& material,
                                     int& counter) {
                    if (!mesh || !mesh->gpuBuffers) {
                        return;
                    }
                    AddAssetLedNaturalisticRenderable(*m_registry,
                                                      tag.c_str(),
                                                      assetId,
                                                      mesh,
                                                      position,
                                                      scale,
                                                      euler,
                                                      material);
                    counter++;
                };

                auto cabinPlace = [&](float x, float y, float z) -> glm::vec3 {
                    if (!hasCabin) {
                        return glm::vec3(x, y, z);
                    }
                    const auto& structure = genExt.structures.front();
                    const float yawRad = glm::radians(structure.yawDeg);
                    const float cs = std::cos(yawRad);
                    const float sn = std::sin(yawRad);
                    return structure.position + glm::vec3(cs * x + sn * z,
                                                          y,
                                                          -sn * x + cs * z);
                };

                int lanterns = 0;
                int utilityProps = 0;
                int anchorRocks = 0;

                for (int i = 0; i < genExt.sourceGeometryFidelity.scannedLanternCount; ++i) {
                    const glm::vec3 pos = hasCabin
                        ? cabinPlace(-1.15f + 2.30f * static_cast<float>(i % 2), 0.42f, 2.28f + 0.18f * static_cast<float>(i / 2))
                        : glm::vec3(0.94f + 0.46f * static_cast<float>(i), 0.38f, 1.18f + 0.16f * static_cast<float>(i % 2));
                    addSource("GenerativeExterior_SourceLantern" + std::to_string(i),
                              "Lantern_01",
                              lanternMesh,
                              pos,
                              glm::vec3(hasCabin ? 0.32f : 0.28f),
                              glm::vec3(0.0f, glm::radians(-18.0f + 24.0f * static_cast<float>(i)), 0.0f),
                              warmBrass,
                              lanterns);
                }

                for (int i = 0; i < genExt.sourceGeometryFidelity.scannedUtilityPropCount; ++i) {
                    const bool useTable = (i == 0);
                    const auto& mesh = useTable ? tableMesh : barrelMesh;
                    const char* assetId = useTable ? "WoodenTable_01" : "Barrel_01";
                    const glm::vec3 scale = useTable
                        ? glm::vec3(hasCabin ? 0.48f : 0.42f)
                        : glm::vec3(hasCabin ? 0.34f : 0.30f);
                    const glm::vec3 pos = hasCabin
                        ? cabinPlace(-0.48f + 0.72f * static_cast<float>(i), useTable ? 0.30f : 0.34f, 2.50f + 0.22f * static_cast<float>(i % 2))
                        : glm::vec3(-0.85f + 0.72f * static_cast<float>(i), useTable ? 0.28f : 0.33f, 1.52f - 0.24f * static_cast<float>(i % 2));
                    addSource("GenerativeExterior_SourceUtilityProp" + std::to_string(i),
                              assetId,
                              mesh,
                              pos,
                              scale,
                              glm::vec3(0.0f, glm::radians(-20.0f + 22.0f * static_cast<float>(i)), 0.0f),
                              useTable ? wetWood : darkUtility,
                              utilityProps);
                }

                for (int i = 0; i < genExt.sourceGeometryFidelity.scannedAnchorRockCount; ++i) {
                    const float side = (i % 2 == 0) ? -1.0f : 1.0f;
                    const float x = side * (2.2f + 1.25f * static_cast<float>(i / 2));
                    const float z = shoreZ + 0.62f + 0.58f * static_cast<float>(i % 3);
                    addSource("GenerativeExterior_SourceAnchorRock" + std::to_string(i),
                              "boulder_01",
                              boulderMesh,
                              glm::vec3(x, 0.055f, z),
                              glm::vec3(0.20f + 0.035f * static_cast<float>(i % 3)),
                              glm::vec3(glm::radians(-2.0f + 3.0f * static_cast<float>(i % 2)),
                                        glm::radians(31.0f * static_cast<float>(i)),
                                        glm::radians(-4.0f + 5.0f * static_cast<float>(i % 3))),
                              anchorStone,
                              anchorRocks);
                }

                const int heroAnchors = lanterns + utilityProps + anchorRocks;
                if (heroAnchors > 0) {
                    spdlog::info("generative_exterior: source-bound hero geometry lanterns={} utility_props={} anchor_rocks={} hero_anchors={} source_sets={}",
                                 lanterns,
                                 utilityProps,
                                 anchorRocks,
                                 heroAnchors,
                                 sourceSets);
                } else {
                    spdlog::warn("generative_exterior: source-bound hero geometry requested but no scanned hero meshes were created");
                }
            }
        }
    }

    if (genExt.valid && genExt.authoredSceneModule.enabled) {
        if (auto* renderer = m_renderer.get()) {
            auto cubeMesh = Utils::MeshGenerator::CreateCube();
            auto planeMesh = Utils::MeshGenerator::CreatePlane(1.0f, 1.0f);
            auto cylinderMesh = Utils::MeshGenerator::CreateCylinder(0.5f, 1.0f, 24);
            auto shardMesh = CreateGenerativeRockShardMesh(21.73f);
            auto cliffMesh = CreateGenerativeCliffWallMesh(genExt.extent * 0.34f,
                                                           std::max(4.2f, genExt.terrainRelief * 7.0f + 3.2f),
                                                           1.72f,
                                                           23.41f,
                                                           6u);
            const auto upCube = renderer->UploadMesh(cubeMesh);
            const auto upPlane = renderer->UploadMesh(planeMesh);
            const auto upCylinder = renderer->UploadMesh(cylinderMesh);
            const auto upShard = renderer->UploadMesh(shardMesh);
            const auto upCliff = renderer->UploadMesh(cliffMesh);
            if (upCube.IsErr() || upPlane.IsErr() || upCylinder.IsErr() || upShard.IsErr() || upCliff.IsErr()) {
                spdlog::warn("generative_exterior: authored scene module mesh upload failed cube='{}' plane='{}' cylinder='{}' shard='{}' cliff='{}'",
                             upCube.IsErr() ? upCube.Error() : "ok",
                             upPlane.IsErr() ? upPlane.Error() : "ok",
                             upCylinder.IsErr() ? upCylinder.Error() : "ok",
                             upShard.IsErr() ? upShard.Error() : "ok",
                             upCliff.IsErr() ? upCliff.Error() : "ok");
            } else {
                const std::string module = genExt.authoredSceneModule.moduleId.empty()
                    ? std::string("exterior_landscape_setpiece")
                    : genExt.authoredSceneModule.moduleId;
                const bool campsiteModule = module == "campsite_lake_dawn";
                const bool canyonModule = module == "desert_canyon_river";
                const bool alpineModule = module == "alpine_cabin_lake";
                const float shoreZ = genExt.waterOn ? (genExt.waterFromZ + 0.5f) : -genExt.extent * 0.32f;
                const float groundW = genExt.extent * 1.86f;
                const glm::vec3 baseGround = genExt.groundColorSet
                    ? genExt.groundColor
                    : (canyonModule ? glm::vec3(0.48f, 0.22f, 0.12f) : glm::vec3(0.20f, 0.28f, 0.18f));

                const AssetLedMaterialSettings bankMat{
                    glm::vec4(glm::max(glm::mix(baseGround, canyonModule ? glm::vec3(0.72f, 0.30f, 0.15f) : glm::vec3(0.18f, 0.22f, 0.17f), 0.42f), glm::vec3(0.025f)), 1.0f),
                    0.0f, 0.82f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f,
                    genExt.groundWetness * 0.22f, 0.56f, false,
                    Scene::RenderableComponent::AlphaMode::Opaque,
                    Scene::RenderableComponent::RenderLayer::Opaque,
                    canyonModule ? "masonry" : "naturalistic"
                };
                const AssetLedMaterialSettings darkBank{
                    glm::vec4(glm::max(baseGround * (canyonModule ? 0.46f : 0.38f), glm::vec3(0.018f)), 1.0f),
                    0.0f, 0.88f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f,
                    genExt.groundWetness * 0.16f, 0.48f, false,
                    Scene::RenderableComponent::AlphaMode::Opaque,
                    Scene::RenderableComponent::RenderLayer::Opaque,
                    "masonry"
                };
                const AssetLedMaterialSettings wetShore{
                    glm::vec4(glm::mix(baseGround * 0.45f, genExt.waterShallow, 0.22f), 1.0f),
                    0.0f, 0.46f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f,
                    std::max(0.48f, genExt.groundWetness), 0.52f, false,
                    Scene::RenderableComponent::AlphaMode::Opaque,
                    Scene::RenderableComponent::RenderLayer::Opaque,
                    "wet_masonry"
                };
                const AssetLedMaterialSettings waterAccent{
                    glm::vec4(glm::max(genExt.waterShallow, glm::vec3(0.05f)), 0.42f),
                    0.0f, 0.08f, 0.22f, 1.333f, glm::vec3(0.0f), 1.0f,
                    0.85f, 0.18f, true,
                    Scene::RenderableComponent::AlphaMode::Blend,
                    Scene::RenderableComponent::RenderLayer::Overlay,
                    "water"
                };
                const AssetLedMaterialSettings warmWood{
                    glm::vec4(0.24f, 0.13f, 0.060f, 1.0f),
                    0.0f, 0.62f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f,
                    genExt.groundWetness * 0.28f, 0.42f, false,
                    Scene::RenderableComponent::AlphaMode::Opaque,
                    Scene::RenderableComponent::RenderLayer::Opaque,
                    "wood"
                };
                const AssetLedMaterialSettings darkFabric{
                    glm::vec4(campsiteModule ? glm::vec3(0.16f, 0.070f, 0.090f) : glm::vec3(0.18f, 0.18f, 0.20f), 1.0f),
                    0.0f, 0.72f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f,
                    genExt.groundWetness * 0.18f, 0.34f, false,
                    Scene::RenderableComponent::AlphaMode::Opaque,
                    Scene::RenderableComponent::RenderLayer::Opaque,
                    "fabric"
                };
                const AssetLedMaterialSettings warmGlow{
                    glm::vec4(1.0f, 0.48f, 0.16f, 1.0f),
                    0.0f, 0.34f, 0.0f, 1.5f, glm::vec3(1.0f, 0.36f, 0.10f), 3.8f,
                    0.0f, 0.08f, true,
                    Scene::RenderableComponent::AlphaMode::Opaque,
                    Scene::RenderableComponent::RenderLayer::Opaque,
                    "emissive"
                };

                auto addModulePart = [&](const std::string& tag,
                                         const std::shared_ptr<Scene::MeshData>& mesh,
                                         const glm::vec3& position,
                                         const glm::vec3& scale,
                                         const glm::vec3& euler,
                                         const AssetLedMaterialSettings& material) {
                    AddAssetLedRenderable(*m_registry, tag.c_str(), mesh, position, scale, euler, material);
                };

                int compositionAnchors = 0;
                int terrainSetpieces = 0;
                int heroClusters = 0;
                int foregroundFrames = 0;
                int backdropGates = 0;
                int lightingZones = 0;
                int materialFamilies = 7;
                int waterSegments = 0;
                int practicalLights = 0;

                const int backdropTarget = std::max(3, genExt.authoredSceneModule.backdropGateCount);
                for (int i = 0; i < backdropTarget; ++i) {
                    const float side = (i % 2 == 0) ? -1.0f : 1.0f;
                    const float z = (canyonModule ? -19.0f : -22.0f) - static_cast<float>(i / 2) * 4.6f;
                    const float x = side * (canyonModule ? genExt.extent * 0.54f : genExt.extent * (0.36f + 0.025f * static_cast<float>(i)));
                    addModulePart("GenerativeExterior_AuthoredBackdropGate" + std::to_string(i),
                                  cliffMesh,
                                  glm::vec3(x, canyonModule ? 0.18f : -0.55f, z),
                                  canyonModule ? glm::vec3(0.78f, 0.88f, 0.82f) : glm::vec3(0.58f, 0.66f, 0.62f),
                                  glm::vec3(glm::radians(-2.0f + 1.5f * static_cast<float>(i % 3)),
                                            glm::radians(side * (canyonModule ? 8.0f : 20.0f)),
                                            glm::radians(side * 1.5f)),
                                  canyonModule ? bankMat : darkBank);
                    backdropGates++;
                    terrainSetpieces++;
                    compositionAnchors++;
                }

                const int foregroundTarget = std::max(3, genExt.authoredSceneModule.foregroundFrameCount);
                for (int i = 0; i < foregroundTarget; ++i) {
                    const float side = (i % 2 == 0) ? -1.0f : 1.0f;
                    const float x = side * (3.8f + 0.78f * static_cast<float>(i));
                    const float z = 4.85f - 0.18f * static_cast<float>(i);
                    addModulePart("GenerativeExterior_AuthoredForegroundFrame" + std::to_string(i),
                                  shardMesh,
                                  glm::vec3(x, 0.10f + 0.018f * static_cast<float>(i % 2), z),
                                  glm::vec3(0.36f + 0.04f * static_cast<float>(i % 3),
                                            0.18f,
                                            0.30f + 0.03f * static_cast<float>(i % 2)),
                                  glm::vec3(glm::radians(-2.0f + 1.0f * static_cast<float>(i % 3)),
                                            glm::radians(side * (18.0f + 9.0f * static_cast<float>(i))),
                                            glm::radians(side * 2.5f)),
                                  (i % 2 == 0) ? darkBank : bankMat);
                    foregroundFrames++;
                    compositionAnchors++;
                }

                if (genExt.waterOn) {
                    const int waterTarget = std::max(6, genExt.authoredSceneModule.waterShapeSegmentCount);
                    for (int i = 0; i < waterTarget; ++i) {
                        const float lane = static_cast<float>(i % 3);
                        const float x = (static_cast<float>(i) - static_cast<float>(waterTarget - 1) * 0.5f) *
                                        (groundW * 0.70f / static_cast<float>(std::max(1, waterTarget)));
                        const float z = shoreZ - 0.14f - lane * 0.26f;
                        addModulePart("GenerativeExterior_AuthoredWaterShape" + std::to_string(i),
                                      planeMesh,
                                      glm::vec3(x, genExt.waterLevel + 0.050f + static_cast<float>(i % 5) * 0.0009f, z),
                                      glm::vec3(1.35f + 0.18f * static_cast<float>(i % 4), 1.0f, 0.055f),
                                      glm::vec3(0.0f, glm::radians(-10.0f + 2.5f * static_cast<float>(i)), 0.0f),
                                      waterAccent);
                        waterSegments++;
                    }
                    addModulePart("GenerativeExterior_AuthoredCurvedShoreLeft",
                                  cubeMesh,
                                  glm::vec3(-groundW * 0.28f, 0.060f, shoreZ + 0.22f),
                                  glm::vec3(groundW * 0.26f, 0.055f, 0.72f),
                                  glm::vec3(0.0f, glm::radians(-7.0f), 0.0f),
                                  wetShore);
                    addModulePart("GenerativeExterior_AuthoredCurvedShoreRight",
                                  cubeMesh,
                                  glm::vec3(groundW * 0.24f, 0.062f, shoreZ + 0.34f),
                                  glm::vec3(groundW * 0.22f, 0.055f, 0.64f),
                                  glm::vec3(0.0f, glm::radians(8.0f), 0.0f),
                                  wetShore);
                    terrainSetpieces += 2;
                    compositionAnchors += 2;
                }

                if (campsiteModule) {
                    addModulePart("GenerativeExterior_AuthoredCampPad",
                                  cubeMesh,
                                  glm::vec3(1.38f, 0.036f, 1.10f),
                                  glm::vec3(4.8f, 0.060f, 2.72f),
                                  glm::vec3(0.0f, glm::radians(-10.0f), 0.0f),
                                  darkBank);
                    addModulePart("GenerativeExterior_AuthoredTentShadowBacking",
                                  cubeMesh,
                                  glm::vec3(3.34f, 0.42f, 0.30f),
                                  glm::vec3(2.65f, 0.76f, 0.18f),
                                  glm::vec3(0.0f, glm::radians(-20.0f), glm::radians(-3.0f)),
                                  darkFabric);
                    addModulePart("GenerativeExterior_AuthoredLogSeatArcA",
                                  cylinderMesh,
                                  glm::vec3(-1.34f, 0.22f, 1.58f),
                                  glm::vec3(0.12f, 1.62f, 0.12f),
                                  glm::vec3(glm::radians(88.0f), glm::radians(64.0f), glm::radians(2.0f)),
                                  warmWood);
                    addModulePart("GenerativeExterior_AuthoredLogSeatArcB",
                                  cylinderMesh,
                                  glm::vec3(1.34f, 0.22f, 2.10f),
                                  glm::vec3(0.12f, 1.42f, 0.12f),
                                  glm::vec3(glm::radians(88.0f), glm::radians(-42.0f), glm::radians(-2.0f)),
                                  warmWood);
                    addModulePart("GenerativeExterior_AuthoredFireGlowCore",
                                  cubeMesh,
                                  glm::vec3(-0.34f, 0.18f, 0.42f),
                                  glm::vec3(0.36f, 0.11f, 0.32f),
                                  glm::vec3(0.0f, glm::radians(12.0f), 0.0f),
                                  warmGlow);
                    terrainSetpieces += 1;
                    heroClusters += 3;
                    compositionAnchors += 5;
                    AddAssetLedPointLight(*m_registry, "GenerativeExterior_AuthoredCampfireKey", glm::vec3(-0.32f, 0.72f, 0.42f), glm::vec3(1.0f, 0.38f, 0.12f), 4.9f, 6.0f);
                    AddAssetLedPointLight(*m_registry, "GenerativeExterior_AuthoredLanternFill", glm::vec3(1.15f, 0.86f, 1.42f), glm::vec3(1.0f, 0.58f, 0.22f), 2.8f, 4.2f);
                    AddAssetLedSpotLight(*m_registry, "GenerativeExterior_AuthoredDawnRim", glm::vec3(-5.2f, 4.4f, -2.8f), glm::vec3(0.5f, 0.42f, 0.6f), glm::vec3(1.0f, 0.44f, 0.18f), 5.4f, 18.0f, false);
                    practicalLights += 3;
                    lightingZones += 4;
                } else if (canyonModule) {
                    for (int i = 0; i < 4; ++i) {
                        const float side = (i % 2 == 0) ? -1.0f : 1.0f;
                        const float z = -4.0f - static_cast<float>(i / 2) * 5.8f;
                        addModulePart("GenerativeExterior_AuthoredCanyonWallTerrace" + std::to_string(i),
                                      cubeMesh,
                                      glm::vec3(side * (genExt.extent * 0.44f), 0.72f + 0.25f * static_cast<float>(i / 2), z),
                                      glm::vec3(1.1f, 1.35f + 0.22f * static_cast<float>(i), 4.2f),
                                      glm::vec3(0.0f, glm::radians(side * 5.5f), glm::radians(side * 2.0f)),
                                      (i % 2 == 0) ? bankMat : darkBank);
                        terrainSetpieces++;
                        compositionAnchors++;
                    }
                    addModulePart("GenerativeExterior_AuthoredRiverCutShadow",
                                  cubeMesh,
                                  glm::vec3(0.0f, 0.048f, shoreZ + 0.44f),
                                  glm::vec3(groundW * 0.30f, 0.050f, 0.34f),
                                  glm::vec3(0.0f, glm::radians(-2.0f), 0.0f),
                                  darkBank);
                    addModulePart("GenerativeExterior_AuthoredForegroundCanyonLedge",
                                  shardMesh,
                                  glm::vec3(-4.2f, 0.16f, 3.72f),
                                  glm::vec3(0.82f, 0.34f, 0.62f),
                                  glm::vec3(glm::radians(-4.0f), glm::radians(28.0f), glm::radians(-5.0f)),
                                  bankMat);
                    terrainSetpieces += 2;
                    heroClusters += 2;
                    compositionAnchors += 2;
                    AddAssetLedSpotLight(*m_registry, "GenerativeExterior_AuthoredCanyonSideKey", glm::vec3(-6.0f, 5.2f, -4.0f), glm::vec3(0.0f, 0.65f, -5.0f), glm::vec3(1.0f, 0.58f, 0.30f), 5.6f, 20.0f, false);
                    AddAssetLedPointLight(*m_registry, "GenerativeExterior_AuthoredCanyonWarmBounce", glm::vec3(2.8f, 1.0f, -1.8f), glm::vec3(1.0f, 0.36f, 0.16f), 1.8f, 8.0f);
                    practicalLights += 2;
                    lightingZones += 3;
                } else if (alpineModule) {
                    addModulePart("GenerativeExterior_AuthoredCabinGroundingDeck",
                                  cubeMesh,
                                  glm::vec3(1.24f, 0.08f, 2.18f),
                                  glm::vec3(4.4f, 0.12f, 1.34f),
                                  glm::vec3(0.0f, glm::radians(-10.0f), 0.0f),
                                  warmWood);
                    for (int i = 0; i < 5; ++i) {
                        addModulePart("GenerativeExterior_AuthoredCabinWindowGlow" + std::to_string(i),
                                      cubeMesh,
                                      glm::vec3(0.38f + 0.38f * static_cast<float>(i % 3), 1.08f + 0.12f * static_cast<float>(i / 3), 2.72f),
                                      glm::vec3(0.18f, 0.20f, 0.030f),
                                      glm::vec3(0.0f, glm::radians(-10.0f), 0.0f),
                                      warmGlow);
                    }
                    for (int i = 0; i < 4; ++i) {
                        addModulePart("GenerativeExterior_AuthoredDockPlank" + std::to_string(i),
                                      cubeMesh,
                                      glm::vec3(-1.2f + 0.28f * static_cast<float>(i), 0.075f, shoreZ + 0.65f - 0.34f * static_cast<float>(i)),
                                      glm::vec3(0.18f, 0.055f, 1.35f),
                                      glm::vec3(0.0f, glm::radians(-13.0f), 0.0f),
                                      warmWood);
                    }
                    terrainSetpieces += 2;
                    heroClusters += 3;
                    compositionAnchors += 6;
                    AddAssetLedPointLight(*m_registry, "GenerativeExterior_AuthoredCabinInteriorGlow", glm::vec3(1.12f, 1.18f, 2.45f), glm::vec3(1.0f, 0.55f, 0.22f), 5.2f, 6.0f);
                    AddAssetLedPointLight(*m_registry, "GenerativeExterior_AuthoredPorchLantern", glm::vec3(-0.30f, 0.92f, 2.15f), glm::vec3(1.0f, 0.62f, 0.30f), 2.6f, 4.0f);
                    AddAssetLedSpotLight(*m_registry, "GenerativeExterior_AuthoredMoonRim", glm::vec3(-5.5f, 5.2f, -3.8f), glm::vec3(0.5f, 0.75f, 0.8f), glm::vec3(0.38f, 0.50f, 1.0f), 4.4f, 18.0f, false);
                    AddAssetLedPointLight(*m_registry, "GenerativeExterior_AuthoredColdWaterFill", glm::vec3(-2.4f, 0.68f, shoreZ - 1.8f), glm::vec3(0.20f, 0.34f, 0.92f), 1.4f, 7.5f);
                    practicalLights += 4;
                    lightingZones += 4;
                } else {
                    addModulePart("GenerativeExterior_AuthoredGenericHeroPad",
                                  cubeMesh,
                                  glm::vec3(0.0f, 0.050f, 1.35f),
                                  glm::vec3(4.2f, 0.070f, 2.2f),
                                  glm::vec3(0.0f, glm::radians(-7.0f), 0.0f),
                                  darkBank);
                    terrainSetpieces += 1;
                    heroClusters += 2;
                    compositionAnchors += 2;
                    AddAssetLedSpotLight(*m_registry, "GenerativeExterior_AuthoredGenericRim", glm::vec3(-4.8f, 4.4f, -3.0f), glm::vec3(0.0f, 0.45f, 0.0f), glm::vec3(0.82f, 0.88f, 1.0f), 3.4f, 16.0f, false);
                    practicalLights += 1;
                    lightingZones += 2;
                }

                lightingZones = std::max(lightingZones, genExt.authoredSceneModule.lightingZoneCount);
                materialFamilies = std::max(materialFamilies, genExt.authoredSceneModule.materialFamilyCount);
                spdlog::info("generative_exterior: authored scene module module={} anchors={} terrain_setpieces={} hero_clusters={} foreground_frames={} backdrop_gates={} lighting_zones={} material_families={} water_segments={} practical_lights={}",
                             module,
                             compositionAnchors,
                             terrainSetpieces,
                             heroClusters,
                             foregroundFrames,
                             backdropGates,
                             lightingZones,
                             materialFamilies,
                             waterSegments,
                             practicalLights);
            }
        }
    }

    if (genExt.valid && genExt.waterOn) {
        if (auto* renderer = m_renderer.get()) {
            const float farEdge = -(genExt.extent * 1.9f + 10.0f); // to the ground's far edge: no bare seabed strip at the horizon
            const float waterLen = genExt.waterFromZ - farEdge;
            const float waterMidZ = (genExt.waterFromZ + farEdge) * 0.5f;
            auto waterPlane = Utils::MeshGenerator::CreatePlane(genExt.extent * 2.0f, waterLen);
            auto up = renderer->UploadMesh(waterPlane);
            if (up.IsErr()) {
                spdlog::warn("generative_exterior: water mesh upload failed: {}", up.Error());
            } else {
                entt::entity water = m_registry->CreateEntity();
                m_registry->AddComponent<Scene::TagComponent>(water, "GenerativeExterior_Water");
                auto& t = m_registry->AddComponent<TransformComponent>(water);
                t.position = glm::vec3(0.0f, genExt.waterLevel, waterMidZ);
                t.scale = glm::vec3(1.0f);
                auto& r = m_registry->AddComponent<Scene::RenderableComponent>(water);
                r.mesh = waterPlane;
                r.albedoColor = glm::vec4(genExt.waterShallow, 0.94f);
                r.metallic = 0.0f;
                r.roughness = genExt.waterRough;
                r.ao = 1.0f;
                r.occlusionStrength = 0.84f;
                r.clearcoatFactor = 0.42f;
                r.clearcoatRoughnessFactor = std::clamp(genExt.waterRough + 0.12f, 0.08f, 0.65f);
                r.specularFactor = 1.18f;
                r.anisotropyStrength = 0.20f;
                r.presetName = "water";
                Scene::WaterSurfaceComponent sea{};
                sea.absorption = genExt.waterAbsorption;   // v3 controls explicit color readability
                sea.foamStrength = genExt.waterFoam;
                sea.viscosity = genExt.waterViscosity;
                sea.emissiveHeat = genExt.waterColorStrength; // water shader uses this as authored color strength for liquidType::Water
                sea.bodyThickness = genExt.waterBodyThickness;
                sea.meniscusStrength = 0.38f;
                sea.flowSpeed = 0.46f;
                sea.shallowTint = genExt.waterShallow;
                sea.deepTint = genExt.waterDeep;
                m_registry->AddComponent<Scene::WaterSurfaceComponent>(water, sea);
            }
        }
    }

    if (genExt.valid && !genExt.structures.empty()) {
        if (auto* renderer = m_renderer.get()) {
            auto cubeMesh = Utils::MeshGenerator::CreateCube();
            auto upCube = renderer->UploadMesh(cubeMesh);
            if (upCube.IsErr()) {
                spdlog::warn("generative_exterior: cabin cube mesh upload failed: {}", upCube.Error());
            } else {
                int cabinCount = 0;
                int cabinDetailCount = 0;
                for (const auto& structure : genExt.structures) {
                    if (structure.type != "cabin") {
                        continue;
                    }

                    const float yawRad = glm::radians(structure.yawDeg);
                    const float cs = std::cos(yawRad);
                    const float sn = std::sin(yawRad);
                    const glm::quat rotation(glm::vec3(0.0f, yawRad, 0.0f));
                    auto place = [&](float x, float y, float z) -> glm::vec3 {
                        return structure.position + glm::vec3(cs * x + sn * z,
                                                              y,
                                                              -sn * x + cs * z);
                    };
                    auto addCubePart = [&](const std::string& tag,
                                           const glm::vec3& localCenter,
                                           const glm::vec3& scale,
                                           const glm::vec4& albedo,
                                           const char* preset,
                                           float roughness,
                                           const glm::vec3& emissive = glm::vec3(0.0f),
                                           float emissiveStrength = 1.0f) {
                        entt::entity part = m_registry->CreateEntity();
                        m_registry->AddComponent<Scene::TagComponent>(part, tag);
                        auto& t = m_registry->AddComponent<TransformComponent>(part);
                        t.position = place(localCenter.x, localCenter.y, localCenter.z);
                        t.rotation = rotation;
                        t.scale = scale;
                        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(part);
                        r.mesh = cubeMesh;
                        r.albedoColor = albedo;
                        r.metallic = 0.0f;
                        r.roughness = roughness;
                        r.ao = 1.0f;
                        r.occlusionStrength = 0.78f;
                        r.normalScale = 0.26f;
                        r.proceduralMaskStrength = 0.28f;
                        r.wetnessFactor = genExt.groundWetness * 0.20f;
                        r.clearcoatFactor = std::min(genExt.groundWetness * 0.18f, 0.10f);
                        r.clearcoatRoughnessFactor = 0.70f;
                        r.anisotropyStrength = std::string(preset) == "wood" ? 0.24f : 0.06f;
                        r.sheenWeight = std::string(preset) == "naturalistic" ? 0.08f : 0.0f;
                        r.presetName = preset;
                        r.emissiveColor = emissive;
                        r.emissiveStrength = emissiveStrength;
                        r.emissiveBloomFactor = glm::length(emissive) > 0.0f ? 0.42f : 0.0f;
                        const std::string presetName = preset ? preset : "";
                        if (presetName == "wood") {
                            applyGeneratedTextureMaterial(r, "wood", true);
                        } else if (presetName == "masonry") {
                            applyGeneratedTextureMaterial(r, "rock", true);
                        }
                    };

                    const float w = structure.widthM;
                    const float d = structure.depthM;
                    const float h = structure.wallHeightM;
                    const float frontZ = d * 0.5f + 0.035f;

                    addCubePart("GenerativeExterior_Cabin_Body",
                                glm::vec3(0.0f, h * 0.5f, 0.0f),
                                glm::vec3(w, h, d),
                                glm::vec4(0.34f, 0.22f, 0.13f, 1.0f),
                                "wood",
                                0.74f);
                    addCubePart("GenerativeExterior_Cabin_Door",
                                glm::vec3(-w * 0.20f, 0.58f, frontZ),
                                glm::vec3(w * 0.22f, h * 0.56f, 0.07f),
                                glm::vec4(0.12f, 0.075f, 0.045f, 1.0f),
                                "wood",
                                0.82f);

                    const glm::vec3 windowGlow = structure.litWindows
                        ? glm::vec3(1.0f, 0.53f, 0.19f)
                        : glm::vec3(0.0f);
                    const float windowEmit = structure.litWindows ? 4.8f : 1.0f;
                    addCubePart("GenerativeExterior_Cabin_Window_L",
                                glm::vec3(w * 0.22f, h * 0.64f, frontZ + 0.01f),
                                glm::vec3(w * 0.18f, h * 0.24f, 0.055f),
                                glm::vec4(1.0f, 0.58f, 0.24f, 1.0f),
                                "naturalistic",
                                0.18f,
                                windowGlow,
                                windowEmit);
                    addCubePart("GenerativeExterior_Cabin_Window_R",
                                glm::vec3(w * 0.41f, h * 0.64f, frontZ + 0.01f),
                                glm::vec3(w * 0.16f, h * 0.22f, 0.055f),
                                glm::vec4(1.0f, 0.58f, 0.24f, 1.0f),
                                "naturalistic",
                                0.18f,
                                windowGlow,
                                windowEmit);

                    auto roofMesh = CreateGenerativeGableRoofMesh(w + 0.52f,
                                                                   d + 0.44f,
                                                                   structure.roofHeightM);
                    auto upRoof = renderer->UploadMesh(roofMesh);
                    if (upRoof.IsErr()) {
                        spdlog::warn("generative_exterior: cabin roof mesh upload failed: {}", upRoof.Error());
                    } else {
                        entt::entity roof = m_registry->CreateEntity();
                        m_registry->AddComponent<Scene::TagComponent>(roof, "GenerativeExterior_Cabin_Roof");
                        auto& t = m_registry->AddComponent<TransformComponent>(roof);
                        t.position = place(0.0f, h, 0.0f);
                        t.rotation = rotation;
                        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(roof);
                        r.mesh = roofMesh;
                        r.albedoColor = glm::vec4(0.105f, 0.075f, 0.065f, 1.0f);
                        r.metallic = 0.0f;
                        r.roughness = 0.86f;
                        r.ao = 1.0f;
                        r.doubleSided = true;
                        r.presetName = "wood";
                        applyGeneratedTextureMaterial(r, "wood", true);
                    }

                    if (genExt.assetFidelity.enabled && genExt.assetFidelity.cabinFacadeDetailCount > 0) {
                        const glm::vec4 trimWood(0.18f, 0.11f, 0.060f, 1.0f);
                        const glm::vec4 sidingA(0.39f, 0.255f, 0.145f, 1.0f);
                        const glm::vec4 sidingB(0.27f, 0.165f, 0.090f, 1.0f);
                        for (int i = 0; i < 8; ++i) {
                            const float y = 0.32f + static_cast<float>(i) * (h * 0.092f);
                            addCubePart("GenerativeExterior_Cabin_SidingBand" + std::to_string(i),
                                        glm::vec3(0.0f, y, frontZ + 0.045f),
                                        glm::vec3(w + 0.10f, 0.032f, 0.045f),
                                        (i % 2 == 0) ? sidingA : sidingB,
                                        "wood",
                                        0.78f);
                            cabinDetailCount++;
                        }

                        const std::array<float, 2> cornerXs{-w * 0.515f, w * 0.515f};
                        for (float x : cornerXs) {
                            addCubePart("GenerativeExterior_Cabin_CornerTrim" + std::to_string(cabinDetailCount),
                                        glm::vec3(x, h * 0.50f, frontZ + 0.075f),
                                        glm::vec3(0.085f, h * 0.96f, 0.08f),
                                        trimWood,
                                        "wood",
                                        0.70f);
                            cabinDetailCount++;
                        }

                        const float doorX = -w * 0.20f;
                        const float doorH = h * 0.64f;
                        const float doorW = w * 0.28f;
                        for (int i = 0; i < 4; ++i) {
                            const bool vertical = i < 2;
                            const float sx = vertical ? 0.055f : doorW;
                            const float sy = vertical ? doorH : 0.055f;
                            const float x = doorX + (vertical ? (i == 0 ? -doorW * 0.50f : doorW * 0.50f) : 0.0f);
                            const float y = 0.58f + (vertical ? 0.0f : (i == 2 ? -doorH * 0.50f : doorH * 0.50f));
                            addCubePart("GenerativeExterior_Cabin_DoorTrim" + std::to_string(i),
                                        glm::vec3(x, y, frontZ + 0.105f),
                                        glm::vec3(sx, sy, 0.060f),
                                        trimWood,
                                        "wood",
                                        0.68f);
                            cabinDetailCount++;
                        }

                        auto addWindowTrim = [&](const char* baseTag, float cx, float cy, float ww, float wh) {
                            for (int i = 0; i < 6; ++i) {
                                const bool vertical = i < 2 || i == 4;
                                const bool mullion = i >= 4;
                                const float sx = vertical ? 0.040f : ww;
                                const float sy = vertical ? wh : 0.040f;
                                const float x = cx + (mullion ? 0.0f : (vertical ? (i == 0 ? -ww * 0.54f : ww * 0.54f) : 0.0f));
                                const float y = cy + (mullion ? 0.0f : (vertical ? 0.0f : (i == 2 ? -wh * 0.54f : wh * 0.54f)));
                                addCubePart(std::string(baseTag) + std::to_string(i),
                                            glm::vec3(x, y, frontZ + 0.120f),
                                            glm::vec3(sx, sy, 0.055f),
                                            trimWood,
                                            "wood",
                                            0.66f);
                                cabinDetailCount++;
                            }
                        };
                        addWindowTrim("GenerativeExterior_Cabin_WindowTrimL_", w * 0.22f, h * 0.64f, w * 0.22f, h * 0.29f);
                        addWindowTrim("GenerativeExterior_Cabin_WindowTrimR_", w * 0.41f, h * 0.64f, w * 0.20f, h * 0.27f);

                        addCubePart("GenerativeExterior_Cabin_PorchDeck",
                                    glm::vec3(0.02f, 0.085f, d * 0.5f + 0.62f),
                                    glm::vec3(w * 0.70f, 0.16f, 0.82f),
                                    glm::vec4(0.22f, 0.13f, 0.075f, 1.0f),
                                    "wood",
                                    0.82f);
                        cabinDetailCount++;
                        for (int i = 0; i < 2; ++i) {
                            addCubePart("GenerativeExterior_Cabin_PorchStep" + std::to_string(i),
                                        glm::vec3(0.02f, 0.035f + i * 0.045f, d * 0.5f + 1.12f + i * 0.18f),
                                        glm::vec3(w * (0.56f - i * 0.10f), 0.070f, 0.23f),
                                        glm::vec4(0.19f, 0.115f, 0.065f, 1.0f),
                                        "wood",
                                        0.84f);
                            cabinDetailCount++;
                        }
                        addCubePart("GenerativeExterior_Cabin_Chimney",
                                    glm::vec3(-w * 0.34f, h + structure.roofHeightM * 0.55f, -d * 0.17f),
                                    glm::vec3(0.34f, structure.roofHeightM * 0.92f, 0.34f),
                                    glm::vec4(0.20f, 0.11f, 0.080f, 1.0f),
                                    "masonry",
                                    0.90f);
                        cabinDetailCount++;
                        addCubePart("GenerativeExterior_Cabin_RoofRidgeCap",
                                    glm::vec3(0.0f, h + structure.roofHeightM + 0.035f, 0.0f),
                                    glm::vec3(0.12f, 0.07f, d + 0.62f),
                                    glm::vec4(0.055f, 0.040f, 0.036f, 1.0f),
                                    "wood",
                                    0.88f);
                        cabinDetailCount++;
                        addCubePart("GenerativeExterior_Cabin_WarmLightSpill",
                                    glm::vec3(w * 0.18f, 0.030f, d * 0.5f + 1.12f),
                                    glm::vec3(w * 0.52f, 0.030f, 0.52f),
                                    glm::vec4(1.0f, 0.48f, 0.16f, 0.54f),
                                    "naturalistic",
                                    0.56f,
                                    structure.litWindows ? glm::vec3(1.0f, 0.32f, 0.10f) : glm::vec3(0.0f),
                                    structure.litWindows ? 1.8f : 1.0f);
                        cabinDetailCount++;
                    }

                    if (structure.litWindows) {
                        entt::entity lamp = m_registry->CreateEntity();
                        m_registry->AddComponent<Scene::TagComponent>(lamp, "GenerativeExterior_Cabin_WindowGlow");
                        auto& t = m_registry->AddComponent<TransformComponent>(lamp);
                        t.position = place(w * 0.25f, h * 0.63f, d * 0.5f + 0.35f);
                        auto& l = m_registry->AddComponent<Scene::LightComponent>(lamp);
                        l.type = Scene::LightType::Point;
                        l.color = glm::vec3(1.0f, 0.48f, 0.18f);
                        l.intensity = 4.2f;
                        l.range = 5.8f;
                        l.castsShadows = false;
                    }
                    cabinCount++;
                }
                if (cabinCount > 0) {
                    spdlog::info("generative_exterior: created {} procedural cabin structure(s)", cabinCount);
                    if (cabinDetailCount > 0) {
                        spdlog::info("generative_exterior: created hero asset detail cabin_facade={} camp=0 foreground=0",
                                     cabinDetailCount);
                    }
                }
            }
        }
    }

    if (genExt.valid && genExt.assetFidelity.enabled && genExt.assetFidelity.campDetailCount > 0) {
        if (auto* renderer = m_renderer.get()) {
            auto cubeMesh = Utils::MeshGenerator::CreateCube();
            auto cylinderMesh = Utils::MeshGenerator::CreateCylinder(0.5f, 1.0f, 16);
            const auto upCube = renderer->UploadMesh(cubeMesh);
            const auto upCylinder = renderer->UploadMesh(cylinderMesh);
            if (upCube.IsErr() || upCylinder.IsErr()) {
                spdlog::warn("generative_exterior: camp detail mesh upload failed cube='{}' cylinder='{}'",
                             upCube.IsErr() ? upCube.Error() : "ok",
                             upCylinder.IsErr() ? upCylinder.Error() : "ok");
            } else {
                auto dressDetail = [&](Scene::RenderableComponent& r,
                                       const glm::vec4& color,
                                       const char* preset,
                                       float roughness,
                                       const glm::vec3& emissive = glm::vec3(0.0f),
                                       float emissiveStrength = 1.0f) {
                    r.albedoColor = color;
                    r.metallic = 0.0f;
                    r.roughness = roughness;
                    r.ao = 1.0f;
                    r.occlusionStrength = 0.70f;
                    r.normalScale = 0.22f;
                    r.wetnessFactor = genExt.groundWetness * 0.18f;
                    r.proceduralMaskStrength = 0.30f;
                    r.specularFactor = 0.22f;
                    r.clearcoatFactor = std::min(genExt.groundWetness * 0.12f, 0.08f);
                    r.clearcoatRoughnessFactor = 0.74f;
                    r.anisotropyStrength = std::string(preset) == "wood" ? 0.30f : 0.08f;
                    r.sheenWeight = std::string(preset) == "fabric" ? 0.26f : 0.0f;
                    r.presetName = preset;
                    r.emissiveColor = emissive;
                    r.emissiveStrength = emissiveStrength;
                    r.emissiveBloomFactor = glm::length(emissive) > 0.0f ? 0.55f : 0.0f;
                };
                auto addPart = [&](const std::string& tag,
                                   const std::shared_ptr<Scene::MeshData>& mesh,
                                   const glm::vec3& position,
                                   const glm::vec3& scale,
                                   const glm::vec3& euler,
                                   const glm::vec4& color,
                                   const char* preset,
                                   float roughness,
                                   const glm::vec3& emissive = glm::vec3(0.0f),
                                   float emissiveStrength = 1.0f) {
                    entt::entity part = m_registry->CreateEntity();
                    m_registry->AddComponent<Scene::TagComponent>(part, tag);
                    auto& t = m_registry->AddComponent<TransformComponent>(part);
                    t.position = position;
                    t.scale = scale;
                    t.rotation = glm::quat(euler);
                    auto& r = m_registry->AddComponent<Scene::RenderableComponent>(part);
                    r.mesh = mesh;
                    dressDetail(r, color, preset, roughness, emissive, emissiveStrength);
                };

                int campDetailCount = 0;
                int foregroundCount = 0;
                const glm::vec3 tentCenter(2.9f, 0.0f, 0.9f);
                const float tentYaw = glm::radians(-18.0f);
                const float cs = std::cos(tentYaw);
                const float sn = std::sin(tentYaw);
                auto tentPlace = [&](float x, float y, float z) -> glm::vec3 {
                    return tentCenter + glm::vec3(cs * x + sn * z, y, -sn * x + cs * z);
                };
                const glm::vec4 ropeColor(0.78f, 0.66f, 0.46f, 1.0f);
                const glm::vec4 seamColor(0.035f, 0.040f, 0.048f, 1.0f);
                addPart("GenerativeExterior_Tent_RidgeSeam", cubeMesh, tentPlace(0.0f, 1.18f, 0.0f),
                        glm::vec3(0.055f, 0.040f, 2.35f), glm::vec3(0.0f, tentYaw, 0.0f),
                        seamColor, "fabric", 0.78f);
                campDetailCount++;
                for (int i = 0; i < 4; ++i) {
                    const float sx = (i < 2) ? -1.18f : 1.18f;
                    const float sz = (i % 2 == 0) ? -0.92f : 0.92f;
                    addPart("GenerativeExterior_Tent_GuyLine" + std::to_string(i),
                            cubeMesh,
                            tentPlace(sx * 1.12f, 0.12f, sz * 1.18f),
                            glm::vec3(0.035f, 0.028f, 1.22f),
                            glm::vec3(0.0f, tentYaw + glm::radians(24.0f * (i - 1.5f)), 0.0f),
                            ropeColor,
                            "fabric",
                            0.70f);
                    campDetailCount++;
                    addPart("GenerativeExterior_Tent_Stake" + std::to_string(i),
                            cylinderMesh,
                            tentPlace(sx * 1.72f, 0.18f, sz * 1.58f),
                            glm::vec3(0.065f, 0.36f, 0.065f),
                            glm::vec3(glm::radians(7.0f), tentYaw, glm::radians((i % 2 == 0) ? -5.0f : 5.0f)),
                            glm::vec4(0.24f, 0.14f, 0.075f, 1.0f),
                            "wood",
                            0.82f);
                    campDetailCount++;
                }
                for (int i = 0; i < 5; ++i) {
                    addPart("GenerativeExterior_Tent_SeamPatch" + std::to_string(i),
                            cubeMesh,
                            tentPlace(-0.74f + i * 0.37f, 0.70f + (i % 2) * 0.08f, 1.04f),
                            glm::vec3(0.22f, 0.035f, 0.035f),
                            glm::vec3(0.0f, tentYaw, 0.0f),
                            glm::vec4(0.055f, 0.065f, 0.070f, 1.0f),
                            "fabric",
                            0.76f);
                    campDetailCount++;
                }

                const glm::vec3 fireCenter(-0.35f, 0.0f, 0.35f);
                for (int i = 0; i < 9; ++i) {
                    const float a = static_cast<float>(i) * 0.698f;
                    const float r = 0.16f + 0.035f * static_cast<float>(i % 3);
                    addPart("GenerativeExterior_Fire_Ember" + std::to_string(i),
                            cubeMesh,
                            fireCenter + glm::vec3(std::cos(a) * r, 0.075f + (i % 2) * 0.025f, std::sin(a) * r),
                            glm::vec3(0.075f, 0.045f, 0.075f),
                            glm::vec3(0.0f, a, 0.0f),
                            glm::vec4(1.0f, 0.32f, 0.08f, 1.0f),
                            "naturalistic",
                            0.48f,
                            glm::vec3(1.0f, 0.24f, 0.06f),
                            3.0f);
                    campDetailCount++;
                }
                for (int i = 0; i < 3; ++i) {
                    const float yaw = glm::radians(120.0f * i + 18.0f);
                    addPart("GenerativeExterior_Fire_TripodLeg" + std::to_string(i),
                            cylinderMesh,
                            fireCenter + glm::vec3(std::cos(yaw) * 0.22f, 0.62f, std::sin(yaw) * 0.22f),
                            glm::vec3(0.045f, 1.18f, 0.045f),
                            glm::vec3(glm::radians(18.0f), yaw, glm::radians((i - 1) * 8.0f)),
                            glm::vec4(0.16f, 0.095f, 0.055f, 1.0f),
                            "wood",
                            0.82f);
                    campDetailCount++;
                }
                addPart("GenerativeExterior_Fire_HangingKettle", cubeMesh,
                        fireCenter + glm::vec3(0.0f, 0.64f, 0.02f),
                        glm::vec3(0.24f, 0.18f, 0.20f),
                        glm::vec3(0.0f, glm::radians(18.0f), 0.0f),
                        glm::vec4(0.055f, 0.050f, 0.048f, 1.0f),
                        "masonry",
                        0.64f);
                campDetailCount++;

                entt::entity lanternLight = m_registry->CreateEntity();
                m_registry->AddComponent<Scene::TagComponent>(lanternLight, "GenerativeExterior_Lantern_Practical");
                auto& lanternT = m_registry->AddComponent<TransformComponent>(lanternLight);
                lanternT.position = glm::vec3(1.15f, 0.62f, 1.42f);
                auto& lanternL = m_registry->AddComponent<Scene::LightComponent>(lanternLight);
                lanternL.type = Scene::LightType::Point;
                lanternL.color = glm::vec3(1.0f, 0.49f, 0.16f);
                lanternL.intensity = 2.8f;
                lanternL.range = 4.4f;
                lanternL.castsShadows = false;

                for (int i = 0; i < genExt.assetFidelity.foregroundDressingClusters; ++i) {
                    const float side = (i % 2 == 0) ? -1.0f : 1.0f;
                    const float yaw = glm::radians(31.0f * static_cast<float>(i));
                    addPart("GenerativeExterior_Foreground_Twig" + std::to_string(i),
                            cubeMesh,
                            glm::vec3(side * (2.2f + 0.35f * i), 0.075f, 4.25f - 0.28f * i),
                            glm::vec3(0.72f, 0.050f, 0.065f),
                            glm::vec3(0.0f, yaw, glm::radians(2.0f * (i % 3 - 1))),
                            glm::vec4(0.17f, 0.095f, 0.045f, 1.0f),
                            "wood",
                            0.86f);
                    foregroundCount++;
                }

                spdlog::info("generative_exterior: created hero asset detail cabin_facade=0 camp={} foreground={}",
                             campDetailCount,
                             foregroundCount);
            }
        }
    }

    if (genExt.valid && genExt.heroEnvironmentGeometry.enabled) {
        if (auto* renderer = m_renderer.get()) {
            auto cubeMesh = Utils::MeshGenerator::CreateCube();
            auto cylinderMesh = Utils::MeshGenerator::CreateCylinder(0.5f, 1.0f, 24);
            auto coneMesh = Utils::MeshGenerator::CreateCone(0.5f, 1.0f, 28);
            auto sphereMesh = Utils::MeshGenerator::CreateSphere(0.5f, 24);
            auto shardMesh = CreateGenerativeRockShardMesh(12.47f);
            auto cliffMassMesh = CreateGenerativeCliffWallMesh(genExt.extent * 0.32f,
                                                               std::max(4.6f, genExt.terrainRelief * 8.5f + 3.0f),
                                                               1.38f,
                                                               11.29f,
                                                               5u);
            const auto upCube = renderer->UploadMesh(cubeMesh);
            const auto upCylinder = renderer->UploadMesh(cylinderMesh);
            const auto upCone = renderer->UploadMesh(coneMesh);
            const auto upSphere = renderer->UploadMesh(sphereMesh);
            const auto upShard = renderer->UploadMesh(shardMesh);
            const auto upCliffMass = renderer->UploadMesh(cliffMassMesh);
            if (upCube.IsErr() || upCylinder.IsErr() || upCone.IsErr() ||
                upSphere.IsErr() || upShard.IsErr() || upCliffMass.IsErr()) {
                spdlog::warn("generative_exterior: hero environment geometry mesh upload failed cube='{}' cylinder='{}' cone='{}' sphere='{}' shard='{}' cliff='{}'",
                             upCube.IsErr() ? upCube.Error() : "ok",
                             upCylinder.IsErr() ? upCylinder.Error() : "ok",
                             upCone.IsErr() ? upCone.Error() : "ok",
                             upSphere.IsErr() ? upSphere.Error() : "ok",
                             upShard.IsErr() ? upShard.Error() : "ok",
                             upCliffMass.IsErr() ? upCliffMass.Error() : "ok");
            } else {
                const float shoreZ = genExt.waterOn ? (genExt.waterFromZ + 0.5f) : -genExt.extent * 0.36f;
                const float groundW = genExt.extent * 1.86f;
                const bool canyonLike = genExt.worldGeometry.canyonWallLayers > 0;
                const bool desertSurface = genExt.groundKind.find("dirt") != std::string::npos || canyonLike;
                const glm::vec3 baseGround = genExt.groundColorSet
                    ? genExt.groundColor
                    : (desertSurface ? glm::vec3(0.44f, 0.22f, 0.13f) : glm::vec3(0.24f, 0.34f, 0.18f));
                auto pseudo = [](int i, float salt) {
                    const float n = std::sin(static_cast<float>(i) * 17.371f + salt * 43.117f) * 21942.123f;
                    return n - std::floor(n);
                };
                auto dressSolid = [&](Scene::RenderableComponent& r,
                                      const glm::vec4& color,
                                      const char* preset,
                                      float roughness,
                                      float normalScale,
                                      float proceduralMask,
                                      float wetness = 0.0f,
                                      bool heroSurface = true,
                                      bool shoreSurface = false) {
                    r.albedoColor = color;
                    r.metallic = 0.0f;
                    r.roughness = roughness;
                    r.ao = 0.92f;
                    r.occlusionStrength = 0.80f;
                    r.normalScale = normalScale;
                    r.proceduralMaskStrength = proceduralMask;
                    r.wetnessFactor = wetness;
                    r.specularFactor = 0.18f + wetness * 0.18f;
                    r.clearcoatFactor = std::min(wetness * 0.24f, 0.14f);
                    r.clearcoatRoughnessFactor = 0.72f;
                    r.anisotropyStrength = std::string(preset) == "wood" ? 0.34f : 0.10f;
                    r.sheenWeight = std::string(preset) == "fabric" ? 0.24f : 0.0f;
                    r.doubleSided = true;
                    r.presetName = preset;
                    const std::string presetName = preset ? preset : "";
                    if (presetName == "wood") {
                        applyGeneratedTextureMaterial(r, shoreSurface ? "driftwood" : "wood", heroSurface, shoreSurface);
                    } else if (presetName == "fabric") {
                        applyGeneratedTextureMaterial(r, "fabric", heroSurface, shoreSurface);
                    } else if (presetName == "masonry") {
                        applyGeneratedTextureMaterial(r, shoreSurface ? "rock" : "rock_cliff", heroSurface, shoreSurface);
                    }
                };
                auto addPart = [&](const std::string& tag,
                                   const std::shared_ptr<Scene::MeshData>& mesh,
                                   const glm::vec3& position,
                                   const glm::vec3& scale,
                                   const glm::vec3& euler,
                                   const glm::vec4& color,
                                   const char* preset,
                                   float roughness,
                                   float normalScale,
                                   float proceduralMask,
                                   float wetness = 0.0f,
                                   bool shoreSurface = false) {
                    entt::entity part = m_registry->CreateEntity();
                    m_registry->AddComponent<Scene::TagComponent>(part, tag);
                    auto& t = m_registry->AddComponent<TransformComponent>(part);
                    t.position = position;
                    t.scale = scale;
                    t.rotation = glm::quat(euler);
                    auto& r = m_registry->AddComponent<Scene::RenderableComponent>(part);
                    r.mesh = mesh;
                    dressSolid(r, color, preset, roughness, normalScale, proceduralMask, wetness, true, shoreSurface);
                };

                int mountainLayers = 0;
                int cliffMassPieces = 0;
                const int mountainTarget = std::max(0, genExt.heroEnvironmentGeometry.mountainMassLayerCount);
                for (int i = 0; i < mountainTarget; ++i) {
                    const float width = canyonLike
                        ? genExt.extent * (0.34f + 0.05f * static_cast<float>(i % 4))
                        : genExt.extent * (0.26f + 0.04f * static_cast<float>(i % 4));
                    const float height = canyonLike
                        ? std::max(3.0f, 3.4f + genExt.terrainRelief * 2.2f + 0.45f * static_cast<float>(i % 3))
                        : std::max(2.2f, 2.8f + genExt.terrainRelief * 1.7f + 0.34f * static_cast<float>(i % 3));
                    auto massMesh = CreateGenerativeCliffWallMesh(width,
                                                                  height,
                                                                  1.52f,
                                                                  14.21f + static_cast<float>(i) * 2.83f,
                                                                  5u);
                    const auto upMass = renderer->UploadMesh(massMesh);
                    if (upMass.IsErr()) {
                        spdlog::warn("generative_exterior: mountain massing mesh upload failed: {}", upMass.Error());
                        continue;
                    }
                    const float side = (i % 2 == 0) ? -1.0f : 1.0f;
                    const float z = canyonLike
                        ? -22.0f - static_cast<float>(i) * 4.2f
                        : -24.0f - static_cast<float>(i) * 3.8f;
                    const float xOffset = canyonLike
                        ? side * (genExt.extent * (0.56f + 0.025f * static_cast<float>(i % 3)))
                        : side * (genExt.extent * (0.42f + 0.035f * static_cast<float>(i % 3)));
                    addPart("GenerativeExterior_MountainMassLayer" + std::to_string(i),
                            massMesh,
                            glm::vec3(xOffset,
                                      canyonLike ? (-0.20f + 0.10f * static_cast<float>(i % 2))
                                                 : (-0.85f + 0.05f * static_cast<float>(i % 2)),
                                      z),
                            canyonLike
                                ? glm::vec3(0.58f + 0.04f * static_cast<float>(i % 2), 0.64f, 0.55f)
                                : glm::vec3(0.50f + 0.05f * static_cast<float>(i % 2), 0.62f, 0.54f),
                            glm::vec3(glm::radians(-1.2f + 0.8f * static_cast<float>(i % 3)),
                                      glm::radians(side * (canyonLike ? (7.0f + 2.0f * static_cast<float>(i % 2))
                                                                      : (18.0f + 5.0f * static_cast<float>(i % 2)))),
                                      0.0f),
                            glm::vec4(glm::max(glm::mix(baseGround,
                                                        desertSurface ? glm::vec3(0.62f, 0.28f, 0.15f)
                                                                      : glm::vec3(0.20f, 0.22f, 0.21f),
                                                        0.60f + 0.05f * static_cast<float>(i % 3)),
                                               glm::vec3(0.025f)),
                                      1.0f),
                            "masonry",
                            0.91f,
                            0.86f,
                            0.72f,
                            genExt.groundWetness * 0.22f);
                    mountainLayers++;
                }

                const int cliffTarget = std::max(0, genExt.heroEnvironmentGeometry.cliffMassPieceCount);
                for (int i = 0; i < cliffTarget; ++i) {
                    const float side = (i % 2 == 0) ? -1.0f : 1.0f;
                    const float flank = genExt.worldGeometry.canyonWidthM > 1.0f
                        ? genExt.worldGeometry.canyonWidthM * 0.62f
                        : genExt.extent * 0.36f;
                    const float z = -10.5f - pseudo(i + 1301, 1.83f) * genExt.extent * 0.52f;
                    const float y = 0.22f + pseudo(i + 1311, 2.09f) * 0.92f;
                    addPart("GenerativeExterior_HeroEnvCliffMass" + std::to_string(i),
                            (i % 3 == 0) ? cliffMassMesh : shardMesh,
                            glm::vec3(side * (flank + 0.80f + pseudo(i, 2.37f) * 1.85f), y, z),
                            glm::vec3(0.44f + pseudo(i, 2.73f) * 0.36f,
                                      0.42f + pseudo(i, 3.01f) * 0.58f,
                                      0.42f + pseudo(i, 3.37f) * 0.70f),
                            glm::vec3(glm::radians(-6.0f + pseudo(i, 3.71f) * 12.0f),
                                      glm::radians(side > 0.0f ? 180.0f : 0.0f) + glm::radians(-8.0f + pseudo(i, 4.03f) * 16.0f),
                                      glm::radians(-5.0f + pseudo(i, 4.31f) * 10.0f)),
                            glm::vec4(glm::max(glm::mix(baseGround,
                                                        desertSurface ? glm::vec3(0.70f, 0.34f, 0.18f)
                                                                      : glm::vec3(0.16f, 0.15f, 0.13f),
                                                        0.52f + 0.08f * pseudo(i, 4.77f)),
                                               glm::vec3(0.022f)),
                                      1.0f),
                            "masonry",
                            0.90f,
                            0.82f,
                            0.76f,
                            genExt.groundWetness * 0.24f);
                    cliffMassPieces++;
                }

                int shorelineProps = 0;
                if (genExt.waterOn && genExt.heroEnvironmentGeometry.shorelinePropCount > 0) {
                    for (int i = 0; i < genExt.heroEnvironmentGeometry.shorelinePropCount; ++i) {
                        const float x = (pseudo(i + 1401, 1.41f) - 0.5f) * groundW * 0.76f;
                        const float z = shoreZ + 0.18f + pseudo(i + 1411, 1.97f) * 1.55f;
                        if (i % 3 == 0) {
                            addPart("GenerativeExterior_Shoreline_Driftwood" + std::to_string(i),
                                    cylinderMesh,
                                    glm::vec3(x, 0.135f, z),
                                    glm::vec3(0.085f, 1.10f + pseudo(i, 2.61f) * 0.72f, 0.085f),
                                    glm::vec3(glm::radians(82.0f + pseudo(i, 3.17f) * 11.0f),
                                              glm::radians(pseudo(i, 3.71f) * 180.0f),
                                              glm::radians(-8.0f + pseudo(i, 4.13f) * 16.0f)),
                                    glm::vec4(0.24f, 0.15f, 0.075f, 1.0f),
                                    "wood",
                                    0.82f,
                                    0.46f,
                                    0.48f,
                                    std::min(0.42f, genExt.groundWetness + 0.12f),
                                    true);
                        } else {
                            const float s = 0.22f + pseudo(i, 4.57f) * 0.26f;
                            addPart("GenerativeExterior_Shoreline_WetStone" + std::to_string(i),
                                    shardMesh,
                                    glm::vec3(x, 0.080f + static_cast<float>(i % 3) * 0.004f, z),
                                    glm::vec3(s * 1.55f, s * 0.56f, s),
                                    glm::vec3(glm::radians(-4.0f + pseudo(i, 5.01f) * 8.0f),
                                              glm::radians(pseudo(i, 5.37f) * 360.0f),
                                              glm::radians(-6.0f + pseudo(i, 5.81f) * 12.0f)),
                                    glm::vec4(glm::max(glm::mix(baseGround, glm::vec3(0.09f, 0.085f, 0.078f), 0.58f),
                                                       glm::vec3(0.018f)),
                                              1.0f),
                                    "masonry",
                                    0.76f,
                                    0.74f,
                                    0.62f,
                                    std::min(0.55f, genExt.groundWetness + 0.20f),
                                    true);
                        }
                        shorelineProps++;
                    }
                }

                int treeSilhouettes = 0;
                if (!desertSurface && genExt.heroEnvironmentGeometry.irregularTreeSilhouetteCount > 0) {
                    for (int i = 0; i < genExt.heroEnvironmentGeometry.irregularTreeSilhouetteCount; ++i) {
                        const float side = (i % 2 == 0) ? -1.0f : 1.0f;
                        const float lane = static_cast<float>((i / 2) % 4);
                        const float x = side * (7.4f + lane * 2.15f + pseudo(i, 6.11f) * 0.85f);
                        const float z = 1.5f + pseudo(i + 1501, 6.57f) * 5.6f;
                        const float trunkH = 1.35f + pseudo(i, 6.91f) * 0.95f;
                        addPart("GenerativeExterior_IrregularTree_Trunk" + std::to_string(i),
                                cylinderMesh,
                                glm::vec3(x, trunkH * 0.50f, z),
                                glm::vec3(0.11f + pseudo(i, 7.23f) * 0.040f, trunkH, 0.11f),
                                glm::vec3(glm::radians(-4.0f + pseudo(i, 7.61f) * 8.0f),
                                          glm::radians(19.0f * static_cast<float>(i)),
                                          glm::radians(side * (3.0f + pseudo(i, 8.03f) * 5.0f))),
                                glm::vec4(0.12f, 0.075f, 0.040f, 1.0f),
                                "wood",
                                0.82f,
                                0.52f,
                                0.44f,
                                genExt.groundWetness * 0.18f);
                        const int crowns = 2 + (i % 3);
                        for (int c = 0; c < crowns; ++c) {
                            const float crownY = trunkH + 0.35f + 0.32f * static_cast<float>(c);
                            const float offset = (static_cast<float>(c) - 1.0f) * 0.18f;
                            addPart("GenerativeExterior_IrregularTree_Crown" + std::to_string(i) + "_" + std::to_string(c),
                                    coneMesh,
                                    glm::vec3(x + side * offset, crownY, z + 0.10f * static_cast<float>(c % 2)),
                                    glm::vec3(0.72f + 0.13f * static_cast<float>((i + c) % 3),
                                              1.05f + 0.18f * static_cast<float>(c),
                                              0.76f + 0.11f * static_cast<float>((i + 2 * c) % 4)),
                                    glm::vec3(glm::radians(-5.0f + pseudo(i + c, 8.37f) * 10.0f),
                                              glm::radians(37.0f * static_cast<float>(i + c)),
                                              glm::radians(side * (2.0f + 2.0f * static_cast<float>(c)))),
                                    glm::vec4(0.055f + 0.020f * static_cast<float>(c % 2),
                                              0.17f + 0.045f * pseudo(i + c, 8.73f),
                                              0.070f,
                                              1.0f),
                                    "foliage",
                                    0.72f,
                                    0.44f,
                                    0.48f,
                                    genExt.groundWetness * 0.14f);
                        }
                        treeSilhouettes++;
                    }
                }

                int campPieces = 0;
                if (genExt.heroEnvironmentGeometry.highDetailCampPieceCount > 0) {
                    const glm::vec3 tentCenter(2.9f, 0.0f, 0.9f);
                    const float tentYaw = glm::radians(-18.0f);
                    const float cs = std::cos(tentYaw);
                    const float sn = std::sin(tentYaw);
                    auto tentPlace = [&](float x, float y, float z) -> glm::vec3 {
                        return tentCenter + glm::vec3(cs * x + sn * z, y, -sn * x + cs * z);
                    };
                    const int target = genExt.heroEnvironmentGeometry.highDetailCampPieceCount;
                    for (int i = 0; i < target; ++i) {
                        const int mode = i % 6;
                        if (mode == 0) {
                            const float side = (i % 2 == 0) ? -1.0f : 1.0f;
                            addPart("GenerativeExterior_HighDetailCamp_TentPole" + std::to_string(i),
                                    cylinderMesh,
                                    tentPlace(side * 1.06f, 0.64f, -0.78f + 0.34f * static_cast<float>((i / 6) % 4)),
                                    glm::vec3(0.040f, 1.18f, 0.040f),
                                    glm::vec3(glm::radians(10.0f * side),
                                              tentYaw,
                                              glm::radians(-8.0f * side)),
                                    glm::vec4(0.18f, 0.11f, 0.060f, 1.0f),
                                    "wood",
                                    0.78f,
                                    0.52f,
                                    0.38f,
                                    genExt.groundWetness * 0.12f);
                        } else if (mode == 1) {
                            addPart("GenerativeExterior_HighDetailCamp_FlyPanel" + std::to_string(i),
                                    cubeMesh,
                                    tentPlace(-0.80f + 0.34f * static_cast<float>((i / 6) % 6),
                                              0.78f,
                                              1.22f),
                                    glm::vec3(0.26f, 0.022f, 0.20f),
                                    glm::vec3(glm::radians(9.0f),
                                              tentYaw,
                                              glm::radians(-5.0f + pseudo(i, 9.17f) * 10.0f)),
                                    glm::vec4(0.12f, 0.055f, 0.070f, 1.0f),
                                    "fabric",
                                    0.76f,
                                    0.40f,
                                    0.42f,
                                    genExt.groundWetness * 0.10f);
                        } else if (mode == 2) {
                            addPart("GenerativeExterior_HighDetailCamp_Bedroll" + std::to_string(i),
                                    cylinderMesh,
                                    glm::vec3(-1.40f + 0.32f * static_cast<float>((i / 6) % 5),
                                              0.19f,
                                              1.76f + 0.13f * static_cast<float>((i / 6) % 3)),
                                    glm::vec3(0.18f, 0.48f, 0.18f),
                                    glm::vec3(glm::radians(88.0f),
                                              glm::radians(23.0f * static_cast<float>(i)),
                                              glm::radians(-3.0f + pseudo(i, 9.73f) * 6.0f)),
                                    glm::vec4(0.19f, 0.12f, 0.075f, 1.0f),
                                    "fabric",
                                    0.82f,
                                    0.42f,
                                    0.36f,
                                    genExt.groundWetness * 0.08f);
                        } else if (mode == 3) {
                            addPart("GenerativeExterior_HighDetailCamp_LogSeat" + std::to_string(i),
                                    cylinderMesh,
                                    glm::vec3(-0.92f + 0.38f * static_cast<float>((i / 6) % 6),
                                              0.22f,
                                              -0.54f - 0.18f * static_cast<float>((i / 6) % 3)),
                                    glm::vec3(0.11f, 0.88f, 0.11f),
                                    glm::vec3(glm::radians(88.0f),
                                              glm::radians(18.0f + 22.0f * static_cast<float>(i)),
                                              glm::radians(-6.0f + pseudo(i, 10.11f) * 12.0f)),
                                    glm::vec4(0.22f, 0.13f, 0.065f, 1.0f),
                                    "wood",
                                    0.84f,
                                    0.50f,
                                    0.48f,
                                    genExt.groundWetness * 0.18f);
                        } else if (mode == 4) {
                            addPart("GenerativeExterior_HighDetailCamp_Cookware" + std::to_string(i),
                                    sphereMesh,
                                    glm::vec3(-0.22f + 0.08f * static_cast<float>((i / 6) % 3),
                                              0.24f,
                                              0.54f + 0.07f * static_cast<float>((i / 6) % 4)),
                                    glm::vec3(0.16f, 0.10f, 0.16f),
                                    glm::vec3(0.0f, glm::radians(31.0f * static_cast<float>(i)), 0.0f),
                                    glm::vec4(0.050f, 0.046f, 0.042f, 1.0f),
                                    "masonry",
                                    0.56f,
                                    0.26f,
                                    0.34f,
                                    0.0f);
                        } else {
                            addPart("GenerativeExterior_HighDetailCamp_PackLantern" + std::to_string(i),
                                    cubeMesh,
                                    glm::vec3(1.05f + 0.18f * static_cast<float>((i / 6) % 4),
                                              0.32f,
                                              1.58f + 0.15f * static_cast<float>((i / 6) % 3)),
                                    glm::vec3(0.20f, 0.32f, 0.16f),
                                    glm::vec3(glm::radians(-2.0f + pseudo(i, 10.77f) * 4.0f),
                                              glm::radians(27.0f * static_cast<float>(i)),
                                              glm::radians(-3.0f + pseudo(i, 11.13f) * 6.0f)),
                                    glm::vec4(0.11f, 0.075f, 0.045f, 1.0f),
                                    "fabric",
                                    0.78f,
                                    0.38f,
                                    0.42f,
                                    genExt.groundWetness * 0.08f);
                        }
                        campPieces++;
                    }
                    if (campPieces > 0) {
                        spdlog::info("generative_exterior: created high detail camp kit pieces={}", campPieces);
                    }
                }

                int cabinPieces = 0;
                if (genExt.heroEnvironmentGeometry.highDetailCabinPieceCount > 0 && !genExt.structures.empty()) {
                    const auto& structure = genExt.structures.front();
                    const float yawRad = glm::radians(structure.yawDeg);
                    const float cs = std::cos(yawRad);
                    const float sn = std::sin(yawRad);
                    auto place = [&](float x, float y, float z) -> glm::vec3 {
                        return structure.position + glm::vec3(cs * x + sn * z,
                                                              y,
                                                              -sn * x + cs * z);
                    };
                    const float w = structure.widthM;
                    const float d = structure.depthM;
                    const float h = structure.wallHeightM;
                    const float frontZ = d * 0.5f + 0.18f;
                    const int target = genExt.heroEnvironmentGeometry.highDetailCabinPieceCount;
                    for (int i = 0; i < target; ++i) {
                        const int mode = i % 5;
                        if (mode == 0) {
                            const float row = static_cast<float>((i / 5) % 8);
                            addPart("GenerativeExterior_HighDetailCabin_LogCourse" + std::to_string(i),
                                    cylinderMesh,
                                    place(-w * 0.39f + row * w * 0.11f, 0.30f + row * 0.11f, frontZ + 0.035f),
                                    glm::vec3(0.042f, w * 0.18f, 0.042f),
                                    glm::vec3(glm::radians(88.0f), yawRad + glm::radians(90.0f), 0.0f),
                                    glm::vec4(0.24f, 0.14f, 0.070f, 1.0f),
                                    "wood",
                                    0.82f,
                                    0.55f,
                                    0.44f,
                                    genExt.groundWetness * 0.16f);
                        } else if (mode == 1) {
                            addPart("GenerativeExterior_HighDetailCabin_Rafter" + std::to_string(i),
                                    cubeMesh,
                                    place(-w * 0.42f + static_cast<float>((i / 5) % 6) * w * 0.17f,
                                          h + structure.roofHeightM * 0.55f,
                                          -d * 0.10f + static_cast<float>((i / 10) % 2) * d * 0.36f),
                                    glm::vec3(0.055f, 0.075f, d * 0.58f),
                                    glm::vec3(0.0f, yawRad, glm::radians(-8.0f + 3.0f * static_cast<float>(i % 4))),
                                    glm::vec4(0.10f, 0.060f, 0.036f, 1.0f),
                                    "wood",
                                    0.86f,
                                    0.48f,
                                    0.42f,
                                    genExt.groundWetness * 0.12f);
                        } else if (mode == 2) {
                            const float railX = (i % 2 == 0) ? -w * 0.36f : w * 0.36f;
                            addPart("GenerativeExterior_HighDetailCabin_PorchRail" + std::to_string(i),
                                    cubeMesh,
                                    place(railX, 0.64f, d * 0.5f + 0.82f + 0.10f * static_cast<float>((i / 5) % 3)),
                                    glm::vec3(0.050f, 0.54f, 0.050f),
                                    glm::vec3(0.0f, yawRad, 0.0f),
                                    glm::vec4(0.15f, 0.085f, 0.045f, 1.0f),
                                    "wood",
                                    0.82f,
                                    0.42f,
                                    0.40f,
                                    genExt.groundWetness * 0.12f);
                        } else if (mode == 3) {
                            addPart("GenerativeExterior_HighDetailCabin_FoundationRock" + std::to_string(i),
                                    shardMesh,
                                    place(-w * 0.44f + static_cast<float>((i / 5) % 8) * w * 0.13f,
                                          0.10f,
                                          d * 0.5f + 0.22f),
                                    glm::vec3(0.20f, 0.15f, 0.18f),
                                    glm::vec3(glm::radians(-3.0f + pseudo(i, 11.59f) * 6.0f),
                                              yawRad + glm::radians(12.0f * static_cast<float>(i % 5)),
                                              glm::radians(-4.0f + pseudo(i, 12.03f) * 8.0f)),
                                    glm::vec4(0.16f, 0.14f, 0.12f, 1.0f),
                                    "masonry",
                                    0.90f,
                                    0.58f,
                                    0.56f,
                                    genExt.groundWetness * 0.20f);
                        } else {
                            addPart("GenerativeExterior_HighDetailCabin_Woodpile" + std::to_string(i),
                                    cylinderMesh,
                                    place(w * 0.56f,
                                          0.16f + 0.045f * static_cast<float>((i / 5) % 3),
                                          d * 0.45f - 0.22f * static_cast<float>((i / 5) % 4)),
                                    glm::vec3(0.050f, 0.48f, 0.050f),
                                    glm::vec3(glm::radians(88.0f),
                                              yawRad + glm::radians(72.0f + 9.0f * static_cast<float>(i % 4)),
                                              glm::radians(-3.0f + pseudo(i, 12.47f) * 6.0f)),
                                    glm::vec4(0.20f, 0.12f, 0.060f, 1.0f),
                                    "wood",
                                    0.86f,
                                    0.48f,
                                    0.42f,
                                    genExt.groundWetness * 0.12f);
                        }
                        cabinPieces++;
                    }
                    if (cabinPieces > 0) {
                        spdlog::info("generative_exterior: created high detail cabin kit pieces={}", cabinPieces);
                    }
                }

                if (mountainLayers > 0 || cliffMassPieces > 0) {
                    spdlog::info("generative_exterior: created mountain massing geometry layers={} cliff_mass={}",
                                 mountainLayers,
                                 cliffMassPieces);
                }
                if (treeSilhouettes > 0) {
                    spdlog::info("generative_exterior: created irregular tree silhouette geometry trees={}", treeSilhouettes);
                }
                spdlog::info("generative_exterior: created hero environment geometry camp={} cabin={} mountain_layers={} cliff_mass={} shoreline_props={} tree_silhouettes={} support_props={}",
                             campPieces,
                             cabinPieces,
                             mountainLayers,
                             cliffMassPieces,
                             shorelineProps,
                             treeSilhouettes,
                             genExt.heroEnvironmentGeometry.supportPropCount);
            }
        }
    }

    if (genExt.valid && genExt.meshSilhouetteRealism.enabled && genExt.meshSilhouetteRealism.heroBevelDetailCount > 0) {
        if (auto* renderer = m_renderer.get()) {
            auto cubeMesh = Utils::MeshGenerator::CreateCube();
            auto cylinderMesh = Utils::MeshGenerator::CreateCylinder(0.5f, 1.0f, 16);
            const auto upCube = renderer->UploadMesh(cubeMesh);
            const auto upCylinder = renderer->UploadMesh(cylinderMesh);
            if (upCube.IsErr() || upCylinder.IsErr()) {
                spdlog::warn("generative_exterior: hero silhouette mesh upload failed cube='{}' cylinder='{}'",
                             upCube.IsErr() ? upCube.Error() : "ok",
                             upCylinder.IsErr() ? upCylinder.Error() : "ok");
            } else {
                auto dressBevel = [&](Scene::RenderableComponent& r,
                                      const glm::vec4& color,
                                      const char* preset,
                                      float roughness,
                                      float normalScale) {
                    r.albedoColor = color;
                    r.metallic = 0.0f;
                    r.roughness = roughness;
                    r.ao = 0.86f;
                    r.occlusionStrength = 0.74f;
                    r.normalScale = normalScale;
                    r.wetnessFactor = genExt.groundWetness * 0.18f;
                    r.proceduralMaskStrength = 0.34f;
                    r.specularFactor = 0.20f;
                    r.clearcoatFactor = std::min(genExt.groundWetness * 0.16f, 0.09f);
                    r.clearcoatRoughnessFactor = 0.72f;
                    r.anisotropyStrength = std::string(preset) == "wood" ? 0.32f : 0.12f;
                    r.sheenWeight = std::string(preset) == "fabric" ? 0.24f : 0.0f;
                    r.presetName = preset;
                    const std::string presetName = preset ? preset : "";
                    if (presetName == "wood") {
                        applyGeneratedTextureMaterial(r, "wood", true);
                    } else if (presetName == "fabric") {
                        applyGeneratedTextureMaterial(r, "fabric", true);
                    } else if (presetName == "masonry") {
                        applyGeneratedTextureMaterial(r, "rock", true);
                    }
                };
                auto addBevel = [&](const std::string& tag,
                                    const std::shared_ptr<Scene::MeshData>& mesh,
                                    const glm::vec3& position,
                                    const glm::vec3& scale,
                                    const glm::vec3& euler,
                                    const glm::vec4& color,
                                    const char* preset,
                                    float roughness,
                                    float normalScale) {
                    entt::entity part = m_registry->CreateEntity();
                    m_registry->AddComponent<Scene::TagComponent>(part, tag);
                    auto& t = m_registry->AddComponent<TransformComponent>(part);
                    t.position = position;
                    t.scale = scale;
                    t.rotation = glm::quat(euler);
                    auto& r = m_registry->AddComponent<Scene::RenderableComponent>(part);
                    r.mesh = mesh;
                    dressBevel(r, color, preset, roughness, normalScale);
                };

                int cabinBevels = 0;
                int campBevels = 0;
                const int bevelTarget = genExt.meshSilhouetteRealism.heroBevelDetailCount;
                if (!genExt.structures.empty()) {
                    const auto& structure = genExt.structures.front();
                    const float yawRad = glm::radians(structure.yawDeg);
                    const float cs = std::cos(yawRad);
                    const float sn = std::sin(yawRad);
                    auto place = [&](float x, float y, float z) -> glm::vec3 {
                        return structure.position + glm::vec3(cs * x + sn * z,
                                                              y,
                                                              -sn * x + cs * z);
                    };
                    const float w = structure.widthM;
                    const float d = structure.depthM;
                    const float h = structure.wallHeightM;
                    const float roofY = h + structure.roofHeightM * 0.20f;
                    const glm::vec4 darkWood(0.070f, 0.042f, 0.026f, 1.0f);
                    for (int i = 0; i < bevelTarget; ++i) {
                        if (i < 4) {
                            const bool side = (i % 2) == 0;
                            addBevel("GenerativeExterior_HeroSilhouette_CabinEave" + std::to_string(i),
                                     cubeMesh,
                                     place(side ? -w * 0.54f : w * 0.54f,
                                           roofY + 0.08f * static_cast<float>(i / 2),
                                           (i < 2) ? d * 0.28f : -d * 0.28f),
                                     glm::vec3(0.10f, 0.10f, d * 0.58f),
                                     glm::vec3(0.0f, yawRad, glm::radians(side ? -4.0f : 4.0f)),
                                     darkWood,
                                     "wood",
                                     0.86f,
                                     0.38f);
                        } else if (i < 8) {
                            const float x = (i % 2 == 0) ? -w * 0.43f : w * 0.43f;
                            const float z = (i < 6) ? d * 0.64f : d * 0.38f;
                            addBevel("GenerativeExterior_HeroSilhouette_CabinPorchPost" + std::to_string(i),
                                     cubeMesh,
                                     place(x, 0.58f, z),
                                     glm::vec3(0.075f, 1.02f, 0.075f),
                                     glm::vec3(0.0f, yawRad, glm::radians((i % 3 - 1) * 1.5f)),
                                     glm::vec4(0.12f, 0.072f, 0.040f, 1.0f),
                                     "wood",
                                     0.82f,
                                     0.34f);
                        } else {
                            const float col = static_cast<float>((i - 8) % 6);
                            addBevel("GenerativeExterior_HeroSilhouette_CabinFoundationStone" + std::to_string(i),
                                     cubeMesh,
                                     place(-w * 0.44f + col * w * 0.18f,
                                           0.10f,
                                           d * 0.58f + 0.10f * static_cast<float>((i - 8) / 6)),
                                     glm::vec3(0.22f, 0.14f, 0.16f),
                                     glm::vec3(glm::radians(2.0f * static_cast<float>(i % 3 - 1)),
                                               yawRad + glm::radians(7.0f * static_cast<float>(i % 5)),
                                               glm::radians(-2.0f + static_cast<float>(i % 4))),
                                     glm::vec4(0.18f, 0.16f, 0.14f, 1.0f),
                                     "masonry",
                                     0.90f,
                                     0.42f);
                        }
                        cabinBevels++;
                    }
                } else {
                    const glm::vec3 tentCenter(2.9f, 0.0f, 0.9f);
                    const float tentYaw = glm::radians(-18.0f);
                    const float cs = std::cos(tentYaw);
                    const float sn = std::sin(tentYaw);
                    auto tentPlace = [&](float x, float y, float z) -> glm::vec3 {
                        return tentCenter + glm::vec3(cs * x + sn * z, y, -sn * x + cs * z);
                    };
                    for (int i = 0; i < bevelTarget; ++i) {
                        if (i < 6) {
                            const float side = (i % 2 == 0) ? -1.0f : 1.0f;
                            addBevel("GenerativeExterior_HeroSilhouette_TentHem" + std::to_string(i),
                                     cubeMesh,
                                     tentPlace(side * 1.04f, 0.18f + 0.035f * static_cast<float>(i / 2), -0.84f + 0.42f * static_cast<float>(i / 2)),
                                     glm::vec3(0.075f, 0.075f, 0.78f),
                                     glm::vec3(glm::radians(1.5f * static_cast<float>(i % 3 - 1)),
                                               tentYaw,
                                               glm::radians(side * 5.0f)),
                                     glm::vec4(0.085f, 0.045f, 0.055f, 1.0f),
                                     "fabric",
                                     0.78f,
                                     0.36f);
                        } else if (i < 12) {
                            addBevel("GenerativeExterior_HeroSilhouette_TentFlapDepth" + std::to_string(i),
                                     cubeMesh,
                                     tentPlace(-0.55f + 0.22f * static_cast<float>((i - 6) % 6),
                                               0.48f + 0.045f * static_cast<float>((i - 6) / 3),
                                               1.18f),
                                     glm::vec3(0.055f, 0.20f, 0.055f),
                                     glm::vec3(glm::radians(6.0f),
                                               tentYaw,
                                               glm::radians(-6.0f + static_cast<float>(i % 4) * 3.0f)),
                                     glm::vec4(0.11f, 0.060f, 0.070f, 1.0f),
                                     "fabric",
                                     0.76f,
                                     0.34f);
                        } else {
                            addBevel("GenerativeExterior_HeroSilhouette_GearDepth" + std::to_string(i),
                                     cylinderMesh,
                                     glm::vec3(-2.70f + 0.26f * static_cast<float>((i - 12) % 6),
                                               0.16f,
                                               1.88f + 0.16f * static_cast<float>((i - 12) / 6)),
                                     glm::vec3(0.16f, 0.42f, 0.16f),
                                     glm::vec3(glm::radians(88.0f),
                                               glm::radians(17.0f * static_cast<float>(i)),
                                               glm::radians(4.0f * static_cast<float>(i % 3))),
                                     glm::vec4(0.18f, 0.10f, 0.055f, 1.0f),
                                     "wood",
                                     0.84f,
                                     0.38f);
                        }
                        campBevels++;
                    }
                }

                spdlog::info("generative_exterior: created hero silhouette bevel detail cabin={} camp={} prop_depth_layers={}",
                             cabinBevels,
                             campBevels,
                             genExt.meshSilhouetteRealism.propDepthLayerCount);
            }
        }
    }

    if (genExt.valid && genExt.textureMaterialFidelity.enabled) {
        spdlog::info("generative_exterior: texture material fidelity terrain={} rock={} wood={} fabric={} hero={} shore={} texture_sets={}",
                     textureMaterialCounts.terrain,
                     textureMaterialCounts.rock,
                     textureMaterialCounts.wood,
                     textureMaterialCounts.fabric,
                     textureMaterialCounts.hero,
                     textureMaterialCounts.shore,
                     textureMaterialCounts.sets.size());
    }

    if (genExt.valid && genExt.atmosphereFidelity.enabled) {
        if (auto* renderer = m_renderer.get()) {
            auto cubeMesh = Utils::MeshGenerator::CreateCube();
            const auto upCube = renderer->UploadMesh(cubeMesh);
            if (upCube.IsErr()) {
                spdlog::warn("generative_exterior: atmosphere detail mesh upload failed: {}", upCube.Error());
            } else {
                auto addAtmospherePart = [&](const std::string& tag,
                                             const glm::vec3& position,
                                             const glm::vec3& scale,
                                             const glm::vec3& euler,
                                             const glm::vec4& color,
                                             float roughness,
                                             float emissiveStrength = 0.0f) {
                    entt::entity part = m_registry->CreateEntity();
                    m_registry->AddComponent<Scene::TagComponent>(part, tag);
                    auto& t = m_registry->AddComponent<TransformComponent>(part);
                    t.position = position;
                    t.scale = scale;
                    t.rotation = glm::quat(euler);
                    auto& r = m_registry->AddComponent<Scene::RenderableComponent>(part);
                    r.mesh = cubeMesh;
                    r.albedoColor = color;
                    r.metallic = 0.0f;
                    r.roughness = roughness;
                    r.ao = 0.65f;
                    r.occlusionStrength = 0.45f;
                    r.normalScale = 0.04f;
                    r.proceduralMaskStrength = 0.08f;
                    r.specularFactor = 0.05f;
                    r.doubleSided = true;
                    r.alphaMode = Scene::RenderableComponent::AlphaMode::Blend;
                    r.renderLayer = Scene::RenderableComponent::RenderLayer::Overlay;
                    r.presetName = "naturalistic";
                    if (emissiveStrength > 0.0f) {
                        r.emissiveColor = glm::vec3(color);
                        r.emissiveStrength = emissiveStrength;
                        r.emissiveBloomFactor = 0.10f;
                    }
                };

                int hazeCount = 0;
                for (int i = 0; i < genExt.atmosphereFidelity.hazeDepthLayers; ++i) {
                    const float f = static_cast<float>(i);
                    const glm::vec3 hazeColor = genExt.atmosphereFidelity.nightSkyControl
                        ? glm::vec3(0.12f, 0.18f, 0.34f)
                        : glm::vec3(0.62f, 0.66f, 0.70f);
                    addAtmospherePart("GenerativeExterior_AtmosphereHazeBand" + std::to_string(i),
                                      glm::vec3(0.0f, 0.26f + f * 0.12f, -5.8f - f * 4.2f),
                                      glm::vec3(genExt.extent * (0.92f + f * 0.08f),
                                                0.18f + f * 0.03f,
                                                0.035f),
                                      glm::vec3(0.0f, glm::radians((i % 2 == 0) ? -2.0f : 2.0f), 0.0f),
                                      glm::vec4(hazeColor, genExt.atmosphereFidelity.nightSkyControl ? 0.105f : 0.075f),
                                      0.96f,
                                      genExt.atmosphereFidelity.nightSkyControl ? 0.18f : 0.0f);
                    hazeCount++;
                }

                int rainCount = 0;
                for (int i = 0; i < genExt.atmosphereFidelity.rainStreakCount; ++i) {
                    const float p = std::sin(static_cast<float>(i) * 12.9898f) * 43758.5453f;
                    const float u = p - std::floor(p);
                    const float q = std::sin(static_cast<float>(i) * 78.233f) * 19341.371f;
                    const float v = q - std::floor(q);
                    const float x = (u - 0.5f) * genExt.extent * 1.45f;
                    const float z = 5.6f - v * genExt.extent * 0.72f;
                    const float y = 1.15f + (i % 7) * 0.25f;
                    addAtmospherePart("GenerativeExterior_RainStreak" + std::to_string(i),
                                      glm::vec3(x, y, z),
                                      glm::vec3(0.018f, 0.68f + 0.08f * (i % 4), 0.018f),
                                      glm::vec3(glm::radians(-13.0f), glm::radians(6.0f), glm::radians(-10.0f)),
                                      glm::vec4(0.62f, 0.72f, 0.90f, 0.18f),
                                      0.35f,
                                      0.10f);
                    rainCount++;
                }

                spdlog::info("generative_exterior: atmospheric pass night_sky={} storm_layers={} haze_layers={} rain_streaks={}",
                             genExt.atmosphereFidelity.nightSkyControl ? "on" : "off",
                             genExt.atmosphereFidelity.stormLayerCount,
                             hazeCount,
                             rainCount);
            }
        }
    }

    if (genExt.valid && !genExt.ridgeLayers.empty()) {
        if (auto* renderer = m_renderer.get()) {
            int ridgeCount = 0;
            for (std::size_t i = 0; i < genExt.ridgeLayers.size(); ++i) {
                const auto& layer = genExt.ridgeLayers[i];
                const float width = genExt.extent * (2.25f + static_cast<float>(i) * 0.32f);
                const float baseY = -1.15f - static_cast<float>(i) * 0.55f;
                auto ridgeMesh = CreateGenerativeRidgeMesh(width,
                                                           layer.heightM,
                                                           baseY,
                                                           1.73f + static_cast<float>(i) * 4.91f);
                auto up = renderer->UploadMesh(ridgeMesh);
                if (up.IsErr()) {
                    spdlog::warn("generative_exterior: ridge mesh upload failed: {}", up.Error());
                    continue;
                }
                entt::entity ridge = m_registry->CreateEntity();
                m_registry->AddComponent<Scene::TagComponent>(
                    ridge, "GenerativeExterior_RidgeLayer" + std::to_string(i));
                auto& t = m_registry->AddComponent<TransformComponent>(ridge);
                t.position = glm::vec3(0.0f, 0.0f, -layer.distanceM);
                auto& r = m_registry->AddComponent<Scene::RenderableComponent>(ridge);
                r.mesh = ridgeMesh;
                const float depthFade = std::clamp(1.0f - static_cast<float>(i) * 0.18f, 0.55f, 1.0f);
                r.albedoColor = glm::vec4(glm::max(layer.color * depthFade, glm::vec3(0.035f)), 1.0f);
                r.metallic = 0.0f;
                r.roughness = 0.98f;
                r.ao = 1.0f;
                r.doubleSided = true;
                r.presetName = "naturalistic";
                ridgeCount++;
            }
            if (ridgeCount > 0) {
                spdlog::info("generative_exterior: created {} procedural ridge layer(s)", ridgeCount);
            }
            if (genExt.assetFidelity.enabled && genExt.assetFidelity.backdropDetailLayers > 0) {
                int backdropDetailCount = 0;
                for (int i = 0; i < genExt.assetFidelity.backdropDetailLayers; ++i) {
                    const auto& source = genExt.ridgeLayers[static_cast<std::size_t>(i) % genExt.ridgeLayers.size()];
                    const float width = genExt.extent * (1.85f + static_cast<float>(i) * 0.18f);
                    const float height = std::max(3.0f, source.heightM * (0.32f + 0.04f * static_cast<float>(i % 3)));
                    const float baseY = -0.62f - static_cast<float>(i) * 0.20f;
                    auto detailMesh = CreateGenerativeRidgeMesh(width,
                                                                height,
                                                                baseY,
                                                                9.37f + static_cast<float>(i) * 3.41f);
                    auto up = renderer->UploadMesh(detailMesh);
                    if (up.IsErr()) {
                        spdlog::warn("generative_exterior: backdrop detail ridge upload failed: {}", up.Error());
                        continue;
                    }
                    entt::entity detail = m_registry->CreateEntity();
                    m_registry->AddComponent<Scene::TagComponent>(
                        detail, "GenerativeExterior_BackdropSilhouetteDetail" + std::to_string(i));
                    auto& t = m_registry->AddComponent<TransformComponent>(detail);
                    t.position = glm::vec3((i % 2 == 0 ? -1.0f : 1.0f) * (1.4f + i * 0.22f),
                                           0.0f,
                                           -source.distanceM + 4.5f + static_cast<float>(i) * 2.15f);
                    auto& r = m_registry->AddComponent<Scene::RenderableComponent>(detail);
                    r.mesh = detailMesh;
                    const glm::vec3 notchColor = glm::mix(source.color, glm::vec3(0.025f, 0.030f, 0.040f), 0.24f + 0.08f * (i % 2));
                    r.albedoColor = glm::vec4(glm::max(notchColor, glm::vec3(0.030f)), 1.0f);
                    r.metallic = 0.0f;
                    r.roughness = 0.99f;
                    r.ao = 1.0f;
                    r.normalScale = 0.18f;
                    r.proceduralMaskStrength = 0.18f;
                    r.doubleSided = true;
                    r.presetName = "naturalistic";
                    backdropDetailCount++;
                }
                if (backdropDetailCount > 0) {
                    spdlog::info("generative_exterior: created backdrop silhouette detail layers={}",
                                 backdropDetailCount);
                }
            }
        }
    }

    if (genExt.valid) {
        // The real SUN for generative exteriors: a shadow-casting directional light
        // matching the IR sun (SetSunDirection only drives the sky/shadow state --
        // direct surface lighting needs the ECS light, same as the interior window sun).
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "GenerativeExterior_Sun");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        const float az = glm::radians(genExt.sunAz);
        const float el = glm::radians(genExt.sunEl);
        const glm::vec3 dirToLight = glm::normalize(
            glm::vec3(std::sin(az) * std::cos(el), std::sin(el), std::cos(az) * std::cos(el)));
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        const glm::vec3 lightTravelDirection = -dirToLight;
        if (std::abs(glm::dot(up, lightTravelDirection)) > 0.98f) {
            up = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        t.rotation = glm::quatLookAtLH(lightTravelDirection, up);
        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Directional;
        l.color = genExt.sunColor;
        l.intensity = genExt.sunInt * 1.5f * lightingBalance.sunScale;
        l.castsShadows = true;
    }

    if (genExt.valid && genExt.rimLightCount > 0) {
        int rimLights = 0;
        const bool moonRim = genExt.lookTime.find("moon") != std::string::npos ||
                             genExt.lookGrade.find("moon") != std::string::npos;
        for (int i = 0; i < genExt.rimLightCount; ++i) {
            const float side = (i % 2 == 0) ? -1.0f : 1.0f;
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "GenerativeExterior_RimLight" + std::to_string(i));
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(side * 8.5f, 3.6f, 3.8f - static_cast<float>(i) * 2.0f);
            const glm::vec3 target(0.0f, 0.62f, -4.6f);
            t.rotation = glm::quatLookAtLH(glm::normalize(target - t.position), glm::vec3(0.0f, 1.0f, 0.0f));
            auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
            l.type = Scene::LightType::Spot;
            l.color = moonRim ? glm::vec3(0.48f, 0.58f, 1.0f) : glm::vec3(1.0f, 0.52f, 0.24f);
            l.intensity = moonRim ? 3.2f : 4.4f;
            l.range = 22.0f;
            l.innerConeDegrees = 28.0f;
            l.outerConeDegrees = 64.0f;
            l.castsShadows = false;
            l.semanticClassId = 4u;
            rimLights++;
        }
        spdlog::info("generative_exterior: graphics lighting pass rim_lights={} fixed_exposure=on raking_key=on", rimLights);
    }

    if (!outdoor) {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Recipe_WindowSun_Directional");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        const glm::vec3 dirToLight = glm::normalize(glm::vec3(-0.42f, 0.74f, -0.52f));
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        const glm::vec3 lightTravelDirection = -dirToLight;
        if (std::abs(glm::dot(up, lightTravelDirection)) > 0.98f) {
            up = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        t.rotation = glm::quatLookAtLH(lightTravelDirection, up);

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Directional;
        l.color = glm::vec3(1.0f, 0.88f, 0.68f);
        l.intensity = std::clamp(4.25f + style.brightness * 0.20f, 3.3f, 4.9f) *
                      lightingBalance.sunScale;
        l.castsShadows = true;
    }

    // Soft key light: outdoor stays high like sun fill; interiors use a
    // ceiling-mounted spot just below the capped room shell.
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Recipe_KeyLight");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = outdoor ? glm::vec3(2.5f, 6.5f, -2.0f) : glm::vec3(0.55f, 2.58f, 0.25f);
        const glm::vec3 keyTarget = outdoor ? glm::vec3(0.55f, 0.0f, 0.6f) : glm::vec3(-0.30f, 0.55f, -1.35f);
        t.rotation = glm::quatLookAtLH(glm::normalize(keyTarget - t.position), glm::vec3(0.0f, 1.0f, 0.0f));
        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Spot;
        // Warm or cool key light by style (cool modern <-> warm rustic).
        l.color = glm::mix(glm::vec3(0.92f, 0.96f, 1.0f), glm::vec3(1.0f, 0.90f, 0.78f),
                           glm::clamp(style.warmth * 0.5f + 0.5f, 0.0f, 1.0f));
        l.intensity = (outdoor ? (10.0f + style.brightness * 2.5f) : (7.4f + style.brightness * 1.0f)) *
                      lightingBalance.localFixtureScale;
        l.range = outdoor ? 24.0f : 8.5f;
        l.innerConeDegrees = outdoor ? 48.0f : 46.0f;
        l.outerConeDegrees = outdoor ? 80.0f : 86.0f;
        l.castsShadows = true;
        l.semanticClassId = outdoor ? 3u : 1u;
    }

    // Interior 3/4 camera looks into the room from below the capped ceiling.
    {
        entt::entity cam = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(cam, "MainCamera");
        auto& t = m_registry->AddComponent<TransformComponent>(cam);
        // Interior eye-level 3/4 view from a front corner (front wall behind the
        // camera so it doesn't occlude), framed per recipe on its focal furniture.
        glm::vec3 camPos(2.7f, 1.7f, 2.9f);
        glm::vec3 target(-0.3f, 0.8f, -1.7f);
        float camFov = 60.0f;
        if (recipe == "kitchen") {
            camPos = glm::vec3(2.6f, 1.7f, 3.0f);
            target = glm::vec3(0.1f, 0.9f, -2.1f); // counter run + bar
        } else if (recipe == "bedroom") {
            camPos = glm::vec3(2.9f, 1.65f, 2.8f);
            target = glm::vec3(-0.2f, 0.7f, -1.9f); // bed
        } else if (recipe == "office") {
            camPos = glm::vec3(2.6f, 1.65f, 2.6f);
            target = glm::vec3(-1.0f, 0.8f, -1.9f); // desk + chair
        } else if (recipe == "dining_room") {
            camPos = glm::vec3(2.7f, 1.78f, 3.0f);
            target = glm::vec3(-0.1f, 0.6f, -1.3f); // table + chairs
        } else if (recipe == "bathroom") {
            camPos = glm::vec3(1.9f, 1.55f, 1.95f); // smaller room: camera sits inside
            target = glm::vec3(-0.1f, 0.55f, -1.4f); // tub + fixtures
        } else if (recipe == "garden") {
            camPos = glm::vec3(3.9f, 2.1f, 3.9f);  // outdoor 3/4 view centred on the patio set
            target = glm::vec3(0.2f, 0.35f, -0.9f);
            camFov = 52.0f;
        } else if (recipe == "generative_exterior") {
            if (genExt.valid && !genExt.structures.empty()) {
                // Cabin exteriors need more distance; otherwise the authored cabin
                // becomes a cropped wall and loses the lake/mountain context.
                camPos = glm::vec3(0.0f, 3.05f, 11.0f);
                target = glm::vec3(0.0f, 0.10f, -6.5f);
                camFov = 55.0f;
                spdlog::info("generative_exterior: shot camera pass profile=balanced_cabin_hero pos=(0.00,3.05,11.00) target=(0.00,0.10,-6.50) fov=55.0");
            } else {
                // Closer exterior hero shot: keeps the water/horizon depth cue, but
                // moves campsite/desert hero props out of tiny blockout scale.
                camPos = glm::vec3(0.0f, 2.25f, 8.0f);
                target = glm::vec3(0.0f, -0.04f, -6.5f);
                camFov = 52.0f;
                spdlog::info("generative_exterior: shot camera pass profile=closer_midground_hero pos=(0.00,2.25,8.00) target=(0.00,-0.04,-6.50) fov=52.0");
            }
        }
        // Showcase hero framing: low, front-centre, looking up at the blinded window so the
        // filtered volumetric daylight reads coming down past the furniture (avoids the
        // right-wall shelf that occluded the earlier side angle).
        const bool showcaseCam = !outdoor && (std::getenv("CORTEX_SHOWCASE") != nullptr);
        if (showcaseCam) {
            camPos = glm::vec3(0.15f, 0.85f, 2.05f);
            target = glm::vec3(0.0f, 1.62f, -3.0f); // the window high on the back wall
            camFov = 66.0f;
        }
        // A generative (model-composed) scene fills the WHOLE room, not one hero
        // subject, so it wants a wider establishing shot from the front doorway that
        // takes in the full floor + furniture arrangement. The critique loop refines
        // from here via the CORTEX_AUTOCAM_* overrides below.
        if (showcaseCam && recipe == "generative") {
            camPos = glm::vec3(0.1f, 1.5f, 2.95f);
            target = glm::vec3(0.0f, 0.85f, -1.4f); // room centre, slightly low
            camFov = 64.0f;
        }
        // --- Autonomous compose->render->critique->FIX overrides ---
        // The critique loop (tools/auto_scene.mjs) sets these env vars to reframe a bad
        // composition (e.g. a desk blocking the hero camera) and correct exposure between
        // iterations, WITHOUT hand-editing the recipe. All no-ops when unset, so normal
        // renders are unaffected -- this is how the per-scene hand-tuning is generalized.
        {
            auto autoEnvF = [](const char* n, float d) -> float {
                const char* v = std::getenv(n);
                if (!v || !*v) return d;
                char* e = nullptr; const float x = std::strtof(v, &e);
                return (e == v || !std::isfinite(x)) ? d : x;
            };
            const float dolly  = autoEnvF("CORTEX_AUTOCAM_DOLLY", 0.0f);   // +m back along the view ray
            const float lift   = autoEnvF("CORTEX_AUTOCAM_LIFT", 0.0f);    // +m up
            const float fovAdd = autoEnvF("CORTEX_AUTOCAM_FOV_ADD", 0.0f); // +deg (wider = more context)
            const float yaw    = autoEnvF("CORTEX_AUTOCAM_YAW", 0.0f);     // +deg pan the aim right
            if (std::fabs(dolly) > 1e-4f || std::fabs(lift) > 1e-4f) {
                const glm::vec3 viewDir = glm::normalize(target - camPos);
                camPos -= viewDir * dolly;   // dolly back = away from the target
                camPos.y += lift;
                target.y += lift * 0.4f;     // keep the framing roughly centred as we lift
            }
            if (std::fabs(yaw) > 1e-3f) {    // re-aim to re-centre a subject crowding one side
                const float rad = glm::radians(yaw);
                const glm::vec3 d = target - camPos;
                const float cs = std::cos(rad), sn = std::sin(rad);
                target = camPos + glm::vec3(cs * d.x + sn * d.z, d.y, -sn * d.x + cs * d.z);
            }
            camFov = glm::clamp(camFov + fovAdd, 28.0f, 100.0f);
            const float expMul = autoEnvF("CORTEX_AUTOEXPOSURE_MULT", 1.0f);
            if (auto* r = m_renderer.get(); r && std::fabs(expMul - 1.0f) > 1e-3f) {
                r->SetExposure(r->GetExposure() * glm::clamp(expMul, 0.2f, 5.0f));
            }
        }
        if (auto* renderer = m_renderer.get()) {
            const float focusDistance = glm::length(target - camPos);
            // Generative exteriors are wide establishing shots: keep everything crisp
            // (heavy DoF turns near-flank trees into translucent smears).
            const float focalRange = recipe == "generative_exterior" ? 12.0f
                                     : outdoor ? 4.5f : (recipe == "bathroom" ? 1.05f : 1.25f);
            const float dofAmount = recipe == "generative_exterior" ? 0.03f : (outdoor ? 0.12f : 0.30f);
            renderer->SetCinematicPostEffects(0.0f,
                                              dofAmount,
                                              focusDistance,
                                              focalRange,
                                              false,
                                              true);
        }
        t.position = camPos;
        t.rotation = glm::quatLookAtLH(glm::normalize(target - t.position), glm::vec3(0.0f, 1.0f, 0.0f));
        auto& c = m_registry->AddComponent<Scene::CameraComponent>(cam);
        c.fov = camFov;
        // Exterior generative scenes see out to the fogged horizon (water/sky), so the
        // far plane matches the beach showcase; rooms keep the tighter clip.
        ConfigureShowcaseCameraClip(c, recipe == "generative_exterior" ? 240.0f : 120.0f);
        c.isActive = true;
        m_activeCameraEntity = cam;
    }

    // Build the recipe onto the now-empty scene. A LOCAL command queue is used
    // because the engine's m_commandQueue is not constructed until after scene
    // initialization runs (RebuildScene happens during Engine init).
    LLM::CommandQueue recipeQueue;
    auto cmds = LLM::BuildSceneRecipe(recipe, recipeQueue.EnsureCatalog(), 0, style);
    if (cmds.empty()) {
        spdlog::warn("Recipe '{}' produced no commands; empty scene", recipe);
    } else {
        recipeQueue.PushBatch(cmds);
        recipeQueue.ExecuteAll(m_registry.get(), m_renderer.get());
    }
    const bool dustShowcase = (std::getenv("CORTEX_SHOWCASE") != nullptr);
    const glm::vec3 dustPos = outdoor ? glm::vec3(0.10f, 1.20f, -1.35f)
                            : (dustShowcase ? glm::vec3(0.05f, 1.15f, -2.0f)   // in the window-shaft path
                                            : glm::vec3(-0.12f, 1.34f, -1.18f));
    auto recipeDust = AddParticleEffect(*m_registry,
                      outdoor ? "Recipe_Garden_SunDust" : "Recipe_Room_ShaftDust",
                      "dust",
                      dustPos);
    if (!outdoor) {
        // Interior dust = a few subtle motes catching the light, not a field of speckle
        // (a no-context reviewer mistook the dense version for a reflection firefly).
        // Showcase: denser/brighter motes sitting in the window shaft so they read as
        // floating dust caught in the volumetric daylight, still kept below speckle.
        if (dustShowcase) {
            ScaleParticleEffect(*m_registry, recipeDust, 0.60f, 1.15f, 0.75f);
        } else {
            ScaleParticleEffect(*m_registry, recipeDust, 0.30f, 1.0f, 0.55f);
        }
    }
    spdlog::info("Recipe scene '{}' built ({} commands)", recipe, cmds.size());
}

void Engine::BuildLiquidGalleryScene() {
    spdlog::info("Building public scene: Liquid Gallery");

    auto* renderer = m_renderer.get();
    if (renderer) {
        Graphics::ApplyLiquidGallerySceneControls(*renderer);
    }

    auto floorPlane = Utils::MeshGenerator::CreatePlane(18.0f, 12.0f);
    auto wallPlane = Utils::MeshGenerator::CreatePlane(18.0f, 6.0f);
    auto liquidPlane = Utils::MeshGenerator::CreatePlane(3.1f, 3.1f);
    auto cubeMesh = Utils::MeshGenerator::CreateCube();
    auto sphereMesh = Utils::MeshGenerator::CreateSphere(0.5f, 32);
    auto quadMesh = Utils::MeshGenerator::CreateQuad(1.0f, 1.0f);
    auto scannedBarrelMesh = LoadNaturalisticShowcaseMesh("Barrel_01/Barrel_01_1k.gltf");
    auto scannedLanternMesh = LoadNaturalisticShowcaseMesh("Lantern_01/Lantern_01_1k.gltf");

    if (renderer) {
        auto uploadMesh = [&](const std::shared_ptr<Scene::MeshData>& mesh, const char* label) {
            if (!mesh) return true;
            auto res = renderer->UploadMesh(mesh);
            if (res.IsErr()) {
                spdlog::warn("Failed to upload LiquidGallery {} mesh: {}", label, res.Error());
                return false;
            }
            if (renderer->IsDeviceRemoved()) {
                spdlog::error("DX12 device was removed while uploading LiquidGallery {} mesh", label);
                return false;
            }
            return true;
        };

        if (!uploadMesh(floorPlane, "floor") ||
            !uploadMesh(wallPlane, "wall") ||
            !uploadMesh(liquidPlane, "liquid") ||
            !uploadMesh(cubeMesh, "cube") ||
            !uploadMesh(sphereMesh, "sphere") ||
            !uploadMesh(quadMesh, "quad") ||
            !uploadMesh(scannedBarrelMesh, "naturalistic Barrel_01") ||
            !uploadMesh(scannedLanternMesh, "naturalistic Lantern_01")) {
            return;
        }
    }

    {
        entt::entity camEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(camEntity, "MainCamera");
        auto& t = m_registry->AddComponent<TransformComponent>(camEntity);
        t.position = glm::vec3(-2.0f, 1.65f, -5.20f);
        const glm::vec3 target(-1.20f, 0.45f, -0.20f);
        t.rotation = glm::quatLookAtLH(glm::normalize(target - t.position), glm::vec3(0.0f, 1.0f, 0.0f));

        auto& cam = m_registry->AddComponent<Scene::CameraComponent>(camEntity);
        cam.fov = 42.0f;
        ConfigureShowcaseCameraClip(cam, 160.0f);
        cam.isActive = true;
        m_activeCameraEntity = camEntity;
    }

    auto addRenderable = [&](const std::string& tag,
                             const std::shared_ptr<Scene::MeshData>& mesh,
                             const glm::vec3& position,
                             const glm::vec3& scale,
                             const glm::vec3& euler,
                             const glm::vec4& color,
                             float metallic,
                             float roughness,
                             const char* preset) -> entt::entity {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, tag);
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = position;
        t.scale = scale;
        t.rotation = glm::quat(euler);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = mesh;
        r.albedoColor = color;
        r.metallic = metallic;
        r.roughness = roughness;
        r.ao = 1.0f;
        r.presetName = preset;
        return e;
    };

    if (floorPlane && floorPlane->gpuBuffers) {
        auto floor = addRenderable("LiquidGallery_Floor", floorPlane,
                                   glm::vec3(0.0f, -0.02f, 0.0f),
                                   glm::vec3(1.0f),
                                   glm::vec3(0.0f),
                                   glm::vec4(0.31f, 0.30f, 0.27f, 1.0f),
                                   0.0f, 0.58f, "wet_stone");
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(floor);
        r.doubleSided = true;
        r.wetnessFactor = 0.42f;
    }

    if (wallPlane && wallPlane->gpuBuffers) {
        auto wall = addRenderable("LiquidGallery_BackWall", wallPlane,
                                  glm::vec3(0.0f, 3.0f, 4.9f),
                                  glm::vec3(1.0f),
                                  glm::vec3(-glm::half_pi<float>(), 0.0f, 0.0f),
                                  glm::vec4(0.34f, 0.31f, 0.28f, 1.0f),
                                  0.0f, 0.70f, "masonry");
        m_registry->GetComponent<Scene::RenderableComponent>(wall).doubleSided = true;
    }

    if (cubeMesh && cubeMesh->gpuBuffers) {
        const struct GalleryTrim {
            const char* tag;
            glm::vec3 position;
            glm::vec3 scale;
            glm::vec4 color;
            const char* preset;
            float roughness;
        } trims[] = {
            {"LiquidGallery_BackWall_BaseTrim", glm::vec3(0.0f, 0.58f, 4.68f), glm::vec3(8.6f, 0.14f, 0.10f), glm::vec4(0.24f, 0.22f, 0.20f, 1.0f), "wet_stone", 0.50f},
            {"LiquidGallery_BackWall_TopTrim", glm::vec3(0.0f, 2.78f, 4.66f), glm::vec3(7.8f, 0.10f, 0.10f), glm::vec4(0.28f, 0.25f, 0.22f, 1.0f), "wet_stone", 0.52f},
            {"LiquidGallery_BackWall_RecessedShadow", glm::vec3(-1.35f, 1.68f, 4.57f), glm::vec3(5.45f, 1.38f, 0.08f), glm::vec4(0.13f, 0.13f, 0.12f, 1.0f), "wet_stone", 0.44f},
            {"LiquidGallery_LeftSideWall_Return", glm::vec3(-7.05f, 1.34f, 0.72f), glm::vec3(0.12f, 1.32f, 4.70f), glm::vec4(0.24f, 0.22f, 0.20f, 1.0f), "masonry", 0.64f},
            {"LiquidGallery_RightSideWall_Return", glm::vec3(4.26f, 1.34f, 0.72f), glm::vec3(0.12f, 1.32f, 4.70f), glm::vec4(0.24f, 0.22f, 0.20f, 1.0f), "masonry", 0.64f},
            {"LiquidGallery_LeftAisleLine", glm::vec3(-2.72f, 0.035f, 0.55f), glm::vec3(0.065f, 0.030f, 4.95f), glm::vec4(0.18f, 0.21f, 0.23f, 1.0f), "wet_stone", 0.36f},
            {"LiquidGallery_RightAisleLine", glm::vec3(3.90f, 0.035f, 0.55f), glm::vec3(0.065f, 0.030f, 4.95f), glm::vec4(0.18f, 0.21f, 0.23f, 1.0f), "wet_stone", 0.36f},
            {"LiquidGallery_CenterDrainGrate", glm::vec3(-1.35f, 0.045f, 0.58f), glm::vec3(0.46f, 0.030f, 0.13f), glm::vec4(0.10f, 0.11f, 0.11f, 1.0f), "brushed_metal", 0.28f}
        };
        for (const auto& trim : trims) {
            auto e = addRenderable(trim.tag, cubeMesh, trim.position, trim.scale, glm::vec3(0.0f),
                                   trim.color, std::string(trim.preset) == "brushed_metal" ? 1.0f : 0.0f,
                                   trim.roughness, trim.preset);
            if (std::string(trim.preset) == "wet_stone") {
                m_registry->GetComponent<Scene::RenderableComponent>(e).wetnessFactor = 0.48f;
            }
        }

        const struct IntegratedDeckPiece {
            const char* tag;
            glm::vec3 position;
            glm::vec3 scale;
            glm::vec4 color;
            float metallic;
            float roughness;
            const char* preset;
        } deckPieces[] = {
            {"LiquidGallery_IntegratedCountertop", glm::vec3(-1.42f, 0.055f, 0.55f), glm::vec3(10.55f, 0.10f, 3.58f), glm::vec4(0.26f, 0.24f, 0.21f, 1.0f), 0.0f, 0.38f, "wet_stone"},
            {"LiquidGallery_ContinuousVatDeck", glm::vec3(-1.42f, 0.018f, 0.55f), glm::vec3(10.35f, 0.040f, 3.42f), glm::vec4(0.21f, 0.20f, 0.18f, 1.0f), 0.0f, 0.44f, "wet_stone"},
            {"LiquidGallery_FrontApron", glm::vec3(-1.42f, 0.32f, -2.90f), glm::vec3(10.70f, 0.42f, 0.20f), glm::vec4(0.34f, 0.29f, 0.23f, 1.0f), 0.0f, 0.46f, "wet_stone"},
            {"LiquidGallery_RearApron", glm::vec3(-1.42f, 0.32f, 3.98f), glm::vec3(10.70f, 0.42f, 0.20f), glm::vec4(0.34f, 0.29f, 0.23f, 1.0f), 0.0f, 0.46f, "wet_stone"},
            {"LiquidGallery_LeftApron", glm::vec3(-6.78f, 0.32f, 0.55f), glm::vec3(0.22f, 0.42f, 3.62f), glm::vec4(0.34f, 0.29f, 0.23f, 1.0f), 0.0f, 0.46f, "wet_stone"},
            {"LiquidGallery_RightApron", glm::vec3(3.94f, 0.32f, 0.55f), glm::vec3(0.22f, 0.42f, 3.62f), glm::vec4(0.34f, 0.29f, 0.23f, 1.0f), 0.0f, 0.46f, "wet_stone"},
            {"LiquidGallery_CenterSpine", glm::vec3(-1.42f, 0.40f, 0.55f), glm::vec3(10.40f, 0.12f, 0.16f), glm::vec4(0.17f, 0.17f, 0.15f, 1.0f), 0.0f, 0.40f, "wet_stone"},
            {"LiquidGallery_CrossSpine", glm::vec3(-1.42f, 0.39f, 0.55f), glm::vec3(0.14f, 0.10f, 3.30f), glm::vec4(0.17f, 0.17f, 0.15f, 1.0f), 0.0f, 0.40f, "wet_stone"}
        };
        for (const auto& piece : deckPieces) {
            auto e = addRenderable(piece.tag, cubeMesh, piece.position, piece.scale, glm::vec3(0.0f),
                                   piece.color, piece.metallic, piece.roughness, piece.preset);
            m_registry->GetComponent<Scene::RenderableComponent>(e).wetnessFactor = 0.52f;
        }
    }

    struct LiquidVat {
        const char* name;
        Scene::WaterSurfaceComponent::LiquidType type;
        glm::vec3 center;
        glm::vec4 surfaceColor;
        glm::vec3 shallow;
        glm::vec3 deep;
        float absorption;
        float foam;
        float viscosity;
        float emissiveHeat;
        float roughness;
        float bodyThickness;
        float sloshStrength;
        float meniscusStrength;
        float flowSpeed;
        const char* preset;
    };

    const LiquidVat vats[] = {
        {"Water", Scene::WaterSurfaceComponent::LiquidType::Water,
         {-4.2f, 0.0f, -1.15f}, {0.08f, 0.42f, 0.72f, 0.72f},
         {0.10f, 0.56f, 0.84f}, {0.005f, 0.06f, 0.23f}, 0.42f, 0.95f, 0.12f, 0.0f, 0.032f,
         0.52f, 0.36f, 0.44f, 1.20f, "water"},
        {"Lava", Scene::WaterSurfaceComponent::LiquidType::Lava,
         {1.35f, 0.0f, -1.15f}, {1.0f, 0.25f, 0.04f, 0.96f},
         {1.0f, 0.42f, 0.06f}, {0.18f, 0.025f, 0.008f}, 0.95f, 0.0f, 0.78f, 5.2f, 0.24f,
         0.84f, 0.14f, 0.62f, 0.46f, "lava"},
        {"Honey", Scene::WaterSurfaceComponent::LiquidType::Honey,
         {-4.2f, 0.0f, 2.25f}, {1.0f, 0.62f, 0.14f, 0.82f},
         {1.0f, 0.75f, 0.20f}, {0.46f, 0.20f, 0.035f}, 0.58f, 0.08f, 0.78f, 0.0f, 0.16f,
         0.82f, 0.10f, 0.74f, 0.32f, "honey"},
        {"Molasses", Scene::WaterSurfaceComponent::LiquidType::Molasses,
         {1.35f, 0.0f, 2.25f}, {0.15f, 0.065f, 0.025f, 0.88f},
         {0.25f, 0.11f, 0.04f}, {0.025f, 0.010f, 0.004f}, 0.90f, 0.02f, 0.95f, 0.0f, 0.10f,
         0.96f, 0.06f, 0.88f, 0.18f, "molasses"},
    };

    for (const LiquidVat& vat : vats) {
        if (cubeMesh && cubeMesh->gpuBuffers) {
            auto basin = addRenderable(std::string("LiquidGallery_") + vat.name + "_Basin",
                                       cubeMesh,
                                       vat.center + glm::vec3(0.0f, 0.16f, 0.0f),
                                       glm::vec3(1.82f, 0.30f, 1.82f),
                                       glm::vec3(0.0f),
                                       glm::vec4(0.25f, 0.23f, 0.20f, 1.0f),
                                       0.0f, 0.42f, "wet_stone");
            auto& basinR = m_registry->GetComponent<Scene::RenderableComponent>(basin);
            basinR.wetnessFactor = 0.56f;

            addRenderable(std::string("LiquidGallery_") + vat.name + "_BackLip",
                          cubeMesh,
                          vat.center + glm::vec3(0.0f, 0.48f, 1.68f),
                          glm::vec3(2.05f, 0.36f, 0.28f),
                          glm::vec3(0.0f),
                          glm::vec4(0.30f, 0.26f, 0.20f, 1.0f),
                          0.0f, 0.40f, "wet_stone");
            addRenderable(std::string("LiquidGallery_") + vat.name + "_FrontLip",
                          cubeMesh,
                          vat.center + glm::vec3(0.0f, 0.48f, -1.68f),
                          glm::vec3(2.05f, 0.36f, 0.28f),
                          glm::vec3(0.0f),
                          glm::vec4(0.30f, 0.26f, 0.20f, 1.0f),
                          0.0f, 0.40f, "wet_stone");
            addRenderable(std::string("LiquidGallery_") + vat.name + "_LeftLip",
                          cubeMesh,
                          vat.center + glm::vec3(-1.68f, 0.48f, 0.0f),
                          glm::vec3(0.28f, 0.36f, 2.05f),
                          glm::vec3(0.0f),
                          glm::vec4(0.30f, 0.26f, 0.20f, 1.0f),
                          0.0f, 0.40f, "wet_stone");
            addRenderable(std::string("LiquidGallery_") + vat.name + "_RightLip",
                          cubeMesh,
                          vat.center + glm::vec3(1.68f, 0.48f, 0.0f),
                          glm::vec3(0.28f, 0.36f, 2.05f),
                          glm::vec3(0.0f),
                          glm::vec4(0.30f, 0.26f, 0.20f, 1.0f),
                          0.0f, 0.40f, "wet_stone");

            const glm::vec3 cornerOffsets[] = {
                {-1.68f, 0.50f, -1.68f},
                { 1.68f, 0.50f, -1.68f},
                {-1.68f, 0.50f,  1.68f},
                { 1.68f, 0.50f,  1.68f}
            };
            for (int c = 0; c < 4; ++c) {
                auto corner = addRenderable(std::string("LiquidGallery_") + vat.name + "_CornerPost_" + std::to_string(c),
                                            cubeMesh,
                                            vat.center + cornerOffsets[c],
                                            glm::vec3(0.34f, 0.42f, 0.34f),
                                            glm::vec3(0.0f),
                                            glm::vec4(0.22f, 0.20f, 0.17f, 1.0f),
                                            0.0f, 0.38f, "wet_stone");
                m_registry->GetComponent<Scene::RenderableComponent>(corner).wetnessFactor = 0.58f;
            }
        }

        if (liquidPlane && liquidPlane->gpuBuffers) {
            if (cubeMesh && cubeMesh->gpuBuffers) {
                auto body = addRenderable(std::string("LiquidGallery_") + vat.name + "_Body",
                                          cubeMesh,
                                          vat.center + glm::vec3(0.0f, 0.12f, 0.0f),
                                          glm::vec3(1.62f, 0.24f + vat.bodyThickness * 0.16f, 1.62f),
                                          glm::vec3(0.0f),
                                          glm::vec4(vat.deep, 0.34f + vat.bodyThickness * 0.20f),
                                          0.0f, vat.roughness, vat.preset);
                auto& bodyR = m_registry->GetComponent<Scene::RenderableComponent>(body);
                bodyR.transmissionFactor = (vat.type == Scene::WaterSurfaceComponent::LiquidType::Lava) ? 0.0f : 0.22f;
                bodyR.ior = (vat.type == Scene::WaterSurfaceComponent::LiquidType::Honey) ? 1.47f : 1.33f;
                bodyR.specularFactor = (vat.type == Scene::WaterSurfaceComponent::LiquidType::Molasses) ? 0.9f : 1.1f;
                if (vat.emissiveHeat > 0.0f) {
                    bodyR.emissiveColor = vat.shallow;
                    bodyR.emissiveStrength = vat.emissiveHeat * 0.65f;
                    bodyR.emissiveBloomFactor = 0.55f;
                }
            }

            auto liquid = addRenderable(std::string("LiquidGallery_") + vat.name + "_Surface",
                                        liquidPlane,
                                        vat.center + glm::vec3(0.0f, 0.24f, 0.0f),
                                        glm::vec3(1.0f),
                                        glm::vec3(0.0f),
                                        vat.surfaceColor,
                                        0.0f, vat.roughness, vat.preset);
            auto& r = m_registry->GetComponent<Scene::RenderableComponent>(liquid);
            r.transmissionFactor = (vat.type == Scene::WaterSurfaceComponent::LiquidType::Water ||
                                    vat.type == Scene::WaterSurfaceComponent::LiquidType::Honey) ? 0.35f : 0.0f;
            r.ior = (vat.type == Scene::WaterSurfaceComponent::LiquidType::Honey) ? 1.47f : 1.33f;
            if (vat.emissiveHeat > 0.0f) {
                r.emissiveColor = vat.shallow;
                r.emissiveStrength = vat.emissiveHeat;
                r.emissiveBloomFactor = 0.8f;
            }

            Scene::WaterSurfaceComponent liquidComponent{};
            liquidComponent.liquidType = vat.type;
            liquidComponent.absorption = vat.absorption;
            liquidComponent.foamStrength = vat.foam;
            liquidComponent.viscosity = vat.viscosity;
            liquidComponent.emissiveHeat = vat.emissiveHeat;
            liquidComponent.bodyThickness = vat.bodyThickness;
            liquidComponent.sloshStrength = vat.sloshStrength;
            liquidComponent.meniscusStrength = vat.meniscusStrength;
            liquidComponent.flowSpeed = vat.flowSpeed;
            liquidComponent.shallowTint = vat.shallow;
            liquidComponent.deepTint = vat.deep;
            m_registry->AddComponent<Scene::WaterSurfaceComponent>(liquid, liquidComponent);
        }
    }

    if (sphereMesh && sphereMesh->gpuBuffers) {
        addRenderable("LiquidGallery_ChromeProbe", sphereMesh,
                      glm::vec3(-1.35f, 0.92f, -3.0f),
                      glm::vec3(0.72f),
                      glm::vec3(0.0f),
                      glm::vec4(0.88f, 0.90f, 0.96f, 1.0f),
                      1.0f, 0.045f, "chrome");
        auto glassProbe = addRenderable("LiquidGallery_GlassProbe", sphereMesh,
                                        glm::vec3(4.3f, 0.88f, 0.65f),
                                        glm::vec3(0.62f),
                                        glm::vec3(0.0f),
                                        glm::vec4(0.70f, 0.88f, 1.0f, 1.0f),
                                        0.0f, 0.025f, "glass");
        auto& glassR = m_registry->GetComponent<Scene::RenderableComponent>(glassProbe);
        glassR.transmissionFactor = 0.74f;
        glassR.ior = 1.50f;
        glassR.specularFactor = 1.35f;
    }

    if (quadMesh && quadMesh->gpuBuffers) {
        auto labelGlow = addRenderable("LiquidGallery_WarmReflectionPanel", quadMesh,
                                       glm::vec3(-1.25f, 2.70f, 4.62f),
                                       glm::vec3(3.25f, 0.48f, 1.0f),
                                       glm::vec3(0.0f),
                                       glm::vec4(1.0f, 0.52f, 0.22f, 1.0f),
                                       0.0f, 0.20f, "emissive_panel");
        auto& panel = m_registry->GetComponent<Scene::RenderableComponent>(labelGlow);
        panel.emissiveColor = glm::vec3(1.0f, 0.45f, 0.16f);
        panel.emissiveStrength = 1.55f;
        panel.doubleSided = true;

        auto coolPanel = addRenderable("LiquidGallery_CoolReflectionPanel", quadMesh,
                                       glm::vec3(2.38f, 2.52f, 4.61f),
                                       glm::vec3(2.55f, 0.38f, 1.0f),
                                       glm::vec3(0.0f),
                                       glm::vec4(0.36f, 0.62f, 1.0f, 1.0f),
                                       0.0f, 0.20f, "emissive_panel");
        auto& coolPanelR = m_registry->GetComponent<Scene::RenderableComponent>(coolPanel);
        coolPanelR.emissiveColor = glm::vec3(0.18f, 0.42f, 1.0f);
        coolPanelR.emissiveStrength = 0.90f;
        coolPanelR.doubleSided = true;
    }

    AddParticleEffect(*m_registry, "LiquidGallery_LavaEmbers", "embers", glm::vec3(1.35f, 0.80f, -1.15f));
    AddParticleEffect(*m_registry, "LiquidGallery_WaterMist", "mist", glm::vec3(-4.2f, 0.62f, -1.15f));

    if (scannedBarrelMesh && scannedBarrelMesh->gpuBuffers) {
        const struct LiquidGalleryBarrelAsset {
            const char* tag;
            glm::vec3 position;
            glm::vec3 scale;
            float yaw;
        } barrels[] = {
            {"LiquidGallery_ScannedBarrel_Left", glm::vec3(-6.15f, 0.28f, -2.55f), glm::vec3(0.46f), 0.22f},
            {"LiquidGallery_ScannedBarrel_Right", glm::vec3(3.35f, 0.28f, -2.35f), glm::vec3(0.42f), -0.34f}
        };
        for (const auto& barrel : barrels) {
            auto e = addRenderable(barrel.tag, scannedBarrelMesh, barrel.position, barrel.scale,
                                   glm::vec3(0.0f, barrel.yaw, 0.0f),
                                   glm::vec4(0.50f, 0.34f, 0.20f, 1.0f),
                                   0.0f, 0.44f, "wood");
            auto& r = m_registry->GetComponent<Scene::RenderableComponent>(e);
            ApplyNaturalisticAssetTextures(r, "Barrel_01");
            r.wetnessFactor = 0.34f;
        }
    }

    if (scannedLanternMesh && scannedLanternMesh->gpuBuffers) {
        const struct LiquidGalleryLanternAsset {
            const char* tag;
            glm::vec3 position;
            glm::vec3 scale;
            float yaw;
        } lanterns[] = {
            {"LiquidGallery_ScannedLantern_Water", glm::vec3(-5.88f, 0.52f, 1.98f), glm::vec3(0.30f), 0.42f},
            {"LiquidGallery_ScannedLantern_Lava", glm::vec3(3.05f, 0.52f, 1.86f), glm::vec3(0.30f), -0.26f}
        };
        for (const auto& lantern : lanterns) {
            auto e = addRenderable(lantern.tag, scannedLanternMesh, lantern.position, lantern.scale,
                                   glm::vec3(0.0f, lantern.yaw, 0.0f),
                                   glm::vec4(0.92f, 0.68f, 0.36f, 1.0f),
                                   1.0f, 0.24f, "brushed_gold");
            ApplyNaturalisticAssetTextures(m_registry->GetComponent<Scene::RenderableComponent>(e), "Lantern_01");
        }
    }

    auto addLight = [&](const char* tag,
                        Scene::LightType type,
                        const glm::vec3& position,
                        const glm::vec3& direction,
                        const glm::vec3& color,
                        float intensity,
                        float range) {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, tag);
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = position;
        if (glm::length(direction) > 0.001f) {
            t.rotation = glm::quatLookAtLH(glm::normalize(direction), glm::vec3(0.0f, 1.0f, 0.0f));
        }
        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = type;
        l.color = color;
        l.intensity = intensity;
        l.range = range;
        l.castsShadows = type != Scene::LightType::Point;
        if (type == Scene::LightType::Spot) {
            l.innerConeDegrees = 24.0f;
            l.outerConeDegrees = 44.0f;
        } else if (type == Scene::LightType::AreaRect) {
            l.areaSize = glm::vec2(5.5f, 2.2f);
        }
    };

    addLight("LiquidGallery_KeySoftbox", Scene::LightType::AreaRect,
             glm::vec3(-3.5f, 4.4f, -4.4f), glm::vec3(0.45f, -0.72f, 0.42f),
             glm::vec3(1.0f, 0.86f, 0.68f), 3.8f, 28.0f);
    addLight("LiquidGallery_CoolRim", Scene::LightType::Spot,
             glm::vec3(4.8f, 3.8f, -3.6f), glm::vec3(-0.58f, -0.54f, 0.62f),
             glm::vec3(0.54f, 0.72f, 1.0f), 4.5f, 24.0f);
    addLight("LiquidGallery_LavaFill", Scene::LightType::Point,
             glm::vec3(1.35f, 1.0f, -1.15f), glm::vec3(0.0f),
             glm::vec3(1.0f, 0.34f, 0.08f), 3.2f, 9.0f);
}

void Engine::BuildEffectsShowcaseScene() {
    spdlog::info("Building public scene: Effects Showcase");

    auto* renderer = m_renderer.get();
    if (renderer) {
        Graphics::ApplyEffectsShowcaseSceneControls(*renderer);
    }

    auto floorPlane = Utils::MeshGenerator::CreatePlane(18.0f, 12.0f);
    auto wallPlane = Utils::MeshGenerator::CreatePlane(18.0f, 6.0f);
    auto sideWallPlane = Utils::MeshGenerator::CreatePlane(12.0f, 6.0f);
    auto quadMesh = Utils::MeshGenerator::CreateQuad(1.0f, 1.0f);
    auto cubeMesh = Utils::MeshGenerator::CreateCube();
    auto sphereMesh = Utils::MeshGenerator::CreateSphere(0.5f, 32);
    auto cylinderMesh = Utils::MeshGenerator::CreateCylinder(0.22f, 2.4f, 32);
    auto torusMesh = Utils::MeshGenerator::CreateTorus(0.58f, 0.14f, 32, 16);
    auto scannedBarrelMesh = LoadNaturalisticShowcaseMesh("Barrel_01/Barrel_01_1k.gltf");
    auto scannedLanternMesh = LoadNaturalisticShowcaseMesh("Lantern_01/Lantern_01_1k.gltf");

    if (renderer) {
        auto uploadMesh = [&](const std::shared_ptr<Scene::MeshData>& mesh, const char* label) {
            if (!mesh) return true;
            auto res = renderer->UploadMesh(mesh);
            if (res.IsErr()) {
                spdlog::warn("Failed to upload EffectsShowcase {} mesh: {}", label, res.Error());
                return false;
            }
            if (renderer->IsDeviceRemoved()) {
                spdlog::error("DX12 device was removed while uploading EffectsShowcase {} mesh", label);
                return false;
            }
            return true;
        };

        if (!uploadMesh(floorPlane, "floor") ||
            !uploadMesh(wallPlane, "wall") ||
            !uploadMesh(sideWallPlane, "side wall") ||
            !uploadMesh(quadMesh, "quad") ||
            !uploadMesh(cubeMesh, "cube") ||
            !uploadMesh(sphereMesh, "sphere") ||
            !uploadMesh(cylinderMesh, "cylinder") ||
            !uploadMesh(torusMesh, "torus") ||
            !uploadMesh(scannedBarrelMesh, "naturalistic Barrel_01") ||
            !uploadMesh(scannedLanternMesh, "naturalistic Lantern_01")) {
            return;
        }
    }

    {
        entt::entity camEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(camEntity, "MainCamera");
        auto& t = m_registry->AddComponent<TransformComponent>(camEntity);
        t.position = glm::vec3(0.0f, 2.15f, -8.6f);
        const glm::vec3 target(0.0f, 1.25f, -0.15f);
        t.rotation = glm::quatLookAtLH(glm::normalize(target - t.position), glm::vec3(0.0f, 1.0f, 0.0f));

        auto& cam = m_registry->AddComponent<Scene::CameraComponent>(camEntity);
        cam.fov = 55.0f;
        ConfigureShowcaseCameraClip(cam, 140.0f);
        cam.isActive = true;
        m_activeCameraEntity = camEntity;
    }

    auto addRenderable = [&](const char* tag,
                             const std::shared_ptr<Scene::MeshData>& mesh,
                             const glm::vec3& position,
                             const glm::vec3& scale,
                             const glm::vec3& euler,
                             const glm::vec4& color,
                             float metallic,
                             float roughness,
                             const char* preset) -> entt::entity {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, tag);
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = position;
        t.scale = scale;
        t.rotation = glm::quat(euler);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = mesh;
        r.albedoColor = color;
        r.metallic = metallic;
        r.roughness = roughness;
        r.ao = 1.0f;
        r.presetName = preset;
        return e;
    };

    if (floorPlane && floorPlane->gpuBuffers) {
        auto floor = addRenderable("EffectsShowcase_Floor", floorPlane,
                                   glm::vec3(0.0f, 0.0f, 0.0f),
                                   glm::vec3(1.0f),
                                   glm::vec3(0.0f),
                                   glm::vec4(0.10f, 0.11f, 0.14f, 1.0f),
                                   0.0f, 0.62f, "masonry");
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(floor);
        r.doubleSided = true;
        r.normalScale = 0.18f;
    }

    if (wallPlane && wallPlane->gpuBuffers) {
        auto back = addRenderable("EffectsShowcase_BackWall", wallPlane,
                                  glm::vec3(0.0f, 3.0f, 4.8f),
                                  glm::vec3(1.0f),
                                  glm::vec3(-glm::half_pi<float>(), 0.0f, 0.0f),
                                  glm::vec4(0.08f, 0.09f, 0.13f, 1.0f),
                                  0.0f, 0.72f, "backdrop");
        m_registry->GetComponent<Scene::RenderableComponent>(back).doubleSided = true;
    }

    if (sideWallPlane && sideWallPlane->gpuBuffers) {
        auto left = addRenderable("EffectsShowcase_LeftWall", sideWallPlane,
                                  glm::vec3(-9.0f, 3.0f, -1.0f),
                                  glm::vec3(1.0f),
                                  glm::vec3(-glm::half_pi<float>(), glm::half_pi<float>(), 0.0f),
                                  glm::vec4(0.09f, 0.09f, 0.12f, 1.0f),
                                  0.0f, 0.76f, "masonry");
        auto right = addRenderable("EffectsShowcase_RightWall", sideWallPlane,
                                   glm::vec3(9.0f, 3.0f, -1.0f),
                                   glm::vec3(1.0f),
                                   glm::vec3(-glm::half_pi<float>(), -glm::half_pi<float>(), 0.0f),
                                   glm::vec4(0.09f, 0.09f, 0.12f, 1.0f),
                                   0.0f, 0.76f, "masonry");
        m_registry->GetComponent<Scene::RenderableComponent>(left).doubleSided = true;
        m_registry->GetComponent<Scene::RenderableComponent>(right).doubleSided = true;
    }

    if (cubeMesh && cubeMesh->gpuBuffers) {
        auto plinthA = addRenderable("EffectsShowcase_CenterPlinth", cubeMesh,
                                     glm::vec3(-2.1f, 0.28f, -0.1f),
                                     glm::vec3(1.45f, 0.56f, 1.45f),
                                     glm::vec3(0.0f),
                                     glm::vec4(0.18f, 0.18f, 0.22f, 1.0f),
                                     0.0f, 0.5f, "backdrop");
        auto plinthB = addRenderable("EffectsShowcase_GlassPlinth", cubeMesh,
                                     glm::vec3(2.2f, 0.28f, -0.2f),
                                     glm::vec3(1.35f, 0.56f, 1.35f),
                                     glm::vec3(0.0f),
                                     glm::vec4(0.16f, 0.16f, 0.20f, 1.0f),
                                     0.0f, 0.52f, "backdrop");
        (void)plinthA;
        (void)plinthB;
    }

    if (scannedBarrelMesh && scannedBarrelMesh->gpuBuffers) {
        const struct BarrelProp {
            glm::vec3 position;
            glm::vec3 scale;
            float yaw;
        } barrels[] = {
            {glm::vec3(-5.85f, 0.02f, 0.95f), glm::vec3(1.25f), 0.34f},
            {glm::vec3( 5.65f, 0.02f, 1.15f), glm::vec3(1.10f), -0.52f}
        };
        for (int i = 0; i < 2; ++i) {
            auto barrel = addRenderable(("EffectsShowcase_ScannedBarrel_" + std::to_string(i)).c_str(),
                                        scannedBarrelMesh,
                                        barrels[i].position,
                                        barrels[i].scale,
                                        glm::vec3(0.0f, barrels[i].yaw, 0.0f),
                                        glm::vec4(0.56f, 0.13f, 0.08f, 1.0f),
                                        0.65f, 0.38f, "brushed_metal");
            auto& r = m_registry->GetComponent<Scene::RenderableComponent>(barrel);
            ApplyNaturalisticAssetTextures(r, "Barrel_01");
            r.clearcoatFactor = 0.28f;
            r.specularFactor = 1.12f;
        }
    }

    if (scannedLanternMesh && scannedLanternMesh->gpuBuffers) {
        auto lantern = addRenderable("EffectsShowcase_ScannedLantern", scannedLanternMesh,
                                     glm::vec3(4.35f, 0.64f, -1.25f),
                                     glm::vec3(2.65f),
                                     glm::vec3(0.0f, 0.42f, 0.0f),
                                     glm::vec4(0.82f, 0.56f, 0.30f, 1.0f),
                                     1.0f, 0.22f, "brushed_metal");
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(lantern);
        ApplyNaturalisticAssetTextures(r, "Lantern_01");
        r.clearcoatFactor = 0.4f;
        r.specularFactor = 1.25f;
    }

    if (sphereMesh && sphereMesh->gpuBuffers) {
        addRenderable("EffectsShowcase_ChromeOrb", sphereMesh,
                      glm::vec3(-2.1f, 1.08f, -0.1f),
                      glm::vec3(1.25f),
                      glm::vec3(0.0f),
                      glm::vec4(0.66f, 0.68f, 0.75f, 1.0f),
                      1.0f, 0.10f, "chrome");
    }

    if (cubeMesh && cubeMesh->gpuBuffers) {
        auto glass = addRenderable("EffectsShowcase_GlassCube", cubeMesh,
                                   glm::vec3(2.2f, 1.1f, -0.2f),
                                   glm::vec3(1.1f),
                                   glm::vec3(0.0f, 0.55f, 0.0f),
                                   glm::vec4(0.58f, 0.86f, 1.0f, 1.0f),
                                   0.0f, 0.04f, "glass");
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(glass);
        r.transmissionFactor = 0.62f;
        r.ior = 1.45f;
        r.specularFactor = 1.15f;
    }

    if (torusMesh && torusMesh->gpuBuffers) {
        auto torus = addRenderable("EffectsShowcase_ClearcoatTorus", torusMesh,
                                   glm::vec3(0.0f, 1.05f, 1.7f),
                                   glm::vec3(0.95f),
                                   glm::vec3(glm::half_pi<float>(), 0.0f, 0.0f),
                                   glm::vec4(0.22f, 0.15f, 0.80f, 1.0f),
                                   0.0f, 0.18f, "clearcoat");
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(torus);
        r.clearcoatFactor = 0.85f;
        r.clearcoatRoughnessFactor = 0.08f;
        r.specularColorFactor = glm::vec3(0.75f, 0.85f, 1.0f);
    }

    if (cylinderMesh && cylinderMesh->gpuBuffers) {
        for (int i = 0; i < 5; ++i) {
            const float x = -5.4f + static_cast<float>(i) * 2.7f;
            addRenderable(("EffectsShowcase_LightColumn_" + std::to_string(i)).c_str(),
                          cylinderMesh,
                          glm::vec3(x, 1.2f, 2.6f),
                          glm::vec3(0.75f, 1.0f, 0.75f),
                          glm::vec3(0.0f),
                          glm::vec4(0.13f, 0.15f, 0.18f, 1.0f),
                          0.0f, 0.44f, "brushed_metal");
        }
    }

    if (quadMesh && quadMesh->gpuBuffers) {
        const struct NeonPanel {
            const char* tag;
            glm::vec3 position;
            glm::vec3 scale;
            glm::vec3 color;
            float strength;
        } panels[] = {
            {"EffectsShowcase_NeonPanel_Magenta", glm::vec3(-4.6f, 2.2f, 4.68f), glm::vec3(2.0f, 0.52f, 1.0f), glm::vec3(1.0f, 0.18f, 0.78f), 6.0f},
            {"EffectsShowcase_NeonPanel_Cyan", glm::vec3(0.0f, 2.9f, 4.67f), glm::vec3(2.4f, 0.42f, 1.0f), glm::vec3(0.12f, 0.75f, 1.0f), 5.4f},
            {"EffectsShowcase_NeonPanel_Amber", glm::vec3(4.5f, 2.1f, 4.66f), glm::vec3(1.8f, 0.52f, 1.0f), glm::vec3(1.0f, 0.55f, 0.12f), 5.0f}
        };

        for (const auto& panel : panels) {
            auto e = addRenderable(panel.tag, quadMesh,
                                   panel.position,
                                   panel.scale,
                                   glm::vec3(0.0f),
                                   glm::vec4(panel.color, 1.0f),
                                   0.0f, 0.25f, "emissive_panel");
            auto& r = m_registry->GetComponent<Scene::RenderableComponent>(e);
            r.emissiveColor = panel.color;
            r.emissiveStrength = panel.strength;
            r.doubleSided = true;
        }
    }

    auto addPointLight = [&](const char* tag,
                             const glm::vec3& position,
                             const glm::vec3& color,
                             float intensity,
                             float range) {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, tag);
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = position;
        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Point;
        l.color = color;
        l.intensity = intensity;
        l.range = range;
        l.castsShadows = false;
    };

    addPointLight("EffectsShowcase_MagentaGlow", glm::vec3(-4.6f, 2.4f, 3.2f), glm::vec3(1.0f, 0.22f, 0.76f), 5.4f, 8.0f);
    addPointLight("EffectsShowcase_CyanGlow", glm::vec3(0.0f, 3.0f, 2.7f), glm::vec3(0.16f, 0.72f, 1.0f), 4.8f, 8.0f);
    addPointLight("EffectsShowcase_AmberGlow", glm::vec3(4.6f, 2.3f, 3.1f), glm::vec3(1.0f, 0.54f, 0.14f), 4.6f, 7.0f);

    AddParticleEffect(*m_registry, "EffectsShowcase_FireEmitter", "fire", glm::vec3(-2.0f, 1.32f, -0.85f));
    AddParticleEffect(*m_registry, "EffectsShowcase_MoteEmitter", "smoke", glm::vec3(1.2f, 1.65f, 0.2f));
    AddParticleEffect(*m_registry, "EffectsShowcase_DustEmitter", "dust", glm::vec3(-3.4f, 1.7f, 0.35f));
    AddParticleEffect(*m_registry, "EffectsShowcase_SparkEmitter", "sparks", glm::vec3(-1.25f, 1.45f, -0.55f));
    AddParticleEffect(*m_registry, "EffectsShowcase_EmberEmitter", "embers", glm::vec3(-0.55f, 1.50f, -0.35f));
    AddParticleEffect(*m_registry, "EffectsShowcase_MistEmitter", "mist", glm::vec3(2.6f, 1.42f, -0.45f));
    AddParticleEffect(*m_registry, "EffectsShowcase_RainEmitter", "rain", glm::vec3(4.0f, 3.25f, 0.2f));
    AddParticleEffect(*m_registry, "EffectsShowcase_SnowEmitter", "snow", glm::vec3(3.2f, 3.1f, 0.95f));
}

void Engine::BuildRTShowcaseScene() {
    spdlog::info("Building hero scene: RT Showcase Gallery");

    auto* renderer = m_renderer.get();
    const bool conservative = (m_qualityMode != EngineConfig::QualityMode::Default);
    const auto sceneProfile = Graphics::BuildGalleryCinematicProfile(conservative);

    if (renderer) {
        Graphics::ApplyRTShowcaseSceneControls(*renderer, conservative);
    }

    // Shared meshes
    auto floorPlane   = Utils::MeshGenerator::CreatePlane(20.0f, 6.0f);
    auto hubFloor     = Utils::MeshGenerator::CreatePlane(16.0f, 12.0f);
    auto wallPlane    = Utils::MeshGenerator::CreatePlane(6.0f, 4.0f);
    auto tallWall     = Utils::MeshGenerator::CreatePlane(8.0f, 12.0f);
    auto poolWaterPlane = Utils::MeshGenerator::CreatePlane(5.7f, 5.7f);
    auto quadPanel    = Utils::MeshGenerator::CreateQuad(2.0f, 2.0f);
    auto sphereMesh   = Utils::MeshGenerator::CreateSphere(0.5f, 32);
    auto smallSphere  = Utils::MeshGenerator::CreateSphere(0.25f, 24);
    auto cubeMesh     = Utils::MeshGenerator::CreateCube();
    auto cylinderMesh = Utils::MeshGenerator::CreateCylinder(0.3f, 1.8f, 32);
    auto tallCylinder = Utils::MeshGenerator::CreateCylinder(0.2f, 3.0f, 24);
    auto torusMesh    = Utils::MeshGenerator::CreateTorus(0.6f, 0.18f, 32, 16);

    // Upload shared meshes once.
    if (renderer) {
        auto uploadMesh = [&](const std::shared_ptr<Scene::MeshData>& mesh, const char* label) {
            if (!mesh) return true;
            auto res = renderer->UploadMesh(mesh);
            if (res.IsErr()) {
                spdlog::warn("Failed to upload {} mesh: {}", label, res.Error());
                return false;
            }
            if (renderer->IsDeviceRemoved()) {
                spdlog::error("DX12 device was removed while uploading {} mesh; aborting RT showcase geometry.", label);
                return false;
            }
            return true;
        };

        if (!uploadMesh(floorPlane,   "RTShowcase floor") ||
            !uploadMesh(hubFloor,     "RTShowcase hub floor") ||
            !uploadMesh(wallPlane,    "RTShowcase wall") ||
            !uploadMesh(tallWall,     "RTShowcase tall wall") ||
            !uploadMesh(poolWaterPlane, "RTShowcase pool water") ||
            !uploadMesh(quadPanel,    "RTShowcase quad panel") ||
            !uploadMesh(sphereMesh,   "RTShowcase sphere") ||
            !uploadMesh(smallSphere,  "RTShowcase small sphere") ||
            !uploadMesh(cubeMesh,     "RTShowcase cube") ||
            !uploadMesh(cylinderMesh, "RTShowcase cylinder") ||
            !uploadMesh(tallCylinder, "RTShowcase tall cylinder") ||
            !uploadMesh(torusMesh,    "RTShowcase torus")) {
            return;
        }
    }

    // Camera positioned as a gallery hero shot so the default validation
    // frame exercises representative RT materials instead of mostly skybox.
    {
        entt::entity camEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(camEntity, "MainCamera");
        auto& t = m_registry->AddComponent<TransformComponent>(camEntity);
        t.position = glm::vec3(-14.0f, 2.05f, -6.8f);
        glm::vec3 target(-14.0f, 1.05f, 0.25f);
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        t.rotation = glm::quatLookAtLH(glm::normalize(target - t.position), up);

        auto& cam = m_registry->AddComponent<Scene::CameraComponent>(camEntity);
        cam.fov = 56.0f;
        ConfigureShowcaseCameraClip(cam, 180.0f);
        cam.isActive = true;
        m_activeCameraEntity = camEntity;
    }

    // --------------------
    // Zone A: Reflective gallery (x < 0)
    // --------------------
    const float galleryX = -14.0f;
    const bool overbrightReflectionStress = [] {
        const char* value = std::getenv("CORTEX_RT_REFLECTION_OVERBRIGHT_STRESS");
        return value && value[0] != '\0' && value[0] != '0';
    }();
    const bool stableLargePlanes = [] {
        const char* value = std::getenv("CORTEX_RT_SHOWCASE_NOISY_LARGE_PLANES");
        return !(value && value[0] != '\0' && value[0] != '0');
    }();

    if (floorPlane && floorPlane->gpuBuffers) {
        // Floor
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_Floor");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(galleryX, 0.0f, 0.0f);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = floorPlane;
        r.albedoColor = glm::vec4(0.70f, 0.62f, 0.52f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.92f;
        r.ao = 1.0f;
        // These large bright grazing planes dominate the interactive default
        // view. Keep texture identity, but reduce high-frequency normal
        // response so mouse-look does not read as material flicker.
        r.normalScale = 0.06f;
        r.specularFactor = 0.15f;
        r.presetName = "wood_floor";
        r.doubleSided = true;
        // Phase 2: RT showcase floor uses pre-compressed BC7/BC5 textures when
        // available. The loader will fall back to placeholders if these DDS
        // assets are missing.
        if (!stableLargePlanes) {
            r.textures.albedoPath = "assets/textures/rtshowcase/rt_gallery_floor_albedo.dds";
            r.textures.normalPath = "assets/textures/rtshowcase/rt_gallery_floor_normal_bc5.dds";
        }
    }

    if (floorPlane && floorPlane->gpuBuffers) {
        // Ceiling
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_Ceiling");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(galleryX, 4.0f, 0.0f);
        t.rotation = glm::quat(glm::vec3(glm::pi<float>(), 0.0f, 0.0f));

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = floorPlane;
        r.albedoColor = glm::vec4(0.92f, 0.92f, 0.96f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.6f;
        r.ao = 1.0f;
        r.presetName = "backdrop";
        r.doubleSided = true;
    }

    if (floorPlane && floorPlane->gpuBuffers) {
        // Rear wall closes the validation view so the RT showcase reads as an
        // authored gallery instead of an open HDRI probe.
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_RearWall");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(galleryX, 2.0f, 3.0f);
        t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), 0.0f, 0.0f));

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = floorPlane;
        r.albedoColor = glm::vec4(0.68f, 0.64f, 0.58f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.82f;
        r.ao = 1.0f;
        r.presetName = "backdrop";
        r.doubleSided = true;
    }

    if (wallPlane && wallPlane->gpuBuffers) {
        // Left wall (brick)
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_LeftWall");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(galleryX - 10.0f, 2.0f, 0.0f);
        t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), glm::half_pi<float>(), 0.0f));

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = wallPlane;
        r.albedoColor = glm::vec4(0.62f, 0.60f, 0.58f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.85f;
        r.ao = 1.0f;
        r.normalScale = 0.10f;
        r.presetName = "brick";
        r.doubleSided = true;
        if (!stableLargePlanes) {
            r.textures.albedoPath = "assets/textures/rtshowcase/rt_gallery_leftwall_albedo.dds";
            r.textures.normalPath = "assets/textures/rtshowcase/rt_gallery_leftwall_normal_bc5.dds";
        }

        // Mirror panels on the left wall
        if (quadPanel && quadPanel->gpuBuffers) {
            entt::entity m1 = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(m1, "RTGallery_MirrorPanel1");
            auto& mt1 = m_registry->AddComponent<TransformComponent>(m1);
            mt1.position = glm::vec3(galleryX - 9.8f, 1.2f, -1.5f);
            mt1.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), glm::half_pi<float>(), 0.0f));
            mt1.scale = glm::vec3(2.2f, 1.8f, 1.0f);

            auto& mr1 = m_registry->AddComponent<Scene::RenderableComponent>(m1);
            mr1.mesh = quadPanel;
            mr1.albedoColor = glm::vec4(1.0f);
            mr1.metallic = 1.0f;
            mr1.roughness = 0.02f;
            mr1.ao = 1.0f;
            mr1.presetName = "mirror";

            entt::entity m2 = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(m2, "RTGallery_MirrorPanel2");
            auto& mt2 = m_registry->AddComponent<TransformComponent>(m2);
            mt2.position = glm::vec3(galleryX - 9.8f, 2.6f, 1.5f);
            mt2.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), glm::half_pi<float>(), 0.0f));
            mt2.scale = glm::vec3(2.2f, 1.8f, 1.0f);

            auto& mr2 = m_registry->AddComponent<Scene::RenderableComponent>(m2);
            mr2.mesh = quadPanel;
            mr2.albedoColor = glm::vec4(1.0f);
            mr2.metallic = 1.0f;
            mr2.roughness = 0.03f;
            mr2.ao = 1.0f;
            mr2.presetName = "mirror";
        }
    }

    if (wallPlane && wallPlane->gpuBuffers) {
        // Right wall (neutral)
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_RightWall");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(galleryX + 10.0f, 2.0f, 0.0f);
        t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), -glm::half_pi<float>(), 0.0f));

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = wallPlane;
        r.albedoColor = glm::vec4(0.92f, 0.91f, 0.88f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.7f;
        r.ao = 1.0f;
        r.normalScale = 0.10f;
        r.presetName = "backdrop";
        r.doubleSided = true;
        if (!stableLargePlanes) {
            r.textures.albedoPath = "assets/textures/rtshowcase/rt_gallery_rightwall_albedo.dds";
            r.textures.normalPath = "assets/textures/rtshowcase/rt_gallery_rightwall_normal_bc5.dds";
        }
        if (overbrightReflectionStress) {
            r.albedoColor = glm::vec4(1.0f, 0.76f, 0.42f, 1.0f);
            r.roughness = 0.32f;
            r.emissiveColor = glm::vec3(1.0f, 0.74f, 0.36f);
            r.emissiveStrength = 32.0f;
            r.presetName = "emissive_panel";
            r.textures.albedoPath.clear();
            r.textures.normalPath.clear();
        }
    }

    if (overbrightReflectionStress && quadPanel && quadPanel->gpuBuffers) {
        spdlog::info("RTShowcase overbright reflection stress enabled");

        entt::entity hotPanel = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(hotPanel, "RTGallery_OverbrightClampPanel");
        auto& hotT = m_registry->AddComponent<TransformComponent>(hotPanel);
        hotT.position = glm::vec3(galleryX + 9.65f, 2.05f, -1.5f);
        hotT.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), -glm::half_pi<float>(), 0.0f));
        hotT.scale = glm::vec3(1.3f, 1.1f, 1.0f);

        auto& hotR = m_registry->AddComponent<Scene::RenderableComponent>(hotPanel);
        hotR.mesh = quadPanel;
        hotR.albedoColor = glm::vec4(1.0f, 0.78f, 0.42f, 1.0f);
        hotR.metallic = 0.0f;
        hotR.roughness = 0.18f;
        hotR.ao = 1.0f;
        hotR.emissiveColor = glm::vec3(1.0f, 0.72f, 0.36f);
        hotR.emissiveStrength = 48.0f;
        hotR.presetName = "emissive_panel";
        hotR.doubleSided = true;

        entt::entity hotLight = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(hotLight, "RTGallery_OverbrightClampLight");
        auto& lightT = m_registry->AddComponent<Scene::TransformComponent>(hotLight);
        lightT.position = glm::vec3(galleryX + 8.9f, 2.25f, -1.35f);
        lightT.rotation = glm::quatLookAtLH(glm::normalize(glm::vec3(-1.0f, -0.15f, 0.05f)),
                                            glm::vec3(0.0f, 1.0f, 0.0f));

        auto& light = m_registry->AddComponent<Scene::LightComponent>(hotLight);
        light.type = Scene::LightType::Spot;
        light.color = glm::vec3(1.0f, 0.78f, 0.48f);
        light.intensity = 18.0f;
        light.range = 18.0f;
        light.innerConeDegrees = 18.0f;
        light.outerConeDegrees = 36.0f;
        light.castsShadows = false;
    }

    // Profile-owned local reflection probes exercise VB deferred local IBL
    // selection. They intentionally overlap the hero gallery so debug view 42
    // shows the probe/global blend gradient instead of a binary on/off mask.
    const size_t profileReflectionProbes = AddSceneProfileReflectionProbes(*m_registry, sceneProfile);

    // Row of primitives down the gallery
    if (sphereMesh && sphereMesh->gpuBuffers && cubeMesh && cubeMesh->gpuBuffers && torusMesh && torusMesh->gpuBuffers) {
        const float baseZ = -1.0f;
        // Chrome sphere
        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_SphereChrome");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(galleryX - 6.0f, 0.6f, baseZ);

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = sphereMesh;
            r.albedoColor = glm::vec4(0.62f, 0.62f, 0.66f, 1.0f);
            r.metallic = 1.0f;
            r.roughness = 0.18f;
            r.ao = 1.0f;
            r.presetName = "chrome";
        }
        // Brushed metal cylinder
        if (cylinderMesh && cylinderMesh->gpuBuffers) {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_CylinderBrushed");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(galleryX - 2.0f, 0.9f, baseZ);

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = cylinderMesh;
            r.albedoColor = glm::vec4(0.7f, 0.7f, 0.75f, 1.0f);
            r.metallic = 1.0f;
            r.roughness = 0.25f;
            r.ao = 1.0f;
            r.presetName = "brushed_metal";
            r.textures.albedoPath = "assets/textures/rtshowcase/rt_gallery_cylinder_brushed_albedo.dds";
            r.textures.normalPath = "assets/textures/rtshowcase/rt_gallery_cylinder_brushed_normal_bc5.dds";
        }
        // Plastic cube
        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_CubePlastic");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(galleryX + 2.0f, 0.5f, baseZ);
            t.scale = glm::vec3(1.2f, 1.2f, 1.2f);

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = cubeMesh;
            r.albedoColor = glm::vec4(0.9f, 0.15f, 0.2f, 1.0f);
            r.metallic = 0.0f;
            r.roughness = 0.35f;
            r.ao = 1.0f;
            r.presetName = "plastic";
            r.textures.albedoPath = "assets/textures/rtshowcase/rt_gallery_cube_plastic_albedo.dds";
            r.textures.normalPath = "assets/textures/rtshowcase/rt_gallery_cube_plastic_normal_bc5.dds";
        }
        // Anisotropic torus
        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "RTGallery_TorusAniso");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(galleryX + 6.0f, 0.6f, baseZ);

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = torusMesh;
            r.albedoColor = glm::vec4(0.9f, 0.85f, 0.8f, 1.0f);
            r.metallic = 1.0f;
            r.roughness = 0.18f;
            r.ao = 1.0f;
            r.presetName = "brushed_metal";
        }
    }

    // Hero dragon + chrome sphere on plinths reused from the sample model.
    std::shared_ptr<Scene::MeshData> dragonMesh;
    auto dragonResult = Utils::LoadSampleModelMesh("DragonAttenuation");
    if (dragonResult.IsOk()) {
        dragonMesh = dragonResult.Value();
        if (renderer) {
            // Keep the hero asset present in the showcase. Renderer-side
            // upload throttling, BLAS accounting and internal render scaling
            // are responsible for staying within budget; the scene should not
            // remove content just because RT is enabled.
            auto upload = renderer->UploadMesh(dragonMesh);
            if (upload.IsErr()) {
                spdlog::warn("Failed to upload RTShowcase dragon mesh: {}", upload.Error());
                dragonMesh.reset();
            }
        }
    } else {
        spdlog::warn("RTShowcase: failed to load DragonAttenuation: {}", dragonResult.Error());
    }

    if (dragonMesh && cubeMesh && cubeMesh->gpuBuffers) {
        // Dragon plinth
        entt::entity pe = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(pe, "RTGallery_DragonPlinth");
        auto& pt = m_registry->AddComponent<TransformComponent>(pe);
        pt.position = glm::vec3(galleryX, 0.4f, 1.2f);
        pt.scale = glm::vec3(1.6f, 0.8f, 1.6f);

        auto& pr = m_registry->AddComponent<Scene::RenderableComponent>(pe);
        pr.mesh = cubeMesh;
        pr.albedoColor = glm::vec4(0.8f, 0.8f, 0.82f, 1.0f);
        pr.metallic = 0.0f;
        pr.roughness = 0.6f;
        pr.ao = 1.0f;
        pr.presetName = "backdrop";

        // Dragon
        entt::entity de = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(de, "RTGallery_MetalDragon");
        auto& dt = m_registry->AddComponent<TransformComponent>(de);
        dt.position = glm::vec3(galleryX, 0.82f, 1.2f);
        dt.scale = glm::vec3(0.16f);
        dt.rotation = glm::quat(glm::vec3(glm::radians(90.0f), glm::radians(180.0f), 0.0f));

        auto& dr = m_registry->AddComponent<Scene::RenderableComponent>(de);
        dr.mesh = dragonMesh;
        dr.albedoColor = glm::vec4(0.48f, 0.47f, 0.43f, 1.0f);
        dr.metallic = 1.0f;
        dr.roughness = 0.36f;
        dr.ao = 1.0f;
        dr.presetName = "brushed_metal";

        if (quadPanel && quadPanel->gpuBuffers) {
            const struct DragonBackdropPanel {
                const char* tag;
                glm::vec3 position;
                glm::vec3 scale;
                glm::vec3 color;
                float strength;
            } panels[] = {
                {"RTGallery_DragonReflectionPanel_Warm", glm::vec3(galleryX - 1.75f, 1.65f, 2.48f), glm::vec3(1.6f, 0.45f, 1.0f), glm::vec3(1.0f, 0.58f, 0.24f), 2.4f},
                {"RTGallery_DragonReflectionPanel_Cool", glm::vec3(galleryX + 1.75f, 1.78f, 2.46f), glm::vec3(1.6f, 0.42f, 1.0f), glm::vec3(0.35f, 0.58f, 1.0f), 1.8f}
            };
            for (const auto& panel : panels) {
                entt::entity panelEntity = m_registry->CreateEntity();
                m_registry->AddComponent<Scene::TagComponent>(panelEntity, panel.tag);
                auto& panelT = m_registry->AddComponent<TransformComponent>(panelEntity);
                panelT.position = panel.position;
                panelT.scale = panel.scale;

                auto& panelR = m_registry->AddComponent<Scene::RenderableComponent>(panelEntity);
                panelR.mesh = quadPanel;
                panelR.albedoColor = glm::vec4(panel.color, 1.0f);
                panelR.metallic = 0.0f;
                panelR.roughness = 0.22f;
                panelR.ao = 1.0f;
                panelR.presetName = "emissive_panel";
                panelR.emissiveColor = panel.color;
                panelR.emissiveStrength = panel.strength;
                panelR.doubleSided = true;
            }
        }
    }

    if (smallSphere && smallSphere->gpuBuffers && cubeMesh && cubeMesh->gpuBuffers) {
        // Chrome sphere on a small plinth.
        entt::entity pe = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(pe, "RTGallery_SpherePlinth");
        auto& pt = m_registry->AddComponent<TransformComponent>(pe);
        pt.position = glm::vec3(galleryX + 4.0f, 0.3f, 1.3f);
        pt.scale = glm::vec3(0.8f, 0.4f, 0.8f);

        auto& pr = m_registry->AddComponent<Scene::RenderableComponent>(pe);
        pr.mesh = cubeMesh;
        pr.albedoColor = glm::vec4(0.8f, 0.8f, 0.82f, 1.0f);
        pr.metallic = 0.0f;
        pr.roughness = 0.6f;
        pr.ao = 1.0f;
        pr.presetName = "backdrop";

        entt::entity se = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(se, "RTGallery_SmallChromeSphere");
        auto& st = m_registry->AddComponent<TransformComponent>(se);
        st.position = glm::vec3(galleryX + 4.0f, 0.8f, 1.3f);

        auto& sr = m_registry->AddComponent<Scene::RenderableComponent>(se);
        sr.mesh = smallSphere;
        sr.albedoColor = glm::vec4(0.68f, 0.68f, 0.72f, 1.0f);
        sr.metallic = 1.0f;
        sr.roughness = 0.18f;
        sr.ao = 1.0f;
        sr.presetName = "chrome";
    }

    if (cubeMesh && cubeMesh->gpuBuffers) {
        auto addGalleryDetailBlock = [&](const char* tag,
                                         const glm::vec3& position,
                                         const glm::vec3& scale,
                                         const glm::vec4& color,
                                         float metallic,
                                         float roughness,
                                         const char* preset) {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, tag);
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = position;
            t.scale = scale;

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = cubeMesh;
            r.albedoColor = color;
            r.metallic = metallic;
            r.roughness = roughness;
            r.ao = 1.0f;
            r.presetName = preset;
            r.doubleSided = false;
        };

        const glm::vec4 brushedRail(0.50f, 0.52f, 0.56f, 1.0f);
        const glm::vec4 darkReveal(0.11f, 0.105f, 0.10f, 1.0f);
        const glm::vec4 warmWood(0.50f, 0.33f, 0.18f, 1.0f);
        const glm::vec4 tileInsert(0.56f, 0.66f, 0.74f, 1.0f);

        addGalleryDetailBlock("RTGallery_Detail_BackBaseRail",
                              glm::vec3(galleryX, 0.10f, 2.92f),
                              glm::vec3(8.8f, 0.08f, 0.05f),
                              brushedRail,
                              1.0f,
                              0.32f,
                              "brushed_metal");
        addGalleryDetailBlock("RTGallery_Detail_BackTopRail",
                              glm::vec3(galleryX, 3.46f, 2.90f),
                              glm::vec3(8.6f, 0.055f, 0.045f),
                              brushedRail,
                              1.0f,
                              0.36f,
                              "brushed_metal");
        addGalleryDetailBlock("RTGallery_Detail_BackShadowReveal",
                              glm::vec3(galleryX, 2.24f, 2.88f),
                              glm::vec3(6.8f, 0.035f, 0.035f),
                              darkReveal,
                              0.0f,
                              0.68f,
                              "rubber");

        const float floorZs[] = {-5.20f, -4.20f, -3.20f, -2.20f, -1.20f, -0.20f, 0.80f, 1.80f};
        for (int i = 0; i < 8; ++i) {
            addGalleryDetailBlock(("RTGallery_Detail_FloorCrossInlay_" + std::to_string(i)).c_str(),
                                  glm::vec3(galleryX, 0.024f, floorZs[i]),
                                  glm::vec3(8.2f, 0.018f, 0.026f),
                                  (i % 2 == 0) ? brushedRail : darkReveal,
                                  (i % 2 == 0) ? 1.0f : 0.0f,
                                  (i % 2 == 0) ? 0.38f : 0.72f,
                                  (i % 2 == 0) ? "brushed_metal" : "rubber");
        }

        const float floorXs[] = {-18.6f, -16.8f, -15.0f, -13.2f, -11.4f, -9.6f};
        for (int i = 0; i < 6; ++i) {
            addGalleryDetailBlock(("RTGallery_Detail_FloorLongInlay_" + std::to_string(i)).c_str(),
                                  glm::vec3(floorXs[i], 0.026f, -1.70f),
                                  glm::vec3(0.022f, 0.018f, 6.6f),
                                  (i % 2 == 0) ? darkReveal : tileInsert,
                                  0.0f,
                                  (i % 2 == 0) ? 0.70f : 0.42f,
                                  (i % 2 == 0) ? "rubber" : "ceramic_tile");
        }

        const struct GalleryBevel {
            const char* tag;
            glm::vec3 position;
            glm::vec3 scale;
        } bevels[] = {
            {"RTGallery_Detail_DragonPlinthFrontBevel", glm::vec3(galleryX, 0.82f, 0.38f), glm::vec3(1.72f, 0.045f, 0.045f)},
            {"RTGallery_Detail_DragonPlinthBackBevel", glm::vec3(galleryX, 0.82f, 2.02f), glm::vec3(1.72f, 0.045f, 0.045f)},
            {"RTGallery_Detail_DragonPlinthLeftBevel", glm::vec3(galleryX - 0.82f, 0.82f, 1.20f), glm::vec3(0.045f, 0.045f, 1.72f)},
            {"RTGallery_Detail_DragonPlinthRightBevel", glm::vec3(galleryX + 0.82f, 0.82f, 1.20f), glm::vec3(0.045f, 0.045f, 1.72f)},
            {"RTGallery_Detail_SpherePlinthFrontBevel", glm::vec3(galleryX + 4.0f, 0.52f, 0.88f), glm::vec3(0.92f, 0.035f, 0.035f)},
            {"RTGallery_Detail_SpherePlinthBackBevel", glm::vec3(galleryX + 4.0f, 0.52f, 1.72f), glm::vec3(0.92f, 0.035f, 0.035f)}
        };
        for (const auto& bevel : bevels) {
            addGalleryDetailBlock(bevel.tag,
                                  bevel.position,
                                  bevel.scale,
                                  darkReveal,
                                  0.0f,
                                  0.64f,
                                  "rubber");
        }

        if (quadPanel && quadPanel->gpuBuffers) {
            const struct GalleryPanel {
                const char* tag;
                glm::vec3 position;
                glm::vec3 scale;
                glm::vec4 color;
                const char* preset;
                glm::vec3 emissive;
                float emissiveStrength;
            } panels[] = {
                {"RTGallery_Detail_RearPanel_WarmStudy", glm::vec3(galleryX - 3.35f, 1.78f, 2.86f), glm::vec3(0.78f, 0.46f, 1.0f), glm::vec4(0.82f, 0.50f, 0.22f, 1.0f), "painted_wall", glm::vec3(0.0f), 0.0f},
                {"RTGallery_Detail_RearPanel_CoolStudy", glm::vec3(galleryX + 3.35f, 1.82f, 2.86f), glm::vec3(0.78f, 0.46f, 1.0f), glm::vec4(0.24f, 0.48f, 0.82f, 1.0f), "painted_wall", glm::vec3(0.0f), 0.0f},
                {"RTGallery_Detail_RearSignalPanel", glm::vec3(galleryX, 2.88f, 2.85f), glm::vec3(1.35f, 0.18f, 1.0f), glm::vec4(0.20f, 0.62f, 0.90f, 1.0f), "screen_panel", glm::vec3(0.06f, 0.22f, 0.38f), 0.72f}
            };
            for (const auto& panel : panels) {
                entt::entity panelEntity = m_registry->CreateEntity();
                m_registry->AddComponent<Scene::TagComponent>(panelEntity, panel.tag);
                auto& panelT = m_registry->AddComponent<TransformComponent>(panelEntity);
                panelT.position = panel.position;
                panelT.scale = panel.scale;

                auto& panelR = m_registry->AddComponent<Scene::RenderableComponent>(panelEntity);
                panelR.mesh = quadPanel;
                panelR.albedoColor = panel.color;
                panelR.metallic = 0.0f;
                panelR.roughness = 0.46f;
                panelR.ao = 1.0f;
                panelR.presetName = panel.preset;
                panelR.doubleSided = true;
                panelR.emissiveColor = panel.emissive;
                panelR.emissiveStrength = panel.emissiveStrength;
            }
        }
    }

    const size_t profileLights =
        AddSceneProfileLights(*m_registry, sceneProfile, glm::vec3(galleryX, 1.05f, 0.25f));
    spdlog::info("RT Showcase profile assets: profile_lights={} reflection_probes={}",
                 profileLights,
                 profileReflectionProbes);

    // Dragon fire emitter near the gallery dragon's mouth. This uses the
    // shared CPU-driven particle system and renders as small emissive
    // billboards that are bright enough to feed bloom and RT reflections.
    AddParticleEffect(*m_registry, "RTGallery_FireEmitter", "fire", glm::vec3(galleryX + 0.4f, 1.4f, 2.0f));

    // --------------------
    // Zone B: Liquid courtyard (center)
    // --------------------
    const float courtyardZ = -5.5f;

    if (hubFloor && hubFloor->gpuBuffers) {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Courtyard_Floor");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(0.0f, 0.0f, courtyardZ);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = hubFloor;
        r.albedoColor = glm::vec4(0.48f, 0.48f, 0.5f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.8f;
        r.ao = 1.0f;
        r.presetName = "brick";
        r.doubleSided = true;
    }

    if (poolWaterPlane && poolWaterPlane->gpuBuffers) {
        // The courtyard pool is an explicit contained assembly. The old setup
        // used a full 8x8 white plane and another full 8x8 transparent water
        // plane only 0.01 units apart, which left surface ownership ambiguous
        // under grazing mouse-look in the visibility-buffer path.
        if (cubeMesh && cubeMesh->gpuBuffers) {
            struct RimStrip {
                const char* tag;
                glm::vec3 position;
                glm::vec3 scale;
            };
            const RimStrip rimStrips[] = {
                {"Courtyard_PoolRim_Front", glm::vec3(0.0f, 0.035f, courtyardZ - 3.425f), glm::vec3(8.0f, 0.07f, 1.15f)},
                {"Courtyard_PoolRim_Back",  glm::vec3(0.0f, 0.035f, courtyardZ + 3.425f), glm::vec3(8.0f, 0.07f, 1.15f)},
                {"Courtyard_PoolRim_Left",  glm::vec3(-3.425f, 0.035f, courtyardZ), glm::vec3(1.15f, 0.07f, 5.7f)},
                {"Courtyard_PoolRim_Right", glm::vec3( 3.425f, 0.035f, courtyardZ), glm::vec3(1.15f, 0.07f, 5.7f)},
            };

            for (const auto& strip : rimStrips) {
                entt::entity rim = m_registry->CreateEntity();
                m_registry->AddComponent<Scene::TagComponent>(rim, strip.tag);
                auto& rt = m_registry->AddComponent<Scene::TransformComponent>(rim);
                rt.position = strip.position;
                rt.scale = strip.scale;

                auto& rr = m_registry->AddComponent<Scene::RenderableComponent>(rim);
                rr.mesh = cubeMesh;
                rr.albedoColor = glm::vec4(0.9f, 0.9f, 0.92f, 1.0f);
                rr.metallic = 0.0f;
                rr.roughness = 0.75f;
                rr.ao = 1.0f;
                rr.presetName = "concrete";
            }
        }

        // Water surface: smaller than the rim footprint and below the rim top,
        // so it can reflect the IBL without competing for the same broad pixels.
        entt::entity water = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(water, "Courtyard_WaterSurface");
        auto& wt = m_registry->AddComponent<Scene::TransformComponent>(water);
        wt.position = glm::vec3(0.0f, 0.052f, courtyardZ);

        auto& wr = m_registry->AddComponent<Scene::RenderableComponent>(water);
        wr.mesh = poolWaterPlane;
        wr.albedoColor = glm::vec4(0.05f, 0.18f, 0.24f, 0.62f);
        wr.metallic = 0.0f;
        wr.roughness = 0.06f;
        wr.ao = 1.0f;
        wr.presetName = "water";
        m_registry->AddComponent<Scene::WaterSurfaceComponent>(water, Scene::WaterSurfaceComponent{0.0f});
    }

    // Columns / arches around the pool
    if (tallCylinder && tallCylinder->gpuBuffers) {
        const float colRadius = 4.5f;
        for (int i = 0; i < 4; ++i) {
            float angle = glm::half_pi<float>() * static_cast<float>(i);
            float x = std::cos(angle) * colRadius;
            float z = courtyardZ + std::sin(angle) * colRadius;

            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "Courtyard_Column");
            auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
            t.position = glm::vec3(x, 1.5f, z);

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = tallCylinder;
            r.albedoColor = glm::vec4(0.82f, 0.82f, 0.86f, 1.0f);
            r.metallic = 0.0f;
            r.roughness = 0.4f;
            r.ao = 1.0f;
            r.presetName = "concrete";
        }
    }

    // Glass box over the pool and an emissive panel
    if (quadPanel && quadPanel->gpuBuffers) {
        // Glass roof
        entt::entity roof = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(roof, "Courtyard_GlassRoof");
        auto& rt = m_registry->AddComponent<Scene::TransformComponent>(roof);
        rt.position = glm::vec3(0.0f, 2.5f, courtyardZ);
        rt.rotation = glm::quat(glm::vec3(glm::half_pi<float>(), 0.0f, 0.0f));
        rt.scale = glm::vec3(6.0f, 6.0f, 1.0f);

        auto& rr = m_registry->AddComponent<Scene::RenderableComponent>(roof);
        rr.mesh = quadPanel;
        rr.albedoColor = glm::vec4(0.7f, 0.85f, 1.0f, 1.0f);
        rr.metallic = 0.0f;
        rr.roughness = 0.05f;
        rr.ao = 1.0f;
        rr.presetName = "glass_panel";

        // Suspended emissive panel
        entt::entity ep = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(ep, "Courtyard_EmissivePanel");
        auto& et = m_registry->AddComponent<Scene::TransformComponent>(ep);
        et.position = glm::vec3(0.0f, 2.2f, courtyardZ - 2.5f);
        et.rotation = glm::quat(glm::vec3(0.0f, 0.0f, 0.0f));
        et.scale = glm::vec3(3.0f, 1.0f, 1.0f);

        auto& er = m_registry->AddComponent<Scene::RenderableComponent>(ep);
        er.mesh = quadPanel;
        er.albedoColor = glm::vec4(1.0f, 0.86f, 0.58f, 1.0f);
        er.metallic = 0.0f;
        er.roughness = 0.2f;
        er.ao = 1.0f;
        er.emissiveColor = glm::vec3(1.0f, 0.86f, 0.58f);
        er.emissiveStrength = 6.0f;
        er.presetName = "emissive_panel";
    }

    // Courtyard lights
    {
        // Underwater blue fill
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Courtyard_UnderwaterLight");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(0.0f, -0.4f, courtyardZ);

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Point;
        l.color = glm::vec3(0.2f, 0.4f, 0.9f);
        l.intensity = 1.8f;
        l.range = 10.0f;
        l.castsShadows = false;
    }

    // --------------------
    // Zone C: Volumetric atrium (x > 0)
    // --------------------
    const float atriumX = 16.0f;
    const float atriumHeight = 9.0f;

    if (floorPlane && floorPlane->gpuBuffers) {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Atrium_Floor");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(atriumX, 0.0f, 0.0f);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = floorPlane;
        r.albedoColor = glm::vec4(0.36f, 0.36f, 0.39f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.85f;
        r.ao = 1.0f;
        r.presetName = "brick";
    }

    if (tallWall && tallWall->gpuBuffers) {
        // Back wall
        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "Atrium_BackWall");
            auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
            t.position = glm::vec3(atriumX, atriumHeight * 0.5f, 6.0f);
            t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), 0.0f, 0.0f));

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = tallWall;
            r.albedoColor = glm::vec4(0.28f, 0.28f, 0.31f, 1.0f);
            r.metallic = 0.0f;
            r.roughness = 0.9f;
            r.ao = 1.0f;
            r.presetName = "brick";
        }

        // Side wall with slits/windows
        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "Atrium_SlitWall");
            auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
            t.position = glm::vec3(atriumX - 5.0f, atriumHeight * 0.5f, 0.0f);
            t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), glm::half_pi<float>(), 0.0f));

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = tallWall;
            r.albedoColor = glm::vec4(0.32f, 0.32f, 0.36f, 1.0f);
            r.metallic = 0.0f;
            r.roughness = 0.85f;
            r.ao = 1.0f;
            r.presetName = "brick";
        }

        if (quadPanel && quadPanel->gpuBuffers) {
            // Vertical slits/windows for god rays.
            for (int i = 0; i < 3; ++i) {
                float y = 2.0f + static_cast<float>(i) * 2.0f;
                entt::entity e = m_registry->CreateEntity();
                m_registry->AddComponent<Scene::TagComponent>(e, "Atrium_SlitWindow");
                auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
                t.position = glm::vec3(atriumX - 4.99f, y, -1.5f + i * 1.5f);
                t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), glm::half_pi<float>(), 0.0f));
                t.scale = glm::vec3(0.5f, 1.6f, 1.0f);

                auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
                r.mesh = quadPanel;
                r.albedoColor = glm::vec4(0.9f, 0.95f, 1.0f, 0.2f);
                r.metallic = 0.0f;
                r.roughness = 0.15f;
                r.ao = 1.0f;
                r.presetName = "glass_panel";
            }
        }
    }

    // Matte statues/blocks catching beams
    if (cubeMesh && cubeMesh->gpuBuffers) {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Atrium_Block");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(atriumX + 1.5f, 0.75f, -1.5f);
        t.scale = glm::vec3(1.5f, 1.5f, 1.5f);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = cubeMesh;
        r.albedoColor = glm::vec4(0.5f, 0.5f, 0.55f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.9f;
        r.ao = 1.0f;
        r.presetName = "matte";
    }

    if (torusMesh && torusMesh->gpuBuffers) {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Atrium_Torus");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(atriumX - 0.5f, 1.2f, 1.0f);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = torusMesh;
        r.albedoColor = glm::vec4(0.5f, 0.5f, 0.54f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.85f;
        r.ao = 1.0f;
        r.presetName = "matte";
    }

    // Dust / mote particle emitter near the light shafts.
    AddParticleEffect(*m_registry, "Atrium_DustEmitter", "dust", glm::vec3(atriumX - 2.5f, 2.0f, -0.5f));

    // Small highlight light in the atrium
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "Atrium_SculptureLight");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(atriumX + 2.0f, 3.0f, 0.0f);
        glm::vec3 dir(-0.4f, -1.0f, 0.1f);
        t.rotation = glm::quatLookAtLH(glm::normalize(dir), glm::vec3(0.0f, 1.0f, 0.0f));

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Spot;
        l.color = glm::vec3(1.0f, 0.95f, 0.9f);
        l.intensity = 2.6f;
        l.range = 15.0f;
        l.innerConeDegrees = 20.0f;
        l.outerConeDegrees = 35.0f;
        l.castsShadows = false;
    }
}

void Engine::BuildTemporalValidationScene() {
    spdlog::info("Building validation scene: Temporal Reprojection Lab");

    auto* renderer = m_renderer.get();
    if (renderer) {
        Graphics::ApplyTemporalValidationSceneControls(*renderer);
    }

    auto floorMesh = Utils::MeshGenerator::CreatePlane(10.0f, 8.0f);
    auto wallMesh = Utils::MeshGenerator::CreatePlane(10.0f, 4.0f);
    auto cubeMesh = Utils::MeshGenerator::CreateCube();
    auto sphereMesh = Utils::MeshGenerator::CreateSphere(0.55f, 32);
    auto torusMesh = Utils::MeshGenerator::CreateTorus(0.55f, 0.16f, 32, 16);
    auto quadMesh = Utils::MeshGenerator::CreateQuad(1.5f, 2.0f);
    auto waterMesh = Utils::MeshGenerator::CreatePlane(4.0f, 2.5f);

    if (renderer) {
        auto upload = [&](const std::shared_ptr<Scene::MeshData>& mesh, const char* label) {
            if (!mesh) {
                return false;
            }
            auto result = renderer->UploadMesh(mesh);
            if (result.IsErr()) {
                spdlog::warn("Failed to upload temporal validation {} mesh: {}", label, result.Error());
                return false;
            }
            return !renderer->IsDeviceRemoved();
        };
        if (!upload(floorMesh, "floor") ||
            !upload(wallMesh, "wall") ||
            !upload(cubeMesh, "cube") ||
            !upload(sphereMesh, "sphere") ||
            !upload(torusMesh, "torus") ||
            !upload(quadMesh, "alpha panel") ||
            !upload(waterMesh, "water")) {
            return;
        }
    }

    {
        entt::entity camEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(camEntity, "MainCamera");
        auto& t = m_registry->AddComponent<TransformComponent>(camEntity);
        t.position = glm::vec3(0.0f, 2.3f, -6.4f);
        const glm::vec3 target(0.0f, 1.0f, 0.1f);
        t.rotation = glm::quatLookAtLH(glm::normalize(target - t.position), glm::vec3(0.0f, 1.0f, 0.0f));
        auto& cam = m_registry->AddComponent<Scene::CameraComponent>(camEntity);
        cam.fov = 54.0f;
        ConfigureShowcaseCameraClip(cam, 120.0f);
        cam.isActive = true;
        m_activeCameraEntity = camEntity;
    }

    auto addRenderable = [&](const char* tag,
                             const std::shared_ptr<Scene::MeshData>& mesh,
                             const glm::vec3& position,
                             const glm::quat& rotation,
                             const glm::vec3& scale,
                             const glm::vec4& albedo,
                             float metallic,
                             float roughness) -> entt::entity {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, tag);
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = position;
        t.rotation = rotation;
        t.scale = scale;
        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = mesh;
        r.albedoColor = albedo;
        r.metallic = metallic;
        r.roughness = roughness;
        r.ao = 1.0f;
        r.presetName = tag ? tag : "";
        return e;
    };

    if (floorMesh && floorMesh->gpuBuffers) {
        auto floor = addRenderable("TemporalLab_Floor",
                                   floorMesh,
                                   glm::vec3(0.0f, 0.0f, 0.5f),
                                   glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                   glm::vec3(1.0f),
                                   glm::vec4(0.45f, 0.48f, 0.52f, 1.0f),
                                   0.0f,
                                   0.72f);
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(floor);
        r.doubleSided = true;
    }

    if (wallMesh && wallMesh->gpuBuffers) {
        auto back = addRenderable("TemporalLab_BackWall",
                                  wallMesh,
                                  glm::vec3(0.0f, 2.0f, 3.8f),
                                  glm::quat(glm::vec3(-glm::half_pi<float>(), 0.0f, 0.0f)),
                                  glm::vec3(1.0f),
                                  glm::vec4(0.78f, 0.80f, 0.84f, 1.0f),
                                  0.0f,
                                  0.58f);
        m_registry->GetComponent<Scene::RenderableComponent>(back).doubleSided = true;
    }

    if (cubeMesh && cubeMesh->gpuBuffers) {
        auto e = addRenderable("TemporalLab_RotatingChromeBlock",
                               cubeMesh,
                               glm::vec3(-1.45f, 0.9f, 0.15f),
                               glm::quat(glm::vec3(0.0f, 0.35f, 0.0f)),
                               glm::vec3(0.75f, 1.25f, 0.75f),
                               glm::vec4(0.85f, 0.88f, 0.92f, 1.0f),
                               1.0f,
                               0.18f);
        m_registry->AddComponent<Scene::RotationComponent>(
            e,
            Scene::RotationComponent{glm::normalize(glm::vec3(0.2f, 1.0f, 0.1f)), 2.2f});
    }

    if (sphereMesh && sphereMesh->gpuBuffers) {
        auto e = addRenderable("TemporalLab_RotatingRedSphere",
                               sphereMesh,
                               glm::vec3(0.0f, 0.85f, -0.05f),
                               glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                               glm::vec3(1.0f),
                               glm::vec4(0.95f, 0.12f, 0.08f, 1.0f),
                               0.0f,
                               0.36f);
        m_registry->AddComponent<Scene::RotationComponent>(
            e,
            Scene::RotationComponent{glm::normalize(glm::vec3(0.0f, 1.0f, 0.6f)), 1.7f});
    }

    if (torusMesh && torusMesh->gpuBuffers) {
        auto e = addRenderable("TemporalLab_EmissiveSpinner",
                               torusMesh,
                               glm::vec3(1.45f, 1.05f, 0.15f),
                               glm::quat(glm::vec3(glm::half_pi<float>(), 0.0f, 0.0f)),
                               glm::vec3(1.0f),
                               glm::vec4(0.10f, 0.18f, 0.95f, 1.0f),
                               0.0f,
                               0.24f);
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(e);
        r.emissiveColor = glm::vec3(0.2f, 0.45f, 1.0f);
        r.emissiveStrength = 1.8f;
        r.presetName = "emissive";
        m_registry->AddComponent<Scene::RotationComponent>(
            e,
            Scene::RotationComponent{glm::normalize(glm::vec3(1.0f, 0.2f, 0.0f)), 2.8f});
    }

    if (quadMesh && quadMesh->gpuBuffers) {
        auto e = addRenderable("TemporalLab_AlphaMaskPanel",
                               quadMesh,
                               glm::vec3(0.0f, 1.1f, 1.15f),
                               glm::quat(glm::vec3(0.0f, 0.0f, 0.0f)),
                               glm::vec3(1.0f),
                               glm::vec4(0.05f, 0.7f, 0.25f, 1.0f),
                               0.0f,
                               0.42f);
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(e);
        r.alphaMode = Scene::RenderableComponent::AlphaMode::Mask;
        r.alphaCutoff = 0.5f;
        r.doubleSided = true;
        m_registry->AddComponent<Scene::RotationComponent>(
            e,
            Scene::RotationComponent{glm::normalize(glm::vec3(0.0f, 1.0f, 0.0f)), 1.1f});
    }

    if (waterMesh && waterMesh->gpuBuffers) {
        auto e = addRenderable("TemporalLab_WaterSurface",
                               waterMesh,
                               glm::vec3(0.0f, 0.015f, -1.9f),
                               glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                               glm::vec3(1.0f),
                               glm::vec4(0.02f, 0.09f, 0.15f, 0.78f),
                               0.0f,
                               0.05f);
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(e);
        r.presetName = "water";
        r.doubleSided = true;
        m_registry->AddComponent<Scene::WaterSurfaceComponent>(e, Scene::WaterSurfaceComponent{0.0f});
    }

    auto addLight = [&](const char* tag,
                        const glm::vec3& position,
                        const glm::vec3& color,
                        float intensity,
                        float range) {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, tag);
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = position;
        auto& light = m_registry->AddComponent<Scene::LightComponent>(e);
        light.type = Scene::LightType::Point;
        light.color = color;
        light.intensity = intensity;
        light.range = range;
        light.castsShadows = false;
    };
    addLight("TemporalLab_KeyLight", glm::vec3(-2.6f, 3.2f, -2.6f), glm::vec3(1.0f, 0.86f, 0.72f), 7.0f, 8.0f);
    addLight("TemporalLab_BlueRim", glm::vec3(2.4f, 2.4f, 1.5f), glm::vec3(0.35f, 0.55f, 1.0f), 4.0f, 7.0f);
}

void Engine::BuildGodRaysScene() {
    spdlog::info("Building hero scene: God Rays Atrium");

    auto* renderer = m_renderer.get();

    // Camera placed at one end of the atrium, looking toward a bright,
    // backlit wall so volumetric beams and water reflections read clearly.
    {
        entt::entity cameraEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(cameraEntity, "MainCamera");

        auto& t = m_registry->AddComponent<TransformComponent>(cameraEntity);
        t.position = glm::vec3(0.0f, 3.0f, -16.0f);
        glm::vec3 focus(0.0f, 1.5f, 0.0f);
        t.rotation = glm::quatLookAtLH(glm::normalize(focus - t.position),
                                     glm::vec3(0.0f, 1.0f, 0.0f));

        auto& cam = m_registry->AddComponent<Scene::CameraComponent>(cameraEntity);
        cam.fov = 55.0f;
        ConfigureShowcaseCameraClip(cam, 160.0f);
        cam.isActive = true;
    }

    if (renderer) {
        Graphics::ApplyGodRaysSceneControls(*renderer);
    }

    Graphics::Renderer* rendererPtr = m_renderer.get();

    // Atrium dimensions (left-handed, +Z forward).
    const float hallLength = 32.0f;
    const float hallWidth  = 12.0f;
    const float wallHeight = 8.0f;

    // Floor
    auto floorMesh = Utils::MeshGenerator::CreatePlane(hallLength, hallWidth);
    if (rendererPtr) {
        auto upload = rendererPtr->UploadMesh(floorMesh);
        if (upload.IsErr()) {
            spdlog::warn("GodRays: failed to upload floor mesh: {}", upload.Error());
            floorMesh.reset();
        }
        if (rendererPtr->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading GodRays floor; aborting scene build.");
            return;
        }
    }
    if (floorMesh && floorMesh->gpuBuffers) {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "GodRays_Floor");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(0.0f, 0.0f, 0.0f);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = floorMesh;
        r.albedoColor = glm::vec4(0.18f, 0.16f, 0.15f, 1.0f);
        r.metallic = 0.0f;
        r.roughness = 0.6f;
        r.ao = 1.0f;
        r.presetName = "godrays_floor";
    }

    // Walls: long planes enclosing the atrium, leaving the far end open so
    // beams can rake across the interior.
    auto wallMesh = Utils::MeshGenerator::CreatePlane(hallLength, wallHeight);
    if (rendererPtr) {
        auto upload = rendererPtr->UploadMesh(wallMesh);
        if (upload.IsErr()) {
            spdlog::warn("GodRays: failed to upload wall mesh: {}", upload.Error());
            wallMesh.reset();
        }
        if (rendererPtr->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading GodRays walls; aborting scene build.");
            return;
        }
    }

    if (wallMesh && wallMesh->gpuBuffers) {
        const float halfWidth = hallWidth * 0.5f;

        // Left wall
        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "GodRays_LeftWall");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(-halfWidth, wallHeight * 0.5f, 0.0f);
            t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), -glm::half_pi<float>(), 0.0f));

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = wallMesh;
            r.albedoColor = glm::vec4(0.65f, 0.65f, 0.7f, 1.0f);
            r.metallic = 0.0f;
            r.roughness = 0.5f;
            r.ao = 1.0f;
            r.presetName = "godrays_wall";
        }

        // Right wall
        {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "GodRays_RightWall");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(halfWidth, wallHeight * 0.5f, 0.0f);
            t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), glm::half_pi<float>(), 0.0f));

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = wallMesh;
            r.albedoColor = glm::vec4(0.65f, 0.65f, 0.7f, 1.0f);
            r.metallic = 0.0f;
            r.roughness = 0.5f;
            r.ao = 1.0f;
            r.presetName = "godrays_wall";
        }

        // Back wall that catches the main god rays.
        auto backWallMesh = Utils::MeshGenerator::CreatePlane(hallWidth, wallHeight);
        if (rendererPtr) {
            auto upload = rendererPtr->UploadMesh(backWallMesh);
            if (upload.IsErr()) {
                spdlog::warn("GodRays: failed to upload back wall mesh: {}", upload.Error());
                backWallMesh.reset();
            }
        }
        if (backWallMesh && backWallMesh->gpuBuffers) {
            entt::entity e = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(e, "GodRays_BackWall");
            auto& t = m_registry->AddComponent<TransformComponent>(e);
            t.position = glm::vec3(0.0f, wallHeight * 0.5f, hallLength * 0.5f);
            t.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), 0.0f, 0.0f));

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
            r.mesh = backWallMesh;
            r.albedoColor = glm::vec4(0.9f, 0.9f, 0.95f, 1.0f);
            r.metallic = 0.0f;
            r.roughness = 0.35f;
            r.ao = 1.0f;
            r.presetName = "godrays_backwall";
        }
    }

    // Shallow central pool running along the atrium floor. This shares plane
    // geometry between the rim and the water surface.
    auto poolMesh = Utils::MeshGenerator::CreatePlane(hallLength * 0.7f, hallWidth * 0.45f);
    if (rendererPtr) {
        auto upload = rendererPtr->UploadMesh(poolMesh);
        if (upload.IsErr()) {
            spdlog::warn("GodRays: failed to upload pool mesh: {}", upload.Error());
            poolMesh.reset();
        }
        if (rendererPtr->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading GodRays pool; aborting remaining geometry.");
            return;
        }
    }

    if (poolMesh && poolMesh->gpuBuffers) {
        // Pool rim
        entt::entity rim = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(rim, "GodRays_PoolRim");
        auto& rimXf = m_registry->AddComponent<TransformComponent>(rim);
        // Avoid coplanar z-fighting with GodRays_Floor.
        rimXf.position = glm::vec3(0.0f, 0.002f, 4.0f);

        auto& rimR = m_registry->AddComponent<Scene::RenderableComponent>(rim);
        rimR.mesh = poolMesh;
        rimR.albedoColor = glm::vec4(0.85f, 0.85f, 0.87f, 1.0f);
        rimR.metallic = 0.0f;
        rimR.roughness = 0.8f;
        rimR.ao = 1.0f;
        rimR.presetName = "godrays_poolrim";

        // Water surface slightly below the rim.
        entt::entity water = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(water, "GodRays_Water");
        auto& waterXf = m_registry->AddComponent<TransformComponent>(water);
        waterXf.position = glm::vec3(0.0f, -0.02f, 4.0f);

        auto& waterR = m_registry->AddComponent<Scene::RenderableComponent>(water);
        waterR.mesh = poolMesh;
        waterR.albedoColor = glm::vec4(0.03f, 0.09f, 0.13f, 0.8f);
        waterR.metallic = 0.0f;
        waterR.roughness = 0.06f;
        waterR.ao = 1.0f;
        waterR.presetName = "godrays_water";
        m_registry->AddComponent<Scene::WaterSurfaceComponent>(water, Scene::WaterSurfaceComponent{0.0f});
    }

    // Simple columns along the pool to break up beams and provide structure.
    auto columnMesh = Utils::MeshGenerator::CreateCylinder(0.25f, wallHeight, 24);
    if (rendererPtr) {
        auto upload = rendererPtr->UploadMesh(columnMesh);
        if (upload.IsErr()) {
            spdlog::warn("GodRays: failed to upload column mesh: {}", upload.Error());
            columnMesh.reset();
        }
    }
    if (columnMesh && columnMesh->gpuBuffers) {
        const float zStart = -2.0f;
        const float zEnd   = 10.0f;
        const int   count  = 4;
        for (int i = 0; i < count; ++i) {
            float t = (count > 1) ? (float(i) / float(count - 1)) : 0.0f;
            float z = glm::mix(zStart, zEnd, t);

            for (int side = -1; side <= 1; side += 2) {
                entt::entity e = m_registry->CreateEntity();
                m_registry->AddComponent<Scene::TagComponent>(e, "GodRays_Column");
                auto& xf = m_registry->AddComponent<TransformComponent>(e);
                xf.position = glm::vec3(side * 3.0f, wallHeight * 0.5f, z);

                auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
                r.mesh = columnMesh;
                r.albedoColor = glm::vec4(0.7f, 0.7f, 0.75f, 1.0f);
                r.metallic = 0.0f;
                r.roughness = 0.4f;
                r.ao = 1.0f;
                r.presetName = "godrays_column";
            }
        }
    }

    // A pair of hero primitives resting near the pool to show reflections and
    // specular highlights inside the beams.
    auto sphereMesh = Utils::MeshGenerator::CreateSphere(0.5f, 32);
    auto cubeMesh   = Utils::MeshGenerator::CreateCube();
    if (rendererPtr) {
        auto upSphere = rendererPtr->UploadMesh(sphereMesh);
        if (upSphere.IsErr()) {
            spdlog::warn("GodRays: failed to upload sphere mesh: {}", upSphere.Error());
            sphereMesh.reset();
        }
        auto upCube = rendererPtr->UploadMesh(cubeMesh);
        if (upCube.IsErr()) {
            spdlog::warn("GodRays: failed to upload cube mesh: {}", upCube.Error());
            cubeMesh.reset();
        }
        if (rendererPtr->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading GodRays hero meshes; skipping remaining geometry.");
            return;
        }
    }
    if (sphereMesh && sphereMesh->gpuBuffers) {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "GodRays_Sphere");
        auto& xf = m_registry->AddComponent<TransformComponent>(e);
        xf.position = glm::vec3(-1.6f, 0.6f, 4.5f);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = sphereMesh;
        r.albedoColor = glm::vec4(1.0f, 0.98f, 0.95f, 1.0f);
        r.metallic = 1.0f;
        r.roughness = 0.08f;
        r.ao = 1.0f;
        r.presetName = "godrays_chrome_sphere";
    }
    if (cubeMesh && cubeMesh->gpuBuffers) {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "GodRays_GlassCube");
        auto& xf = m_registry->AddComponent<TransformComponent>(e);
        xf.position = glm::vec3(1.8f, 0.7f, 3.5f);
        xf.scale    = glm::vec3(1.2f, 1.2f, 1.2f);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(e);
        r.mesh = cubeMesh;
        r.albedoColor = glm::vec4(0.6f, 0.8f, 1.0f, 0.35f);
        r.metallic = 0.0f;
        r.roughness = 0.05f;
        r.ao = 1.0f;
        r.presetName = "godrays_glass_cube";
    }

    // Simple interior light rig: a warm key and a cool rim to complement the
    // sun and provide additional structure in the beams.
    auto makeSpotRotation = [](const glm::vec3& dir) {
        glm::vec3 fwd = glm::normalize(dir);
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(fwd, up)) > 0.99f) {
            up = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        return glm::quatLookAtLH(fwd, up);
    };

    // Warm key light from above-left, angled through the fog.
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "GodRays_KeyLight");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(-4.0f, 6.0f, 2.0f);
        glm::vec3 dir(0.5f, -0.9f, 0.3f);
        t.rotation = makeSpotRotation(dir);

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Spot;
        l.color = glm::vec3(1.0f, 0.92f, 0.85f);
        l.intensity = 9.0f;
        l.range = 30.0f;
        l.innerConeDegrees = 20.0f;
        l.outerConeDegrees = 35.0f;
        l.castsShadows = true;
    }

    // Cool rim light grazing across the back wall and columns.
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "GodRays_RimLight");
        auto& t = m_registry->AddComponent<TransformComponent>(e);
        t.position = glm::vec3(4.0f, 5.0f, 6.0f);
        glm::vec3 dir(-0.4f, -0.7f, -0.6f);
        t.rotation = makeSpotRotation(dir);

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Spot;
        l.color = glm::vec3(0.8f, 0.9f, 1.1f);
        l.intensity = 6.0f;
        l.range = 28.0f;
        l.innerConeDegrees = 22.0f;
        l.outerConeDegrees = 40.0f;
        l.castsShadows = false;
    }
}

void Engine::BuildDragonStudioScene() {
    spdlog::info("Building hero scene: Dragon Over Water Studio");

    // Hero staging scene: "Dragon Over Water Studio"
    //
    // This scene is designed to exercise:
    //  - Planar water rendering (waves, reflections)
    //  - Direct lighting + cascaded sun shadows
    //  - Hybrid SSR / RT reflections and RT GI
    //  - LLM-driven edits on top of a curated layout.
    //
    // Layout (left-handed, +Z forward):
    //  - Large studio floor centered at z = -3
    //  - Square pool and water surface inset into the floor
    //  - Metal dragon hovering above the water
    //  - Chrome sphere opposite the dragon
    //  - Colored cube on the near rim
    //  - Backdrop wall behind the pool
    //  - Three-point studio lighting rig (key / fill / rim).

    const float poolZ = -3.0f;

    // Create a camera
    entt::entity cameraEntity = m_registry->CreateEntity();
    m_registry->AddComponent<Scene::TagComponent>(cameraEntity, "MainCamera");

    auto& cameraTransform = m_registry->AddComponent<Scene::TransformComponent>(cameraEntity);
    // Place camera above and behind the pool, looking toward its center.
    cameraTransform.position = glm::vec3(0.0f, 3.0f, -8.0f);
    glm::vec3 focus(0.0f, 1.0f, poolZ);
    cameraTransform.rotation = glm::quatLookAtLH(
        glm::normalize(focus - cameraTransform.position),
        glm::vec3(0.0f, 1.0f, 0.0f));

    auto& camera = m_registry->AddComponent<Scene::CameraComponent>(cameraEntity);
    camera.fov = 55.0f;  // Slightly wider FOV for full scene framing
    ConfigureShowcaseCameraClip(camera, 140.0f);
    camera.isActive = true;

    if (m_renderer) {
        Graphics::ApplyDragonWaterStudioSunControls(*m_renderer);
    }

    // Initialize the Khronos sample model library so we can spawn the hero
    // dragon mesh by logical name ("DragonAttenuation"). Failures here should
    // not abort scene creation; we fall back to primitives if needed.
    auto sampleLibResult = Utils::InitializeSampleModelLibrary();
    if (sampleLibResult.IsErr()) {
        spdlog::warn("SampleModelLibrary initialization failed: {}", sampleLibResult.Error());
    }

    // Convenience alias for the renderer pointer.
    Graphics::Renderer* renderer = m_renderer.get();

    // Studio floor: large plane under the pool.
    auto floorMesh = Utils::MeshGenerator::CreatePlane(20.0f, 20.0f);
    if (renderer) {
        auto uploadResult = renderer->UploadMesh(floorMesh);
        if (uploadResult.IsErr()) {
            spdlog::warn("Failed to upload floor mesh: {}", uploadResult.Error());
            floorMesh.reset();
        }
        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading floor mesh; aborting Dragon studio geometry build for this run.");
            return;
        }
    }

    if (floorMesh && floorMesh->gpuBuffers) {
        entt::entity floorEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(floorEntity, "StudioFloor");
        auto& floorXform = m_registry->AddComponent<Scene::TransformComponent>(floorEntity);
        floorXform.position = glm::vec3(0.0f, 0.0f, poolZ);
        floorXform.scale = glm::vec3(1.0f);

        auto& floorRenderable = m_registry->AddComponent<Scene::RenderableComponent>(floorEntity);
        floorRenderable.mesh = floorMesh;
        floorRenderable.albedoColor = glm::vec4(0.35f, 0.25f, 0.18f, 1.0f);
        floorRenderable.metallic = 0.0f;
        floorRenderable.roughness = 0.6f;
        floorRenderable.ao = 1.0f;
        floorRenderable.presetName = "wood_floor";
    } else {
        spdlog::warn("Studio floor mesh is unavailable; 'StudioFloor' entity will be skipped.");
    }

    // Reusable box geometry for raised architectural pieces. Keep this near
    // the pool construction so thin coplanar floor overlays do not creep back
    // into the hero scene.
    auto cubeMesh = Utils::MeshGenerator::CreateCube();
    if (renderer) {
        auto uploadResult = renderer->UploadMesh(cubeMesh);
        if (uploadResult.IsErr()) {
            spdlog::warn("Failed to upload cube mesh: {}", uploadResult.Error());
            cubeMesh.reset();
        }
        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading cube mesh; remaining Dragon studio geometry will be skipped.");
            return;
        }
    }

    // Water surface only. The old scene used one nearly coplanar full-size
    // white plane over the studio floor as a "rim"; camera rotation/TAA jitter
    // exposed that as dark/light flicker. The rim is now raised box coping
    // below, and the water plane is physically separated from the floor.
    auto poolMesh = Utils::MeshGenerator::CreatePlane(6.6f, 5.2f);
    if (renderer) {
        auto uploadResult = renderer->UploadMesh(poolMesh);
        if (uploadResult.IsErr()) {
            spdlog::warn("Failed to upload pool mesh: {}", uploadResult.Error());
            poolMesh.reset();
        }
        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading pool mesh; aborting Dragon studio geometry build for this run.");
            return;
        }
    }

    if (cubeMesh && cubeMesh->gpuBuffers) {
        struct PoolCopingPiece {
            const char* tag;
            glm::vec3 position;
            glm::vec3 scale;
        };

        constexpr float copingY = 0.065f;
        const std::array<PoolCopingPiece, 4> copingPieces{{
            {"PoolCoping_North", glm::vec3(0.0f, copingY, poolZ + 2.82f), glm::vec3(7.55f, 0.12f, 0.36f)},
            {"PoolCoping_South", glm::vec3(0.0f, copingY, poolZ - 2.82f), glm::vec3(7.55f, 0.12f, 0.36f)},
            {"PoolCoping_West",  glm::vec3(-3.78f, copingY, poolZ),        glm::vec3(0.36f, 0.12f, 5.28f)},
            {"PoolCoping_East",  glm::vec3( 3.78f, copingY, poolZ),        glm::vec3(0.36f, 0.12f, 5.28f)}
        }};

        for (const auto& piece : copingPieces) {
            entt::entity rimEntity = m_registry->CreateEntity();
            m_registry->AddComponent<Scene::TagComponent>(rimEntity, piece.tag);
            auto& rimXform = m_registry->AddComponent<Scene::TransformComponent>(rimEntity);
            rimXform.position = piece.position;
            rimXform.scale = piece.scale;

            auto& rimRenderable = m_registry->AddComponent<Scene::RenderableComponent>(rimEntity);
            rimRenderable.mesh = cubeMesh;
            rimRenderable.albedoColor = glm::vec4(0.86f, 0.86f, 0.84f, 1.0f);
            rimRenderable.metallic = 0.0f;
            rimRenderable.roughness = 0.78f;
            rimRenderable.ao = 1.0f;
            rimRenderable.presetName = "concrete";
        }
    } else {
        spdlog::warn("Cube mesh is unavailable; raised pool coping will be skipped.");
    }

    if (poolMesh && poolMesh->gpuBuffers) {
        // Water surface is above the studio floor and below the raised coping
        // top. This avoids both floor/water fighting and rim/water fighting
        // under camera rotation.
        entt::entity waterEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(waterEntity, "WaterSurface");
        auto& waterXform = m_registry->AddComponent<Scene::TransformComponent>(waterEntity);
        waterXform.position = glm::vec3(0.0f, 0.028f, poolZ);
        waterXform.scale = glm::vec3(1.0f);

        auto& waterRenderable = m_registry->AddComponent<Scene::RenderableComponent>(waterEntity);
        waterRenderable.mesh = poolMesh;
        waterRenderable.albedoColor = glm::vec4(0.02f, 0.08f, 0.12f, 0.7f);
        waterRenderable.metallic = 0.0f;
        waterRenderable.roughness = 0.08f;
        waterRenderable.ao = 1.0f;
        waterRenderable.presetName = "water";
        m_registry->AddComponent<Scene::WaterSurfaceComponent>(waterEntity, Scene::WaterSurfaceComponent{0.0f});
    } else {
        spdlog::warn("Pool mesh is unavailable; 'WaterSurface' entity will be skipped.");
    }

    // Backdrop wall behind the pool to catch shadows and reflections.
    auto wallMesh = Utils::MeshGenerator::CreatePlane(20.0f, 10.0f);
    if (renderer) {
        auto uploadResult = renderer->UploadMesh(wallMesh);
        if (uploadResult.IsErr()) {
            spdlog::warn("Failed to upload wall mesh: {}", uploadResult.Error());
            wallMesh.reset();
        }
        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading backdrop wall mesh; aborting remaining Dragon studio geometry.");
            return;
        }
    }

    if (wallMesh && wallMesh->gpuBuffers) {
        entt::entity wallEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(wallEntity, "BackdropWall");
        auto& wallXform = m_registry->AddComponent<Scene::TransformComponent>(wallEntity);
        wallXform.position = glm::vec3(0.0f, 5.0f, poolZ + 8.0f);
        // Rotate plane upright so its normal points roughly toward the camera.
        wallXform.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), 0.0f, 0.0f));
        wallXform.scale = glm::vec3(1.0f);

        auto& wallRenderable = m_registry->AddComponent<Scene::RenderableComponent>(wallEntity);
        wallRenderable.mesh = wallMesh;
        wallRenderable.albedoColor = glm::vec4(0.15f, 0.15f, 0.18f, 1.0f);
        wallRenderable.metallic = 0.0f;
        wallRenderable.roughness = 0.85f;
        wallRenderable.ao = 1.0f;
        wallRenderable.presetName = "backdrop";
    } else {
        spdlog::warn("Backdrop wall mesh is unavailable; 'BackdropWall' entity will be skipped.");
    }

    // Hero dragon mesh over the water.
    std::shared_ptr<Scene::MeshData> dragonMesh;
    auto dragonResult = Utils::LoadSampleModelMesh("DragonAttenuation");
    if (dragonResult.IsOk()) {
        dragonMesh = dragonResult.Value();
        if (renderer) {
            auto uploadResult = renderer->UploadMesh(dragonMesh);
            if (uploadResult.IsErr()) {
                spdlog::warn("Failed to upload dragon mesh: {}", uploadResult.Error());
                dragonMesh.reset();
            }
        }
    } else {
        spdlog::warn("Failed to load DragonAttenuation sample mesh: {}", dragonResult.Error());
    }

    if (dragonMesh && dragonMesh->gpuBuffers) {
        entt::entity dragonEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(dragonEntity, "MetalDragon");
        auto& dragonXform = m_registry->AddComponent<Scene::TransformComponent>(dragonEntity);
        dragonXform.position = glm::vec3(1.5f, 1.0f, poolZ);
        dragonXform.scale = glm::vec3(1.0f);

        auto& dragonRenderable = m_registry->AddComponent<Scene::RenderableComponent>(dragonEntity);
        dragonRenderable.mesh = dragonMesh;
        dragonRenderable.albedoColor = glm::vec4(0.75f, 0.75f, 0.8f, 1.0f);
        dragonRenderable.metallic = 1.0f;
        dragonRenderable.roughness = 0.22f;
        dragonRenderable.ao = 1.0f;
        dragonRenderable.presetName = "polished_metal";
    }

    // Chrome test sphere opposite the dragon.
    auto sphereMesh = Utils::MeshGenerator::CreateSphere(0.75f, 32);
    if (renderer) {
        auto uploadResult = renderer->UploadMesh(sphereMesh);
        if (uploadResult.IsErr()) {
            spdlog::warn("Failed to upload sphere mesh: {}", uploadResult.Error());
            sphereMesh.reset();
        }
        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading sphere mesh; remaining Dragon studio geometry will be skipped.");
            return;
        }
    }

    if (sphereMesh && sphereMesh->gpuBuffers) {
        entt::entity sphereEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(sphereEntity, "MetalSphere");
        auto& sphereXform = m_registry->AddComponent<Scene::TransformComponent>(sphereEntity);
        sphereXform.position = glm::vec3(-1.5f, 1.0f, poolZ);
        sphereXform.scale = glm::vec3(1.0f);

        auto& sphereRenderable = m_registry->AddComponent<Scene::RenderableComponent>(sphereEntity);
        sphereRenderable.mesh = sphereMesh;
        sphereRenderable.albedoColor = glm::vec4(0.75f, 0.75f, 0.8f, 1.0f);
        sphereRenderable.metallic = 1.0f;
        sphereRenderable.roughness = 0.05f;
        sphereRenderable.ao = 1.0f;
        sphereRenderable.presetName = "chrome";
    } else {
        spdlog::warn("Sphere mesh is unavailable; 'MetalSphere' entity will be skipped.");
    }

    // Colored cube on the near rim for GI/reflection contrast.
    if (cubeMesh && cubeMesh->gpuBuffers) {
        entt::entity cubeEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(cubeEntity, "ColorCube");
        auto& cubeXform = m_registry->AddComponent<Scene::TransformComponent>(cubeEntity);
        cubeXform.position = glm::vec3(0.0f, 0.5f, poolZ - 1.5f);
        cubeXform.scale = glm::vec3(1.5f, 1.0f, 1.5f);

        auto& cubeRenderable = m_registry->AddComponent<Scene::RenderableComponent>(cubeEntity);
        cubeRenderable.mesh = cubeMesh;
        cubeRenderable.albedoColor = glm::vec4(0.5f, 0.1f, 0.8f, 1.0f);
        cubeRenderable.metallic = 0.0f;
        cubeRenderable.roughness = 0.4f;
        cubeRenderable.ao = 1.0f;
        cubeRenderable.presetName = "painted_plastic";
    } else {
        spdlog::warn("Cube mesh is unavailable; 'ColorCube' entity will be skipped.");
    }

    // Studio lighting rig: warm key, cool rim, and soft fill.
    auto makeSpotRotation = [](const glm::vec3& dir) {
        glm::vec3 fwd = glm::normalize(dir);
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(fwd, up)) > 0.99f) {
            up = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        return glm::quatLookAtLH(fwd, up);
    };

    // Key light
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "KeyLight");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(3.0f, 4.0f, poolZ - 1.0f);
        glm::vec3 dir(-0.6f, -0.8f, 0.7f);
        t.rotation = makeSpotRotation(dir);

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Spot;
        l.color = glm::vec3(1.0f, 0.95f, 0.85f);
        // Slightly reduced intensity and a softer outer cone keep the floor
        // hotspot under the dragon bright but less extreme. We rely on the
        // sun/cascaded shadows for structure and disable key-light shadows
        // entirely so small PCF/PCSS variations do not cause flicker in the
        // patch under the dragon.
        l.intensity = 10.0f;
        l.range = 25.0f;
        l.innerConeDegrees = 22.0f;
        l.outerConeDegrees = 40.0f;
        l.castsShadows = false;
    }

    // Fill light
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "FillLight");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(-3.0f, 2.0f, poolZ - 0.0f);

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Point;
        l.color = glm::vec3(0.8f, 0.85f, 1.0f);
        l.intensity = 4.0f;
        l.range = 20.0f;
        l.castsShadows = false;
    }

    // Rim light
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "RimLight");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(0.0f, 3.0f, poolZ + 7.0f);
        glm::vec3 dir(0.0f, -0.5f, -1.0f);
        t.rotation = makeSpotRotation(dir);

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Spot;
        l.color = glm::vec3(0.9f, 0.9f, 1.0f);
        l.intensity = 6.0f;
        l.range = 25.0f;
        l.innerConeDegrees = 25.0f;
        l.outerConeDegrees = 42.0f;
        l.castsShadows = false;
    }

    // Large softbox-style area light above the pool to produce broad,
    // studio-like highlights on metals and water. This is implemented as a
    // rectangular area light with no dedicated shadow map; it relies on the
    // existing sun shadows and volumetric fog for structure.
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "SoftboxArea");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(0.0f, 6.0f, poolZ - 1.0f);
        glm::vec3 dir(0.0f, -1.0f, 0.1f);
        t.rotation = makeSpotRotation(dir);

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::AreaRect;
        l.color = glm::vec3(1.0f, 0.98f, 0.94f);
        l.intensity = 3.0f;
        l.range = 30.0f;
        l.areaSize = glm::vec2(6.0f, 4.0f);
        l.twoSided = false;
        l.castsShadows = false;
    }

}

void Engine::BuildCoastalCliffFoundryScene() {
    spdlog::info("Building asset-led scene: Coastal Cliff Foundry");

    auto* renderer = m_renderer.get();
    if (renderer) {
        renderer->SetLightingRigContract("coastal_foundry_dusk", "scene_preset", false);
        renderer->SetWorldShaderPaletteContract("coastal_foundry_dusk", "coastal_foundry_dusk");
        renderer->SetEnvironmentPreset("cool_overcast");
        renderer->SetIBLEnabled(true);
        renderer->SetIBLIntensity(0.72f, 0.98f);
        renderer->SetBackgroundPresentation(false, 0.95f, 0.08f);
        renderer->SetSunDirection(glm::normalize(glm::vec3(-0.42f, 0.48f, 0.25f)));
        renderer->SetSunColor(glm::vec3(1.0f, 0.56f, 0.28f));
        renderer->SetSunIntensity(2.4f);
        renderer->SetRenderScale(0.85f);
        renderer->SetExposure(0.92f);
        renderer->SetBloomIntensity(0.30f);
        renderer->SetBloomShape(0.90f, 0.48f, 1.90f);
        renderer->SetTAAEnabled(true);
        renderer->SetFXAAEnabled(true);
        renderer->SetSSREnabled(true);
        renderer->SetSSAOEnabled(true);
        renderer->SetFogEnabled(true);
        renderer->SetFogParams(0.018f, 0.0f, 0.46f);
        renderer->SetParticlesEnabled(true);
        renderer->SetRTReflectionsEnabled(true);
        renderer->SetWaterParams(-0.03f, 0.09f, 8.0f, 0.75f, 0.85f, 0.28f, 0.045f, 0.65f);
    }

    auto planeMesh = Utils::MeshGenerator::CreatePlane(1.0f, 1.0f);
    auto cubeMesh = Utils::MeshGenerator::CreateCube();
    auto sphereMesh = Utils::MeshGenerator::CreateSphere(0.5f, 24);
    auto boulderMesh = LoadNaturalisticShowcaseMesh("boulder_01/boulder_01_1k.gltf");
    auto barrelMesh = LoadNaturalisticShowcaseMesh("Barrel_01/Barrel_01_1k.gltf");
    if (!UploadAssetLedMesh(renderer, planeMesh, "plane") ||
        !UploadAssetLedMesh(renderer, cubeMesh, "cube") ||
        !UploadAssetLedMesh(renderer, sphereMesh, "sphere") ||
        !UploadAssetLedMesh(renderer, boulderMesh, "boulder_01") ||
        !UploadAssetLedMesh(renderer, barrelMesh, "Barrel_01")) {
        return;
    }

    m_activeCameraEntity = AddAssetLedCamera(*m_registry,
        glm::vec3(-2.35f, 1.42f, -3.08f), glm::vec3(0.28f, 0.72f, -0.06f), 34.0f, 180.0f);

    const AssetLedMaterialSettings wetBasalt{glm::vec4(0.08f, 0.105f, 0.11f, 1.0f), 0.0f, 0.38f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.85f, 0.46f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "wet_masonry"};
    const AssetLedMaterialSettings ocean{glm::vec4(0.025f, 0.18f, 0.26f, 0.78f), 0.0f, 0.07f, 0.35f, 1.333f, glm::vec3(0.0f), 1.0f, 1.0f, 0.2f, true, Scene::RenderableComponent::AlphaMode::Blend, Scene::RenderableComponent::RenderLayer::Opaque, "water"};
    const AssetLedMaterialSettings lava{glm::vec4(1.0f, 0.56f, 0.06f, 1.0f), 0.0f, 0.18f, 0.0f, 1.5f, glm::vec3(1.0f, 0.36f, 0.08f), 5.8f, 0.2f, 0.62f, true, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "lava"};
    const AssetLedMaterialSettings iron{glm::vec4(0.12f, 0.105f, 0.095f, 1.0f), 0.85f, 0.42f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.20f, 0.34f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "brushed_metal"};
    const AssetLedMaterialSettings furnaceSoot{glm::vec4(0.055f, 0.052f, 0.048f, 1.0f), 0.65f, 0.56f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.12f, 0.45f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "soot_grime"};
    const AssetLedMaterialSettings foam{glm::vec4(0.72f, 0.86f, 0.88f, 0.64f), 0.0f, 0.45f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.6f, 0.1f, true, Scene::RenderableComponent::AlphaMode::Blend, Scene::RenderableComponent::RenderLayer::Overlay, "water_foam"};

    AddAssetLedRenderable(*m_registry, "CoastalFoundry_OceanPlane", planeMesh, glm::vec3(-0.6f, -0.04f, 2.6f), glm::vec3(10.5f, 1.0f, 5.0f), glm::vec3(0.0f), ocean);
    AddAssetLedRenderable(*m_registry, "CoastalFoundry_WetBasaltDeck", cubeMesh, glm::vec3(-0.8f, -0.09f, -0.65f), glm::vec3(7.2f, 0.18f, 4.2f), glm::vec3(0.0f, glm::radians(-8.0f), 0.0f), wetBasalt);
    AddAssetLedRenderable(*m_registry, "CoastalFoundry_ForegroundWetLedge", cubeMesh, glm::vec3(-2.9f, 0.02f, -2.15f), glm::vec3(3.0f, 0.18f, 1.0f), glm::vec3(0.0f, glm::radians(-16.0f), 0.0f), wetBasalt);
    AddAssetLedRenderable(*m_registry, "CoastalFoundry_OceanDropSkirt", cubeMesh, glm::vec3(-1.2f, -0.54f, 1.25f), glm::vec3(8.0f, 0.92f, 0.34f), glm::vec3(0.0f, glm::radians(-8.0f), 0.0f), wetBasalt);
    AddAssetLedRenderable(*m_registry, "CoastalFoundry_RearCliffShelf", cubeMesh, glm::vec3(0.15f, 0.28f, 3.05f), glm::vec3(6.4f, 0.48f, 1.10f), glm::vec3(0.0f, glm::radians(-6.0f), 0.0f), wetBasalt);
    AddAssetLedRenderable(*m_registry, "CoastalFoundry_UpperBasaltCut", cubeMesh, glm::vec3(-1.55f, 0.62f, 3.40f), glm::vec3(4.2f, 0.24f, 0.34f), glm::vec3(0.0f, glm::radians(11.0f), glm::radians(-3.0f)), wetBasalt);
    AddAssetLedRenderable(*m_registry, "CoastalFoundry_DistantCliffWall", cubeMesh, glm::vec3(-1.15f, 0.64f, 3.92f), glm::vec3(5.0f, 0.58f, 0.26f), glm::vec3(0.0f, glm::radians(-4.0f), glm::radians(1.0f)), wetBasalt);
    AddAssetLedRenderable(*m_registry, "CoastalFoundry_CliffNotchLeft", cubeMesh, glm::vec3(-3.35f, 0.92f, 3.48f), glm::vec3(1.0f, 0.30f, 0.30f), glm::vec3(0.0f, glm::radians(17.0f), glm::radians(-8.0f)), wetBasalt);
    AddAssetLedRenderable(*m_registry, "CoastalFoundry_CliffNotchRight", cubeMesh, glm::vec3(2.65f, 0.90f, 3.58f), glm::vec3(1.05f, 0.28f, 0.30f), glm::vec3(0.0f, glm::radians(-14.0f), glm::radians(6.0f)), wetBasalt);
    AddAssetLedRenderable(*m_registry, "CoastalFoundry_BrokenCliffShoulderA", cubeMesh, glm::vec3(-3.15f, 0.36f, 2.10f), glm::vec3(1.55f, 0.62f, 0.78f), glm::vec3(0.0f, glm::radians(18.0f), 0.0f), wetBasalt);
    AddAssetLedRenderable(*m_registry, "CoastalFoundry_BrokenCliffShoulderB", cubeMesh, glm::vec3(2.42f, 0.38f, 2.05f), glm::vec3(1.70f, 0.64f, 0.70f), glm::vec3(0.0f, glm::radians(-15.0f), 0.0f), wetBasalt);
    AddAssetLedRenderable(*m_registry, "CoastalFoundry_LeftCliffReturn", cubeMesh, glm::vec3(-5.35f, 0.62f, 1.65f), glm::vec3(0.35f, 1.25f, 5.2f), glm::vec3(0.0f, glm::radians(8.0f), 0.0f), wetBasalt);
    AddAssetLedRenderable(*m_registry, "CoastalFoundry_RightIndustrialSilhouette", cubeMesh, glm::vec3(4.75f, 0.54f, 1.2f), glm::vec3(0.24f, 0.78f, 2.55f), glm::vec3(0.0f, glm::radians(-8.0f), 0.0f), iron);
    AddAssetLedRenderable(*m_registry, "CoastalFoundry_LavaSurface", cubeMesh, glm::vec3(0.40f, 0.42f, -0.10f), glm::vec3(2.75f, 0.04f, 0.88f), glm::vec3(0.0f, glm::radians(-8.0f), 0.0f), lava);
    AddAssetLedRenderable(*m_registry, "CoastalFoundry_ChannelNorthWall", cubeMesh, glm::vec3(0.32f, 0.55f, -0.70f), glm::vec3(2.95f, 0.22f, 0.13f), glm::vec3(0.0f, glm::radians(-8.0f), 0.0f), furnaceSoot);
    AddAssetLedRenderable(*m_registry, "CoastalFoundry_ChannelSouthWall", cubeMesh, glm::vec3(0.52f, 0.55f, 0.46f), glm::vec3(2.95f, 0.22f, 0.13f), glm::vec3(0.0f, glm::radians(-8.0f), 0.0f), furnaceSoot);
    AddAssetLedRenderable(*m_registry, "CoastalFoundry_FurnaceUprightLeft", cubeMesh, glm::vec3(-1.50f, 0.86f, 0.62f), glm::vec3(0.11f, 0.66f, 0.13f), glm::vec3(0.0f, glm::radians(-8.0f), 0.0f), furnaceSoot);
    AddAssetLedRenderable(*m_registry, "CoastalFoundry_FurnaceUprightRight", cubeMesh, glm::vec3(2.12f, 0.86f, 0.10f), glm::vec3(0.11f, 0.66f, 0.13f), glm::vec3(0.0f, glm::radians(-8.0f), 0.0f), furnaceSoot);
    AddAssetLedRenderable(*m_registry, "CoastalFoundry_FurnaceCrossBeam", cubeMesh, glm::vec3(0.24f, 0.86f, 0.24f), glm::vec3(1.20f, 0.040f, 0.070f), glm::vec3(0.0f, glm::radians(-8.0f), 0.0f), furnaceSoot);
    AddAssetLedRenderable(*m_registry, "CoastalFoundry_DiagonalBraceLeft", cubeMesh, glm::vec3(-0.82f, 0.74f, -0.48f), glm::vec3(0.72f, 0.055f, 0.09f), glm::vec3(0.0f, glm::radians(-8.0f), glm::radians(-17.0f)), furnaceSoot);
    AddAssetLedRenderable(*m_registry, "CoastalFoundry_DiagonalBraceRight", cubeMesh, glm::vec3(1.52f, 0.72f, 0.34f), glm::vec3(0.78f, 0.055f, 0.09f), glm::vec3(0.0f, glm::radians(-8.0f), glm::radians(16.0f)), furnaceSoot);
    AddAssetLedRenderable(*m_registry, "CoastalFoundry_DrainApron", cubeMesh, glm::vec3(2.85f, 0.36f, -0.02f), glm::vec3(1.2f, 0.14f, 0.78f), glm::vec3(0.0f, glm::radians(-8.0f), 0.0f), furnaceSoot);
    AddAssetLedRenderable(*m_registry, "CoastalFoundry_LeftRailRun", cubeMesh, glm::vec3(-0.22f, 0.75f, -1.02f), glm::vec3(1.58f, 0.036f, 0.044f), glm::vec3(0.0f, glm::radians(-8.0f), 0.0f), iron);
    AddAssetLedRenderable(*m_registry, "CoastalFoundry_RightRailRun", cubeMesh, glm::vec3(0.62f, 0.75f, 0.76f), glm::vec3(1.58f, 0.036f, 0.044f), glm::vec3(0.0f, glm::radians(-8.0f), 0.0f), iron);
    AddAssetLedRenderable(*m_registry, "CoastalFoundry_LeftLowerRail", cubeMesh, glm::vec3(-0.22f, 0.58f, -1.02f), glm::vec3(1.58f, 0.030f, 0.036f), glm::vec3(0.0f, glm::radians(-8.0f), 0.0f), iron);
    AddAssetLedRenderable(*m_registry, "CoastalFoundry_RightLowerRail", cubeMesh, glm::vec3(0.62f, 0.58f, 0.76f), glm::vec3(1.58f, 0.030f, 0.036f), glm::vec3(0.0f, glm::radians(-8.0f), 0.0f), iron);
    for (int i = 0; i < 4; ++i) {
        const float x = -1.20f + static_cast<float>(i) * 0.78f;
        AddAssetLedRenderable(*m_registry, "CoastalFoundry_RailPost", cubeMesh, glm::vec3(x, 0.52f, -1.15f), glm::vec3(0.060f, 0.34f, 0.060f), glm::vec3(0.0f, glm::radians(-8.0f), 0.0f), iron);
        AddAssetLedRenderable(*m_registry, "CoastalFoundry_RailPost", cubeMesh, glm::vec3(x + 0.22f, 0.52f, 0.92f), glm::vec3(0.060f, 0.34f, 0.060f), glm::vec3(0.0f, glm::radians(-8.0f), 0.0f), iron);
        AddAssetLedRenderable(*m_registry, "CoastalFoundry_RailFoot", cubeMesh, glm::vec3(x, 0.34f, -1.15f), glm::vec3(0.18f, 0.055f, 0.16f), glm::vec3(0.0f, glm::radians(-8.0f), 0.0f), iron);
        AddAssetLedRenderable(*m_registry, "CoastalFoundry_RailFoot", cubeMesh, glm::vec3(x + 0.22f, 0.34f, 0.92f), glm::vec3(0.18f, 0.055f, 0.16f), glm::vec3(0.0f, glm::radians(-8.0f), 0.0f), iron);
    }
    for (int i = 0; i < 6; ++i) {
        const float x = -1.70f + static_cast<float>(i) * 0.72f;
        AddAssetLedRenderable(*m_registry, "CoastalFoundry_ChannelGrateSlat", cubeMesh, glm::vec3(x, 0.67f, -0.10f), glm::vec3(0.045f, 0.05f, 1.18f), glm::vec3(0.0f, glm::radians(-8.0f), 0.0f), furnaceSoot);
    }
    AddAssetLedRenderable(*m_registry, "CoastalFoundry_ShoreFoamBand", planeMesh, glm::vec3(-1.4f, 0.02f, 1.05f), glm::vec3(7.4f, 1.0f, 0.22f), glm::vec3(0.0f, glm::radians(-8.0f), 0.0f), foam);
    if (boulderMesh && boulderMesh->gpuBuffers) {
        AddAssetLedNaturalisticRenderable(*m_registry, "CoastalFoundry_BoulderAnchor", "boulder_01", boulderMesh, glm::vec3(-2.7f, 0.20f, 1.8f), glm::vec3(1.35f), glm::vec3(0.0f, glm::radians(28.0f), 0.0f), wetBasalt);
        AddAssetLedNaturalisticRenderable(*m_registry, "CoastalFoundry_ForegroundRockMass", "boulder_01", boulderMesh, glm::vec3(-1.86f, 0.10f, -2.08f), glm::vec3(0.46f), glm::vec3(0.0f, glm::radians(-18.0f), 0.0f), wetBasalt);
        AddAssetLedNaturalisticRenderable(*m_registry, "CoastalFoundry_RightShoreRockMass", "boulder_01", boulderMesh, glm::vec3(3.65f, 0.16f, 1.35f), glm::vec3(0.85f), glm::vec3(0.0f, glm::radians(54.0f), 0.0f), wetBasalt);
        AddAssetLedNaturalisticRenderable(*m_registry, "CoastalFoundry_RearBasaltMassLeft", "boulder_01", boulderMesh, glm::vec3(-3.10f, 0.42f, 2.72f), glm::vec3(0.82f), glm::vec3(0.0f, glm::radians(-38.0f), glm::radians(4.0f)), wetBasalt);
        AddAssetLedNaturalisticRenderable(*m_registry, "CoastalFoundry_RearBasaltMassCenter", "boulder_01", boulderMesh, glm::vec3(-0.55f, 0.44f, 3.02f), glm::vec3(0.96f), glm::vec3(0.0f, glm::radians(12.0f), glm::radians(-7.0f)), wetBasalt);
        AddAssetLedNaturalisticRenderable(*m_registry, "CoastalFoundry_RearBasaltMassRight", "boulder_01", boulderMesh, glm::vec3(2.35f, 0.40f, 2.70f), glm::vec3(0.78f), glm::vec3(0.0f, glm::radians(51.0f), glm::radians(5.0f)), wetBasalt);
        AddAssetLedNaturalisticRenderable(*m_registry, "CoastalFoundry_LeftCliffToe", "boulder_01", boulderMesh, glm::vec3(-4.35f, 0.24f, 0.85f), glm::vec3(1.05f), glm::vec3(0.0f, glm::radians(74.0f), 0.0f), wetBasalt);
        AddAssetLedNaturalisticRenderable(*m_registry, "CoastalFoundry_UpperCliffCrownLeft", "boulder_01", boulderMesh, glm::vec3(-2.65f, 0.66f, 3.34f), glm::vec3(0.44f), glm::vec3(0.0f, glm::radians(-18.0f), glm::radians(5.0f)), wetBasalt);
        AddAssetLedNaturalisticRenderable(*m_registry, "CoastalFoundry_UpperCliffCrownRight", "boulder_01", boulderMesh, glm::vec3(1.42f, 0.62f, 3.38f), glm::vec3(0.38f), glm::vec3(0.0f, glm::radians(36.0f), glm::radians(-4.0f)), wetBasalt);
    }
    if (barrelMesh && barrelMesh->gpuBuffers) {
        AddAssetLedNaturalisticRenderable(*m_registry, "CoastalFoundry_GroundedBarrel", "Barrel_01", barrelMesh, glm::vec3(2.45f, 0.18f, -1.32f), glm::vec3(0.58f), glm::vec3(0.0f, glm::radians(-24.0f), 0.0f), iron);
    }
    AddParticleEffect(*m_registry, "CoastalFoundry_EmberColumn", "embers", glm::vec3(0.55f, 0.92f, -0.05f));
    AddParticleEffect(*m_registry, "CoastalFoundry_SmokeColumn", "smoke", glm::vec3(0.15f, 1.08f, -0.05f));
    AddAssetLedPointLight(*m_registry, "CoastalFoundry_LavaLight", glm::vec3(0.5f, 0.72f, -0.05f), glm::vec3(1.0f, 0.36f, 0.10f), 7.5f, 8.0f);
    AddAssetLedSpotLight(*m_registry, "CoastalFoundry_CoolSkyRim", glm::vec3(-4.6f, 4.2f, 3.8f), glm::vec3(0.0f, 0.45f, -0.2f), glm::vec3(0.32f, 0.48f, 0.72f), 4.5f, 18.0f, false);
}

void Engine::BuildRainGlassPavilionScene() {
    spdlog::info("Building asset-led scene: Rain Glass Pavilion");

    auto* renderer = m_renderer.get();
    if (renderer) {
        renderer->SetLightingRigContract("rain_pavilion_night", "scene_preset", false);
        renderer->SetWorldShaderPaletteContract("rain_pavilion_night", "rain_pavilion_night");
        renderer->SetEnvironmentPreset("neutral_procedural");
        renderer->SetIBLEnabled(true);
        renderer->SetIBLIntensity(0.76f, 0.58f);
        renderer->SetBackgroundPresentation(false, 0.90f, 0.14f);
        renderer->SetSunDirection(glm::normalize(glm::vec3(-0.18f, 0.72f, 0.50f)));
        renderer->SetSunColor(glm::vec3(0.42f, 0.62f, 1.0f));
        renderer->SetSunIntensity(4.4f);
        renderer->SetExposure(2.08f);
        renderer->SetBloomIntensity(0.30f);
        renderer->SetTAAEnabled(true);
        renderer->SetFXAAEnabled(true);
        renderer->SetSSREnabled(true);
        renderer->SetSSAOEnabled(true);
        renderer->SetFogEnabled(true);
        renderer->SetFogParams(0.022f, 0.0f, 0.45f);
        renderer->SetParticlesEnabled(true);
        renderer->SetRTReflectionsEnabled(true);
        renderer->SetWaterParams(0.03f, 0.035f, 4.6f, 0.65f, 0.55f, 0.18f, 0.018f, 0.55f);
    }

    auto planeMesh = Utils::MeshGenerator::CreatePlane(1.0f, 1.0f);
    auto cubeMesh = Utils::MeshGenerator::CreateCube();
    auto cylinderMesh = Utils::MeshGenerator::CreateCylinder(0.5f, 0.12f, 32);
    auto sphereMesh = Utils::MeshGenerator::CreateSphere(0.5f, 32);
    auto torusMesh = Utils::MeshGenerator::CreateTorus(0.42f, 0.035f, 32, 12);
    auto lanternMesh = LoadNaturalisticShowcaseMesh("Lantern_01/Lantern_01_1k.gltf");
    auto tableMesh = LoadNaturalisticShowcaseMesh("WoodenTable_01/WoodenTable_01_1k.gltf");
    auto bushMesh = LoadNaturalisticShowcaseMesh("wild_rooibos_bush/wild_rooibos_bush_1k.gltf");
    auto fernMesh = LoadNaturalisticShowcaseMesh("fern_02/fern_02_1k.gltf");
    auto pretrainedLanternMesh = LoadPretrainedGeneratedMesh("openai_shap_e_text300m/rain_glass_pavilion/rain_tabletop_lantern_cluster/rain_glass_pavilion_rain_tabletop_lantern_cluster_openai_shap_e_text300m.gltf");
    auto pretrainedTableDressingMesh = LoadPretrainedGeneratedMesh("openai_shap_e_text300m/rain_glass_pavilion/rain_glass_table_dressing/rain_glass_pavilion_rain_glass_table_dressing_openai_shap_e_text300m.gltf");
    auto pretrainedPuddleMesh = LoadPretrainedGeneratedMesh("openai_shap_e_text300m/rain_glass_pavilion/rain_puddle_floor_patch/rain_glass_pavilion_rain_puddle_floor_patch_openai_shap_e_text300m.gltf");
    auto pretrainedWindowPanelMesh = LoadPretrainedGeneratedMesh("openai_shap_e_text300m/rain_glass_pavilion/rain_streak_window_panel/rain_glass_pavilion_rain_streak_window_panel_openai_shap_e_text300m.gltf");
    const auto pretrainedLayout = LoadPretrainedRuntimeLayout();
    if (!UploadAssetLedMesh(renderer, planeMesh, "plane") ||
        !UploadAssetLedMesh(renderer, cubeMesh, "cube") ||
        !UploadAssetLedMesh(renderer, cylinderMesh, "cylinder") ||
        !UploadAssetLedMesh(renderer, sphereMesh, "sphere") ||
        !UploadAssetLedMesh(renderer, torusMesh, "torus") ||
        !UploadAssetLedMesh(renderer, lanternMesh, "Lantern_01") ||
        !UploadAssetLedMesh(renderer, tableMesh, "WoodenTable_01") ||
        !UploadAssetLedMesh(renderer, bushMesh, "wild_rooibos_bush") ||
        !UploadAssetLedMesh(renderer, fernMesh, "fern_02") ||
        !UploadAssetLedMesh(renderer, pretrainedLanternMesh, "pretrained_rain_tabletop_lantern_cluster") ||
        !UploadAssetLedMesh(renderer, pretrainedTableDressingMesh, "pretrained_rain_glass_table_dressing") ||
        !UploadAssetLedMesh(renderer, pretrainedPuddleMesh, "pretrained_rain_puddle_floor_patch") ||
        !UploadAssetLedMesh(renderer, pretrainedWindowPanelMesh, "pretrained_rain_streak_window_panel")) {
        return;
    }

    m_activeCameraEntity = AddAssetLedCamera(*m_registry,
        pretrainedLayout.cameraPosition, pretrainedLayout.cameraTarget, pretrainedLayout.cameraFov, 120.0f);

    const AssetLedMaterialSettings wetTile{glm::vec4(0.24f, 0.26f, 0.30f, 1.0f), 0.0f, 0.46f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.42f, 0.18f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "wet_masonry"};
    const AssetLedMaterialSettings gardenMat{glm::vec4(0.11f, 0.15f, 0.13f, 1.0f), 0.0f, 0.74f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.24f, 0.46f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "moss_vegetation"};
    const AssetLedMaterialSettings vegetation{glm::vec4(0.12f, 0.24f, 0.16f, 1.0f), 0.0f, 0.64f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.16f, 0.32f, true, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "moss_vegetation"};
    const AssetLedMaterialSettings glass{glm::vec4(0.18f, 0.30f, 0.42f, 0.14f), 0.0f, 0.20f, 0.72f, 1.45f, glm::vec3(0.0f), 1.0f, 0.04f, 0.02f, true, Scene::RenderableComponent::AlphaMode::Blend, Scene::RenderableComponent::RenderLayer::Opaque, "glass"};
    const AssetLedMaterialSettings frameMetal{glm::vec4(0.58f, 0.64f, 0.68f, 1.0f), 1.0f, 0.28f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.22f, 0.04f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "oxidized_metal"};
    const AssetLedMaterialSettings chrome{glm::vec4(0.72f, 0.78f, 0.84f, 1.0f), 0.82f, 0.24f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.16f, 0.04f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "chrome"};
    const AssetLedMaterialSettings wetWood{glm::vec4(0.44f, 0.31f, 0.22f, 1.0f), 0.0f, 0.54f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.18f, 0.08f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "wet_masonry"};
    const AssetLedMaterialSettings warmLight{glm::vec4(1.0f, 0.70f, 0.40f, 1.0f), 0.0f, 0.18f, 0.0f, 1.5f, glm::vec3(1.0f, 0.58f, 0.26f), 4.2f, 0.0f, 0.05f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "emissive"};
    const AssetLedMaterialSettings warmMat{glm::vec4(0.24f, 0.16f, 0.10f, 1.0f), 0.0f, 0.54f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.14f, 0.26f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "wood"};
    const AssetLedMaterialSettings tabletopWarmAccent{glm::vec4(0.70f, 0.28f, 0.10f, 1.0f), 0.0f, 0.38f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.08f, 0.16f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "ceramic"};
    const AssetLedMaterialSettings pretrainedBrass{glm::vec4(0.72f, 0.48f, 0.24f, 1.0f), 0.58f, 0.42f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.10f, 0.04f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "brushed_metal"};
    const AssetLedMaterialSettings pretrainedGlassAccent{glm::vec4(0.62f, 0.82f, 0.94f, 0.38f), 0.0f, 0.22f, 0.38f, 1.45f, glm::vec3(0.0f), 1.0f, 0.12f, 0.04f, true, Scene::RenderableComponent::AlphaMode::Blend, Scene::RenderableComponent::RenderLayer::Opaque, "glass"};
    const AssetLedMaterialSettings pretrainedWetPatch{glm::vec4(0.18f, 0.28f, 0.34f, 0.52f), 0.0f, 0.22f, 0.34f, 1.333f, glm::vec3(0.0f), 1.0f, 0.36f, 0.04f, true, Scene::RenderableComponent::AlphaMode::Blend, Scene::RenderableComponent::RenderLayer::Opaque, "water"};
    const AssetLedMaterialSettings tabletopVignette{glm::vec4(0.12f, 0.14f, 0.15f, 1.0f), 0.0f, 0.52f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.28f, 0.22f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "wet_masonry"};

    AddAssetLedRenderable(*m_registry, "RainPavilion_TiledFloor", cubeMesh, glm::vec3(0.0f, -0.04f, 0.0f), glm::vec3(7.0f, 0.08f, 5.2f), glm::vec3(0.0f), wetTile);
    AddAssetLedRenderable(*m_registry, "RainPavilion_ExtendedWetTerrace", cubeMesh, glm::vec3(0.0f, -0.08f, 1.6f), glm::vec3(11.0f, 0.08f, 6.4f), glm::vec3(0.0f), wetTile);
    AddAssetLedRenderable(*m_registry, "RainPavilion_FrontStoneApron", cubeMesh, glm::vec3(0.0f, -0.03f, -3.10f), glm::vec3(9.4f, 0.10f, 1.6f), glm::vec3(0.0f), wetTile);
    AddAssetLedRenderable(*m_registry, "RainPavilion_LeftTerraceSkirt", cubeMesh, glm::vec3(-5.5f, -0.42f, 0.55f), glm::vec3(0.18f, 0.76f, 7.0f), glm::vec3(0.0f), wetTile);
    AddAssetLedRenderable(*m_registry, "RainPavilion_RightTerraceSkirt", cubeMesh, glm::vec3(5.5f, -0.42f, 0.55f), glm::vec3(0.18f, 0.76f, 7.0f), glm::vec3(0.0f), wetTile);
    AddAssetLedRenderable(*m_registry, "RainPavilion_RearTerraceSkirt", cubeMesh, glm::vec3(0.0f, -0.42f, 4.65f), glm::vec3(11.0f, 0.76f, 0.18f), glm::vec3(0.0f), wetTile);
    AddAssetLedRenderable(*m_registry, "RainPavilion_DarkGardenBackdrop", cubeMesh, glm::vec3(0.0f, 0.42f, 4.05f), glm::vec3(8.4f, 0.84f, 0.16f), glm::vec3(0.0f), gardenMat);
    AddAssetLedRenderable(*m_registry, "RainPavilion_LeftGardenMass", cubeMesh, glm::vec3(-4.65f, 0.36f, 1.05f), glm::vec3(0.18f, 0.72f, 4.5f), glm::vec3(0.0f), gardenMat);
    AddAssetLedRenderable(*m_registry, "RainPavilion_RightGardenMass", cubeMesh, glm::vec3(4.65f, 0.36f, 1.05f), glm::vec3(0.18f, 0.72f, 4.5f), glm::vec3(0.0f), gardenMat);
    AddAssetLedRenderable(*m_registry, "RainPavilion_RearPlanterLeft", cubeMesh, glm::vec3(-2.15f, 0.18f, 3.34f), glm::vec3(1.72f, 0.36f, 0.38f), glm::vec3(0.0f), wetTile);
    AddAssetLedRenderable(*m_registry, "RainPavilion_RearPlanterRight", cubeMesh, glm::vec3(1.95f, 0.18f, 3.28f), glm::vec3(1.92f, 0.36f, 0.38f), glm::vec3(0.0f), wetTile);
    for (const float x : {-0.82f, 0.72f}) {
        AddAssetLedRenderable(*m_registry, "RainPavilion_RearWoodScreen", cubeMesh, glm::vec3(x, 0.18f, 1.90f), glm::vec3(0.64f, 0.18f, 0.045f), glm::vec3(0.0f), warmMat);
        AddAssetLedRenderable(*m_registry, "RainPavilion_RearWoodScreenCap", cubeMesh, glm::vec3(x, 0.30f, 1.87f), glm::vec3(0.72f, 0.040f, 0.080f), glm::vec3(0.0f), wetWood);
    }
    for (int i = 0; i < 4; ++i) {
        const float x = -1.00f + static_cast<float>(i) * 0.52f;
        const float height = (i % 2 == 0) ? 0.34f : 0.26f;
        AddAssetLedRenderable(*m_registry, "RainPavilion_RearWoodScreenSlat", cubeMesh, glm::vec3(x, 0.34f + height * 0.5f, 1.84f), glm::vec3(0.032f, height, 0.052f), glm::vec3(0.0f), wetWood);
    }
    for (int i = 0; i < 7; ++i) {
        const float x = -3.0f + static_cast<float>(i) * 1.0f;
        const float height = (i % 3 == 0) ? 0.62f : 0.50f;
        AddAssetLedRenderable(*m_registry, "RainPavilion_GardenScreenSlat", cubeMesh, glm::vec3(x, 0.42f + height * 0.5f, 3.82f), glm::vec3(0.040f, height, 0.052f), glm::vec3(0.0f), gardenMat);
    }
    AddAssetLedRenderable(*m_registry, "RainPavilion_PuddleSheet_A", planeMesh, glm::vec3(-0.9f, 0.012f, -1.3f), glm::vec3(2.4f, 1.0f, 1.1f), glm::vec3(0.0f), glass);
    AddAssetLedRenderable(*m_registry, "RainPavilion_TableWarmMat", cubeMesh, glm::vec3(-0.16f, 0.025f, 1.02f), glm::vec3(1.48f, 0.030f, 0.72f), glm::vec3(0.0f, glm::radians(-12.0f), 0.0f), warmMat);
    AddAssetLedRenderable(*m_registry, "RainPavilion_GlassWallLeft", cubeMesh, glm::vec3(-2.2f, 1.00f, 0.0f), glm::vec3(0.055f, 1.86f, 4.2f), glm::vec3(0.0f), glass);
    AddAssetLedRenderable(*m_registry, "RainPavilion_GlassWallRight", cubeMesh, glm::vec3(2.2f, 1.00f, 0.0f), glm::vec3(0.055f, 1.86f, 4.2f), glm::vec3(0.0f), glass);
    AddAssetLedRenderable(*m_registry, "RainPavilion_RearGlassWall", cubeMesh, glm::vec3(0.0f, 1.00f, 2.08f), glm::vec3(4.4f, 1.86f, 0.055f), glm::vec3(0.0f), glass);
    AddAssetLedRenderable(*m_registry, "RainPavilion_LeftGlassBaseRail", cubeMesh, glm::vec3(-2.2f, 0.12f, 0.0f), glm::vec3(0.16f, 0.14f, 4.35f), glm::vec3(0.0f), frameMetal);
    AddAssetLedRenderable(*m_registry, "RainPavilion_RightGlassBaseRail", cubeMesh, glm::vec3(2.2f, 0.12f, 0.0f), glm::vec3(0.16f, 0.14f, 4.35f), glm::vec3(0.0f), frameMetal);
    AddAssetLedRenderable(*m_registry, "RainPavilion_RearGlassBaseRail", cubeMesh, glm::vec3(0.0f, 0.12f, 2.08f), glm::vec3(4.55f, 0.14f, 0.16f), glm::vec3(0.0f), frameMetal);
    AddAssetLedRenderable(*m_registry, "RainPavilion_RoofFrame", cubeMesh, glm::vec3(0.0f, 2.18f, -2.12f), glm::vec3(4.45f, 0.055f, 0.08f), glm::vec3(0.0f), frameMetal);
    AddAssetLedRenderable(*m_registry, "RainPavilion_RoofRearBeam", cubeMesh, glm::vec3(0.0f, 2.18f, 2.12f), glm::vec3(4.45f, 0.055f, 0.08f), glm::vec3(0.0f), frameMetal);
    AddAssetLedRenderable(*m_registry, "RainPavilion_RoofLeftBeam", cubeMesh, glm::vec3(-2.25f, 2.18f, 0.0f), glm::vec3(0.08f, 0.055f, 4.2f), glm::vec3(0.0f), frameMetal);
    AddAssetLedRenderable(*m_registry, "RainPavilion_RoofRightBeam", cubeMesh, glm::vec3(2.25f, 2.18f, 0.0f), glm::vec3(0.08f, 0.055f, 4.2f), glm::vec3(0.0f), frameMetal);
    AddAssetLedRenderable(*m_registry, "RainPavilion_GlassRoofPanel", cubeMesh, glm::vec3(0.0f, 2.14f, 0.02f), glm::vec3(4.18f, 0.035f, 4.05f), glm::vec3(0.0f), glass);
    for (const float x : {-2.25f, 2.25f}) {
        for (const float z : {-2.08f, 2.08f}) {
            AddAssetLedRenderable(*m_registry, "RainPavilion_CornerPost", cubeMesh, glm::vec3(x, 1.10f, z), glm::vec3(0.08f, 2.08f, 0.08f), glm::vec3(0.0f), frameMetal);
        }
    }
    for (int i = 0; i < 5; ++i) {
        const float x = -2.2f + static_cast<float>(i) * 1.1f;
        AddAssetLedRenderable(*m_registry, "RainPavilion_RoofMullion", cubeMesh, glm::vec3(x, 2.16f, 0.0f), glm::vec3(0.045f, 0.045f, 4.05f), glm::vec3(0.0f), frameMetal);
        AddAssetLedRenderable(*m_registry, "RainPavilion_FloorChannel", cubeMesh, glm::vec3(x, 0.10f, 0.0f), glm::vec3(0.05f, 0.07f, 4.15f), glm::vec3(0.0f), frameMetal);
        AddAssetLedRenderable(*m_registry, "RainPavilion_GlassMullion", cubeMesh, glm::vec3(x, 1.08f, 2.10f), glm::vec3(0.04f, 1.82f, 0.06f), glm::vec3(0.0f), frameMetal);
    }
    AddAssetLedRenderable(*m_registry, "RainPavilion_ChromeDrain", cylinderMesh, glm::vec3(0.9f, 0.04f, -1.7f), glm::vec3(0.42f, 1.0f, 0.42f), glm::vec3(0.0f), chrome);
    AddAssetLedRenderable(*m_registry, "RainPavilion_ChromePuddleRing", torusMesh, glm::vec3(0.9f, 0.075f, -1.7f), glm::vec3(0.72f), glm::vec3(0.0f), chrome);
    AddAssetLedRenderable(*m_registry, "RainPavilion_WarmInteriorStrip", cubeMesh, glm::vec3(0.0f, 1.82f, 1.96f), glm::vec3(2.05f, 0.035f, 0.045f), glm::vec3(0.0f), warmLight);
    if (lanternMesh && lanternMesh->gpuBuffers) {
        AddAssetLedNaturalisticRenderable(*m_registry, "RainPavilion_GroundedLantern", "Lantern_01", lanternMesh, glm::vec3(-0.84f, 0.40f, 1.02f), glm::vec3(0.42f), glm::vec3(0.0f, glm::radians(18.0f), 0.0f), warmLight);
    }
    if (tableMesh && tableMesh->gpuBuffers) {
        AddAssetLedNaturalisticRenderable(*m_registry, "RainPavilion_GroundedInteriorTable", "WoodenTable_01", tableMesh, glm::vec3(-0.10f, 0.33f, 1.10f), glm::vec3(0.62f), glm::vec3(0.0f, glm::radians(-12.0f), 0.0f), wetWood);
    }
    const auto trayTransform = GetRuntimeOverride(pretrainedLayout, "table_warm_tray", glm::vec3(-0.38f, 0.675f, 0.86f), glm::vec3(0.22f, 0.008f, 0.075f), glm::vec3(0.0f, glm::radians(-12.0f), 0.0f));
    const auto glassPaneTransform = GetRuntimeOverride(pretrainedLayout, "table_glass_pane", glm::vec3(-0.34f, 0.690f, 0.90f), glm::vec3(0.44f, 1.0f, 0.22f), glm::vec3(0.0f, glm::radians(-12.0f), 0.0f));
    const auto cupTransform = GetRuntimeOverride(pretrainedLayout, "table_warm_cup", glm::vec3(-0.22f, 0.715f, 1.02f), glm::vec3(0.038f, 0.30f, 0.038f), glm::vec3(0.0f));
    AddAssetLedRenderable(*m_registry, "RainPavilion_TableWarmTray", cubeMesh, trayTransform.position, trayTransform.scale, trayTransform.rotation, wetWood);
    AddAssetLedRenderable(*m_registry, "RainPavilion_TableGlassPane", planeMesh, glassPaneTransform.position, glassPaneTransform.scale, glassPaneTransform.rotation, glass);
    AddAssetLedRenderable(*m_registry, "RainPavilion_TableWarmCup", cylinderMesh, cupTransform.position, cupTransform.scale, cupTransform.rotation, warmMat);
    AddAssetLedRenderable(*m_registry, "RainPavilion_TableChromeRod", cylinderMesh, glm::vec3(-0.52f, 0.702f, 0.94f), glm::vec3(0.022f, 0.86f, 0.022f), glm::vec3(glm::radians(90.0f), 0.0f, glm::radians(-12.0f)), chrome);
    const auto vignetteTransform = pretrainedLayout.tabletopVignette.value_or(
        RuntimeLayoutTransform{glm::vec3(0.12f, 0.98f, 1.46f), glm::vec3(3.80f, 1.32f, 0.055f), glm::vec3(0.0f, glm::radians(-5.0f), 0.0f)});
    AddAssetLedRenderable(*m_registry, "RainPavilion_TabletopVignetteScreen", cubeMesh, vignetteTransform.position, vignetteTransform.scale, vignetteTransform.rotation, tabletopVignette);
    if (sphereMesh && sphereMesh->gpuBuffers) {
        AddAssetLedRenderable(*m_registry, "RainPavilion_TableChromeOrb", sphereMesh, glm::vec3(-0.60f, 0.725f, 0.78f), glm::vec3(0.055f), glm::vec3(0.0f), chrome);
        AddAssetLedRenderable(*m_registry, "RainPavilion_TableGlassBead", sphereMesh, glm::vec3(-0.10f, 0.720f, 0.74f), glm::vec3(0.050f), glm::vec3(0.0f), glass);
        AddAssetLedRenderable(*m_registry, "RainPavilion_TableSmallChromeBead", sphereMesh, glm::vec3(-0.34f, 0.708f, 0.66f), glm::vec3(0.034f), glm::vec3(0.0f), chrome);
    }
    if (pretrainedLanternMesh && pretrainedLanternMesh->gpuBuffers) {
        const auto transform = GetLayoutTransform(pretrainedLayout, "warm_fixture", glm::vec3(-0.54f, 0.705f, 0.82f), glm::vec3(0.48f), glm::vec3(0.0f, glm::radians(24.0f), 0.0f));
        AddAssetLedRenderable(*m_registry, "RainPavilion_PretrainedLanternCluster", pretrainedLanternMesh, transform.position, transform.scale, transform.rotation, pretrainedBrass);
    }
    if (pretrainedTableDressingMesh && pretrainedTableDressingMesh->gpuBuffers) {
        const auto transform = GetLayoutTransform(pretrainedLayout, "glass_accent", glm::vec3(0.06f, 0.710f, 1.05f), glm::vec3(0.50f), glm::vec3(0.0f, glm::radians(-14.0f), 0.0f));
        AddAssetLedRenderable(*m_registry, "RainPavilion_PretrainedGlassTableDressing", pretrainedTableDressingMesh, transform.position, transform.scale, transform.rotation, pretrainedGlassAccent);
    }
    if (pretrainedPuddleMesh && pretrainedPuddleMesh->gpuBuffers) {
        const auto transform = GetLayoutTransform(pretrainedLayout, "wet_contact", glm::vec3(-0.34f, 0.704f, 0.86f), glm::vec3(10.0f, 1.0f, 10.0f), glm::vec3(0.0f, glm::radians(8.0f), 0.0f));
        AddAssetLedRenderable(*m_registry, "RainPavilion_PretrainedPuddleFloorPatch", pretrainedPuddleMesh, transform.position, transform.scale, transform.rotation, pretrainedWetPatch);
    }
    if (pretrainedWindowPanelMesh && pretrainedWindowPanelMesh->gpuBuffers) {
        const auto transform = GetLayoutTransform(pretrainedLayout, "foreground_frame", glm::vec3(0.52f, 0.718f, 1.38f), glm::vec3(0.58f, 0.78f, 0.18f), glm::vec3(0.0f, glm::radians(-12.0f), 0.0f));
        AddAssetLedRenderable(*m_registry, "RainPavilion_PretrainedRainStreakPanel", pretrainedWindowPanelMesh, transform.position, transform.scale, transform.rotation, pretrainedGlassAccent);
    }
    if (bushMesh && bushMesh->gpuBuffers) {
        AddAssetLedNaturalisticRenderable(*m_registry, "RainPavilion_GardenBush", "wild_rooibos_bush", bushMesh, glm::vec3(-2.05f, 0.32f, 2.92f), glm::vec3(0.72f), glm::vec3(0.0f, glm::radians(12.0f), 0.0f), vegetation);
        AddAssetLedNaturalisticRenderable(*m_registry, "RainPavilion_GardenBush", "wild_rooibos_bush", bushMesh, glm::vec3(0.18f, 0.30f, 3.02f), glm::vec3(0.66f), glm::vec3(0.0f, glm::radians(-24.0f), 0.0f), vegetation);
        AddAssetLedNaturalisticRenderable(*m_registry, "RainPavilion_GardenBush", "wild_rooibos_bush", bushMesh, glm::vec3(2.15f, 0.32f, 2.86f), glm::vec3(0.78f), glm::vec3(0.0f, glm::radians(34.0f), 0.0f), vegetation);
    }
    if (fernMesh && fernMesh->gpuBuffers) {
        AddAssetLedNaturalisticRenderable(*m_registry, "RainPavilion_GardenFern", "fern_02", fernMesh, glm::vec3(-1.72f, 0.06f, 2.32f), glm::vec3(0.56f), glm::vec3(0.0f, glm::radians(18.0f), 0.0f), vegetation);
        AddAssetLedNaturalisticRenderable(*m_registry, "RainPavilion_GardenFern", "fern_02", fernMesh, glm::vec3(1.56f, 0.06f, 2.38f), glm::vec3(0.50f), glm::vec3(0.0f, glm::radians(-16.0f), 0.0f), vegetation);
    }
    AddParticleEffect(*m_registry, "RainPavilion_RainColumn", "rain", glm::vec3(1.35f, 2.7f, 1.55f));
    AddParticleEffect(*m_registry, "RainPavilion_Mist", "mist", glm::vec3(3.0f, 0.32f, 1.95f));
    AddAssetLedPointLight(*m_registry, "RainPavilion_WarmInteriorLight", glm::vec3(0.0f, 1.55f, 1.55f), glm::vec3(1.0f, 0.62f, 0.34f), 6.2f, 6.0f);
    AddAssetLedPointLight(*m_registry, "RainPavilion_TabletopFillLight", glm::vec3(-0.55f, 1.05f, 0.80f), glm::vec3(1.0f, 0.72f, 0.48f), 4.1f, 3.4f);
    AddAssetLedPointLight(*m_registry, "RainPavilion_CameraSoftFill", glm::vec3(-1.25f, 1.25f, -0.55f), glm::vec3(0.62f, 0.72f, 1.0f), 2.6f, 4.6f);
    AddAssetLedSpotLight(*m_registry, "RainPavilion_BlueRainKey", glm::vec3(-3.0f, 4.2f, -3.0f), glm::vec3(0.0f, 0.4f, 0.1f), glm::vec3(0.42f, 0.62f, 1.0f), 7.2f, 15.0f, false);
}

void Engine::BuildDesertRelicGalleryScene() {
    spdlog::info("Building asset-led scene: Desert Relic Gallery");

    auto* renderer = m_renderer.get();
    if (renderer) {
        renderer->SetLightingRigContract("desert_relic_sun", "scene_preset", false);
        renderer->SetWorldShaderPaletteContract("desert_relic_sun", "desert_relic_sun");
        renderer->SetEnvironmentPreset("cool_overcast");
        renderer->SetIBLEnabled(true);
        renderer->SetIBLIntensity(0.76f, 0.68f);
        renderer->SetBackgroundPresentation(false, 0.86f, 0.16f);
        renderer->SetSunDirection(glm::normalize(glm::vec3(0.58f, 0.68f, 0.22f)));
        renderer->SetSunColor(glm::vec3(1.0f, 0.82f, 0.52f));
        renderer->SetSunIntensity(3.6f);
        renderer->SetExposure(1.04f);
        renderer->SetBloomIntensity(0.08f);
        renderer->SetTAAEnabled(true);
        renderer->SetFXAAEnabled(true);
        renderer->SetSSREnabled(true);
        renderer->SetSSAOEnabled(true);
        renderer->SetFogEnabled(true);
        renderer->SetFogParams(0.010f, 0.0f, 0.58f);
        renderer->SetParticlesEnabled(false);
    }

    auto cubeMesh = Utils::MeshGenerator::CreateCube();
    auto torusMesh = Utils::MeshGenerator::CreateTorus(0.52f, 0.12f, 32, 12);
    auto planeMesh = Utils::MeshGenerator::CreatePlane(1.0f, 1.0f);
    auto cylinderMesh = Utils::MeshGenerator::CreateCylinder(0.5f, 1.0f, 32);
    auto coneMesh = Utils::MeshGenerator::CreateCone(0.5f, 1.0f, 32);
    auto sphereMesh = Utils::MeshGenerator::CreateSphere(0.5f, 32);
    auto boulderMesh = LoadNaturalisticShowcaseMesh("boulder_01/boulder_01_1k.gltf");
    auto branchMesh = LoadNaturalisticShowcaseMesh("dry_branches_medium_01/dry_branches_medium_01_1k.gltf");
    if (!UploadAssetLedMesh(renderer, cubeMesh, "cube") ||
        !UploadAssetLedMesh(renderer, torusMesh, "torus") ||
        !UploadAssetLedMesh(renderer, planeMesh, "plane") ||
        !UploadAssetLedMesh(renderer, cylinderMesh, "cylinder") ||
        !UploadAssetLedMesh(renderer, coneMesh, "cone") ||
        !UploadAssetLedMesh(renderer, sphereMesh, "sphere") ||
        !UploadAssetLedMesh(renderer, boulderMesh, "boulder_01") ||
        !UploadAssetLedMesh(renderer, branchMesh, "dry_branches_medium_01")) {
        return;
    }

    m_activeCameraEntity = AddAssetLedCamera(*m_registry,
        glm::vec3(-1.32f, 1.00f, -1.84f), glm::vec3(0.06f, 0.84f, 0.02f), 28.0f, 180.0f);

    const AssetLedMaterialSettings stone{glm::vec4(0.64f, 0.58f, 0.46f, 1.0f), 0.0f, 0.74f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.0f, 0.62f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "masonry"};
    const AssetLedMaterialSettings shadowStone{glm::vec4(0.34f, 0.30f, 0.24f, 1.0f), 0.0f, 0.82f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.0f, 0.72f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "masonry"};
    const AssetLedMaterialSettings warmStone{glm::vec4(0.70f, 0.52f, 0.30f, 1.0f), 0.0f, 0.78f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.0f, 0.66f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "masonry"};
    const AssetLedMaterialSettings tileBlue{glm::vec4(0.12f, 0.33f, 0.40f, 1.0f), 0.0f, 0.36f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.06f, 0.28f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "stained_tile"};
    const AssetLedMaterialSettings sand{glm::vec4(0.80f, 0.64f, 0.40f, 1.0f), 0.0f, 0.88f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.0f, 0.46f, true, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "sand"};
    const AssetLedMaterialSettings dryBrush{glm::vec4(0.34f, 0.24f, 0.15f, 1.0f), 0.0f, 0.76f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.0f, 0.35f, true, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "wood"};
    const AssetLedMaterialSettings bronze{glm::vec4(0.76f, 0.46f, 0.22f, 1.0f), 0.88f, 0.24f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.0f, 0.28f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "brushed_metal"};
    const AssetLedMaterialSettings glass{glm::vec4(0.35f, 0.68f, 0.92f, 0.48f), 0.0f, 0.05f, 0.45f, 1.45f, glm::vec3(0.0f), 1.0f, 0.0f, 0.12f, true, Scene::RenderableComponent::AlphaMode::Blend, Scene::RenderableComponent::RenderLayer::Opaque, "glass"};
    const AssetLedMaterialSettings ceramic{glm::vec4(0.64f, 0.42f, 0.30f, 1.0f), 0.0f, 0.42f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.0f, 0.18f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "ceramic"};

    AddAssetLedRenderable(*m_registry, "DesertRelic_SandFloor", planeMesh, glm::vec3(0.0f, -0.02f, 0.0f), glm::vec3(12.0f, 1.0f, 10.0f), glm::vec3(0.0f), sand);
    AddAssetLedRenderable(*m_registry, "DesertRelic_DuneBackdrop", cubeMesh, glm::vec3(-1.2f, 0.20f, 4.2f), glm::vec3(4.8f, 0.34f, 1.05f), glm::vec3(0.0f, glm::radians(5.0f), 0.0f), sand);
    AddAssetLedRenderable(*m_registry, "DesertRelic_ForegroundSandLip", cubeMesh, glm::vec3(-1.34f, 0.030f, -2.10f), glm::vec3(0.92f, 0.045f, 0.20f), glm::vec3(0.0f, glm::radians(-16.0f), 0.0f), sand);
    AddAssetLedRenderable(*m_registry, "DesertRelic_LeftRuinReturn", cubeMesh, glm::vec3(-5.7f, 0.52f, 1.58f), glm::vec3(0.24f, 1.02f, 2.20f), glm::vec3(0.0f, glm::radians(7.0f), 0.0f), stone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_RightRuinReturn", cubeMesh, glm::vec3(5.7f, 0.38f, 1.82f), glm::vec3(0.18f, 0.74f, 1.35f), glm::vec3(0.0f, glm::radians(-7.0f), 0.0f), stone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_MainPlinth", cubeMesh, glm::vec3(0.0f, 0.25f, 0.0f), glm::vec3(1.62f, 0.50f, 0.92f), glm::vec3(0.0f), stone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_PlinthStepFront", cubeMesh, glm::vec3(0.0f, 0.10f, -0.80f), glm::vec3(2.05f, 0.20f, 0.26f), glm::vec3(0.0f), warmStone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_PlinthStepRear", cubeMesh, glm::vec3(0.0f, 0.15f, 0.68f), glm::vec3(1.86f, 0.22f, 0.22f), glm::vec3(0.0f), stone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_PlinthCreviceA", cubeMesh, glm::vec3(-0.52f, 0.615f, -0.18f), glm::vec3(0.035f, 0.025f, 0.96f), glm::vec3(0.0f, glm::radians(-18.0f), 0.0f), shadowStone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_PlinthCreviceB", cubeMesh, glm::vec3(0.72f, 0.618f, 0.20f), glm::vec3(0.030f, 0.025f, 0.72f), glm::vec3(0.0f, glm::radians(28.0f), 0.0f), shadowStone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_PlinthEdgeShadow", cubeMesh, glm::vec3(0.0f, 0.622f, -0.58f), glm::vec3(1.72f, 0.022f, 0.035f), glm::vec3(0.0f), shadowStone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_FrontPlinthChippedLeft", cubeMesh, glm::vec3(-0.74f, 0.27f, -0.492f), glm::vec3(0.26f, 0.18f, 0.030f), glm::vec3(0.0f, glm::radians(-2.0f), glm::radians(1.5f)), shadowStone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_FrontPlinthChippedRight", cubeMesh, glm::vec3(0.70f, 0.22f, -0.496f), glm::vec3(0.22f, 0.13f, 0.030f), glm::vec3(0.0f, glm::radians(2.0f), glm::radians(-1.0f)), warmStone);
    for (int i = 0; i < 7; ++i) {
        const float x = -0.92f + static_cast<float>(i) * 0.28f;
        const AssetLedMaterialSettings& tileMat = (i % 3 == 1) ? warmStone : tileBlue;
        AddAssetLedRenderable(*m_registry, "DesertRelic_FrontMosaicBand", cubeMesh, glm::vec3(x, 0.38f, -0.622f), glm::vec3(0.18f, 0.095f, 0.025f), glm::vec3(0.0f), tileMat);
    }
    for (int i = 0; i < 6; ++i) {
        const float x = -0.94f + static_cast<float>(i) * 0.32f;
        const AssetLedMaterialSettings& chipMat = (i % 2 == 0) ? warmStone : shadowStone;
        AddAssetLedRenderable(*m_registry, "DesertRelic_FrontStoneChip", cubeMesh, glm::vec3(x, 0.64f, -0.56f), glm::vec3(0.11f, 0.035f, 0.045f), glm::vec3(0.0f, glm::radians(static_cast<float>(i) * 9.0f), 0.0f), chipMat);
    }
    AddAssetLedRenderable(*m_registry, "DesertRelic_LeftBrokenBlock", cubeMesh, glm::vec3(-1.55f, 0.18f, 0.58f), glm::vec3(0.74f, 0.36f, 0.46f), glm::vec3(0.0f, glm::radians(16.0f), 0.0f), stone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_RightBrokenBlock", cubeMesh, glm::vec3(1.55f, 0.16f, -0.65f), glm::vec3(0.62f, 0.32f, 0.42f), glm::vec3(0.0f, glm::radians(-20.0f), 0.0f), stone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_BronzeRing", torusMesh, glm::vec3(0.15f, 0.90f, 0.0f), glm::vec3(0.72f), glm::vec3(glm::radians(74.0f), glm::radians(18.0f), 0.0f), bronze);
    AddAssetLedRenderable(*m_registry, "DesertRelic_GlassInlay", cubeMesh, glm::vec3(-0.65f, 0.68f, 0.03f), glm::vec3(0.42f, 0.08f, 0.42f), glm::vec3(0.0f, glm::radians(22.0f), 0.0f), glass);
    AddAssetLedRenderable(*m_registry, "DesertRelic_CeramicVesselLeft", sphereMesh, glm::vec3(-0.88f, 0.48f, -0.24f), glm::vec3(0.18f, 0.26f, 0.18f), glm::vec3(0.0f, glm::radians(-14.0f), 0.0f), ceramic);
    AddAssetLedRenderable(*m_registry, "DesertRelic_CeramicVesselRight", sphereMesh, glm::vec3(0.94f, 0.43f, 0.32f), glm::vec3(0.15f, 0.22f, 0.15f), glm::vec3(0.0f, glm::radians(20.0f), 0.0f), ceramic);
    AddAssetLedRenderable(*m_registry, "DesertRelic_BronzePedestal", cylinderMesh, glm::vec3(0.15f, 0.52f, 0.0f), glm::vec3(0.42f, 0.18f, 0.42f), glm::vec3(0.0f), bronze);
    AddAssetLedRenderable(*m_registry, "DesertRelic_SandDriftFront", cubeMesh, glm::vec3(-0.62f, 0.028f, -1.12f), glm::vec3(1.20f, 0.045f, 0.26f), glm::vec3(0.0f, glm::radians(-12.0f), glm::radians(1.5f)), sand);
    AddAssetLedRenderable(*m_registry, "DesertRelic_LeftSandRamp", cubeMesh, glm::vec3(-1.65f, 0.052f, -0.72f), glm::vec3(0.96f, 0.055f, 0.42f), glm::vec3(0.0f, glm::radians(18.0f), glm::radians(3.0f)), sand);
    AddAssetLedRenderable(*m_registry, "DesertRelic_RightSandRamp", cubeMesh, glm::vec3(1.64f, 0.048f, 0.72f), glm::vec3(0.82f, 0.052f, 0.38f), glm::vec3(0.0f, glm::radians(-16.0f), glm::radians(-2.5f)), sand);
    AddAssetLedRenderable(*m_registry, "DesertRelic_LeftCoolShadowPatch", cubeMesh, glm::vec3(-2.35f, 0.015f, -1.35f), glm::vec3(1.70f, 0.025f, 0.70f), glm::vec3(0.0f, glm::radians(22.0f), 0.0f), shadowStone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_RearCoolShadowPatch", cubeMesh, glm::vec3(1.25f, 0.018f, 1.42f), glm::vec3(2.25f, 0.025f, 0.52f), glm::vec3(0.0f, glm::radians(-14.0f), 0.0f), shadowStone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_LeftBrokenColumnBase", cylinderMesh, glm::vec3(-2.55f, 0.22f, -0.70f), glm::vec3(0.42f, 0.42f, 0.42f), glm::vec3(0.0f), stone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_LeftBrokenColumnStump", cylinderMesh, glm::vec3(-2.55f, 0.72f, -0.70f), glm::vec3(0.32f, 0.92f, 0.32f), glm::vec3(0.0f, glm::radians(0.0f), glm::radians(-4.0f)), stone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_RightFallenColumn", cylinderMesh, glm::vec3(2.34f, 0.23f, -0.18f), glm::vec3(0.24f, 1.36f, 0.24f), glm::vec3(glm::radians(86.0f), glm::radians(-24.0f), glm::radians(0.0f)), stone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_BackBrokenWallLeft", cubeMesh, glm::vec3(-2.45f, 0.68f, 1.48f), glm::vec3(1.05f, 1.28f, 0.28f), glm::vec3(0.0f, glm::radians(10.0f), 0.0f), stone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_BackBrokenWallRight", cubeMesh, glm::vec3(2.50f, 0.44f, 1.66f), glm::vec3(0.46f, 0.72f, 0.20f), glm::vec3(0.0f, glm::radians(-8.0f), glm::radians(3.0f)), stone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_BackHighRuinLeft", cubeMesh, glm::vec3(-2.95f, 0.86f, 2.94f), glm::vec3(0.76f, 0.72f, 0.20f), glm::vec3(0.0f, glm::radians(7.0f), glm::radians(-3.0f)), stone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_BackHighRuinRight", cubeMesh, glm::vec3(2.95f, 0.70f, 2.88f), glm::vec3(0.54f, 0.56f, 0.18f), glm::vec3(0.0f, glm::radians(-7.0f), glm::radians(4.0f)), stone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_BackHighLintel", cubeMesh, glm::vec3(-1.06f, 1.04f, 2.96f), glm::vec3(0.52f, 0.080f, 0.16f), glm::vec3(0.0f, glm::radians(1.5f), glm::radians(4.0f)), warmStone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_BackHighLintelBrokenRight", cubeMesh, glm::vec3(1.36f, 0.94f, 2.92f), glm::vec3(0.24f, 0.070f, 0.15f), glm::vec3(0.0f, glm::radians(-5.0f), glm::radians(-6.0f)), stone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_LeftShadowAlcove", cubeMesh, glm::vec3(-3.55f, 0.64f, 0.35f), glm::vec3(0.28f, 1.26f, 1.05f), glm::vec3(0.0f, glm::radians(14.0f), 0.0f), stone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_RightShadowAlcove", cubeMesh, glm::vec3(3.46f, 0.48f, 0.18f), glm::vec3(0.22f, 0.94f, 0.78f), glm::vec3(0.0f, glm::radians(-12.0f), glm::radians(2.0f)), stone);
    for (int i = 0; i < 3; ++i) {
        const float x = -3.2f + static_cast<float>(i) * 3.2f;
        AddAssetLedRenderable(*m_registry, "DesertRelic_ArchColumn", cubeMesh, glm::vec3(x, 0.72f, 2.2f), glm::vec3(0.26f, 1.32f, 0.32f), glm::vec3(0.0f), stone);
        AddAssetLedRenderable(*m_registry, "DesertRelic_RoundColumnCore", cylinderMesh, glm::vec3(x, 0.76f, 2.0f), glm::vec3(0.24f, 1.36f, 0.24f), glm::vec3(0.0f), stone);
    }
    AddAssetLedRenderable(*m_registry, "DesertRelic_ArchLintel", cubeMesh, glm::vec3(-0.26f, 1.02f, 2.2f), glm::vec3(1.16f, 0.070f, 0.18f), glm::vec3(0.0f, glm::radians(-1.0f), glm::radians(1.5f)), warmStone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_BrokenArchCapLeft", cubeMesh, glm::vec3(-2.08f, 1.06f, 2.15f), glm::vec3(0.22f, 0.060f, 0.18f), glm::vec3(0.0f, glm::radians(-7.0f), glm::radians(5.0f)), stone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_BrokenArchCapRight", cubeMesh, glm::vec3(1.92f, 1.00f, 2.15f), glm::vec3(0.20f, 0.055f, 0.16f), glm::vec3(0.0f, glm::radians(8.0f), glm::radians(-4.0f)), stone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_DistantSpireLeft", coneMesh, glm::vec3(-4.6f, 1.62f, 3.3f), glm::vec3(0.55f, 1.35f, 0.55f), glm::vec3(0.0f, glm::radians(8.0f), 0.0f), stone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_DistantSpireRight", coneMesh, glm::vec3(4.45f, 1.35f, 3.45f), glm::vec3(0.42f, 1.05f, 0.42f), glm::vec3(0.0f, glm::radians(-12.0f), 0.0f), stone);
    AddAssetLedRenderable(*m_registry, "DesertRelic_BackWallLow", cubeMesh, glm::vec3(0.0f, 0.48f, 2.55f), glm::vec3(6.2f, 0.74f, 0.20f), glm::vec3(0.0f), shadowStone);
    if (boulderMesh && boulderMesh->gpuBuffers) {
        AddAssetLedNaturalisticRenderable(*m_registry, "DesertRelic_GroundedBoulderClusterLeft", "boulder_01", boulderMesh, glm::vec3(-2.15f, 0.05f, -1.12f), glm::vec3(0.26f), glm::vec3(0.0f, glm::radians(34.0f), 0.0f), stone);
        AddAssetLedNaturalisticRenderable(*m_registry, "DesertRelic_GroundedBoulderClusterRight", "boulder_01", boulderMesh, glm::vec3(2.16f, 0.04f, 0.95f), glm::vec3(0.22f), glm::vec3(0.0f, glm::radians(-42.0f), 0.0f), stone);
        AddAssetLedNaturalisticRenderable(*m_registry, "DesertRelic_ForegroundStoneScatter", "boulder_01", boulderMesh, glm::vec3(-1.42f, 0.02f, -1.42f), glm::vec3(0.11f), glm::vec3(0.0f, glm::radians(70.0f), 0.0f), stone);
        AddAssetLedNaturalisticRenderable(*m_registry, "DesertRelic_PlinthStoneAnchor", "boulder_01", boulderMesh, glm::vec3(1.36f, 0.08f, -0.72f), glm::vec3(0.24f), glm::vec3(0.0f, glm::radians(8.0f), glm::radians(-5.0f)), stone);
    }
    if (branchMesh && branchMesh->gpuBuffers) {
        AddAssetLedNaturalisticRenderable(*m_registry, "DesertRelic_DryBrushCluster", "dry_branches_medium_01", branchMesh, glm::vec3(-2.72f, 0.08f, 0.42f), glm::vec3(0.42f), glm::vec3(glm::radians(2.0f), glm::radians(28.0f), glm::radians(-4.0f)), dryBrush);
    }
    AddAssetLedSpotLight(*m_registry, "DesertRelic_WarmKey", glm::vec3(-3.2f, 5.0f, -3.5f), glm::vec3(0.0f, 0.55f, 0.0f), glm::vec3(1.0f, 0.82f, 0.52f), 5.5f, 20.0f, false);
}

void Engine::BuildNeonAlleyMaterialMarketScene() {
    spdlog::info("Building asset-led scene: Neon Alley Material Market");

    auto* renderer = m_renderer.get();
    if (renderer) {
        renderer->SetLightingRigContract("neon_market_rain", "scene_preset", false);
        renderer->SetWorldShaderPaletteContract("neon_market_rain", "neon_market_rain");
        renderer->SetEnvironmentPreset("studio");
        renderer->SetIBLEnabled(true);
        renderer->SetIBLIntensity(0.44f, 0.56f);
        renderer->SetBackgroundPresentation(false, 0.45f, 0.40f);
        renderer->SetSunDirection(glm::normalize(glm::vec3(-0.16f, 0.72f, 0.38f)));
        renderer->SetSunColor(glm::vec3(0.12f, 0.42f, 0.88f));
        renderer->SetSunIntensity(1.35f);
        renderer->SetExposure(1.06f);
        renderer->SetBloomIntensity(0.42f);
        renderer->SetBloomShape(0.75f, 0.58f, 2.25f);
        renderer->SetCinematicPostEnabled(true);
        renderer->SetCinematicPost(0.18f, 0.24f);
        renderer->SetToneMapperPreset("filmic_soft");
        renderer->SetColorGrade(0.10f, 0.22f);
        renderer->SetTAAEnabled(true);
        renderer->SetFXAAEnabled(true);
        renderer->SetSSREnabled(true);
        renderer->SetSSAOEnabled(true);
        renderer->SetFogEnabled(true);
        renderer->SetFogParams(0.026f, 0.0f, 0.42f);
        renderer->SetParticlesEnabled(true);
        renderer->SetRTReflectionsEnabled(true);
    }

    auto cubeMesh = Utils::MeshGenerator::CreateCube();
    auto planeMesh = Utils::MeshGenerator::CreatePlane(1.0f, 1.0f);
    auto tableMesh = LoadNaturalisticShowcaseMesh("WoodenTable_01/WoodenTable_01_1k.gltf");
    auto barrelMesh = LoadNaturalisticShowcaseMesh("Barrel_01/Barrel_01_1k.gltf");
    if (!UploadAssetLedMesh(renderer, cubeMesh, "cube") ||
        !UploadAssetLedMesh(renderer, planeMesh, "plane") ||
        !UploadAssetLedMesh(renderer, tableMesh, "WoodenTable_01") ||
        !UploadAssetLedMesh(renderer, barrelMesh, "Barrel_01")) {
        return;
    }

    m_activeCameraEntity = AddAssetLedCamera(*m_registry,
        glm::vec3(-1.20f, 1.05f, -2.20f), glm::vec3(1.15f, 0.90f, -0.10f), 34.0f, 120.0f);

    const AssetLedMaterialSettings wetAsphalt{glm::vec4(0.052f, 0.056f, 0.066f, 1.0f), 0.0f, 0.38f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.54f, 0.34f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "wet_masonry"};
    const AssetLedMaterialSettings alleyBrick{glm::vec4(0.100f, 0.088f, 0.076f, 1.0f), 0.0f, 0.58f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.06f, 0.26f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "wet_masonry"};
    const AssetLedMaterialSettings blackenedMetal{glm::vec4(0.038f, 0.044f, 0.052f, 1.0f), 0.65f, 0.30f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.22f, 0.18f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "soot_grime"};
    const AssetLedMaterialSettings neonPink{glm::vec4(1.0f, 0.12f, 0.48f, 1.0f), 0.0f, 0.18f, 0.0f, 1.5f, glm::vec3(1.0f, 0.12f, 0.48f), 3.1f, 0.0f, 0.1f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "emissive"};
    const AssetLedMaterialSettings neonCyan{glm::vec4(0.08f, 0.82f, 0.72f, 1.0f), 0.0f, 0.18f, 0.0f, 1.5f, glm::vec3(0.08f, 0.82f, 0.72f), 2.2f, 0.0f, 0.1f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "emissive"};
    const AssetLedMaterialSettings neonAmber{glm::vec4(1.0f, 0.58f, 0.14f, 1.0f), 0.0f, 0.20f, 0.0f, 1.5f, glm::vec3(1.0f, 0.42f, 0.08f), 1.55f, 0.0f, 0.08f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "emissive"};
    const AssetLedMaterialSettings signMask{glm::vec4(0.012f, 0.016f, 0.022f, 1.0f), 0.0f, 0.32f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.22f, 0.2f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "soot_grime"};
    const AssetLedMaterialSettings glass{glm::vec4(0.32f, 0.52f, 0.66f, 0.30f), 0.0f, 0.10f, 0.38f, 1.45f, glm::vec3(0.0f), 1.0f, 0.28f, 0.16f, true, Scene::RenderableComponent::AlphaMode::Blend, Scene::RenderableComponent::RenderLayer::Opaque, "glass"};
    const AssetLedMaterialSettings chrome{glm::vec4(0.72f, 0.82f, 0.86f, 1.0f), 1.0f, 0.08f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.55f, 0.2f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "chrome"};

    AddAssetLedRenderable(*m_registry, "NeonMarket_WetAlleyPlane", cubeMesh, glm::vec3(0.0f, -0.04f, -0.25f), glm::vec3(5.8f, 0.08f, 8.2f), glm::vec3(0.0f), wetAsphalt);
    AddAssetLedRenderable(*m_registry, "NeonMarket_LeftCurbStrip", cubeMesh, glm::vec3(-1.75f, 0.03f, -0.35f), glm::vec3(0.16f, 0.14f, 7.6f), glm::vec3(0.0f), chrome);
    AddAssetLedRenderable(*m_registry, "NeonMarket_RightCurbStrip", cubeMesh, glm::vec3(1.75f, 0.03f, -0.35f), glm::vec3(0.16f, 0.14f, 7.6f), glm::vec3(0.0f), chrome);
    AddAssetLedRenderable(*m_registry, "NeonMarket_RearAlleyWall", cubeMesh, glm::vec3(0.0f, 1.28f, 3.20f), glm::vec3(5.4f, 2.55f, 0.32f), glm::vec3(0.0f), alleyBrick);
    AddAssetLedRenderable(*m_registry, "NeonMarket_RearLowerServiceDoor", cubeMesh, glm::vec3(-0.85f, 0.78f, 3.02f), glm::vec3(0.75f, 1.45f, 0.08f), glm::vec3(0.0f), blackenedMetal);
    AddAssetLedRenderable(*m_registry, "NeonMarket_RearMenuBoard", cubeMesh, glm::vec3(0.62f, 1.34f, 2.98f), glm::vec3(0.62f, 0.42f, 0.055f), glm::vec3(0.0f), neonPink);
    AddAssetLedRenderable(*m_registry, "NeonMarket_RearMenuGlyphA", cubeMesh, glm::vec3(0.30f, 1.42f, 2.935f), glm::vec3(0.22f, 0.030f, 0.030f), glm::vec3(0.0f), signMask);
    AddAssetLedRenderable(*m_registry, "NeonMarket_RearMenuGlyphB", cubeMesh, glm::vec3(0.68f, 1.30f, 2.935f), glm::vec3(0.28f, 0.030f, 0.030f), glm::vec3(0.0f), signMask);
    AddAssetLedRenderable(*m_registry, "NeonMarket_RearMenuGlyphC", cubeMesh, glm::vec3(0.92f, 1.42f, 2.935f), glm::vec3(0.040f, 0.14f, 0.030f), glm::vec3(0.0f), signMask);
    AddAssetLedRenderable(*m_registry, "NeonMarket_RearPipeStackA", cubeMesh, glm::vec3(1.05f, 1.32f, 2.98f), glm::vec3(0.08f, 2.15f, 0.08f), glm::vec3(0.0f), chrome);
    AddAssetLedRenderable(*m_registry, "NeonMarket_RearPipeStackB", cubeMesh, glm::vec3(1.26f, 1.02f, 2.96f), glm::vec3(0.055f, 1.55f, 0.055f), glm::vec3(0.0f), blackenedMetal);
    AddAssetLedRenderable(*m_registry, "NeonMarket_OverheadBeamFront", cubeMesh, glm::vec3(0.0f, 2.54f, -2.20f), glm::vec3(4.7f, 0.13f, 0.14f), glm::vec3(0.0f), blackenedMetal);
    AddAssetLedRenderable(*m_registry, "NeonMarket_OverheadBeamMid", cubeMesh, glm::vec3(0.0f, 2.50f, -0.25f), glm::vec3(4.4f, 0.10f, 0.10f), glm::vec3(0.0f), blackenedMetal);
    AddAssetLedRenderable(*m_registry, "NeonMarket_OverheadBeamRear", cubeMesh, glm::vec3(0.0f, 2.46f, 1.75f), glm::vec3(4.2f, 0.10f, 0.12f), glm::vec3(0.0f), blackenedMetal);
    AddAssetLedRenderable(*m_registry, "NeonMarket_LeftStorefront", cubeMesh, glm::vec3(-2.25f, 1.1f, -0.05f), glm::vec3(0.38f, 2.2f, 4.6f), glm::vec3(0.0f), alleyBrick);
    AddAssetLedRenderable(*m_registry, "NeonMarket_RightStorefront", cubeMesh, glm::vec3(2.25f, 1.05f, 0.2f), glm::vec3(0.34f, 2.1f, 4.4f), glm::vec3(0.0f), alleyBrick);
    AddAssetLedRenderable(*m_registry, "NeonMarket_LeftAwning", cubeMesh, glm::vec3(-1.95f, 1.58f, -0.65f), glm::vec3(0.58f, 0.08f, 2.70f), glm::vec3(0.0f), blackenedMetal);
    AddAssetLedRenderable(*m_registry, "NeonMarket_RightAwning", cubeMesh, glm::vec3(1.92f, 1.36f, 0.72f), glm::vec3(0.54f, 0.08f, 2.25f), glm::vec3(0.0f), blackenedMetal);
    AddAssetLedRenderable(*m_registry, "NeonMarket_RightInsetPanel", cubeMesh, glm::vec3(1.98f, 1.03f, -0.55f), glm::vec3(0.065f, 0.88f, 1.22f), glm::vec3(0.0f), signMask);
    AddAssetLedRenderable(*m_registry, "NeonMarket_RightFacadeTrimTop", cubeMesh, glm::vec3(1.90f, 1.36f, -0.55f), glm::vec3(0.052f, 0.035f, 1.18f), glm::vec3(0.0f), chrome);
    AddAssetLedRenderable(*m_registry, "NeonMarket_RightFacadeTrimBottom", cubeMesh, glm::vec3(1.90f, 0.66f, -0.55f), glm::vec3(0.052f, 0.030f, 1.10f), glm::vec3(0.0f), chrome);
    AddAssetLedRenderable(*m_registry, "NeonMarket_RightFacadeInsetBreakA", cubeMesh, glm::vec3(1.91f, 1.10f, -1.10f), glm::vec3(0.046f, 0.24f, 0.020f), glm::vec3(0.0f), blackenedMetal);
    AddAssetLedRenderable(*m_registry, "NeonMarket_RightFacadeInsetBreakB", cubeMesh, glm::vec3(1.91f, 0.88f, 0.10f), glm::vec3(0.046f, 0.19f, 0.020f), glm::vec3(0.0f), blackenedMetal);
    AddAssetLedRenderable(*m_registry, "NeonMarket_RightServiceShelf", cubeMesh, glm::vec3(1.86f, 0.82f, -0.44f), glm::vec3(0.42f, 0.045f, 1.06f), glm::vec3(0.0f), chrome);
    AddAssetLedRenderable(*m_registry, "NeonMarket_RightCyanPriceTabs", cubeMesh, glm::vec3(1.82f, 1.08f, -0.82f), glm::vec3(0.052f, 0.13f, 0.22f), glm::vec3(0.0f), neonCyan);
    AddAssetLedRenderable(*m_registry, "NeonMarket_RightAmberPriceTabs", cubeMesh, glm::vec3(1.82f, 0.86f, -0.16f), glm::vec3(0.052f, 0.10f, 0.28f), glm::vec3(0.0f), neonAmber);
    AddAssetLedRenderable(*m_registry, "NeonMarket_RightLowDisplayCase", cubeMesh, glm::vec3(1.52f, 0.39f, -0.56f), glm::vec3(0.40f, 0.23f, 0.66f), glm::vec3(0.0f, glm::radians(4.0f), 0.0f), glass);
    AddAssetLedRenderable(*m_registry, "NeonMarket_RightLowDisplayTrim", cubeMesh, glm::vec3(1.52f, 0.56f, -0.56f), glm::vec3(0.45f, 0.030f, 0.72f), glm::vec3(0.0f, glm::radians(4.0f), 0.0f), chrome);
    AddAssetLedRenderable(*m_registry, "NeonMarket_RightDisplayProductCyan", cubeMesh, glm::vec3(1.30f, 0.50f, -0.88f), glm::vec3(0.052f, 0.075f, 0.064f), glm::vec3(0.0f, glm::radians(4.0f), 0.0f), neonCyan);
    AddAssetLedRenderable(*m_registry, "NeonMarket_RightDisplayProductAmber", cubeMesh, glm::vec3(1.52f, 0.49f, -0.50f), glm::vec3(0.070f, 0.040f, 0.075f), glm::vec3(0.0f, glm::radians(4.0f), 0.0f), neonAmber);
    AddAssetLedRenderable(*m_registry, "NeonMarket_RightDisplayProductDark", cubeMesh, glm::vec3(1.72f, 0.48f, -0.18f), glm::vec3(0.048f, 0.060f, 0.064f), glm::vec3(0.0f, glm::radians(4.0f), 0.0f), blackenedMetal);
    AddAssetLedRenderable(*m_registry, "NeonMarket_MountedPinkSign", cubeMesh, glm::vec3(-2.02f, 2.1f, -0.85f), glm::vec3(0.10f, 0.38f, 1.35f), glm::vec3(0.0f), neonPink);
    AddAssetLedRenderable(*m_registry, "NeonMarket_MountedCyanSign", cubeMesh, glm::vec3(2.02f, 1.72f, 0.95f), glm::vec3(0.10f, 0.32f, 1.15f), glm::vec3(0.0f), neonCyan);
    AddAssetLedRenderable(*m_registry, "NeonMarket_CyanSignGlyphA", cubeMesh, glm::vec3(1.94f, 1.77f, 0.58f), glm::vec3(0.035f, 0.055f, 0.30f), glm::vec3(0.0f), signMask);
    AddAssetLedRenderable(*m_registry, "NeonMarket_CyanSignGlyphB", cubeMesh, glm::vec3(1.94f, 1.66f, 1.00f), glm::vec3(0.035f, 0.055f, 0.34f), glm::vec3(0.0f), signMask);
    AddAssetLedRenderable(*m_registry, "NeonMarket_CyanSignGlyphC", cubeMesh, glm::vec3(1.94f, 1.78f, 1.34f), glm::vec3(0.035f, 0.16f, 0.045f), glm::vec3(0.0f), signMask);
    AddAssetLedRenderable(*m_registry, "NeonMarket_PinkSignGlyphA", cubeMesh, glm::vec3(-1.94f, 2.16f, -1.18f), glm::vec3(0.035f, 0.05f, 0.28f), glm::vec3(0.0f), signMask);
    AddAssetLedRenderable(*m_registry, "NeonMarket_PinkSignGlyphB", cubeMesh, glm::vec3(-1.94f, 2.04f, -0.72f), glm::vec3(0.035f, 0.14f, 0.045f), glm::vec3(0.0f), signMask);
    AddAssetLedRenderable(*m_registry, "NeonMarket_SmallAmberBladeSign", cubeMesh, glm::vec3(-2.04f, 1.38f, 1.35f), glm::vec3(0.08f, 0.22f, 0.72f), glm::vec3(0.0f), neonAmber);
    AddAssetLedRenderable(*m_registry, "NeonMarket_AmberBladeGlyphA", cubeMesh, glm::vec3(-1.98f, 1.43f, 1.15f), glm::vec3(0.035f, 0.05f, 0.18f), glm::vec3(0.0f), signMask);
    AddAssetLedRenderable(*m_registry, "NeonMarket_AmberBladeGlyphB", cubeMesh, glm::vec3(-1.98f, 1.33f, 1.55f), glm::vec3(0.035f, 0.045f, 0.20f), glm::vec3(0.0f), signMask);
    AddAssetLedRenderable(*m_registry, "NeonMarket_LeftStallGlowStrip", cubeMesh, glm::vec3(-1.86f, 0.88f, -0.20f), glm::vec3(0.045f, 0.050f, 1.26f), glm::vec3(0.0f), neonAmber);
    AddAssetLedRenderable(*m_registry, "NeonMarket_LeftStallLowerCyan", cubeMesh, glm::vec3(-1.84f, 0.52f, -0.34f), glm::vec3(0.040f, 0.035f, 0.78f), glm::vec3(0.0f), neonCyan);
    AddAssetLedRenderable(*m_registry, "NeonMarket_PinkSignBracketTop", cubeMesh, glm::vec3(-2.06f, 2.37f, -0.85f), glm::vec3(0.22f, 0.05f, 1.52f), glm::vec3(0.0f), chrome);
    AddAssetLedRenderable(*m_registry, "NeonMarket_PinkSignBracketBottom", cubeMesh, glm::vec3(-2.06f, 1.83f, -0.85f), glm::vec3(0.22f, 0.05f, 1.52f), glm::vec3(0.0f), chrome);
    AddAssetLedRenderable(*m_registry, "NeonMarket_CyanSignBracketTop", cubeMesh, glm::vec3(2.06f, 1.95f, 0.95f), glm::vec3(0.22f, 0.05f, 1.32f), glm::vec3(0.0f), chrome);
    AddAssetLedRenderable(*m_registry, "NeonMarket_CyanSignBracketBottom", cubeMesh, glm::vec3(2.06f, 1.49f, 0.95f), glm::vec3(0.22f, 0.05f, 1.32f), glm::vec3(0.0f), chrome);
    AddAssetLedRenderable(*m_registry, "NeonMarket_DisplayGlass", cubeMesh, glm::vec3(0.58f, 0.58f, -1.14f), glm::vec3(1.02f, 0.46f, 0.46f), glm::vec3(0.0f, glm::radians(-6.0f), 0.0f), glass);
    AddAssetLedRenderable(*m_registry, "NeonMarket_DisplayChromeTrim", cubeMesh, glm::vec3(0.58f, 0.85f, -1.14f), glm::vec3(1.12f, 0.045f, 0.52f), glm::vec3(0.0f, glm::radians(-6.0f), 0.0f), chrome);
    AddAssetLedRenderable(*m_registry, "NeonMarket_DisplayBase", cubeMesh, glm::vec3(0.58f, 0.17f, -1.14f), glm::vec3(1.18f, 0.34f, 0.60f), glm::vec3(0.0f, glm::radians(-6.0f), 0.0f), wetAsphalt);
    AddAssetLedRenderable(*m_registry, "NeonMarket_DisplayInnerAmberShelf", cubeMesh, glm::vec3(0.54f, 0.54f, -1.16f), glm::vec3(0.76f, 0.035f, 0.26f), glm::vec3(0.0f, glm::radians(-6.0f), 0.0f), neonAmber);
    AddAssetLedRenderable(*m_registry, "NeonMarket_DisplayProductCyan", cubeMesh, glm::vec3(0.24f, 0.63f, -1.20f), glm::vec3(0.060f, 0.080f, 0.052f), glm::vec3(0.0f, glm::radians(-6.0f), 0.0f), neonCyan);
    AddAssetLedRenderable(*m_registry, "NeonMarket_DisplayProductPink", cubeMesh, glm::vec3(0.58f, 0.62f, -1.16f), glm::vec3(0.075f, 0.050f, 0.065f), glm::vec3(0.0f, glm::radians(-6.0f), 0.0f), neonPink);
    AddAssetLedRenderable(*m_registry, "NeonMarket_DisplayProductChrome", cubeMesh, glm::vec3(0.88f, 0.61f, -1.10f), glm::vec3(0.052f, 0.060f, 0.052f), glm::vec3(0.0f, glm::radians(-6.0f), 0.0f), chrome);
    AddAssetLedRenderable(*m_registry, "NeonMarket_DisplayLeftPost", cubeMesh, glm::vec3(0.00f, 0.50f, -1.21f), glm::vec3(0.045f, 0.56f, 0.052f), glm::vec3(0.0f, glm::radians(-6.0f), 0.0f), chrome);
    AddAssetLedRenderable(*m_registry, "NeonMarket_DisplayRightPost", cubeMesh, glm::vec3(1.13f, 0.50f, -1.08f), glm::vec3(0.045f, 0.56f, 0.052f), glm::vec3(0.0f, glm::radians(-6.0f), 0.0f), chrome);
    AddAssetLedRenderable(*m_registry, "NeonMarket_PuddleBreakupA", cubeMesh, glm::vec3(-0.92f, 0.012f, -1.88f), glm::vec3(0.72f, 0.018f, 0.38f), glm::vec3(0.0f, glm::radians(8.0f), 0.0f), blackenedMetal);
    AddAssetLedRenderable(*m_registry, "NeonMarket_PuddleBreakupB", cubeMesh, glm::vec3(0.82f, 0.014f, 0.42f), glm::vec3(0.88f, 0.018f, 0.30f), glm::vec3(0.0f, glm::radians(-16.0f), 0.0f), blackenedMetal);
    for (int i = 0; i < 5; ++i) {
        const float z = -1.72f + static_cast<float>(i) * 0.86f;
        AddAssetLedRenderable(*m_registry, "NeonMarket_RightStorefrontSlat", cubeMesh, glm::vec3(2.03f, 1.05f, z), glm::vec3(0.035f, 1.65f, 0.045f), glm::vec3(0.0f), signMask);
        AddAssetLedRenderable(*m_registry, "NeonMarket_LeftStorefrontSlat", cubeMesh, glm::vec3(-2.03f, 1.12f, z), glm::vec3(0.035f, 1.75f, 0.045f), glm::vec3(0.0f), signMask);
    }
    if (tableMesh && tableMesh->gpuBuffers) {
        AddAssetLedNaturalisticRenderable(*m_registry, "NeonMarket_GroundedMarketTable", "WoodenTable_01", tableMesh, glm::vec3(-0.85f, 0.40f, 1.0f), glm::vec3(0.72f), glm::vec3(0.0f, glm::radians(12.0f), 0.0f), wetAsphalt);
    }
    if (barrelMesh && barrelMesh->gpuBuffers) {
        AddAssetLedNaturalisticRenderable(*m_registry, "NeonMarket_GroundedBarrel", "Barrel_01", barrelMesh, glm::vec3(1.55f, 0.45f, -2.0f), glm::vec3(0.55f), glm::vec3(0.0f, glm::radians(-18.0f), 0.0f), chrome);
    }
    AddParticleEffect(*m_registry, "NeonMarket_SteamPuffs", "steam", glm::vec3(-0.2f, 0.22f, -1.6f));
    AddParticleEffect(*m_registry, "NeonMarket_Rain", "rain", glm::vec3(0.0f, 3.1f, 1.10f));
    AddAssetLedPointLight(*m_registry, "NeonMarket_PinkLight", glm::vec3(-1.9f, 1.8f, -0.7f), glm::vec3(1.0f, 0.18f, 0.58f), 6.4f, 6.5f);
    AddAssetLedPointLight(*m_registry, "NeonMarket_CyanLight", glm::vec3(1.9f, 1.65f, 0.9f), glm::vec3(0.15f, 1.0f, 0.78f), 5.4f, 6.0f);
    AddAssetLedPointLight(*m_registry, "NeonMarket_LeftStallWarmLight", glm::vec3(-1.45f, 0.86f, -0.25f), glm::vec3(1.0f, 0.48f, 0.18f), 3.4f, 3.8f);
    AddAssetLedPointLight(*m_registry, "NeonMarket_RightShelfCyanLight", glm::vec3(1.48f, 0.92f, -0.48f), glm::vec3(0.12f, 0.95f, 0.78f), 2.4f, 3.4f);
    AddAssetLedPointLight(*m_registry, "NeonMarket_CameraFillLight", glm::vec3(-0.15f, 1.35f, -1.85f), glm::vec3(0.42f, 0.58f, 0.78f), 1.8f, 5.0f);
}

void Engine::BuildModelAuthoredScene() {
    std::filesystem::path seedPath;
    const auto seed = LoadModelAuthoredSceneSeed(seedPath);
    if (!seed) {
        BuildRainGlassPavilionScene();
        return;
    }

    const auto& root = *seed;
    const std::string seedId = root.value("id", std::string("model_authored_scene"));
    const std::string sceneFamily = root.value("scene_family", std::string("unknown"));
    ApplyModelAuthoredLighting(m_renderer.get(), sceneFamily);
    const auto sceneProfile = Graphics::BuildSceneLocalCinematicProfile(sceneFamily);
    const float modelAuthoredFixtureScale = glm::clamp(sceneProfile.lightingBalance.localFixtureScale, 0.0f, 2.0f);

    glm::vec3 cameraPosition{-1.4f, 0.9f, -1.8f};
    glm::vec3 cameraTarget{0.0f, 0.55f, 0.3f};
    float cameraFov = 30.0f;
    if (root.contains("camera")) {
        const auto& camera = root["camera"];
        if (camera.contains("position")) { cameraPosition = ReadJsonVec3Or(camera["position"], cameraPosition); }
        if (camera.contains("target")) { cameraTarget = ReadJsonVec3Or(camera["target"], cameraTarget); }
        cameraFov = camera.value("fov", cameraFov);
    }

    m_activeCameraEntity = AddAssetLedCamera(*m_registry, cameraPosition, cameraTarget, cameraFov, 90.0f);
    AddAssetLedSpotLight(*m_registry,
                         "ModelAuthored_KeyLight",
                         cameraPosition + glm::vec3(0.15f, 0.50f, -0.20f),
                         cameraTarget,
                         glm::vec3(1.0f, 0.80f, 0.58f),
                         28.0f * modelAuthoredFixtureScale,
                         8.0f,
                         true);
    AddAssetLedPointLight(*m_registry,
                          "ModelAuthored_CoolFill",
                          cameraTarget + glm::vec3(1.20f, 0.70f, 0.65f),
                          glm::vec3(0.30f, 0.55f, 1.0f),
                          7.0f * modelAuthoredFixtureScale,
                          5.5f);

    std::unordered_map<std::string, std::shared_ptr<Scene::MeshData>> meshCache;
    size_t requestedObjects = 0;
    size_t builtObjects = 0;
    size_t primitiveObjects = 0;
    size_t assetObjects = 0;
    size_t seedLights = 0;

    for (const auto& object : root.value("objects", nlohmann::json::array())) {
        ++requestedObjects;
        if (!object.contains("id") || !object.contains("transform")) {
            spdlog::warn("Skipping model-authored object without id/transform in seed '{}'", seedId);
            continue;
        }
        if (object.value("validation_only", false) || object.value("renderable", true) == false) {
            continue;
        }

        const std::string id = object.value("id", std::string("unnamed"));
        const std::string kind = ToLowerAscii(object.value("kind", std::string("primitive")));
        const std::string cacheKey = kind == "primitive"
            ? std::string("primitive:") + object.value("primitive", std::string("cube"))
            : std::string("asset:") + object.value("runtime_asset", std::string{});

        auto meshIt = meshCache.find(cacheKey);
        if (meshIt == meshCache.end()) {
            auto mesh = LoadModelAuthoredObjectMesh(object);
            if (mesh && !UploadAssetLedMesh(m_renderer.get(), mesh, id.c_str())) {
                mesh.reset();
            }
            meshIt = meshCache.emplace(cacheKey, mesh).first;
        }

        const auto& mesh = meshIt->second;
        if (!mesh) {
            spdlog::warn("Skipping model-authored object '{}' because mesh could not be created or loaded", id);
            continue;
        }

        const RuntimeLayoutTransform transform = TransformFromJson(object["transform"]);
        const std::string materialId = object.value("material", std::string{});
        const AssetLedMaterialSettings material = ModelAuthoredMaterialFromJson(root, materialId);
        const std::string tag = std::string("ModelAuthored_") + id;
        entt::entity entity = AddAssetLedRenderable(*m_registry,
                                                    tag.c_str(),
                                                    mesh,
                                                    transform.position,
                                                    transform.scale,
                                                    transform.rotation,
                                                    material);

        if (kind == "naturalistic_asset") {
            std::string runtimeAsset = object.value("runtime_asset", std::string{});
            std::replace(runtimeAsset.begin(), runtimeAsset.end(), '\\', '/');
            const std::string prefix = "assets/models/naturalistic_showcase/";
            if (runtimeAsset.rfind(prefix, 0) == 0) {
                const std::string relative = runtimeAsset.substr(prefix.size());
                const size_t slash = relative.find('/');
                const std::string assetId = slash == std::string::npos ? relative : relative.substr(0, slash);
                if (m_registry->HasComponent<Scene::RenderableComponent>(entity)) {
                    ApplyNaturalisticAssetTextures(m_registry->GetComponent<Scene::RenderableComponent>(entity), assetId.c_str());
                }
            }
        }

        if (kind == "primitive") {
            ++primitiveObjects;
        } else {
            ++assetObjects;
        }
        ++builtObjects;
    }

    if (builtObjects == 0) {
        spdlog::warn("Model-authored seed '{}' produced no renderable objects; falling back to RainGlassPavilion", seedId);
        BuildRainGlassPavilionScene();
        return;
    }

    seedLights = AddModelAuthoredSeedLights(*m_registry,
                                            root,
                                            sceneProfile.lightingBalance.localFixtureScale);
    const size_t profileLights = AddSceneProfileLights(*m_registry, sceneProfile, cameraTarget);
    const size_t reflectionProbes = AddSceneProfileReflectionProbes(*m_registry, sceneProfile);

    SetFocusTarget(seedId);
    spdlog::info("Loaded model-authored scene seed '{}' family={} requested_objects={} built_objects={} primitives={} assets={} seed_lights={} profile_lights={} reflection_probes={}",
                 seedPath.string(),
                 sceneFamily,
                 requestedObjects,
                 builtObjects,
                 primitiveObjects,
                 assetObjects,
                 seedLights,
                 profileLights,
                 reflectionProbes);
}

void Engine::BuildForestCreekShrineScene() {
    spdlog::info("Building asset-led scene: Forest Creek Shrine");

    auto* renderer = m_renderer.get();
    if (renderer) {
        renderer->SetLightingRigContract("forest_creek_mist", "scene_preset", false);
        renderer->SetWorldShaderPaletteContract("forest_creek_mist", "forest_creek_mist");
        renderer->SetEnvironmentPreset("cool_overcast");
        renderer->SetIBLEnabled(true);
        renderer->SetIBLIntensity(0.72f, 0.82f);
        renderer->SetBackgroundPresentation(false, 0.78f, 0.30f);
        renderer->SetSunDirection(glm::normalize(glm::vec3(-0.25f, 0.80f, 0.36f)));
        renderer->SetSunColor(glm::vec3(0.72f, 0.88f, 0.62f));
        renderer->SetSunIntensity(1.55f);
        renderer->SetExposure(0.86f);
        renderer->SetBloomIntensity(0.08f);
        renderer->SetTAAEnabled(true);
        renderer->SetFXAAEnabled(true);
        renderer->SetSSREnabled(true);
        renderer->SetSSAOEnabled(true);
        renderer->SetFogEnabled(true);
        renderer->SetFogParams(0.018f, 0.0f, 0.48f);
        renderer->SetParticlesEnabled(true);
        renderer->SetWaterParams(0.05f, 0.035f, 4.8f, 0.45f, 0.55f, 0.18f, 0.018f, 0.35f);
    }

    auto cubeMesh = Utils::MeshGenerator::CreateCube();
    auto planeMesh = Utils::MeshGenerator::CreatePlane(1.0f, 1.0f);
    auto cylinderMesh = Utils::MeshGenerator::CreateCylinder(0.5f, 1.0f, 24);
    auto coneMesh = Utils::MeshGenerator::CreateCone(0.5f, 1.0f, 24);
    auto sphereMesh = Utils::MeshGenerator::CreateSphere(0.5f, 24);
    auto boulderMesh = LoadNaturalisticShowcaseMesh("boulder_01/boulder_01_1k.gltf");
    auto trunkMesh = LoadNaturalisticShowcaseMesh("dead_tree_trunk/dead_tree_trunk_1k.gltf");
    auto fernMesh = LoadNaturalisticShowcaseMesh("fern_02/fern_02_1k.gltf");
    auto branchMesh = LoadNaturalisticShowcaseMesh("dry_branches_medium_01/dry_branches_medium_01_1k.gltf");
    auto grassMesh = LoadNaturalisticShowcaseMesh("grass_bermuda_01/grass_bermuda_01_1k.gltf");
    auto stumpMesh = LoadNaturalisticShowcaseMesh("tree_stump_01/tree_stump_01_1k.gltf");
    auto mossRockSetMesh = LoadNaturalisticShowcaseMesh("rock_moss_set_01/rock_moss_set_01_1k.gltf");
    auto bushMesh = LoadNaturalisticShowcaseMesh("wild_rooibos_bush/wild_rooibos_bush_1k.gltf");
    if (!UploadAssetLedMesh(renderer, cubeMesh, "cube") ||
        !UploadAssetLedMesh(renderer, planeMesh, "plane") ||
        !UploadAssetLedMesh(renderer, cylinderMesh, "cylinder") ||
        !UploadAssetLedMesh(renderer, coneMesh, "cone") ||
        !UploadAssetLedMesh(renderer, sphereMesh, "sphere") ||
        !UploadAssetLedMesh(renderer, boulderMesh, "boulder_01") ||
        !UploadAssetLedMesh(renderer, trunkMesh, "dead_tree_trunk") ||
        !UploadAssetLedMesh(renderer, fernMesh, "fern_02") ||
        !UploadAssetLedMesh(renderer, branchMesh, "dry_branches_medium_01") ||
        !UploadAssetLedMesh(renderer, grassMesh, "grass_bermuda_01") ||
        !UploadAssetLedMesh(renderer, stumpMesh, "tree_stump_01") ||
        !UploadAssetLedMesh(renderer, mossRockSetMesh, "rock_moss_set_01") ||
        !UploadAssetLedMesh(renderer, bushMesh, "wild_rooibos_bush")) {
        return;
    }

    m_activeCameraEntity = AddAssetLedCamera(*m_registry,
        glm::vec3(-0.98f, 0.28f, -1.52f), glm::vec3(-0.42f, 0.035f, -0.70f), 34.0f, 140.0f);

    const AssetLedMaterialSettings mossStone{glm::vec4(0.18f, 0.20f, 0.17f, 1.0f), 0.0f, 0.66f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.66f, 0.72f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "mossy_masonry"};
    const AssetLedMaterialSettings creek{glm::vec4(0.035f, 0.16f, 0.14f, 0.72f), 0.0f, 0.08f, 0.42f, 1.333f, glm::vec3(0.0f), 1.0f, 0.82f, 0.2f, true, Scene::RenderableComponent::AlphaMode::Blend, Scene::RenderableComponent::RenderLayer::Opaque, "water"};
    const AssetLedMaterialSettings wetBark{glm::vec4(0.19f, 0.12f, 0.08f, 1.0f), 0.0f, 0.62f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.65f, 0.42f, false, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "wood"};
    const AssetLedMaterialSettings vegetation{glm::vec4(0.10f, 0.20f, 0.11f, 1.0f), 0.0f, 0.78f, 0.0f, 1.5f, glm::vec3(0.0f), 1.0f, 0.48f, 0.46f, true, Scene::RenderableComponent::AlphaMode::Opaque, Scene::RenderableComponent::RenderLayer::Opaque, "vegetation"};

    AddAssetLedRenderable(*m_registry, "ForestShrine_GroundBank", cubeMesh, glm::vec3(0.0f, -0.08f, 0.0f), glm::vec3(5.2f, 0.14f, 4.4f), glm::vec3(0.0f), mossStone);
    AddAssetLedRenderable(*m_registry, "ForestShrine_LeftRaisedBank", cubeMesh, glm::vec3(-1.34f, 0.005f, -0.50f), glm::vec3(0.62f, 0.105f, 1.62f), glm::vec3(0.0f, glm::radians(-14.0f), glm::radians(2.0f)), mossStone);
    AddAssetLedRenderable(*m_registry, "ForestShrine_RightRaisedBank", cubeMesh, glm::vec3(1.05f, 0.000f, -0.24f), glm::vec3(0.54f, 0.095f, 1.48f), glm::vec3(0.0f, glm::radians(14.0f), glm::radians(-1.5f)), mossStone);
    AddAssetLedRenderable(*m_registry, "ForestShrine_ForegroundMudLip", cubeMesh, glm::vec3(-0.46f, -0.005f, -1.92f), glm::vec3(0.82f, 0.045f, 0.13f), glm::vec3(0.0f, glm::radians(-8.0f), glm::radians(1.5f)), mossStone);
    AddAssetLedRenderable(*m_registry, "ForestShrine_BackCanopyRise", cubeMesh, glm::vec3(-0.20f, -0.005f, 2.48f), glm::vec3(1.55f, 0.055f, 0.18f), glm::vec3(0.0f, glm::radians(-4.0f), 0.0f), vegetation);
    if (bushMesh && bushMesh->gpuBuffers) {
        AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_BackCanopyUpper", "wild_rooibos_bush", bushMesh, glm::vec3(-1.10f, 0.08f, 1.96f), glm::vec3(0.72f), glm::vec3(0.0f, glm::radians(8.0f), 0.0f), vegetation);
        AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_BackCanopyBlobRight", "wild_rooibos_bush", bushMesh, glm::vec3(0.92f, 0.08f, 1.96f), glm::vec3(0.66f), glm::vec3(0.0f, glm::radians(-18.0f), 0.0f), vegetation);
        AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_BackCanopyMid", "wild_rooibos_bush", bushMesh, glm::vec3(-0.08f, 0.08f, 1.76f), glm::vec3(0.68f), glm::vec3(0.0f, glm::radians(22.0f), 0.0f), vegetation);
    }
    AddAssetLedRenderable(*m_registry, "ForestShrine_LeftTreeWall", cylinderMesh, glm::vec3(-2.54f, 0.38f, 0.64f), glm::vec3(0.075f, 0.58f, 0.075f), glm::vec3(0.0f, glm::radians(5.0f), glm::radians(-3.0f)), wetBark);
    AddAssetLedRenderable(*m_registry, "ForestShrine_RightTreeWall", cylinderMesh, glm::vec3(2.28f, 0.36f, 0.70f), glm::vec3(0.070f, 0.54f, 0.070f), glm::vec3(0.0f, glm::radians(-5.0f), glm::radians(2.5f)), wetBark);
    AddAssetLedRenderable(*m_registry, "ForestShrine_CreekSheet", planeMesh, glm::vec3(-0.42f, 0.045f, -0.84f), glm::vec3(0.36f, 1.0f, 1.02f), glm::vec3(0.0f, glm::radians(-10.0f), 0.0f), creek);
    AddAssetLedRenderable(*m_registry, "ForestShrine_CreekLeftFoamEdge", cubeMesh, glm::vec3(-0.56f, 0.060f, -0.86f), glm::vec3(0.008f, 0.008f, 0.18f), glm::vec3(0.0f, glm::radians(-10.0f), 0.0f), vegetation);
    AddAssetLedRenderable(*m_registry, "ForestShrine_CreekRightFoamEdge", cubeMesh, glm::vec3(-0.30f, 0.060f, -0.94f), glm::vec3(0.008f, 0.008f, 0.17f), glm::vec3(0.0f, glm::radians(-10.0f), 0.0f), vegetation);
    AddAssetLedRenderable(*m_registry, "ForestShrine_ShrineBase", cubeMesh, glm::vec3(0.46f, 0.095f, 0.82f), glm::vec3(0.46f, 0.14f, 0.34f), glm::vec3(0.0f, glm::radians(18.0f), glm::radians(-2.0f)), mossStone);
    AddAssetLedRenderable(*m_registry, "ForestShrine_Capstone", cubeMesh, glm::vec3(0.44f, 0.33f, 0.88f), glm::vec3(0.52f, 0.055f, 0.36f), glm::vec3(0.0f, glm::radians(18.0f), glm::radians(2.0f)), mossStone);
    AddAssetLedRenderable(*m_registry, "ForestShrine_FrontStep", cubeMesh, glm::vec3(0.26f, 0.045f, 0.48f), glm::vec3(0.44f, 0.040f, 0.13f), glm::vec3(0.0f, glm::radians(18.0f), glm::radians(1.0f)), mossStone);
    AddAssetLedRenderable(*m_registry, "ForestShrine_BackStoneSilhouette", cubeMesh, glm::vec3(0.50f, 0.39f, 1.00f), glm::vec3(0.16f, 0.075f, 0.065f), glm::vec3(0.0f, glm::radians(18.0f), glm::radians(-6.0f)), mossStone);
    AddAssetLedRenderable(*m_registry, "ForestShrine_LeftShrinePost", cylinderMesh, glm::vec3(0.24f, 0.30f, 0.80f), glm::vec3(0.045f, 0.36f, 0.045f), glm::vec3(0.0f, glm::radians(18.0f), 0.0f), wetBark);
    AddAssetLedRenderable(*m_registry, "ForestShrine_RightShrinePost", cylinderMesh, glm::vec3(0.76f, 0.30f, 0.78f), glm::vec3(0.045f, 0.36f, 0.045f), glm::vec3(0.0f, glm::radians(18.0f), 0.0f), wetBark);
    AddAssetLedRenderable(*m_registry, "ForestShrine_MossRoof", coneMesh, glm::vec3(0.48f, 0.52f, 0.98f), glm::vec3(0.24f, 0.08f, 0.22f), glm::vec3(0.0f, glm::radians(18.0f), glm::radians(1.0f)), mossStone);
    if (trunkMesh && trunkMesh->gpuBuffers) {
        AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_FallenTrunk", "dead_tree_trunk", trunkMesh, glm::vec3(-1.56f, 0.08f, 0.74f), glm::vec3(0.34f), glm::vec3(glm::radians(4.0f), glm::radians(-34.0f), glm::radians(-3.0f)), wetBark);
        AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_BackLeftTrunk", "dead_tree_trunk", trunkMesh, glm::vec3(-2.08f, 0.10f, 1.82f), glm::vec3(0.40f), glm::vec3(glm::radians(4.0f), glm::radians(28.0f), glm::radians(-5.0f)), wetBark);
        AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_BackRightTrunk", "dead_tree_trunk", trunkMesh, glm::vec3(1.90f, 0.10f, 1.72f), glm::vec3(0.36f), glm::vec3(glm::radians(-2.0f), glm::radians(-32.0f), glm::radians(4.0f)), wetBark);
    }
    if (stumpMesh && stumpMesh->gpuBuffers) {
        AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_LeftStumpAnchor", "tree_stump_01", stumpMesh, glm::vec3(-1.26f, 0.04f, -0.64f), glm::vec3(0.30f), glm::vec3(0.0f, glm::radians(28.0f), glm::radians(-2.0f)), wetBark);
        AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_BackStumpAnchor", "tree_stump_01", stumpMesh, glm::vec3(0.82f, 0.05f, 1.18f), glm::vec3(0.27f), glm::vec3(0.0f, glm::radians(-34.0f), glm::radians(3.0f)), wetBark);
    }
    if (boulderMesh && boulderMesh->gpuBuffers) {
        AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_LeftBankRock", "boulder_01", boulderMesh, glm::vec3(-1.16f, 0.02f, -0.42f), glm::vec3(0.18f), glm::vec3(0.0f, glm::radians(15.0f), 0.0f), mossStone);
        AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_RightBankRock", "boulder_01", boulderMesh, glm::vec3(0.62f, 0.01f, -1.00f), glm::vec3(0.16f), glm::vec3(0.0f, glm::radians(-35.0f), 0.0f), mossStone);
        AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_CreekStepRockA", "boulder_01", boulderMesh, glm::vec3(-0.58f, 0.01f, -0.38f), glm::vec3(0.16f), glm::vec3(0.0f, glm::radians(42.0f), 0.0f), mossStone);
        AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_CreekStepRockB", "boulder_01", boulderMesh, glm::vec3(0.08f, 0.01f, -0.12f), glm::vec3(0.15f), glm::vec3(0.0f, glm::radians(-18.0f), 0.0f), mossStone);
        AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_BackMossBoulder", "boulder_01", boulderMesh, glm::vec3(1.02f, 0.03f, 0.92f), glm::vec3(0.20f), glm::vec3(0.0f, glm::radians(62.0f), 0.0f), mossStone);
        AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_StandingMossStone", "boulder_01", boulderMesh, glm::vec3(-0.04f, 0.06f, 0.26f), glm::vec3(0.18f), glm::vec3(glm::radians(-6.0f), glm::radians(32.0f), glm::radians(9.0f)), mossStone);
        AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_ShrineMossCladdingLeft", "boulder_01", boulderMesh, glm::vec3(0.20f, 0.04f, 0.72f), glm::vec3(0.11f), glm::vec3(glm::radians(5.0f), glm::radians(18.0f), glm::radians(-4.0f)), mossStone);
        AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_ShrineMossCladdingRight", "boulder_01", boulderMesh, glm::vec3(0.72f, 0.04f, 0.84f), glm::vec3(0.10f), glm::vec3(glm::radians(-3.0f), glm::radians(-36.0f), glm::radians(4.0f)), mossStone);
        AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_ForegroundCreekStone", "boulder_01", boulderMesh, glm::vec3(-0.86f, 0.01f, -1.20f), glm::vec3(0.15f), glm::vec3(0.0f, glm::radians(-52.0f), 0.0f), mossStone);
        AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_LeftCreekWallStone", "boulder_01", boulderMesh, glm::vec3(-1.10f, 0.02f, -0.06f), glm::vec3(0.17f), glm::vec3(glm::radians(3.0f), glm::radians(74.0f), glm::radians(-4.0f)), mossStone);
        AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_RightCreekWallStone", "boulder_01", boulderMesh, glm::vec3(0.56f, 0.01f, -0.58f), glm::vec3(0.15f), glm::vec3(glm::radians(-2.0f), glm::radians(-24.0f), glm::radians(5.0f)), mossStone);
    }
    if (mossRockSetMesh && mossRockSetMesh->gpuBuffers) {
        AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_MossRockBankLeft", "rock_moss_set_01", mossRockSetMesh, glm::vec3(-1.30f, -0.05f, -0.92f), glm::vec3(0.080f), glm::vec3(0.0f, glm::radians(18.0f), 0.0f), mossStone);
        AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_MossRockBankRight", "rock_moss_set_01", mossRockSetMesh, glm::vec3(0.46f, -0.05f, -0.92f), glm::vec3(0.070f), glm::vec3(0.0f, glm::radians(-36.0f), 0.0f), mossStone);
    }
    if (fernMesh && fernMesh->gpuBuffers) {
        for (int i = 0; i < 6; ++i) {
            const float side = (i % 2 == 0) ? -1.0f : 1.0f;
            AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_FernCluster", "fern_02", fernMesh,
                                              glm::vec3(side * (1.6f + 0.2f * i), 0.05f, -0.8f + 0.52f * i),
                                              glm::vec3(0.45f + 0.04f * i),
                                              glm::vec3(0.0f, glm::radians(24.0f * i), 0.0f),
                                              vegetation);
        }
        AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_FernClusterHeroLeft", "fern_02", fernMesh, glm::vec3(-1.04f, 0.05f, -0.74f), glm::vec3(0.38f), glm::vec3(0.0f, glm::radians(18.0f), 0.0f), vegetation);
        AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_FernClusterHeroRight", "fern_02", fernMesh, glm::vec3(0.74f, 0.05f, -0.58f), glm::vec3(0.34f), glm::vec3(0.0f, glm::radians(-34.0f), 0.0f), vegetation);
    }
    if (branchMesh && branchMesh->gpuBuffers) {
        AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_BranchArchLeft", "dry_branches_medium_01", branchMesh, glm::vec3(-2.15f, 0.46f, 0.35f), glm::vec3(0.78f), glm::vec3(glm::radians(4.0f), glm::radians(42.0f), glm::radians(-8.0f)), wetBark);
        AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_BranchArchRight", "dry_branches_medium_01", branchMesh, glm::vec3(1.78f, 0.42f, 0.15f), glm::vec3(0.66f), glm::vec3(glm::radians(0.0f), glm::radians(-28.0f), glm::radians(7.0f)), wetBark);
    }
    if (grassMesh && grassMesh->gpuBuffers) {
        for (int i = 0; i < 8; ++i) {
            const float x = -2.2f + 0.62f * static_cast<float>(i);
            const float z = (i % 2 == 0) ? -1.72f : -0.12f;
            AddAssetLedNaturalisticRenderable(*m_registry, "ForestShrine_GrassBankCluster", "grass_bermuda_01", grassMesh,
                                              glm::vec3(x, 0.05f, z),
                                              glm::vec3(0.34f + 0.02f * static_cast<float>(i % 3)),
                                              glm::vec3(0.0f, glm::radians(19.0f * static_cast<float>(i)), 0.0f),
                                              vegetation);
        }
    }
    AddAssetLedSpotLight(*m_registry, "ForestShrine_FilteredSun", glm::vec3(-3.2f, 4.4f, -2.6f), glm::vec3(0.0f, 0.45f, 0.2f), glm::vec3(0.72f, 0.88f, 0.62f), 4.0f, 16.0f, false);
}

void Engine::SetCameraToSceneDefault(Scene::TransformComponent& transform) {
    glm::vec3 pos;
    glm::vec3 target;

    if (m_currentScenePreset == ScenePreset::CornellBox) {
        pos = glm::vec3(0.0f, 1.6f, -3.0f);
        target = glm::vec3(0.0f, 1.2f, 0.0f);
    } else if (m_currentScenePreset == ScenePreset::RTShowcase ||
               m_currentScenePreset == ScenePreset::IBLGallery) {
        pos = glm::vec3(-14.0f, 2.05f, -6.8f);
        target = glm::vec3(-14.0f, 1.05f, 0.25f);
    } else if (m_currentScenePreset == ScenePreset::MaterialLab) {
        pos = glm::vec3(0.0f, 2.45f, -8.2f);
        target = glm::vec3(0.0f, 1.05f, -0.15f);
    } else if (m_currentScenePreset == ScenePreset::OutdoorSunsetBeach) {
        pos = glm::vec3(0.0f, 3.1f, -12.0f);
        target = glm::vec3(0.0f, 0.85f, -0.8f);
    } else if (m_currentScenePreset == ScenePreset::LiquidGallery) {
        pos = glm::vec3(0.0f, 2.65f, -10.2f);
        target = glm::vec3(0.0f, 0.65f, 0.0f);
    } else if (m_currentScenePreset == ScenePreset::CoastalCliffFoundry) {
        pos = glm::vec3(-2.35f, 1.28f, -3.35f);
        target = glm::vec3(0.92f, 0.68f, 0.08f);
    } else if (m_currentScenePreset == ScenePreset::RainGlassPavilion) {
        pos = glm::vec3(-2.35f, 1.08f, -2.75f);
        target = glm::vec3(0.48f, 0.76f, 0.10f);
    } else if (m_currentScenePreset == ScenePreset::DesertRelicGallery) {
        pos = glm::vec3(-2.55f, 1.35f, -4.05f);
        target = glm::vec3(0.06f, 0.84f, 0.46f);
    } else if (m_currentScenePreset == ScenePreset::NeonAlleyMaterialMarket) {
        pos = glm::vec3(-1.85f, 1.02f, -3.15f);
        target = glm::vec3(0.62f, 0.82f, -0.10f);
    } else if (m_currentScenePreset == ScenePreset::ForestCreekShrine) {
        pos = glm::vec3(-1.05f, 0.34f, -1.78f);
        target = glm::vec3(-0.62f, 0.06f, -0.86f);
    } else if (m_currentScenePreset == ScenePreset::ModelAuthoredScene) {
        pos = glm::vec3(-1.12f, 0.84f, 0.15f);
        target = glm::vec3(-0.34f, 0.71f, 0.88f);
    } else if (m_currentScenePreset == ScenePreset::TemporalValidation) {
        pos = glm::vec3(0.0f, 2.3f, -6.4f);
        target = glm::vec3(0.0f, 1.0f, 0.1f);
    } else if (m_currentScenePreset == ScenePreset::ProceduralTerrain) {
        pos = glm::vec3(0.0f, 50.0f, -10.0f);
        target = glm::vec3(0.0f, 30.0f, 50.0f);
    } else {
        pos = glm::vec3(0.0f, 3.0f, -8.0f);
        target = glm::vec3(0.0f, 1.0f, kHeroPoolZ);
    }

    glm::vec3 up(0.0f, 1.0f, 0.0f);
    glm::vec3 forward = glm::normalize(target - pos);
    if (std::abs(glm::dot(forward, up)) > 0.99f) {
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    transform.position = pos;
    transform.rotation = glm::quatLookAtLH(forward, up);

    forward = glm::normalize(forward);
    m_cameraYaw = std::atan2(forward.x, forward.z);
    m_cameraPitch = std::asin(glm::clamp(forward.y, -1.0f, 1.0f));
    float pitchLimit = glm::radians(89.0f);
    m_cameraPitch = glm::clamp(m_cameraPitch, -pitchLimit, pitchLimit);
}

// =============================================================================
// Procedural Terrain Scene (appended - does not modify existing code)
// =============================================================================

void Engine::BuildProceduralTerrainScene() {
    // ==========================================================================
    // PROCEDURAL TERRAIN WORLD - Minecraft-style explorable world
    // ==========================================================================

    // Enable terrain system with varied, interesting terrain
    m_terrainEnabled = true;
    m_terrainParams = Scene::TerrainNoiseParams{};
    m_terrainParams.seed = 42;
    m_terrainParams.amplitude = 20.0f;      // Taller mountains
    m_terrainParams.frequency = 0.003f;     // Larger features
    m_terrainParams.octaves = 6;            // More detail
    m_terrainParams.lacunarity = 2.0f;
    m_terrainParams.gain = 0.5f;
    m_terrainParams.warp = 15.0f;           // Domain warping for natural look

    // Simple hash function for procedural placement
    auto hash = [](int x, int z, int seed) -> float {
        uint32_t h = static_cast<uint32_t>(x * 374761393 + z * 668265263 + seed);
        h = (h ^ (h >> 13)) * 1274126177;
        return static_cast<float>(h & 0xFFFF) / 65535.0f;
    };

    // Create camera at a nice starting position
    {
        entt::entity camera = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(camera, "MainCamera");

        // Start at origin, sample terrain height
        float startY = Scene::SampleTerrainHeight(0.0, 0.0, m_terrainParams) + 2.0f;

        auto& transform = m_registry->AddComponent<TransformComponent>(camera);
        transform.position = glm::vec3(0.0f, startY, 0.0f);
        glm::vec3 forward = glm::normalize(glm::vec3(0.0f, 0.0f, 1.0f));
        transform.rotation = glm::quatLookAtLH(forward, glm::vec3(0.0f, 1.0f, 0.0f));

        auto& cam = m_registry->AddComponent<Scene::CameraComponent>(camera);
        cam.fov = 75.0f;  // Wider FOV for exploration
        cam.nearPlane = 0.1f;
        cam.farPlane = 1500.0f;

        m_activeCameraEntity = camera;
    }

    // Create terrain chunks - use same radius as dynamic loading
    const int32_t chunkRadius = CHUNK_LOAD_RADIUS;
    const uint32_t gridDim = 64;
    const float chunkSize = TERRAIN_CHUNK_SIZE;
    int chunkCount = 0;

    // Clear loaded chunks tracking (will be populated below)
    m_loadedChunks.clear();

    for (int32_t cz = -chunkRadius; cz <= chunkRadius; ++cz) {
        for (int32_t cx = -chunkRadius; cx <= chunkRadius; ++cx) {
            entt::entity chunk = m_registry->CreateEntity();

            char tagName[64];
            snprintf(tagName, sizeof(tagName), "TerrainChunk_%d_%d", cx, cz);
            m_registry->AddComponent<Scene::TagComponent>(chunk, tagName);

            auto& transform = m_registry->AddComponent<TransformComponent>(chunk);
            // Position chunk at correct world location - mesh uses local coords
            transform.position = glm::vec3(cx * chunkSize, 0.0f, cz * chunkSize);
            transform.scale = glm::vec3(1.0f);

            auto mesh = Utils::MeshGenerator::CreateTerrainHeightmapChunk(
                gridDim, chunkSize, cx, cz, m_terrainParams);

            auto& renderable = m_registry->AddComponent<Scene::RenderableComponent>(chunk);
            renderable.mesh = mesh;
            renderable.presetName = "terrain";
            renderable.albedoColor = glm::vec4(0.18f, 0.35f, 0.12f, 1.0f);  // Forest green
            renderable.roughness = 0.95f;
            renderable.metallic = 0.0f;

            auto& terrainComp = m_registry->AddComponent<Scene::TerrainChunkComponent>(chunk);
            terrainComp.chunkX = cx;
            terrainComp.chunkZ = cz;
            terrainComp.chunkSize = chunkSize;
            terrainComp.lodLevel = 0;

            // Register chunk in loaded set for dynamic streaming
            m_loadedChunks.insert({cx, cz});

            ++chunkCount;
        }
    }

    // Directional sun light - warm afternoon sun
    {
        entt::entity sun = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(sun, "Sun");

        auto& transform = m_registry->AddComponent<TransformComponent>(sun);
        transform.position = glm::vec3(500.0f, 800.0f, 300.0f);
        glm::vec3 sunDir = glm::normalize(glm::vec3(-0.3f, -0.85f, -0.4f));
        transform.rotation = glm::quatLookAtLH(sunDir, glm::vec3(0.0f, 1.0f, 0.0f));

        auto& light = m_registry->AddComponent<Scene::LightComponent>(sun);
        light.type = Scene::LightType::Directional;
        light.color = glm::vec3(1.0f, 0.95f, 0.8f);  // Warm sunlight
        light.intensity = 4.0f;
        light.castsShadows = true;
    }

    // Helper: Spawn a tree at position
    auto spawnTree = [&](float x, float z, int treeId) {
        float groundY = Scene::SampleTerrainHeight(
            static_cast<double>(x), static_cast<double>(z), m_terrainParams);

        float trunkHeight = 3.0f + hash(static_cast<int>(x), static_cast<int>(z), 100) * 2.0f;
        float trunkRadius = 0.15f + hash(static_cast<int>(x), static_cast<int>(z), 200) * 0.1f;
        float foliageRadius = 1.2f + hash(static_cast<int>(x), static_cast<int>(z), 300) * 0.8f;

        // Trunk
        {
            entt::entity trunk = m_registry->CreateEntity();
            char name[32];
            snprintf(name, sizeof(name), "TreeTrunk_%d", treeId);
            m_registry->AddComponent<Scene::TagComponent>(trunk, name);

            auto& t = m_registry->AddComponent<TransformComponent>(trunk);
            t.position = glm::vec3(x, groundY + trunkHeight * 0.5f, z);
            t.scale = glm::vec3(trunkRadius * 2.0f, trunkHeight, trunkRadius * 2.0f);

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(trunk);
            r.mesh = Utils::MeshGenerator::CreateCylinder(0.5f, 1.0f, 8);
            r.presetName = "wood";
            r.albedoColor = glm::vec4(0.35f, 0.22f, 0.1f, 1.0f);  // Brown bark
            r.roughness = 0.9f;
            r.metallic = 0.0f;
        }

        // Foliage (cone shape for pine tree look)
        {
            entt::entity foliage = m_registry->CreateEntity();
            char name[32];
            snprintf(name, sizeof(name), "TreeFoliage_%d", treeId);
            m_registry->AddComponent<Scene::TagComponent>(foliage, name);

            auto& t = m_registry->AddComponent<TransformComponent>(foliage);
            t.position = glm::vec3(x, groundY + trunkHeight + foliageRadius * 0.5f, z);
            t.scale = glm::vec3(foliageRadius * 2.0f, foliageRadius * 2.5f, foliageRadius * 2.0f);

            auto& r = m_registry->AddComponent<Scene::RenderableComponent>(foliage);
            r.mesh = Utils::MeshGenerator::CreateCone(0.5f, 1.0f, 8);
            r.presetName = "leaves";
            r.albedoColor = glm::vec4(0.1f, 0.4f, 0.15f, 1.0f);  // Dark green
            r.roughness = 0.8f;
            r.metallic = 0.0f;
        }
    };

    // Helper: Spawn a rock at position
    auto spawnRock = [&](float x, float z, int rockId) {
        float groundY = Scene::SampleTerrainHeight(
            static_cast<double>(x), static_cast<double>(z), m_terrainParams);

        float size = 0.3f + hash(static_cast<int>(x * 10), static_cast<int>(z * 10), 400) * 0.6f;

        entt::entity rock = m_registry->CreateEntity();
        char name[32];
        snprintf(name, sizeof(name), "Rock_%d", rockId);
        m_registry->AddComponent<Scene::TagComponent>(rock, name);

        auto& t = m_registry->AddComponent<TransformComponent>(rock);
        t.position = glm::vec3(x, groundY + size * 0.3f, z);
        t.scale = glm::vec3(size, size * 0.6f, size);
        // Random rotation
        float yaw = hash(static_cast<int>(x * 7), static_cast<int>(z * 7), 500) * 6.28f;
        t.rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(rock);
        r.mesh = Utils::MeshGenerator::CreateSphere(0.5f, 8);
        r.presetName = "stone";
        r.albedoColor = glm::vec4(0.4f, 0.4f, 0.42f, 1.0f);  // Gray stone
        r.roughness = 0.85f;
        r.metallic = 0.0f;
    };

    // Procedurally place trees and rocks across the terrain
    int treeCount = 0;
    int rockCount = 0;
    const float worldExtent = chunkRadius * chunkSize;

    for (float x = -worldExtent; x < worldExtent; x += 20.0f) {
        for (float z = -worldExtent; z < worldExtent; z += 20.0f) {
            // Add some jitter
            float jx = x + (hash(static_cast<int>(x), static_cast<int>(z), 1) - 0.5f) * 6.0f;
            float jz = z + (hash(static_cast<int>(x), static_cast<int>(z), 2) - 0.5f) * 6.0f;

            float h = Scene::SampleTerrainHeight(
                static_cast<double>(jx), static_cast<double>(jz), m_terrainParams);

            // Trees on mid-height terrain (not too high, not too low)
            if (h > 4.0f && h < 16.0f && hash(static_cast<int>(jx), static_cast<int>(jz), 3) > 0.7f) {
                spawnTree(jx, jz, treeCount++);
            }
            // Rocks scattered more randomly
            else if (hash(static_cast<int>(jx * 2), static_cast<int>(jz * 2), 4) > 0.92f) {
                spawnRock(jx, jz, rockCount++);
            }
        }
    }

    // Spawn interactable objects near spawn
    auto spawnInteractable = [&](const char* name, float x, float z, float radius,
                                  const glm::vec4& color) {
        float groundY = Scene::SampleTerrainHeight(
            static_cast<double>(x), static_cast<double>(z), m_terrainParams);

        entt::entity obj = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(obj, name);

        auto& t = m_registry->AddComponent<TransformComponent>(obj);
        t.position = glm::vec3(x, groundY + radius + 0.1f, z);
        t.scale = glm::vec3(radius * 2.0f);

        auto& r = m_registry->AddComponent<Scene::RenderableComponent>(obj);
        r.mesh = Utils::MeshGenerator::CreateSphere(0.5f, 16);
        r.presetName = "shiny";
        r.albedoColor = color;
        r.roughness = 0.2f;
        r.metallic = 0.8f;

        auto& i = m_registry->AddComponent<Scene::InteractableComponent>(obj);
        i.type = Scene::InteractionType::Pickup;
        i.highlightColor = glm::vec3(1.0f, 1.0f, 0.5f);
        i.interactionRadius = radius * 2.0f;
        i.isHighlighted = false;

        auto& p = m_registry->AddComponent<Scene::PhysicsBodyComponent>(obj);
        p.velocity = glm::vec3(0.0f);
        p.angularVelocity = glm::vec3(0.0f);
        p.mass = 1.0f;
        p.restitution = 0.5f;
        p.friction = 0.4f;
        p.useGravity = true;
        p.isKinematic = false;
    };

    // Place collectible orbs near spawn
    spawnInteractable("RedOrb", 5.0f, 8.0f, 0.4f, glm::vec4(0.9f, 0.2f, 0.1f, 1.0f));
    spawnInteractable("BlueOrb", -6.0f, 10.0f, 0.35f, glm::vec4(0.1f, 0.3f, 0.9f, 1.0f));
    spawnInteractable("GreenOrb", 8.0f, -5.0f, 0.45f, glm::vec4(0.2f, 0.9f, 0.3f, 1.0f));
    spawnInteractable("GoldOrb", -4.0f, -8.0f, 0.5f, glm::vec4(1.0f, 0.8f, 0.2f, 1.0f));
    spawnInteractable("PurpleOrb", 12.0f, 3.0f, 0.38f, glm::vec4(0.7f, 0.2f, 0.9f, 1.0f));

    if (m_renderer) {
        m_worldState.Update(0.0f);
        Graphics::ApplyOutdoorWorldSceneControls(*m_renderer,
                                                 m_worldState.sunDirection,
                                                 m_worldState.sunColor,
                                                 m_worldState.sunIntensity);
    }

    spdlog::info("=== TERRAIN WORLD READY ===");
    spdlog::info("  {} terrain chunks", chunkCount);
    spdlog::info("  {} trees, {} rocks", treeCount, rockCount);
    spdlog::info("  Time: {:.1f}h - Press ./,/L to control time", m_worldState.timeOfDay);
    spdlog::info("  Press F5 for play mode, WASD to move, E to interact");
    spdlog::info("  Press J to exit terrain world");
}

void Engine::BuildEditorModeTerrainScene() {
    // Minimal scene setup for Engine Editor Mode - EditorWorld handles terrain chunks
    // We just need camera, sun, and basic settings

    // Enable terrain system for player physics
    m_terrainEnabled = true;

    // Get terrain params from EditorWorld if available (so physics matches rendered terrain)
    if (m_editorModeController && m_editorModeController->GetWorld()) {
        m_terrainParams = m_editorModeController->GetWorld()->GetTerrainParams();
    } else {
        // Fallback to default params
        m_terrainParams = Scene::TerrainNoiseParams{};
        m_terrainParams.seed = 42;
        m_terrainParams.amplitude = 20.0f;
        m_terrainParams.frequency = 0.003f;
        m_terrainParams.octaves = 6;
        m_terrainParams.lacunarity = 2.0f;
        m_terrainParams.gain = 0.5f;
        m_terrainParams.warp = 15.0f;
    }

    // Create camera at origin, sample terrain height for starting position
    {
        entt::entity camera = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(camera, "MainCamera");

        float startY = Scene::SampleTerrainHeight(0.0, 0.0, m_terrainParams) + 2.0f;

        auto& transform = m_registry->AddComponent<TransformComponent>(camera);
        transform.position = glm::vec3(0.0f, startY, 0.0f);
        glm::vec3 forward = glm::normalize(glm::vec3(0.0f, 0.0f, 1.0f));
        transform.rotation = glm::quatLookAtLH(forward, glm::vec3(0.0f, 1.0f, 0.0f));

        auto& cam = m_registry->AddComponent<Scene::CameraComponent>(camera);
        cam.fov = 75.0f;
        cam.nearPlane = 0.1f;
        cam.farPlane = 1500.0f;
        cam.isActive = true;

        m_activeCameraEntity = camera;
    }

    // Directional sun light
    {
        entt::entity sun = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(sun, "Sun");

        auto& transform = m_registry->AddComponent<TransformComponent>(sun);
        transform.position = glm::vec3(500.0f, 800.0f, 300.0f);
        glm::vec3 sunDir = glm::normalize(glm::vec3(-0.3f, -0.85f, -0.4f));
        transform.rotation = glm::quatLookAtLH(sunDir, glm::vec3(0.0f, 1.0f, 0.0f));

        auto& light = m_registry->AddComponent<Scene::LightComponent>(sun);
        light.type = Scene::LightType::Directional;
        light.color = glm::vec3(1.0f, 0.95f, 0.8f);
        light.intensity = 4.0f;
        light.castsShadows = true;
    }

    if (m_renderer) {
        m_worldState.Update(0.0f);
        Graphics::ApplyOutdoorWorldSceneControls(*m_renderer,
                                                 m_worldState.sunDirection,
                                                 m_worldState.sunColor,
                                                 m_worldState.sunIntensity);
    }

    spdlog::info("=== EDITOR MODE TERRAIN READY ===");
    spdlog::info("  EditorWorld handles terrain chunks");
    spdlog::info("  Time: {:.1f}h - Press ./,/L to control time", m_worldState.timeOfDay);
    spdlog::info("  Press F5 for play mode, WASD to move, Space to jump");
}

bool Engine::ApplyParticleEffectPresetToScene(const std::string& presetId) {
    if (!m_registry) {
        return false;
    }

    const std::string selected = presetId.empty() ? "gallery_mix" : presetId;
    bool changed = false;
    auto view = m_registry->View<Scene::ParticleEmitterComponent>();
    for (auto entity : view) {
        auto& emitter = view.get<Scene::ParticleEmitterComponent>(entity);
        const std::string target =
            (selected == "gallery_mix")
                ? (emitter.defaultEffectPresetId.empty() ? emitter.effectPresetId : emitter.defaultEffectPresetId)
                : selected;
        if (target.empty() || target == emitter.effectPresetId) {
            continue;
        }

        const std::string defaultPreset = emitter.defaultEffectPresetId;
        if (!Scene::ApplyParticleEffectDescriptor(target, emitter)) {
            continue;
        }
        emitter.defaultEffectPresetId = defaultPreset.empty() ? target : defaultPreset;
        emitter.emissionAccumulator = 0.0f;
        emitter.particles.clear();
        changed = true;
    }

    if (m_renderer) {
        m_renderer->SetParticleEffectPreset(selected);
    }
    return changed;
}

} // namespace Cortex
