#include "Graphics/Subsystems/VoxelSubsystem.h"

#include "Graphics/Passes/VoxelPass.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace Cortex::Graphics {

void VoxelSubsystem::SetBackendEnabled(bool enabled, bool pipelineAvailable) {
    m_state.backendEnabled = enabled && pipelineAvailable;
}

bool VoxelSubsystem::Render(Scene::ECS_Registry* registry, const VoxelDrawContext& ctx) {
    // Build or refresh the dense voxel grid from the current scene. Errors are
    // non-fatal; the shader renders the background gradient when no grid exists.
    if (registry) {
        auto voxelResult = BuildGridFromScene(registry, ctx.device, ctx.descriptorManager);
        if (voxelResult.IsErr()) {
            spdlog::warn("RenderVoxel: {}", voxelResult.Error());
        }
    }

    static bool s_loggedOnce = false;
    if (!s_loggedOnce) {
        spdlog::info("RenderVoxel: voxel backend active, beginning voxel frame");
        s_loggedOnce = true;
    }

    if (!ctx.pipeline) {
        return false;
    }
    if (!ctx.backBuffer) {
        spdlog::error("RenderVoxel: back buffer is null; skipping frame");
        return false;
    }

    VoxelPass::DrawContext drawContext{};
    drawContext.commandList = ctx.commandList;
    drawContext.rootSignature = ctx.rootSignature;
    drawContext.pipeline = ctx.pipeline;
    drawContext.descriptorManager = ctx.descriptorManager;
    drawContext.frameConstants = ctx.frameConstants;
    drawContext.voxelGridSrv = m_state.gridSRV;
    drawContext.backBuffer = ctx.backBuffer;
    drawContext.backBufferRtv = ctx.backBufferRtv;
    drawContext.width = ctx.width;
    drawContext.height = ctx.height;
    if (!VoxelPass::Draw(drawContext)) {
        spdlog::warn("RenderVoxel: voxel pass prerequisites missing; skipping draw");
        return false;
    }
    return true;
}

