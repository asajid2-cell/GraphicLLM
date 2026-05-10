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

D3D12_RESOURCE_STATES RenderGraph::GetResourceState(RGResourceHandle handle) const {
    auto it = m_finalStates.find(handle);
    if (it != m_finalStates.end()) {
        return it->second;
    }
    if (handle.IsValid() && handle.id < m_resources.size()) {
        const auto& states = m_resources[handle.id].subresourceStates;
        if (states.empty()) {
            return D3D12_RESOURCE_STATE_COMMON;
        }
        const D3D12_RESOURCE_STATES first = states[0];
        return AreAllSubresourcesInState(states, first) ? first : D3D12_RESOURCE_STATE_COMMON;
    }
    return D3D12_RESOURCE_STATE_COMMON;
}

D3D12_RESOURCE_STATES RenderGraph::GetResourceState(RGResourceHandle handle, uint32_t subresource) const {
    if (!handle.IsValid() || handle.id >= m_resources.size()) {
        return D3D12_RESOURCE_STATE_COMMON;
    }

    if (subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) {
        return GetResourceState(handle);
    }

    const auto& states = m_resources[handle.id].subresourceStates;
    if (states.empty()) {
        return D3D12_RESOURCE_STATE_COMMON;
    }

    if (subresource >= static_cast<uint32_t>(states.size())) {
        return D3D12_RESOURCE_STATE_COMMON;
    }

    return states[subresource];
}

} // namespace Cortex::Graphics

