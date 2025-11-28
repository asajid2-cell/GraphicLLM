import onnx
import gc

input_orig = "models/dreamer/onnx/sdxl_turbo_unet_768x768.onnx"
input_fp16 = "models/dreamer/onnx/sdxl_turbo_unet_768x768_fp16.onnx"

print("1. Loading BOTH models (this uses RAM)...")
model_orig = onnx.load(input_orig)
model_fp16 = onnx.load(input_fp16)

print(f"   > Original Opset: {model_orig.opset_import[0].version}")
print(f"   > Broken Opset:   {model_fp16.opset_import[0].version if model_fp16.opset_import else 'None'}")

print("2. Performing Metadata Transplant...")
# Copy Opset Version (The Language)
del model_fp16.opset_import[:]
model_fp16.opset_import.extend(model_orig.opset_import)

# Copy IR Version (The File Format)
model_fp16.ir_version = model_orig.ir_version

# Copy Outputs (The Exit Sign) - Just to be double safe
if len(model_fp16.graph.output) == 0:
    print("   > Re-grafting outputs...")
    model_fp16.graph.output.extend(model_orig.graph.output)

print(f"   > New Opset: {model_fp16.opset_import[0].version}")

# Delete original to free RAM
del model_orig
gc.collect()

print("3. Saving Fixed Model...")
onnx.save_model(
    model_fp16, 
    input_fp16, 
    save_as_external_data=True, 
    all_tensors_to_one_file=True, 
    location="sdxl_turbo_unet_768x768_fp16.onnx.data"
)
print("Done! The model is now healthy.")
