#include "Graphics/FrameContractJson.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <string>

namespace Cortex::Graphics {

namespace {
constexpr double kToMB = 1.0 / (1024.0 * 1024.0);
using nlohmann::json;

json FeatureFlagsToJson(const FrameContract::FeatureFlags& features) {
    return {
        {"ray_tracing_supported", features.rayTracingSupported},
        {"ray_tracing_enabled", features.rayTracingEnabled},
        {"rt_reflections_enabled", features.rtReflectionsEnabled},
        {"rt_gi_enabled", features.rtGIEnabled},
        {"shadows_enabled", features.shadowsEnabled},
        {"gpu_culling_enabled", features.gpuCullingEnabled},
        {"visibility_buffer_enabled", features.visibilityBufferEnabled},
        {"taa_enabled", features.taaEnabled},
        {"ssr_enabled", features.ssrEnabled},
        {"ssao_enabled", features.ssaoEnabled},
        {"bloom_enabled", features.bloomEnabled},
        {"fxaa_enabled", features.fxaaEnabled},
        {"ibl_enabled", features.iblEnabled},
        {"fog_enabled", features.fogEnabled},
        {"particles_enabled", features.particlesEnabled},
        {"voxel_backend_enabled", features.voxelBackendEnabled}
    };
}
}

json FrameContractToJson(const FrameContract& contract) {
    const uint32_t contractDepthWriting =
        contract.renderables.opaqueDepthWriting +
        contract.renderables.alphaTestedDepthWriting +
        contract.renderables.doubleSidedOpaqueDepthWriting +
        contract.renderables.doubleSidedAlphaTestedDepthWriting;
    const uint32_t contractDepthTestedNoWrite =
        contract.renderables.transparentDepthTested +
        contract.renderables.waterDepthTestedNoWrite +
        contract.renderables.overlay;

    json result = {
        {"absolute_frame", contract.absoluteFrame},
        {"swapchain_frame_index", contract.swapchainFrameIndex},
        {"render_resolution", {
            {"width", contract.renderWidth},
            {"height", contract.renderHeight}
        }},
        {"presentation_resolution", {
            {"width", contract.presentationWidth},
            {"height", contract.presentationHeight}
        }},
        {"startup", {
            {"preflight_ran", contract.startup.preflightRan},
            {"preflight_passed", contract.startup.preflightPassed},
            {"safe_mode", contract.startup.safeMode},
            {"dxr_requested", contract.startup.dxrRequested},
            {"environment_manifest_present", contract.startup.environmentManifestPresent},
            {"environment_fallback_available", contract.startup.environmentFallbackAvailable},
            {"issue_count", contract.startup.issueCount},
            {"warning_count", contract.startup.warningCount},
            {"error_count", contract.startup.errorCount},
            {"config_profile", contract.startup.configProfile},
            {"working_directory", contract.startup.workingDirectory}
        }},
        {"health", {
            {"adapter_name", contract.health.adapterName},
            {"quality_preset", contract.health.qualityPreset},
            {"ray_tracing_requested", contract.health.rayTracingRequested},
            {"ray_tracing_effective", contract.health.rayTracingEffective},
            {"environment_loaded", contract.health.environmentLoaded},
            {"environment_fallback", contract.health.environmentFallback},
            {"frame_warnings", contract.health.frameWarnings},
            {"asset_fallbacks", contract.health.assetFallbacks},
            {"descriptor_persistent_used", contract.health.descriptorPersistentUsed},
            {"descriptor_persistent_budget", contract.health.descriptorPersistentBudget},
            {"descriptor_transient_used", contract.health.descriptorTransientUsed},
            {"descriptor_transient_budget", contract.health.descriptorTransientBudget},
            {"estimated_vram_bytes", contract.health.estimatedVRAMBytes},
            {"last_warning_code", contract.health.lastWarningCode},
            {"last_warning_message", contract.health.lastWarningMessage}
        }},
        {"environment", {
            {"active", contract.environment.active},
            {"requested", contract.environment.requested},
            {"runtime_path", contract.environment.runtimePath},
            {"budget_class", contract.environment.budgetClass},
            {"loaded", contract.environment.loaded},
            {"fallback", contract.environment.fallback},
            {"fallback_reason", contract.environment.fallbackReason},
            {"manifest_present", contract.environment.manifestPresent},
            {"ibl_limit_enabled", contract.environment.iblLimitEnabled},
            {"background_visible", contract.environment.backgroundVisible},
            {"background_exposure", contract.environment.backgroundExposure},
            {"background_blur", contract.environment.backgroundBlur},
            {"resident_count", contract.environment.residentCount},
            {"pending_count", contract.environment.pendingCount},
            {"resident_limit", contract.environment.residentLimit},
            {"active_width", contract.environment.activeWidth},
            {"active_height", contract.environment.activeHeight},
            {"max_runtime_dimension", contract.environment.maxRuntimeDimension},
            {"resident_bytes", contract.environment.residentBytes}
        }},
        {"graphics_preset", {
            {"id", contract.graphicsPreset.id},
            {"schema", contract.graphicsPreset.schema},
            {"dirty_from_ui", contract.graphicsPreset.dirtyFromUI},
            {"render_scale", contract.graphicsPreset.renderScale}
        }},
        {"features", FeatureFlagsToJson(contract.features)},
        {"planned_features", FeatureFlagsToJson(contract.plannedFeatures)},
        {"executed_features", FeatureFlagsToJson(contract.executedFeatures)},
        {"renderables", {
            {"total", contract.renderables.total},
            {"visible", contract.renderables.visible},
            {"invisible", contract.renderables.invisible},
            {"meshless", contract.renderables.meshless},
            {"opaque_depth_writing", contract.renderables.opaqueDepthWriting},
            {"alpha_tested_depth_writing", contract.renderables.alphaTestedDepthWriting},
            {"double_sided_opaque_depth_writing", contract.renderables.doubleSidedOpaqueDepthWriting},
            {"double_sided_alpha_tested_depth_writing", contract.renderables.doubleSidedAlphaTestedDepthWriting},
            {"transparent_depth_tested", contract.renderables.transparentDepthTested},
            {"water_depth_tested_no_write", contract.renderables.waterDepthTestedNoWrite},
            {"overlay", contract.renderables.overlay},
            {"emissive", contract.renderables.emissive},
            {"metallic", contract.renderables.metallic},
            {"transmissive", contract.renderables.transmissive},
            {"clearcoat", contract.renderables.clearcoat}
        }},
        {"materials", {
            {"sampled", contract.materials.sampled},
            {"min_albedo_luminance", contract.materials.minAlbedoLuminance},
            {"max_albedo_luminance", contract.materials.maxAlbedoLuminance},
            {"avg_albedo_luminance", contract.materials.avgAlbedoLuminance},
            {"min_roughness", contract.materials.minRoughness},
            {"max_roughness", contract.materials.maxRoughness},
            {"avg_roughness", contract.materials.avgRoughness},
            {"min_metallic", contract.materials.minMetallic},
            {"max_metallic", contract.materials.maxMetallic},
            {"avg_metallic", contract.materials.avgMetallic},
            {"very_dark_albedo", contract.materials.veryDarkAlbedo},
            {"very_bright_albedo", contract.materials.veryBrightAlbedo},
            {"roughness_out_of_range", contract.materials.roughnessOutOfRange},
            {"metallic_out_of_range", contract.materials.metallicOutOfRange},
            {"alpha_blend", contract.materials.alphaBlend},
            {"alpha_mask", contract.materials.alphaMask},
            {"preset_named", contract.materials.presetNamed},
            {"preset_default_metallic", contract.materials.presetDefaultMetallic},
            {"preset_default_roughness", contract.materials.presetDefaultRoughness},
            {"preset_default_transmission", contract.materials.presetDefaultTransmission},
            {"preset_default_emission", contract.materials.presetDefaultEmission},
            {"resolved_metallic", contract.materials.resolvedMetallic},
            {"resolved_conductor", contract.materials.resolvedConductor},
            {"resolved_transmissive", contract.materials.resolvedTransmissive},
            {"resolved_emissive", contract.materials.resolvedEmissive},
            {"resolved_clearcoat", contract.materials.resolvedClearcoat},
            {"advanced_feature_materials", contract.materials.advancedFeatureMaterials},
            {"advanced_clearcoat", contract.materials.advancedClearcoat},
            {"advanced_transmission", contract.materials.advancedTransmission},
            {"advanced_emissive", contract.materials.advancedEmissive},
            {"advanced_specular", contract.materials.advancedSpecular},
            {"advanced_sheen", contract.materials.advancedSheen},
            {"advanced_subsurface", contract.materials.advancedSubsurface},
            {"surface_default", contract.materials.surfaceDefault},
            {"surface_glass", contract.materials.surfaceGlass},
            {"surface_mirror", contract.materials.surfaceMirror},
            {"surface_plastic", contract.materials.surfacePlastic},
            {"surface_masonry", contract.materials.surfaceMasonry},
            {"surface_emissive", contract.materials.surfaceEmissive},
            {"surface_brushed_metal", contract.materials.surfaceBrushedMetal},
            {"surface_wood", contract.materials.surfaceWood},
            {"surface_water", contract.materials.surfaceWater},
            {"reflection_eligible", contract.materials.reflectionEligible},
            {"reflection_high_ceiling", contract.materials.reflectionHighCeiling},
            {"reflection_mirror", contract.materials.reflectionMirror},
            {"reflection_conductor", contract.materials.reflectionConductor},
            {"reflection_transmissive", contract.materials.reflectionTransmissive},
            {"reflection_water", contract.materials.reflectionWater},
            {"max_reflection_ceiling_estimate", contract.materials.maxReflectionCeilingEstimate},
            {"avg_reflection_ceiling_estimate", contract.materials.avgReflectionCeilingEstimate},
            {"validation_issues", contract.materials.validationIssues},
            {"validation_warnings", contract.materials.validationWarnings},
            {"validation_errors", contract.materials.validationErrors},
            {"blend_transmission", contract.materials.blendTransmission},
            {"metallic_transmission", contract.materials.metallicTransmission},
            {"low_roughness_normal", contract.materials.lowRoughnessNormal}
        }},
        {"lighting", {
            {"rig_id", contract.lighting.rigId},
            {"rig_source", contract.lighting.rigSource},
            {"safe_rig_on_low_vram", contract.lighting.safeRigOnLowVRAM},
            {"safe_rig_variant_active", contract.lighting.safeRigVariantActive},
            {"exposure", contract.lighting.exposure},
            {"sun_intensity", contract.lighting.sunIntensity},
            {"ibl_diffuse_intensity", contract.lighting.iblDiffuseIntensity},
            {"ibl_specular_intensity", contract.lighting.iblSpecularIntensity},
            {"bloom_intensity", contract.lighting.bloomIntensity},
            {"ssao_radius", contract.lighting.ssaoRadius},
            {"ssao_bias", contract.lighting.ssaoBias},
            {"ssao_intensity", contract.lighting.ssaoIntensity},
            {"fog_density", contract.lighting.fogDensity},
            {"fog_height", contract.lighting.fogHeight},
            {"fog_falloff", contract.lighting.fogFalloff},
            {"god_ray_intensity", contract.lighting.godRayIntensity},
            {"shadow_bias", contract.lighting.shadowBias},
            {"shadow_pcf_radius", contract.lighting.shadowPCFRadius},
            {"light_count", contract.lighting.lightCount},
            {"shadow_casting_light_count", contract.lighting.shadowCastingLightCount},
            {"total_light_intensity", contract.lighting.totalLightIntensity},
            {"max_light_intensity", contract.lighting.maxLightIntensity}
        }},
        {"culling", {
            {"gpu_culling_enabled", contract.culling.gpuCullingEnabled},
            {"culling_frozen", contract.culling.cullingFrozen},
            {"stats_valid", contract.culling.statsValid},
            {"tested", contract.culling.tested},
            {"frustum_culled", contract.culling.frustumCulled},
            {"occluded", contract.culling.occluded},
            {"visible", contract.culling.visible},
            {"visibility_buffer_planned", contract.culling.visibilityBufferPlanned},
            {"visibility_buffer_rendered", contract.culling.visibilityBufferRendered},
            {"hzb_resource_valid", contract.culling.hzbResourceValid},
            {"hzb_valid", contract.culling.hzbValid},
            {"hzb_capture_valid", contract.culling.hzbCaptureValid},
            {"hzb_occlusion_used_by_visibility_buffer", contract.culling.hzbOcclusionUsedByVisibilityBuffer},
            {"hzb_occlusion_used_by_gpu_culling", contract.culling.hzbOcclusionUsedByGpuCulling},
            {"hzb_width", contract.culling.hzbWidth},
            {"hzb_height", contract.culling.hzbHeight},
            {"hzb_mip_count", contract.culling.hzbMipCount},
            {"hzb_capture_frame", contract.culling.hzbCaptureFrame},
            {"hzb_age_frames", contract.culling.hzbAgeFrames}
        }},
        {"particles", {
            {"enabled", contract.particles.enabled},
            {"planned", contract.particles.planned},
            {"executed", contract.particles.executed},
            {"instance_map_failed", contract.particles.instanceMapFailed},
            {"capped", contract.particles.capped},
            {"density_scale", contract.particles.densityScale},
            {"emitter_count", contract.particles.emitterCount},
            {"live_particles", contract.particles.liveParticles},
            {"submitted_instances", contract.particles.submittedInstances},
            {"frustum_culled", contract.particles.frustumCulled},
            {"max_instances", contract.particles.maxInstances},
            {"instance_capacity", contract.particles.instanceCapacity},
            {"instance_buffer_bytes", contract.particles.instanceBufferBytes}
        }},
        {"cinematic_post", {
            {"enabled", contract.cinematicPost.enabled},
            {"post_process_planned", contract.cinematicPost.postProcessPlanned},
            {"post_process_executed", contract.cinematicPost.postProcessExecuted},
            {"bloom_planned", contract.cinematicPost.bloomPlanned},
            {"bloom_executed", contract.cinematicPost.bloomExecuted},
            {"bloom_intensity", contract.cinematicPost.bloomIntensity},
            {"bloom_threshold", contract.cinematicPost.bloomThreshold},
            {"bloom_soft_knee", contract.cinematicPost.bloomSoftKnee},
            {"bloom_max_contribution", contract.cinematicPost.bloomMaxContribution},
            {"vignette", contract.cinematicPost.vignette},
            {"lens_dirt", contract.cinematicPost.lensDirt},
            {"warm", contract.cinematicPost.warm},
            {"cool", contract.cinematicPost.cool},
            {"god_ray_intensity", contract.cinematicPost.godRayIntensity}
        }},
        {"motion_vectors", {
            {"planned", contract.motionVectors.planned},
            {"executed", contract.motionVectors.executed},
            {"visibility_buffer_motion", contract.motionVectors.visibilityBufferMotion},
            {"camera_only_fallback", contract.motionVectors.cameraOnlyFallback},
            {"previous_transform_history_reset", contract.motionVectors.previousTransformHistoryReset},
            {"instance_count", contract.motionVectors.instanceCount},
            {"mesh_count", contract.motionVectors.meshCount},
            {"previous_world_matrices", contract.motionVectors.previousWorldMatrices},
            {"seeded_previous_world_matrices", contract.motionVectors.seededPreviousWorldMatrices},
            {"pruned_previous_world_matrices", contract.motionVectors.prunedPreviousWorldMatrices},
            {"max_object_motion_world", contract.motionVectors.maxObjectMotionWorld}
        }},
        {"temporal_mask", {
            {"valid", contract.temporalMask.valid},
            {"built", contract.temporalMask.built},
            {"sample_frame", contract.temporalMask.sampleFrame},
            {"pixel_count", contract.temporalMask.pixelCount},
            {"accepted_ratio", contract.temporalMask.acceptedRatio},
            {"disocclusion_ratio", contract.temporalMask.disocclusionRatio},
            {"high_motion_ratio", contract.temporalMask.highMotionRatio},
            {"out_of_bounds_ratio", contract.temporalMask.outOfBoundsRatio},
            {"readback_latency_frames", contract.temporalMask.readbackLatencyFrames}
        }},
        {"renderer_budget", {
            {"profile", contract.budget.profileName},
            {"forced", contract.budget.forced},
            {"dedicated_video_memory_bytes", contract.budget.dedicatedVideoMemoryBytes},
            {"target_render_scale", contract.budget.targetRenderScale},
            {"max_render_width", contract.budget.maxRenderWidth},
            {"max_render_height", contract.budget.maxRenderHeight},
            {"ssao_divisor", contract.budget.ssaoDivisor},
            {"shadow_map_size", contract.budget.shadowMapSize},
            {"bloom_levels", contract.budget.bloomLevels},
            {"ibl_resident_environment_limit", contract.budget.iblResidentEnvironmentLimit},
            {"material_texture_max_dimension", contract.budget.materialTextureMaxDimension},
            {"material_texture_budget_floor_dimension", contract.budget.materialTextureBudgetFloorDimension},
            {"texture_budget_bytes", contract.budget.textureBudgetBytes},
            {"environment_budget_bytes", contract.budget.environmentBudgetBytes},
            {"geometry_budget_bytes", contract.budget.geometryBudgetBytes},
            {"rt_structure_budget_bytes", contract.budget.rtStructureBudgetBytes},
            {"rt_resolution_scale", contract.budget.rtResolutionScale},
            {"reflection_update_cadence", contract.budget.reflectionUpdateCadence},
            {"gi_update_cadence", contract.budget.giUpdateCadence}
        }},
        {"asset_memory", {
            {"texture_bytes", contract.assetMemory.textureBytes},
            {"environment_bytes", contract.assetMemory.environmentBytes},
            {"geometry_bytes", contract.assetMemory.geometryBytes},
            {"rt_structure_bytes", contract.assetMemory.rtStructureBytes},
            {"texture_budget_exceeded", contract.assetMemory.textureBudgetExceeded},
            {"environment_budget_exceeded", contract.assetMemory.environmentBudgetExceeded},
            {"geometry_budget_exceeded", contract.assetMemory.geometryBudgetExceeded},
            {"rt_structure_budget_exceeded", contract.assetMemory.rtStructureBudgetExceeded}
        }},
        {"ray_tracing", {
            {"supported", contract.rayTracing.supported},
            {"enabled", contract.rayTracing.enabled},
            {"warming_up", contract.rayTracing.warmingUp},
            {"scheduler_enabled", contract.rayTracing.schedulerEnabled},
            {"scheduler_build_tlas", contract.rayTracing.schedulerBuildTLAS},
            {"dispatch_shadows", contract.rayTracing.dispatchShadows},
            {"dispatch_reflections", contract.rayTracing.dispatchReflections},
            {"dispatch_gi", contract.rayTracing.dispatchGI},
            {"denoise_shadows", contract.rayTracing.denoiseShadows},
            {"denoise_reflections", contract.rayTracing.denoiseReflections},
            {"denoise_gi", contract.rayTracing.denoiseGI},
            {"denoiser_executed", contract.rayTracing.denoiserExecuted},
            {"denoiser_passes", contract.rayTracing.denoiserPasses},
            {"denoiser_uses_depth_normal_rejection", contract.rayTracing.denoiserUsesDepthNormalRejection},
            {"denoiser_uses_velocity_reprojection", contract.rayTracing.denoiserUsesVelocityReprojection},
            {"denoiser_uses_disocclusion_rejection", contract.rayTracing.denoiserUsesDisocclusionRejection},
            {"shadow_denoise_alpha", contract.rayTracing.shadowDenoiseAlpha},
            {"reflection_denoise_alpha", contract.rayTracing.reflectionDenoiseAlpha},
            {"gi_denoise_alpha", contract.rayTracing.giDenoiseAlpha},
            {"budget_profile", contract.rayTracing.budgetProfile},
            {"scheduler_disabled_reason", contract.rayTracing.schedulerDisabledReason},
            {"scheduler_tlas_candidates", contract.rayTracing.schedulerTLASCandidates},
            {"scheduler_max_tlas_instances", contract.rayTracing.schedulerMaxTLASInstances},
            {"reflection_dispatch_ready", contract.rayTracing.reflectionDispatchReady},
            {"reflection_has_pipeline", contract.rayTracing.reflectionHasPipeline},
            {"reflection_has_tlas", contract.rayTracing.reflectionHasTLAS},
            {"reflection_has_material_buffer", contract.rayTracing.reflectionHasMaterialBuffer},
            {"reflection_has_output", contract.rayTracing.reflectionHasOutput},
            {"reflection_has_depth", contract.rayTracing.reflectionHasDepth},
            {"reflection_has_normal_roughness", contract.rayTracing.reflectionHasNormalRoughness},
            {"reflection_has_material_ext2", contract.rayTracing.reflectionHasMaterialExt2},
            {"reflection_has_environment_table", contract.rayTracing.reflectionHasEnvironmentTable},
            {"reflection_has_frame_constants", contract.rayTracing.reflectionHasFrameConstants},
            {"reflection_has_dispatch_descriptors", contract.rayTracing.reflectionHasDispatchDescriptors},
            {"reflection_dispatch_width", contract.rayTracing.reflectionDispatchWidth},
            {"reflection_dispatch_height", contract.rayTracing.reflectionDispatchHeight},
            {"reflection_readiness_reason", contract.rayTracing.reflectionReadinessReason},
            {"reflection_signal_stats_captured", contract.rayTracing.reflectionSignalStatsCaptured},
            {"reflection_signal_valid", contract.rayTracing.reflectionSignalValid},
            {"reflection_signal_sample_frame", contract.rayTracing.reflectionSignalSampleFrame},
            {"reflection_signal_pixel_count", contract.rayTracing.reflectionSignalPixelCount},
            {"reflection_signal_avg_luma", contract.rayTracing.reflectionSignalAvgLuma},
            {"reflection_signal_max_luma", contract.rayTracing.reflectionSignalMaxLuma},
            {"reflection_signal_nonzero_ratio", contract.rayTracing.reflectionSignalNonZeroRatio},
            {"reflection_signal_bright_ratio", contract.rayTracing.reflectionSignalBrightRatio},
            {"reflection_signal_outlier_ratio", contract.rayTracing.reflectionSignalOutlierRatio},
            {"reflection_signal_readback_latency_frames", contract.rayTracing.reflectionSignalReadbackLatencyFrames},
            {"reflection_history_signal_stats_captured", contract.rayTracing.reflectionHistorySignalStatsCaptured},
            {"reflection_history_signal_valid", contract.rayTracing.reflectionHistorySignalValid},
            {"reflection_history_signal_sample_frame", contract.rayTracing.reflectionHistorySignalSampleFrame},
            {"reflection_history_signal_pixel_count", contract.rayTracing.reflectionHistorySignalPixelCount},
            {"reflection_history_signal_avg_luma", contract.rayTracing.reflectionHistorySignalAvgLuma},
            {"reflection_history_signal_max_luma", contract.rayTracing.reflectionHistorySignalMaxLuma},
            {"reflection_history_signal_nonzero_ratio", contract.rayTracing.reflectionHistorySignalNonZeroRatio},
            {"reflection_history_signal_bright_ratio", contract.rayTracing.reflectionHistorySignalBrightRatio},
            {"reflection_history_signal_outlier_ratio", contract.rayTracing.reflectionHistorySignalOutlierRatio},
            {"reflection_history_signal_avg_luma_delta", contract.rayTracing.reflectionHistorySignalAvgLumaDelta},
            {"reflection_history_signal_readback_latency_frames",
             contract.rayTracing.reflectionHistorySignalReadbackLatencyFrames},
            {"reflection_width", contract.rayTracing.reflectionWidth},
            {"reflection_height", contract.rayTracing.reflectionHeight},
            {"gi_width", contract.rayTracing.giWidth},
            {"gi_height", contract.rayTracing.giHeight},
            {"reflection_update_cadence", contract.rayTracing.reflectionUpdateCadence},
            {"gi_update_cadence", contract.rayTracing.giUpdateCadence},
            {"reflection_frame_phase", contract.rayTracing.reflectionFramePhase},
            {"gi_frame_phase", contract.rayTracing.giFramePhase},
            {"dedicated_video_memory_bytes", contract.rayTracing.dedicatedVideoMemoryBytes},
            {"max_blas_build_bytes_per_frame", contract.rayTracing.maxBLASBuildBytesPerFrame},
            {"max_blas_total_bytes", contract.rayTracing.maxBLASTotalBytes},
            {"pending_blas", contract.rayTracing.pendingBLAS},
            {"pending_renderer_blas_jobs", contract.rayTracing.pendingRendererBLASJobs},
            {"tlas_instances", contract.rayTracing.tlasInstances},
            {"material_records", contract.rayTracing.materialRecords},
            {"material_buffer_bytes", contract.rayTracing.materialBufferBytes},
            {"tlas_candidates", contract.rayTracing.tlasCandidates},
            {"tlas_skipped_invalid", contract.rayTracing.tlasSkippedInvalid},
            {"tlas_missing_geometry", contract.rayTracing.tlasMissingGeometry},
            {"tlas_distance_culled", contract.rayTracing.tlasDistanceCulled},
            {"tlas_blas_build_requested", contract.rayTracing.tlasBLASBuildRequested},
            {"tlas_blas_build_budget_deferred", contract.rayTracing.tlasBLASBuildBudgetDeferred},
            {"tlas_blas_total_budget_skipped", contract.rayTracing.tlasBLASTotalBudgetSkipped},
            {"tlas_blas_build_failed", contract.rayTracing.tlasBLASBuildFailed},
            {"surface_default", contract.rayTracing.surfaceDefault},
            {"surface_glass", contract.rayTracing.surfaceGlass},
            {"surface_mirror", contract.rayTracing.surfaceMirror},
            {"surface_plastic", contract.rayTracing.surfacePlastic},
            {"surface_masonry", contract.rayTracing.surfaceMasonry},
            {"surface_emissive", contract.rayTracing.surfaceEmissive},
            {"surface_brushed_metal", contract.rayTracing.surfaceBrushedMetal},
            {"surface_wood", contract.rayTracing.surfaceWood},
            {"surface_water", contract.rayTracing.surfaceWater},
            {"material_surface_parity_comparable", contract.rayTracing.materialSurfaceParityComparable},
            {"material_surface_parity_matches", contract.rayTracing.materialSurfaceParityMatches},
            {"material_surface_parity_mismatches", contract.rayTracing.materialSurfaceParityMismatches}
        }},
        {"depth_policy", {
            {"depth_writing_total", contractDepthWriting},
            {"depth_tested_no_write_total", contractDepthTestedNoWrite},
            {"opaque_pass", "depth_writing_only"},
            {"depth_prepass", "depth_writing_only"},
            {"transparent_pass", "depth_test_no_write"},
            {"water_pass", "depth_test_no_write"},
            {"overlay_pass", "depth_test_no_write"}
        }},
        {"draw_counts", {
            {"depth_prepass_draws", contract.draws.depthPrepassDraws},
            {"shadow_draws", contract.draws.shadowDraws},
            {"opaque_draws", contract.draws.opaqueDraws},
            {"visibility_buffer_instances", contract.draws.visibilityBufferInstances},
            {"visibility_buffer_meshes", contract.draws.visibilityBufferMeshes},
            {"visibility_buffer_draw_batches", contract.draws.visibilityBufferDrawBatches},
            {"indirect_execute_calls", contract.draws.indirectExecuteCalls},
            {"indirect_commands", contract.draws.indirectCommands},
            {"overlay_draws", contract.draws.overlayDraws},
            {"water_draws", contract.draws.waterDraws},
            {"transparent_draws", contract.draws.transparentDraws},
            {"particle_draws", contract.draws.particleDraws},
            {"particle_instances", contract.draws.particleInstances},
            {"debug_line_draws", contract.draws.debugLineDraws},
            {"debug_line_vertices", contract.draws.debugLineVertices}
        }}
    };

    json contractResources = json::array();
    for (const auto& resource : contract.resources) {
        contractResources.push_back({
            {"name", resource.name},
            {"valid", resource.valid},
            {"width", resource.width},
            {"height", resource.height},
            {"expected_width", resource.expectedWidth},
            {"expected_height", resource.expectedHeight},
            {"size_matches_contract", resource.sizeMatchesContract},
            {"mb", static_cast<double>(resource.bytes) * kToMB}
        });
    }
    result["resources"] = std::move(contractResources);

    json contractHistories = json::array();
    for (const auto& history : contract.histories) {
        contractHistories.push_back({
            {"name", history.name},
            {"valid", history.valid},
            {"resource_valid", history.resourceValid},
            {"width", history.width},
            {"height", history.height},
            {"last_valid_frame", history.lastValidFrame},
            {"age_frames", history.ageFrames},
            {"invalid_reason", history.invalidReason},
            {"last_reset_reason", history.lastResetReason},
            {"seeded", history.seeded},
            {"last_invalidated_frame", history.lastInvalidatedFrame},
            {"rejection_mode", history.rejectionMode},
            {"accumulation_alpha", history.accumulationAlpha},
            {"uses_depth_normal_rejection", history.usesDepthNormalRejection},
            {"uses_velocity_reprojection", history.usesVelocityReprojection},
            {"uses_disocclusion_rejection", history.usesDisocclusionRejection}
        });
    }
    result["histories"] = std::move(contractHistories);

    json contractPasses = json::array();
    double passEstimatedWriteMBTotal = 0.0;
    uint32_t passFullScreenCount = 0;
    uint32_t passRayTracingCount = 0;
    uint32_t passRenderGraphCount = 0;
    uint32_t passTransientDescriptorDeltaTotal = 0;
    json topWritePasses = json::array();
    json topTransientDescriptorPasses = json::array();

    auto maybePushTopPass = [](json& array, const std::string& name, double value, const char* valueName) {
        if (value <= 0.0) {
            return;
        }
        array.push_back({{"name", name}, {valueName, value}});
        std::sort(array.begin(), array.end(), [valueName](const json& a, const json& b) {
            return a.value(valueName, 0.0) > b.value(valueName, 0.0);
        });
        if (array.size() > 5) {
            array.erase(array.begin() + 5, array.end());
        }
    };

    auto serializeAccesses = [](const auto& accesses) {
        json serialized = json::array();
        for (const auto& access : accesses) {
            serialized.push_back({
                {"name", access.name},
                {"state_class", access.stateClass}
            });
        }
        return serialized;
    };

    for (const auto& pass : contract.passes) {
        passEstimatedWriteMBTotal += pass.estimatedWriteMB;
        if (pass.fullScreen) {
            ++passFullScreenCount;
        }
        if (pass.rayTracing) {
            ++passRayTracingCount;
        }
        if (pass.renderGraph) {
            ++passRenderGraphCount;
        }
        passTransientDescriptorDeltaTotal += pass.descriptors.transientDelta;

        maybePushTopPass(topWritePasses, pass.name, pass.estimatedWriteMB, "estimated_write_mb");
        maybePushTopPass(topTransientDescriptorPasses,
                         pass.name,
                         static_cast<double>(pass.descriptors.transientDelta),
                         "transient_descriptor_delta");

        contractPasses.push_back({
            {"name", pass.name},
            {"planned", pass.planned},
            {"executed", pass.executed},
            {"fallback_used", pass.fallbackUsed},
            {"fallback_reason", pass.fallbackReason},
            {"draw_count", pass.drawCount},
            {"estimated_write_mb", pass.estimatedWriteMB},
            {"resolution_class", pass.resolutionClass},
            {"full_screen", pass.fullScreen},
            {"history_dependent", pass.historyDependent},
            {"ray_tracing", pass.rayTracing},
            {"render_graph", pass.renderGraph},
            {"descriptors", {
                {"rtv_used", pass.descriptors.rtvUsed},
                {"dsv_used", pass.descriptors.dsvUsed},
                {"shader_visible_used", pass.descriptors.shaderVisibleUsed},
                {"shader_visible_delta", pass.descriptors.shaderVisibleDelta},
                {"transient_used", pass.descriptors.transientUsed},
                {"transient_delta", pass.descriptors.transientDelta},
                {"staging_used", pass.descriptors.stagingUsed},
                {"staging_delta", pass.descriptors.stagingDelta}
            }},
            {"reads", pass.reads},
            {"writes", pass.writes},
            {"read_resources", serializeAccesses(pass.readResources)},
            {"write_resources", serializeAccesses(pass.writeResources)}
        });
    }
    result["passes"] = std::move(contractPasses);
    result["pass_budget_summary"] = {
        {"estimated_write_mb_total", passEstimatedWriteMBTotal},
        {"full_screen_passes", passFullScreenCount},
        {"ray_tracing_passes", passRayTracingCount},
        {"render_graph_passes", passRenderGraphCount},
        {"transient_descriptor_delta_total", passTransientDescriptorDeltaTotal},
        {"top_write_passes", std::move(topWritePasses)},
        {"top_transient_descriptor_passes", std::move(topTransientDescriptorPasses)}
    };
    result["render_graph"] = {
        {"active", contract.renderGraph.active},
        {"executions", contract.renderGraph.executions},
        {"pass_records", contract.renderGraph.passRecords},
        {"graph_passes", contract.renderGraph.graphPasses},
        {"culled_passes", contract.renderGraph.culledPasses},
        {"barriers", contract.renderGraph.barriers},
        {"fallback_executions", contract.renderGraph.fallbackExecutions},
        {"transient_validation_ran", contract.renderGraph.transientValidationRan},
        {"transient_resources", contract.renderGraph.transientResources},
        {"placed_resources", contract.renderGraph.placedResources},
        {"aliased_resources", contract.renderGraph.aliasedResources},
        {"aliasing_barriers", contract.renderGraph.aliasingBarriers},
        {"transient_requested_bytes", contract.renderGraph.transientRequestedBytes},
        {"transient_heap_used_bytes", contract.renderGraph.transientHeapUsedBytes},
        {"transient_heap_size_bytes", contract.renderGraph.transientHeapSizeBytes},
        {"transient_saved_bytes", contract.renderGraph.transientSavedBytes}
    };
    result["warnings"] = contract.warnings;
    return result;
}

} // namespace Cortex::Graphics
