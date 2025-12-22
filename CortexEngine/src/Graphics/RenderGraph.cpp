#include "RenderGraph.h"
#include "RHI/DX12Device.h"
#include "RHI/DX12CommandQueue.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cassert>
#include <unordered_set>

namespace Cortex::Graphics {

namespace {
uint32_t GetSubresourceCount(const D3D12_RESOURCE_DESC& desc) {
    if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
        return 1;
    }

    const uint32_t mipLevels = std::max<uint32_t>(1u, desc.MipLevels);
    if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
        return mipLevels;
    }

    const uint32_t arraySize = std::max<uint32_t>(1u, desc.DepthOrArraySize);
    return mipLevels * arraySize;
}

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

// RGPassBuilder implementation
RGPassBuilder::RGPassBuilder(RenderGraph* graph, size_t passIndex)
    : m_graph(graph), m_passIndex(passIndex) {}

RGPassBuilder& RGPassBuilder::Read(RGResourceHandle handle, RGResourceUsage usage, uint32_t subresource) {
    if (handle.IsValid()) {
        m_graph->RegisterRead(m_passIndex, handle, usage, subresource);
    }
    return *this;
}

RGPassBuilder& RGPassBuilder::Write(RGResourceHandle handle, RGResourceUsage usage, uint32_t subresource) {
    if (handle.IsValid()) {
        m_graph->RegisterWrite(m_passIndex, handle, usage, subresource);
    }
    return *this;
}

RGPassBuilder& RGPassBuilder::ReadWrite(RGResourceHandle handle, uint32_t subresource) {
    if (handle.IsValid()) {
        m_graph->RegisterReadWrite(m_passIndex, handle, subresource);
    }
    return *this;
}

RGPassBuilder& RGPassBuilder::Alias(RGResourceHandle before, RGResourceHandle after) {
    m_graph->RegisterAliasing(m_passIndex, before, after);
    return *this;
}

RGPassBuilder& RGPassBuilder::SetType(RGPassType type) {
    if (m_passIndex < m_graph->m_passes.size()) {
        m_graph->m_passes[m_passIndex].type = type;
    }
    return *this;
}

RGResourceHandle RGPassBuilder::CreateTransient(const RGResourceDesc& desc) {
    return m_graph->CreateTransientResource(desc);
}

// RenderGraph implementation
Result<void> RenderGraph::Initialize(DX12Device* device,
                                      DX12CommandQueue* graphicsQueue,
                                      DX12CommandQueue* computeQueue,
                                      DX12CommandQueue* copyQueue) {
    if (!device || !graphicsQueue) {
        return Result<void>::Err("RenderGraph requires device and graphics queue");
    }

    m_device = device;
    m_graphicsQueue = graphicsQueue;
    m_computeQueue = computeQueue;
    m_copyQueue = copyQueue;

    spdlog::info("RenderGraph initialized (compute queue: {}, copy queue: {})",
                 computeQueue != nullptr ? "yes" : "no",
                 copyQueue != nullptr ? "yes" : "no");

    return Result<void>::Ok();
}

void RenderGraph::Shutdown() {
    m_passes.clear();
    m_resources.clear();
    m_transientResources.clear();
    m_transientPool.clear();
    m_finalStates.clear();
    m_device = nullptr;
    m_graphicsQueue = nullptr;
    m_computeQueue = nullptr;
    m_copyQueue = nullptr;
}

void RenderGraph::BeginFrame() {
    // Return last frame's transient resources to the pool.
    for (const auto& res : m_transientResources) {
        if (!res) {
            continue;
        }
        const auto key = MakePoolKey(res->GetDesc());
        m_transientPool[key].push_back(res);
    }

    // Clear passes from previous frame
    m_passes.clear();
    m_resources.clear();
    m_transientResources.clear();
    m_finalStates.clear();
    m_nextResourceId = 0;
    m_compiled = false;
    m_culledPassCount = 0;
    m_totalBarrierCount = 0;
}

RGResourceHandle RenderGraph::ImportResource(ID3D12Resource* resource,
                                              D3D12_RESOURCE_STATES currentState,
                                              const std::string& name) {
    if (!resource) {
        return RGResourceHandle{UINT32_MAX};
    }

    RGResource rgRes;
    rgRes.resource = resource;
    rgRes.subresourceStates.assign(GetSubresourceCount(resource->GetDesc()), currentState);
    rgRes.isExternal = true;
    rgRes.isTransient = false;
    rgRes.name = name.empty() ? "ExternalResource" : name;

    RGResourceHandle handle;
    handle.id = m_nextResourceId++;
    m_resources.push_back(rgRes);

    return handle;
}

size_t RenderGraph::AddPassInternal(const std::string& name, RGPass::ExecuteCallback execute) {
    RGPass pass;
    pass.name = name;
    pass.execute = std::move(execute);
    m_passes.push_back(std::move(pass));
    return m_passes.size() - 1;
}

void RenderGraph::RegisterRead(size_t passIndex,
                               RGResourceHandle handle,
                               RGResourceUsage usage,
                               uint32_t subresource) {
    if (passIndex < m_passes.size()) {
        m_passes[passIndex].reads.push_back(RGResourceAccess{handle, usage, subresource});
    }
}

void RenderGraph::RegisterWrite(size_t passIndex,
                                RGResourceHandle handle,
                                RGResourceUsage usage,
                                uint32_t subresource) {
    if (passIndex < m_passes.size()) {
        m_passes[passIndex].writes.push_back(RGResourceAccess{handle, usage, subresource});
    }
}

