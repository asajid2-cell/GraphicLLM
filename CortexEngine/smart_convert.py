import onnx
from onnxconverter_common import float16
import gc

input_path = "models/dreamer/onnx/sdxl_turbo_unet_768x768.onnx"
output_path = "models/dreamer/onnx/sdxl_turbo_unet_768x768_fp16.onnx"

print(f"1. Loading model from {input_path}...")
model = onnx.load(input_path)

# Verify outputs exist before we touch anything
print(f"   > Original Inputs: {len(model.graph.input)}")
print(f"   > Original Outputs: {len(model.graph.output)}")
if len(model.graph.output) == 0:
    raise ValueError("CRITICAL: The source model has no outputs! You might need to re-export from PyTorch.")

print("2. Converting to FP16 (Safe Mode)...")
# keep_io_types=True prevents the converter from messing with the input/output signatures
model_fp16 = float16.convert_float_to_float16(model, keep_io_types=True)

# Verify outputs STILL exist
print(f"   > New Outputs: {len(model_fp16.graph.output)}")
if len(model_fp16.graph.output) == 0:
    raise ValueError("CRITICAL: Conversion wiped the outputs. This should not happen with keep_io_types=True.")

# Clean up RAM
del model
gc.collect()

print(f"3. Saving consolidated model to {output_path}...")
onnx.save_model(
    model_fp16, 
    output_path, 
    save_as_external_data=True, 
    all_tensors_to_one_file=True, 
    location="sdxl_turbo_unet_768x768_fp16.onnx.data"
)
print("Done! The graph is valid.")
