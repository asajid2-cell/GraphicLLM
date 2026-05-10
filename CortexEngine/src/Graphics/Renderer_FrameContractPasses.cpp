#include "Renderer.h"

#include "Graphics/FrameContractResources.h"
#include "Graphics/FrameContractValidation.h"
#include "Graphics/RenderableClassification.h"
#include "Graphics/SurfaceClassification.h"
#include "Scene/Components.h"
#include "Scene/ECS_Registry.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include <glm/geometric.hpp>

namespace Cortex::Graphics {

FrameContract::DescriptorUsage Renderer::CaptureFrameDescriptorUsage() const {
    FrameContract::DescriptorUsage usage{};
    const DescriptorStats stats = GetDescriptorStats();
    usage.rtvUsed = stats.rtvUsed;
    usage.dsvUsed = stats.dsvUsed;
    usage.shaderVisibleUsed = stats.shaderVisibleUsed;
    usage.stagingUsed = stats.stagingUsed;
    if (stats.shaderVisibleUsed > stats.transientStart) {
        usage.transientUsed = stats.shaderVisibleUsed - stats.transientStart;
    }
    return usage;
}

void Renderer::RecordFramePass(const char* name,
                               bool planned,
                               bool executed,
                               uint32_t drawCount,
                               std::initializer_list<const char*> reads,
                               std::initializer_list<const char*> writes,
                               bool fallbackUsed,
                               const char* fallbackReason,
                               bool renderGraphOwned) {
    FrameContract::PassRecord record{};
    record.name = name ? name : "";
    record.planned = planned;
    record.executed = executed;
    record.fallbackUsed = fallbackUsed;
    record.drawCount = drawCount;
    record.fallbackReason = fallbackReason ? fallbackReason : "";

    auto estimateResourceMB = [&](const char* resourceName) -> double {
        if (!resourceName) {
            return 0.0;
        }

        auto resourceBytes = [](ID3D12Resource* resource) -> double {
            if (!resource) {
                return 0.0;
            }
            const D3D12_RESOURCE_DESC desc = resource->GetDesc();
            if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
                return 0.0;
            }
            const double pixels =
                static_cast<double>(desc.Width) * static_cast<double>(std::max<UINT>(1u, desc.Height));
            const double bytes = pixels * static_cast<double>(BytesPerPixelForContract(desc.Format));
            return bytes / (1024.0 * 1024.0);
        };

        const std::string resource = resourceName;
        if (resource == "depth") return resourceBytes(m_depthResources.buffer.Get());
        if (resource == "hdr_color") return resourceBytes(m_mainTargets.hdrColor.Get());
        if (resource == "gbuffer_normal_roughness") return resourceBytes(m_mainTargets.gbufferNormalRoughness.Get());
        if (resource == "ssao") return resourceBytes(m_ssaoResources.texture.Get());
        if (resource == "ssr_color") return resourceBytes(m_ssrResources.color.Get());
        if (resource == "velocity") return resourceBytes(m_temporalScreenState.velocityBuffer.Get());
        if (resource == "temporal_rejection_mask") return resourceBytes(m_temporalMaskState.texture.Get());
        if (resource == "taa_history") return resourceBytes(m_temporalScreenState.historyColor.Get());
        if (resource == "rt_shadow_mask") return resourceBytes(m_rtShadowTargets.mask.Get());
        if (resource == "rt_gi") return resourceBytes(m_rtGITargets.color.Get());
        if (resource == "rt_reflection") return resourceBytes(m_rtReflectionTargets.color.Get());
        if (resource == "shadow_map") return resourceBytes(m_shadowResources.map.Get());
        if (resource == "hzb") return resourceBytes(m_hzbResources.texture.Get());
        if (resource == "back_buffer" && m_services.window) return resourceBytes(m_services.window->GetCurrentBackBuffer());
        if (resource == "bloom") {
            double total = 0.0;
            for (uint32_t level = 0; level < m_bloomResources.activeLevels; ++level) {
                total += resourceBytes(m_bloomResources.texA[level].Get());
                total += resourceBytes(m_bloomResources.texB[level].Get());
            }
            return total;
        }
        return 0.0;
    };

