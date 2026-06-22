#include "Renderer.h"
#include "Renderer_FramePhaseGpuScope.h"

#include "Scene/Components.h"
#include "Scene/ECS_Registry.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {
namespace {

void PublishCaptureContract(FrameContract::EnvironmentInfo& environment,
                            const LocalReflectionProbeCubemapCaptureState& capture) {
    environment.localReflectionCubemapCaptureAllocated = capture.allocated;
    environment.localReflectionCubemapCaptureScheduled = capture.scheduledThisFrame;
    environment.localReflectionCubemapCaptureExecuted = capture.executedThisFrame;
    environment.localReflectionCubemapCaptureFailed = capture.failedThisFrame;
    environment.localReflectionCubemapCaptureFaceSize = capture.faceSize;
    environment.localReflectionCubemapCaptureFaceCount = LocalReflectionProbeCubemapCaptureState::kFaceCount;
    environment.localReflectionCubemapCaptureScheduledProbes = capture.scheduledProbes;
    environment.localReflectionCubemapCaptureCapturedFaces = capture.capturedFaces;
    environment.localReflectionCubemapCaptureCenterX = capture.captureCenter.x;
    environment.localReflectionCubemapCaptureCenterY = capture.captureCenter.y;
    environment.localReflectionCubemapCaptureCenterZ = capture.captureCenter.z;
    environment.localReflectionCubemapCaptureMode = capture.captureMode;
    environment.localReflectionCubemapCaptureFailureReason = capture.failureReason;
}

void BuildCubemapCameraBasis(LocalReflectionProbeCubemapCaptureState& capture) {
    constexpr std::array<glm::vec3, LocalReflectionProbeCubemapCaptureState::kFaceCount> kDirections = {
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(-1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 0.0f, -1.0f),
    };
    constexpr std::array<glm::vec3, LocalReflectionProbeCubemapCaptureState::kFaceCount> kUps = {
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, -1.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
    };

    for (uint32_t face = 0; face < LocalReflectionProbeCubemapCaptureState::kFaceCount; ++face) {
        capture.viewMatrices[face] =
            glm::lookAtLH(capture.captureCenter, capture.captureCenter + kDirections[face], kUps[face]);
    }
    capture.projectionMatrix = glm::perspectiveLH_ZO(glm::radians(90.0f), 1.0f, 0.05f, 64.0f);
}

bool FindFirstReflectionProbe(Scene::ECS_Registry* registry,
                              glm::vec3& center,
                              uint32_t& probeCount) {
    probeCount = 0;
    if (!registry) {
        return false;
    }

    auto probeView = registry->View<Scene::ReflectionProbeComponent, Scene::TransformComponent>();
    for (auto entity : probeView) {
        const auto& probe = probeView.get<Scene::ReflectionProbeComponent>(entity);
        if (probe.enabled == 0u) {
            continue;
        }
        const auto& transform = probeView.get<Scene::TransformComponent>(entity);
        ++probeCount;
        if (probeCount == 1u) {
            center = glm::vec3(transform.worldMatrix[3]);
        }
    }

    return probeCount > 0u;
}

} // namespace

