#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <cstdint>

#include "Utils/Result.h"

using Microsoft::WRL::ComPtr;

namespace Cortex::Graphics {

class DX12Device;
class DX12CommandQueue;

// Resource usage flags for render graph passes
enum class RGResourceUsage : uint32_t {
    None = 0,
    ShaderResource = 1 << 0,      // SRV read
    UnorderedAccess = 1 << 1,     // UAV read/write
    RenderTarget = 1 << 2,        // RTV write
    DepthStencilWrite = 1 << 3,   // DSV write
    DepthStencilRead = 1 << 4,    // DSV read-only
    CopySrc = 1 << 5,             // Copy source
    CopyDst = 1 << 6,             // Copy destination
    IndirectArgument = 1 << 7,    // Indirect draw/dispatch argument
    Present = 1 << 8              // Present to swap chain
};

inline RGResourceUsage operator|(RGResourceUsage a, RGResourceUsage b) {
    return static_cast<RGResourceUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline RGResourceUsage operator&(RGResourceUsage a, RGResourceUsage b) {
    return static_cast<RGResourceUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline bool HasFlag(RGResourceUsage usage, RGResourceUsage flag) {
    return (static_cast<uint32_t>(usage) & static_cast<uint32_t>(flag)) != 0;
}

// Resource handle in the render graph (lightweight ID)
struct RGResourceHandle {
    uint32_t id = UINT32_MAX;

    bool IsValid() const { return id != UINT32_MAX; }
    bool operator==(const RGResourceHandle& other) const { return id == other.id; }
    bool operator!=(const RGResourceHandle& other) const { return id != other.id; }
};

// Hash for RGResourceHandle
struct RGResourceHandleHash {
    size_t operator()(const RGResourceHandle& h) const { return std::hash<uint32_t>()(h.id); }
};

// Resource description for transient resources created by the graph
struct RGResourceDesc {
    enum class Type { Texture2D, Buffer };

    Type type = Type::Texture2D;
    uint32_t width = 0;
    uint32_t height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    uint32_t mipLevels = 1;
    uint32_t arraySize = 1;
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;

    // For buffers
    uint64_t bufferSize = 0;

    // Debug name
    std::string debugName;

    static RGResourceDesc Texture2D(uint32_t w, uint32_t h, DXGI_FORMAT fmt,
                                     D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
                                     const std::string& name = "") {
        RGResourceDesc desc;
        desc.type = Type::Texture2D;
        desc.width = w;
        desc.height = h;
        desc.format = fmt;
        desc.flags = flags;
        desc.debugName = name;
        return desc;
    }

    static RGResourceDesc Buffer(uint64_t size, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
                                  const std::string& name = "") {
        RGResourceDesc desc;
        desc.type = Type::Buffer;
        desc.bufferSize = size;
        desc.flags = flags;
        desc.debugName = name;
        return desc;
    }
};

// Internal resource data tracked by the graph
struct RGResource {
    ID3D12Resource* resource = nullptr;          // The actual D3D12 resource
    D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_COMMON;
    RGResourceDesc desc;
    bool isExternal = false;                     // True if resource lifetime is external
    bool isTransient = false;                    // True if created/destroyed by graph
    std::string name;
};

// Pass type for queue selection
enum class RGPassType {
    Graphics,   // Runs on graphics queue
    Compute,    // Runs on async compute queue
    Copy        // Runs on copy queue
};

// Forward declaration
class RenderGraph;

// Render pass builder - used to declare resource dependencies
class RGPassBuilder {
public:
    RGPassBuilder(RenderGraph* graph, size_t passIndex);

    // Read a resource as SRV
    RGPassBuilder& Read(RGResourceHandle handle, RGResourceUsage usage = RGResourceUsage::ShaderResource);

    // Write to a resource (RTV, UAV, DSV)
    RGPassBuilder& Write(RGResourceHandle handle, RGResourceUsage usage);

    // Read and write (UAV)
    RGPassBuilder& ReadWrite(RGResourceHandle handle);

    // Set pass type for queue selection
    RGPassBuilder& SetType(RGPassType type);

    // Create a transient resource (lifetime managed by graph)
    RGResourceHandle CreateTransient(const RGResourceDesc& desc);

private:
    RenderGraph* m_graph;
    size_t m_passIndex;
};

// Render pass - represents a single render operation
struct RGPass {
    std::string name;
    RGPassType type = RGPassType::Graphics;

    // Resources read by this pass
    std::vector<std::pair<RGResourceHandle, RGResourceUsage>> reads;
    // Resources written by this pass
    std::vector<std::pair<RGResourceHandle, RGResourceUsage>> writes;

    // Execution callback
    using ExecuteCallback = std::function<void(ID3D12GraphicsCommandList*, const RenderGraph&)>;
    ExecuteCallback execute;

    // Computed barriers
    std::vector<D3D12_RESOURCE_BARRIER> preBarriers;
    std::vector<D3D12_RESOURCE_BARRIER> postBarriers;

    // Culled flag (set if pass has no side effects)
    bool culled = false;
};

// Render Graph
// Declarative system for organizing render passes with automatic barrier generation.
//
// Usage:
//   1. Begin frame with BeginFrame()
//   2. Register external resources with ImportResource()
//   3. Add passes with AddPass(), declaring dependencies via RGPassBuilder
//   4. Call Compile() to compute barriers and cull unused passes
//   5. Call Execute() to run all passes in order
//   6. Call EndFrame() to finalize
//
class RenderGraph {
public:
    RenderGraph() = default;
    ~RenderGraph() = default;

    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    // Initialize the render graph with device and queues
    Result<void> Initialize(DX12Device* device,
                            DX12CommandQueue* graphicsQueue,
                            DX12CommandQueue* computeQueue = nullptr,
                            DX12CommandQueue* copyQueue = nullptr);

    // Shutdown and release resources
    void Shutdown();

    // Begin a new frame (clears passes, resets transient allocations)
    void BeginFrame();

    // Import an external resource (lifetime managed externally)
    RGResourceHandle ImportResource(ID3D12Resource* resource,
                                     D3D12_RESOURCE_STATES currentState,
                                     const std::string& name = "");

    // Add a render pass
    // The setup callback receives a builder to declare dependencies
    // The execute callback runs the actual GPU work
    template<typename SetupFunc, typename ExecuteFunc>
    void AddPass(const std::string& name, SetupFunc&& setup, ExecuteFunc&& execute);

    // Compile the graph (compute barriers, cull unused passes)
    Result<void> Compile();

    // Execute all passes on the command list
    Result<void> Execute(ID3D12GraphicsCommandList* cmdList);

    // End frame (release transient resources if using aliasing)
    void EndFrame();

    // Get resource by handle (for execute callbacks)
    ID3D12Resource* GetResource(RGResourceHandle handle) const;
    D3D12_RESOURCE_STATES GetResourceState(RGResourceHandle handle) const;

    // Get current frame's final resource states (for external tracking)
    const std::unordered_map<RGResourceHandle, D3D12_RESOURCE_STATES, RGResourceHandleHash>&
        GetFinalResourceStates() const { return m_finalStates; }

    // Statistics
    [[nodiscard]] uint32_t GetPassCount() const { return static_cast<uint32_t>(m_passes.size()); }
    [[nodiscard]] uint32_t GetCulledPassCount() const { return m_culledPassCount; }
    [[nodiscard]] uint32_t GetBarrierCount() const { return m_totalBarrierCount; }

private:
    friend class RGPassBuilder;

    // Add a pass (internal)
    size_t AddPassInternal(const std::string& name, RGPass::ExecuteCallback execute);

    // Register resource usage for a pass
    void RegisterRead(size_t passIndex, RGResourceHandle handle, RGResourceUsage usage);
    void RegisterWrite(size_t passIndex, RGResourceHandle handle, RGResourceUsage usage);

    // Create transient resource
    RGResourceHandle CreateTransientResource(const RGResourceDesc& desc);

    // Convert usage to D3D12 state
    D3D12_RESOURCE_STATES UsageToState(RGResourceUsage usage) const;

    // Compute barriers between passes
    void ComputeBarriers();

    // Cull passes with no side effects
    void CullPasses();

    DX12Device* m_device = nullptr;
    DX12CommandQueue* m_graphicsQueue = nullptr;
    DX12CommandQueue* m_computeQueue = nullptr;
    DX12CommandQueue* m_copyQueue = nullptr;

    // Resources (external + transient)
    std::vector<RGResource> m_resources;
    uint32_t m_nextResourceId = 0;

    // Passes for current frame
    std::vector<RGPass> m_passes;

    // Final states after graph execution
    std::unordered_map<RGResourceHandle, D3D12_RESOURCE_STATES, RGResourceHandleHash> m_finalStates;

    // Transient resources (created each frame, can alias)
    std::vector<ComPtr<ID3D12Resource>> m_transientResources;

    // Statistics
    uint32_t m_culledPassCount = 0;
    uint32_t m_totalBarrierCount = 0;

    bool m_compiled = false;
};

// Template implementation
template<typename SetupFunc, typename ExecuteFunc>
void RenderGraph::AddPass(const std::string& name, SetupFunc&& setup, ExecuteFunc&& execute) {
    size_t passIndex = AddPassInternal(name, std::forward<ExecuteFunc>(execute));
    RGPassBuilder builder(this, passIndex);
    setup(builder);
}

} // namespace Cortex::Graphics
