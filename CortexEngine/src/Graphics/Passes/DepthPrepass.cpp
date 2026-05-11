#include "DepthPrepass.h"

#include "Graphics/MaterialState.h"
#include "Graphics/RenderableClassification.h"
#include "Graphics/RendererGeometryUtils.h"

namespace Cortex::Graphics::DepthPrepass {

namespace {

void Fail(const GraphContext& context, const char* stage) {
    if (context.status.failed) {
        *context.status.failed = true;
    }
    if (context.status.stage) {
        *context.status.stage = stage ? stage : "unknown";
    }
}

[[nodiscard]] bool IsUsable(const GraphContext& context) {
    return context.depth.IsValid() && context.draw.snapshot && context.draw.target.commandList;
}

} // namespace

bool Draw(const DrawContext& context) {
    if (!context.snapshot || !context.target.commandList || !context.depthOnly || !context.objectConstants) {
        return false;
    }

    if (!DepthPrepassTargetPass::BindAndClear(context.target)) {
        return false;
    }

    MeshDrawPass::PipelineStateContext pipelineContext = context.pipeline;
    pipelineContext.pipelineState = context.depthOnly->GetPipelineState();
    if (!MeshDrawPass::BindPipelineState(pipelineContext)) {
        return false;
    }
    DX12Pipeline* currentDepthPipeline = context.depthOnly;

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

        DX12Pipeline* desiredDepthPipeline = nullptr;
        if (IsAlphaTestedDepthClass(depthClass)) {
            desiredDepthPipeline =
                (IsDoubleSidedDepthClass(depthClass) && context.depthOnlyAlphaDoubleSided)
                    ? context.depthOnlyAlphaDoubleSided
                    : context.depthOnlyAlpha;
            if (!desiredDepthPipeline) {
                continue;
            }
        } else {
            desiredDepthPipeline =
                (IsDoubleSidedDepthClass(depthClass) && context.depthOnlyDoubleSided)
                    ? context.depthOnlyDoubleSided
                    : context.depthOnly;
        }

        if (desiredDepthPipeline != currentDepthPipeline) {
            if (!MeshDrawPass::SwitchPipelineState(context.target.commandList,
                                                   desiredDepthPipeline->GetPipelineState())) {
                continue;
            }
            currentDepthPipeline = desiredDepthPipeline;
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
        objectData.depthBiasNdc = sep.depthBiasNdc;

        const D3D12_GPU_VIRTUAL_ADDRESS objectCB =
            context.objectConstants->AllocateAndWrite(objectData);

        D3D12_GPU_VIRTUAL_ADDRESS materialCB = 0;
        DescriptorHandle materialTable{};
        if (IsAlphaTestedDepthClass(depthClass)) {
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

    return true;
}

RGResourceHandle AddToGraph(RenderGraph& graph, const GraphContext& context) {
    if (!IsUsable(context)) {
        Fail(context, "depth_prepass_graph_contract");
        return {};
    }

    graph.AddPass(
        "DepthPrepass",
        [context](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Graphics);
            builder.Write(context.depth, RGResourceUsage::DepthStencilWrite);
        },
        [context](ID3D12GraphicsCommandList*, const RenderGraph&) {
            if (!Draw(context.draw)) {
                Fail(context, "depth_prepass_execute");
            }
        });

    return context.depth;
}

} // namespace Cortex::Graphics::DepthPrepass
