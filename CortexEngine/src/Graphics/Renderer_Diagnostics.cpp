#include "Renderer.h"
#include "RenderGraph.h"
#include "Debug/GPUProfiler.h"
#include "Core/Window.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"
#include "Graphics/TextureLoader.h"
#include "Graphics/MaterialState.h"
#include "Graphics/MeshBuffers.h"
#include "Graphics/SurfaceClassification.h"
#include "Graphics/RenderableClassification.h"
#include "Graphics/FrameContractValidation.h"
#include "Graphics/FrameContractResources.h"
#include "Graphics/RendererGeometryUtils.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

Renderer::QualityState Renderer::GetQualityState() const {
    QualityState state{};
    state.activeGraphicsPresetId = m_qualityRuntimeState.activeGraphicsPresetId.empty()
        ? "runtime"
        : m_qualityRuntimeState.activeGraphicsPresetId;
    state.graphicsPresetDirtyFromUI = m_qualityRuntimeState.graphicsPresetDirtyFromUI;
    state.exposure = m_qualityRuntimeState.exposure;
    state.bloomIntensity = m_bloomResources.intensity;
    state.renderScale = m_qualityRuntimeState.renderScale;
    state.shadowsEnabled = m_shadowResources.enabled;
    state.debugViewMode = static_cast<int>(m_debugViewState.mode);
    state.hzbDebugMip = m_hzbResources.debugMip;
    state.shadowBias = m_shadowResources.bias;
    state.shadowPCFRadius = m_shadowResources.pcfRadius;
    state.cascadeSplitLambda = m_shadowResources.cascadeSplitLambda;
    state.cascade0ResolutionScale = m_shadowResources.cascadeResolutionScale[0];
    state.visualValidationCaptured = m_frameLifecycle.visualValidationCaptured;
    return state;
}

Renderer::FeatureState Renderer::GetFeatureState() const {
    FeatureState state{};
    state.taaEnabled = m_temporalAAState.enabled;
    state.fxaaEnabled = m_postProcessState.fxaaEnabled;
    state.pcssEnabled = m_shadowResources.pcssEnabled;
    state.ssaoEnabled = m_ssaoResources.enabled;
    state.ssaoRadius = m_ssaoResources.radius;
    state.ssaoBias = m_ssaoResources.bias;
    state.ssaoIntensity = m_ssaoResources.intensity;
    state.iblEnabled = m_environmentState.enabled;
    state.iblLimitEnabled = m_environmentState.limitEnabled;
    state.ssrEnabled = m_ssrResources.enabled;
    state.ssrMaxDistance = m_ssrResources.maxDistance;
    state.ssrThickness = m_ssrResources.thickness;
    state.ssrStrength = m_ssrResources.strength;
    state.fogEnabled = m_fogState.enabled;
    state.particlesEnabled = m_particleState.enabledForScene;
    state.particleDensityScale = m_particleState.densityScale;
    state.vegetationEnabled = m_vegetationState.enabled;
    state.fogDensity = m_fogState.density;
    state.fogHeight = m_fogState.height;
    state.fogFalloff = m_fogState.falloff;
    state.iblDiffuseIntensity = m_environmentState.diffuseIntensity;
    state.iblSpecularIntensity = m_environmentState.specularIntensity;
    state.backgroundVisible = m_environmentState.backgroundVisible;
    state.backgroundExposure = m_environmentState.backgroundExposure;
    state.backgroundBlur = m_environmentState.backgroundBlur;
    state.godRayIntensity = m_postProcessState.godRayIntensity;
    state.areaLightSizeScale = m_lightingState.areaLightSizeScale;
    state.sunIntensity = m_lightingState.directionalIntensity;
    state.useSafeLightingRigOnLowVRAM = m_lightingState.useSafeRigOnLowVRAM;
    return state;
}

Renderer::RayTracingState Renderer::GetRayTracingState() const {
    RayTracingState state{};
    state.supported = m_rtRuntimeState.supported;
    state.enabled = m_rtRuntimeState.enabled;
    state.reflectionsEnabled = m_rtRuntimeState.reflectionsEnabled;
    state.giEnabled = m_rtRuntimeState.giEnabled;
    state.warmingUp = IsRTWarmingUp();
    state.reflectionDenoiseAlpha = m_rtDenoiseState.reflectionHistoryAlpha;
    state.reflectionCompositionStrength = m_rtDenoiseState.reflectionCompositionStrength;
    state.reflectionRoughnessThreshold = m_rtDenoiseState.reflectionRoughnessThreshold;
    state.reflectionHistoryMaxBlend = m_rtDenoiseState.reflectionHistoryMaxBlend;
    state.reflectionFireflyClampLuma = m_rtDenoiseState.reflectionFireflyClampLuma;
    state.reflectionSignalScale = m_rtDenoiseState.reflectionSignalScale;
    return state;
}

