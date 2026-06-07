#pragma once

#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/RHI/DX12Texture.h"
#include "Graphics/RHI/DescriptorHeap.h"
#include "Graphics/RenderGraph.h"

#include <array>
#include <cstdint>
#include <memory>

namespace Cortex::Graphics::FullSceneShaderV3Passes {

struct FullSceneCompositeV3Context {
    RGResourceHandle directLighting;
    RGResourceHandle indirectLighting;
    RGResourceHandle shadowVisibility;
    RGResourceHandle legacyHdr;
    RGResourceHandle localReflectionRadiance;
    RGResourceHandle reflectionConfidence;
    RGResourceHandle materialAlbedo;
    RGResourceHandle sceneLocalEnvironment;
    RGResourceHandle output;
    RGResourceHandle energyClampPolicy;
    RGResourceHandle overbrightDiagnostics;
    RGResourceHandle compositeContributionMap;
    RGResourceHandle legacyRescueUsage;
    ID3D12Device* device = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    DX12Pipeline* pipeline = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    DescriptorHandle directLightingSRV;
    DescriptorHandle indirectLightingSRV;
    DescriptorHandle shadowVisibilitySRV;
    DescriptorHandle legacyHdrSRV;
    DescriptorHandle sceneLocalEnvironmentSRV;
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 5> outputRTVs{};
    uint32_t width = 0;
    uint32_t height = 0;
    bool* ran = nullptr;
    bool* failed = nullptr;
    const char** stage = nullptr;
};

struct SceneLocalEnvironmentV3Context {
    RGResourceHandle sceneLocalEnvironment;
    RGResourceHandle ambientLighting;
    RGResourceHandle visibleBackground;
    RGResourceHandle reflectionBackground;
    RGResourceHandle atmosphere;
    ID3D12Device* device = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    DX12Pipeline* pipeline = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    std::shared_ptr<DX12Texture> payloadAlbedo;
    std::shared_ptr<DX12Texture> payloadNormal;
    std::shared_ptr<DX12Texture> irradianceProxy;
    std::shared_ptr<DX12Texture> specularProxy;
    std::shared_ptr<DX12Texture> visibleBackgroundProxy;
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 5> outputRTVs{};
    uint32_t width = 0;
    uint32_t height = 0;
    bool* ran = nullptr;
    bool* failed = nullptr;
    const char** stage = nullptr;
};

struct FullSceneReflectionResolverV3Context {
    RGResourceHandle localReflectionRadiance;
    RGResourceHandle ssr;
    RGResourceHandle rtReflection;
    RGResourceHandle historyPrevSourceId;
    RGResourceHandle historyValidity;
    RGResourceHandle historyRejection;
    RGResourceHandle normalRoughness;
    RGResourceHandle emissiveMetallic;
    RGResourceHandle materialExt2;
    RGResourceHandle radiance;
    RGResourceHandle confidence;
    RGResourceHandle sourceId;
    RGResourceHandle rejectedSourceMask;
    RGResourceHandle temporalDelta;
    RGResourceHandle ssrSourceSignal;
    RGResourceHandle rtSourceSignal;
    RGResourceHandle sourceSuppression;
    ID3D12Device* device = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    DX12Pipeline* pipeline = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 8> outputRTVs{};
    uint32_t width = 0;
    uint32_t height = 0;
    bool* ran = nullptr;
    bool* failed = nullptr;
    const char** stage = nullptr;
};

struct FullSceneReflectionHistoryV3Context {
    RGResourceHandle radiance;
    RGResourceHandle sourceId;
    RGResourceHandle temporalDelta;
    RGResourceHandle historyPrev;
    RGResourceHandle historyPrevSourceId;
    RGResourceHandle depth;
    RGResourceHandle normalRoughness;
    RGResourceHandle velocity;
    RGResourceHandle historyCurr;
    RGResourceHandle historyValidity;
    RGResourceHandle historyRejection;
    ID3D12Device* device = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    DX12Pipeline* pipeline = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 3> outputRTVs{};
    uint32_t width = 0;
    uint32_t height = 0;
    bool* ran = nullptr;
    bool* failed = nullptr;
    const char** stage = nullptr;
};

struct FullSceneReflectionHistoryV3CopyContext {
    RGResourceHandle historyCurr;
    RGResourceHandle historyPrev;
    RGResourceHandle sourceId;
    RGResourceHandle historyPrevSourceId;
    bool* ran = nullptr;
    bool* failed = nullptr;
    const char** stage = nullptr;
};

struct CandidateBeautyDisplayContext {
    const char* passName = "FullSceneCandidateBeautyV3Display";
    RGResourceHandle candidate;
    RGResourceHandle backBuffer;
    ID3D12Device* device = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    DX12Pipeline* pipeline = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    DescriptorHandle candidateSRV;
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferRTV{};
    uint32_t width = 0;
    uint32_t height = 0;
    bool* ran = nullptr;
    bool* failed = nullptr;
    const char** stage = nullptr;
};

[[nodiscard]] bool AddSceneLocalEnvironmentV3Pass(RenderGraph& graph,
                                                  const SceneLocalEnvironmentV3Context& context);
[[nodiscard]] bool AddFullSceneCompositeV3Pass(RenderGraph& graph,
                                               const FullSceneCompositeV3Context& context);
[[nodiscard]] bool AddFullSceneReflectionResolverV3Pass(
    RenderGraph& graph,
    const FullSceneReflectionResolverV3Context& context);
[[nodiscard]] bool AddFullSceneReflectionHistoryV3Pass(
    RenderGraph& graph,
    const FullSceneReflectionHistoryV3Context& context);
[[nodiscard]] bool AddFullSceneReflectionHistoryV3CopyPass(
    RenderGraph& graph,
    const FullSceneReflectionHistoryV3CopyContext& context);
[[nodiscard]] bool AddCandidateBeautyDisplayPass(RenderGraph& graph,
                                                 const CandidateBeautyDisplayContext& context);

} // namespace Cortex::Graphics::FullSceneShaderV3Passes
