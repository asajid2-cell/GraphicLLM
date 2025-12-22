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

uint32_t GetSubresourceCountFromDesc(const RGResourceDesc& desc) {
    if (desc.type == RGResourceDesc::Type::Buffer) {
        return 1;
    }
    const uint32_t mipLevels = std::max<uint32_t>(1u, desc.mipLevels);
    const uint32_t arraySize = std::max<uint32_t>(1u, desc.arraySize);
    return mipLevels * arraySize;
}

uint64_t AlignUp(uint64_t value, uint64_t alignment) {
    if (alignment == 0) {
        return value;
    }
    const uint64_t mask = alignment - 1;
    return (value + mask) & ~mask;
}

D3D12_RESOURCE_DESC ToD3D12Desc(const RGResourceDesc& desc) {
    D3D12_RESOURCE_DESC resDesc{};
    if (desc.type == RGResourceDesc::Type::Texture2D) {
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resDesc.Width = desc.width;
        resDesc.Height = desc.height;
        resDesc.DepthOrArraySize = static_cast<UINT16>(std::max<uint32_t>(1u, desc.arraySize));
        resDesc.MipLevels = static_cast<UINT16>(std::max<uint32_t>(1u, desc.mipLevels));
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
    return resDesc;
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
    m_transientHeap.Reset();
    m_transientHeapSize = 0;
    m_finalStates.clear();
    m_device = nullptr;
    m_graphicsQueue = nullptr;
    m_computeQueue = nullptr;
    m_copyQueue = nullptr;
}

void RenderGraph::BeginFrame() {
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
    if (!m_device) {
        return RGResourceHandle{UINT32_MAX};
    }

    RGResource rgRes;
    rgRes.desc = desc;
    rgRes.subresourceStates.assign(GetSubresourceCountFromDesc(desc), D3D12_RESOURCE_STATE_COMMON);
    rgRes.isExternal = false;
    rgRes.isTransient = true;
    rgRes.name = desc.debugName;

    RGResourceHandle handle;
    handle.id = m_nextResourceId++;
    m_resources.push_back(rgRes);

    return handle;
}

Result<void> RenderGraph::AllocateTransientResources() {
    if (!m_device) {
        return Result<void>::Err("RenderGraph: AllocateTransientResources requires a device");
    }

    // Compute per-resource lifetimes in the pass list.
    struct Lifetime {
        uint32_t first = UINT32_MAX;
        uint32_t last = 0;
        bool used = false;
    };
    std::vector<Lifetime> lifetimes;
    lifetimes.resize(m_resources.size());

    auto noteUse = [&](RGResourceHandle h, uint32_t passIndex) {
        if (!h.IsValid() || h.id >= m_resources.size()) {
            return;
        }
        const auto& res = m_resources[h.id];
        if (!res.isTransient) {
            return;
        }
        auto& lt = lifetimes[h.id];
        lt.used = true;
        lt.first = std::min(lt.first, passIndex);
        lt.last = std::max(lt.last, passIndex);
    };

    for (uint32_t passIndex = 0; passIndex < static_cast<uint32_t>(m_passes.size()); ++passIndex) {
        const auto& pass = m_passes[passIndex];
        if (pass.culled) {
            continue;
        }
        for (const auto& access : pass.reads) {
            noteUse(access.handle, passIndex);
        }
        for (const auto& access : pass.readWrites) {
            noteUse(access.handle, passIndex);
        }
        for (const auto& access : pass.writes) {
            noteUse(access.handle, passIndex);
        }
    }

    struct Item {
        uint32_t resId = UINT32_MAX;
        uint32_t firstUse = 0;
        uint32_t lastUse = 0;
        uint64_t size = 0;
        uint64_t alignment = 0;
        uint64_t offset = 0;
        uint32_t aliasBefore = UINT32_MAX;
        D3D12_RESOURCE_DESC d3dDesc{};
        bool hasClear = false;
        D3D12_CLEAR_VALUE clearValue{};
    };

    std::vector<Item> items;
    items.reserve(m_resources.size());

    ID3D12Device* device = m_device->GetDevice();
    if (!device) {
        return Result<void>::Err("RenderGraph: D3D12 device not available");
    }

    for (uint32_t id = 0; id < static_cast<uint32_t>(m_resources.size()); ++id) {
        auto& res = m_resources[id];
        if (!res.isTransient) {
            continue;
        }

        Item item;
        item.resId = id;
        const auto& lt = lifetimes[id];
        if (!lt.used) {
            continue;
        }
        item.firstUse = lt.first;
        item.lastUse = lt.last;
        item.d3dDesc = ToD3D12Desc(res.desc);

        const auto allocInfo = device->GetResourceAllocationInfo(0, 1, &item.d3dDesc);
        item.size = allocInfo.SizeInBytes;
        item.alignment = allocInfo.Alignment;
        if (item.size == 0 || item.alignment == 0) {
            return Result<void>::Err("RenderGraph: Invalid allocation info for transient resource '" + res.name + "'");
        }

        // Derive a default clear value for RTV/DSV transient targets for fast clear support.
        if ((item.d3dDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0) {
            item.hasClear = true;
            item.clearValue.Format = item.d3dDesc.Format;
            item.clearValue.Color[0] = 0.0f;
            item.clearValue.Color[1] = 0.0f;
            item.clearValue.Color[2] = 0.0f;
            item.clearValue.Color[3] = 1.0f;
        } else if ((item.d3dDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0) {
            item.hasClear = true;
            item.clearValue.Format = item.d3dDesc.Format;
            item.clearValue.DepthStencil.Depth = 1.0f;
            item.clearValue.DepthStencil.Stencil = 0;
        }

        items.push_back(item);
    }

    if (items.empty()) {
        return Result<void>::Ok();
    }

    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.firstUse != b.firstUse) return a.firstUse < b.firstUse;
        if (a.lastUse != b.lastUse) return a.lastUse < b.lastUse;
        return a.resId < b.resId;
    });

    struct ActiveAlloc {
        uint32_t lastUse = 0;
        uint64_t offset = 0;
        uint64_t size = 0;
        uint32_t owner = UINT32_MAX;
    };
    struct FreeBlock {
        uint64_t offset = 0;
        uint64_t size = 0;
        uint32_t lastOwner = UINT32_MAX;
    };

    std::vector<ActiveAlloc> active;
    std::vector<FreeBlock> freeBlocks;

    uint64_t heapEnd = 0;
    uint64_t maxAlignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    uint32_t aliasBarrierCount = 0;

    auto releaseBlocks = [&](uint32_t passIndex) {
        for (size_t i = 0; i < active.size();) {
            if (active[i].lastUse < passIndex) {
                FreeBlock block;
                block.offset = active[i].offset;
                block.size = active[i].size;
                block.lastOwner = active[i].owner;
                freeBlocks.push_back(block);
                active[i] = active.back();
                active.pop_back();
                continue;
            }
            ++i;
        }
    };

    for (auto& item : items) {
        releaseBlocks(item.firstUse);
        maxAlignment = std::max<uint64_t>(maxAlignment, item.alignment);

        int bestIndex = -1;
        uint64_t bestWaste = UINT64_MAX;
        uint64_t bestOffset = 0;

        for (int i = 0; i < static_cast<int>(freeBlocks.size()); ++i) {
            const auto& block = freeBlocks[i];
            const uint64_t alignedOffset = AlignUp(block.offset, item.alignment);
            if (alignedOffset < block.offset) {
                continue;
            }
            const uint64_t padding = alignedOffset - block.offset;
            if (padding > block.size) {
                continue;
            }
            const uint64_t usable = block.size - padding;
            if (usable < item.size) {
                continue;
            }
            const uint64_t waste = usable - item.size;
            if (waste < bestWaste) {
                bestWaste = waste;
                bestIndex = i;
                bestOffset = alignedOffset;
            }
        }

        if (bestIndex >= 0) {
            const FreeBlock block = freeBlocks[bestIndex];
            freeBlocks[bestIndex] = freeBlocks.back();
            freeBlocks.pop_back();

            const uint64_t alignedOffset = bestOffset;
            item.offset = alignedOffset;
            item.aliasBefore = block.lastOwner;

            const uint64_t prefixSize = alignedOffset - block.offset;
            if (prefixSize > 0) {
                FreeBlock prefix;
                prefix.offset = block.offset;
                prefix.size = prefixSize;
                prefix.lastOwner = block.lastOwner;
                freeBlocks.push_back(prefix);
            }
            const uint64_t end = alignedOffset + item.size;
            const uint64_t suffixOffset = end;
            const uint64_t suffixSize = (block.offset + block.size > end) ? (block.offset + block.size - end) : 0u;
            if (suffixSize > 0) {
                FreeBlock suffix;
                suffix.offset = suffixOffset;
                suffix.size = suffixSize;
                suffix.lastOwner = block.lastOwner;
                freeBlocks.push_back(suffix);
            }
        } else {
            const uint64_t alignedOffset = AlignUp(heapEnd, item.alignment);
            item.offset = alignedOffset;
            heapEnd = alignedOffset + item.size;
            item.aliasBefore = UINT32_MAX;
        }

        ActiveAlloc a;
        a.lastUse = item.lastUse;
        a.offset = item.offset;
        a.size = item.size;
        a.owner = item.resId;
        active.push_back(a);
    }

    // Validate that heap ranges do not overlap for overlapping lifetimes.
    for (size_t i = 0; i < items.size(); ++i) {
        for (size_t j = i + 1; j < items.size(); ++j) {
            const auto& a = items[i];
            const auto& b = items[j];
            const bool lifetimeOverlap = !(a.lastUse < b.firstUse || b.lastUse < a.firstUse);
            if (!lifetimeOverlap) {
                continue;
            }
            const uint64_t a0 = a.offset;
            const uint64_t a1 = a.offset + a.size;
            const uint64_t b0 = b.offset;
            const uint64_t b1 = b.offset + b.size;
            const bool rangeOverlap = (a0 < b1) && (b0 < a1);
            if (rangeOverlap) {
                const std::string an = (a.resId < m_resources.size() ? m_resources[a.resId].name : "Unknown");
                const std::string bn = (b.resId < m_resources.size() ? m_resources[b.resId].name : "Unknown");
                spdlog::error("RenderGraph: Transient heap overlap between '{}' and '{}' (lifetimes overlap)",
                              an, bn);
                return Result<void>::Err("RenderGraph transient heap packing overlap");
            }
        }
    }

    // Create or grow the backing heap.
    const uint64_t alignedHeapSize = AlignUp(heapEnd, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
    uint64_t heapSize = alignedHeapSize;
    if (heapSize == 0) {
        heapSize = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    }
    heapSize = AlignUp(heapSize, std::max<uint64_t>(D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT, maxAlignment));

    if (!m_transientHeap || heapSize > m_transientHeapSize) {
        const uint64_t newSize = std::max<uint64_t>(heapSize, m_transientHeapSize + (m_transientHeapSize / 2u) + (16u * 1024u * 1024u));

        D3D12_HEAP_DESC heapDesc{};
        heapDesc.SizeInBytes = AlignUp(newSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
        heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        heapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapDesc.Properties.CreationNodeMask = 1;
        heapDesc.Properties.VisibleNodeMask = 1;
        heapDesc.Alignment = 0;
        heapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;

        ComPtr<ID3D12Heap> heap;
        HRESULT hr = device->CreateHeap(&heapDesc, IID_PPV_ARGS(&heap));
        if (FAILED(hr) || !heap) {
            return Result<void>::Err("RenderGraph: failed to create transient heap");
        }
        heap->SetName(L"RenderGraphTransientHeap");
        m_transientHeap = heap;
        m_transientHeapSize = heapDesc.SizeInBytes;
        spdlog::info("RenderGraph: transient heap resized to {} MB", m_transientHeapSize / (1024u * 1024u));
    }

    // Materialize placed resources and store them in the graph.
    m_transientResources.reserve(m_transientResources.size() + items.size());
    for (const auto& item : items) {
        ComPtr<ID3D12Resource> resource;
        const D3D12_CLEAR_VALUE* clearPtr = item.hasClear ? &item.clearValue : nullptr;

        HRESULT hr = device->CreatePlacedResource(
            m_transientHeap.Get(),
            item.offset,
            &item.d3dDesc,
            D3D12_RESOURCE_STATE_COMMON,
            clearPtr,
            IID_PPV_ARGS(&resource)
        );
        if (FAILED(hr) || !resource) {
            const std::string name = (item.resId < m_resources.size() ? m_resources[item.resId].name : "Transient");
            return Result<void>::Err("RenderGraph: failed to create placed resource '" + name + "'");
        }

        if (item.resId < m_resources.size()) {
            auto& res = m_resources[item.resId];
            res.resource = resource.Get();
            if (!res.name.empty()) {
                std::wstring wname(res.name.begin(), res.name.end());
                resource->SetName(wname.c_str());
            }
        }

        m_transientResources.push_back(resource);
    }

    // Inject automatic aliasing barriers for reused blocks.
    for (const auto& item : items) {
        if (item.aliasBefore == UINT32_MAX || item.aliasBefore == item.resId) {
            continue;
        }
        if (item.firstUse >= m_passes.size()) {
            continue;
        }
        RGResourceHandle before{ item.aliasBefore };
        RGResourceHandle after{ item.resId };
        m_passes[item.firstUse].aliasing.push_back(RGAliasingBarrier{ before, after });
        ++aliasBarrierCount;
    }

    if (std::getenv("CORTEX_RG_HEAP_DUMP") != nullptr) {
        spdlog::info("RenderGraph heap: transients={}, heapUsed={:.2f} MB, heapSize={:.2f} MB, aliasBarriers={}",
                     static_cast<uint32_t>(items.size()),
                     static_cast<double>(heapEnd) / (1024.0 * 1024.0),
                     static_cast<double>(m_transientHeapSize) / (1024.0 * 1024.0),
                     aliasBarrierCount);
    }

    return Result<void>::Ok();
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
    // The backing heap (m_transientHeap) persists across frames and grows
    // as needed; resources are recreated as placed resources each frame.
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
