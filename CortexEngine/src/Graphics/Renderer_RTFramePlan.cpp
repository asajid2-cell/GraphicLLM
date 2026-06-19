#include "Renderer.h"

// RTSubsystem context builder + frame-plan forwarder. The ray-tracing family
// (shadows, reflections, GI, denoise, signal stats) lives in
// Graphics/Subsystems/RTSubsystem; MakeRTContext snapshots the per-frame
// Renderer dependencies it needs.
namespace Cortex::Graphics {

RTContext Renderer::MakeRTContext() {
    RTContext ctx{};
    ctx.device = m_services.device;
    ctx.descriptorManager = m_services.descriptorManager.get();
    ctx.rayTracingContext = m_services.rayTracingContext.get();
    ctx.rtDenoiser = m_services.rtDenoiser.get();
    ctx.rtReflectionSignalStats = m_services.rtReflectionSignalStats.get();
    ctx.visibilityBuffer = m_services.visibilityBuffer.get();
    ctx.window = m_services.window;
    ctx.commandList = m_commandResources.graphicsList.Get();

    ctx.frameConstants = m_constantBuffers.currentFrameGPU;
    ctx.frameIndex = m_frameRuntime.frameIndex;
    ctx.absoluteFrameIndex = m_frameRuntime.absoluteFrameIndex;
    ctx.renderFrameCounter = m_frameLifecycle.renderFrameCounter;
    ctx.internalRenderWidth = GetInternalRenderWidth();
    ctx.internalRenderHeight = GetInternalRenderHeight();

    ctx.depthBuffer = m_depthResources.resources.buffer.Get();
    ctx.depthState = &m_depthResources.resources.resourceState;
    ctx.depthSrv = m_depthResources.descriptors.srv;

    ctx.hdrColor = m_mainTargets.hdr.resources.color.Get();
    ctx.normalRoughness = m_mainTargets.normalRoughness.resources.texture.Get();
    ctx.normalRoughnessState = &m_mainTargets.normalRoughness.resources.state;
    ctx.normalRoughnessSrv = m_mainTargets.normalRoughness.descriptors.srv;
    ctx.previousLitColor = m_temporal.ScreenState().historyColor.Get();
    ctx.previousLitColorState = &m_temporal.ScreenState().historyState;
    ctx.previousLitColorSrv = m_temporal.ScreenState().historySRV;

    ctx.maskTexture = m_temporalMaskState.texture.Get();
    ctx.maskState = &m_temporalMaskState.resourceState;
    ctx.maskSrv = m_temporalMaskState.srv;
    ctx.maskBuiltThisFrame = m_temporalMaskState.builtThisFrame;

    ctx.velocityBuffer = m_temporal.ScreenState().velocityBuffer.Get();
    ctx.velocityState = &m_temporal.ScreenState().velocityState;
    ctx.velocitySrv = m_temporal.ScreenState().velocitySRV;

    ctx.environment = &m_environmentState;
    ctx.visibilityBufferRenderedThisFrame = m_vb.State().renderedThisFrame;
    ctx.debugViewMode = m_debugViewState.mode;
    ctx.historyManager = &m_temporalHistory.manager;

    ctx.framePlanning = &m_framePlanning;
    ctx.assetRuntime = &m_assetRuntime;
    ctx.rtReflectionWrittenThisFrame = &m_frameLifecycle.rtReflectionWrittenThisFrame;

    ctx.updateEnvironmentTable = [this]() { UpdateEnvironmentDescriptorTable(); };
    ctx.recordFramePass = [this](const char* name,
                                 bool planned,
                                 bool executed,
                                 uint32_t drawCount,
                                 std::initializer_list<const char*> reads,
                                 std::initializer_list<const char*> writes,
                                 bool fallbackUsed,
                                 const char* fallbackReason) {
        RecordFramePass(name, planned, executed, drawCount, reads, writes, fallbackUsed, fallbackReason);
    };
    return ctx;
}

void Renderer::UpdateRTFramePlan(const FrameFeaturePlan& featurePlan) {
    m_rt.UpdateRTFramePlan(featurePlan, MakeRTContext());
}

} // namespace Cortex::Graphics