void RenderGraph::RegisterReadWrite(size_t passIndex, RGResourceHandle handle, uint32_t subresource) {
    if (passIndex < m_passes.size()) {
        m_passes[passIndex].readWrites.push_back(
            RGResourceAccess{handle, RGResourceUsage::UnorderedAccess, subresource});
    }
}

void RenderGraph::RegisterAliasing(size_t passIndex, RGResourceHandle before, RGResourceHandle after) {
    if (passIndex < m_passes.size()) {
        m_passes[passIndex].aliasing.push_back(RGAliasingBarrier{before, after});
    }
}

RGResourceHandle RenderGraph::CreateTransientResource(const RGResourceDesc& desc) {
    // For now, create the resource immediately
    // Future: use aliasing and deferred allocation

    if (!m_device) {
        return RGResourceHandle{UINT32_MAX};
    }

    ComPtr<ID3D12Resource> resource;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resDesc = {};

    if (desc.type == RGResourceDesc::Type::Texture2D) {
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resDesc.Width = desc.width;
        resDesc.Height = desc.height;
        resDesc.DepthOrArraySize = static_cast<UINT16>(desc.arraySize);
        resDesc.MipLevels = static_cast<UINT16>(desc.mipLevels);
        resDesc.Format = desc.format;
        resDesc.SampleDesc.Count = 1;
        resDesc.SampleDesc.Quality = 0;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resDesc.Flags = desc.flags;
    } else {
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Width = desc.bufferSize;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.Format = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc.Count = 1;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resDesc.Flags = desc.flags;
    }

    D3D12_CLEAR_VALUE* clearValue = nullptr;
    D3D12_CLEAR_VALUE clearValueData = {};

    // Set clear value for render targets and depth buffers
    if (desc.flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) {
        clearValueData.Format = desc.format;
        clearValueData.Color[0] = 0.0f;
        clearValueData.Color[1] = 0.0f;
        clearValueData.Color[2] = 0.0f;
        clearValueData.Color[3] = 1.0f;
        clearValue = &clearValueData;
    } else if (desc.flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) {
        clearValueData.Format = desc.format;
        clearValueData.DepthStencil.Depth = 1.0f;
        clearValueData.DepthStencil.Stencil = 0;
        clearValue = &clearValueData;
    }

    // Try to reuse a compatible resource from the pool.
    const auto poolKey = MakePoolKey(resDesc);
    auto poolIt = m_transientPool.find(poolKey);
    if (poolIt != m_transientPool.end() && !poolIt->second.empty()) {
        resource = poolIt->second.back();
        poolIt->second.pop_back();
        if (!resource) {
            resource.Reset();
        }
    }

    HRESULT hr = S_OK;
    if (!resource) {
        hr = m_device->GetDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resDesc,
            D3D12_RESOURCE_STATE_COMMON,
            clearValue,
            IID_PPV_ARGS(&resource)
        );
    }

    if (FAILED(hr)) {
        spdlog::error("RenderGraph: Failed to create transient resource '{}'", desc.debugName);
        return RGResourceHandle{UINT32_MAX};
    }

    // Set debug name
    if (!desc.debugName.empty()) {
        std::wstring wname(desc.debugName.begin(), desc.debugName.end());
        resource->SetName(wname.c_str());
    }

    // Track the transient resource
    m_transientResources.push_back(resource);

    RGResource rgRes;
    rgRes.resource = resource.Get();
    rgRes.subresourceStates.assign(GetSubresourceCount(resource->GetDesc()), D3D12_RESOURCE_STATE_COMMON);
    rgRes.desc = desc;
    rgRes.isExternal = false;
    rgRes.isTransient = true;
    rgRes.name = desc.debugName;

    RGResourceHandle handle;
    handle.id = m_nextResourceId++;
    m_resources.push_back(rgRes);

    return handle;
}

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

    // Track UAV write hazards per-subresource. When a resource stays in UAV state
    // across passes, transitions won't be emitted, but UAV->UAV ordering may still
    // require an explicit UAV barrier.
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
            // if the previous pass wrote UAV data for this subresource.
            if (currentState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS && uavPending[sub] != 0) {
                emitUavBarrier(pass, access.handle.id);
            }

            uavPending[sub] = isWriteAccess ? 1u : 0u;
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
                    std::fill(uavPending.begin(), uavPending.end(), isWriteAccess ? 1u : 0u);
                } else if (required != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
                    std::fill(uavPending.begin(), uavPending.end(), 0u);
                } else {
                    // Transition into UAV: pending hazard is cleared by the transition.
                    std::fill(uavPending.begin(), uavPending.end(), isWriteAccess ? 1u : 0u);
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

    for (const auto& pass : m_passes) {
        if (pass.culled) continue;

        // Execute pre-barriers
        if (!pass.preBarriers.empty()) {
            cmdList->ResourceBarrier(
                static_cast<UINT>(pass.preBarriers.size()),
                pass.preBarriers.data()
            );
        }

        // Execute the pass
        if (pass.execute) {
            pass.execute(cmdList, *this);
        }

        // Execute post-barriers (if any)
        if (!pass.postBarriers.empty()) {
            cmdList->ResourceBarrier(
                static_cast<UINT>(pass.postBarriers.size()),
                pass.postBarriers.data()
            );
        }
    }

    return Result<void>::Ok();
}

void RenderGraph::EndFrame() {
    // Transient resources are held in m_transientResources and will be
    // released on the next BeginFrame() or Shutdown().
    // Future: implement proper resource aliasing to reuse memory.
}

ID3D12Resource* RenderGraph::GetResource(RGResourceHandle handle) const {
    if (!handle.IsValid() || handle.id >= m_resources.size()) {
        return nullptr;
    }
    return m_resources[handle.id].resource;
}

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
