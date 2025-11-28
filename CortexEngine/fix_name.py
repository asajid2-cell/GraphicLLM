import onnx
import gc

path = "models/dreamer/onnx/sdxl_turbo_unet_768x768_fp16.onnx"

print(f"Loading {path}...")
model = onnx.load(path)

print("Fixing missing graph name...")
model.graph.name = "SDXL_Turbo_UNet"

print("Saving...")
onnx.save_model(
    model, 
    path, 
    save_as_external_data=True, 
    all_tensors_to_one_file=True, 
    location="sdxl_turbo_unet_768x768_fp16.onnx.data"
)
print("Done! Graph is named.")
