#include "Graphics/FrameContractValidation.h"

#include <cstdlib>
#include <string>
#include <utility>

namespace Cortex::Graphics {

void ValidateFrameContractSnapshot(FrameContract& contract,
                                   const FrameContractValidationContext& context) {
    contract.warnings.clear();

    auto warn = [&](std::string message) {
        contract.warnings.push_back(std::move(message));
    };

    auto resourceByName = [&](const char* name) -> const FrameContract::ResourceInfo* {
        for (const auto& resource : contract.resources) {
            if (resource.name == name) {
                return &resource;
            }
        }
        return nullptr;
    };

    auto historyByName = [&](const char* name) -> const FrameContract::HistoryInfo* {
        for (const auto& history : contract.histories) {
            if (history.name == name) {
                return &history;
            }
        }
        return nullptr;
    };

    auto requireResource = [&](const char* name, bool required, const char* reason) {
        if (!required) {
            return;
        }
        const auto* resource = resourceByName(name);
        if (!resource || !resource->valid) {
            warn(std::string("required_resource_missing:") + name + ":" + reason);
        } else if (!resource->sizeMatchesContract) {
            warn(std::string("required_resource_wrong_size:") + name +
                 ":actual=" + std::to_string(resource->width) + "x" + std::to_string(resource->height) +
                 ":expected=" + std::to_string(resource->expectedWidth) + "x" + std::to_string(resource->expectedHeight));
        }
    };

    if (contract.renderWidth == 0 || contract.renderHeight == 0 ||
        contract.presentationWidth == 0 || contract.presentationHeight == 0) {
        warn("invalid_zero_frame_dimensions");
    }
    if (contract.budget.profileName.empty()) {
        warn("renderer_budget_profile_missing");
    }
    if (contract.budget.maxRenderWidth == 0 || contract.budget.maxRenderHeight == 0) {
        warn("renderer_budget_invalid_render_extent");
    }
    if (contract.budget.targetRenderScale <= 0.0f || contract.budget.targetRenderScale > 1.0f) {
        warn("renderer_budget_invalid_render_scale");
    }
    if (contract.budget.rtResolutionScale <= 0.0f || contract.budget.rtResolutionScale > 1.0f) {
        warn("renderer_budget_invalid_rt_resolution_scale");
    }
    if (contract.rayTracing.maxBLASTotalBytes > contract.budget.rtStructureBudgetBytes &&
        contract.budget.rtStructureBudgetBytes > 0) {
        warn("rt_budget_exceeds_renderer_rt_structure_budget");
    }
    if (contract.assetMemory.textureBudgetExceeded) {
        warn("asset_texture_budget_exceeded");
    }
    if (contract.assetMemory.environmentBudgetExceeded) {
        warn("asset_environment_budget_exceeded");
    }
    if (contract.assetMemory.geometryBudgetExceeded) {
        warn("asset_geometry_budget_exceeded");
    }
    if (contract.assetMemory.rtStructureBudgetExceeded) {
        warn("asset_rt_structure_budget_exceeded");
    }
    if (contract.environment.loaded && contract.environment.active.empty()) {
        warn("environment_loaded_without_active_id");
    }
    if (contract.environment.loaded && !contract.environment.fallback) {
        if (contract.environment.runtimePath.empty()) {
            warn("environment_runtime_path_missing");
        }
        if (contract.environment.activeWidth == 0 || contract.environment.activeHeight == 0) {
            warn("environment_active_extent_missing");
        }
        if (contract.environment.maxRuntimeDimension > 0 &&
            (contract.environment.activeWidth > contract.environment.maxRuntimeDimension ||
             contract.environment.activeHeight > contract.environment.maxRuntimeDimension)) {
            warn("environment_exceeds_manifest_runtime_dimension");
        }
    }
    if (contract.environment.iblLimitEnabled &&
        contract.environment.residentLimit > 0 &&
        contract.environment.residentCount > contract.environment.residentLimit) {
        warn("environment_resident_count_exceeds_limit");
    }
    if (contract.environment.backgroundExposure < 0.0f ||
        contract.environment.backgroundExposure > 4.0f) {
        warn("environment_background_exposure_out_of_range");
    }
    if (contract.environment.backgroundBlur < 0.0f ||
        contract.environment.backgroundBlur > 1.0f) {
        warn("environment_background_blur_out_of_range");
    }

    if (!contract.features.voxelBackendEnabled) {
        requireResource("depth", true, "core_depth_contract");
        requireResource("hdr_color", true, "core_hdr_contract");
        requireResource("gbuffer_normal_roughness", contract.features.ssrEnabled || contract.features.rtReflectionsEnabled,
                        "normal_roughness_sampling");
    }

    if (contract.culling.visibilityBufferRendered) {
        requireResource("vb_gbuffer_albedo", true, "visibility_buffer_albedo");
        requireResource("vb_gbuffer_normal_roughness", true, "visibility_buffer_normal_roughness");
        requireResource("vb_gbuffer_emissive_metallic", true, "visibility_buffer_emissive_metallic");
        requireResource("vb_gbuffer_material_ext0", true, "visibility_buffer_material_ext0");
        requireResource("vb_gbuffer_material_ext1", true, "visibility_buffer_material_ext1");
        requireResource("vb_gbuffer_material_ext2", true, "visibility_buffer_surface_class");
    }

    requireResource("ssao", contract.features.ssaoEnabled, "ssao_active");
    requireResource("ssr_color", contract.features.ssrEnabled, "ssr_active");
    requireResource("velocity", contract.features.taaEnabled, "taa_motion_vectors");
    requireResource("temporal_rejection_mask", contract.features.taaEnabled || contract.rayTracing.denoiserExecuted,
                    "temporal_reprojection_rejection");
    requireResource("temporal_rejection_mask_stats", contract.temporalMask.built,
                    "temporal_rejection_mask_statistics");
    requireResource("taa_history", contract.features.taaEnabled, "taa_history");
    requireResource("taa_intermediate", contract.features.taaEnabled, "taa_resolve");
    requireResource("rt_shadow_mask", contract.features.rayTracingEnabled, "rt_shadows");
    requireResource("rt_shadow_history", contract.features.rayTracingEnabled, "rt_shadow_history");
    requireResource("rt_reflection", contract.features.rayTracingEnabled && contract.features.rtReflectionsEnabled,
                    "rt_reflections");
    requireResource("rt_reflection_history", contract.features.rayTracingEnabled && contract.features.rtReflectionsEnabled,
                    "rt_reflection_history");
    requireResource("rt_gi", contract.features.rayTracingEnabled && contract.features.rtGIEnabled,
                    "rt_gi");
    requireResource("rt_gi_history", contract.features.rayTracingEnabled && contract.features.rtGIEnabled,
                    "rt_gi_history");
    requireResource("shadow_map", contract.features.shadowsEnabled, "shadow_pass");

    if (contract.features.bloomEnabled) {
        requireResource("bloom_a_0", true, "bloom_active");
        requireResource("bloom_b_0", true, "bloom_active");
    }

    for (const auto& resource : contract.resources) {
        if (resource.valid && !resource.sizeMatchesContract) {
            warn(std::string("resource_size_mismatch:") + resource.name +
                 ":actual=" + std::to_string(resource.width) + "x" + std::to_string(resource.height) +
                 ":expected=" + std::to_string(resource.expectedWidth) + "x" + std::to_string(resource.expectedHeight));
        }
    }

    auto validateHistory = [&](const char* name, const char* resourceName, bool featureActive) {
        const auto* history = historyByName(name);
        const auto* resource = resourceByName(resourceName);
        if (!history) {
            warn(std::string("history_missing_from_contract:") + name);
            return;
        }
        if (history->valid && !featureActive) {
            warn(std::string("history_valid_while_feature_inactive:") + name);
        }
        if (history->valid && (!history->resourceValid || !resource || !resource->sizeMatchesContract)) {
            warn(std::string("history_valid_with_invalid_resource:") + name);
        }
        if (history->valid && !history->seeded) {
            warn(std::string("history_valid_without_seed:") + name);
        }
        if (!history->valid && featureActive && history->invalidReason.empty()) {
            warn(std::string("history_invalid_without_reason:") + name);
        }
        if (history->valid && history->rejectionMode.empty()) {
            warn(std::string("history_valid_without_rejection_mode:") + name);
        }
        if (history->valid && history->usesVelocityReprojection && !history->usesDisocclusionRejection) {
            warn(std::string("history_reprojects_without_disocclusion_rejection:") + name);
        }
    };

    validateHistory("taa_color", "taa_history", contract.features.taaEnabled);
    validateHistory("rt_shadow_mask", "rt_shadow_history", contract.features.rayTracingEnabled);
    validateHistory("rt_reflection", "rt_reflection_history", contract.features.rayTracingEnabled && contract.features.rtReflectionsEnabled);
    validateHistory("rt_gi", "rt_gi_history", contract.features.rayTracingEnabled && contract.features.rtGIEnabled);

    if (contract.features.rayTracingEnabled && !contract.features.rayTracingSupported) {
        warn("ray_tracing_enabled_without_support");
    }

    auto warnExecutedWithoutPlan = [&](bool planned, bool executed, const char* name) {
        if (executed && !planned) {
            warn(std::string("feature_executed_without_planning:") + name);
        }
    };
    warnExecutedWithoutPlan(contract.plannedFeatures.rayTracingEnabled,
                            contract.executedFeatures.rayTracingEnabled,
                            "ray_tracing");
    warnExecutedWithoutPlan(contract.plannedFeatures.rtReflectionsEnabled,
                            contract.executedFeatures.rtReflectionsEnabled,
                            "rt_reflections");
    warnExecutedWithoutPlan(contract.plannedFeatures.rtGIEnabled,
                            contract.executedFeatures.rtGIEnabled,
                            "rt_gi");
    warnExecutedWithoutPlan(contract.plannedFeatures.shadowsEnabled,
                            contract.executedFeatures.shadowsEnabled,
                            "shadows");
    warnExecutedWithoutPlan(contract.plannedFeatures.gpuCullingEnabled,
                            contract.executedFeatures.gpuCullingEnabled,
                            "gpu_culling");
    warnExecutedWithoutPlan(contract.plannedFeatures.visibilityBufferEnabled,
                            contract.executedFeatures.visibilityBufferEnabled,
                            "visibility_buffer");
    warnExecutedWithoutPlan(contract.plannedFeatures.taaEnabled,
                            contract.executedFeatures.taaEnabled,
                            "taa");
    warnExecutedWithoutPlan(contract.plannedFeatures.ssrEnabled,
                            contract.executedFeatures.ssrEnabled,
                            "ssr");
    warnExecutedWithoutPlan(contract.plannedFeatures.ssaoEnabled,
                            contract.executedFeatures.ssaoEnabled,
                            "ssao");
    warnExecutedWithoutPlan(contract.plannedFeatures.bloomEnabled,
                            contract.executedFeatures.bloomEnabled,
                            "bloom");
    warnExecutedWithoutPlan(contract.plannedFeatures.fxaaEnabled,
                            contract.executedFeatures.fxaaEnabled,
                            "fxaa");
    warnExecutedWithoutPlan(contract.plannedFeatures.iblEnabled,
                            contract.executedFeatures.iblEnabled,
                            "ibl");
    warnExecutedWithoutPlan(contract.plannedFeatures.fogEnabled,
                            contract.executedFeatures.fogEnabled,
                            "fog");
    warnExecutedWithoutPlan(contract.plannedFeatures.voxelBackendEnabled,
                            contract.executedFeatures.voxelBackendEnabled,
                            "voxel_backend");

    if (contract.lighting.rigId.empty()) {
        warn("lighting_rig_id_missing");
    }
    if (contract.lighting.rigSource.empty()) {
        warn("lighting_rig_source_missing");
    }
    if (contract.lighting.safeRigVariantActive && contract.lighting.rigId == "custom") {
        warn("lighting_safe_variant_active_for_custom_rig");
    }
    if (contract.screenSpace.ssrMaxDistance < 1.0f ||
        contract.screenSpace.ssrMaxDistance > 120.0f ||
        contract.screenSpace.ssrThickness < 0.005f ||
        contract.screenSpace.ssrThickness > 1.0f ||
        contract.screenSpace.ssrStrength < 0.0f ||
        contract.screenSpace.ssrStrength > 1.0f ||
        contract.screenSpace.ssaoRadius < 0.0f ||
        contract.screenSpace.ssaoRadius > 5.0f ||
        contract.screenSpace.ssaoBias < 0.0f ||
        contract.screenSpace.ssaoBias > 0.1f ||
        contract.screenSpace.ssaoIntensity < 0.0f ||
        contract.screenSpace.ssaoIntensity > 5.0f) {
        warn("screen_space_params_out_of_range");
    }

    if (contract.features.rayTracingEnabled &&
        contract.rayTracing.tlasInstances != contract.rayTracing.materialRecords) {
        warn("rt_material_record_mismatch:tlas_instances=" +
             std::to_string(contract.rayTracing.tlasInstances) +
             ":materials=" + std::to_string(contract.rayTracing.materialRecords));
    }
    if (contract.features.rayTracingEnabled &&
        contract.rayTracing.tlasInstances > 0 &&
        contract.rayTracing.materialBufferBytes == 0) {
        warn("rt_material_buffer_missing");
    }
    const uint32_t rtMaterialClassTotal =
        contract.rayTracing.surfaceDefault +
        contract.rayTracing.surfaceGlass +
        contract.rayTracing.surfaceMirror +
        contract.rayTracing.surfacePlastic +
        contract.rayTracing.surfaceMasonry +
        contract.rayTracing.surfaceEmissive +
        contract.rayTracing.surfaceBrushedMetal +
        contract.rayTracing.surfaceWood +
        contract.rayTracing.surfaceWater;
    if (contract.rayTracing.materialRecords > 0 &&
        rtMaterialClassTotal != contract.rayTracing.materialRecords) {
        warn("rt_material_surface_class_count_mismatch:classes=" +
             std::to_string(rtMaterialClassTotal) +
             ":records=" + std::to_string(contract.rayTracing.materialRecords));
    }
    const uint32_t materialClassTotal =
        contract.materials.surfaceDefault +
        contract.materials.surfaceGlass +
        contract.materials.surfaceMirror +
        contract.materials.surfacePlastic +
        contract.materials.surfaceMasonry +
        contract.materials.surfaceEmissive +
        contract.materials.surfaceBrushedMetal +
        contract.materials.surfaceWood +
        contract.materials.surfaceWater;
    if (materialClassTotal != contract.materials.sampled) {
        warn("material_surface_class_count_mismatch:classes=" +
             std::to_string(materialClassTotal) +
             ":sampled=" + std::to_string(contract.materials.sampled));
    }
    auto materialCountExceedsSampled = [&](uint32_t count, const char* name) {
        if (count > contract.materials.sampled) {
            warn(std::string("material_advanced_count_exceeds_sampled:") + name +
                 ":count=" + std::to_string(count) +
                 ":sampled=" + std::to_string(contract.materials.sampled));
        }
    };
    materialCountExceedsSampled(contract.materials.advancedFeatureMaterials,
                                "advanced_feature_materials");
    materialCountExceedsSampled(contract.materials.advancedClearcoat, "advanced_clearcoat");
    materialCountExceedsSampled(contract.materials.advancedTransmission, "advanced_transmission");
    materialCountExceedsSampled(contract.materials.advancedEmissive, "advanced_emissive");
    materialCountExceedsSampled(contract.materials.advancedSpecular, "advanced_specular");
    materialCountExceedsSampled(contract.materials.advancedSheen, "advanced_sheen");
    materialCountExceedsSampled(contract.materials.advancedSubsurface, "advanced_subsurface");
    const uint32_t advancedFeatureSum =
        contract.materials.advancedClearcoat +
        contract.materials.advancedTransmission +
        contract.materials.advancedEmissive +
        contract.materials.advancedSpecular +
        contract.materials.advancedSheen +
        contract.materials.advancedSubsurface;
    if (contract.materials.advancedFeatureMaterials > 0 && advancedFeatureSum == 0) {
        warn("material_advanced_feature_materials_without_feature_counts");
    }
    auto classMismatch = [](uint32_t a, uint32_t b) {
        return static_cast<uint32_t>(std::abs(static_cast<int64_t>(a) - static_cast<int64_t>(b)));
    };
    contract.rayTracing.materialSurfaceParityComparable =
        contract.features.rayTracingEnabled &&
        contract.rayTracing.materialRecords > 0 &&
        contract.rayTracing.materialRecords == contract.materials.sampled;
    contract.rayTracing.materialSurfaceParityMismatches = 0;
    contract.rayTracing.materialSurfaceParityMatches = false;
    if (contract.rayTracing.materialSurfaceParityComparable) {
        contract.rayTracing.materialSurfaceParityMismatches =
            classMismatch(contract.rayTracing.surfaceDefault, contract.materials.surfaceDefault) +
            classMismatch(contract.rayTracing.surfaceGlass, contract.materials.surfaceGlass) +
            classMismatch(contract.rayTracing.surfaceMirror, contract.materials.surfaceMirror) +
            classMismatch(contract.rayTracing.surfacePlastic, contract.materials.surfacePlastic) +
            classMismatch(contract.rayTracing.surfaceMasonry, contract.materials.surfaceMasonry) +
            classMismatch(contract.rayTracing.surfaceEmissive, contract.materials.surfaceEmissive) +
            classMismatch(contract.rayTracing.surfaceBrushedMetal, contract.materials.surfaceBrushedMetal) +
            classMismatch(contract.rayTracing.surfaceWood, contract.materials.surfaceWood) +
            classMismatch(contract.rayTracing.surfaceWater, contract.materials.surfaceWater);
        contract.rayTracing.materialSurfaceParityMatches =
            contract.rayTracing.materialSurfaceParityMismatches == 0;
        if (!contract.rayTracing.materialSurfaceParityMatches) {
            warn("rt_material_surface_parity_mismatch:mismatches=" +
                 std::to_string(contract.rayTracing.materialSurfaceParityMismatches));
        }
    }
    if (contract.features.rayTracingEnabled &&
        contract.rayTracing.tlasCandidates > 0 &&
        contract.rayTracing.tlasInstances == 0) {
        warn("rt_tlas_empty_with_candidates:candidates=" +
             std::to_string(contract.rayTracing.tlasCandidates));
    }
    if (contract.features.rayTracingEnabled &&
        contract.rayTracing.tlasBLASBuildFailed > 0) {
        warn("rt_blas_build_failures:count=" +
             std::to_string(contract.rayTracing.tlasBLASBuildFailed));
    }
    if (contract.features.rayTracingEnabled &&
        contract.rayTracing.tlasBLASTotalBudgetSkipped > 0) {
        warn("rt_blas_total_budget_skipped:count=" +
             std::to_string(contract.rayTracing.tlasBLASTotalBudgetSkipped));
    }
    if (contract.rayTracing.dispatchReflections) {
        if (!contract.rayTracing.reflectionDispatchReady) {
            warn("rt_reflection_dispatch_not_ready:" +
                 contract.rayTracing.reflectionReadinessReason);
        }
        if (contract.rayTracing.reflectionDispatchWidth == 0 ||
            contract.rayTracing.reflectionDispatchHeight == 0) {
            warn("rt_reflection_dispatch_invalid_extent");
        }
    }
    if (context.rtReflectionWrittenThisFrame &&
        !contract.rayTracing.reflectionDispatchReady) {
        warn("rt_reflection_written_without_ready_contract");
    }
    if (context.rtReflectionWrittenThisFrame && !contract.rayTracing.reflectionSignalStatsCaptured) {
        warn("rt_reflection_written_without_signal_stats");
    }
    if (contract.rayTracing.reflectionSignalValid) {
        if (contract.rayTracing.reflectionSignalPixelCount == 0) {
            warn("rt_reflection_signal_valid_without_pixels");
        }
        if (contract.rayTracing.reflectionSignalNonZeroRatio < -0.001f ||
            contract.rayTracing.reflectionSignalNonZeroRatio > 1.001f ||
            contract.rayTracing.reflectionSignalBrightRatio < -0.001f ||
            contract.rayTracing.reflectionSignalBrightRatio > 1.001f ||
            contract.rayTracing.reflectionSignalOutlierRatio < -0.001f ||
            contract.rayTracing.reflectionSignalOutlierRatio > 1.001f) {
            warn("rt_reflection_signal_ratio_out_of_range");
        }
    }
    if (contract.rayTracing.denoiseReflections && context.rtReflectionDenoisedThisFrame) {
        if (contract.rayTracing.reflectionRequestedDenoiseAlpha < 0.02f ||
            contract.rayTracing.reflectionRequestedDenoiseAlpha > 1.0f ||
            contract.rayTracing.reflectionCompositionStrength < 0.0f ||
            contract.rayTracing.reflectionCompositionStrength > 1.0f) {
            warn("rt_reflection_tuning_out_of_range");
        }
        if (!contract.rayTracing.reflectionHistorySignalStatsCaptured) {
            warn("rt_reflection_history_without_signal_stats");
        }
        if (contract.rayTracing.reflectionHistorySignalValid &&
            contract.rayTracing.reflectionHistorySignalPixelCount == 0) {
            warn("rt_reflection_history_signal_valid_without_pixels");
        }
        if (contract.rayTracing.reflectionHistorySignalValid &&
            (contract.rayTracing.reflectionHistorySignalNonZeroRatio < -0.001f ||
             contract.rayTracing.reflectionHistorySignalNonZeroRatio > 1.001f ||
             contract.rayTracing.reflectionHistorySignalBrightRatio < -0.001f ||
             contract.rayTracing.reflectionHistorySignalBrightRatio > 1.001f ||
             contract.rayTracing.reflectionHistorySignalOutlierRatio < -0.001f ||
             contract.rayTracing.reflectionHistorySignalOutlierRatio > 1.001f)) {
            warn("rt_reflection_history_signal_ratio_out_of_range");
        }
    }

    const auto& r = contract.renderables;
    const uint32_t classifiedVisible =
        r.opaqueDepthWriting +
        r.alphaTestedDepthWriting +
        r.doubleSidedOpaqueDepthWriting +
        r.doubleSidedAlphaTestedDepthWriting +
        r.transparentDepthTested +
        r.waterDepthTestedNoWrite +
        r.overlay;
    const uint32_t visibleWithMesh = (r.visible >= r.meshless) ? (r.visible - r.meshless) : 0;
    if (classifiedVisible != visibleWithMesh) {
        warn("renderable_classification_sum_mismatch:classified=" +
             std::to_string(classifiedVisible) +
             ":visible_mesh=" + std::to_string(visibleWithMesh));
    }
    if (r.transparentDepthTested > 0 &&
        (r.transparentDepthTested + r.waterDepthTestedNoWrite + r.overlay) > r.visible) {
        warn("transparent_depth_class_count_invalid");
    }
    const uint32_t depthWriting =
        r.opaqueDepthWriting +
        r.alphaTestedDepthWriting +
        r.doubleSidedOpaqueDepthWriting +
        r.doubleSidedAlphaTestedDepthWriting;
    if (depthWriting > 0 && !context.depthOnlyPipelineReady) {
        warn("depth_contract_missing_pipeline:depth_only");
    }
    if ((r.alphaTestedDepthWriting > 0 || r.doubleSidedAlphaTestedDepthWriting > 0) &&
        !context.alphaDepthOnlyPipelineReady) {
        warn("depth_contract_missing_pipeline:alpha_test_depth_only");
    }
    if (r.doubleSidedOpaqueDepthWriting > 0 && !context.doubleSidedDepthOnlyPipelineReady) {
        warn("depth_contract_missing_pipeline:double_sided_depth_only");
    }
    if (r.doubleSidedAlphaTestedDepthWriting > 0 && !context.doubleSidedAlphaDepthOnlyPipelineReady) {
        warn("depth_contract_missing_pipeline:double_sided_alpha_test_depth_only");
    }
    if (r.transparentDepthTested > 0 && !context.transparentPipelineReady) {
        warn("depth_contract_missing_pipeline:transparent_depth_test_no_write");
    }
    if (r.waterDepthTestedNoWrite > 0 && !context.waterOverlayPipelineReady) {
        warn("depth_contract_missing_pipeline:water_depth_test_no_write");
    }
    if (r.overlay > 0 && !context.overlayPipelineReady) {
        warn("depth_contract_missing_pipeline:overlay_depth_test_no_write");
    }
    if ((r.transparentDepthTested > 0 || r.waterDepthTestedNoWrite > 0 || r.overlay > 0) &&
        !context.readOnlyDepthStencilViewReady) {
        warn("depth_contract_read_only_dsv_unavailable_for_no_write_passes");
    }

    if (contract.culling.statsValid &&
        contract.culling.visible > contract.culling.tested) {
        warn("gpu_culling_visible_exceeds_tested");
    }
    if (contract.culling.visibilityBufferPlanned &&
        !contract.culling.visibilityBufferRendered &&
        !contract.features.voxelBackendEnabled) {
        warn("visibility_buffer_planned_but_not_rendered");
    }
    if (contract.culling.hzbValid && !contract.culling.hzbCaptureValid) {
        warn("hzb_valid_without_capture_contract");
    }
    if ((contract.culling.hzbOcclusionUsedByVisibilityBuffer ||
         contract.culling.hzbOcclusionUsedByGpuCulling) &&
        (!contract.culling.hzbResourceValid || !contract.culling.hzbCaptureValid)) {
        warn("hzb_occlusion_used_without_valid_resource_or_capture");
    }
    if (contract.culling.hzbResourceValid &&
        contract.culling.hzbValid &&
        contract.culling.hzbAgeFrames > 1u) {
        warn("hzb_capture_stale:age_frames=" + std::to_string(contract.culling.hzbAgeFrames));
    }

    const auto& mv = contract.motionVectors;
    if (mv.planned && !mv.executed) {
        warn("motion_vectors_planned_but_not_executed");
    }
    if (mv.executed && contract.culling.visibilityBufferRendered && !mv.visibilityBufferMotion) {
        warn("visibility_buffer_rendered_without_visibility_motion_vectors");
    }
    if (mv.visibilityBufferMotion && mv.cameraOnlyFallback) {
        warn("motion_vectors_conflicting_modes");
    }
    if (mv.visibilityBufferMotion && mv.instanceCount == 0) {
        warn("visibility_motion_vectors_without_instances");
    }
    if (mv.visibilityBufferMotion &&
        mv.previousWorldMatrices + mv.seededPreviousWorldMatrices != mv.instanceCount) {
        warn("motion_vector_previous_transform_count_mismatch:previous=" +
             std::to_string(mv.previousWorldMatrices) +
             ":seeded=" + std::to_string(mv.seededPreviousWorldMatrices) +
             ":instances=" + std::to_string(mv.instanceCount));
    }
    if (mv.visibilityBufferMotion && mv.meshCount == 0) {
        warn("visibility_motion_vectors_without_meshes");
    }

    const auto& temporalMask = contract.temporalMask;
    if (temporalMask.built && !temporalMask.valid) {
        warn("temporal_mask_built_without_statistics");
    }
    if (temporalMask.valid) {
        if (temporalMask.pixelCount == 0) {
            warn("temporal_mask_statistics_zero_pixels");
        }
        auto ratioInvalid = [](float value) {
            return value < -0.001f || value > 1.001f;
        };
        if (ratioInvalid(temporalMask.acceptedRatio) ||
            ratioInvalid(temporalMask.disocclusionRatio) ||
            ratioInvalid(temporalMask.highMotionRatio) ||
            ratioInvalid(temporalMask.outOfBoundsRatio)) {
            warn("temporal_mask_ratio_out_of_range");
        }
        if (temporalMask.built && temporalMask.readbackLatencyFrames > 8u) {
            warn("temporal_mask_statistics_stale:latency_frames=" +
                 std::to_string(temporalMask.readbackLatencyFrames));
        }
    }

    const auto& d = contract.draws;
    if (depthWriting > 0 && d.depthPrepassDraws == 0 && !contract.features.voxelBackendEnabled) {
        warn("depth_writing_renderables_but_no_depth_prepass_draws");
    }
    if (d.depthPrepassDraws > depthWriting) {
        warn("depth_prepass_draws_exceed_depth_writing_classes:draws=" +
             std::to_string(d.depthPrepassDraws) +
             ":depth_writing=" + std::to_string(depthWriting));
    }
    if (contract.features.shadowsEnabled && depthWriting > 0 && d.shadowDraws == 0) {
        warn("shadows_enabled_but_no_shadow_draws");
    }
    if (contract.culling.visibilityBufferRendered && d.visibilityBufferInstances == 0) {
        warn("visibility_buffer_rendered_but_no_instances_submitted");
    }
    if (contract.culling.visibilityBufferRendered && d.visibilityBufferInstances > depthWriting) {
        warn("visibility_buffer_instances_exceed_depth_writing_classes:instances=" +
             std::to_string(d.visibilityBufferInstances) +
             ":depth_writing=" + std::to_string(depthWriting));
    }
    if (!contract.culling.visibilityBufferRendered && d.opaqueDraws > depthWriting) {
        warn("opaque_draws_exceed_depth_writing_classes:draws=" +
             std::to_string(d.opaqueDraws) +
             ":depth_writing=" + std::to_string(depthWriting));
    }
    if (d.waterDraws > r.waterDepthTestedNoWrite) {
        warn("water_draws_exceed_water_classes");
    }
    if (d.transparentDraws > r.transparentDepthTested) {
        warn("transparent_draws_exceed_transparent_classes");
    }
    if (d.overlayDraws > r.overlay) {
        warn("overlay_draws_exceed_overlay_classes");
    }

    auto passExecuted = [&](const char* name) {
        for (const auto& pass : contract.passes) {
            if (pass.name == name && pass.executed) {
                return true;
            }
        }
        return false;
    };

    for (const auto& pass : contract.passes) {
        if (pass.name.empty()) {
            warn("pass_contract_empty_name");
        }
        if (pass.executed && !pass.planned) {
            warn("pass_executed_without_planning:" + pass.name);
        }
        if (pass.fallbackUsed && pass.fallbackReason.empty()) {
            warn("pass_fallback_missing_reason:" + pass.name);
        }
        if (pass.executed && pass.writes.empty()) {
            warn("pass_executed_without_declared_writes:" + pass.name);
        }
        if (pass.executed && pass.resolutionClass.empty()) {
            warn("pass_budget_missing_resolution_class:" + pass.name);
        }
        if (pass.estimatedWriteMB < 0.0) {
            warn("pass_budget_negative_write_footprint:" + pass.name);
        }
        if (pass.readResources.size() != pass.reads.size()) {
            warn("pass_read_state_contract_mismatch:" + pass.name);
        }
        if (pass.writeResources.size() != pass.writes.size()) {
            warn("pass_write_state_contract_mismatch:" + pass.name);
        }
        for (const auto& access : pass.readResources) {
            if (access.name.empty() || access.stateClass.empty()) {
                warn("pass_read_state_contract_empty:" + pass.name);
            }
        }
        for (const auto& access : pass.writeResources) {
            if (access.name.empty() || access.stateClass.empty()) {
                warn("pass_write_state_contract_empty:" + pass.name);
            }
        }
    }

    if (d.depthPrepassDraws > 0 && !passExecuted("DepthPrepass")) {
        warn("depth_prepass_draws_without_pass_record");
    }
    if (d.shadowDraws > 0 && !passExecuted("ShadowPass")) {
        warn("shadow_draws_without_pass_record");
    }
    if (contract.culling.visibilityBufferRendered && !passExecuted("VisibilityBuffer")) {
        warn("visibility_buffer_rendered_without_pass_record");
    }
    if (d.opaqueDraws > 0 && !passExecuted("ForwardSceneFallback")) {
        warn("opaque_draws_without_forward_pass_record");
    }
    if (d.waterDraws > 0 && !passExecuted("Water")) {
        warn("water_draws_without_pass_record");
    }
    if (d.transparentDraws > 0 && !passExecuted("Transparent")) {
        warn("transparent_draws_without_pass_record");
    }
    if (d.overlayDraws > 0 && !passExecuted("Overlays")) {
        warn("overlay_draws_without_pass_record");
    }
    if (d.particleDraws > 0 && !passExecuted("Particles")) {
        warn("particle_draws_without_pass_record");
    }
    if (contract.particles.executed && d.particleDraws == 0) {
        warn("particle_contract_executed_without_draw_count");
    }
    if (contract.particles.submittedInstances != d.particleInstances) {
        warn("particle_contract_instance_count_mismatch");
    }
    if (contract.particles.capped) {
        warn("particle_instances_capped");
    }
    if (contract.particles.instanceMapFailed) {
        warn("particle_instance_map_failed");
    }
    if (contract.water.waveAmplitude < 0.0f ||
        contract.water.waveAmplitude > 2.0f ||
        contract.water.waveLength < 0.1f ||
        contract.water.waveLength > 100.0f ||
        contract.water.waveSpeed < 0.0f ||
        contract.water.waveSpeed > 20.0f ||
        contract.water.secondaryAmplitude < 0.0f ||
        contract.water.secondaryAmplitude > 2.0f ||
        contract.water.steepness < 0.0f ||
        contract.water.steepness > 1.0f ||
        contract.water.roughness < 0.01f ||
        contract.water.roughness > 1.0f ||
        contract.water.fresnelStrength < 0.0f ||
        contract.water.fresnelStrength > 3.0f) {
        warn("water_params_out_of_range");
    }
    if (contract.cinematicPost.bloomThreshold < 0.1f ||
        contract.cinematicPost.bloomThreshold > 10.0f ||
        contract.cinematicPost.bloomSoftKnee < 0.0f ||
        contract.cinematicPost.bloomSoftKnee > 1.0f ||
        contract.cinematicPost.bloomMaxContribution < 0.0f ||
        contract.cinematicPost.bloomMaxContribution > 16.0f ||
        contract.cinematicPost.contrast < 0.5f ||
        contract.cinematicPost.contrast > 1.5f ||
        contract.cinematicPost.saturation < 0.0f ||
        contract.cinematicPost.saturation > 2.0f ||
        contract.cinematicPost.vignette < 0.0f ||
        contract.cinematicPost.vignette > 1.0f ||
        contract.cinematicPost.lensDirt < 0.0f ||
        contract.cinematicPost.lensDirt > 1.0f) {
        warn("cinematic_post_params_out_of_range");
    }
    if (contract.cinematicPost.bloomExecuted && !passExecuted("Bloom")) {
        warn("cinematic_post_bloom_executed_without_pass_record");
    }
    if (contract.cinematicPost.postProcessExecuted &&
        !passExecuted("PostProcess") &&
        !passExecuted("RenderGraphEndFrame")) {
        warn("cinematic_post_postprocess_executed_without_pass_record");
    }
    if (d.debugLineDraws > 0 && !passExecuted("DebugLines")) {
        warn("debug_line_draws_without_pass_record");
    }
    if (contract.culling.hzbValid && !passExecuted("HZB")) {
        warn("hzb_valid_without_pass_record");
    }
    if (context.rtReflectionWrittenThisFrame && !passExecuted("RTReflections")) {
        warn("rt_reflection_written_without_pass_record");
    }
}

} // namespace Cortex::Graphics
