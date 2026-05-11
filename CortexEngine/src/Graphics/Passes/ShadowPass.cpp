#include "ShadowPass.h"

#include "Graphics/MaterialState.h"
#include "Graphics/RenderableClassification.h"
#include "Graphics/RendererGeometryUtils.h"

#include <algorithm>

namespace Cortex::Graphics::ShadowPass {

namespace {

void Fail(const GraphContext& context, const char* stage) {
    if (context.failStage) {
        context.failStage(stage);
    }
}

[[nodiscard]] bool IsUsable(const GraphContext& context) {
    return context.shadowMap.IsValid() && context.draw.snapshot && context.draw.target.commandList;
}

void DrawShadowCasters(const DrawContext& context, DX12Pipeline*& currentPipeline) {
    for (uint32_t entryIndex : context.snapshot->depthWritingIndices) {
        if (entryIndex >= context.snapshot->entries.size()) {
            continue;
        }

        const RendererSceneRenderable& sceneEntry = context.snapshot->entries[entryIndex];
        auto& renderable = *sceneEntry.renderable;
        const RenderableDepthClass depthClass = sceneEntry.depthClass;
        if (!sceneEntry.hasGpuBuffers) {
            continue;
        }

        const bool alphaTest = IsAlphaTestedDepthClass(depthClass);
        const bool doubleSided = IsDoubleSidedDepthClass(depthClass);

        DX12Pipeline* desired = context.shadow;
        if (alphaTest) {
            desired = doubleSided ? context.shadowAlphaDoubleSided : context.shadowAlpha;
        } else {
            desired = doubleSided ? context.shadowDoubleSided : context.shadow;
        }
        if (!desired) {
            desired = context.shadow;
        }
        if (desired && desired != currentPipeline) {
            currentPipeline = desired;
            if (!MeshDrawPass::SwitchPipelineState(context.target.commandList,
                                                   currentPipeline->GetPipelineState())) {
                continue;
            }
        }

        ObjectConstants objectData = {};
        glm::mat4 modelMatrix = sceneEntry.worldMatrix;
        const uint32_t stableKey = static_cast<uint32_t>(sceneEntry.entity);
        if (renderable.mesh && !renderable.mesh->hasBounds) {
            renderable.mesh->UpdateBounds();
        }
        const AutoDepthSeparation sep =
            ComputeAutoDepthSeparationForThinSurfaces(renderable, modelMatrix, stableKey);
        ApplyAutoDepthOffset(modelMatrix, sep.worldOffset);
        objectData.modelMatrix = modelMatrix;
        objectData.normalMatrix = sceneEntry.normalMatrix;

        const D3D12_GPU_VIRTUAL_ADDRESS objectCB =
            context.objectConstants ? context.objectConstants->AllocateAndWrite(objectData) : 0;

        D3D12_GPU_VIRTUAL_ADDRESS materialCB = 0;
        DescriptorHandle materialTable{};
        if (alphaTest && (context.shadowAlpha || context.shadowAlphaDoubleSided)) {
            const MaterialModel materialModel =
                MaterialResolver::ResolveRenderable(renderable, context.materialFallbacks);
            MaterialConstants materialData = MaterialResolver::BuildMaterialConstants(materialModel);
            MaterialResolver::FillMaterialTextureIndices(renderable, materialData);

            if (context.materialConstants) {
                materialCB = context.materialConstants->AllocateAndWrite(materialData);
            }

            if (renderable.textures.gpuState && renderable.textures.gpuState->descriptors[0].IsValid()) {
                materialTable = renderable.textures.gpuState->descriptors[0];
            }
        }

        MeshDrawPass::ObjectMaterialContext materialContext{};
        materialContext.commandList = context.target.commandList;
        materialContext.objectConstants = objectCB;
        materialContext.materialConstants = materialCB;
        materialContext.materialTable = materialTable;
        if (!MeshDrawPass::BindObjectMaterial(materialContext)) {
            continue;
        }

        const auto drawResult =
            MeshDrawPass::DrawIndexedMesh(context.target.commandList, *renderable.mesh);
        if (drawResult.submitted && context.drawCounter) {
            ++(*context.drawCounter);
        }
    }
}

bool DrawShadowSlice(const DrawContext& context, uint32_t slice, DX12Pipeline*& currentPipeline) {
    if (!context.shadowConstants || slice >= context.dsvs.size()) {
        return false;
    }

    ShadowConstants shadowData{};
    shadowData.cascadeIndex = glm::uvec4(slice, 0u, 0u, 0u);
    const D3D12_GPU_VIRTUAL_ADDRESS shadowCB =
        context.shadowConstants->AllocateAndWrite(shadowData);

    MeshDrawPass::ShadowConstantsContext shadowConstantsContext{};
    shadowConstantsContext.commandList = context.target.commandList;
    shadowConstantsContext.frameConstants = context.frameConstants;
    shadowConstantsContext.shadowConstants = shadowCB;
    if (!MeshDrawPass::BindShadowConstants(shadowConstantsContext)) {
        return false;
    }

    ShadowTargetPass::SliceContext sliceContext{};
    sliceContext.commandList = context.target.commandList;
    sliceContext.dsv = context.dsvs[slice];
    sliceContext.viewport = context.viewport;
    sliceContext.scissor = context.scissor;
    if (!ShadowTargetPass::BindAndClearSlice(sliceContext)) {
        return false;
    }

    DrawShadowCasters(context, currentPipeline);
    return true;
}

} // namespace

bool Draw(const DrawContext& context) {
    if (!context.snapshot || !context.target.commandList || !context.target.shadowMap || !context.shadow) {
        return false;
    }

    if (!ShadowTargetPass::TransitionToDepthWrite(context.target)) {
        return false;
    }

    DX12Pipeline* currentPipeline = context.shadow;
    MeshDrawPass::PipelineStateContext pipelineContext = context.pipeline;
    pipelineContext.pipelineState = currentPipeline->GetPipelineState();
    if (!MeshDrawPass::BindPipelineState(pipelineContext)) {
        return false;
    }

    for (uint32_t cascadeIndex = 0; cascadeIndex < context.cascadeCount; ++cascadeIndex) {
        (void)DrawShadowSlice(context, cascadeIndex, currentPipeline);
    }

    if (context.localShadowHasShadow && context.localShadowCount > 0) {
        const uint32_t maxLocal = std::min(context.localShadowCount, context.maxShadowedLocalLights);
        for (uint32_t i = 0; i < maxLocal; ++i) {
            const uint32_t slice = context.cascadeCount + i;
            if (slice >= context.shadowArraySize) {
                break;
            }
            (void)DrawShadowSlice(context, slice, currentPipeline);
        }
    }

    return ShadowTargetPass::TransitionToShaderResource(context.target);
}

RGResourceHandle AddToGraph(RenderGraph& graph, const GraphContext& context) {
    if (!IsUsable(context)) {
        Fail(context, "shadow_graph_contract");
        return {};
    }

    graph.AddPass(
        "ShadowPass",
        [context](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Graphics);
            builder.Write(context.shadowMap, RGResourceUsage::DepthStencilWrite);
        },
        [context](ID3D12GraphicsCommandList*, const RenderGraph&) {
            if (!Draw(context.draw)) {
                Fail(context, "shadow_execute");
            }
        });

    graph.AddPass(
        "ShadowFinalize",
        [context](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Graphics);
            builder.Read(context.shadowMap, RGResourceUsage::ShaderResource);
        },
        [](ID3D12GraphicsCommandList*, const RenderGraph&) {});

    return context.shadowMap;
}

} // namespace Cortex::Graphics::ShadowPass