Renderer::HealthState Renderer::BuildHealthState() const {
    HealthState state{};
    state.adapterName = "unknown";
    if (m_services.device && m_services.device->GetAdapter()) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(m_services.device->GetAdapter()->GetDesc1(&desc))) {
            std::wstring wide(desc.Description);
            state.adapterName.clear();
            state.adapterName.reserve(wide.size());
            for (wchar_t c : wide) {
                if (c == L'\0') {
                    break;
                }
                state.adapterName.push_back(c >= 0 && c < 128 ? static_cast<char>(c) : '?');
            }
            if (state.adapterName.empty()) {
                state.adapterName = "unknown";
            }
        }
    }

    state.qualityPreset = m_framePlanning.budgetPlan.profileName.empty()
        ? "runtime"
        : m_framePlanning.budgetPlan.profileName;
    state.graphicsPresetId = m_qualityRuntimeState.activeGraphicsPresetId.empty()
        ? "runtime"
        : m_qualityRuntimeState.activeGraphicsPresetId;
    state.graphicsPresetDirtyFromUI = m_qualityRuntimeState.graphicsPresetDirtyFromUI;
    state.rayTracingRequested = m_rtRuntimeState.enabled ||
                                m_rtRuntimeState.reflectionsEnabled ||
                                m_rtRuntimeState.giEnabled;
    state.rayTracingEffective = m_rtRuntimeState.supported &&
                                m_rtRuntimeState.enabled &&
                                !m_frameLifecycle.deviceRemoved;
    state.environmentLoaded = m_environmentState.HasResidentEnvironment();
    state.activeEnvironment = m_environmentState.ActiveEnvironmentName();
    state.environmentFallback = m_environmentState.UsingFallbackEnvironment();
    state.residentEnvironments = m_environmentState.ResidentCount();
    state.pendingEnvironments = m_environmentState.PendingCount();
    state.frameWarnings = static_cast<uint32_t>(m_frameDiagnostics.contract.contract.warnings.size());
    state.assetFallbacks = state.environmentFallback ? 1u : 0u;

    const DescriptorStats descriptors = GetDescriptorStats();
    state.descriptorPersistentUsed = descriptors.persistentUsed;
    state.descriptorPersistentBudget = descriptors.persistentReserve;
    state.descriptorTransientUsed =
        descriptors.transientEnd > descriptors.transientStart
            ? descriptors.transientEnd - descriptors.transientStart
            : 0u;
    state.descriptorTransientBudget = state.descriptorTransientUsed;

    const VRAMBreakdown vram = GetEstimatedVRAMBreakdown();
    state.estimatedVRAMBytes = vram.TotalBytes();
    state.textureBytes = vram.textureBytes;
    state.environmentBytes = vram.environmentBytes;
    state.geometryBytes = vram.geometryBytes;
    state.rtStructureBytes = vram.rtStructureBytes;

    if (!m_frameDiagnostics.contract.contract.warnings.empty()) {
        state.lastWarningMessage = m_frameDiagnostics.contract.contract.warnings.back();
        const size_t split = state.lastWarningMessage.find(':');
        state.lastWarningCode = split == std::string::npos
            ? state.lastWarningMessage
            : state.lastWarningMessage.substr(0, split);
    }
    return state;
}

Renderer::WaterState Renderer::GetWaterState() const {
    return m_waterState;
}

Renderer::PostProcessState Renderer::GetPostProcessState() const {
    PostProcessState state = m_postProcessState;
    state.bloomThreshold = m_bloomResources.threshold;
    state.bloomSoftKnee = m_bloomResources.softKnee;
    state.bloomMaxContribution = m_bloomResources.maxContribution;
    return state;
}

bool Renderer::HasCapturedVisualValidation() const {
    return GetQualityState().visualValidationCaptured;
}

const AssetRegistry& Renderer::GetAssetRegistry() const {
    return m_assetRuntime.registry;
}

AssetRegistry::MemoryBreakdown Renderer::GetAssetMemoryBreakdown() const {
    return m_assetRuntime.registry.GetMemoryBreakdown();
}

