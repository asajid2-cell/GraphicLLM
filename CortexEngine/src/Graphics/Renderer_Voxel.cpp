#include "Renderer.h"

#include "Graphics/Passes/VoxelPass.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace Cortex::Graphics {

void Renderer::SetVoxelBackendEnabled(bool enabled) {
    m_voxelState.backendEnabled = enabled && (m_pipelineState.voxel != nullptr);
}

bool Renderer::IsVoxelBackendEnabled() const {
    return m_voxelState.backendEnabled;
}

void Renderer::MarkVoxelGridDirty() {
    m_voxelState.gridDirty = true;
}

void Renderer::RenderVoxel(Scene::ECS_Registry* registry) {
    // Build or refresh the dense voxel grid from the current scene so the
    // voxel renderer can visualize real geometry instead of a hardcoded test
    // pattern. Errors here are non-fatal; the shader will simply render the
    // background gradient when no grid is available.
    if (registry) {
        auto voxelResult = BuildVoxelGridFromScene(registry);
        if (voxelResult.IsErr()) {
            spdlog::warn("RenderVoxel: {}", voxelResult.Error());
        }
    }

    static bool s_loggedOnce = false;
    if (!s_loggedOnce) {
        spdlog::info("RenderVoxel: voxel backend active, beginning voxel frame");
        s_loggedOnce = true;
    }

    // Minimal fullscreen voxel prototype. Renders directly into the current
    // back buffer using a fullscreen triangle and the experimental voxel
    // raymarch pixel shader. We intentionally bypass the traditional HDR
    // path here so the prototype can stay self-contained.
    if (!m_services.window || !m_pipelineState.voxel) {
        return;
    }

    ID3D12Resource* backBuffer = m_services.window->GetCurrentBackBuffer();
    if (!backBuffer) {
        spdlog::error("RenderVoxel: back buffer is null; skipping frame");
        return;
    }

    VoxelPass::DrawContext drawContext{};
    drawContext.commandList = m_commandResources.graphicsList.Get();
    drawContext.rootSignature = m_pipelineState.rootSignature.get();
    drawContext.pipeline = m_pipelineState.voxel.get();
    drawContext.descriptorManager = m_services.descriptorManager.get();
    drawContext.frameConstants = m_constantBuffers.currentFrameGPU;
    drawContext.voxelGridSrv = m_voxelState.gridSRV;
    drawContext.backBuffer = backBuffer;
    drawContext.backBufferRtv = m_services.window->GetCurrentRTV();
    drawContext.width = m_services.window->GetWidth();
    drawContext.height = m_services.window->GetHeight();
    if (!VoxelPass::Draw(drawContext)) {
        spdlog::warn("RenderVoxel: voxel pass prerequisites missing; skipping draw");
        return;
    }
    m_frameLifecycle.backBufferUsedAsRTThisFrame = true;

    RecordFramePass("RenderVoxel",
                    true,
                    true,
                    1,
                    {"frame_constants", "voxel_grid"},
                    {"back_buffer"});
}

