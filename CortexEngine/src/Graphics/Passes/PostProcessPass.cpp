#include "PostProcessPass.h"

#include "DescriptorTable.h"
#include "FullscreenPass.h"

namespace Cortex::Graphics::PostProcessPass {

bool UpdateDescriptorTable(const DescriptorUpdateContext& context) {
    if (!context.device || context.srvTable.empty()) {
        return false;
    }

    auto writeOrNull = [&](size_t slot,
                           ID3D12Resource* resource,
                           DXGI_FORMAT fmt,
                           uint32_t mipLevels = 1) {
        if (slot >= context.srvTable.size() || !context.srvTable[slot].IsValid()) {
            return;
        }

        DescriptorTable::WriteTexture2DSRV(context.device, context.srvTable[slot], nullptr, fmt, mipLevels);
        if (resource) {
            DescriptorTable::WriteTexture2DSRV(context.device, context.srvTable[slot], resource, fmt, mipLevels);
        }
    };

    // Must match PostProcess.hlsl bindings.
    writeOrNull(0, context.hdr, DXGI_FORMAT_R16G16B16A16_FLOAT);

    ID3D12Resource* bloom = nullptr;
    if (context.bloomIntensity > 0.0f) {
        bloom = context.bloomOverride ? context.bloomOverride : context.bloomFallback;
    }
    writeOrNull(1, bloom, DXGI_FORMAT_R11G11B10_FLOAT);

    writeOrNull(2, context.ssao, DXGI_FORMAT_R8_UNORM);
    writeOrNull(3, context.history, DXGI_FORMAT_R16G16B16A16_FLOAT);
    writeOrNull(4, context.depth, DXGI_FORMAT_R32_FLOAT);
    writeOrNull(5, context.normalRoughness, DXGI_FORMAT_R16G16B16A16_FLOAT);

    if (context.wantsHzbDebug && context.hzb && context.hzbMipCount > 0) {
        writeOrNull(6, context.hzb, DXGI_FORMAT_R32_FLOAT, context.hzbMipCount);
    } else {
        writeOrNull(6, context.ssr, DXGI_FORMAT_R16G16B16A16_FLOAT);
    }

    writeOrNull(7, context.velocity, DXGI_FORMAT_R16G16_FLOAT);
    writeOrNull(8, context.rtReflection, DXGI_FORMAT_R16G16B16A16_FLOAT);
    writeOrNull(9, context.rtReflectionHistory, DXGI_FORMAT_R16G16B16A16_FLOAT);
    writeOrNull(10, context.emissiveMetallic, DXGI_FORMAT_R16G16B16A16_FLOAT);
    writeOrNull(11, context.materialExt1, DXGI_FORMAT_R16G16B16A16_FLOAT);
    writeOrNull(12, context.materialExt2, DXGI_FORMAT_R8G8B8A8_UNORM);
    return true;
}

bool Draw(const DrawContext& context) {
    if (!context.commandList || !context.pipeline || !context.pipeline->GetPipelineState() ||
        context.width == 0 || context.height == 0 || !context.targetRtv.ptr ||
        context.srvTable.empty()) {
        return false;
    }

    context.commandList->OMSetRenderTargets(1, &context.targetRtv, FALSE, nullptr);
    FullscreenPass::SetViewportAndScissor(context.commandList, context.width, context.height);

    if (!FullscreenPass::BindGraphicsState({
            context.commandList,
            context.descriptorManager,
            context.rootSignature,
            context.frameConstants,
        })) {
        return false;
    }

    context.commandList->SetPipelineState(context.pipeline->GetPipelineState());
    context.commandList->SetGraphicsRootDescriptorTable(3, context.srvTable[0].gpu);
    if (context.shadowAndEnvironmentTable.IsValid()) {
        context.commandList->SetGraphicsRootDescriptorTable(4, context.shadowAndEnvironmentTable.gpu);
    }

    FullscreenPass::DrawTriangle(context.commandList);
    return true;
}

} // namespace Cortex::Graphics::PostProcessPass
