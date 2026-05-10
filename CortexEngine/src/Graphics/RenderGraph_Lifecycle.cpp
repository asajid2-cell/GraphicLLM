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
uint32_t GetSubresourceCountFromDesc(const RGResourceDesc& desc) {
    if (desc.type == RGResourceDesc::Type::Buffer) {
        return 1;
    }
    const uint32_t mipLevels = std::max<uint32_t>(1u, desc.mipLevels);
    const uint32_t arraySize = std::max<uint32_t>(1u, desc.arraySize);
    return mipLevels * arraySize;
}
} // namespace

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
    for (auto& resource : m_transientResources) {
        DeferredGPUDeletionQueue::Instance().QueueResource(std::move(resource));
    }
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
    for (auto& resource : m_transientResources) {
        DeferredGPUDeletionQueue::Instance().QueueResource(std::move(resource));
    }
    m_transientResources.clear();
    m_finalStates.clear();
    m_nextResourceId = 0;
    m_compiled = false;
    m_culledPassCount = 0;
    m_totalBarrierCount = 0;
    m_transientStats = {};
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

} // namespace Cortex::Graphics

