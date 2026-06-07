#!/usr/bin/env python3
"""Generate explicit scene-local environment proxy DDS assets.

The V3 renderer can bind separate irradiance, specular, and visible-background
proxy resources. This tool creates small deterministic BC1 DDS proxy maps from
scene profile palettes plus sampled scene-local material payloads so the
renderer no longer has to treat payload albedo/normal textures as the proxy
resources themselves.
"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path
from typing import Any

try:
    from PIL import Image
except Exception:  # pragma: no cover - optional local tool dependency
    Image = None  # type: ignore[assignment]


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ID = "generate_scene_local_environment_proxies.py"
DERIVATION_METHOD = "profile_payload_material_room_light_v1"
PROXY_RESOURCE_SHAPE = "filtered_directional_bc1_v1"
GENERATED_CONTRACT_HEADER = ROOT / "src" / "Graphics" / "Generated" / "SceneLocalProxyContracts.generated.h"
DEFAULT_SETS = {
    "rt_showcase_gallery": {
        "irradiance": (150, 146, 134),
        "specular": (183, 186, 188),
        "visible_background": (169, 174, 181),
    },
    "home_kitchen_lantern": {
        "irradiance": (186, 143, 98),
        "specular": (212, 179, 130),
        "visible_background": (132, 106, 82),
    },
    "home_office_evening": {
        "irradiance": (124, 105, 84),
        "specular": (170, 156, 138),
        "visible_background": (88, 83, 78),
    },
    "school_classroom_day": {
        "irradiance": (161, 173, 154),
        "specular": (190, 202, 194),
        "visible_background": (144, 157, 168),
    },
    "basketball_gym_day": {
        "irradiance": (179, 158, 111),
        "specular": (200, 192, 166),
        "visible_background": (74, 112, 158),
    },
    "neon_streamer_concert": {
        "irradiance": (86, 34, 126),
        "specular": (61, 190, 218),
        "visible_background": (23, 18, 42),
    },
    "red_light_room": {
        "irradiance": (138, 31, 38),
        "specular": (207, 76, 70),
        "visible_background": (59, 20, 24),
    },
    "stadium_night_match": {
        "irradiance": (62, 82, 96),
        "specular": (172, 192, 204),
        "visible_background": (21, 34, 48),
    },
}
PAYLOAD_ALIASES = {
    "rt_showcase_gallery": ["assets/textures/scene_local/rt_showcase_gallery", "assets/textures/rtshowcase"],
    "home_kitchen_lantern": ["assets/textures/scene_local/home_kitchen_lantern", "assets/textures/rtshowcase"],
    "home_office_evening": ["assets/textures/scene_local/home_office_evening", "assets/textures/rtshowcase"],
    "school_classroom_day": ["assets/textures/scene_local/school_classroom_day", "assets/textures/rtshowcase"],
    "basketball_gym_day": ["assets/textures/scene_local/basketball_gym_day"],
    "neon_streamer_concert": ["assets/textures/scene_local/neon_streamer_concert", "assets/textures/rtshowcase"],
    "red_light_room": ["assets/textures/scene_local/red_light_room", "assets/textures/rtshowcase"],
    "stadium_night_match": ["assets/textures/scene_local/stadium_night_match", "assets/textures/rtshowcase"],
}
ROLE_TINTS = {
    "floor": (178, 160, 126),
    "wall": (136, 142, 143),
    "cube": (156, 158, 164),
    "cylinder": (176, 177, 171),
    "metal": (196, 198, 198),
    "stage": (70, 42, 105),
    "exterior": (54, 77, 98),
}
ROOM_LIGHT_CONTRACTS = {
    "rt_showcase_gallery": {
        "room_shell": {
            "enclosure": "gallery_partial",
            "wall_reflectance": 0.66,
            "ceiling_reflectance": 0.72,
            "local_background_occlusion": 0.22,
        },
        "light_rig": {
            "mode": "neutral_gallery_key",
            "key_rgb": (214, 218, 224),
            "fill_rgb": (170, 176, 184),
            "accent_rgb": (192, 196, 198),
            "accent_strength": 0.10,
        },
    },
    "home_kitchen_lantern": {
        "room_shell": {
            "enclosure": "warm_enclosed_room",
            "wall_reflectance": 0.58,
            "ceiling_reflectance": 0.62,
            "local_background_occlusion": 0.46,
        },
        "light_rig": {
            "mode": "warm_practical_plus_fill",
            "key_rgb": (255, 191, 119),
            "fill_rgb": (167, 150, 133),
            "accent_rgb": (255, 148, 70),
            "accent_strength": 0.24,
        },
    },
    "home_office_evening": {
        "room_shell": {
            "enclosure": "evening_enclosed_room",
            "wall_reflectance": 0.52,
            "ceiling_reflectance": 0.56,
            "local_background_occlusion": 0.52,
        },
        "light_rig": {
            "mode": "soft_warm_desk_fill",
            "key_rgb": (235, 187, 127),
            "fill_rgb": (120, 130, 148),
            "accent_rgb": (255, 171, 92),
            "accent_strength": 0.18,
        },
    },
    "school_classroom_day": {
        "room_shell": {
            "enclosure": "bright_enclosed_room",
            "wall_reflectance": 0.72,
            "ceiling_reflectance": 0.78,
            "local_background_occlusion": 0.28,
        },
        "light_rig": {
            "mode": "cool_daylight_windows",
            "key_rgb": (210, 226, 242),
            "fill_rgb": (174, 185, 174),
            "accent_rgb": (206, 216, 196),
            "accent_strength": 0.08,
        },
    },
    "basketball_gym_day": {
        "room_shell": {
            "enclosure": "tall_gym_volume",
            "wall_reflectance": 0.62,
            "ceiling_reflectance": 0.68,
            "local_background_occlusion": 0.34,
        },
        "light_rig": {
            "mode": "high_bay_day_fill",
            "key_rgb": (228, 230, 220),
            "fill_rgb": (184, 198, 211),
            "accent_rgb": (255, 188, 86),
            "accent_strength": 0.12,
        },
    },
    "neon_streamer_concert": {
        "room_shell": {
            "enclosure": "dark_stage_volume",
            "wall_reflectance": 0.24,
            "ceiling_reflectance": 0.18,
            "local_background_occlusion": 0.72,
        },
        "light_rig": {
            "mode": "cyan_magenta_stage",
            "key_rgb": (40, 205, 255),
            "fill_rgb": (89, 48, 156),
            "accent_rgb": (255, 61, 166),
            "accent_strength": 0.42,
        },
    },
    "red_light_room": {
        "room_shell": {
            "enclosure": "dark_red_room",
            "wall_reflectance": 0.30,
            "ceiling_reflectance": 0.24,
            "local_background_occlusion": 0.66,
        },
        "light_rig": {
            "mode": "red_practical_accent",
            "key_rgb": (255, 42, 50),
            "fill_rgb": (110, 24, 34),
            "accent_rgb": (255, 94, 62),
            "accent_strength": 0.36,
        },
    },
    "stadium_night_match": {
        "room_shell": {
            "enclosure": "open_exterior_bowl",
            "wall_reflectance": 0.36,
            "ceiling_reflectance": 0.04,
            "local_background_occlusion": 0.18,
        },
        "light_rig": {
            "mode": "cool_floodlights",
            "key_rgb": (214, 229, 255),
            "fill_rgb": (72, 92, 118),
            "accent_rgb": (122, 185, 255),
            "accent_strength": 0.20,
        },
    },
}


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def cpp_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def format_float(value: float) -> str:
    return f"{value:.6f}f"


def write_contract_header(path: Path) -> None:
    lines = [
        "#pragma once",
        "",
        "#include <string_view>",
        "",
        "namespace Cortex::Graphics::Generated {",
        "",
        "struct SceneLocalProxyContractRecord {",
        "    const char* textureSetId;",
        "    const char* derivationMethod;",
        "    const char* roomShell;",
        "    float roomOcclusion;",
        "    const char* lightRig;",
        "    float lightAccentStrength;",
        "};",
        "",
        f'inline constexpr const char* kSceneLocalProxyDerivationMethod = "{cpp_string(DERIVATION_METHOD)}";',
        "",
        "inline constexpr SceneLocalProxyContractRecord kSceneLocalProxyContracts[] = {",
    ]
    for set_id in sorted(ROOM_LIGHT_CONTRACTS):
        contract = ROOM_LIGHT_CONTRACTS[set_id]
        room = contract["room_shell"]
        light = contract["light_rig"]
        lines.append(
            "    {"
            f'"{cpp_string(set_id)}", '
            "kSceneLocalProxyDerivationMethod, "
            f'"{cpp_string(str(room["enclosure"]))}", '
            f'{format_float(float(room["local_background_occlusion"]))}, '
            f'"{cpp_string(str(light["mode"]))}", '
            f'{format_float(float(light["accent_strength"]))}'
            "},"
        )
    lines.extend([
        "};",
        "",
        "inline const SceneLocalProxyContractRecord* FindSceneLocalProxyContract(std::string_view textureSetId) {",
        "    for (const SceneLocalProxyContractRecord& contract : kSceneLocalProxyContracts) {",
        "        if (textureSetId == contract.textureSetId) {",
        "            return &contract;",
        "        }",
        "    }",
        "    return nullptr;",
        "}",
        "",
        "} // namespace Cortex::Graphics::Generated",
        "",
    ])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def rgb565(color: tuple[int, int, int]) -> int:
    r, g, b = (max(0, min(255, int(v))) for v in color)
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def clamp_rgb(color: tuple[float, float, float] | tuple[int, int, int]) -> tuple[int, int, int]:
    return tuple(max(0, min(255, int(round(v)))) for v in color)  # type: ignore[return-value]


def mix(a: tuple[int, int, int], b: tuple[int, int, int], t: float) -> tuple[int, int, int]:
    t = max(0.0, min(1.0, t))
    return clamp_rgb(tuple(float(a[i]) * (1.0 - t) + float(b[i]) * t for i in range(3)))


def brighten(color: tuple[int, int, int], amount: float) -> tuple[int, int, int]:
    return clamp_rgb(tuple(float(c) + (255.0 - float(c)) * amount for c in color))


def darken(color: tuple[int, int, int], amount: float) -> tuple[int, int, int]:
    return clamp_rgb(tuple(float(c) * (1.0 - amount) for c in color))


def texture_role(name: str) -> str:
    lower = name.lower()
    if "floor" in lower:
        return "floor"
    if "wall" in lower:
        return "wall"
    if "metal" in lower or "brushed" in lower:
        return "metal"
    if "cylinder" in lower:
        return "cylinder"
    if "cube" in lower:
        return "cube"
    return "other"


def is_albedo_payload(name: str) -> bool:
    lower = name.lower()
    if "normal" in lower or "roughness" in lower or "metallic" in lower:
        return False
    return "albedo" in lower or "diff" in lower or "basecolor" in lower or "base_color" in lower


def average_colors(colors: list[tuple[int, int, int]]) -> tuple[int, int, int] | None:
    if not colors:
        return None
    return clamp_rgb(tuple(sum(float(c[i]) for c in colors) / float(len(colors)) for i in range(3)))


def mix_optional(
    base: tuple[int, int, int], sample: tuple[int, int, int] | None, weight: float
) -> tuple[int, int, int]:
    if sample is None:
        return base
    return mix(base, sample, weight)


def sample_material_color(path: Path) -> dict[str, Any]:
    row: dict[str, Any] = {
        "path": rel(path),
        "role": texture_role(path.name),
        "sampled": False,
        "rgb": None,
        "decoder": "none",
        "failure": "not_attempted",
    }
    if not is_albedo_payload(path.name):
        row["failure"] = "non_color_payload"
        return row
    if Image is None:
        row["failure"] = "pillow_unavailable"
        return row
    try:
        with Image.open(path) as image:
            rgb_image = image.convert("RGB")
            try:
                resampling = Image.Resampling.BOX  # type: ignore[attr-defined]
            except AttributeError:  # pragma: no cover - old Pillow fallback
                resampling = Image.BOX  # type: ignore[attr-defined]
            pixel = rgb_image.resize((1, 1), resampling).getpixel((0, 0))
        row["sampled"] = True
        row["rgb"] = [int(pixel[0]), int(pixel[1]), int(pixel[2])]
        row["decoder"] = "pillow_dds"
        row["failure"] = "none"
    except Exception as exc:
        row["failure"] = f"{type(exc).__name__}: {exc}"
    return row


def material_sample_summary(dds_files: list[Path]) -> dict[str, Any]:
    samples = [sample_material_color(path) for path in dds_files if is_albedo_payload(path.name)]
    sampled_rows = [row for row in samples if row.get("sampled") is True and isinstance(row.get("rgb"), list)]
    sampled_colors = [tuple(int(v) for v in row["rgb"]) for row in sampled_rows]
    role_colors: dict[str, list[tuple[int, int, int]]] = {}
    for row in sampled_rows:
        role_colors.setdefault(str(row["role"]), []).append(tuple(int(v) for v in row["rgb"]))
    role_average_rgb = {
        role: list(avg)
        for role, colors in sorted(role_colors.items())
        if (avg := average_colors(colors)) is not None
    }
    failures = [row for row in samples if row.get("sampled") is not True]
    return {
        "decoder": "pillow_dds" if Image is not None else "none",
        "color_payload_count": len(samples),
        "sampled_color_payload_count": len(sampled_rows),
        "failed_color_payload_count": len(failures),
        "average_rgb": list(average_colors(sampled_colors) or (0, 0, 0)),
        "role_average_rgb": role_average_rgb,
        "sampled_textures": [
            {
                "path": row["path"],
                "role": row["role"],
                "rgb": row["rgb"],
                "decoder": row["decoder"],
            }
            for row in sampled_rows[:12]
        ],
        "failed_textures": [
            {
                "path": row["path"],
                "role": row["role"],
                "failure": row["failure"],
            }
            for row in failures[:12]
        ],
    }


def apply_room_light_contract(
    colors: dict[str, tuple[int, int, int]],
    contract: dict[str, Any],
) -> tuple[dict[str, tuple[int, int, int]], dict[str, Any]]:
    room_shell = contract["room_shell"]
    light_rig = contract["light_rig"]
    wall_reflectance = float(room_shell["wall_reflectance"])
    ceiling_reflectance = float(room_shell["ceiling_reflectance"])
    local_background_occlusion = float(room_shell["local_background_occlusion"])
    accent_strength = float(light_rig["accent_strength"])
    key_rgb = tuple(int(v) for v in light_rig["key_rgb"])
    fill_rgb = tuple(int(v) for v in light_rig["fill_rgb"])
    accent_rgb = tuple(int(v) for v in light_rig["accent_rgb"])

    diffuse_reflectance = max(0.0, min(1.0, (wall_reflectance + ceiling_reflectance) * 0.5))
    ambient_strength = 0.16 + 0.22 * diffuse_reflectance
    occlusion_strength = max(0.0, min(0.78, local_background_occlusion))

    irradiance = colors["irradiance"]
    specular = colors["specular"]
    visible = colors["visible_background"]

    irradiance = mix(irradiance, fill_rgb, ambient_strength)
    irradiance = darken(irradiance, occlusion_strength * 0.18)
    specular = mix(specular, key_rgb, 0.18)
    specular = mix(specular, accent_rgb, accent_strength * 0.32)
    visible = mix(visible, fill_rgb, 0.12 + 0.14 * diffuse_reflectance)
    visible = darken(visible, occlusion_strength * 0.36)
    if str(room_shell["enclosure"]).startswith("open_"):
        visible = mix(visible, key_rgb, 0.12)
        irradiance = mix(irradiance, key_rgb, 0.08)
    if "stage" in str(light_rig["mode"]) or "red" in str(light_rig["mode"]):
        visible = mix(visible, accent_rgb, accent_strength * 0.22)
        irradiance = mix(irradiance, accent_rgb, accent_strength * 0.18)

    influenced = {
        "irradiance": irradiance,
        "specular": specular,
        "visible_background": visible,
    }
    influence = {
        "room_shell": {
            "enclosure": room_shell["enclosure"],
            "wall_reflectance": wall_reflectance,
            "ceiling_reflectance": ceiling_reflectance,
            "local_background_occlusion": local_background_occlusion,
            "diffuse_reflectance": diffuse_reflectance,
            "ambient_strength": ambient_strength,
        },
        "light_rig": {
            "mode": light_rig["mode"],
            "key_rgb": list(key_rgb),
            "fill_rgb": list(fill_rgb),
            "accent_rgb": list(accent_rgb),
            "accent_strength": accent_strength,
        },
    }
    return influenced, influence


def dds_header(width: int, height: int, linear_size: int) -> bytes:
    ddsd_caps = 0x00000001
    ddsd_height = 0x00000002
    ddsd_width = 0x00000004
    ddsd_pixelformat = 0x00001000
    ddsd_linearsize = 0x00080000
    ddpf_fourcc = 0x00000004
    dds_caps_texture = 0x00001000
    fourcc_dxt1 = int.from_bytes(b"DXT1", "little")
    header_flags = ddsd_caps | ddsd_height | ddsd_width | ddsd_pixelformat | ddsd_linearsize

    reserved1 = [0] * 11
    pixel_format = struct.pack(
        "<IIIIIIII",
        32,
        ddpf_fourcc,
        fourcc_dxt1,
        0,
        0,
        0,
        0,
        0,
    )
    return b"DDS " + struct.pack(
        "<IIIIIII11I32sIIIII",
        124,
        header_flags,
        height,
        width,
        linear_size,
        0,
        0,
        *reserved1,
        pixel_format,
        dds_caps_texture,
        0,
        0,
        0,
        0,
    )


def solid_bc1_data(width: int, height: int, color: tuple[int, int, int]) -> bytes:
    blocks_x = max(1, (width + 3) // 4)
    blocks_y = max(1, (height + 3) // 4)
    c0 = rgb565(color)
    c1 = 0
    indices = 0
    block = struct.pack("<HHI", c0, c1, indices)
    return block * (blocks_x * blocks_y)


def directional_proxy_color(
    role: str,
    base_color: tuple[int, int, int],
    derivation: dict[str, Any],
    block_x: int,
    block_y: int,
    blocks_x: int,
    blocks_y: int,
) -> tuple[int, int, int]:
    u = (float(block_x) + 0.5) / max(1.0, float(blocks_x))
    v = (float(block_y) + 0.5) / max(1.0, float(blocks_y))
    influence = derivation["scene_contract_influence"]
    light = influence["light_rig"]
    room = influence["room_shell"]
    material_samples = derivation["payload_inventory"]["material_samples"]
    role_avg = material_samples.get("role_average_rgb", {})
    key_rgb = tuple(int(value) for value in light["key_rgb"])
    fill_rgb = tuple(int(value) for value in light["fill_rgb"])
    accent_rgb = tuple(int(value) for value in light["accent_rgb"])
    wall_rgb = tuple(int(value) for value in role_avg.get("wall", fill_rgb))
    floor_rgb = tuple(int(value) for value in role_avg.get("floor", base_color))
    object_rgb = average_colors([
        tuple(int(value) for value in role_avg[name])
        for name in ("cube", "cylinder", "metal", "other")
        if name in role_avg
    ]) or base_color
    occlusion = float(room["local_background_occlusion"])
    accent_strength = float(light["accent_strength"])
    horizon = max(0.0, min(1.0, 1.0 - abs(v - 0.62) * 2.2))
    key_lobe = max(0.0, min(1.0, 1.0 - ((u - 0.72) ** 2 + (v - 0.30) ** 2) * 5.5))
    accent_lobe = max(0.0, min(1.0, 1.0 - ((u - 0.22) ** 2 + (v - 0.42) ** 2) * 7.0))
    vertical = max(0.0, min(1.0, v))

    color = base_color
    if role == "irradiance":
        color = mix(color, fill_rgb, 0.18 + 0.16 * horizon)
        color = mix(color, key_rgb, 0.08 + 0.12 * key_lobe)
        color = mix(color, floor_rgb, 0.06 + 0.10 * vertical)
        color = darken(color, occlusion * 0.08 * (1.0 - key_lobe))
    elif role == "specular":
        color = mix(color, object_rgb, 0.12)
        color = mix(color, key_rgb, 0.12 + 0.34 * key_lobe)
        color = mix(color, accent_rgb, accent_strength * (0.18 + 0.42 * accent_lobe))
        color = brighten(color, 0.04 + 0.12 * max(key_lobe, accent_lobe))
    else:
        color = mix(color, wall_rgb, 0.18 + 0.22 * (1.0 - vertical))
        color = mix(color, floor_rgb, 0.16 * vertical)
        color = mix(color, key_rgb, 0.05 * key_lobe)
        color = mix(color, accent_rgb, accent_strength * 0.18 * accent_lobe)
        color = darken(color, occlusion * (0.14 + 0.18 * (1.0 - horizon)))
    return color


def filtered_bc1_data(
    width: int,
    height: int,
    role: str,
    color: tuple[int, int, int],
    derivation: dict[str, Any],
) -> tuple[bytes, dict[str, Any]]:
    blocks_x = max(1, (width + 3) // 4)
    blocks_y = max(1, (height + 3) // 4)
    parts: list[bytes] = []
    colors: list[tuple[int, int, int]] = []
    for by in range(blocks_y):
        for bx in range(blocks_x):
            block_color = directional_proxy_color(role, color, derivation, bx, by, blocks_x, blocks_y)
            colors.append(block_color)
            parts.append(struct.pack("<HHI", rgb565(block_color), 0, 0))
    average = average_colors(colors) or color
    max_delta = max(
        (
            abs(c[0] - average[0]) +
            abs(c[1] - average[1]) +
            abs(c[2] - average[2])
            for c in colors
        ),
        default=0,
    )
    stats = {
        "shape": PROXY_RESOURCE_SHAPE,
        "blocks_x": blocks_x,
        "blocks_y": blocks_y,
        "block_count": blocks_x * blocks_y,
        "average_rgb": list(average),
        "min_rgb": [min(c[i] for c in colors) for i in range(3)],
        "max_rgb": [max(c[i] for c in colors) for i in range(3)],
        "variance_score": round(float(max_delta) / (255.0 * 3.0), 6),
    }
    return b"".join(parts), stats


def write_bc1_dds(
    path: Path,
    role: str,
    color: tuple[int, int, int],
    derivation: dict[str, Any],
    size: int,
    overwrite: bool,
) -> tuple[bool, dict[str, Any]]:
    width = max(4, size)
    height = max(4, size)
    payload, stats = filtered_bc1_data(width, height, role, color, derivation)
    data = dds_header(width, height, len(payload)) + payload
    if path.exists() and not overwrite and path.read_bytes() == data:
        return False, stats
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return True, stats


def payload_inventory(set_id: str) -> dict[str, Any]:
    paths = []
    for raw in PAYLOAD_ALIASES.get(set_id, []):
        p = ROOT / raw
        if p.exists() and p.is_dir():
            paths.append(p)
    dds_files = []
    for p in paths[:1]:
        dds_files.extend(sorted(p.glob("*.dds")))
    names = [p.name.lower() for p in dds_files]
    albedo = [n for n in names if "albedo" in n or "diff" in n]
    normal = [n for n in names if "normal" in n]
    roles = {
        "floor": sum(1 for n in names if "floor" in n),
        "wall": sum(1 for n in names if "wall" in n),
        "cube": sum(1 for n in names if "cube" in n),
        "cylinder": sum(1 for n in names if "cylinder" in n),
        "metal": sum(1 for n in names if "metal" in n or "brushed" in n),
    }
    material_samples = material_sample_summary(dds_files)
    return {
        "source_paths": [rel(p) for p in paths[:1]],
        "texture_count": len(dds_files),
        "albedo_count": len(albedo),
        "normal_count": len(normal),
        "roles": roles,
        "material_samples": material_samples,
        "sample_textures": [p.name for p in dds_files[:8]],
    }


def derive_colors(set_id: str) -> tuple[dict[str, tuple[int, int, int]], dict[str, Any]]:
    base = DEFAULT_SETS[set_id]
    inv = payload_inventory(set_id)
    roles = inv["roles"]
    material_samples = inv["material_samples"]
    role_average_rgb = material_samples.get("role_average_rgb", {})
    sampled_count = int(material_samples.get("sampled_color_payload_count", 0) or 0)
    material_sample_weight = 0.0 if sampled_count <= 0 else min(0.55, 0.18 + 0.06 * sampled_count)
    scene_avg = tuple(int(v) for v in material_samples.get("average_rgb", [0, 0, 0]))
    floor_sample = tuple(role_average_rgb["floor"]) if "floor" in role_average_rgb else None
    wall_samples = [
        tuple(role_average_rgb[role])
        for role in ("wall",)
        if role in role_average_rgb
    ]
    object_samples = [
        tuple(role_average_rgb[role])
        for role in ("cube", "cylinder", "other")
        if role in role_average_rgb
    ]
    metal_samples = [
        tuple(role_average_rgb[role])
        for role in ("metal", "cylinder")
        if role in role_average_rgb
    ]
    wall_sample = average_colors(wall_samples)
    object_sample = average_colors(object_samples)
    metal_sample = average_colors(metal_samples)
    role_count = max(1, sum(int(v) for v in roles.values()))
    floor_w = min(0.45, 0.10 + 0.08 * roles["floor"])
    wall_w = min(0.55, 0.12 + 0.08 * roles["wall"])
    object_w = min(0.35, 0.08 + 0.05 * (roles["cube"] + roles["cylinder"]))
    metal_w = min(0.45, 0.08 + 0.08 * roles["metal"])

    irradiance = mix(base["irradiance"], ROLE_TINTS["floor"], floor_w)
    irradiance = mix(irradiance, ROLE_TINTS["wall"], wall_w * 0.35)
    visible = mix(base["visible_background"], ROLE_TINTS["wall"], wall_w)
    specular = mix(base["specular"], ROLE_TINTS["metal"], metal_w)
    specular = mix(specular, ROLE_TINTS["cylinder"], object_w * 0.45)
    irradiance = mix_optional(irradiance, floor_sample or scene_avg, material_sample_weight * 0.48)
    visible = mix_optional(visible, wall_sample or scene_avg, material_sample_weight * 0.42)
    specular = mix_optional(specular, metal_sample or object_sample or scene_avg, material_sample_weight * 0.35)

    if set_id in {"neon_streamer_concert", "red_light_room"}:
        irradiance = mix(irradiance, ROLE_TINTS["stage"], 0.35)
        visible = darken(mix(visible, ROLE_TINTS["stage"], 0.45), 0.18)
        specular = brighten(specular, 0.12)
    elif set_id == "stadium_night_match":
        irradiance = mix(irradiance, ROLE_TINTS["exterior"], 0.35)
        visible = mix(visible, ROLE_TINTS["exterior"], 0.55)
        specular = brighten(mix(specular, ROLE_TINTS["exterior"], 0.25), 0.18)
    elif set_id == "basketball_gym_day":
        irradiance = brighten(irradiance, 0.08)
        specular = brighten(specular, 0.10)

    colors = {
        "irradiance": irradiance,
        "specular": specular,
        "visible_background": visible,
    }
    colors, scene_contract_influence = apply_room_light_contract(colors, ROOM_LIGHT_CONTRACTS[set_id])
    derivation = {
        "method": DERIVATION_METHOD,
        "base_rgb": {role: list(rgb) for role, rgb in base.items()},
        "derived_rgb": {role: list(rgb) for role, rgb in colors.items()},
        "payload_inventory": inv,
        "scene_contract_influence": scene_contract_influence,
        "weights": {
            "floor": floor_w,
            "wall": wall_w,
            "object": object_w,
            "metal": metal_w,
            "role_count": role_count,
            "material_sample": material_sample_weight,
        },
    }
    return colors, derivation


def mirror_runtime(path: Path, overwrite: bool) -> str | None:
    try:
        rel_path = path.relative_to(ROOT)
    except ValueError:
        return None
    runtime_path = ROOT / "build" / "bin" / rel_path
    runtime_path.parent.mkdir(parents=True, exist_ok=True)
    if overwrite or not runtime_path.exists() or runtime_path.read_bytes() != path.read_bytes():
        runtime_path.write_bytes(path.read_bytes())
    return rel(runtime_path)


def generate(texture_root: Path, sets: list[str], size: int, overwrite: bool, mirror: bool) -> dict[str, Any]:
    rows: list[dict[str, Any]] = []
    manifest_sets: dict[str, Any] = {}
    for set_id in sets:
        colors, derivation = derive_colors(set_id)
        set_dir = texture_root / set_id
        outputs = {}
        changed = 0
        for role, color in colors.items():
            path = set_dir / f"scene_local_{role}_proxy.dds"
            wrote, filter_stats = write_bc1_dds(path, role, color, derivation, size, overwrite)
            if wrote:
                changed += 1
            outputs[role] = {
                "path": rel(path),
                "rgb": list(color),
                "changed": wrote,
                "runtime_mirror": mirror_runtime(path, overwrite) if mirror else None,
                "filter": filter_stats,
            }
            outputs[role]["derivation_method"] = DERIVATION_METHOD
        manifest_sets[set_id] = {
            "set_id": set_id,
            "output_dir": rel(set_dir),
            "proxy_resource_shape": PROXY_RESOURCE_SHAPE,
            "derivation": derivation,
            "outputs": {role: value["path"] for role, value in outputs.items()},
            "output_metadata": {
                role: {
                    "rgb": value["rgb"],
                    "filter": value["filter"],
                    "derivation_method": value["derivation_method"],
                }
                for role, value in outputs.items()
            },
        }
        rows.append({
            "set_id": set_id,
            "output_dir": rel(set_dir),
            "derivation_method": DERIVATION_METHOD,
            "payload_texture_count": derivation["payload_inventory"]["texture_count"],
            "changed_count": changed,
            "outputs": outputs,
        })
    manifest = {
        "schema": "cortex.scene_local_environment_proxy_manifest.v1",
        "source": SOURCE_ID,
        "derivation_method": DERIVATION_METHOD,
        "sets": manifest_sets,
    }
    manifest_path = texture_root / "proxy_manifest.json"
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    if mirror:
        runtime_manifest = ROOT / "build" / "bin" / manifest_path.relative_to(ROOT)
        runtime_manifest.parent.mkdir(parents=True, exist_ok=True)
        runtime_manifest.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    write_contract_header(GENERATED_CONTRACT_HEADER)
    return {
        "schema": "cortex.scene_local_environment_proxy_assets.v1",
        "source": SOURCE_ID,
        "derivation_method": DERIVATION_METHOD,
        "proxy_resource_shape": PROXY_RESOURCE_SHAPE,
        "texture_root": rel(texture_root),
        "manifest": rel(manifest_path),
        "generated_contract_header": rel(GENERATED_CONTRACT_HEADER),
        "set_count": len(rows),
        "size": size,
        "sets": rows,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--texture-root", type=Path, default=Path("assets/textures/scene_local_proxy"))
    parser.add_argument("--set", dest="sets", action="append", choices=sorted(DEFAULT_SETS))
    parser.add_argument("--size", type=int, default=32)
    parser.add_argument("--overwrite", action="store_true")
    parser.add_argument("--no-runtime-mirror", action="store_true")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()

    texture_root = args.texture_root if args.texture_root.is_absolute() else ROOT / args.texture_root
    set_ids = args.sets or sorted(DEFAULT_SETS)
    report = generate(texture_root, set_ids, args.size, args.overwrite, not args.no_runtime_mirror)
    if args.out:
        out_path = args.out if args.out.is_absolute() else ROOT / args.out
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
