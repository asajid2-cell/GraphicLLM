#pragma once

#include "Graphics/RHI/D3D12Includes.h"
#include "Graphics/RHI/DescriptorHeap.h"
#include "Graphics/RHI/DX12Pipeline.h"
#include "Utils/Result.h"

#include <memory>

namespace Cortex::Graphics {

class RTDenoiser {
public:
    enum class Signal : uint8_t {
        Shadow,
        Reflection,
        GI
    };

    struct DispatchDesc {
        Signal signal = Signal::Shadow;
        bool historyValid = false;
        uint32_t width = 0;
        uint32_t height = 0;
        D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;

        DescriptorHandle currentSRV;
        DescriptorHandle historySRV;
        DescriptorHandle depthSRV;
        DescriptorHandle normalRoughnessSRV;
        DescriptorHandle velocitySRV;
        DescriptorHandle temporalMaskSRV;
        DescriptorHandle historyUAV;
        ID3D12Resource* currentResource = nullptr;
        ID3D12Resource* historyResource = nullptr;
        ID3D12Resource* depthResource = nullptr;
        ID3D12Resource* normalRoughnessResource = nullptr;
        ID3D12Resource* velocityResource = nullptr;
        ID3D12Resource* temporalMaskResource = nullptr;
        DescriptorHandle srvTable;
        DescriptorHandle uavTable;
        float accumulationAlpha = -1.0f;
    };

    struct DispatchResult {
        bool executed = false;
        bool seededHistory = false;
        bool usedHistory = false;
        bool usedDepthNormalRejection = false;
        bool usedVelocityReprojection = false;
        bool usedDisocclusionRejection = false;
        float accumulationAlpha = 1.0f;
    };

    Result<void> Initialize(ID3D12Device* device, ID3D12RootSignature* rootSignature);

    [[nodiscard]] bool IsReady() const;
    DispatchResult Dispatch(ID3D12GraphicsCommandList* cmdList,
                            ID3D12Device* device,
                            DescriptorHeapManager* descriptorManager,
                            const DispatchDesc& desc) const;

private:
    [[nodiscard]] ID3D12PipelineState* SelectPipeline(Signal signal, bool historyValid) const;
    [[nodiscard]] float AlphaForSignal(Signal signal, bool historyValid, float overrideAlpha = -1.0f) const;

    ID3D12RootSignature* m_rootSignature = nullptr;
    std::unique_ptr<DX12ComputePipeline> m_shadowSeedPipeline;
    std::unique_ptr<DX12ComputePipeline> m_shadowTemporalPipeline;
    std::unique_ptr<DX12ComputePipeline> m_reflectionSeedPipeline;
    std::unique_ptr<DX12ComputePipeline> m_reflectionTemporalPipeline;
    std::unique_ptr<DX12ComputePipeline> m_giSeedPipeline;
    std::unique_ptr<DX12ComputePipeline> m_giTemporalPipeline;
};

} // namespace Cortex::Graphics
