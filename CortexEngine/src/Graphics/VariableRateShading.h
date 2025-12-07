#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <memory>
#include <functional>

#include "Utils/Result.h"
#include "RHI/DescriptorHeap.h"

using Microsoft::WRL::ComPtr;

namespace Cortex::Graphics {

class DX12Device;
class DescriptorHeapManager;

// VRS Tier support levels
enum class VRSTier {
    NotSupported = 0,
    Tier1 = 1,  // Per-draw shading rate only
    Tier2 = 2   // Per-draw + image-based shading rate
};

// VRS modes
enum class VRSMode {
    Off = 0,           // VRS disabled
    PerDraw = 1,       // Fixed shading rate per draw call
    ImageBased = 2,    // Per-tile shading rate from VRS image
    Adaptive = 3       // Dynamic based on motion/depth
};

// Shading rates (matches D3D12_SHADING_RATE)
enum class ShadingRate : uint8_t {
    Rate1x1 = 0,   // Full resolution
    Rate1x2 = 1,   // Half vertical
    Rate2x1 = 4,   // Half horizontal
    Rate2x2 = 5,   // Quarter resolution
    Rate2x4 = 6,   // 1/8 resolution
    Rate4x2 = 9,   // 1/8 resolution
    Rate4x4 = 10   // 1/16 resolution
};

// Variable Rate Shading Manager
// Implements D3D12 VRS Tier 2 for adaptive shading rate control.
//
// Usage:
//   1. Initialize with device
//   2. Call SetMode() to configure VRS behavior
//   3. Before rendering, call UpdateVRSImage() to generate shading rate image
//   4. Call BindForRendering() to apply VRS to command list
//
class VariableRateShadingManager {
public:
    VariableRateShadingManager() = default;
    ~VariableRateShadingManager() = default;

    VariableRateShadingManager(const VariableRateShadingManager&) = delete;
    VariableRateShadingManager& operator=(const VariableRateShadingManager&) = delete;

    // Initialize VRS system
    Result<void> Initialize(
        DX12Device* device,
        DescriptorHeapManager* descriptorManager,
        uint32_t screenWidth,
        uint32_t screenHeight
    );

    // Shutdown and release resources
    void Shutdown();

    // Resize VRS image (call on window resize)
    Result<void> Resize(uint32_t screenWidth, uint32_t screenHeight);

    // Get VRS tier supported by this device
    [[nodiscard]] VRSTier GetSupportedTier() const { return m_tier; }

    // Check if VRS is available
    [[nodiscard]] bool IsSupported() const { return m_tier != VRSTier::NotSupported; }

    // Get/Set VRS mode
    void SetMode(VRSMode mode);
    [[nodiscard]] VRSMode GetMode() const { return m_mode; }

    // Set base shading rate for per-draw mode
    void SetBaseShadingRate(ShadingRate rate) { m_baseShadingRate = rate; }
    [[nodiscard]] ShadingRate GetBaseShadingRate() const { return m_baseShadingRate; }

    // Adaptive VRS parameters
    void SetMotionThreshold(float threshold) { m_motionThreshold = threshold; }
    void SetDepthThreshold(float threshold) { m_depthThreshold = threshold; }
    void SetEdgeThreshold(float threshold) { m_edgeThreshold = threshold; }

    // Update VRS image based on velocity/depth buffers
    // Call before rendering the main pass
    Result<void> UpdateVRSImage(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* velocityBuffer,
        D3D12_GPU_DESCRIPTOR_HANDLE velocitySRV,
        ID3D12Resource* depthBuffer,
        D3D12_GPU_DESCRIPTOR_HANDLE depthSRV
    );

    // Bind VRS for rendering (applies shading rate to subsequent draws)
    void BindForRendering(ID3D12GraphicsCommandList5* cmdList);

    // Unbind VRS (reset to 1x1)
    void UnbindForRendering(ID3D12GraphicsCommandList5* cmdList);

    // Get VRS image for debug visualization
    [[nodiscard]] ID3D12Resource* GetVRSImage() const { return m_vrsImage.Get(); }

    // Get tile size (typically 8x8, 16x16, or 32x32 depending on hardware)
    [[nodiscard]] uint32_t GetTileSize() const { return m_tileSize; }

    // Statistics
    [[nodiscard]] float GetAverageShadingRate() const { return m_averageShadingRate; }

    // Flush callback for safe resizes
    using FlushCallback = std::function<void()>;
    void SetFlushCallback(FlushCallback callback) { m_flushCallback = std::move(callback); }

private:
    Result<void> CreateVRSImage();
    Result<void> CreateComputePipeline();

    DX12Device* m_device = nullptr;
    DescriptorHeapManager* m_descriptorManager = nullptr;

    VRSTier m_tier = VRSTier::NotSupported;
    VRSMode m_mode = VRSMode::Off;
    ShadingRate m_baseShadingRate = ShadingRate::Rate1x1;

    uint32_t m_screenWidth = 0;
    uint32_t m_screenHeight = 0;
    uint32_t m_tileSize = 16;  // VRS tile size
    uint32_t m_vrsWidth = 0;   // VRS image width (screen width / tile size)
    uint32_t m_vrsHeight = 0;  // VRS image height

    // VRS image (R8_UINT, one byte per tile)
    ComPtr<ID3D12Resource> m_vrsImage;
    DescriptorHandle m_vrsSRV;
    DescriptorHandle m_vrsUAV;
    D3D12_RESOURCE_STATES m_vrsState = D3D12_RESOURCE_STATE_COMMON;

    // Compute pipeline for generating adaptive VRS image
    ComPtr<ID3D12RootSignature> m_computeRootSignature;
    ComPtr<ID3D12PipelineState> m_computePipeline;

    // Adaptive VRS parameters
    float m_motionThreshold = 0.01f;  // Velocity threshold for reduced shading
    float m_depthThreshold = 0.1f;    // Depth discontinuity threshold
    float m_edgeThreshold = 0.1f;     // Edge detection threshold

    // Statistics
    float m_averageShadingRate = 1.0f;

    FlushCallback m_flushCallback;
};

} // namespace Cortex::Graphics
