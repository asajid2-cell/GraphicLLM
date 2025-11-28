import onnx
from onnxconverter_common import float16
import os
import gc

input_path = "models/dreamer/onnx/sdxl_turbo_unet_768x768.onnx"
output_path = "models/dreamer/onnx/sdxl_turbo_unet_768x768_fp16.onnx"

print(f"Loading exploded model from {input_path}...")
model = onnx.load(input_path)

print("Converting to FP16 (Shrinking size by 50%)...")
model_fp16 = float16.convert_float_to_float16(model)

# clean up memory
del model
gc.collect()

print(f"Saving consolidated model to {output_path}...")
onnx.save_model(
    model_fp16, 
    output_path, 
    save_as_external_data=True, 
    all_tensors_to_one_file=True, 
    location="sdxl_turbo_unet_768x768_fp16.onnx.data"
)
print("Done! You can now build the engine.")
