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

if (WIN32 AND MSVC AND NOT CMAKE_RC_COMPILER)
    # rc.exe is provided by the Windows SDK / MSVC toolset and
    # should be on PATH when using the VS developer environment.
    set(CMAKE_RC_COMPILER "rc.exe" CACHE FILEPATH "Resource compiler" FORCE)
endif()

add_subdirectory(vendor/llama.cpp)

# Fix llama.cpp C++20 char8_t issues with MSVC
if(MSVC AND TARGET llama)
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
set(CORTEX_SOURCES
    src/Core/Engine.cpp
    src/Core/Engine_Scenes.cpp
    src/Core/Window.cpp
    src/Core/ServiceLocator.cpp

    src/Graphics/RHI/DX12Device.cpp
    src/Graphics/RHI/DX12Texture.cpp
    src/Graphics/RHI/DX12CommandQueue.cpp
    src/Graphics/RHI/DescriptorHeap.cpp
    src/Graphics/RHI/DX12Pipeline.cpp
    src/Graphics/RHI/DX12Raytracing.cpp
    src/Graphics/RHI/BindlessResources.cpp
    src/Graphics/AssetRegistry.cpp
    src/Graphics/TextureLoader.cpp
    src/Graphics/GPUCulling.cpp
    src/Graphics/RenderGraph.cpp
    src/Graphics/VisibilityBuffer.cpp
    src/Graphics/MeshletBuilder.cpp
    src/Graphics/VariableRateShading.cpp
    src/Graphics/Renderer.cpp
    src/Graphics/Renderer_Bloom.cpp
    src/Graphics/Renderer_SSAO.cpp

    src/Scene/ECS_Registry.cpp
    src/Scene/Components.cpp
    src/Scene/TerrainNoise.cpp

    src/Game/InteractionSystem.cpp

    src/Utils/FileUtils.cpp
    src/Utils/MeshGenerator.cpp
    src/Utils/GLTFLoader.cpp

    # Diagnostics
    src/Core/PerfDiagnostics.cpp

    # Phase 3: The Dreamer (Vision)
    src/AI/Vision/DreamerService.cpp
    src/AI/Vision/DiffusionEngine.cpp

    # Phase 2: The Architect
    src/LLM/LLMService.cpp
    src/LLM/SceneCommands.cpp
    src/LLM/CommandQueue.cpp
    src/LLM/SceneLookup.cpp
    src/LLM/RegressionTests.cpp
    src/LLM/CompoundLibrary.cpp

    # UI helpers
    src/UI/TextPrompt.cpp
    src/UI/DebugMenu.cpp
    src/UI/QuickSettingsWindow.cpp
    src/UI/QualitySettingsWindow.cpp
    src/UI/SceneEditorWindow.cpp
    src/UI/PerformanceWindow.cpp
    src/UI/LightingWindow.cpp
    src/UI/HierarchyWindow.cpp
)

set(CORTEX_HEADERS
    src/Core/Engine.h
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
    src/Graphics/RenderGraph.h
    src/Graphics/VisibilityBuffer.h
    src/Graphics/MeshletBuilder.h
    src/Graphics/VariableRateShading.h
    src/Graphics/Renderer.h
    src/Graphics/ShaderTypes.h

    src/Scene/ECS_Registry.h
    src/Scene/Components.h
    src/Scene/TerrainNoise.h

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
    src/LLM/SceneLookup.h
    src/LLM/Prompts.h
    src/LLM/RegressionTests.h
    src/LLM/CompoundLibrary.h

    # UI helpers
    src/UI/TextPrompt.h
    src/UI/DebugMenu.h
    src/UI/QuickSettingsWindow.h
    src/UI/QualitySettingsWindow.h
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
    llama
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
# Use a stamp-based custom target so asset changes are picked up on every
# build invocation (including when only shaders are edited).
file(GLOB_RECURSE CORTEX_ASSET_FILES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/assets/*"
)
set(CORTEX_ASSET_STAMP "${CMAKE_BINARY_DIR}/cortex_assets.stamp")
add_custom_command(
    OUTPUT "${CORTEX_ASSET_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:CortexEngine>/assets"
    COMMAND ${CMAKE_COMMAND} -E touch "${CORTEX_ASSET_STAMP}"
    DEPENDS ${CORTEX_ASSET_FILES}
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
