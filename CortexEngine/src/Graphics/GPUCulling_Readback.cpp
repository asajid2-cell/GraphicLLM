#include "GPUCulling.h"
#include <algorithm>
#include <cstring>
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

namespace {

static void LogIndirectCommand(const char* label, uint32_t index, const IndirectCommand& cmd) {
    spdlog::info(
        "{}[{}]: objectCBV=0x{:016X} materialCBV=0x{:016X} "
        "VBV(addr=0x{:016X} size={} stride={}) "
        "IBV(addr=0x{:016X} size={} fmt={}) "
        "draw(indexCount={} instanceCount={} startIndex={} baseVertex={} startInstance={})",
        label,
        index,
        static_cast<uint64_t>(cmd.objectCBV),
        static_cast<uint64_t>(cmd.materialCBV),
        static_cast<uint64_t>(cmd.vertexBuffer.BufferLocation),
        cmd.vertexBuffer.SizeInBytes,
        cmd.vertexBuffer.StrideInBytes,
        static_cast<uint64_t>(cmd.indexBuffer.BufferLocation),
        cmd.indexBuffer.SizeInBytes,
        static_cast<unsigned int>(cmd.indexBuffer.Format),
        cmd.draw.indexCountPerInstance,
        cmd.draw.instanceCount,
        cmd.draw.startIndexLocation,
        cmd.draw.baseVertexLocation,
        cmd.draw.startInstanceLocation);
}

} // namespace
void GPUCullingPipeline::RequestCommandReadback(uint32_t commandCount) {
    if (commandCount == 0) {
        return;
    }
    m_commandReadbackRequested = true;
    m_commandReadbackCount = commandCount;
}

void GPUCullingPipeline::UpdateVisibleCountFromReadback() {
    if (!m_commandCountReadback) {
        return;
    }

    m_debugStats.enabled = m_debugEnabled;
    m_debugStats.valid = false;

    uint32_t* mapped = nullptr;
    D3D12_RANGE readRange = {0, sizeof(uint32_t)};
    HRESULT hr = m_commandCountReadback->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
    if (SUCCEEDED(hr) && mapped) {
        m_visibleCount = std::min(*mapped, m_totalInstances);
        m_commandCountReadback->Unmap(0, nullptr);
    }

    // The command counter is always copied for telemetry. Detailed frustum and
    // occlusion counters are optional, but the frame contract still needs a
    // valid high-level culling summary in normal builds.
    m_debugStats.valid = m_totalInstances > 0;
    m_debugStats.tested = m_totalInstances;
    m_debugStats.frustumCulled = 0;
    m_debugStats.occluded = 0;
    m_debugStats.visible = m_visibleCount;

    if (m_commandReadbackPending && m_visibleCommandReadback && m_commandReadbackCount > 0) {
        const size_t readbackBytes = static_cast<size_t>(m_commandReadbackCount) * sizeof(IndirectCommand);
        D3D12_RANGE cmdReadRange = { 0, readbackBytes };
        void* commandData = nullptr;
        HRESULT cmdHr = m_visibleCommandReadback->Map(0, &cmdReadRange, &commandData);
        if (SUCCEEDED(cmdHr) && commandData) {
            const auto* commands = static_cast<const IndirectCommand*>(commandData);
            const uint32_t maxLog = std::min(m_commandReadbackCount, 2u);
            for (uint32_t i = 0; i < maxLog; ++i) {
                LogIndirectCommand("GPU VisibleCmd", i, commands[i]);
            }
            m_visibleCommandReadback->Unmap(0, nullptr);
        }
        m_commandReadbackPending = false;
    }

    if (m_debugReadbackPending && m_debugReadback) {
        void* mappedData = nullptr;
        D3D12_RANGE dbgRange = {0, 64};
        HRESULT dbgHr = m_debugReadback->Map(0, &dbgRange, &mappedData);
        if (SUCCEEDED(dbgHr) && mappedData) {
            const auto* u32 = static_cast<const uint32_t*>(mappedData);
            m_debugStats.valid = true;
            m_debugStats.tested = u32[0];
            m_debugStats.frustumCulled = u32[1];
            m_debugStats.occluded = u32[2];
            m_debugStats.visible = u32[3];

            float nearDepth = 0.0f;
            float hzbDepth = 0.0f;
            std::memcpy(&nearDepth, &u32[4], sizeof(float));
            std::memcpy(&hzbDepth, &u32[5], sizeof(float));
            m_debugStats.sampleNearDepth = nearDepth;
            m_debugStats.sampleHzbDepth = hzbDepth;
            m_debugStats.sampleMip = u32[6];
            m_debugStats.sampleFlags = u32[7];

            m_debugReadback->Unmap(0, nullptr);
        } else {
            m_debugStats.enabled = false;
        }
        m_debugReadbackPending = false;
    }
}

} // namespace Cortex::Graphics

