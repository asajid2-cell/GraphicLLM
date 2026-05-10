#include "RenderGraph.h"
#include "MeshBuffers.h"
#include "RHI/DX12Device.h"
#include "RHI/DX12CommandQueue.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <unordered_set>

namespace Cortex::Graphics {
namespace {
bool AreAllSubresourcesInState(const std::vector<D3D12_RESOURCE_STATES>& states,
                              D3D12_RESOURCE_STATES state) {
    for (const auto& s : states) {
        if (s != state) {
            return false;
        }
    }
    return true;
}
} // namespace

void RenderGraph::CullPasses() {
    // Simple culling: mark passes as culled if they have no writes
    // and no side effects (execute callback can mark pass as having side effects)
    // For now, don't cull any passes - all passes are assumed to have side effects

    m_culledPassCount = 0;

    // Future: implement proper dead-pass elimination by tracking
    // which resources are actually consumed by subsequent passes
    // or external outputs (swap chain, etc.)
}

Result<void> RenderGraph::Compile() {
    if (m_compiled) {
        return Result<void>::Ok();
    }

    // First cull unused passes
    CullPasses();

    // Debug validation: a pass must declare ReadWrite() if it both reads and writes
    // the same subresource; this catches accidental SRV+UAV mismatches.
    for (const auto& pass : m_passes) {
        if (pass.culled) {
            continue;
        }

        std::unordered_set<uint64_t> readSubs;
        std::unordered_set<uint64_t> writeSubs;
        std::unordered_set<uint64_t> readWriteSubs;

        auto addExpanded = [&](const RGResourceAccess& access, std::unordered_set<uint64_t>& dst) -> bool {
            if (!access.handle.IsValid() || access.handle.id >= m_resources.size()) {
                return true;
            }

            const auto& states = m_resources[access.handle.id].subresourceStates;
            const uint32_t subCount = states.empty() ? 1u : static_cast<uint32_t>(states.size());

            auto addOne = [&](uint32_t sub) {
                const uint64_t key = (static_cast<uint64_t>(access.handle.id) << 32) | sub;
                dst.insert(key);
            };

            if (access.subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) {
                for (uint32_t sub = 0; sub < subCount; ++sub) {
                    addOne(sub);
                }
                return true;
            }

            if (access.subresource >= subCount) {
                spdlog::error("RenderGraph: Pass '{}' requested subresource {} for '{}' ({} subresources)",
                              pass.name, access.subresource, m_resources[access.handle.id].name, subCount);
                return false;
            }

            addOne(access.subresource);
            return true;
        };

        for (const auto& access : pass.reads) {
            if (!addExpanded(access, readSubs)) {
                return Result<void>::Err("RenderGraph Compile validation failed (invalid read subresource)");
            }
        }
        for (const auto& access : pass.writes) {
            if (!addExpanded(access, writeSubs)) {
                return Result<void>::Err("RenderGraph Compile validation failed (invalid write subresource)");
            }
        }
        for (const auto& access : pass.readWrites) {
            if (!addExpanded(access, readWriteSubs)) {
                return Result<void>::Err("RenderGraph Compile validation failed (invalid readwrite subresource)");
            }
        }

        for (const auto& key : readSubs) {
            if (writeSubs.contains(key) && !readWriteSubs.contains(key)) {
                const uint32_t resId = static_cast<uint32_t>(key >> 32);
                const uint32_t sub = static_cast<uint32_t>(key & 0xFFFFFFFFu);
                spdlog::error("RenderGraph: Pass '{}' both reads+writes '{}' subresource {} without ReadWrite()",
                              pass.name, (resId < m_resources.size() ? m_resources[resId].name : "Unknown"), sub);
#ifndef NDEBUG
                assert(false && "RenderGraph: pass reads+writes same subresource without ReadWrite()");
#endif
                return Result<void>::Err("RenderGraph Compile validation failed (read+write without ReadWrite)");
            }
        }
    }

    // Allocate transient resources (placed resources) after validation but
    // before barrier computation (barriers reference the actual ID3D12Resource*).
    if (auto allocResult = AllocateTransientResources(); allocResult.IsErr()) {
        return allocResult;
    }

    // Then compute barriers
    ComputeBarriers();

    m_compiled = true;

    if (std::getenv("CORTEX_RG_DUMP") != nullptr) {
        auto stateToString = [](D3D12_RESOURCE_STATES s) -> std::string {
            // Bitmask-friendly rendering of common composite states.
            if (s == D3D12_RESOURCE_STATE_COMMON) {
                // Note: PRESENT is also 0.
                return "COMMON/PRESENT";
            }

            std::string out;
            auto add = [&](const char* tag) {
                if (!out.empty()) {
                    out += "|";
                }
                out += tag;
            };

            if ((s & D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER) != 0) add("VB|CB");
            if ((s & D3D12_RESOURCE_STATE_INDEX_BUFFER) != 0) add("IB");
            if ((s & D3D12_RESOURCE_STATE_RENDER_TARGET) != 0) add("RTV");
            if ((s & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) != 0) add("UAV");
            if ((s & D3D12_RESOURCE_STATE_DEPTH_WRITE) != 0) add("DEPTH_WRITE");
            if ((s & D3D12_RESOURCE_STATE_DEPTH_READ) != 0) add("DEPTH_READ");
            if ((s & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) != 0) add("PIXEL_SRV");
            if ((s & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) != 0) add("NON_PIXEL_SRV");
            if ((s & D3D12_RESOURCE_STATE_COPY_DEST) != 0) add("COPY_DST");
            if ((s & D3D12_RESOURCE_STATE_COPY_SOURCE) != 0) add("COPY_SRC");
            if ((s & D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT) != 0) add("INDIRECT");

            if (out.empty()) {
                out = "UNKNOWN";
            }
            return out;
        };

        spdlog::info("RG dump: passes={}, culled={}, barriers={}", m_passes.size(), m_culledPassCount, m_totalBarrierCount);
        for (uint32_t id = 0; id < static_cast<uint32_t>(m_resources.size()); ++id) {
            const auto& res = m_resources[id];
            const auto& states = res.subresourceStates;
            const D3D12_RESOURCE_STATES first = states.empty() ? D3D12_RESOURCE_STATE_COMMON : states[0];
            const bool uniform = states.empty() ? true : AreAllSubresourcesInState(states, first);
            spdlog::info("  RG res[{}] '{}' ext={} transient={} state={}{}",
                         id,
                         res.name,
                         res.isExternal ? 1 : 0,
                         res.isTransient ? 1 : 0,
                         stateToString(uniform ? first : D3D12_RESOURCE_STATE_COMMON),
                         uniform ? "" : " (per-subresource)");
        }
    }

    spdlog::debug("RenderGraph compiled: {} passes, {} culled, {} barriers",
                  m_passes.size(), m_culledPassCount, m_totalBarrierCount);

    return Result<void>::Ok();
}

Result<void> RenderGraph::Execute(ID3D12GraphicsCommandList* cmdList) {
    if (!m_compiled) {
        auto compileResult = Compile();
        if (compileResult.IsErr()) {
            return compileResult;
        }
    }

    if (!cmdList) {
        return Result<void>::Err("RenderGraph::Execute requires a command list");
    }

    const bool traceExecution = (std::getenv("CORTEX_RG_TRACE_EXECUTION") != nullptr);
    for (const auto& pass : m_passes) {
        if (pass.culled) continue;

        // Execute pre-barriers
        if (!pass.preBarriers.empty()) {
            if (traceExecution) {
                spdlog::info("RG execute: {} pre-barriers={}", pass.name, pass.preBarriers.size());
            }
            cmdList->ResourceBarrier(
                static_cast<UINT>(pass.preBarriers.size()),
                pass.preBarriers.data()
            );
        }

        // Execute the pass
        if (pass.execute) {
            if (traceExecution) {
                spdlog::info("RG execute: {} callback begin", pass.name);
            }
            pass.execute(cmdList, *this);
            if (traceExecution) {
                spdlog::info("RG execute: {} callback end", pass.name);
            }
        }

        // Execute post-barriers (if any)
        if (!pass.postBarriers.empty()) {
            if (traceExecution) {
                spdlog::info("RG execute: {} post-barriers={}", pass.name, pass.postBarriers.size());
            }
            cmdList->ResourceBarrier(
                static_cast<UINT>(pass.postBarriers.size()),
                pass.postBarriers.data()
            );
        }
    }

    return Result<void>::Ok();
}

} // namespace Cortex::Graphics

