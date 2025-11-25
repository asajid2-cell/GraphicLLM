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
# Prefer CUDA acceleration when available
set(LLAMA_CUBLAS ON CACHE BOOL "" FORCE)
add_subdirectory(vendor/llama.cpp)

# Fix llama.cpp C++20 char8_t issues with MSVC
if(MSVC AND TARGET llama)
    target_compile_options(llama PRIVATE /Zc:char8_t-)
endif()

# Source Files
set(CORTEX_SOURCES
    src/Core/Engine.cpp
    src/Core/Window.cpp
    src/Core/ServiceLocator.cpp

    src/Graphics/RHI/DX12Device.cpp
    src/Graphics/RHI/DX12Texture.cpp
    src/Graphics/RHI/DX12CommandQueue.cpp
    src/Graphics/RHI/DescriptorHeap.cpp
    src/Graphics/RHI/DX12Pipeline.cpp
    src/Graphics/Renderer.cpp

    src/Scene/ECS_Registry.cpp
    src/Scene/Components.cpp

    src/Utils/FileUtils.cpp
    src/Utils/MeshGenerator.cpp

    # Phase 2: The Architect
    src/LLM/LLMService.cpp
    src/LLM/SceneCommands.cpp
    src/LLM/CommandQueue.cpp

    # UI helpers
    src/UI/TextPrompt.cpp
)

set(CORTEX_HEADERS
    src/Core/Engine.h
    src/Core/Window.h
    src/Core/ServiceLocator.h

    src/Graphics/RHI/DX12Device.h
    src/Graphics/RHI/DX12Texture.h
    src/Graphics/RHI/DX12CommandQueue.h
    src/Graphics/RHI/DescriptorHeap.h
    src/Graphics/RHI/DX12Pipeline.h
    src/Graphics/RHI/d3dx12.h
    src/Graphics/Renderer.h
    src/Graphics/ShaderTypes.h

    src/Scene/ECS_Registry.h
    src/Scene/Components.h

    src/Utils/Result.h
    src/Utils/FileUtils.h
    src/Utils/MeshGenerator.h

    # Phase 2: The Architect
    src/LLM/LLMService.h
    src/LLM/SceneCommands.h
    src/LLM/CommandQueue.h
    src/LLM/Prompts.h

    # UI helpers
    src/UI/TextPrompt.h
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

# Copy assets to build directory
add_custom_command(TARGET CortexEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:CortexEngine>/assets
)
