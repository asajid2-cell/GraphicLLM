#include "Renderer.h"
#include "Renderer_FramePhaseGpuScope.h"

#include "Graphics/Passes/MainPassTargetPass.h"
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

constexpr std::array<glm::vec3, LocalReflectionProbeCubemapCaptureState::kFaceCount> kCubemapFaceDirections = {
    glm::vec3(1.0f, 0.0f, 0.0f),
    glm::vec3(-1.0f, 0.0f, 0.0f),
    glm::vec3(0.0f, 1.0f, 0.0f),
    glm::vec3(0.0f, -1.0f, 0.0f),
    glm::vec3(0.0f, 0.0f, 1.0f),
    glm::vec3(0.0f, 0.0f, -1.0f),
};

constexpr std::array<glm::vec3, LocalReflectionProbeCubemapCaptureState::kFaceCount> kCubemapFaceUps = {
    glm::vec3(0.0f, 1.0f, 0.0f),
    glm::vec3(0.0f, 1.0f, 0.0f),
    glm::vec3(0.0f, 0.0f, -1.0f),
    glm::vec3(0.0f, 0.0f, 1.0f),
    glm::vec3(0.0f, 1.0f, 0.0f),
    glm::vec3(0.0f, 1.0f, 0.0f),
};

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
    for (uint32_t face = 0; face < LocalReflectionProbeCubemapCaptureState::kFaceCount; ++face) {
        capture.viewMatrices[face] =
            glm::lookAtLH(capture.captureCenter,
                          capture.captureCenter + kCubemapFaceDirections[face],
                          kCubemapFaceUps[face]);
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

FrameConstants BuildCaptureFrameConstants(const FrameConstants& baseFrame,
                                          const LocalReflectionProbeCubemapCaptureState& capture,
                                          uint32_t face) {
    FrameConstants frameData = baseFrame;
    const glm::mat4 view = capture.viewMatrices[face];
    const glm::mat4 projection = capture.projectionMatrix;
    const glm::mat4 viewProjection = projection * view;

    frameData.viewMatrix = view;
    frameData.projectionMatrix = projection;
    frameData.viewProjectionMatrix = viewProjection;
    frameData.invProjectionMatrix = glm::inverse(projection);
    frameData.cameraPosition = glm::vec4(capture.captureCenter, 1.0f);
    frameData.postParams.x = 1.0f / static_cast<float>(capture.faceSize);
    frameData.postParams.y = 1.0f / static_cast<float>(capture.faceSize);
    frameData.taaParams = glm::vec4(0.0f);
    frameData.viewProjectionNoJitter = viewProjection;
    frameData.invViewProjectionNoJitter = glm::inverse(viewProjection);
    frameData.prevViewProjectionMatrix = viewProjection;
    frameData.invViewProjectionMatrix = glm::inverse(viewProjection);
    frameData.projectionParams = glm::vec4(projection[0][0], projection[1][1], 0.05f, 64.0f);
    return frameData;
}

bool CreateArrayRTVs(ID3D12Device* device,
                     ID3D12Resource* resource,
                     DXGI_FORMAT format,
                     std::array<DescriptorHandle, LocalReflectionProbeCubemapCaptureState::kFaceCount>& rtvs) {
    if (!device || !resource) {
        return false;
    }

    for (uint32_t face = 0; face < LocalReflectionProbeCubemapCaptureState::kFaceCount; ++face) {
        if (!rtvs[face].IsValid()) {
            return false;
        }

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = format;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        rtvDesc.Texture2DArray.MipSlice = 0;
        rtvDesc.Texture2DArray.FirstArraySlice = face;
        rtvDesc.Texture2DArray.ArraySize = 1;
        rtvDesc.Texture2DArray.PlaneSlice = 0;
        device->CreateRenderTargetView(resource, &rtvDesc, rtvs[face].cpu);
    }
    return true;
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
    if (!capture.target || !capture.target->GetResource() ||
        !capture.normalRoughnessTarget || !capture.normalRoughnessTarget->GetResource() ||
        !capture.depthTarget || !capture.depthTarget->GetResource() ||
        !capture.frameConstants) {
        auto target = std::make_shared<DX12Texture>();
        TextureDesc colorDesc{};
        colorDesc.width = capture.faceSize;
        colorDesc.height = capture.faceSize;
        colorDesc.mipLevels = 1;
        colorDesc.arraySize = LocalReflectionProbeCubemapCaptureState::kFaceCount;
        colorDesc.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        colorDesc.flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        colorDesc.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        auto textureResult = target->Initialize(device, colorDesc, "LocalReflectionProbe_CaptureCubemap");
        if (textureResult.IsErr()) {
            fail("cubemap_target_allocation_failed:" + textureResult.Error());
            return;
        }

        auto normalTarget = std::make_shared<DX12Texture>();
        auto normalResult =
            normalTarget->Initialize(device, colorDesc, "LocalReflectionProbe_CaptureNormalRoughness");
        if (normalResult.IsErr()) {
            fail("cubemap_normal_target_allocation_failed:" + normalResult.Error());
            return;
        }

        auto depthTarget = std::make_shared<DX12Texture>();
        TextureDesc depthDesc{};
        depthDesc.width = capture.faceSize;
        depthDesc.height = capture.faceSize;
        depthDesc.mipLevels = 1;
        depthDesc.arraySize = 1;
        depthDesc.format = DXGI_FORMAT_D32_FLOAT;
        depthDesc.flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        depthDesc.initialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

        auto depthResult = depthTarget->Initialize(device, depthDesc, "LocalReflectionProbe_CaptureDepth");
        if (depthResult.IsErr()) {
            fail("cubemap_depth_target_allocation_failed:" + depthResult.Error());
            return;
        }

        auto frameConstants = std::make_unique<ConstantBuffer<FrameConstants>>();
        auto frameConstantsResult =
            frameConstants->Initialize(device, LocalReflectionProbeCubemapCaptureState::kFaceCount);
        if (frameConstantsResult.IsErr()) {
            fail("cubemap_frame_constants_allocation_failed:" + frameConstantsResult.Error());
            return;
        }

        for (uint32_t face = 0; face < LocalReflectionProbeCubemapCaptureState::kFaceCount; ++face) {
            auto rtv = m_services.descriptorManager->AllocateRTV();
            if (rtv.IsErr()) {
                fail("cubemap_face_rtv_allocation_failed:" + rtv.Error());
                return;
            }
            capture.faceRTVs[face] = rtv.Value();

            auto normalRtv = m_services.descriptorManager->AllocateRTV();
            if (normalRtv.IsErr()) {
                fail("cubemap_normal_rtv_allocation_failed:" + normalRtv.Error());
                return;
            }
            capture.normalRoughnessRTVs[face] = normalRtv.Value();
        }

        auto dsv = m_services.descriptorManager->AllocateDSV();
        if (dsv.IsErr()) {
            fail("cubemap_depth_dsv_allocation_failed:" + dsv.Error());
            return;
        }
        capture.depthDSV = dsv.Value();

        capture.target = std::move(target);
        capture.normalRoughnessTarget = std::move(normalTarget);
        capture.depthTarget = std::move(depthTarget);
        capture.frameConstants = std::move(frameConstants);
        capture.allocated = true;
        spdlog::info("Local reflection probe cubemap capture render targets ready: {}x{}x6 RGBA16F",
                     capture.faceSize,
                     capture.faceSize);
    }

    ID3D12Resource* resource = capture.target ? capture.target->GetResource() : nullptr;
    ID3D12Resource* normalResource =
        capture.normalRoughnessTarget ? capture.normalRoughnessTarget->GetResource() : nullptr;
    ID3D12Resource* depthResource = capture.depthTarget ? capture.depthTarget->GetResource() : nullptr;
    if (!resource || !normalResource || !depthResource || !capture.frameConstants) {
        fail("cubemap_resources_missing_after_allocation");
        return;
    }

    if (!CreateArrayRTVs(device, resource, capture.target->GetFormat(), capture.faceRTVs) ||
        !CreateArrayRTVs(device,
                         normalResource,
                         capture.normalRoughnessTarget->GetFormat(),
                         capture.normalRoughnessRTVs)) {
        fail("cubemap_face_rtv_missing");
        return;
    }

    if (!capture.depthDSV.IsValid()) {
        fail("cubemap_depth_dsv_missing");
        return;
    }
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    device->CreateDepthStencilView(depthResource, &dsvDesc, capture.depthDSV.cpu);

    FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "LocalReflectionCubemapCapture", "Environment");

    capture.captureMode = "six_face_scene_raster";
    capture.frameConstants->ResetOffset(m_frameRuntime.frameIndex);
    const FrameConstants savedFrameConstantsCPU = m_constantBuffers.frameCPU;
    const D3D12_GPU_VIRTUAL_ADDRESS savedFrameConstantsGPU = m_constantBuffers.currentFrameGPU;
    const RendererCameraFrameState savedCameraState = m_cameraState;
    D3D12_RESOURCE_STATES captureColorState = capture.target->GetCurrentState();
    D3D12_RESOURCE_STATES captureNormalState = capture.normalRoughnessTarget->GetCurrentState();
    D3D12_RESOURCE_STATES captureDepthState = capture.depthTarget->GetCurrentState();
    uint32_t totalSceneDraws = 0;
    const uint32_t opaqueDrawsBeforeCapture = m_frameDiagnostics.contract.drawCounts.opaqueDraws;

    auto restoreMainFrameState = [&]() {
        m_constantBuffers.frameCPU = savedFrameConstantsCPU;
        m_constantBuffers.currentFrameGPU = savedFrameConstantsGPU;
        m_cameraState = savedCameraState;
    };

    for (uint32_t face = 0; face < LocalReflectionProbeCubemapCaptureState::kFaceCount; ++face) {
        FrameConstants captureFrame =
            BuildCaptureFrameConstants(savedFrameConstantsCPU, capture, face);
        m_constantBuffers.currentFrameGPU = capture.frameConstants->AllocateAndWrite(captureFrame);
        m_constantBuffers.frameCPU = captureFrame;
        m_cameraState.positionWS = capture.captureCenter;
        m_cameraState.forwardWS = kCubemapFaceDirections[face];
        m_cameraState.nearPlane = 0.05f;
        m_cameraState.farPlane = 64.0f;

        MainPassTargetPass::PrepareContext prepareContext{};
        prepareContext.commandList = m_commandResources.graphicsList.Get();
        prepareContext.descriptorManager = m_services.descriptorManager.get();
        prepareContext.rootSignature = m_pipelineState.rootSignature.get();
        prepareContext.geometryPipeline = m_pipelineState.geometry.get();
        prepareContext.depthBuffer = depthResource;
        prepareContext.depthState = &captureDepthState;
        prepareContext.depthDsv = capture.depthDSV;
        prepareContext.hdrColor = resource;
        prepareContext.hdrState = &captureColorState;
        prepareContext.hdrRtv = capture.faceRTVs[face];
        prepareContext.normalRoughness = normalResource;
        prepareContext.normalRoughnessState = &captureNormalState;
        prepareContext.normalRoughnessRtv = capture.normalRoughnessRTVs[face];
        prepareContext.backBufferWidth = capture.faceSize;
        prepareContext.backBufferHeight = capture.faceSize;
        prepareContext.clearColor[0] = 0.025f;
        prepareContext.clearColor[1] = 0.028f;
        prepareContext.clearColor[2] = 0.033f;
        prepareContext.clearColor[3] = 1.0f;
        if (!MainPassTargetPass::Prepare(prepareContext)) {
            restoreMainFrameState();
            capture.target->SetState(captureColorState);
            capture.normalRoughnessTarget->SetState(captureNormalState);
            capture.depthTarget->SetState(captureDepthState);
            fail("cubemap_face_target_prepare_failed");
            FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
            return;
        }

        const uint32_t faceDrawsBefore = m_frameDiagnostics.contract.drawCounts.opaqueDraws;
        RenderScene(frameCtx.registry);
        totalSceneDraws += m_frameDiagnostics.contract.drawCounts.opaqueDraws - faceDrawsBefore;
        ++capture.capturedFaces;
    }
    restoreMainFrameState();
    m_frameDiagnostics.contract.drawCounts.opaqueDraws = opaqueDrawsBeforeCapture;
    FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());

    std::array<D3D12_RESOURCE_BARRIER, 2> toShaderResource{};
    uint32_t barrierCount = 0;
    if (captureColorState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        auto& barrier = toShaderResource[barrierCount++];
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = captureColorState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        captureColorState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    if (captureNormalState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        auto& barrier = toShaderResource[barrierCount++];
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = normalResource;
        barrier.Transition.StateBefore = captureNormalState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        captureNormalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    if (barrierCount > 0) {
        m_commandResources.graphicsList->ResourceBarrier(barrierCount, toShaderResource.data());
    }
    capture.target->SetState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    capture.normalRoughnessTarget->SetState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    capture.depthTarget->SetState(captureDepthState);

    capture.executedThisFrame =
        capture.capturedFaces == LocalReflectionProbeCubemapCaptureState::kFaceCount && totalSceneDraws > 0;
    capture.captureCompleted = capture.executedThisFrame;
    capture.failedThisFrame = !capture.executedThisFrame;
    capture.failureReason = capture.executedThisFrame ? "none" : "cubemap_capture_no_scene_draws";
    RecordFramePass("LocalReflectionCubemapCapture",
                    true,
                    capture.executedThisFrame,
                    totalSceneDraws,
                    {"reflection_probe_volume"},
                    {"local_reflection_cubemap"});
    MarkPassComplete("LocalReflectionCubemapCapture_Done");
    PublishCaptureContract(m_frameDiagnostics.contract.contract.environment, capture);
}

} // namespace Cortex::Graphics
