#include "Graphics/Subsystems/WaterSubsystem.h"

#include "Graphics/Passes/ForwardTargetBindingPass.h"
#include "Graphics/Passes/FullscreenPass.h"
#include "Graphics/Passes/MeshDrawPass.h"
#include "Graphics/MaterialPresetRegistry.h"
#include "Graphics/MaterialState.h"
#include "Graphics/RenderableClassification.h"
#include "Graphics/RendererGeometryUtils.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <algorithm>
#include <cstdlib>

namespace Cortex::Graphics {

namespace {

using LiquidType = Scene::WaterSurfaceComponent::LiquidType;

LiquidType LiquidTypeFromPreset(const Scene::RenderableComponent& renderable) {
    if (MaterialPresetRegistry::ContainsToken(renderable.presetName, "lava")) {
        return LiquidType::Lava;
    }
    if (MaterialPresetRegistry::ContainsToken(renderable.presetName, "honey")) {
        return LiquidType::Honey;
    }
    if (MaterialPresetRegistry::ContainsToken(renderable.presetName, "molasses")) {
        return LiquidType::Molasses;
    }
    return LiquidType::Water;
}

Scene::WaterSurfaceComponent LiquidProfileFromRenderable(const Scene::RenderableComponent& renderable) {
    Scene::WaterSurfaceComponent profile{};
    profile.liquidType = LiquidTypeFromPreset(renderable);

    switch (profile.liquidType) {
    case LiquidType::Lava:
        profile.absorption = 0.95f; profile.foamStrength = 0.0f; profile.viscosity = 0.82f;
        profile.emissiveHeat = 4.8f; profile.bodyThickness = 0.82f; profile.sloshStrength = 0.12f;
        profile.meniscusStrength = 0.58f; profile.flowSpeed = 0.48f;
        profile.shallowTint = glm::vec3(1.0f, 0.38f, 0.05f); profile.deepTint = glm::vec3(0.22f, 0.035f, 0.01f);
        break;
    case LiquidType::Honey:
        profile.absorption = 0.62f; profile.foamStrength = 0.08f; profile.viscosity = 0.76f;
        profile.emissiveHeat = 0.0f; profile.bodyThickness = 0.76f; profile.sloshStrength = 0.10f;
        profile.meniscusStrength = 0.70f; profile.flowSpeed = 0.34f;
        profile.shallowTint = glm::vec3(1.0f, 0.72f, 0.18f); profile.deepTint = glm::vec3(0.52f, 0.24f, 0.035f);
        break;
    case LiquidType::Molasses:
        profile.absorption = 0.88f; profile.foamStrength = 0.02f; profile.viscosity = 0.95f;
        profile.emissiveHeat = 0.0f; profile.bodyThickness = 0.92f; profile.sloshStrength = 0.06f;
        profile.meniscusStrength = 0.82f; profile.flowSpeed = 0.18f;
        profile.shallowTint = glm::vec3(0.26f, 0.12f, 0.045f); profile.deepTint = glm::vec3(0.035f, 0.012f, 0.006f);
        break;
    case LiquidType::Water:
    default:
        profile.absorption = 0.42f; profile.foamStrength = 0.82f; profile.viscosity = 0.18f;
        profile.emissiveHeat = 0.0f; profile.bodyThickness = 0.46f; profile.sloshStrength = 0.30f;
        profile.meniscusStrength = 0.40f; profile.flowSpeed = 1.0f;
        profile.shallowTint = glm::vec3(0.10f, 0.50f, 0.78f); profile.deepTint = glm::vec3(0.005f, 0.07f, 0.22f);
        break;
    }
    return profile;
}

} // namespace

void WaterSubsystem::Render(Scene::ECS_Registry* registry, const WaterRenderContext& ctx) {
    if (std::getenv("CORTEX_DISABLE_WATER_PASS")) {
        return;
    }
    if (!ctx.waterPipelineState || !ctx.hdrColor || !ctx.depthBuffer) {
        return;
    }

    RendererSceneSnapshot fallbackSnapshot{};
    const RendererSceneSnapshot* snapshot = ctx.sceneSnapshot;
    if (!snapshot || !snapshot->IsValidForFrame(ctx.renderFrameCounter)) {
        fallbackSnapshot = BuildRendererSceneSnapshot(registry, ctx.renderFrameCounter);
        snapshot = &fallbackSnapshot;
    }
    if (snapshot->waterIndices.empty()) {
        return;
    }

    ForwardTargetBindingPass::BindContext targetContext{};
    targetContext.commandList = ctx.commandList;
    targetContext.hdrColor = ctx.hdrColor;
    targetContext.hdrState = ctx.hdrState;
    targetContext.hdrRtv = ctx.hdrRtv;
    targetContext.depthBuffer = ctx.depthBuffer;
    targetContext.depthState = ctx.depthState;
    targetContext.depthDsv = ctx.depthDsv;
    targetContext.readOnlyDepthDsv = ctx.readOnlyDepthDsv;
    targetContext.readOnlyDepthState = kDepthSampleState;
    if (!ForwardTargetBindingPass::BindHdrAndDepthReadOnly(targetContext)) {
        return;
    }

    FullscreenPass::SetViewportAndScissor(ctx.commandList, ctx.hdrColor);

    MeshDrawPass::PipelineStateContext pipelineContext{};
    pipelineContext.commandList = ctx.commandList;
    pipelineContext.rootSignature = ctx.rootSignature;
    pipelineContext.pipelineState = ctx.waterPipelineState;
    pipelineContext.cbvSrvUavHeap = ctx.cbvSrvUavHeap;
    pipelineContext.frameConstants = ctx.frameConstants;
    pipelineContext.shadowEnvironmentTable = ctx.shadowEnvironmentTable;
    if (!MeshDrawPass::BindPipelineState(pipelineContext)) {
        return;
    }

    const FrustumPlanes frustum = ExtractFrustumPlanesCPU(ctx.viewProjectionNoJitter);

    for (uint32_t entryIndex : snapshot->waterIndices) {
        if (entryIndex >= snapshot->entries.size()) {
            continue;
        }
        const RendererSceneRenderable& entry = snapshot->entries[entryIndex];
        auto* renderablePtr = entry.renderable;
        if (!entry.visible || !entry.hasMesh || !entry.hasTransform || !renderablePtr) {
            continue;
        }
        auto& renderable = *renderablePtr;

        if (!renderable.mesh) {
            continue;
        }
        if (!renderable.mesh->hasBounds) {
            renderable.mesh->UpdateBounds();
        }
        glm::vec3 centerWS = glm::vec3(entry.worldMatrix[3]);
        float radiusWS = 1.0f;
        if (renderable.mesh->hasBounds) {
            centerWS = glm::vec3(entry.worldMatrix * glm::vec4(renderable.mesh->boundsCenter, 1.0f));
            radiusWS = renderable.mesh->boundsRadius * GetMaxWorldScale(entry.worldMatrix);
        }
        if (!SphereIntersectsFrustumCPU(frustum, centerWS, radiusWS)) {
            continue;
        }

        if (ctx.prepareMaterial) {
            ctx.prepareMaterial(renderable);
        }

        Scene::WaterSurfaceComponent liquidProfile = LiquidProfileFromRenderable(renderable);
        if (registry && registry->HasComponent<Scene::WaterSurfaceComponent>(entry.entity)) {
            liquidProfile = registry->GetComponent<Scene::WaterSurfaceComponent>(entry.entity);
        }

        MaterialConstants materialData{};
        materialData.albedo    = renderable.albedoColor;
        materialData.metallic  = 0.0f;
        materialData.roughness = glm::clamp(m_state.roughness, 0.01f, 1.0f);
        materialData.ao        = glm::clamp(renderable.ao, 0.0f, 1.0f);
        materialData._pad0     = 0.0f;
        materialData.mapFlags  = glm::uvec4(0u);
        materialData.mapFlags2 = glm::uvec4(0u);
        materialData.emissiveFactorStrength =
            glm::vec4(glm::max(liquidProfile.shallowTint, glm::vec3(0.0f)),
                      glm::max(liquidProfile.emissiveHeat, 0.0f));
        materialData.extraParams =
            glm::vec4(glm::clamp(liquidProfile.absorption, 0.0f, 1.0f),
                      glm::clamp(liquidProfile.foamStrength, 0.0f, 2.0f),
                      glm::clamp(liquidProfile.viscosity, 0.0f, 1.0f),
                      static_cast<float>(liquidProfile.liquidType));
        materialData.fractalParams0 =
            glm::vec4(glm::max(liquidProfile.shallowTint, glm::vec3(0.0f)),
                      glm::clamp(renderable.albedoColor.a, 0.0f, 1.0f));
        materialData.fractalParams1 =
            glm::vec4(glm::max(liquidProfile.deepTint, glm::vec3(0.0f)),
                      glm::max(liquidProfile.emissiveHeat, 0.0f));
        materialData.transmissionParams =
            glm::vec4(glm::clamp(renderable.transmissionFactor, 0.0f, 1.0f),
                      glm::max(renderable.ior, 1.0f),
                      glm::max(liquidProfile.emissiveHeat, 0.0f),
                      0.0f);
        materialData.specularParams =
            glm::vec4(glm::clamp(m_state.fresnelStrength, 0.0f, 3.0f),
                      glm::clamp(liquidProfile.foamStrength, 0.0f, 2.0f),
                      glm::clamp(liquidProfile.absorption, 0.0f, 1.0f),
                      static_cast<float>(liquidProfile.liquidType));
        materialData.coatParams =
            glm::vec4(glm::clamp(liquidProfile.bodyThickness, 0.0f, 1.5f),
                      glm::clamp(liquidProfile.meniscusStrength, 0.0f, 1.5f),
                      glm::clamp(liquidProfile.sloshStrength, 0.0f, 1.5f),
                      glm::max(liquidProfile.flowSpeed, 0.0f));

        glm::mat4 modelMatrix = entry.worldMatrix;
        const uint32_t stableKey = static_cast<uint32_t>(entry.entity);
        const AutoDepthSeparation sep =
            ComputeAutoDepthSeparationForThinSurfaces(renderable, modelMatrix, stableKey);
        ApplyAutoDepthOffset(modelMatrix, sep.worldOffset);

        ObjectConstants objectData{};
        objectData.modelMatrix  = modelMatrix;
        objectData.normalMatrix = entry.normalMatrix;
        objectData.depthBiasNdc = sep.depthBiasNdc;
        objectData._pad0 = glm::vec3(
            glm::clamp(liquidProfile.bodyThickness, 0.0f, 1.5f),
            glm::clamp(liquidProfile.sloshStrength, 0.0f, 1.5f),
            glm::clamp(liquidProfile.meniscusStrength, 0.0f, 1.5f));

        D3D12_GPU_VIRTUAL_ADDRESS objectCB = ctx.allocObjectConstants ? ctx.allocObjectConstants(objectData) : 0;
        D3D12_GPU_VIRTUAL_ADDRESS materialCB = ctx.allocMaterialConstants ? ctx.allocMaterialConstants(materialData) : 0;

        DescriptorHandle materialTable{};
        if (renderable.textures.gpuState && renderable.textures.gpuState->descriptors[0].IsValid()) {
            materialTable = renderable.textures.gpuState->descriptors[0];
        } else if (ctx.materialFallbackTable.IsValid()) {
            materialTable = ctx.materialFallbackTable;
        }

        MeshDrawPass::ObjectMaterialContext materialContext{};
        materialContext.commandList = ctx.commandList;
        materialContext.objectConstants = objectCB;
        materialContext.materialConstants = materialCB;
        materialContext.materialTable = materialTable;
        if (!MeshDrawPass::BindObjectMaterial(materialContext)) {
            continue;
        }

        const auto drawResult =
            MeshDrawPass::DrawIndexedMesh(ctx.commandList, *renderable.mesh);
        if (drawResult.submitted && ctx.outWaterDraws) {
            ++(*ctx.outWaterDraws);
        }
    }
}

} // namespace Cortex::Graphics
