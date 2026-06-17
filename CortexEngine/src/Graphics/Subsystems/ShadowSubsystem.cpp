#include "Graphics/Subsystems/ShadowSubsystem.h"

#include "Graphics/Passes/ShadowPass.h"
#include "Graphics/RenderableClassification.h"
#include "Graphics/BudgetPlanner.h"
#include "Graphics/RHI/DX12Device.h"
#include "Scene/ECS_Registry.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <span>

namespace Cortex::Graphics {

Result<void> ShadowSubsystem::CreateResources(const ShadowContext& ctx) {
    if (!ctx.device || !ctx.descriptorManager) {
        return Result<void>::Err("Renderer not initialized for shadow map creation");
    }

    const auto budget = BudgetPlanner::BuildPlan(
        ctx.device->GetDedicatedVideoMemoryBytes(),
        std::max(1u, ctx.windowWidth),
        std::max(1u, ctx.windowHeight));
    m_resources.controls.mapSize = static_cast<float>(std::max(1u, budget.shadowMapSize));
    const UINT shadowDim = static_cast<UINT>(m_resources.controls.mapSize);

    auto result = m_resources.resources.CreateMap(
        ctx.device->GetDevice(),
        ctx.descriptorManager,
        shadowDim);
    if (result.IsErr()) {
        return result;
    }

    m_resources.raster.viewport.TopLeftX = 0.0f;
    m_resources.raster.viewport.TopLeftY = 0.0f;
    m_resources.raster.viewport.Width = static_cast<float>(shadowDim);
    m_resources.raster.viewport.Height = static_cast<float>(shadowDim);
    m_resources.raster.viewport.MinDepth = 0.0f;
    m_resources.raster.viewport.MaxDepth = 1.0f;

    m_resources.raster.scissor.left = 0;
    m_resources.raster.scissor.top = 0;
    m_resources.raster.scissor.right = static_cast<LONG>(shadowDim);
    m_resources.raster.scissor.bottom = static_cast<LONG>(shadowDim);

    spdlog::info("Shadow map created ({}x{})", shadowDim, shadowDim);

    if (ctx.updateEnvironmentTable) {
        ctx.updateEnvironmentTable();
    }
    return Result<void>::Ok();
}

void ShadowSubsystem::RecreateForCurrentSize(const ShadowContext& ctx) {
    if (!ctx.device || !ctx.descriptorManager) {
        return;
    }
    if (!m_resources.resources.map) {
        return;
    }

    D3D12_RESOURCE_DESC currentDesc = m_resources.resources.map->GetDesc();
    const UINT desiredDim = static_cast<UINT>(m_resources.controls.mapSize);

    if (currentDesc.Width <= desiredDim && currentDesc.Height <= desiredDim) {
        return;
    }

    m_resources.resources.map.Reset();
    m_resources.resources.srv = {};
    for (auto& dsv : m_resources.resources.dsvs) {
        dsv = {};
    }

    auto result = CreateResources(ctx);
    if (result.IsErr()) {
        spdlog::warn("Renderer: failed to recreate shadow map at safe size: {}", result.Error());
        m_resources.controls.enabled = false;
    }
}

void ShadowSubsystem::RenderPass(Scene::ECS_Registry* registry, const ShadowContext& ctx) {
    if (!registry || !m_resources.resources.map || !ctx.shadowPipelineValid) {
        return;
    }

    RendererSceneSnapshot localSnapshot{};
    const RendererSceneSnapshot* snapshot = ctx.sceneSnapshot;
    if (!snapshot || !snapshot->IsValidForFrame(ctx.renderFrameCounter)) {
        localSnapshot = BuildRendererSceneSnapshot(registry, ctx.renderFrameCounter);
        snapshot = &localSnapshot;
    }
    for (uint32_t entryIndex : snapshot->depthWritingIndices) {
        if (entryIndex >= snapshot->entries.size()) {
            continue;
        }
        const RendererSceneRenderable& sceneEntry = snapshot->entries[entryIndex];
        if (IsAlphaTestedDepthClass(sceneEntry.depthClass) && sceneEntry.renderable && ctx.prepareMaterial) {
            ctx.prepareMaterial(*sceneEntry.renderable);
        }
    }

    ShadowPass::DrawContext draw{};
    draw.target.commandList = ctx.commandList;
    draw.target.shadowMap = m_resources.resources.map.Get();
    draw.target.resourceState = &m_resources.resources.resourceState;
    draw.target.initializedForEditor = &m_resources.resources.initializedForEditor;
    draw.target.skipTransitions = ctx.skipTransitions;
    draw.dsvs = std::span<const DescriptorHandle>(m_resources.resources.dsvs.data(),
                                                  m_resources.resources.dsvs.size());
    draw.viewport = m_resources.raster.viewport;
    draw.scissor = m_resources.raster.scissor;
    draw.pipeline.commandList = ctx.commandList;
    draw.pipeline.rootSignature = ctx.rootSignature;
    draw.pipeline.cbvSrvUavHeap = ctx.cbvSrvUavHeap;
    draw.shadow = ctx.shadow;
    draw.shadowDoubleSided = ctx.shadowDoubleSided;
    draw.shadowAlpha = ctx.shadowAlpha;
    draw.shadowAlphaDoubleSided = ctx.shadowAlphaDoubleSided;
    draw.snapshot = snapshot;
    draw.objectConstants = ctx.objectConstants;
    draw.materialConstants = ctx.materialConstants;
    draw.shadowConstants = ctx.shadowConstants;
    draw.frameConstants = ctx.frameConstants;
    draw.materialFallbacks = ctx.materialFallbacks;
    draw.drawCounter = ctx.outShadowDraws;
    draw.cascadeCount = kShadowCascadeCount;
    draw.maxShadowedLocalLights = kMaxShadowedLocalLights;
    draw.shadowArraySize = kShadowArraySize;
    draw.localShadowHasShadow = m_local.hasShadow;
    draw.localShadowCount = m_local.count;

    (void)ShadowPass::Draw(draw);
}

} // namespace Cortex::Graphics
