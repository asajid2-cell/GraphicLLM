#pragma once

#include "Graphics/RHI/D3D12Includes.h"
#include "Graphics/RHI/DescriptorHeap.h"
#include "Graphics/RHI/DX12Pipeline.h"
#include "Utils/Result.h"

#include <memory>

namespace Cortex::Graphics {

class RTReflectionSignalStats {
public:
    static constexpr uint32_t kStatsWords = 6;
    static constexpr uint32_t kStatsBytes = kStatsWords * sizeof(uint32_t);

    enum class SignalTarget : uint32_t {
        Raw = 0,
        History = 1
    };

    struct DispatchDesc {
        uint32_t width = 0;
        uint32_t height = 0;
        SignalTarget target = SignalTarget::Raw;
        DescriptorHandle reflectionSRV;
        DescriptorHandle statsUAV;
        ID3D12Resource* reflectionResource = nullptr;
        ID3D12Resource* statsResource = nullptr;
        DescriptorHandle srvTable;
        DescriptorHandle uavTable;
    };

    struct CaptureResources {
        ID3D12GraphicsCommandList* commandList = nullptr;
        ID3D12Resource* reflectionResource = nullptr;
        D3D12_RESOURCE_STATES* reflectionState = nullptr;
        ID3D12Resource* statsResource = nullptr;
        D3D12_RESOURCE_STATES* statsState = nullptr;
        ID3D12Resource* readbackResource = nullptr;
    };

    Result<void> Initialize(ID3D12Device* device, ID3D12RootSignature* rootSignature);
    [[nodiscard]] bool IsReady() const;
    [[nodiscard]] static bool PrepareCaptureResources(const CaptureResources& resources);
    [[nodiscard]] static bool FinalizeCaptureReadback(const CaptureResources& resources);
    [[nodiscard]] bool Dispatch(ID3D12GraphicsCommandList* cmdList,
                                ID3D12Device* device,
                                DescriptorHeapManager* descriptorManager,
                                const DispatchDesc& desc) const;

private:
    ID3D12RootSignature* m_rootSignature = nullptr;
    std::unique_ptr<DX12ComputePipeline> m_clearPipeline;
    std::unique_ptr<DX12ComputePipeline> m_reducePipeline;
    std::unique_ptr<DX12ComputePipeline> m_clearHistoryPipeline;
    std::unique_ptr<DX12ComputePipeline> m_reduceHistoryPipeline;
};

} // namespace Cortex::Graphics