Result<void> Renderer::BuildVoxelGridFromScene(Scene::ECS_Registry* registry) {
    if (!registry || !m_services.device) {
        return Result<void>::Ok();
    }

    // Skip rebuild when the grid is still valid. This keeps voxelization
    // cost tied to scene changes instead of every frame.
    if (!m_voxelState.gridDirty && !m_voxelState.gridCPU.empty()) {
        return Result<void>::Ok();
    }

    const uint32_t dim = m_voxelState.gridDim;
    const size_t voxelCount = static_cast<size_t>(dim) * static_cast<size_t>(dim) * static_cast<size_t>(dim);
    m_voxelState.gridCPU.assign(voxelCount, 0u);
    m_voxelState.ResetMaterialPalette();

    // World-space bounds for the voxel volume. These must stay in sync with
    // the values used in VoxelRaymarch.hlsl so CPU voxelization and GPU
    // traversal agree on which region of space is discretized.
    // World-space voxel volume bounds. These are chosen to comfortably
    // enclose the curated hero scenes (Cornell, Dragon, RTShowcase) without
    // being so large that the 128^3 grid becomes too sparse. The same
    // numbers must be kept in sync with VoxelRaymarch.hlsl.
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

        auto it = m_voxelState.materialIds.find(key);
        if (it != m_voxelState.materialIds.end()) {
            return it->second;
        }

        uint8_t id = m_voxelState.nextMaterialId;
        if (id == 0u) {
            id = 1u;
        }
        if (m_voxelState.nextMaterialId < 255u) {
            ++m_voxelState.nextMaterialId;
        }
        m_voxelState.materialIds.emplace(std::move(key), id);
        return id;
    };

    // Helper: stamp a single world-space point into the dense voxel grid.
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

        // Only overwrite empty cells so the first material to claim a voxel
        // keeps it; this avoids excessive flicker when multiple meshes touch.
        if (m_voxelState.gridCPU[idx] == 0u) {
            m_voxelState.gridCPU[idx] = matId;
        }
    };

    // Helper: stamp a polyline between two world-space points into the grid.
    // This densifies thin geometry and small props by filling voxels along
    // triangle edges instead of marking only the original vertices.
    const float cellDiag = glm::length(cellSize);
    auto stampSegment = [&](const glm::vec3& a, const glm::vec3& b, uint8_t matId) {
        glm::vec3 delta = b - a;
        float len = glm::length(delta);
        if (len <= 1e-4f) {
            stampVoxel(a, matId);
            return;
        }

        // Choose the number of samples so that we take at least one sample
        // per voxel diagonal along the segment, with a small safety factor.
        int steps = static_cast<int>(len / cellDiag * 2.0f);
        steps = std::max(1, steps);

        for (int i = 0; i <= steps; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(steps);
            glm::vec3 p = glm::mix(a, b, t);
            stampVoxel(p, matId);
        }
    };

    // Helper: stamp interior samples for a triangle using a simple barycentric
    // grid. This significantly reduces gaps on large walls and planes by
    // marking voxels across the full triangle area instead of only its
    // edges. The cost is amortized over scene rebuilds, not per-frame.
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
            // Triangle-based voxelization: stamp vertices and edges for each
            // indexed triangle to get a much denser surface shell, which
            // keeps smaller props and thin features from falling apart.
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

                // Fill the triangle interior with a modest barycentric grid so
                // large planes and walls do not appear as sparse dotted lines.
                stampTriangleInterior(w0, w1, w2, matId);
            }
        } else {
            // Non-indexed meshes: fall back to stamping vertices only.
            for (const auto& p : positions) {
                glm::vec3 wp = glm::vec3(world * glm::vec4(p, 1.0f));
                stampVoxel(wp, matId);
            }
        }
    }

    // Basic diagnostics: count occupied voxels so voxel mode failures can be
    // distinguished between "no data" and shader-side issues.
    size_t filled = 0;
    for (uint32_t v : m_voxelState.gridCPU) {
        if (v != 0u) {
            ++filled;
        }
    }
    const double density = static_cast<double>(filled) /
        static_cast<double>(m_voxelState.gridCPU.size());
    spdlog::info("Voxel grid built: dim={} filled={} (density {:.6f})",
                 dim, filled, density);

    auto uploadResult = UploadVoxelGridToGPU();
    if (uploadResult.IsOk()) {
        m_voxelState.gridDirty = false;
    }
    return uploadResult;
}

Result<void> Renderer::UploadVoxelGridToGPU() {
    if (!m_services.device || m_voxelState.gridCPU.empty()) {
        return Result<void>::Ok();
    }

    ID3D12Device* device = m_services.device->GetDevice();
    if (!device) {
        return Result<void>::Err("UploadVoxelGridToGPU: device is null");
    }

    const UINT64 byteSize = static_cast<UINT64>(m_voxelState.gridCPU.size() * sizeof(uint32_t));

    // Create or resize the upload buffer backing the voxel grid.
    bool recreate = false;
    if (!m_voxelState.gridBuffer) {
        recreate = true;
    } else {
        auto desc = m_voxelState.gridBuffer->GetDesc();
        if (desc.Width < byteSize) {
            recreate = true;
        }
    }

    if (recreate) {
        m_voxelState.gridBuffer.Reset();

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = byteSize;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_voxelState.gridBuffer));

        if (FAILED(hr)) {
            char buf[64];
            sprintf_s(buf, "0x%08X", static_cast<unsigned int>(hr));
            return Result<void>::Err(std::string("Failed to create voxel grid buffer (hr=") + buf + ")");
        }

        // Allocate a persistent SRV slot the first time we create the buffer.
        if (!m_voxelState.gridSRV.IsValid() && m_services.descriptorManager) {
            auto srvResult = m_services.descriptorManager->AllocateCBV_SRV_UAV();
            if (srvResult.IsErr()) {
                return Result<void>::Err("Failed to allocate SRV for voxel grid: " + srvResult.Error());
            }
            m_voxelState.gridSRV = srvResult.Value();
        }

        if (m_voxelState.gridSRV.IsValid()) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.FirstElement = 0;
            srvDesc.Buffer.NumElements = static_cast<UINT>(m_voxelState.gridCPU.size());
            srvDesc.Buffer.StructureByteStride = sizeof(uint32_t);
            srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

            device->CreateShaderResourceView(m_voxelState.gridBuffer.Get(), &srvDesc, m_voxelState.gridSRV.cpu);
        }
    }

    // Upload the CPU voxel data into the buffer.
    void* mapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    HRESULT mapHr = m_voxelState.gridBuffer->Map(0, &readRange, &mapped);
    if (FAILED(mapHr) || !mapped) {
        char buf[64];
        sprintf_s(buf, "0x%08X", static_cast<unsigned int>(mapHr));
        return Result<void>::Err(std::string("Failed to map voxel grid buffer (hr=") + buf + ")");
    }

    memcpy(mapped, m_voxelState.gridCPU.data(), static_cast<size_t>(byteSize));
    m_voxelState.gridBuffer->Unmap(0, nullptr);

    return Result<void>::Ok();
}

// =============================================================================
// Engine Editor Mode: Selective Renderer Usage
// These public wrappers delegate to the private implementation methods,
// allowing EngineEditorMode to control the render flow.
// =============================================================================

} // namespace Cortex::Graphics
