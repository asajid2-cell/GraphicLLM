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

} // namespace Cortex::Graphics

