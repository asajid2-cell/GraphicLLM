import onnx
from onnx import helper

path = "models/dreamer/onnx/sdxl_turbo_unet_768x768_fp16.onnx"

print(f"Loading {path}...")
model = onnx.load(path)

print("1. Forcing Opset Version to 17...")
# Clear existing versions and force 17
if len(model.opset_import) == 0:
    op = model.opset_import.add()
    op.version = 17
else:
    model.opset_import[0].version = 17

print("2. Ensuring Graph Name is set...")
model.graph.name = "SDXL_Turbo_UNet"

print("3. Ensuring IR Version is modern...")
model.ir_version = 8

print("Saving...")
onnx.save_model(
    model, 
    path, 
    save_as_external_data=True, 
    all_tensors_to_one_file=True, 
    location="sdxl_turbo_unet_768x768_fp16.onnx.data"
)
print("Done! The model is now labeled as Opset 17.")
