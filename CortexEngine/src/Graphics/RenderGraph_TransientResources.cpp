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

Result<void> RenderGraph::AllocateTransientResources() {
    m_transientStats = {};

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
        m_transientStats.requestedBytes += item.size;
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
    const bool aliasingEnabled = (std::getenv("CORTEX_RG_DISABLE_ALIASING") == nullptr);

    uint64_t heapEnd = 0;
    uint64_t maxAlignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    uint32_t aliasBarrierCount = 0;

    auto releaseBlocks = [&](uint32_t passIndex) {
        if (!aliasingEnabled) {
            return;
        }
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

    uint32_t aliasedResourceCount = 0;
    for (const auto& item : items) {
        if (item.aliasBefore != UINT32_MAX && item.aliasBefore != item.resId) {
            ++aliasedResourceCount;
        }
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

    m_transientStats.transientResourceCount = static_cast<uint32_t>(items.size());
    m_transientStats.placedResourceCount = static_cast<uint32_t>(items.size());
    m_transientStats.aliasedResourceCount = aliasedResourceCount;
    m_transientStats.aliasBarrierCount = aliasBarrierCount;
    m_transientStats.heapUsedBytes = alignedHeapSize;
    m_transientStats.heapSizeBytes = m_transientHeapSize;
    if (m_transientStats.requestedBytes > m_transientStats.heapUsedBytes) {
        m_transientStats.savedBytes = m_transientStats.requestedBytes - m_transientStats.heapUsedBytes;
    }

    if (std::getenv("CORTEX_RG_HEAP_DUMP") != nullptr) {
        spdlog::info("RenderGraph heap: transients={}, heapUsed={:.2f} MB, heapSize={:.2f} MB, aliasBarriers={}, aliasing={}",
                     m_transientStats.transientResourceCount,
                     static_cast<double>(m_transientStats.heapUsedBytes) / (1024.0 * 1024.0),
                     static_cast<double>(m_transientHeapSize) / (1024.0 * 1024.0),
                     aliasBarrierCount,
                     aliasingEnabled ? "on" : "off");
    }

    return Result<void>::Ok();
}

} // namespace Cortex::Graphics

