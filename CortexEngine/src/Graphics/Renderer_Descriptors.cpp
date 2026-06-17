#include "Renderer.h"

#include "Passes/DescriptorTable.h"
#include "Passes/PostProcessPass.h"
#include "Passes/TAAPass.h"

#include <spdlog/spdlog.h>

#include <span>
#include <string>

namespace Cortex::Graphics {

Result<void> Renderer::InitializeTAAResolveDescriptorTable() {
    m_temporal.ScreenState().taaResolveSrvTableValid = false;
    for (auto& table : m_temporal.ScreenState().taaResolveSrvTables) {
        for (auto& handle : table) {
            handle = {};
        }
    }

    if (!m_services.device || !m_services.descriptorManager) {
        return Result<void>::Err("Renderer not initialized");
    }

    ID3D12Device* device = m_services.device->GetDevice();
    if (!device) {
        return Result<void>::Err("D3D12 device not available");
    }

    for (size_t frame = 0; frame < kFrameCount; ++frame) {
        auto tableResult = DescriptorTable::AllocateAndWriteNullSRVTable(
            device,
            m_services.descriptorManager.get(),
            m_temporal.ScreenState().taaResolveSrvTables[frame],
            "TAA resolve",
            DXGI_FORMAT_R16G16B16A16_FLOAT);
        if (tableResult.IsErr()) {
            return tableResult;
        }
    }

    m_temporal.ScreenState().taaResolveSrvTableValid = true;
    for (size_t frame = 0; frame < kFrameCount && m_temporal.ScreenState().taaResolveSrvTableValid; ++frame) {
        if (!DescriptorTable::IsContiguous(m_temporal.ScreenState().taaResolveSrvTables[frame])) {
            spdlog::warn("TAA resolve SRV table is not contiguous for frame {}; persistent table disabled", frame);
            m_temporal.ScreenState().taaResolveSrvTableValid = false;
            break;
        }
    }
    return Result<void>::Ok();
}

void Renderer::UpdateTAAResolveDescriptorTable() {
    if (!m_temporal.ScreenState().taaResolveSrvTableValid || !m_services.device) {
        return;
    }

    ID3D12Device* device = m_services.device->GetDevice();
    if (!device) {
        return;
    }

    ID3D12Resource* normalRes = m_mainTargets.normalRoughness.resources.texture.Get();
    if (m_vb.State().renderedThisFrame && m_services.visibilityBuffer && m_services.visibilityBuffer->GetNormalRoughnessBuffer()) {
        normalRes = m_services.visibilityBuffer->GetNormalRoughnessBuffer();
    }

    auto& table = m_temporal.ScreenState().taaResolveSrvTables[m_frameRuntime.frameIndex % kFrameCount];
    TAAPass::DescriptorUpdateContext context{};
    context.device = device;
    context.srvTable = std::span<DescriptorHandle>(table.data(), table.size());
    context.hdr = m_mainTargets.hdr.resources.color.Get();
    context.bloomIntensity = m_bloom.State().controls.intensity;
    context.bloomOverride = m_bloom.State().resources.postProcessOverride;
    context.bloomFallback = (m_bloom.State().resources.activeLevels > 1)
        ? m_bloom.State().resources.texA[1].Get()
        : m_bloom.State().resources.texA[0].Get();
    context.ssao = m_ssao.State().resources.texture.Get();
    context.history = m_temporal.ScreenState().historyColor.Get();
    context.depth = m_depthResources.resources.buffer.Get();
    context.normalRoughness = normalRes;
    context.ssr = m_ssr.State().resources.color.Get();
    context.velocity = m_temporal.ScreenState().velocityBuffer.Get();
    context.temporalMask = m_temporalMaskState.texture.Get();
    (void)TAAPass::UpdateResolveDescriptorTable(context);
}

Result<void> Renderer::InitializePostProcessDescriptorTable() {
    m_temporal.ScreenState().postProcessSrvTableValid = false;
    for (auto& table : m_temporal.ScreenState().postProcessSrvTables) {
        for (auto& handle : table) {
            handle = {};
        }
    }
    m_ssr.State().descriptors.srvTableValid = false;
    for (auto& table : m_ssr.State().descriptors.srvTables) {
        for (auto& handle : table) {
            handle = {};
        }
    }
    m_temporal.ScreenState().motionVectorSrvTableValid = false;
    for (auto& table : m_temporal.ScreenState().motionVectorSrvTables) {
        for (auto& handle : table) {
            handle = {};
        }
    }
    m_temporalMaskState.descriptorTablesValid = false;
    for (auto& table : m_temporalMaskState.srvTables) {
        for (auto& handle : table) {
            handle = {};
        }
    }
    for (auto& table : m_temporalMaskState.uavTables) {
        for (auto& handle : table) {
            handle = {};
        }
    }
    m_temporalMaskState.statsDescriptorTablesValid = false;
    for (auto& table : m_temporalMaskState.statsSrvTables) {
        for (auto& handle : table) {
            handle = {};
        }
    }
    for (auto& table : m_temporalMaskState.statsUavTables) {
        for (auto& handle : table) {
            handle = {};
        }
    }
    m_rt.ReflectionSignalState().descriptors.ResetHandles();
    m_localReflectionRadianceState.descriptors.Reset();
    m_ssao.State().descriptors.descriptorTablesValid = false;
    for (auto& table : m_ssao.State().descriptors.srvTables) {
        for (auto& handle : table) {
            handle = {};
        }
    }
    for (auto& table : m_ssao.State().descriptors.uavTables) {
        for (auto& handle : table) {
            handle = {};
        }
    }
    m_rt.DenoiseState().descriptorTablesValid = false;
    for (auto& table : m_rt.DenoiseState().srvTables) {
        for (auto& handle : table) {
            handle = {};
        }
    }
    for (auto& table : m_rt.DenoiseState().uavTables) {
        for (auto& handle : table) {
            handle = {};
        }
    }
    m_bloom.State().descriptors.srvTableValid = false;
    for (auto& table : m_bloom.State().descriptors.srvTables) {
        for (auto& handle : table) {
            handle = {};
        }
    }
    for (auto& handle : m_rt.ReflectionTargets().dispatchClearUAVs) {
        handle = {};
    }
    for (auto& handle : m_rt.ReflectionTargets().postClearUAVs) {
        handle = {};
    }

    if (!m_services.device || !m_services.descriptorManager) {
        return Result<void>::Err("Renderer not initialized");
    }

    ID3D12Device* device = m_services.device->GetDevice();
    if (!device) {
        return Result<void>::Err("D3D12 device not available");
    }

    auto allocateTableSet = [&](auto& tables, const char* label) -> Result<void> {
        for (size_t frame = 0; frame < kFrameCount; ++frame) {
            auto tableResult = DescriptorTable::AllocateAndWriteNullSRVTable(
                device,
                m_services.descriptorManager.get(),
                tables[frame],
                label,
                DXGI_FORMAT_R16G16B16A16_FLOAT);
            if (tableResult.IsErr()) {
                return tableResult;
            }
        }
        return Result<void>::Ok();
    };

    auto postTableResult = allocateTableSet(m_temporal.ScreenState().postProcessSrvTables, "post-process");
    if (postTableResult.IsErr()) {
        return postTableResult;
    }
    auto ssrTableResult = allocateTableSet(m_ssr.State().descriptors.srvTables, "SSR");
    if (ssrTableResult.IsErr()) {
        return ssrTableResult;
    }
    auto motionTableResult = allocateTableSet(m_temporal.ScreenState().motionVectorSrvTables, "motion-vector");
    if (motionTableResult.IsErr()) {
        return motionTableResult;
    }
    auto ssaoSrvTableResult = allocateTableSet(m_ssao.State().descriptors.srvTables, "SSAO SRV");
    if (ssaoSrvTableResult.IsErr()) {
        return ssaoSrvTableResult;
    }
    auto ssaoUavTableResult = allocateTableSet(m_ssao.State().descriptors.uavTables, "SSAO UAV");
    if (ssaoUavTableResult.IsErr()) {
        return ssaoUavTableResult;
    }
    auto rtDenoiseSrvTableResult = allocateTableSet(m_rt.DenoiseState().srvTables, "RT denoise SRV");
    if (rtDenoiseSrvTableResult.IsErr()) {
        return rtDenoiseSrvTableResult;
    }
    auto rtDenoiseUavTableResult = allocateTableSet(m_rt.DenoiseState().uavTables, "RT denoise UAV");
    if (rtDenoiseUavTableResult.IsErr()) {
        return rtDenoiseUavTableResult;
    }
    auto localRadianceSrvResult = allocateTableSet(
        m_localReflectionRadianceState.descriptors.srvTables,
        "local reflection radiance SRV");
    if (localRadianceSrvResult.IsErr()) {
        return localRadianceSrvResult;
    }
    auto localRadianceUavResult = allocateTableSet(
        m_localReflectionRadianceState.descriptors.uavTables,
        "local reflection radiance UAV");
    if (localRadianceUavResult.IsErr()) {
        return localRadianceUavResult;
    }
    auto temporalMaskSrvTableResult = allocateTableSet(m_temporalMaskState.srvTables, "temporal mask SRV");
    if (temporalMaskSrvTableResult.IsErr()) {
        return temporalMaskSrvTableResult;
    }
    auto temporalMaskUavTableResult = allocateTableSet(m_temporalMaskState.uavTables, "temporal mask UAV");
    if (temporalMaskUavTableResult.IsErr()) {
        return temporalMaskUavTableResult;
    }
    auto temporalMaskStatsSrvTableResult = allocateTableSet(m_temporalMaskState.statsSrvTables, "temporal mask stats SRV");
    if (temporalMaskStatsSrvTableResult.IsErr()) {
        return temporalMaskStatsSrvTableResult;
    }
    auto temporalMaskStatsUavTableResult = allocateTableSet(m_temporalMaskState.statsUavTables, "temporal mask stats UAV");
    if (temporalMaskStatsUavTableResult.IsErr()) {
        return temporalMaskStatsUavTableResult;
    }
    auto rtReflectionSignalStatsSrvResult = allocateTableSet(
        m_rt.ReflectionSignalState().descriptors.srvTables,
        "RT reflection signal stats SRV");
    if (rtReflectionSignalStatsSrvResult.IsErr()) {
        return rtReflectionSignalStatsSrvResult;
    }
    auto rtReflectionSignalStatsUavResult = allocateTableSet(
        m_rt.ReflectionSignalState().descriptors.uavTables,
        "RT reflection signal stats UAV");
    if (rtReflectionSignalStatsUavResult.IsErr()) {
        return rtReflectionSignalStatsUavResult;
    }
    auto bloomTableResult = allocateTableSet(m_bloom.State().descriptors.srvTables, "bloom");
    if (bloomTableResult.IsErr()) {
        return bloomTableResult;
    }

    auto allocateHandleSet = [&](auto& handles, const char* label) -> Result<void> {
        return DescriptorTable::AllocateHandleSet(
            m_services.descriptorManager.get(),
            handles,
            label);
    };

    auto rtDispatchClearResult = allocateHandleSet(m_rt.ReflectionTargets().dispatchClearUAVs, "RT reflection dispatch clear UAV");
    if (rtDispatchClearResult.IsErr()) {
        return rtDispatchClearResult;
    }
    auto rtPostClearResult = allocateHandleSet(m_rt.ReflectionTargets().postClearUAVs, "RT reflection post clear UAV");
    if (rtPostClearResult.IsErr()) {
        return rtPostClearResult;
    }

    auto validateTableSet = [&](auto& tables, bool& valid, const char* label) {
        valid = true;
        for (size_t frame = 0; frame < kFrameCount && valid; ++frame) {
            if (!DescriptorTable::IsContiguous(tables[frame])) {
                spdlog::warn("{} SRV table is not contiguous for frame {}; persistent table disabled",
                             label, frame);
                valid = false;
                break;
            }
        }
    };

    validateTableSet(m_temporal.ScreenState().postProcessSrvTables, m_temporal.ScreenState().postProcessSrvTableValid, "Post-process");
    validateTableSet(m_ssr.State().descriptors.srvTables, m_ssr.State().descriptors.srvTableValid, "SSR");
    validateTableSet(m_temporal.ScreenState().motionVectorSrvTables, m_temporal.ScreenState().motionVectorSrvTableValid, "Motion-vector");
    bool ssaoSrvValid = false;
    bool ssaoUavValid = false;
    bool rtDenoiseSrvValid = false;
    bool rtDenoiseUavValid = false;
    bool localRadianceSrvValid = false;
    bool localRadianceUavValid = false;
    bool temporalMaskSrvValid = false;
    bool temporalMaskUavValid = false;
    bool temporalMaskStatsSrvValid = false;
    bool temporalMaskStatsUavValid = false;
    bool rtReflectionSignalStatsSrvValid = false;
    bool rtReflectionSignalStatsUavValid = false;
    validateTableSet(m_ssao.State().descriptors.srvTables, ssaoSrvValid, "SSAO SRV");
    validateTableSet(m_ssao.State().descriptors.uavTables, ssaoUavValid, "SSAO UAV");
    m_ssao.State().descriptors.descriptorTablesValid = ssaoSrvValid && ssaoUavValid;
    validateTableSet(m_rt.DenoiseState().srvTables, rtDenoiseSrvValid, "RT denoise SRV");
    validateTableSet(m_rt.DenoiseState().uavTables, rtDenoiseUavValid, "RT denoise UAV");
    m_rt.DenoiseState().descriptorTablesValid = rtDenoiseSrvValid && rtDenoiseUavValid;
    validateTableSet(m_localReflectionRadianceState.descriptors.srvTables,
                     localRadianceSrvValid,
                     "Local reflection radiance SRV");
    validateTableSet(m_localReflectionRadianceState.descriptors.uavTables,
                     localRadianceUavValid,
                     "Local reflection radiance UAV");
    m_localReflectionRadianceState.descriptors.valid =
        localRadianceSrvValid && localRadianceUavValid;
    validateTableSet(m_temporalMaskState.srvTables, temporalMaskSrvValid, "Temporal mask SRV");
    validateTableSet(m_temporalMaskState.uavTables, temporalMaskUavValid, "Temporal mask UAV");
    m_temporalMaskState.descriptorTablesValid = temporalMaskSrvValid && temporalMaskUavValid;
    validateTableSet(m_temporalMaskState.statsSrvTables, temporalMaskStatsSrvValid, "Temporal mask stats SRV");
    validateTableSet(m_temporalMaskState.statsUavTables, temporalMaskStatsUavValid, "Temporal mask stats UAV");
    m_temporalMaskState.statsDescriptorTablesValid = temporalMaskStatsSrvValid && temporalMaskStatsUavValid;
    validateTableSet(m_rt.ReflectionSignalState().descriptors.srvTables,
                     rtReflectionSignalStatsSrvValid,
                     "RT reflection signal stats SRV");
    validateTableSet(m_rt.ReflectionSignalState().descriptors.uavTables,
                     rtReflectionSignalStatsUavValid,
                     "RT reflection signal stats UAV");
    m_rt.ReflectionSignalState().descriptors.valid =
        rtReflectionSignalStatsSrvValid && rtReflectionSignalStatsUavValid;
    validateTableSet(m_bloom.State().descriptors.srvTables, m_bloom.State().descriptors.srvTableValid, "Bloom");
    return Result<void>::Ok();
}

void Renderer::UpdatePostProcessDescriptorTable() {
    if (!m_temporal.ScreenState().postProcessSrvTableValid || !m_services.device) {
        return;
    }

    ID3D12Device* device = m_services.device->GetDevice();
    if (!device) {
        return;
    }

    ID3D12Resource* normalRes = m_mainTargets.normalRoughness.resources.texture.Get();
    if (m_vb.State().renderedThisFrame && m_services.visibilityBuffer && m_services.visibilityBuffer->GetNormalRoughnessBuffer()) {
        normalRes = m_services.visibilityBuffer->GetNormalRoughnessBuffer();
    }

    ID3D12Resource* emissiveMetallicRes = nullptr;
    if (m_vb.State().renderedThisFrame && m_services.visibilityBuffer && m_services.visibilityBuffer->GetEmissiveMetallicBuffer()) {
        emissiveMetallicRes = m_services.visibilityBuffer->GetEmissiveMetallicBuffer();
    }

    ID3D12Resource* materialExt1Res = nullptr;
    ID3D12Resource* materialExt2Res = nullptr;
    if (m_vb.State().renderedThisFrame && m_services.visibilityBuffer) {
        materialExt1Res = m_services.visibilityBuffer->GetMaterialExt1Buffer();
        materialExt2Res = m_services.visibilityBuffer->GetMaterialExt2Buffer();
    }

    auto& table = m_temporal.ScreenState().postProcessSrvTables[m_frameRuntime.frameIndex % kFrameCount];
    PostProcessPass::DescriptorUpdateContext context{};
    context.device = device;
    context.srvTable = std::span<DescriptorHandle>(table.data(), table.size());
    context.hdr = m_mainTargets.hdr.resources.color.Get();
    context.bloomIntensity = m_bloom.State().controls.intensity;
    context.bloomOverride = m_bloom.State().resources.postProcessOverride;
    context.bloomFallback = (m_bloom.State().resources.activeLevels > 1)
        ? m_bloom.State().resources.texA[1].Get()
        : m_bloom.State().resources.texA[0].Get();
    context.ssao = m_ssao.State().resources.texture.Get();
    context.history = m_temporal.ScreenState().historyColor.Get();
    context.depth = m_depthResources.resources.buffer.Get();
    context.normalRoughness = normalRes;
    context.hzb = m_hzb.State().resources.texture.Get();
    context.hzbMipCount = m_hzb.State().resources.mipCount;
    context.wantsHzbDebug = (m_debugViewState.mode == 32u);
    context.ssr = m_ssr.State().resources.color.Get();
    context.velocity = m_temporal.ScreenState().velocityBuffer.Get();
    context.rtReflection = m_rt.ReflectionTargets().color.Get();
    context.rtReflectionHistory = m_rt.ReflectionTargets().history.Get();
    context.emissiveMetallic = emissiveMetallicRes;
    context.materialExt1 = materialExt1Res;
    context.materialExt2 = materialExt2Res;
    context.localReflectionRadiance = nullptr;
    (void)PostProcessPass::UpdateDescriptorTable(context);
}

} // namespace Cortex::Graphics
