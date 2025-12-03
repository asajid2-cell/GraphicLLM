import os
import subprocess

import cv2
import numpy as np


# CONFIGURATION
# ---------------------------------------------------------
# Where are your PolyHaven EXRs located?
INPUT_DIR = "assets/polyhaven/textures"
# Where should the final DDS files go?
OUTPUT_DIR = "assets/textures/rtshowcase"
# Path to your texconv executable (relative to CortexEngine/)
TEXCONV_PATH = "./texconv.exe"
# Maximum resolution for generated normal maps (hero vs. props)
MAX_HERO_RES = 2048
MAX_PROP_RES = 1024
# ---------------------------------------------------------


def convert_exr_normals() -> None:
    if not os.path.isdir(INPUT_DIR):
        print(f"[ERROR] INPUT_DIR does not exist: {INPUT_DIR}")
        return

    os.makedirs(OUTPUT_DIR, exist_ok=True)

    files = [
        f
        for f in os.listdir(INPUT_DIR)
        if f.lower().endswith(".exr") and "_nor_" in f
    ]

    if not files:
        print(f"No EXR normal maps found in {INPUT_DIR}")
        return

    temp_tiff = "temp_normal.tif"

    for filename in files:
        full_input_path = os.path.join(INPUT_DIR, filename)

        print(f"Processing: {filename}...")

        # 1. Read EXR using OpenCV (float32, linear space)
        img = cv2.imread(full_input_path, cv2.IMREAD_UNCHANGED)
        if img is None:
            print(f"  [ERROR] Could not read {full_input_path}")
            continue

        # 2. Convert to 0..1 range if needed and scale to 16-bit.
        # PolyHaven normals are typically GL-style in 0..1; if we ever
        # encounter -1..1 data, shift into 0..1.
        img_min = float(img.min())
        img_max = float(img.max())

        if img_min < 0.0:
            img = (img * 0.5) + 0.5

        # Clamp to [0,1] before scaling to avoid overflow.
        img = np.clip(img, 0.0, 1.0)
        img_16bit = (img * 65535.0).astype(np.uint16)

        # 3. Save as intermediate 16-bit TIFF.
        cv2.imwrite(temp_tiff, img_16bit)

        # 4. Determine output name(s) for this normal map.
        output_name = "unknown_normal"
        output_names = []

        lower_name = filename.lower()
        if "wood_shutter" in lower_name:
            output_name = "rt_gallery_floor_normal_bc5"
        elif "castle_brick" in lower_name:
            output_name = "rt_gallery_leftwall_normal_bc5"
        elif "grey_plaster" in lower_name:
            output_name = "rt_gallery_rightwall_normal_bc5"
        elif "metal_plate" in lower_name:
            # Used for both cylinder and cube in the RT showcase.
            output_names = [
                "rt_gallery_cylinder_brushed_normal_bc5",
                "rt_gallery_cube_plastic_normal_bc5",
            ]

        if output_names:
            targets = output_names
        else:
            targets = [output_name]

        # 5. Run texconv to generate BC5 DDS from the TIFF, applying a simple
        # resolution cap so hero normals stay at 2K and props at 1K.
        for target in targets:
            final_dds = os.path.join(OUTPUT_DIR, target + ".dds")

            # Decide cap: hero surfaces (floor / walls) get the higher cap,
            # smaller props (metal plate) use the lower cap.
            if target in (
                "rt_gallery_floor_normal_bc5",
                "rt_gallery_leftwall_normal_bc5",
                "rt_gallery_rightwall_normal_bc5",
            ):
                max_res = MAX_HERO_RES
            else:
                max_res = MAX_PROP_RES

            cmd = [
                TEXCONV_PATH,
                "-f",
                "BC5_UNORM",
                "-y",
                "-w",
                str(max_res),
                "-h",
                str(max_res),
                "-o",
                OUTPUT_DIR,
                temp_tiff,
            ]

            result = subprocess.run(
                cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
            )
            if result.returncode != 0:
                print(f"  [ERROR] texconv failed for {filename}")
                continue

            generated_dds = os.path.join(OUTPUT_DIR, "temp_normal.dds")
            if os.path.exists(generated_dds):
                if os.path.exists(final_dds):
                    os.remove(final_dds)
                os.rename(generated_dds, final_dds)
                print(f"  -> Generated: {os.path.basename(final_dds)}")
            else:
                # texconv names output based on input stem; fall back to that.
                stem = os.path.splitext(os.path.basename(temp_tiff))[0]
                alt_dds = os.path.join(OUTPUT_DIR, stem + ".dds")
                if os.path.exists(alt_dds):
                    if os.path.exists(final_dds):
                        os.remove(final_dds)
                    os.rename(alt_dds, final_dds)
                    print(f"  -> Generated (alt): {os.path.basename(final_dds)}")
                else:
                    print(f"  [WARN] Expected DDS not found for {filename}")

    # Cleanup temp file.
    if os.path.exists(temp_tiff):
        os.remove(temp_tiff)

    print("Done.")


if __name__ == "__main__":
    convert_exr_normals()