const std::vector<TextureUploadReceipt>& Renderer::GetTextureUploadReceipts() const {
    return m_assetRuntime.textureUploads.receipts;
}

const TextureUploadQueueStats& Renderer::GetTextureUploadQueueStats() const {
    return m_assetRuntime.textureUploads.queue.stats;
}

const std::unordered_map<const Scene::MeshData*, std::string>& Renderer::GetMeshAssetKeys() const {
    return m_assetRuntime.meshAssetKeys;
}

float Renderer::GetLastMainPassTimeMS() const {
    return m_frameDiagnostics.timings.mainPassMs;
}

float Renderer::GetLastRTTimeMS() const {
    return m_frameDiagnostics.timings.rtPassMs;
}

float Renderer::GetLastPostTimeMS() const {
    return m_frameDiagnostics.timings.postMs;
}

float Renderer::GetLastDepthPrepassTimeMS() const {
    return m_frameDiagnostics.timings.depthPrepassMs;
}

float Renderer::GetLastShadowPassTimeMS() const {
    return m_frameDiagnostics.timings.shadowPassMs;
}

float Renderer::GetLastSSRTimeMS() const {
    return m_frameDiagnostics.timings.ssrMs;
}

float Renderer::GetLastSSAOTimeMS() const {
    return m_frameDiagnostics.timings.ssaoMs;
}

float Renderer::GetLastBloomTimeMS() const {
    return m_frameDiagnostics.timings.bloomMs;
}

const FrameContract& Renderer::GetFrameContract() const {
    return m_frameDiagnostics.contract.contract;
}

void Renderer::MarkPassComplete(const char* passName) {
    m_frameLifecycle.lastCompletedPass = passName ? passName : "Unknown";
}

