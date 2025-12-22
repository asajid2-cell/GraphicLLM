#pragma once

// Prefer the Windows SDK header when available, but fall back to the vcpkg
// DirectX-Headers package in environments where the Windows SDK include path
// is not present (e.g., non-Developer shells).

#if __has_include(<d3d12.h>)
#include <d3d12.h>
#elif __has_include(<directx/d3d12.h>)
#include <directx/d3d12.h>
#else
#error "Neither <d3d12.h> nor <directx/d3d12.h> could be found. Install the Windows SDK or DirectX-Headers."
#endif