    auto classifyResolution = [](const std::vector<std::string>& writes) {
        for (const auto& write : writes) {
            if (write == "back_buffer") return std::string("presentation_resolution");
            if (write == "shadow_map") return std::string("shadow_map_resolution");
            if (write == "hzb") return std::string("depth_pyramid");
            if (write == "ssao" || write == "rt_gi" || write == "rt_reflection" || write == "bloom") {
                return std::string("reduced_resolution");
            }
            if (write == "depth" || write == "hdr_color" ||
                write == "gbuffer_normal_roughness" || write == "velocity" ||
                write == "temporal_rejection_mask" || write == "taa_history" || write == "ssr_color") {
                return std::string("render_resolution");
            }
        }
        return std::string("non_image_or_external");
    };

    record.reads.reserve(reads.size());
    record.readResources.reserve(reads.size());
    for (const char* read : reads) {
        if (read && *read) {
            record.reads.emplace_back(read);
            record.readResources.push_back({read, ExpectedReadStateClass(read)});
        }
    }
    record.writes.reserve(writes.size());
    record.writeResources.reserve(writes.size());
    for (const char* write : writes) {
        if (write && *write) {
            record.writes.emplace_back(write);
            record.writeResources.push_back({write, ExpectedWriteStateClass(write)});
        }
    }

    for (const auto& write : record.writes) {
        record.estimatedWriteMB += estimateResourceMB(write.c_str());
    }
    record.resolutionClass = classifyResolution(record.writes);
    record.fullScreen =
        record.name == "HZB" ||
        record.name == "MotionVectors" ||
        record.name == "TAA" ||
        record.name == "SSR" ||
        record.name == "SSAO" ||
        record.name == "Bloom" ||
        record.name == "PostProcess" ||
        record.name == "RenderVoxel" ||
        record.name == "RenderGraphEndFrame";
    record.renderGraph = renderGraphOwned || (record.name == "RenderGraphEndFrame");
    record.rayTracing = (record.name.rfind("RT", 0) == 0);
    for (const auto& read : record.reads) {
        if (read.find("history") != std::string::npos || read == "taa_history") {
            record.historyDependent = true;
        }
        if (read.find("rt_") == 0 || read == "acceleration_structures") {
            record.rayTracing = true;
        }
    }
    for (const auto& write : record.writes) {
        if (write.find("history") != std::string::npos || write == "taa_history") {
            record.historyDependent = true;
        }
        if (write.find("rt_") == 0 || write == "acceleration_structures") {
            record.rayTracing = true;
        }
    }

    record.descriptors = CaptureFrameDescriptorUsage();
    record.descriptors.shaderVisibleDelta =
        (record.descriptors.shaderVisibleUsed >= m_frameDiagnostics.contract.lastPassDescriptorUsage.shaderVisibleUsed)
            ? (record.descriptors.shaderVisibleUsed - m_frameDiagnostics.contract.lastPassDescriptorUsage.shaderVisibleUsed)
            : 0u;
    record.descriptors.transientDelta =
        (record.descriptors.transientUsed >= m_frameDiagnostics.contract.lastPassDescriptorUsage.transientUsed)
            ? (record.descriptors.transientUsed - m_frameDiagnostics.contract.lastPassDescriptorUsage.transientUsed)
            : 0u;
    record.descriptors.stagingDelta =
        (record.descriptors.stagingUsed >= m_frameDiagnostics.contract.lastPassDescriptorUsage.stagingUsed)
            ? (record.descriptors.stagingUsed - m_frameDiagnostics.contract.lastPassDescriptorUsage.stagingUsed)
            : 0u;
    m_frameDiagnostics.contract.lastPassDescriptorUsage = record.descriptors;

    if (record.name == "Bloom") {
        m_frameDiagnostics.contract.contract.cinematicPost.bloomPlanned = planned;
        m_frameDiagnostics.contract.contract.cinematicPost.bloomExecuted = executed;
    } else if (record.name == "PostProcess" || record.name == "RenderGraphEndFrame") {
        m_frameDiagnostics.contract.contract.cinematicPost.postProcessPlanned = planned;
        m_frameDiagnostics.contract.contract.cinematicPost.postProcessExecuted = executed;
    }

    m_frameDiagnostics.contract.passRecords.push_back(std::move(record));
}

} // namespace Cortex::Graphics
