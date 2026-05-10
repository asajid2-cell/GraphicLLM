#pragma once

#include "Debug/GPUProfiler.h"

#include <d3d12.h>

namespace Cortex::Graphics::FramePhase {

inline void BeginGpuScope(ID3D12GraphicsCommandList* commandList, const char* name, const char* category) {
    if (commandList) {
        Debug::GPUProfiler::Get().BeginScope(commandList, name, category);
    }
}

inline void EndGpuScope(ID3D12GraphicsCommandList* commandList) {
    if (commandList) {
        Debug::GPUProfiler::Get().EndScope(commandList);
    }
}

} // namespace Cortex::Graphics::FramePhase
