#include "RenderGraph.h"
#include "RHI/DX12Device.h"
#include "RHI/DX12CommandQueue.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace Cortex::Graphics {

// RGPassBuilder implementation
RGPassBuilder::RGPassBuilder(RenderGraph* graph, size_t passIndex)
    : m_graph(graph), m_passIndex(passIndex) {}

RGPassBuilder& RGPassBuilder::Read(RGResourceHandle handle, RGResourceUsage usage) {
    if (handle.IsValid()) {
        m_graph->RegisterRead(m_passIndex, handle, usage);
    }
    return *this;
}

RGPassBuilder& RGPassBuilder::Write(RGResourceHandle handle, RGResourceUsage usage) {
    if (handle.IsValid()) {
        m_graph->RegisterWrite(m_passIndex, handle, usage);
    }
    return *this;
}

RGPassBuilder& RGPassBuilder::ReadWrite(RGResourceHandle handle) {
    if (handle.IsValid()) {
        m_graph->RegisterRead(m_passIndex, handle, RGResourceUsage::UnorderedAccess);
        m_graph->RegisterWrite(m_passIndex, handle, RGResourceUsage::UnorderedAccess);
    }
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
    rgRes.currentState = currentState;
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

void RenderGraph::RegisterRead(size_t passIndex, RGResourceHandle handle, RGResourceUsage usage) {
    if (passIndex < m_passes.size()) {
        m_passes[passIndex].reads.push_back({handle, usage});
    }
}

void RenderGraph::RegisterWrite(size_t passIndex, RGResourceHandle handle, RGResourceUsage usage) {
    if (passIndex < m_passes.size()) {
        m_passes[passIndex].writes.push_back({handle, usage});
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

    HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resDesc,
        D3D12_RESOURCE_STATE_COMMON,
        clearValue,
        IID_PPV_ARGS(&resource)
    );

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
    rgRes.currentState = D3D12_RESOURCE_STATE_COMMON;
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
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

    if (HasFlag(usage, RGResourceUsage::ShaderResource)) {
        state |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }
    if (HasFlag(usage, RGResourceUsage::UnorderedAccess)) {
        state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;  // UAV is exclusive
    }
    if (HasFlag(usage, RGResourceUsage::RenderTarget)) {
        state = D3D12_RESOURCE_STATE_RENDER_TARGET;  // RTV is exclusive
    }
    if (HasFlag(usage, RGResourceUsage::DepthStencilWrite)) {
        state = D3D12_RESOURCE_STATE_DEPTH_WRITE;  // DSV write is exclusive
    }
    if (HasFlag(usage, RGResourceUsage::DepthStencilRead)) {
        state |= D3D12_RESOURCE_STATE_DEPTH_READ;
    }
    if (HasFlag(usage, RGResourceUsage::CopySrc)) {
        state |= D3D12_RESOURCE_STATE_COPY_SOURCE;
    }
    if (HasFlag(usage, RGResourceUsage::CopyDst)) {
        state = D3D12_RESOURCE_STATE_COPY_DEST;  // Copy dest is exclusive
    }
    if (HasFlag(usage, RGResourceUsage::IndirectArgument)) {
        state |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    }
    if (HasFlag(usage, RGResourceUsage::Present)) {
        state = D3D12_RESOURCE_STATE_PRESENT;
    }

    return state;
}

void RenderGraph::ComputeBarriers() {
    // Track current state of each resource across passes
    std::unordered_map<uint32_t, D3D12_RESOURCE_STATES> currentStates;

    // Initialize with imported resource states
    for (size_t i = 0; i < m_resources.size(); ++i) {
        currentStates[static_cast<uint32_t>(i)] = m_resources[i].currentState;
    }

    m_totalBarrierCount = 0;

    for (auto& pass : m_passes) {
        if (pass.culled) continue;

        pass.preBarriers.clear();
        pass.postBarriers.clear();

        // Compute required states for reads
        for (const auto& [handle, usage] : pass.reads) {
            if (!handle.IsValid() || handle.id >= m_resources.size()) continue;

            D3D12_RESOURCE_STATES required = UsageToState(usage);
            D3D12_RESOURCE_STATES current = currentStates[handle.id];

            if (current != required) {
                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = m_resources[handle.id].resource;
                barrier.Transition.StateBefore = current;
                barrier.Transition.StateAfter = required;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                pass.preBarriers.push_back(barrier);
                currentStates[handle.id] = required;
                m_totalBarrierCount++;
            }
        }

        // Compute required states for writes
        for (const auto& [handle, usage] : pass.writes) {
            if (!handle.IsValid() || handle.id >= m_resources.size()) continue;

            D3D12_RESOURCE_STATES required = UsageToState(usage);
            D3D12_RESOURCE_STATES current = currentStates[handle.id];

            if (current != required) {
                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = m_resources[handle.id].resource;
                barrier.Transition.StateBefore = current;
                barrier.Transition.StateAfter = required;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                pass.preBarriers.push_back(barrier);
                currentStates[handle.id] = required;
                m_totalBarrierCount++;
            }
        }
    }

    // Store final states for external tracking
    for (const auto& [id, state] : currentStates) {
        RGResourceHandle handle;
        handle.id = id;
        m_finalStates[handle] = state;
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

    // Then compute barriers
    ComputeBarriers();

    m_compiled = true;

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
        return m_resources[handle.id].currentState;
    }
    return D3D12_RESOURCE_STATE_COMMON;
}

} // namespace Cortex::Graphics
