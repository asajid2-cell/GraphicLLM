#pragma once

#include "Graphics/Passes/FullSceneShaderV3Passes.h"

namespace Cortex::Graphics {

class FullSceneShaderV3GraphBuilder {
public:
    explicit FullSceneShaderV3GraphBuilder(RenderGraph& graph);

    struct DisplayCommon {
        RGResourceHandle backBuffer;
        ID3D12Device* device = nullptr;
        DescriptorHeapManager* descriptorManager = nullptr;
        ID3D12GraphicsCommandList* commandList = nullptr;
        DX12RootSignature* rootSignature = nullptr;
        DX12Pipeline* pipeline = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE backBufferRTV{};
        uint32_t width = 0;
        uint32_t height = 0;
        bool* failed = nullptr;
        const char** stage = nullptr;
    };

    struct DisplaySubmission {
        const char* passName = "FullSceneCandidateBeautyV3Display";
        RGResourceHandle candidate;
        DescriptorHandle candidateSRV;
        bool* ran = nullptr;
    };

    struct SceneLocalEnvironmentCommon {
        ID3D12Device* device = nullptr;
        ID3D12GraphicsCommandList* commandList = nullptr;
        DX12RootSignature* rootSignature = nullptr;
        DX12Pipeline* pipeline = nullptr;
        DescriptorHeapManager* descriptorManager = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        bool* failed = nullptr;
        const char** stage = nullptr;
    };

    struct SceneLocalEnvironmentSubmission {
        RGResourceHandle sceneLocalEnvironment;
        RGResourceHandle ambientLighting;
        RGResourceHandle visibleBackground;
        RGResourceHandle reflectionBackground;
        RGResourceHandle atmosphere;
        std::shared_ptr<DX12Texture> payloadAlbedo;
        std::shared_ptr<DX12Texture> payloadNormal;
        std::shared_ptr<DX12Texture> irradianceProxy;
        std::shared_ptr<DX12Texture> specularProxy;
        std::shared_ptr<DX12Texture> visibleBackgroundProxy;
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 5> outputRTVs{};
        bool* ran = nullptr;
    };

    [[nodiscard]] bool SubmitSceneLocalEnvironment(
        const FullSceneShaderV3Passes::SceneLocalEnvironmentV3Context& context);
    [[nodiscard]] bool SubmitSceneLocalEnvironment(
        const SceneLocalEnvironmentCommon& common,
        const SceneLocalEnvironmentSubmission& submission);
    [[nodiscard]] bool SubmitReflectionResolver(
        const FullSceneShaderV3Passes::FullSceneReflectionResolverV3Context& context);
    [[nodiscard]] bool SubmitReflectionHistory(
        const FullSceneShaderV3Passes::FullSceneReflectionHistoryV3Context& context);
    [[nodiscard]] bool SubmitReflectionHistoryCopy(
        const FullSceneShaderV3Passes::FullSceneReflectionHistoryV3CopyContext& context);
    [[nodiscard]] bool SubmitComposite(
        const FullSceneShaderV3Passes::FullSceneCompositeV3Context& context);
    [[nodiscard]] bool SubmitDisplay(
        const FullSceneShaderV3Passes::CandidateBeautyDisplayContext& context);
    [[nodiscard]] bool SubmitDisplay(const DisplayCommon& common,
                                     const DisplaySubmission& submission);

private:
    RenderGraph& m_graph;
};

} // namespace Cortex::Graphics
