#pragma once

#include <memory>

#include "RHI/DX12Pipeline.h"

namespace Cortex::Graphics {

struct RendererPipelineReadiness {
    bool rootSignature = false;
    bool computeRootSignature = false;
    bool compactComputeRootSignature = false;

    bool geometry = false;
    bool overlay = false;
    bool transparent = false;

    bool depthOnly = false;
    bool depthOnlyDoubleSided = false;
    bool depthOnlyAlpha = false;
    bool depthOnlyAlphaDoubleSided = false;

    bool shadow = false;
    bool shadowDoubleSided = false;
    bool shadowAlpha = false;
    bool shadowAlphaDoubleSided = false;

    bool postProcess = false;
    bool taa = false;
    bool ssr = false;
    bool ssao = false;
    bool ssaoCompute = false;
    bool hzbInit = false;
    bool hzbDownsample = false;
    bool motionVectors = false;
    bool bloomDownsample = false;
    bool bloomBlurH = false;
    bool bloomBlurV = false;
    bool bloomComposite = false;

    bool skybox = false;
    bool proceduralSky = false;
    bool debugLine = false;
    bool water = false;
    bool waterOverlay = false;
    bool particle = false;
    bool particlePrepareCompute = false;
    bool voxel = false;
};

struct RendererPipelineState {
    std::unique_ptr<DX12RootSignature> rootSignature;
    std::unique_ptr<DX12ComputeRootSignature> computeRootSignature;
    ComPtr<ID3D12RootSignature> singleSrvUavComputeRootSignature;

    std::unique_ptr<DX12Pipeline> geometry;
    std::unique_ptr<DX12Pipeline> overlay;
    std::unique_ptr<DX12Pipeline> transparent;

    std::unique_ptr<DX12Pipeline> depthOnly;
    std::unique_ptr<DX12Pipeline> depthOnlyDoubleSided;
    std::unique_ptr<DX12Pipeline> depthOnlyAlpha;
    std::unique_ptr<DX12Pipeline> depthOnlyAlphaDoubleSided;

    std::unique_ptr<DX12Pipeline> shadow;
    std::unique_ptr<DX12Pipeline> shadowDoubleSided;
    std::unique_ptr<DX12Pipeline> shadowAlpha;
    std::unique_ptr<DX12Pipeline> shadowAlphaDoubleSided;

    std::unique_ptr<DX12Pipeline> postProcess;
    std::unique_ptr<DX12Pipeline> taa;
    std::unique_ptr<DX12Pipeline> ssr;
    std::unique_ptr<DX12Pipeline> ssao;
    std::unique_ptr<DX12ComputePipeline> ssaoCompute;
    std::unique_ptr<DX12ComputePipeline> hzbInit;
    std::unique_ptr<DX12ComputePipeline> hzbDownsample;
    std::unique_ptr<DX12Pipeline> motionVectors;
    std::unique_ptr<DX12Pipeline> bloomDownsample;
    std::unique_ptr<DX12Pipeline> bloomBlurH;
    std::unique_ptr<DX12Pipeline> bloomBlurV;
    std::unique_ptr<DX12Pipeline> bloomComposite;

    std::unique_ptr<DX12Pipeline> skybox;
    std::unique_ptr<DX12Pipeline> proceduralSky;
    std::unique_ptr<DX12Pipeline> debugLine;
    std::unique_ptr<DX12Pipeline> water;
    std::unique_ptr<DX12Pipeline> waterOverlay;
    std::unique_ptr<DX12Pipeline> particle;
    std::unique_ptr<DX12ComputePipeline> particlePrepareCompute;
    std::unique_ptr<DX12Pipeline> voxel;

    [[nodiscard]] RendererPipelineReadiness GetReadiness() const {
        RendererPipelineReadiness readiness{};
        readiness.rootSignature = rootSignature != nullptr;
        readiness.computeRootSignature = computeRootSignature != nullptr;
        readiness.compactComputeRootSignature = singleSrvUavComputeRootSignature != nullptr;
        readiness.geometry = geometry != nullptr;
        readiness.overlay = overlay != nullptr;
        readiness.transparent = transparent != nullptr;
        readiness.depthOnly = depthOnly != nullptr;
        readiness.depthOnlyDoubleSided = depthOnlyDoubleSided != nullptr;
        readiness.depthOnlyAlpha = depthOnlyAlpha != nullptr;
        readiness.depthOnlyAlphaDoubleSided = depthOnlyAlphaDoubleSided != nullptr;
        readiness.shadow = shadow != nullptr;
        readiness.shadowDoubleSided = shadowDoubleSided != nullptr;
        readiness.shadowAlpha = shadowAlpha != nullptr;
        readiness.shadowAlphaDoubleSided = shadowAlphaDoubleSided != nullptr;
        readiness.postProcess = postProcess != nullptr;
        readiness.taa = taa != nullptr;
        readiness.ssr = ssr != nullptr;
        readiness.ssao = ssao != nullptr;
        readiness.ssaoCompute = ssaoCompute != nullptr;
        readiness.hzbInit = hzbInit != nullptr;
        readiness.hzbDownsample = hzbDownsample != nullptr;
        readiness.motionVectors = motionVectors != nullptr;
        readiness.bloomDownsample = bloomDownsample != nullptr;
        readiness.bloomBlurH = bloomBlurH != nullptr;
        readiness.bloomBlurV = bloomBlurV != nullptr;
        readiness.bloomComposite = bloomComposite != nullptr;
        readiness.skybox = skybox != nullptr;
        readiness.proceduralSky = proceduralSky != nullptr;
        readiness.debugLine = debugLine != nullptr;
        readiness.water = water != nullptr;
        readiness.waterOverlay = waterOverlay != nullptr;
        readiness.particle = particle != nullptr;
        readiness.particlePrepareCompute = particlePrepareCompute != nullptr;
        readiness.voxel = voxel != nullptr;
        return readiness;
    }

    void Reset() {
        geometry.reset();
        overlay.reset();
        transparent.reset();
        depthOnly.reset();
        depthOnlyDoubleSided.reset();
        depthOnlyAlpha.reset();
        depthOnlyAlphaDoubleSided.reset();
        shadow.reset();
        shadowDoubleSided.reset();
        shadowAlpha.reset();
        shadowAlphaDoubleSided.reset();
        postProcess.reset();
        taa.reset();
        ssr.reset();
        ssao.reset();
        ssaoCompute.reset();
        hzbInit.reset();
        hzbDownsample.reset();
        motionVectors.reset();
        bloomDownsample.reset();
        bloomBlurH.reset();
        bloomBlurV.reset();
        bloomComposite.reset();
        skybox.reset();
        proceduralSky.reset();
        debugLine.reset();
        water.reset();
        waterOverlay.reset();
        particle.reset();
        particlePrepareCompute.reset();
        voxel.reset();
        singleSrvUavComputeRootSignature.Reset();
        computeRootSignature.reset();
        rootSignature.reset();
    }
};

} // namespace Cortex::Graphics
