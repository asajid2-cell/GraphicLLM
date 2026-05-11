#include "Renderer.h"

#include "Passes/DescriptorTable.h"

#include <spdlog/spdlog.h>

#include <string>

namespace Cortex::Graphics {

Result<void> Renderer::InitializeTAAResolveDescriptorTable() {
    m_temporalScreenState.taaResolveSrvTableValid = false;
    for (auto& table : m_temporalScreenState.taaResolveSrvTables) {
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
        for (size_t i = 0; i < m_temporalScreenState.taaResolveSrvTables[frame].size(); ++i) {
            auto handleResult = m_services.descriptorManager->AllocateCBV_SRV_UAV();
            if (handleResult.IsErr()) {
                return Result<void>::Err("Failed to allocate TAA resolve descriptor: " + handleResult.Error());
            }
            m_temporalScreenState.taaResolveSrvTables[frame][i] = handleResult.Value();
            DescriptorTable::WriteTexture2DSRV(
                device,
                m_temporalScreenState.taaResolveSrvTables[frame][i],
                nullptr,
                DXGI_FORMAT_R16G16B16A16_FLOAT);
        }
    }

    m_temporalScreenState.taaResolveSrvTableValid = true;
    for (size_t frame = 0; frame < kFrameCount && m_temporalScreenState.taaResolveSrvTableValid; ++frame) {
        if (!DescriptorTable::IsContiguous(m_temporalScreenState.taaResolveSrvTables[frame])) {
            spdlog::warn("TAA resolve SRV table is not contiguous for frame {}; persistent table disabled", frame);
            m_temporalScreenState.taaResolveSrvTableValid = false;
            break;
        }
    }
    return Result<void>::Ok();
}

void Renderer::UpdateTAAResolveDescriptorTable() {
    if (!m_temporalScreenState.taaResolveSrvTableValid || !m_services.device) {
        return;
    }

    ID3D12Device* device = m_services.device->GetDevice();
    if (!device) {
        return;
    }

    auto& table = m_temporalScreenState.taaResolveSrvTables[m_frameRuntime.frameIndex % kFrameCount];
    auto writeOrNull = [&](size_t slot, ID3D12Resource* resource, DXGI_FORMAT fmt) {
        if (slot >= table.size() || !table[slot].IsValid()) {
            return;
        }

        DescriptorTable::WriteTexture2DSRV(device, table[slot], nullptr, fmt);
        if (resource) DescriptorTable::WriteTexture2DSRV(device, table[slot], resource, fmt);
    };

    // Must match PostProcess.hlsl TAAResolvePS bindings.
    writeOrNull(0, m_mainTargets.hdrColor.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);

    ID3D12Resource* bloomRes = nullptr;
    if (m_bloomResources.controls.intensity > 0.0f) {
        bloomRes = m_bloomResources.resources.postProcessOverride
            ? m_bloomResources.resources.postProcessOverride
            : ((m_bloomResources.resources.activeLevels > 1) ? m_bloomResources.resources.texA[1].Get() : m_bloomResources.resources.texA[0].Get());
    }
    writeOrNull(1, bloomRes, DXGI_FORMAT_R11G11B10_FLOAT);

    writeOrNull(2, m_ssaoResources.resources.texture.Get(), DXGI_FORMAT_R8_UNORM);
    writeOrNull(3, m_temporalScreenState.historyColor.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
    writeOrNull(4, m_depthResources.buffer.Get(), DXGI_FORMAT_R32_FLOAT);

    ID3D12Resource* normalRes = m_mainTargets.gbufferNormalRoughness.Get();
    if (m_visibilityBufferState.renderedThisFrame && m_services.visibilityBuffer && m_services.visibilityBuffer->GetNormalRoughnessBuffer()) {
        normalRes = m_services.visibilityBuffer->GetNormalRoughnessBuffer();
    }
    writeOrNull(5, normalRes, DXGI_FORMAT_R16G16B16A16_FLOAT);

    writeOrNull(6, m_ssrResources.resources.color.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
    writeOrNull(7, m_temporalScreenState.velocityBuffer.Get(), DXGI_FORMAT_R16G16_FLOAT);
    // TAA reuses the t12 material-extension slot as the shared temporal
    // rejection mask; the full post-process table still binds material ext2.
    writeOrNull(12, m_temporalMaskState.texture.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
}

Result<void> Renderer::InitializePostProcessDescriptorTable() {
    m_temporalScreenState.postProcessSrvTableValid = false;
    for (auto& table : m_temporalScreenState.postProcessSrvTables) {
        for (auto& handle : table) {
            handle = {};
        }
    }
    m_ssrResources.descriptors.srvTableValid = false;
    for (auto& table : m_ssrResources.descriptors.srvTables) {
        for (auto& handle : table) {
            handle = {};
        }
    }
    m_temporalScreenState.motionVectorSrvTableValid = false;
    for (auto& table : m_temporalScreenState.motionVectorSrvTables) {
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
    m_rtReflectionSignalState.descriptors.ResetHandles();
    m_ssaoResources.descriptors.descriptorTablesValid = false;
    for (auto& table : m_ssaoResources.descriptors.srvTables) {
        for (auto& handle : table) {
            handle = {};
        }
    }
    for (auto& table : m_ssaoResources.descriptors.uavTables) {
        for (auto& handle : table) {
            handle = {};
        }
    }
    m_rtDenoiseState.descriptorTablesValid = false;
    for (auto& table : m_rtDenoiseState.srvTables) {
        for (auto& handle : table) {
            handle = {};
        }
    }
    for (auto& table : m_rtDenoiseState.uavTables) {
        for (auto& handle : table) {
            handle = {};
        }
    }
    m_bloomResources.descriptors.srvTableValid = false;
    for (auto& table : m_bloomResources.descriptors.srvTables) {
        for (auto& handle : table) {
            handle = {};
        }
    }
    for (auto& handle : m_rtReflectionTargets.dispatchClearUAVs) {
        handle = {};
    }
    for (auto& handle : m_rtReflectionTargets.postClearUAVs) {
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
            for (size_t i = 0; i < tables[frame].size(); ++i) {
                auto handleResult = m_services.descriptorManager->AllocateCBV_SRV_UAV();
                if (handleResult.IsErr()) {
                    return Result<void>::Err(std::string("Failed to allocate ") + label +
                                             " descriptor: " + handleResult.Error());
                }
                tables[frame][i] = handleResult.Value();
                DescriptorTable::WriteTexture2DSRV(
                    device,
                    tables[frame][i],
                    nullptr,
                    DXGI_FORMAT_R16G16B16A16_FLOAT);
            }
        }
        return Result<void>::Ok();
    };

    auto postTableResult = allocateTableSet(m_temporalScreenState.postProcessSrvTables, "post-process");
    if (postTableResult.IsErr()) {
        return postTableResult;
    }
    auto ssrTableResult = allocateTableSet(m_ssrResources.descriptors.srvTables, "SSR");
    if (ssrTableResult.IsErr()) {
        return ssrTableResult;
    }
    auto motionTableResult = allocateTableSet(m_temporalScreenState.motionVectorSrvTables, "motion-vector");
    if (motionTableResult.IsErr()) {
        return motionTableResult;
    }
    auto ssaoSrvTableResult = allocateTableSet(m_ssaoResources.descriptors.srvTables, "SSAO SRV");
    if (ssaoSrvTableResult.IsErr()) {
        return ssaoSrvTableResult;
    }
    auto ssaoUavTableResult = allocateTableSet(m_ssaoResources.descriptors.uavTables, "SSAO UAV");
    if (ssaoUavTableResult.IsErr()) {
        return ssaoUavTableResult;
    }
    auto rtDenoiseSrvTableResult = allocateTableSet(m_rtDenoiseState.srvTables, "RT denoise SRV");
    if (rtDenoiseSrvTableResult.IsErr()) {
        return rtDenoiseSrvTableResult;
    }
    auto rtDenoiseUavTableResult = allocateTableSet(m_rtDenoiseState.uavTables, "RT denoise UAV");
    if (rtDenoiseUavTableResult.IsErr()) {
        return rtDenoiseUavTableResult;
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
        m_rtReflectionSignalState.descriptors.srvTables,
        "RT reflection signal stats SRV");
    if (rtReflectionSignalStatsSrvResult.IsErr()) {
        return rtReflectionSignalStatsSrvResult;
    }
    auto rtReflectionSignalStatsUavResult = allocateTableSet(
        m_rtReflectionSignalState.descriptors.uavTables,
        "RT reflection signal stats UAV");
    if (rtReflectionSignalStatsUavResult.IsErr()) {
        return rtReflectionSignalStatsUavResult;
    }
    auto bloomTableResult = allocateTableSet(m_bloomResources.descriptors.srvTables, "bloom");
    if (bloomTableResult.IsErr()) {
        return bloomTableResult;
    }

    auto allocateHandleSet = [&](auto& handles, const char* label) -> Result<void> {
        for (size_t frame = 0; frame < kFrameCount; ++frame) {
            auto handleResult = m_services.descriptorManager->AllocateCBV_SRV_UAV();
            if (handleResult.IsErr()) {
                return Result<void>::Err(std::string("Failed to allocate ") + label +
                                         " descriptor: " + handleResult.Error());
            }
            handles[frame] = handleResult.Value();
        }
        return Result<void>::Ok();
    };

    auto rtDispatchClearResult = allocateHandleSet(m_rtReflectionTargets.dispatchClearUAVs, "RT reflection dispatch clear UAV");
    if (rtDispatchClearResult.IsErr()) {
        return rtDispatchClearResult;
    }
    auto rtPostClearResult = allocateHandleSet(m_rtReflectionTargets.postClearUAVs, "RT reflection post clear UAV");
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

    validateTableSet(m_temporalScreenState.postProcessSrvTables, m_temporalScreenState.postProcessSrvTableValid, "Post-process");
    validateTableSet(m_ssrResources.descriptors.srvTables, m_ssrResources.descriptors.srvTableValid, "SSR");
    validateTableSet(m_temporalScreenState.motionVectorSrvTables, m_temporalScreenState.motionVectorSrvTableValid, "Motion-vector");
    bool ssaoSrvValid = false;
    bool ssaoUavValid = false;
    bool rtDenoiseSrvValid = false;
    bool rtDenoiseUavValid = false;
    bool temporalMaskSrvValid = false;
    bool temporalMaskUavValid = false;
    bool temporalMaskStatsSrvValid = false;
    bool temporalMaskStatsUavValid = false;
    bool rtReflectionSignalStatsSrvValid = false;
    bool rtReflectionSignalStatsUavValid = false;
    validateTableSet(m_ssaoResources.descriptors.srvTables, ssaoSrvValid, "SSAO SRV");
    validateTableSet(m_ssaoResources.descriptors.uavTables, ssaoUavValid, "SSAO UAV");
    m_ssaoResources.descriptors.descriptorTablesValid = ssaoSrvValid && ssaoUavValid;
    validateTableSet(m_rtDenoiseState.srvTables, rtDenoiseSrvValid, "RT denoise SRV");
    validateTableSet(m_rtDenoiseState.uavTables, rtDenoiseUavValid, "RT denoise UAV");
    m_rtDenoiseState.descriptorTablesValid = rtDenoiseSrvValid && rtDenoiseUavValid;
    validateTableSet(m_temporalMaskState.srvTables, temporalMaskSrvValid, "Temporal mask SRV");
    validateTableSet(m_temporalMaskState.uavTables, temporalMaskUavValid, "Temporal mask UAV");
    m_temporalMaskState.descriptorTablesValid = temporalMaskSrvValid && temporalMaskUavValid;
    validateTableSet(m_temporalMaskState.statsSrvTables, temporalMaskStatsSrvValid, "Temporal mask stats SRV");
    validateTableSet(m_temporalMaskState.statsUavTables, temporalMaskStatsUavValid, "Temporal mask stats UAV");
    m_temporalMaskState.statsDescriptorTablesValid = temporalMaskStatsSrvValid && temporalMaskStatsUavValid;
    validateTableSet(m_rtReflectionSignalState.descriptors.srvTables,
                     rtReflectionSignalStatsSrvValid,
                     "RT reflection signal stats SRV");
    validateTableSet(m_rtReflectionSignalState.descriptors.uavTables,
                     rtReflectionSignalStatsUavValid,
                     "RT reflection signal stats UAV");
    m_rtReflectionSignalState.descriptors.valid =
        rtReflectionSignalStatsSrvValid && rtReflectionSignalStatsUavValid;
    validateTableSet(m_bloomResources.descriptors.srvTables, m_bloomResources.descriptors.srvTableValid, "Bloom");
    return Result<void>::Ok();
}

void Renderer::UpdatePostProcessDescriptorTable() {
    if (!m_temporalScreenState.postProcessSrvTableValid || !m_services.device) {
        return;
    }

    ID3D12Device* device = m_services.device->GetDevice();
    if (!device) {
        return;
    }

    auto& table = m_temporalScreenState.postProcessSrvTables[m_frameRuntime.frameIndex % kFrameCount];
    auto writeOrNull = [&](size_t slot,
                           ID3D12Resource* resource,
                           DXGI_FORMAT fmt,
                           uint32_t mipLevels = 1) {
        if (slot >= table.size() || !table[slot].IsValid()) {
            return;
        }

        DescriptorTable::WriteTexture2DSRV(device, table[slot], nullptr, fmt, mipLevels);
        if (resource) DescriptorTable::WriteTexture2DSRV(device, table[slot], resource, fmt, mipLevels);
    };

    // Must match PostProcess.hlsl bindings.
    writeOrNull(0, m_mainTargets.hdrColor.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);

    ID3D12Resource* bloomRes = nullptr;
    if (m_bloomResources.controls.intensity > 0.0f) {
        bloomRes = (m_bloomResources.resources.activeLevels > 1) ? m_bloomResources.resources.texA[1].Get() : m_bloomResources.resources.texA[0].Get();
    }
    writeOrNull(1, bloomRes, DXGI_FORMAT_R11G11B10_FLOAT);

    writeOrNull(2, m_ssaoResources.resources.texture.Get(), DXGI_FORMAT_R8_UNORM);
    writeOrNull(3, m_temporalScreenState.historyColor.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
    writeOrNull(4, m_depthResources.buffer.Get(), DXGI_FORMAT_R32_FLOAT);

    ID3D12Resource* normalRes = m_mainTargets.gbufferNormalRoughness.Get();
    if (m_visibilityBufferState.renderedThisFrame && m_services.visibilityBuffer && m_services.visibilityBuffer->GetNormalRoughnessBuffer()) {
        normalRes = m_services.visibilityBuffer->GetNormalRoughnessBuffer();
    }
    writeOrNull(5, normalRes, DXGI_FORMAT_R16G16B16A16_FLOAT);

    if (m_debugViewState.mode == 32u && m_hzbResources.texture && m_hzbResources.mipCount > 0) {
        writeOrNull(6, m_hzbResources.texture.Get(), DXGI_FORMAT_R32_FLOAT, m_hzbResources.mipCount);
    } else {
        writeOrNull(6, m_ssrResources.resources.color.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
    }

    writeOrNull(7, m_temporalScreenState.velocityBuffer.Get(), DXGI_FORMAT_R16G16_FLOAT);
    writeOrNull(8, m_rtReflectionTargets.color.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
    writeOrNull(9, m_rtReflectionTargets.history.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);

    ID3D12Resource* emissiveMetallicRes = nullptr;
    if (m_visibilityBufferState.renderedThisFrame && m_services.visibilityBuffer && m_services.visibilityBuffer->GetEmissiveMetallicBuffer()) {
        emissiveMetallicRes = m_services.visibilityBuffer->GetEmissiveMetallicBuffer();
    }
    writeOrNull(10, emissiveMetallicRes, DXGI_FORMAT_R16G16B16A16_FLOAT);

    ID3D12Resource* materialExt1Res = nullptr;
    ID3D12Resource* materialExt2Res = nullptr;
    if (m_visibilityBufferState.renderedThisFrame && m_services.visibilityBuffer) {
        materialExt1Res = m_services.visibilityBuffer->GetMaterialExt1Buffer();
        materialExt2Res = m_services.visibilityBuffer->GetMaterialExt2Buffer();
    }
    writeOrNull(11, materialExt1Res, DXGI_FORMAT_R16G16B16A16_FLOAT);
    writeOrNull(12, materialExt2Res, DXGI_FORMAT_R8G8B8A8_UNORM);
}

} // namespace Cortex::Graphics