// Centralized device-removed reporting. Any code path that encounters a
// failure HRESULT and suspects device loss should call this helper so we
// emit a consistent, information-rich log entry (context, hr, device
// removed reason, frame index, last completed pass, file/line).
void Renderer::ReportDeviceRemoved(const char* context,
                                   HRESULT hr,
                                   const char* file,
                                   int line) {
    const char* ctx = context ? context : "Unknown";

    HRESULT reason = S_OK;
    if (m_services.device && m_services.device->GetDevice()) {
        reason = m_services.device->GetDevice()->GetDeviceRemovedReason();
    }

    // Snapshot the last GPU breadcrumb value (if available) so logs can
    // distinguish between CPU-side pass tags and the last marker the GPU
    // actually reached before the fault.
    uint32_t markerVal = (m_breadcrumbs.mappedValue != nullptr) ? *m_breadcrumbs.mappedValue : 0u;
    const char* markerName = "None";
    switch (static_cast<GpuMarker>(markerVal)) {
        case GpuMarker::BeginFrame:      markerName = "BeginFrame"; break;
        case GpuMarker::ShadowPass:      markerName = "ShadowPass"; break;
        case GpuMarker::Skybox:          markerName = "Skybox"; break;
        case GpuMarker::OpaqueGeometry:  markerName = "OpaqueGeometry"; break;
        case GpuMarker::TransparentGeom: markerName = "TransparentGeom"; break;
        case GpuMarker::MotionVectors:   markerName = "MotionVectors"; break;
        case GpuMarker::TAAResolve:      markerName = "TAAResolve"; break;
        case GpuMarker::SSR:             markerName = "SSR"; break;
        case GpuMarker::Particles:       markerName = "Particles"; break;
        case GpuMarker::SSAO:            markerName = "SSAO"; break;
        case GpuMarker::Bloom:           markerName = "Bloom"; break;
        case GpuMarker::PostProcess:     markerName = "PostProcess"; break;
        case GpuMarker::DebugLines:      markerName = "DebugLines"; break;
        case GpuMarker::EndFrame:        markerName = "EndFrame"; break;
        default:                         markerName = "None"; break;
    }

    auto rs = [](D3D12_RESOURCE_STATES s) {
        return static_cast<unsigned int>(s);
    };

    spdlog::error(
        "DX12 device removed or GPU fault in '{}' (hr=0x{:08X}, reason=0x{:08X}, frameCounter={}, "
        "swapIndex={}, lastPass='{}', lastGpuMarker='{}', at {}:{}). "
        "ResourceStates: depth=0x{:X}, shadowMap=0x{:X}, hdr=0x{:X}, "
        "rtShadowMask=0x{:X}, rtShadowMaskHistory=0x{:X}, gbufferNR=0x{:X}, "
        "ssao=0x{:X}, ssr=0x{:X}, velocity=0x{:X}, history=0x{:X}, "
        "taaIntermediate=0x{:X}, rtRefl=0x{:X}, rtReflHist=0x{:X}, "
        "rtGI=0x{:X}, rtGIHist=0x{:X}",
        ctx,
        static_cast<unsigned int>(hr),
        static_cast<unsigned int>(reason),
        static_cast<unsigned long long>(m_frameLifecycle.renderFrameCounter),
        static_cast<unsigned int>(m_frameRuntime.frameIndex),
        m_frameLifecycle.lastCompletedPass ? m_frameLifecycle.lastCompletedPass : "None",
        markerName,
        file ? file : "unknown",
        line,
        rs(m_depthResources.resourceState),
        rs(m_shadowResources.resourceState),
        rs(m_mainTargets.hdrState),
        rs(m_rtShadowTargets.maskState),
        rs(m_rtShadowTargets.historyState),
        rs(m_mainTargets.gbufferNormalRoughnessState),
        rs(m_ssaoResources.resourceState),
        rs(m_ssrResources.resourceState),
        rs(m_temporalScreenState.velocityState),
        rs(m_temporalScreenState.historyState),
        rs(m_temporalScreenState.taaIntermediateState),
        rs(m_rtReflectionTargets.colorState),
        rs(m_rtReflectionTargets.historyState),
        rs(m_rtGITargets.colorState),
        rs(m_rtGITargets.historyState));

    // Attempt to query DRED (Device Removed Extended Data) so we can log the
    // last command list / breadcrumb and any page-fault information the GPU
    // driver surfaced. This is best-effort and will silently skip if DRED is
    // not available on the current platform.
    if (m_services.device && m_services.device->GetDevice()) {
        Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData1> dred1;
        if (SUCCEEDED(m_services.device->GetDevice()->QueryInterface(IID_PPV_ARGS(&dred1))) && dred1) {
            D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 autoOut{};
            if (SUCCEEDED(dred1->GetAutoBreadcrumbsOutput1(&autoOut)) && autoOut.pHeadAutoBreadcrumbNode) {
                // Walk to the last node in the chain; this corresponds to the
                // most recent command list that executed before the fault.
                const D3D12_AUTO_BREADCRUMB_NODE1* node = autoOut.pHeadAutoBreadcrumbNode;
                std::array<const D3D12_AUTO_BREADCRUMB_NODE1*, 3> lastNodes{};
                while (node->pNext) {
                    lastNodes[0] = lastNodes[1];
                    lastNodes[1] = lastNodes[2];
                    lastNodes[2] = node;
                    node = node->pNext;
                }
                lastNodes[0] = lastNodes[1];
                lastNodes[1] = lastNodes[2];
                lastNodes[2] = node;

                const char* listName = "Unknown";
                if (node->pCommandListDebugNameA) {
                    listName = node->pCommandListDebugNameA;
                }

                UINT lastValue = UINT_MAX;
                if (node->pLastBreadcrumbValue) {
                    lastValue = *node->pLastBreadcrumbValue;
                }

                spdlog::error(
                    "DRED: last command list='{}', breadcrumbCount={}, lastCompletedBreadcrumbValue={}",
                    listName,
                    node->BreadcrumbCount,
                    lastValue);

                // Log the tail of the breadcrumb chain (up to last 3 nodes) to
                // show which command queues/lists were executing prior to the fault.
                for (int i = 2; i >= 0; --i) {
                    const auto* n = lastNodes[i];
                    if (!n) continue;
                    const char* clName = n->pCommandListDebugNameA ? n->pCommandListDebugNameA : "UnknownCL";
                    const char* cqName = n->pCommandQueueDebugNameA ? n->pCommandQueueDebugNameA : "UnknownCQ";
                    UINT completed = (n->pLastBreadcrumbValue) ? *n->pLastBreadcrumbValue : UINT_MAX;
                    spdlog::error(
                        "DRED: chain[-{}] queue='{}' list='{}' breadcrumbs={} lastCompleted={}",
                        (2 - i),
                        cqName,
                        clName,
                        n->BreadcrumbCount,
                        completed);
                }
            }

            D3D12_DRED_PAGE_FAULT_OUTPUT1 pageOut{};
            if (SUCCEEDED(dred1->GetPageFaultAllocationOutput1(&pageOut))) {
                // Log the GPU virtual address that faulted and whether DRED
                // associated it with an existing or recently-freed allocation.
                unsigned long long faultVA =
                    static_cast<unsigned long long>(pageOut.PageFaultVA);
                const char* allocType = "Unknown";
                if (pageOut.pHeadExistingAllocationNode) {
                    allocType = "ExistingAllocation";
                } else if (pageOut.pHeadRecentFreedAllocationNode) {
                    allocType = "RecentFreedAllocation";
                }

                spdlog::error(
                    "DRED: page fault at GPU VA=0x{:016X}, allocationType={}",
                    faultVA,
                    allocType);

                auto logAllocNode = [](const D3D12_DRED_ALLOCATION_NODE1* n, const char* label) {
                    if (!n) return;
                    const char* name = n->ObjectNameA ? n->ObjectNameA : "Unnamed";
                    spdlog::error("DRED: {} allocationType={} name='{}' object={}", label,
                                  static_cast<int>(n->AllocationType),
                                  name,
                                  static_cast<const void*>(n->pObject));
                };
                logAllocNode(pageOut.pHeadExistingAllocationNode, "ExistingAlloc");
                logAllocNode(pageOut.pHeadRecentFreedAllocationNode, "RecentFreedAlloc");
            }
        }
    }

    m_frameLifecycle.deviceRemoved = true;
}

