# How to Build and Debug CortexEngine

## Quick Build Commands

### Option 1: Using Batch Scripts (Recommended)
Double-click one of these files in Windows Explorer:

- **`rebuild-release.bat`** - Build Release version (optimized, fast)
- **`rebuild-debug.bat`** - Build Debug version (with symbols, for debugging)

### Option 2: Using VS Code
Press **Ctrl+Shift+B** and select:
- "Build CortexEngine (Release)" - Default
- "Build CortexEngine (Debug)" - For debugging

### Option 3: Command Line
```bash
# Release build
cd Z:\328\CMPUT328-A2\codexworks\301\graphics
cmake --build CortexEngine/build --config Release --target CortexEngine

# Debug build
cd Z:\328\CMPUT328-A2\codexworks\301\graphics
cmake --build CortexEngine/build --config Debug --target CortexEngine
```

## Where Are The Executables?

After building:
- **Release**: `CortexEngine/build/bin/CortexEngine.exe`
- **Debug**: `CortexEngine/build/bin/Debug/CortexEngine.exe`

## How to Debug

### Step 1: Build Debug Version
Run `rebuild-debug.bat` or use VS Code build task

### Step 2: Launch with Debugger
Press **F5** in VS Code and select one of these:

1. **"CortexEngine (NO Dreamer - RECOMMENDED)"** - Normal debugging without Dreamer
2. **"CortexEngine (Debug)"** - Standard debug launch
3. **"CortexEngine (Break on ALL Exceptions)"** - Breaks on any exception (use this for crashes)

### Step 3: Look for Device Removal Error
When the device removal happens, check:

1. **Console Output** - Shows the error message with resource states
2. **Call Stack** (Ctrl+Shift+D in VS Code) - Shows where the crash occurred
3. **Variables Window** - Inspect values at crash time

## Current Debug Status

### Changes Made:
1. ✅ **Dreamer commented out** - The TensorRT initialization is disabled
2. ✅ **Enhanced error logging** - Device removal shows all resource states
3. ✅ **Descriptor invalidation fixes** - Prevents stale descriptor bugs
4. ✅ **Command line arg logging** - Shows which flags are being parsed

### Expected Console Output:
```
[info] Command line arg[1]: '--no-launcher'
[info] Command line arg[2]: '--no-dreamer'
[info] Command line arg[3]: '--scene=rt_showcase'
[info]   -> Dreamer disabled via --no-dreamer
[info] After parsing args: enableDreamer=false, enableLLM=true
[info] Dreamer initialization SKIPPED (commented out for debugging)
```

Then if device removal still happens:
```
[error] DX12 device removed or GPU fault in 'EndFrame_Present' (hr=0x887A0006, reason=0x887A0006, frameCounter=9, swapIndex=2, lastPass='RenderPostProcess_Done', lastGpuMarker='None', at Z:\...\Renderer.cpp:1874). 
ResourceStates: depth=0x80, shadowMap=0x80, hdr=0x80, rtShadowMask=0x80, rtShadowMaskHistory=0x80, gbufferNR=0x80, ssao=0x80, ssr=0x4, velocity=0x4, history=0x80, taaIntermediate=0x4, rtRefl=0x8, rtReflHist=0x80, rtGI=0x80, rtGIHist=0x80
```

### Resource State Values (for reference):
- `0x1` = D3D12_RESOURCE_STATE_COMMON
- `0x4` = D3D12_RESOURCE_STATE_RENDER_TARGET
- `0x8` = D3D12_RESOURCE_STATE_UNORDERED_ACCESS
- `0x80` = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE

## Troubleshooting Build Errors

### "Cannot open include file"
This means the build environment is corrupted. Try:
```bash
cd CortexEngine/build
cmake -S .. -B .
cmake --build . --config Debug --target CortexEngine
```

### "Executable is locked"
The app is still running. Close it first:
- Press ESC in the game window
- Or kill it from Task Manager
- Then rebuild

### Build succeeds but changes don't appear
Make sure you're running the right executable:
- Debug changes go to: `build/bin/Debug/CortexEngine.exe`
- Release changes go to: `build/bin/CortexEngine.exe`
- VS Code launch.json is configured for the Release path by default

## Next Steps

1. **Build the debug version** with Dreamer commented out
2. **Run with F5** using "CortexEngine (NO Dreamer - RECOMMENDED)"
3. **Check console output** - Does it still crash?
4. **If yes**: The Dreamer was NOT the root cause - we need to look deeper at resource states
5. **If no**: The Dreamer initialization was somehow causing the crash

## Useful Shortcuts

- **F5** - Start debugging
- **Ctrl+Shift+B** - Build
- **Ctrl+Shift+D** - Open debug view / call stack
- **Ctrl+`** - Toggle terminal
- **Shift+F5** - Stop debugging