void Renderer::ExecuteLocalReflectionProbeCubemapCapture(const FrameExecutionContext& frameCtx) {
    auto& capture = m_localReflectionRadianceState.cubemapCapture;
    if (capture.captureCompleted) {
        return;
    }

    capture.ResetPerFrame();
    capture.captureMode = "resource_target_prepared_no_scene_raster";

    glm::vec3 probeCenter{0.0f};
    uint32_t probeCount = 0;
    if (!FindFirstReflectionProbe(frameCtx.registry, probeCenter, probeCount)) {
        capture.failureReason = "no_enabled_reflection_probe";
        PublishCaptureContract(m_frameDiagnostics.contract.contract.environment, capture);
        return;
    }

    capture.scheduledThisFrame = true;
    capture.scheduledProbes = 1;
    capture.captureCenter = probeCenter;
    BuildCubemapCameraBasis(capture);

    auto fail = [&](const std::string& reason) {
        capture.failedThisFrame = true;
        capture.failureReason = reason;
        spdlog::warn("Local reflection cubemap capture stage skipped: {}", reason);
        RecordFramePass("LocalReflectionCubemapCapture",
                        true,
                        false,
                        0,
                        {"reflection_probe_volume"},
                        {"local_reflection_cubemap"},
                        true,
                        reason.c_str(),
                        false);
        MarkPassComplete("LocalReflectionCubemapCapture_Skipped");
        PublishCaptureContract(m_frameDiagnostics.contract.contract.environment, capture);
    };

    if (!m_services.device || !m_services.device->GetDevice() || !m_services.descriptorManager) {
        fail("renderer_device_or_descriptors_unavailable");
        return;
    }
    if (!m_commandResources.graphicsList) {
        fail("graphics_command_list_unavailable");
        return;
    }

    ID3D12Device* device = m_services.device->GetDevice();
    if (!capture.target || !capture.target->GetResource()) {
        auto target = std::make_shared<DX12Texture>();
        TextureDesc desc{};
        desc.width = capture.faceSize;
        desc.height = capture.faceSize;
        desc.mipLevels = 1;
        desc.arraySize = LocalReflectionProbeCubemapCaptureState::kFaceCount;
        desc.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        desc.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        auto textureResult = target->Initialize(device, desc, "LocalReflectionProbe_CaptureCubemap");
        if (textureResult.IsErr()) {
            fail("cubemap_target_allocation_failed:" + textureResult.Error());
            return;
        }

        auto rtv = m_services.descriptorManager->AllocateRTV();
        if (rtv.IsErr()) {
            fail("cubemap_face_rtv_allocation_failed:" + rtv.Error());
            return;
        }
        capture.faceRTVs[0] = rtv.Value();

        capture.target = std::move(target);
        capture.allocated = true;
        spdlog::info("Local reflection probe cubemap capture target ready: {}x{}x6 RGBA16F",
                     capture.faceSize,
                     capture.faceSize);
    }

    ID3D12Resource* resource = capture.target ? capture.target->GetResource() : nullptr;
    if (!resource) {
        fail("cubemap_target_missing_after_allocation");
        return;
    }

    D3D12_RESOURCE_BARRIER toRenderTarget{};
    toRenderTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRenderTarget.Transition.pResource = resource;
    toRenderTarget.Transition.StateBefore = capture.target->GetCurrentState();
    toRenderTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toRenderTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    if (toRenderTarget.Transition.StateBefore != toRenderTarget.Transition.StateAfter) {
        m_commandResources.graphicsList->ResourceBarrier(1, &toRenderTarget);
    }

    FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "LocalReflectionCubemapCapture", "Environment");
    constexpr std::array<std::array<float, 4>, LocalReflectionProbeCubemapCaptureState::kFaceCount> kFaceClearColors = {{
        {{0.030f, 0.034f, 0.040f, 1.0f}},
        {{0.026f, 0.030f, 0.036f, 1.0f}},
        {{0.040f, 0.045f, 0.052f, 1.0f}},
        {{0.018f, 0.020f, 0.024f, 1.0f}},
        {{0.032f, 0.036f, 0.043f, 1.0f}},
        {{0.024f, 0.028f, 0.034f, 1.0f}},
    }};
    for (uint32_t face = 0; face < LocalReflectionProbeCubemapCaptureState::kFaceCount; ++face) {
        if (!capture.faceRTVs[0].IsValid()) {
            fail("cubemap_face_rtv_missing");
            FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
            return;
        }

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = capture.target->GetFormat();
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        rtvDesc.Texture2DArray.MipSlice = 0;
        rtvDesc.Texture2DArray.FirstArraySlice = face;
        rtvDesc.Texture2DArray.ArraySize = 1;
        rtvDesc.Texture2DArray.PlaneSlice = 0;
        device->CreateRenderTargetView(resource, &rtvDesc, capture.faceRTVs[0].cpu);

        m_commandResources.graphicsList->ClearRenderTargetView(
            capture.faceRTVs[0].cpu,
            kFaceClearColors[face].data(),
            0,
            nullptr);
        ++capture.capturedFaces;
    }
    FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());

    D3D12_RESOURCE_BARRIER toShaderResource{};
    toShaderResource.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toShaderResource.Transition.pResource = resource;
    toShaderResource.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toShaderResource.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    toShaderResource.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandResources.graphicsList->ResourceBarrier(1, &toShaderResource);
    capture.target->SetState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    capture.executedThisFrame =
        capture.capturedFaces == LocalReflectionProbeCubemapCaptureState::kFaceCount;
    capture.captureCompleted = capture.executedThisFrame;
    capture.failureReason = "none";
    RecordFramePass("LocalReflectionCubemapCapture",
                    true,
                    capture.executedThisFrame,
                    0,
                    {"reflection_probe_volume"},
                    {"local_reflection_cubemap"});
    MarkPassComplete("LocalReflectionCubemapCapture_Done");
    PublishCaptureContract(m_frameDiagnostics.contract.contract.environment, capture);
}

} // namespace Cortex::Graphics