void Renderer::LogDiagnostics() const {
    spdlog::info("---- Renderer Diagnostics ----");
    const char* lastPass = (m_frameLifecycle.lastCompletedPass && m_frameLifecycle.lastCompletedPass[0]) ? m_frameLifecycle.lastCompletedPass : "Unknown";
    spdlog::info("Last completed pass: {}", lastPass);
    spdlog::info("Frame index: {} (in-flight={})", m_frameRuntime.frameIndex, kFrameCount);
    if (m_services.window) {
        spdlog::info("Window: {}x{} vsync={}", m_services.window->GetWidth(), m_services.window->GetHeight(), m_services.window->IsVsyncEnabled());
    }
    spdlog::info("Render scale: {:.3f}", m_qualityRuntimeState.renderScale);
    spdlog::info("Backbuffer used-as-RT: {}", m_frameLifecycle.backBufferUsedAsRTThisFrame);

    spdlog::info("VB: enabled={} renderedThisFrame={} instances={} meshes={}",
                 m_visibilityBufferState.enabled,
                 m_visibilityBufferState.renderedThisFrame,
                 m_visibilityBufferState.instances.size(),
                 m_visibilityBufferState.meshDraws.size());
    spdlog::info("GPU culling: enabled={} totalInstances={} visibleInstances={}",
                 m_gpuCullingState.enabled,
                 GetGPUTotalInstances(),
                 GetGPUCulledCount());

    spdlog::info("Features: TAA={} FXAA={} SSR={} SSAO={} Bloom={:.2f} Fog={} Shadows={} IBL={}",
                 m_temporalAAState.enabled,
                 m_postProcessState.fxaaEnabled,
                 m_ssrResources.enabled,
                 m_ssaoResources.enabled,
                 m_bloomResources.intensity,
                 m_fogState.enabled,
                 m_shadowResources.enabled,
                 m_environmentState.enabled);
    spdlog::info("RT: supported={} enabled={} reflections={} GI={}",
                 m_rtRuntimeState.supported,
                 m_rtRuntimeState.enabled,
                 m_rtRuntimeState.reflectionsEnabled,
                 m_rtRuntimeState.giEnabled);

    spdlog::info("Resource states: depth=0x{:X} hdr=0x{:X} ssr=0x{:X}",
                 static_cast<uint32_t>(m_depthResources.resourceState),
                 static_cast<uint32_t>(m_mainTargets.hdrState),
                 static_cast<uint32_t>(m_ssrResources.resourceState));
    spdlog::info("Timings (ms): depthPrepass={:.2f} shadow={:.2f} main={:.2f}",
                 m_frameDiagnostics.timings.depthPrepassMs,
                 m_frameDiagnostics.timings.shadowPassMs,
                 m_frameDiagnostics.timings.mainPassMs);
    auto encodingName = [](TextureSourceEncoding encoding) {
        switch (encoding) {
        case TextureSourceEncoding::RGBA8:         return "rgba8";
        case TextureSourceEncoding::DDSCompressed: return "dds_compressed";
        case TextureSourceEncoding::Placeholder:   return "placeholder";
        default:                                   return "unknown";
        }
    };
    auto residencyName = [](TextureResidencyClass residency) {
        switch (residency) {
        case TextureResidencyClass::Environment: return "environment";
        case TextureResidencyClass::Generated:   return "generated";
        case TextureResidencyClass::Placeholder: return "placeholder";
        default:                                 return "generic";
        }
    };
    spdlog::info("Texture receipts: count={}", m_assetRuntime.textureUploads.receipts.size());
    spdlog::info(
        "Texture upload queue: submitted={} completed={} failed={} pending={} uploaded={:.2f}MB avg_ms={:.3f} last_ms={:.3f}",
        m_assetRuntime.textureUploads.queue.stats.submittedJobs,
        m_assetRuntime.textureUploads.queue.stats.completedJobs,
        m_assetRuntime.textureUploads.queue.stats.failedJobs,
        m_assetRuntime.textureUploads.queue.stats.pendingJobs,
        static_cast<double>(m_assetRuntime.textureUploads.queue.stats.uploadedResidentBytes) / (1024.0 * 1024.0),
        m_assetRuntime.textureUploads.queue.stats.AverageUploadMs(),
        m_assetRuntime.textureUploads.queue.stats.lastUploadMs);
    const size_t receiptCount = std::min<size_t>(m_assetRuntime.textureUploads.receipts.size(), 3);
    const size_t firstReceipt = m_assetRuntime.textureUploads.receipts.size() - receiptCount;
    for (size_t i = firstReceipt; i < m_assetRuntime.textureUploads.receipts.size(); ++i) {
        const auto& r = m_assetRuntime.textureUploads.receipts[i];
        spdlog::info(
            "Texture receipt: key='{}' src={} residency={} mips={}->{} firstMip={} resident={}x{} bytes={:.2f}/{:.2f}MB bindless={} trimmed={}",
            r.key,
            encodingName(r.sourceEncoding),
            residencyName(r.residencyClass),
            r.sourceMipLevels,
            r.residentMipLevels,
            r.firstResidentMip,
            r.residentWidth,
            r.residentHeight,
            static_cast<double>(r.residentGpuBytes) / (1024.0 * 1024.0),
            static_cast<double>(r.textureBudgetBytes) / (1024.0 * 1024.0),
            r.bindlessIndex,
            r.budgetTrimmed);
    }
    spdlog::info("------------------------------");
}

