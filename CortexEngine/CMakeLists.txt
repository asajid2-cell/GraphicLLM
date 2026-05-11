cmake_minimum_required(VERSION 3.20)
project(CortexEngine VERSION 0.1.0 LANGUAGES CXX)

# Modern C++20 Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# vcpkg toolchain (automatically set by vcpkg)
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "")
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Dependencies
find_package(SDL3 CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(directx-headers CONFIG REQUIRED)
find_package(directxtk12 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

# LLM integration is optional for renderer-focused builds and validation.
# When disabled, Cortex uses a lightweight mock LLM service and does not pull
# the llama.cpp/CUDA dependency chain into the renderer executable target.
option(CORTEX_ENABLE_LLM_BACKEND "Enable local llama.cpp LLM backend" OFF)

if(CORTEX_ENABLE_LLM_BACKEND)
    # llama.cpp integration
    set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)

    # Enable CUDA backend for ggml / llama.cpp.
    # We no longer force this OFF here; instead we expose GGML_CUDA as a
    # normal cache option so local builds (including setup.ps1, which
    # passes -DGGML_CUDA=ON) can actually turn on the CUDA backend.
    # On machines where CUDA / nvcc is problematic, you can disable it by
    # configuring with -DGGML_CUDA=OFF.
    option(GGML_CUDA "Enable GGML CUDA backend" ON)
endif()

if (WIN32 AND MSVC AND NOT CMAKE_RC_COMPILER)
    # rc.exe is provided by the Windows SDK / MSVC toolset and
    # should be on PATH when using the VS developer environment.
    set(CMAKE_RC_COMPILER "rc.exe" CACHE FILEPATH "Resource compiler" FORCE)
endif()

if(CORTEX_ENABLE_LLM_BACKEND)
    add_subdirectory(vendor/llama.cpp)
endif()

# Fix llama.cpp C++20 char8_t issues with MSVC
if(CORTEX_ENABLE_LLM_BACKEND AND MSVC AND TARGET llama)
    target_compile_options(llama PRIVATE /Zc:char8_t-)
endif()

# Phase 3: Optional TensorRT diffusion backend for Dreamer.
# Disabled by default so the project builds on machines without TensorRT.
option(CORTEX_ENABLE_TENSORRT "Enable Dreamer GPU diffusion via TensorRT" OFF)

# Phase 1: Enable SM6.6 bindless resources (requires DXC compiler and Windows 10 19H1+)
# When enabled, shaders compile with DXC to SM6.6 and use ResourceDescriptorHeap[] for bindless textures.
# When disabled, shaders compile with FXC to SM5.1 and use traditional descriptor tables.
option(CORTEX_ENABLE_BINDLESS "Enable SM6.6 bindless resources" ON)

# Source Files
set(CORTEX_LLM_SERVICE_SOURCE src/LLM/LLMServiceStub.cpp)
if(CORTEX_ENABLE_LLM_BACKEND)
    set(CORTEX_LLM_SERVICE_SOURCE src/LLM/LLMService.cpp)
endif()

set(CORTEX_SOURCES
    src/Core/Engine.cpp
    src/Core/Engine_Camera.cpp
    src/Core/Engine_Input.cpp
    src/Core/Engine_LLM.cpp
    src/Core/Engine_Scenes.cpp
    src/Core/Engine_PlayMode.cpp
    src/Core/Engine_UI.cpp
    src/Core/EngineEditorMode.cpp
    src/Core/StartupPreflight.cpp
    src/Core/VisualValidation.cpp
    src/Core/Window.cpp
    src/Core/ServiceLocator.cpp

    src/Graphics/RHI/DX12Device.cpp
    src/Graphics/RHI/DX12Texture.cpp
    src/Graphics/RHI/DX12Texture_Cube.cpp
    src/Graphics/RHI/DX12Texture_MipUpload.cpp
    src/Graphics/RHI/DX12Texture_Upload.cpp
    src/Graphics/RHI/DX12Texture_Views.cpp
    src/Graphics/RHI/DX12CommandQueue.cpp
    src/Graphics/RHI/DescriptorHeap.cpp
    src/Graphics/RHI/DX12Pipeline.cpp
    src/Graphics/RHI/DX12Raytracing.cpp
    src/Graphics/RHI/DX12Raytracing_BLAS.cpp
    src/Graphics/RHI/DX12Raytracing_Dispatch.cpp
    src/Graphics/RHI/DX12Raytracing_GIPipeline.cpp
    src/Graphics/RHI/DX12Raytracing_Lifecycle.cpp
    src/Graphics/RHI/DX12Raytracing_Materials.cpp
    src/Graphics/RHI/DX12Raytracing_ReflectionPipeline.cpp
    src/Graphics/RHI/DX12Raytracing_RootSignature.cpp
    src/Graphics/RHI/DX12Raytracing_ShadowPipeline.cpp
    src/Graphics/RHI/DX12Raytracing_TLAS.cpp
    src/Graphics/RHI/BindlessResources.cpp
     src/Graphics/AssetRegistry.cpp
     src/Graphics/TextureAdmission.cpp
     src/Graphics/TextureSourcePlan.cpp
     src/Graphics/TextureLoader.cpp
    src/Graphics/RenderableClassification.cpp
    src/Graphics/BudgetPlanner.cpp
    src/Graphics/EnvironmentManifest.cpp
    src/Graphics/FrameFeaturePlan.cpp
    src/Graphics/FrameContractValidation.cpp
    src/Graphics/FrameContractResources.cpp
    src/Graphics/FrameContractJson.cpp
    src/Graphics/MaterialPresetRegistry.cpp
    src/Graphics/MaterialModel.cpp
    src/Graphics/RTDenoiser.cpp
    src/Graphics/RTReflectionSignalStats.cpp
    src/Graphics/RTScheduler.cpp
    src/Graphics/TemporalRejectionMask.cpp
    src/Graphics/TemporalManager.cpp
    src/Graphics/RendererGeometryUtils.cpp
    src/Graphics/RendererSceneSnapshot.cpp
    src/Graphics/Renderer_AssetRegistry.cpp
    src/Graphics/GPUCulling.cpp
    src/Graphics/GPUCulling_Dispatch.cpp
    src/Graphics/GPUCulling_Pipelines.cpp
    src/Graphics/GPUCulling_Readback.cpp
    src/Graphics/GPUCulling_Resources.cpp
    src/Graphics/GPUCulling_Uploads.cpp
    src/Graphics/Passes/BloomPass.cpp
    src/Graphics/Passes/BloomGraphPass.cpp
    src/Graphics/Passes/BackBufferPresentPass.cpp
    src/Graphics/Passes/DepthPrepass.cpp
    src/Graphics/Passes/DepthPrepassTargetPass.cpp
    src/Graphics/Passes/DescriptorTable.cpp
    src/Graphics/Passes/DebugLinePass.cpp
    src/Graphics/Passes/EndFrameShaderResourcePass.cpp
    src/Graphics/Passes/ForwardTargetBindingPass.cpp
    src/Graphics/Passes/FullscreenPass.cpp
    src/Graphics/Passes/HZBPass.cpp
    src/Graphics/Passes/MainPassTargetPass.cpp
    src/Graphics/Passes/MeshDrawPass.cpp
    src/Graphics/Passes/MinimalFramePass.cpp
    src/Graphics/Passes/MotionVectorTargetPass.cpp
    src/Graphics/Passes/MotionVectorPass.cpp
    src/Graphics/Passes/ParticleBillboardPass.cpp
    src/Graphics/Passes/PostProcessGraphPass.cpp
    src/Graphics/Passes/PostProcessTargetPass.cpp
    src/Graphics/Passes/PostProcessPass.cpp
    src/Graphics/Passes/RenderGraphValidationPass.cpp
    src/Graphics/Passes/RTHistoryCopyPass.cpp
    src/Graphics/Passes/ShadowPass.cpp
    src/Graphics/Passes/ShadowTargetPass.cpp
    src/Graphics/Passes/SSAOPass.cpp
    src/Graphics/Passes/SSRPass.cpp
    src/Graphics/Passes/TAACopyPass.cpp
    src/Graphics/Passes/SkyboxPass.cpp
    src/Graphics/Passes/TAAPass.cpp
    src/Graphics/Passes/VisibilityBufferGraphPass.cpp
    src/Graphics/Passes/VoxelPass.cpp
    src/Graphics/RenderGraph.cpp
    src/Graphics/RenderGraph_Barriers.cpp
    src/Graphics/RenderGraph_Builder.cpp
    src/Graphics/RenderGraph_CompileExecute.cpp
    src/Graphics/RenderGraph_Lifecycle.cpp
    src/Graphics/RenderGraph_StateQueries.cpp
    src/Graphics/RenderGraph_TransientResources.cpp
    src/Graphics/VisibilityBuffer.cpp
    src/Graphics/VisibilityBuffer_BRDFLUTPipeline.cpp
    src/Graphics/VisibilityBuffer_ClusteredLightPipeline.cpp
    src/Graphics/VisibilityBuffer_DebugBlitPipelines.cpp
    src/Graphics/VisibilityBuffer_DeferredLightingPipeline.cpp
    src/Graphics/VisibilityBuffer_DebugBlit.cpp
    src/Graphics/VisibilityBuffer_DeferredLighting.cpp
    src/Graphics/VisibilityBuffer_MaterialResolvePipeline.cpp
    src/Graphics/VisibilityBuffer_MotionVectorPipeline.cpp
    src/Graphics/VisibilityBuffer_PipelineStates.cpp
    src/Graphics/VisibilityBuffer_RootSignatures.cpp
    src/Graphics/VisibilityBuffer_Resolve.cpp
    src/Graphics/VisibilityBuffer_Resources.cpp
    src/Graphics/VisibilityBuffer_Uploads.cpp
    src/Graphics/VisibilityBuffer_VisibilityPipelines.cpp
    src/Graphics/VisibilityBuffer_VisibilityPass.cpp
    src/Graphics/MeshletBuilder.cpp
    src/Graphics/VariableRateShading.cpp
    src/Graphics/Renderer.cpp
    src/Graphics/Renderer_Descriptors.cpp
    src/Graphics/Renderer_DebugLines.cpp
    src/Graphics/Renderer_Diagnostics.cpp
    src/Graphics/RendererControlApplier.cpp
    src/Graphics/RendererControlApplier_Debug.cpp
    src/Graphics/RendererControlApplier_Runtime.cpp
    src/Graphics/RendererControlApplier_ScenePresets.cpp
    src/Graphics/RendererTuningState.cpp
    src/Graphics/RendererLightingRigControl.cpp
    src/Graphics/Renderer_DepthPasses.cpp
    src/Graphics/Renderer_DepthTarget.cpp
    src/Graphics/Renderer_EditorHooks.cpp
    src/Graphics/Renderer_Environment.cpp
    src/Graphics/Renderer_ForwardPass.cpp
    src/Graphics/Renderer_FrameBegin.cpp
    src/Graphics/Renderer_FrameContractHistories.cpp
    src/Graphics/Renderer_FrameContractMemory.cpp
    src/Graphics/Renderer_FrameContractPasses.cpp
    src/Graphics/Renderer_FrameContractSnapshot.cpp
    src/Graphics/Renderer_FrameContractValidationBridge.cpp
    src/Graphics/Renderer_FrameConstants.cpp
    src/Graphics/Renderer_FrameEnd.cpp
    src/Graphics/Renderer_FrameLightingConstants.cpp
    src/Graphics/Renderer_FramePostConstants.cpp
    src/Graphics/Renderer_FrameTemporalConstants.cpp
    src/Graphics/Renderer_FramePhases_Main.cpp
    src/Graphics/Renderer_FramePhases_Post.cpp
    src/Graphics/Renderer_FramePhases_Prepare.cpp
    src/Graphics/Renderer_FramePlanning.cpp
    src/Graphics/Renderer_GeometryPipelineSetup.cpp
    src/Graphics/Renderer_GPUDriven.cpp
    src/Graphics/Renderer_HDRTargets.cpp
    src/Graphics/Renderer_IndirectRendering.cpp
    src/Graphics/Renderer_HZB.cpp
    src/Graphics/Renderer_Initialization.cpp
    src/Graphics/Renderer_Materials.cpp
    src/Graphics/Renderer_MainPassSetup.cpp
    src/Graphics/Renderer_MeshUpload.cpp
    src/Graphics/Renderer_OverlayGeometry.cpp
    src/Graphics/Renderer_PostProcess.cpp
    src/Graphics/Renderer_PipelineSetup.cpp
    src/Graphics/Renderer_RootSignatureSetup.cpp
    src/Graphics/GPUParticles.cpp
    src/Graphics/Renderer_Particles.cpp
    src/Graphics/Renderer_RTCache.cpp
    src/Graphics/Renderer_RTDenoise.cpp
    src/Graphics/Renderer_RTFramePlan.cpp
    src/Graphics/Renderer_RTReflections.cpp
    src/Graphics/Renderer_RTReflectionSignalStats.cpp
    src/Graphics/Renderer_RTResources.cpp
    src/Graphics/Renderer_RTShadowsGI.cpp
    src/Graphics/Renderer_ScreenComputePipelineSetup.cpp
    src/Graphics/Renderer_ShaderCompilation.cpp
    src/Graphics/Renderer_RenderGraphBloom.cpp
    src/Graphics/Renderer_RenderGraphDepthShadow.cpp
    src/Graphics/Renderer_RenderGraphDiagnostics.cpp
    src/Graphics/Renderer_RenderGraphEndFrame.cpp
    src/Graphics/Renderer_RenderGraphMotionVectors.cpp
    src/Graphics/Renderer_RenderGraphSSAO.cpp
    src/Graphics/Renderer_RenderGraphSSR.cpp
    src/Graphics/Renderer_RenderGraphTAA.cpp
    src/Graphics/Renderer_RenderGraphTemporalMask.cpp
    src/Graphics/Renderer_RenderGraphTemporalScreen.cpp
    src/Graphics/Renderer_RenderGraphVisibilityBuffer.cpp
    src/Graphics/Renderer_RenderOrchestration.cpp
    src/Graphics/Renderer_DebugSettings.cpp
    src/Graphics/Renderer_FeatureSettings.cpp
    src/Graphics/Renderer_LightingSettings.cpp
    src/Graphics/Renderer_QualitySettings.cpp
    src/Graphics/Renderer_ShadowPass.cpp
    src/Graphics/Renderer_ShadowSettings.cpp
    src/Graphics/Renderer_ShadowResources.cpp
    src/Graphics/Renderer_Shutdown.cpp
    src/Graphics/Renderer_SkyboxPass.cpp
    src/Graphics/Renderer_SSRPass.cpp
    src/Graphics/Renderer_Synchronization.cpp
    src/Graphics/Renderer_SurfaceSettings.cpp
    src/Graphics/Renderer_TAAExecution.cpp
    src/Graphics/Renderer_TemporalMaskPass.cpp
    src/Graphics/Renderer_TemporalResources.cpp
    src/Graphics/Renderer_TemporalReadback.cpp
     src/Graphics/Renderer_TextureCreation.cpp
     src/Graphics/Renderer_MotionVectors.cpp
     src/Graphics/Renderer_TextureLoad.cpp
     src/Graphics/Renderer_TexturePublication.cpp
     src/Graphics/Renderer_TextureQueue.cpp
     src/Graphics/Renderer_TextureUploadExecution.cpp
     src/Graphics/Renderer_TransparentGeometry.cpp
    src/Graphics/Renderer_VisibilityBufferCollection.cpp
    src/Graphics/Renderer_VisibilityBufferCulling.cpp
    src/Graphics/Renderer_VisibilityBufferDeferredLighting.cpp
    src/Graphics/Renderer_VisibilityBufferDiagnostics.cpp
    src/Graphics/Renderer_VisibilityBufferOrchestration.cpp
    src/Graphics/Renderer_VisibilityBufferStages.cpp
    src/Graphics/Renderer_Voxel.cpp
    src/Graphics/Renderer_Vegetation.cpp
    src/Graphics/Renderer_WaterSurfaces.cpp
    src/Graphics/Renderer_Bloom.cpp
    src/Graphics/Renderer_SSAO.cpp

    src/Scene/ECS_Registry.cpp
    src/Scene/Components.cpp
    src/Scene/ParticleEffectLibrary.cpp
    src/Scene/TerrainNoise.cpp
    src/Scene/BiomeMap.cpp

    # Editor (Engine Editor Mode)
    src/Editor/EditorWorld.cpp
    src/Editor/ChunkGenerator.cpp
    src/Editor/SpatialGrid.cpp
    src/Editor/EditorCamera.cpp

    src/Game/InteractionSystem.cpp

    src/Utils/FileUtils.cpp
    src/Utils/MeshGenerator.cpp
    src/Utils/GLTFLoader.cpp
    src/Utils/ConfigLoader.cpp

    # Diagnostics
    src/Core/PerfDiagnostics.cpp
    src/Debug/GPUProfiler.cpp

    # Phase 3: The Dreamer (Vision)
    src/AI/Vision/DreamerService.cpp
    src/AI/Vision/DiffusionEngine.cpp

    # Phase 2: The Architect
    ${CORTEX_LLM_SERVICE_SOURCE}
    src/LLM/SceneCommands.cpp
    src/LLM/CommandQueue.cpp
    src/LLM/RendererCommandApplier.cpp
    src/LLM/SceneLookup.cpp
    src/LLM/RegressionTests.cpp
    src/LLM/CompoundLibrary.cpp

    # UI helpers
    src/UI/TextPrompt.cpp
    src/UI/DebugMenu.cpp
    src/UI/QuickSettingsWindow.cpp
    src/UI/QualitySettingsWindow.cpp
    src/UI/GraphicsSettingsWindow.cpp
    src/UI/SceneEditorWindow.cpp
    src/UI/PerformanceWindow.cpp
    src/UI/LightingWindow.cpp
    src/UI/HierarchyWindow.cpp
)

set(CORTEX_HEADERS
    src/Core/Engine.h
    src/Core/EngineEditorMode.h
    src/Core/StartupPreflight.h
    src/Core/VisualValidation.h
    src/Core/Window.h
    src/Core/ServiceLocator.h
    src/Core/PerfDiagnostics.h

    src/Graphics/RHI/DX12Device.h
    src/Graphics/RHI/DX12Texture.h
    src/Graphics/RHI/DX12CommandQueue.h
    src/Graphics/RHI/DescriptorHeap.h
    src/Graphics/RHI/DX12Pipeline.h
    src/Graphics/RHI/DX12Raytracing.h
    src/Graphics/RHI/d3dx12.h
    src/Graphics/MaterialState.h
    src/Graphics/TextureLoader.h
    src/Graphics/AssetRegistry.h
    src/Graphics/RenderableClassification.h
    src/Graphics/FrameFeaturePlan.h
    src/Graphics/GpuJobQueue.h
    src/Graphics/FrameContractValidation.h
    src/Graphics/FrameContractResources.h
    src/Graphics/FrameContractJson.h
    src/Graphics/EnvironmentManifest.h
    src/Graphics/MaterialPresetRegistry.h
    src/Graphics/MaterialModel.h
    src/Graphics/RTDenoiser.h
    src/Graphics/RTReflectionSignalStats.h
    src/Graphics/RTScheduler.h
    src/Graphics/TemporalRejectionMask.h
    src/Graphics/TemporalManager.h
     src/Graphics/TextureAdmission.h
     src/Graphics/TextureSourcePlan.h
     src/Graphics/TextureUploadQueue.h
     src/Graphics/TextureUploadReceipt.h
    src/Graphics/TextureUploadTicket.h
    src/Graphics/Renderer_ConstantBuffer.h
    src/Graphics/Renderer_DiagnosticsTypes.h
    src/Graphics/RendererPostProcessState.h
    src/Graphics/RendererControlApplier.h
    src/Graphics/RendererControlTypes.h
    src/Graphics/RendererTuningState.h
    src/Graphics/RendererLightingRigControl.h
    src/Graphics/RendererGeometryUtils.h
    src/Graphics/RendererSceneSnapshot.h
    src/Graphics/Passes/BloomPass.h
    src/Graphics/Passes/BloomGraphPass.h
    src/Graphics/Passes/BackBufferPresentPass.h
    src/Graphics/Passes/DepthPrepass.h
    src/Graphics/Passes/DepthPrepassTargetPass.h
    src/Graphics/Passes/DescriptorTable.h
    src/Graphics/Passes/EndFrameShaderResourcePass.h
    src/Graphics/Passes/FullscreenPass.h
    src/Graphics/Passes/HZBPass.h
    src/Graphics/Passes/MainPassTargetPass.h
    src/Graphics/Passes/MinimalFramePass.h
    src/Graphics/Passes/MotionVectorTargetPass.h
    src/Graphics/Passes/MotionVectorPass.h
    src/Graphics/Passes/PostProcessGraphPass.h
    src/Graphics/Passes/PostProcessTargetPass.h
    src/Graphics/Passes/PostProcessPass.h
    src/Graphics/Passes/RenderPassScope.h
    src/Graphics/Passes/RenderGraphValidationPass.h
    src/Graphics/Passes/RTHistoryCopyPass.h
    src/Graphics/Passes/ShadowPass.h
    src/Graphics/Passes/ShadowTargetPass.h
    src/Graphics/Passes/SSAOPass.h
    src/Graphics/Passes/SSRPass.h
    src/Graphics/Passes/TAACopyPass.h
    src/Graphics/Passes/SkyboxPass.h
    src/Graphics/Passes/TAAPass.h
    src/Graphics/Passes/VisibilityBufferGraphPass.h
    src/Graphics/RenderGraph.h
    src/Graphics/VisibilityBuffer.h
    src/Graphics/MeshletBuilder.h
    src/Graphics/VariableRateShading.h
    src/Graphics/Renderer.h
    src/Graphics/ShaderTypes.h

    src/Scene/ECS_Registry.h
    src/Scene/Components.h
    src/Scene/ParticleEffectLibrary.h
    src/Scene/TerrainNoise.h
    src/Scene/BiomeTypes.h
    src/Scene/BiomeMap.h

    # Editor (Engine Editor Mode)
    src/Editor/EditorWorld.h
    src/Editor/ChunkGenerator.h
    src/Editor/SpatialGrid.h

    src/Game/InteractionSystem.h

    src/Utils/Result.h
    src/Utils/FileUtils.h
    src/Utils/MeshGenerator.h
    src/Utils/GLTFLoader.h

    # Phase 3: The Dreamer (Vision)
    src/AI/Vision/DreamerService.h
    src/AI/Vision/DiffusionEngine.h

    # Phase 2: The Architect
    src/LLM/LLMService.h
    src/LLM/SceneCommands.h
    src/LLM/CommandQueue.h
    src/LLM/RendererCommandApplier.h
    src/LLM/SceneLookup.h
    src/LLM/Prompts.h
    src/LLM/RegressionTests.h
    src/LLM/CompoundLibrary.h

    # UI helpers
    src/UI/TextPrompt.h
    src/UI/DebugMenu.h
    src/UI/QuickSettingsWindow.h
    src/UI/QualitySettingsWindow.h
    src/UI/GraphicsSettingsWindow.h
    src/UI/SceneEditorWindow.h
    src/UI/PerformanceWindow.h
    src/UI/LightingWindow.h
    src/UI/HierarchyWindow.h
)

# Executable
add_executable(CortexEngine
    src/main.cpp
    ${CORTEX_SOURCES}
    ${CORTEX_HEADERS}
)

# Include directories
target_include_directories(CortexEngine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)

# Compile-time defines for optional features
if(CORTEX_ENABLE_BINDLESS)
    target_compile_definitions(CortexEngine PRIVATE ENABLE_BINDLESS=1)
    message(STATUS "Bindless resources ENABLED (SM6.6 with DXC)")
else()
    message(STATUS "Bindless resources DISABLED (SM5.1 fallback with FXC)")
endif()

if(CORTEX_ENABLE_LLM_BACKEND)
    target_compile_definitions(CortexEngine PRIVATE CORTEX_ENABLE_LLM_BACKEND=1)
    message(STATUS "LLM backend ENABLED (llama.cpp)")
else()
    message(STATUS "LLM backend DISABLED (mock service, renderer-only build)")
endif()

if(CORTEX_ENABLE_TENSORRT)
    target_compile_definitions(CortexEngine PRIVATE CORTEX_ENABLE_TENSORRT=1)
    # Require the CUDA toolkit so that TensorRT headers (which include
    # cuda_runtime_api.h) compile cleanly, and so we can link against
    # the proper CUDA runtime library.
    find_package(CUDAToolkit REQUIRED)
    target_include_directories(CortexEngine PRIVATE ${CUDAToolkit_INCLUDE_DIRS})

    # TensorRT integration expects the user to provide include/lib paths, e.g. via:
    #   -DTensorRT_INCLUDE_DIR="C:/TensorRT/include"
    #   -DTensorRT_LIB_DIR="C:/TensorRT/lib"
    if(DEFINED TensorRT_INCLUDE_DIR)
        target_include_directories(CortexEngine PRIVATE ${TensorRT_INCLUDE_DIR})
    endif()
    if(DEFINED TensorRT_LIB_DIR)
        target_link_directories(CortexEngine PRIVATE ${TensorRT_LIB_DIR})
    endif()
    # Link core TensorRT + CUDA libs. TensorRT 10.x on Windows uses versioned
    # library names (nvinfer_10.lib), while older versions use nvinfer.lib.
    # Prefer the versioned name when present, fall back otherwise.
    set(CORTEX_TRT_CORE_LIB nvinfer)
    if(DEFINED TensorRT_LIB_DIR)
        if(EXISTS "${TensorRT_LIB_DIR}/nvinfer_10.lib")
            set(CORTEX_TRT_CORE_LIB nvinfer_10)
        endif()
    endif()
    target_link_libraries(CortexEngine PRIVATE ${CORTEX_TRT_CORE_LIB} CUDA::cudart)
endif()

# Link libraries
target_link_libraries(CortexEngine PRIVATE
    SDL3::SDL3
    EnTT::EnTT
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    Microsoft::DirectX-Headers
    Microsoft::DirectXTK12
    glm::glm
    d3d12.lib
    dxgi.lib
    dxguid.lib
    d3dcompiler.lib
    dxcompiler.lib
    user32.lib
    gdi32.lib
    comctl32.lib
    dbghelp.lib
)

if(CORTEX_ENABLE_LLM_BACKEND)
    target_link_libraries(CortexEngine PRIVATE llama)
endif()

# Compiler options
if(MSVC)
    target_compile_options(CortexEngine PRIVATE /W4 /WX- /permissive-)
    target_compile_definitions(CortexEngine PRIVATE
        _CRT_SECURE_NO_WARNINGS
        UNICODE
        _UNICODE
        NOMINMAX
    )
endif()

# Copy assets to the runtime output directory.
#
# Important: shader iteration often changes only files under assets/, which
# does not trigger a relink of the CortexEngine target. A POST_BUILD step
# therefore won't run when the executable is already up-to-date, and the app
# ends up running stale shaders from build/bin/assets.
#
# Use a stamp-based custom target so known asset changes are picked up on every
# build invocation (including when only shaders are edited). New asset files
# require a normal CMake reconfigure; avoiding CONFIGURE_DEPENDS keeps Ninja
# from running CMake's generated glob verifier before every build.
file(GLOB_RECURSE CORTEX_ASSET_FILES
    "${CMAKE_SOURCE_DIR}/assets/*"
)
list(FILTER CORTEX_ASSET_FILES EXCLUDE REGEX "\\.[Hh][Dd][Rr]$")
list(FILTER CORTEX_ASSET_FILES EXCLUDE REGEX "\\.[Ee][Xx][Rr]$")
set(CORTEX_ASSET_STAMP "${CMAKE_BINARY_DIR}/cortex_assets.stamp")
add_custom_command(
    OUTPUT "${CORTEX_ASSET_STAMP}"
    COMMAND ${CMAKE_COMMAND}
            -D "SOURCE_DIR=${CMAKE_SOURCE_DIR}/assets"
            -D "DEST_DIR=$<TARGET_FILE_DIR:CortexEngine>/assets"
            -D "STAMP=${CORTEX_ASSET_STAMP}"
            -P "${CMAKE_SOURCE_DIR}/tools/sync_assets.cmake"
    DEPENDS ${CORTEX_ASSET_FILES}
            "${CMAKE_SOURCE_DIR}/tools/sync_assets.cmake"
    VERBATIM
)
add_custom_target(CortexAssets ALL DEPENDS "${CORTEX_ASSET_STAMP}")

# -----------------------------------------------------------------------------
# DXR shader libraries (DXIL)
# -----------------------------------------------------------------------------
#
# The ray tracing pipelines load precompiled DXIL libraries from:
#   assets/shaders/RaytracedShadows.dxil
#   assets/shaders/RaytracedReflections.dxil
#   assets/shaders/RaytracedGI.dxil
#
# Editing the corresponding HLSL files should therefore recompile these DXIL
# blobs, otherwise the runtime will keep using stale shaders even when the
# HLSL sources (and other assets) are updated.
set(CORTEX_DXC_HINTS)
if (EXISTS "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/tools/directx-dxc/dxc.exe")
    list(APPEND CORTEX_DXC_HINTS "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/tools/directx-dxc")
endif()
find_program(CORTEX_DXC_EXECUTABLE NAMES dxc HINTS ${CORTEX_DXC_HINTS})

function(cortex_compile_dxr_lib out_dxil in_hlsl)
    if (NOT CORTEX_DXC_EXECUTABLE)
        message(WARNING "DXC (dxc.exe) not found; DXR shader libraries will not be recompiled")
        return()
    endif()

    add_custom_command(
        OUTPUT "${out_dxil}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/shaders"
        COMMAND "${CORTEX_DXC_EXECUTABLE}"
                -T lib_6_3
                -O3
                -Qstrip_debug
                -Fo "${out_dxil}"
                "${in_hlsl}"
        # Also depend on the asset stamp. The asset copy step includes legacy
        # precompiled *.dxil blobs from the source tree; rebuilding DXIL after
        # any asset update ensures the runtime directory ends up with the
        # freshly compiled library rather than a stale copied file.
        DEPENDS "${in_hlsl}" "${CORTEX_ASSET_STAMP}"
        VERBATIM
    )
endfunction()

set(CORTEX_DXR_SHADERS_DIR "${CMAKE_SOURCE_DIR}/assets/shaders")
set(CORTEX_DXR_OUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/shaders")

set(CORTEX_DXR_DXIL_OUTPUTS
    "${CORTEX_DXR_OUT_DIR}/RaytracedShadows.dxil"
    "${CORTEX_DXR_OUT_DIR}/RaytracedReflections.dxil"
    "${CORTEX_DXR_OUT_DIR}/RaytracedGI.dxil"
)

cortex_compile_dxr_lib("${CORTEX_DXR_OUT_DIR}/RaytracedShadows.dxil"     "${CORTEX_DXR_SHADERS_DIR}/RaytracedShadows.hlsl")
cortex_compile_dxr_lib("${CORTEX_DXR_OUT_DIR}/RaytracedReflections.dxil" "${CORTEX_DXR_SHADERS_DIR}/RaytracedReflections.hlsl")
cortex_compile_dxr_lib("${CORTEX_DXR_OUT_DIR}/RaytracedGI.dxil"          "${CORTEX_DXR_SHADERS_DIR}/RaytracedGI.hlsl")

add_custom_target(CortexDXRShaders ALL DEPENDS ${CORTEX_DXR_DXIL_OUTPUTS})
add_dependencies(CortexDXRShaders CortexAssets)
add_dependencies(CortexEngine CortexDXRShaders)

find_program(CORTEX_POWERSHELL_EXECUTABLE NAMES pwsh powershell)
if(CORTEX_POWERSHELL_EXECUTABLE)
    add_custom_target(cortex_smoke_rt_showcase
        COMMAND ${CORTEX_POWERSHELL_EXECUTABLE}
                -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tools/run_rt_showcase_smoke.ps1"
                -NoBuild
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS CortexEngine
        COMMENT "Running RT showcase smoke validation"
        VERBATIM
    )
endif()
