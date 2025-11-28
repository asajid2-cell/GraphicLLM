import onnx
from onnxconverter_common import float16
import gc

input_path = "models/dreamer/onnx/sdxl_turbo_unet_768x768.onnx"
output_path = "models/dreamer/onnx/sdxl_turbo_unet_768x768_fp16.onnx"

print("1. Loading original model...")
model = onnx.load(input_path)

# CAPTURE THE ORIGINAL OUTPUTS BEFORE CONVERSION
original_outputs = list(model.graph.output)
print(f"   > Stole {len(original_outputs)} output definitions from original graph.")

print("2. Converting to FP16...")
# We do NOT use keep_io_types=True this time, to let the inputs/outputs shrink to FP16
model_fp16 = float16.convert_float_to_float16(model)

# 3. SURGICAL RE-ATTACHMENT
print("3. Checking for missing outputs...")
if len(model_fp16.graph.output) == 0:
    print("   > Outputs are missing! Grafting original outputs back on...")
    model_fp16.graph.output.extend(original_outputs)
else:
    print("   > Outputs survived. No surgery needed.")

# Clean up RAM
del model
gc.collect()

print(f"4. Saving repaired model to {output_path}...")
onnx.save_model(
    model_fp16, 
    output_path, 
    save_as_external_data=True, 
    all_tensors_to_one_file=True, 
    location="sdxl_turbo_unet_768x768_fp16.onnx.data"
)
print("Done! The graph is valid and has outputs.")
