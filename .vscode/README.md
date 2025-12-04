# VS Code Debugging Setup for CortexEngine

## Quick Start

1. **Press F5** to launch CortexEngine in debug mode
2. The debugger will automatically break when D3D12 errors occur
3. Check the Debug Console for detailed error output

## Available Configurations

### 1. CortexEngine (Debug)
- Launches the debug build
- Normal debugging with symbols
- **Use this for development**

### 2. CortexEngine (Release)  
- Launches the release build (optimized)
- Useful for performance profiling
- Limited debug symbols

### 3. CortexEngine (Attach to Process)
- Attach debugger to already running CortexEngine.exe
- Useful when the app is already running

### 4. CortexEngine (Break on ALL Exceptions)
- **USE THIS TO DEBUG THE DEVICE REMOVAL BUG**
- Breaks on D3D12 validation errors
- Shows exact callstack when error occurs
- Source file mapping enabled

## Build Tasks (Ctrl+Shift+B)

- **Build CortexEngine (Release)** - Default build task
- **Build CortexEngine (Debug)** - Build with full debug info
- **Clean Build** - Clean all build artifacts
- **Reconfigure CMake** - Regenerate CMake cache

## Debugging the Device Removal Issue

When you hit the D3D12 device removal error:

1. **Select**: "CortexEngine (Break on ALL Exceptions)" config
2. **Press F5** to start debugging
3. The debugger will **BREAK** at the exact D3D12 API call causing the error
4. Look at:
   - **Call Stack** window (Ctrl+Shift+D) - Shows which render pass called the failing API
   - **Debug Console** - Shows the D3D12 error message
   - **Variables** window - Inspect resource states

5. **Take a screenshot** of:
   - The Call Stack
   - The line of code where it broke
   - Any D3D12 error messages in Debug Console

## Breakpoint Tips

- Set breakpoints in `Renderer.cpp` at:
  - Line 1238: `m_hdrColor.Reset()` (HDR resize)
  - Line 1224: `m_depthBuffer.Reset()` (Depth resize)
  - Line 709: `ProcessGpuJobsPerFrame()` (Mesh upload)
  
- Set a breakpoint in `DX12Device.cpp` at line 137:
  ```cpp
  infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
  ```
  This is where D3D12 debug layer is configured.

## Expected Output

When the bug triggers, you'll see something like:

```
D3D12 ERROR: ID3D12Device::CopyDescriptorsSimple: SrcDescriptorRangeStart points to...
[BREAK]
```

The debugger will stop at the **exact line** making the bad D3D12 call!

## Notes

- The D3D12 debug layer is **ENABLED** in both Debug and Release builds
- Errors will trigger breakpoints automatically
- Resource states are logged in the error message
- Frame counter shows which frame the error occurred on
