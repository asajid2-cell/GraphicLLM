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

D3D12_RESOURCE_STATES RenderGraph::UsageToState(RGResourceUsage usage) const {
    // State mapping must be composable where legal, but some usages imply an
    // exclusive state (RTV/DSV write/UAV/copy-dst/present).
    //
    // Note: This is a "minimum required" state mapping; specific resources
    // (e.g., depth used as SRV + DSV read-only) may require additional flags.

    if (HasFlag(usage, RGResourceUsage::Present)) {
        return D3D12_RESOURCE_STATE_PRESENT;
    }
    if (HasFlag(usage, RGResourceUsage::CopyDst)) {
        return D3D12_RESOURCE_STATE_COPY_DEST;
    }
    if (HasFlag(usage, RGResourceUsage::DepthStencilWrite)) {
        return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }
    if (HasFlag(usage, RGResourceUsage::RenderTarget)) {
        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    if (HasFlag(usage, RGResourceUsage::UnorderedAccess)) {
        return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

    if (HasFlag(usage, RGResourceUsage::DepthStencilRead)) {
        state |= D3D12_RESOURCE_STATE_DEPTH_READ;
    }
    if (HasFlag(usage, RGResourceUsage::ShaderResource)) {
        state |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }
    if (HasFlag(usage, RGResourceUsage::CopySrc)) {
        state |= D3D12_RESOURCE_STATE_COPY_SOURCE;
    }
    if (HasFlag(usage, RGResourceUsage::IndirectArgument)) {
        state |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    }

    return state;
}

void RenderGraph::ComputeBarriers() {
    m_totalBarrierCount = 0;
    m_finalStates.clear();

    // Track UAV ordering hazards per-subresource. When a resource stays in UAV state
    // across passes, transitions won't be emitted, but UAV->UAV ordering may still
    // require an explicit UAV barrier (read/write ordering).
    std::vector<std::vector<uint8_t>> uavWritePending;
    uavWritePending.resize(m_resources.size());
    for (size_t i = 0; i < m_resources.size(); ++i) {
        const auto& states = m_resources[i].subresourceStates;
        const size_t count = states.empty() ? 1u : states.size();
        uavWritePending[i].assign(count, 0);
    }

    auto emitTransition = [&](RGPass& pass,
                              uint32_t resourceId,
                              uint32_t subresource,
                              D3D12_RESOURCE_STATES stateBefore,
                              D3D12_RESOURCE_STATES stateAfter) {
        if (stateBefore == stateAfter) {
            return;
        }

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_resources[resourceId].resource;
        barrier.Transition.StateBefore = stateBefore;
        barrier.Transition.StateAfter = stateAfter;
        barrier.Transition.Subresource = subresource;
        pass.preBarriers.push_back(barrier);
        m_totalBarrierCount++;
    };

    // Deduplicate UAV barriers within a pass (UAV barrier is per-resource).
    std::vector<uint8_t> uavBarrierEmitted;
    uavBarrierEmitted.resize(m_resources.size());

    auto emitUavBarrier = [&](RGPass& pass, uint32_t resourceId) {
        if (resourceId >= uavBarrierEmitted.size()) {
            return;
        }
        if (uavBarrierEmitted[resourceId] != 0) {
            return;
        }
        uavBarrierEmitted[resourceId] = 1;
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = m_resources[resourceId].resource;
        pass.preBarriers.push_back(barrier);
        m_totalBarrierCount++;
    };

    auto transitionAccess = [&](RGPass& pass, const RGResourceAccess& access, bool isWriteAccess) {
        if (!access.handle.IsValid() || access.handle.id >= m_resources.size()) {
            return;
        }

        RGResource& rgRes = m_resources[access.handle.id];
        if (rgRes.subresourceStates.empty()) {
            rgRes.subresourceStates.assign(1, D3D12_RESOURCE_STATE_COMMON);
        }

        const D3D12_RESOURCE_STATES required = UsageToState(access.usage);
        auto& states = rgRes.subresourceStates;
        auto& uavPending = uavWritePending[access.handle.id];

        auto handleUavHazard = [&](uint32_t sub, D3D12_RESOURCE_STATES currentState) {
            if (required != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
                if (sub < uavPending.size()) {
                    uavPending[sub] = 0;
                }
                return;
            }

            if (sub >= uavPending.size()) {
                return;
            }

            // If both states are UAV, transitions will not be emitted; insert a UAV barrier
            // if any previous UAV access (read or write) for this subresource is pending.
            if (currentState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS && uavPending[sub] != 0) {
                emitUavBarrier(pass, access.handle.id);
            }

            // Mark that this pass performed a UAV access; a subsequent UAV access
            // in a later pass requires ordering via a UAV barrier when no transition
            // occurs between them.
            uavPending[sub] = 1u;
        };

        if (access.subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) {
            const D3D12_RESOURCE_STATES first = states[0];
            const bool uniform = AreAllSubresourcesInState(states, first);

                if (uniform) {
                    // UAV hazard when staying in UAV state across passes.
                    if (required == D3D12_RESOURCE_STATE_UNORDERED_ACCESS &&
                        first == D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
                        const bool anyPending = std::any_of(
                            uavPending.begin(), uavPending.end(), [](uint8_t v) { return v != 0; });
                        if (anyPending) {
                            emitUavBarrier(pass, access.handle.id);
                        }
                        std::fill(uavPending.begin(), uavPending.end(), 1u);
                    } else if (required != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
                        std::fill(uavPending.begin(), uavPending.end(), 0u);
                    } else {
                        // Transition into UAV: pending hazard is cleared by the transition.
                        std::fill(uavPending.begin(), uavPending.end(), 1u);
                    }

                    if (first != required) {
                        emitTransition(pass, access.handle.id, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, first, required);
                        std::fill(states.begin(), states.end(), required);
                    }
                    return;
                }

            // Mixed per-subresource state: emit per-subresource barriers.
            for (uint32_t sub = 0; sub < static_cast<uint32_t>(states.size()); ++sub) {
                const D3D12_RESOURCE_STATES current = states[sub];
                handleUavHazard(sub, current);
                if (current != required) {
                    emitTransition(pass, access.handle.id, sub, current, required);
                    states[sub] = required;
                }
            }
            return;
        }

        const uint32_t sub = access.subresource;
        if (sub >= static_cast<uint32_t>(states.size())) {
            spdlog::error("RenderGraph: Pass '{}' requested subresource {} for '{}' ({} subresources)",
                          pass.name, sub, rgRes.name, states.size());
            return;
        }

        const D3D12_RESOURCE_STATES current = states[sub];
        handleUavHazard(sub, current);
        if (current != required) {
            emitTransition(pass, access.handle.id, sub, current, required);
            states[sub] = required;
        }
    };

    for (auto& pass : m_passes) {
        if (pass.culled) {
            continue;
        }

        pass.preBarriers.clear();
        pass.postBarriers.clear();
        std::fill(uavBarrierEmitted.begin(), uavBarrierEmitted.end(), 0);

        // Explicit aliasing barriers (placed resources only).
        for (const auto& alias : pass.aliasing) {
            ID3D12Resource* before = nullptr;
            ID3D12Resource* after = nullptr;
            if (alias.before.IsValid() && alias.before.id < m_resources.size()) {
                before = m_resources[alias.before.id].resource;
            }
            if (alias.after.IsValid() && alias.after.id < m_resources.size()) {
                after = m_resources[alias.after.id].resource;
            }

            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
            barrier.Aliasing.pResourceBefore = before;
            barrier.Aliasing.pResourceAfter = after;
            pass.preBarriers.push_back(barrier);
            m_totalBarrierCount++;
        }

        for (const auto& access : pass.reads) {
            transitionAccess(pass, access, false);
        }
        for (const auto& access : pass.readWrites) {
            transitionAccess(pass, access, true);
        }
        for (const auto& access : pass.writes) {
            transitionAccess(pass, access, true);
        }
    }

    // Store final (uniform) states for external tracking.
    for (uint32_t id = 0; id < static_cast<uint32_t>(m_resources.size()); ++id) {
        const auto& states = m_resources[id].subresourceStates;
        const D3D12_RESOURCE_STATES first = states.empty() ? D3D12_RESOURCE_STATE_COMMON : states[0];
        const bool uniform = states.empty() ? true : AreAllSubresourcesInState(states, first);

        RGResourceHandle handle;
        handle.id = id;
        m_finalStates[handle] = uniform ? first : D3D12_RESOURCE_STATE_COMMON;
    }
}

} // namespace Cortex::Graphics

