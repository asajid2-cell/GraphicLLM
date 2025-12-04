# Device Removal Bug - ROOT CAUSE IDENTIFIED

## TL;DR - The Problem

The "device removal" error is **NOT** a graphics bug. It's a **TensorRT exception** from a corrupted model file that triggers D3D12 cleanup.

## Evidence

```
Exception thrown: nvinfer1::SerializationError at memory location 0x00000024408FDD40
[TensorRT] Serialization assertion plan.header.size == blobSize failed
DiffusionEngine: failed to deserialize UNet engine
D3D12: Removing Device.
```

The TensorRT engine file is incompatible or corrupted, causing an exception during `DreamerService::Initialize()`. D3D12 then cleans up by removing the device.

## The Fix - Three Options

### Option 1: Disable Dreamer (RECOMMENDED)
Run with the `--no-dreamer` flag:
```bash
CortexEngine.exe --no-dreamer
```

Or use the "CortexEngine (NO Dreamer - RECOMMENDED)" launch configuration in VS Code (F5).

### Option 2: Regenerate TensorRT Engine
The TensorRT engine was built for a different CUDA/TensorRT version. Delete it and rebuild:
```bash
rm "Z:\328\CMPUT328-A2\codexworks\301\graphics\CortexEngine\build\bin\models\dreamer\sdxl_turbo_unet_384x384.engine"
```

Then rebuild the engine with your current TensorRT version (requires TensorRT SDK).

### Option 3: Use Environment Variable
Set the environment variable before running:
```bash
set CORTEX_DISABLE_DREAMER=1
CortexEngine.exe
```

## What Was NOT The Problem

- ❌ Stale descriptor handles (we fixed those anyway as a precaution)
- ❌ GPU memory pressure (your 3070 Ti has plenty of VRAM)
- ❌ Ray tracing BLAS builds
- ❌ Dragon mesh upload timing
- ❌ Thread safety issues

These were all red herrings. The TensorRT exception was the culprit all along.

## Debug Instrumentation Added

Even though the root cause was TensorRT, we added valuable debug features:

1. **Enhanced error reporting** with resource state tracking
2. **D3D12 debug layer break-on-error** enabled
3. **Descriptor invalidation fixes** during resize
4. **Verbose logging flag** for detailed diagnostics
5. **VS Code launch configurations** with proper debugging setup

These will help catch future issues immediately!

## Testing the Fix

1. Press **F5** in VS Code
2. Select **"CortexEngine (NO Dreamer - RECOMMENDED)"**
3. The app should run without device removal errors
4. All graphics features (RT, shadows, bloom, etc.) will work normally

The Dreamer is only for AI texture generation and is not required for core rendering.

## If You Need Dreamer

To fix the TensorRT engine:

1. Ensure you have TensorRT 10.14.1.48 installed (matching your build)
2. Rebuild the engine from the ONNX model:
   ```bash
   trtexec --onnx=sdxl_turbo_unet_384x384.onnx \
           --saveEngine=sdxl_turbo_unet_384x384.engine \
           --fp16
   ```
3. Copy the new engine to the models/dreamer folder

Or contact the original developer for a compatible pre-built engine.