// Convenience macro so call sites automatically capture file/line.
#define CORTEX_REPORT_DEVICE_REMOVED(ctx, hr) \
    ReportDeviceRemoved((ctx), (hr), __FILE__, __LINE__)

Result<void> Renderer::CreateBreadcrumbBuffer() {
    if (!m_services.device || !m_services.device->GetDevice()) {
        return Result<void>::Err("Renderer not initialized for breadcrumb buffer creation");
    }
    if (m_breadcrumbs.buffer) {
        return Result<void>::Ok();
    }

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = sizeof(uint32_t);
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = m_services.device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_breadcrumbs.buffer));

    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create GPU breadcrumb buffer");
    }

    hr = m_breadcrumbs.buffer->Map(0, nullptr, reinterpret_cast<void**>(&m_breadcrumbs.mappedValue));
    if (FAILED(hr)) {
        m_breadcrumbs.buffer.Reset();
        m_breadcrumbs.mappedValue = nullptr;
        return Result<void>::Err("Failed to map GPU breadcrumb buffer");
    }

    if (m_breadcrumbs.mappedValue) {
        *m_breadcrumbs.mappedValue = static_cast<uint32_t>(GpuMarker::None);
    }

    spdlog::info("GPU breadcrumb buffer initialized for device-removed diagnostics");
    return Result<void>::Ok();
}

void Renderer::WriteBreadcrumb(GpuMarker marker) {
    if (!m_breadcrumbs.buffer || !m_commandResources.graphicsList) {
        return;
    }

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> list4;
    if (FAILED(m_commandResources.graphicsList.As(&list4)) || !list4) {
        return;
    }

    D3D12_WRITEBUFFERIMMEDIATE_PARAMETER param{};
    param.Dest = m_breadcrumbs.buffer->GetGPUVirtualAddress();
    param.Value = static_cast<uint32_t>(marker);

    list4->WriteBufferImmediate(1, &param, nullptr);
}
} // namespace Cortex::Graphics
