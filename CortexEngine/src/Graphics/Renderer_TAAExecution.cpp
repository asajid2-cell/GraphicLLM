#include "Renderer.h"
#include "Graphics/RendererGeometryUtils.h"
#include "Passes/TAAPass.h"

#include <span>
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

bool Renderer::SeedTAAHistory(bool skipTransitions) {
    if (!m_mainTargets.hdr.resources.color || !m_temporalScreenState.historyColor) {
        return false;
    }

    if (!skipTransitions &&
        (m_mainTargets.hdr.resources.state != D3D12_RESOURCE_STATE_COPY_SOURCE || m_temporalScreenState.historyState != D3D12_RESOURCE_STATE_COPY_DEST)) {
        D3D12_RESOURCE_BARRIER initBarriers[2] = {};
        UINT initCount = 0;

        if (m_mainTargets.hdr.resources.state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
            initBarriers[initCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            initBarriers[initCount].Transition.pResource = m_mainTargets.hdr.resources.color.Get();
            initBarriers[initCount].Transition.StateBefore = m_mainTargets.hdr.resources.state;
            initBarriers[initCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            initBarriers[initCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++initCount;
        }

        if (m_temporalScreenState.historyState != D3D12_RESOURCE_STATE_COPY_DEST) {
            initBarriers[initCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            initBarriers[initCount].Transition.pResource = m_temporalScreenState.historyColor.Get();
            initBarriers[initCount].Transition.StateBefore = m_temporalScreenState.historyState;
            initBarriers[initCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            initBarriers[initCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++initCount;
        }

        if (initCount > 0) {
            m_commandResources.graphicsList->ResourceBarrier(initCount, initBarriers);
        }
    }

    m_mainTargets.hdr.resources.state = D3D12_RESOURCE_STATE_COPY_SOURCE;
    m_temporalScreenState.historyState = D3D12_RESOURCE_STATE_COPY_DEST;
    m_commandResources.graphicsList->CopyResource(m_temporalScreenState.historyColor.Get(), m_mainTargets.hdr.resources.color.Get());

    if (!skipTransitions) {
        D3D12_RESOURCE_BARRIER postCopy[2] = {};

        postCopy[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        postCopy[0].Transition.pResource = m_mainTargets.hdr.resources.color.Get();
        postCopy[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        postCopy[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        postCopy[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        postCopy[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        postCopy[1].Transition.pResource = m_temporalScreenState.historyColor.Get();
        postCopy[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        postCopy[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        postCopy[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        m_commandResources.graphicsList->ResourceBarrier(2, postCopy);
        m_mainTargets.hdr.resources.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        m_temporalScreenState.historyState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    MarkTAAHistoryValid();
    return true;
}

bool Renderer::ResolveTAAIntermediate(bool skipTransitions) {
    if (!m_temporalAAState.enabled || !m_pipelineState.taa || !m_mainTargets.hdr.resources.color || !m_temporalScreenState.taaIntermediate || !m_services.window) {
        return false;
    }
    if (!m_services.device || !m_services.device->GetDevice() || !m_commandResources.graphicsList) {
        return false;
    }
    if (!m_temporalScreenState.taaResolveSrvTableValid) {
        spdlog::error("RenderTAA: persistent SRV table is invalid");
        return false;
    }

    if (!skipTransitions) {
        D3D12_RESOURCE_BARRIER barriers[6] = {};
        UINT barrierCount = 0;

        if (m_temporalScreenState.taaIntermediateState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[barrierCount].Transition.pResource = m_temporalScreenState.taaIntermediate.Get();
            barriers[barrierCount].Transition.StateBefore = m_temporalScreenState.taaIntermediateState;
            barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++barrierCount;
        }

        if (m_mainTargets.hdr.resources.state != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[barrierCount].Transition.pResource = m_mainTargets.hdr.resources.color.Get();
            barriers[barrierCount].Transition.StateBefore = m_mainTargets.hdr.resources.state;
            barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++barrierCount;
        }

        if (m_depthResources.resources.buffer && m_depthResources.resources.resourceState != kDepthSampleState) {
            barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[barrierCount].Transition.pResource = m_depthResources.resources.buffer.Get();
            barriers[barrierCount].Transition.StateBefore = m_depthResources.resources.resourceState;
            barriers[barrierCount].Transition.StateAfter = kDepthSampleState;
            barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++barrierCount;
        }

        if (m_mainTargets.normalRoughness.resources.texture && m_mainTargets.normalRoughness.resources.state != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[barrierCount].Transition.pResource = m_mainTargets.normalRoughness.resources.texture.Get();
            barriers[barrierCount].Transition.StateBefore = m_mainTargets.normalRoughness.resources.state;
            barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++barrierCount;
        }

        if (m_temporalScreenState.velocityBuffer && m_temporalScreenState.velocityState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[barrierCount].Transition.pResource = m_temporalScreenState.velocityBuffer.Get();
            barriers[barrierCount].Transition.StateBefore = m_temporalScreenState.velocityState;
            barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++barrierCount;
        }

        if (m_temporalScreenState.historyState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[barrierCount].Transition.pResource = m_temporalScreenState.historyColor.Get();
            barriers[barrierCount].Transition.StateBefore = m_temporalScreenState.historyState;
            barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++barrierCount;
        }

        if (m_temporalMaskState.texture &&
            m_temporalMaskState.resourceState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[barrierCount].Transition.pResource = m_temporalMaskState.texture.Get();
            barriers[barrierCount].Transition.StateBefore = m_temporalMaskState.resourceState;
            barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++barrierCount;
        }

        if (barrierCount > 0) {
            m_commandResources.graphicsList->ResourceBarrier(barrierCount, barriers);
        }
    }

    m_temporalScreenState.taaIntermediateState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_mainTargets.hdr.resources.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    if (m_depthResources.resources.buffer) {
        m_depthResources.resources.resourceState = kDepthSampleState;
    }
    if (m_mainTargets.normalRoughness.resources.texture) {
        m_mainTargets.normalRoughness.resources.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    if (m_temporalScreenState.velocityBuffer) {
        m_temporalScreenState.velocityState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    m_temporalScreenState.historyState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    if (m_temporalMaskState.texture) {
        m_temporalMaskState.resourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    UpdateTAAResolveDescriptorTable();
    auto& resolveTable = m_temporalScreenState.taaResolveSrvTables[m_frameRuntime.frameIndex % kFrameCount];
    if (!TAAPass::Resolve({
            m_commandResources.graphicsList.Get(),
            m_services.descriptorManager.get(),
            m_pipelineState.rootSignature.get(),
            m_constantBuffers.currentFrameGPU,
            m_pipelineState.taa.get(),
            m_mainTargets.hdr.resources.color.Get(),
            m_temporalScreenState.taaIntermediateRTV,
            std::span<DescriptorHandle>(resolveTable.data(), resolveTable.size()),
            m_environmentState.shadowAndEnvDescriptors[0],
        })) {
        spdlog::error("RenderTAA: pass resolve failed");
        return false;
    }
    return true;
}

bool Renderer::CopyTAAIntermediateToHDR(bool skipTransitions) {
    if (!m_temporalScreenState.taaIntermediate || !m_mainTargets.hdr.resources.color) {
        return false;
    }

    if (!skipTransitions) {
        D3D12_RESOURCE_BARRIER copyBarriers[2] = {};
        UINT copyCount = 0;

        if (m_temporalScreenState.taaIntermediateState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
            copyBarriers[copyCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            copyBarriers[copyCount].Transition.pResource = m_temporalScreenState.taaIntermediate.Get();
            copyBarriers[copyCount].Transition.StateBefore = m_temporalScreenState.taaIntermediateState;
            copyBarriers[copyCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            copyBarriers[copyCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++copyCount;
        }

        if (m_mainTargets.hdr.resources.state != D3D12_RESOURCE_STATE_COPY_DEST) {
            copyBarriers[copyCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            copyBarriers[copyCount].Transition.pResource = m_mainTargets.hdr.resources.color.Get();
            copyBarriers[copyCount].Transition.StateBefore = m_mainTargets.hdr.resources.state;
            copyBarriers[copyCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            copyBarriers[copyCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++copyCount;
        }

        if (copyCount > 0) {
            m_commandResources.graphicsList->ResourceBarrier(copyCount, copyBarriers);
        }
    }

    m_temporalScreenState.taaIntermediateState = D3D12_RESOURCE_STATE_COPY_SOURCE;
    m_mainTargets.hdr.resources.state = D3D12_RESOURCE_STATE_COPY_DEST;
    m_commandResources.graphicsList->CopyResource(m_mainTargets.hdr.resources.color.Get(), m_temporalScreenState.taaIntermediate.Get());
    return true;
}

bool Renderer::CopyHDRToTAAHistory(bool skipTransitions) {
    if (!m_mainTargets.hdr.resources.color || !m_temporalScreenState.historyColor) {
        return false;
    }

    if (!skipTransitions) {
        D3D12_RESOURCE_BARRIER postTaa[3] = {};
        UINT postCount = 0;

        postTaa[postCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        postTaa[postCount].Transition.pResource = m_temporalScreenState.taaIntermediate.Get();
        postTaa[postCount].Transition.StateBefore = m_temporalScreenState.taaIntermediateState;
        postTaa[postCount].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        postTaa[postCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++postCount;

        postTaa[postCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        postTaa[postCount].Transition.pResource = m_mainTargets.hdr.resources.color.Get();
        postTaa[postCount].Transition.StateBefore = m_mainTargets.hdr.resources.state;
        postTaa[postCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        postTaa[postCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++postCount;

        if (m_temporalScreenState.historyState != D3D12_RESOURCE_STATE_COPY_DEST) {
            postTaa[postCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            postTaa[postCount].Transition.pResource = m_temporalScreenState.historyColor.Get();
            postTaa[postCount].Transition.StateBefore = m_temporalScreenState.historyState;
            postTaa[postCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            postTaa[postCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++postCount;
        }

        m_commandResources.graphicsList->ResourceBarrier(postCount, postTaa);
    }

    if (m_temporalScreenState.taaIntermediate) {
        m_temporalScreenState.taaIntermediateState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    m_mainTargets.hdr.resources.state = D3D12_RESOURCE_STATE_COPY_SOURCE;
    m_temporalScreenState.historyState = D3D12_RESOURCE_STATE_COPY_DEST;
    m_commandResources.graphicsList->CopyResource(m_temporalScreenState.historyColor.Get(), m_mainTargets.hdr.resources.color.Get());

    if (!skipTransitions) {
        D3D12_RESOURCE_BARRIER finalBarriers[2] = {};

        finalBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        finalBarriers[0].Transition.pResource = m_mainTargets.hdr.resources.color.Get();
        finalBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        finalBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        finalBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        finalBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        finalBarriers[1].Transition.pResource = m_temporalScreenState.historyColor.Get();
        finalBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        finalBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        finalBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        m_commandResources.graphicsList->ResourceBarrier(2, finalBarriers);
        m_mainTargets.hdr.resources.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        m_temporalScreenState.historyState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    MarkTAAHistoryValid();
    return true;
}

void Renderer::RenderTAA() {
    if (!m_temporalAAState.enabled || !m_pipelineState.taa || !m_mainTargets.hdr.resources.color || !m_temporalScreenState.taaIntermediate || !m_services.window) {
        // Ensure HDR is in a readable state for subsequent passes even when TAA
        // is disabled so SSR/post-process can still sample it.
        if (m_mainTargets.hdr.resources.color && m_mainTargets.hdr.resources.state != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = m_mainTargets.hdr.resources.color.Get();
            barrier.Transition.StateBefore = m_mainTargets.hdr.resources.state;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            m_commandResources.graphicsList->ResourceBarrier(1, &barrier);
            m_mainTargets.hdr.resources.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
        // History is no longer meaningful once TAA has been disabled.
        InvalidateTAAHistory("feature_disabled");
        return;
    }

    if (!m_services.device || !m_services.device->GetDevice() || !m_commandResources.graphicsList) {
        return;
    }

    if (!m_temporalScreenState.historyColor || !m_temporalScreenState.historySRV.IsValid() ||
        !m_temporalHistory.manager.CanReproject(TemporalHistoryId::TAAColor)) {
        (void)SeedTAAHistory(false);
        return;
    }

    if (!ResolveTAAIntermediate(false)) {
        return;
    }
    if (!CopyTAAIntermediateToHDR(false)) {
        return;
    }
    (void)CopyHDRToTAAHistory(false);
}

} // namespace Cortex::Graphics