Result<void> VoxelSubsystem::BuildGridFromScene(Scene::ECS_Registry* registry,
                                                ID3D12Device* device,
                                                DescriptorHeapManager* descriptorManager) {
    if (!registry || !device) {
        return Result<void>::Ok();
    }

    // Skip rebuild when the grid is still valid.
    if (!m_state.gridDirty && !m_state.gridCPU.empty()) {
        return Result<void>::Ok();
    }

    const uint32_t dim = m_state.gridDim;
    const size_t voxelCount = static_cast<size_t>(dim) * static_cast<size_t>(dim) * static_cast<size_t>(dim);
    m_state.gridCPU.assign(voxelCount, 0u);
    m_state.ResetMaterialPalette();

    // World-space voxel volume bounds; must stay in sync with VoxelRaymarch.hlsl.
    const glm::vec3 gridMin(-10.0f, -2.0f, -10.0f);
    const glm::vec3 gridMax( 10.0f,  8.0f,  10.0f);
    const glm::vec3 gridSize = gridMax - gridMin;
    const glm::vec3 cellSize = gridSize / static_cast<float>(dim);

    auto view = registry->View<Scene::RenderableComponent, Scene::TransformComponent>();
    auto& rawReg = registry->GetRegistry();

    auto getMaterialId = [&](const Scene::RenderableComponent& r, entt::entity e) -> uint8_t {
        std::string key;
        if (!r.presetName.empty()) {
            key = r.presetName;
        } else {
            if (auto* tag = rawReg.try_get<Scene::TagComponent>(e)) {
                key = tag->tag;
            }
        }

        if (key.empty()) {
            key = "default";
        }

        auto it = m_state.materialIds.find(key);
        if (it != m_state.materialIds.end()) {
            return it->second;
        }

        uint8_t id = m_state.nextMaterialId;
        if (id == 0u) {
            id = 1u;
        }
        if (m_state.nextMaterialId < 255u) {
            ++m_state.nextMaterialId;
        }
        m_state.materialIds.emplace(std::move(key), id);
        return id;
    };

    auto stampVoxel = [&](const glm::vec3& wp, uint8_t matId) {
        glm::vec3 local = (wp - gridMin) / cellSize;

        int ix = static_cast<int>(std::floor(local.x));
        int iy = static_cast<int>(std::floor(local.y));
        int iz = static_cast<int>(std::floor(local.z));

        if (ix < 0 || iy < 0 || iz < 0 ||
            ix >= static_cast<int>(dim) ||
            iy >= static_cast<int>(dim) ||
            iz >= static_cast<int>(dim)) {
            return;
        }

        const size_t idx =
            static_cast<size_t>(ix) +
            static_cast<size_t>(iy) * dim +
            static_cast<size_t>(iz) * dim * dim;

        if (m_state.gridCPU[idx] == 0u) {
            m_state.gridCPU[idx] = matId;
        }
    };

    const float cellDiag = glm::length(cellSize);
    auto stampSegment = [&](const glm::vec3& a, const glm::vec3& b, uint8_t matId) {
        glm::vec3 delta = b - a;
        float len = glm::length(delta);
        if (len <= 1e-4f) {
            stampVoxel(a, matId);
            return;
        }

        int steps = static_cast<int>(len / cellDiag * 2.0f);
        steps = std::max(1, steps);

        for (int i = 0; i <= steps; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(steps);
            glm::vec3 p = glm::mix(a, b, t);
            stampVoxel(p, matId);
        }
    };

    auto stampTriangleInterior = [&](const glm::vec3& w0,
                                     const glm::vec3& w1,
                                     const glm::vec3& w2,
                                     uint8_t matId) {
        const int kSubdiv = 6; // ~28 samples per triangle
        for (int i = 0; i <= kSubdiv; ++i) {
            float u = static_cast<float>(i) / static_cast<float>(kSubdiv);
            for (int j = 0; j <= kSubdiv - i; ++j) {
                float v = static_cast<float>(j) / static_cast<float>(kSubdiv);
                float w = 1.0f - u - v;
                if (w < 0.0f) {
                    continue;
                }
                glm::vec3 p = u * w0 + v * w1 + w * w2;
                stampVoxel(p, matId);
            }
        }
    };

    for (auto entity : view) {
        auto& renderable = view.get<Scene::RenderableComponent>(entity);
        auto& transform  = view.get<Scene::TransformComponent>(entity);
        if (!renderable.mesh || !renderable.visible) {
            continue;
        }

        const auto& mesh = *renderable.mesh;
        const auto& positions = mesh.positions;
        if (positions.empty()) {
            continue;
        }

        const glm::mat4 world = transform.worldMatrix;
        const uint8_t matId = getMaterialId(renderable, entity);

        const auto& indices = mesh.indices;

        if (!indices.empty()) {
            const size_t triCount = indices.size() / 3;
            for (size_t tri = 0; tri < triCount; ++tri) {
                const uint32_t i0 = indices[tri * 3 + 0];
                const uint32_t i1 = indices[tri * 3 + 1];
                const uint32_t i2 = indices[tri * 3 + 2];
                if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size()) {
                    continue;
                }

                glm::vec3 w0 = glm::vec3(world * glm::vec4(positions[i0], 1.0f));
                glm::vec3 w1 = glm::vec3(world * glm::vec4(positions[i1], 1.0f));
                glm::vec3 w2 = glm::vec3(world * glm::vec4(positions[i2], 1.0f));

                stampVoxel(w0, matId);
                stampVoxel(w1, matId);
                stampVoxel(w2, matId);

                stampSegment(w0, w1, matId);
                stampSegment(w1, w2, matId);
                stampSegment(w2, w0, matId);

                stampTriangleInterior(w0, w1, w2, matId);
            }
        } else {
            for (const auto& p : positions) {
                glm::vec3 wp = glm::vec3(world * glm::vec4(p, 1.0f));
                stampVoxel(wp, matId);
            }
        }
    }

    size_t filled = 0;
    for (uint32_t v : m_state.gridCPU) {
        if (v != 0u) {
            ++filled;
        }
    }
    const double density = static_cast<double>(filled) /
        static_cast<double>(m_state.gridCPU.size());
    spdlog::info("Voxel grid built: dim={} filled={} (density {:.6f})",
                 dim, filled, density);

    auto uploadResult = m_state.UploadGridToGPU(device, descriptorManager);
    if (uploadResult.IsOk()) {
        m_state.gridDirty = false;
    }
    return uploadResult;
}

Result<void> VoxelSubsystem::UploadGridToGPU(ID3D12Device* device,
                                             DescriptorHeapManager* descriptorManager) {
    if (!device || m_state.gridCPU.empty()) {
        return Result<void>::Ok();
    }
    return m_state.UploadGridToGPU(device, descriptorManager);
}

} // namespace Cortex::Graphics
