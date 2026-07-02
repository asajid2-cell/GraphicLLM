#!/usr/bin/env python3
"""
scene_gen.py -- the generative interior scene pipeline.

    python tools/scene_gen.py "a cozy rustic bedroom in warm evening light"

Flow (no per-scene hand-tuning):
  prompt
   -> COMPOSE  : a model (codex exec / claude -p / deepseek, else an offline
                 heuristic) writes a RELATIONAL scene plan -- room type + dims +
                 palette + a list of {asset|role, anchor, facing, tint}.
   -> VALIDATE : every asset is mapped to a REAL catalog id (fallback by role);
                 palette/dims defaulted; the plan is made well-formed.
   -> SOLVE    : the deterministic draftsman turns relations into exact (x,z,yaw)
                 + footprint, guaranteeing a VALID layout -- every object inside
                 the room, on the floor, non-overlapping, and clear of the front-
                 centre camera bay. Emits the engine Scene IR.
  ( -> CHECK   : validity_check() is an independent gate over the final IR. )
   -> RENDER   : render_ir.ps1 builds the "generative" recipe from the IR.
   -> CRITIQUE : claude -p reads the render and returns camera + exposure deltas;
                 the loop re-renders until the framing converges (composition is
                 already valid from the solver, so the loop only reframes).

The model is the interior DESIGNER (semantics); the solver is the DRAFTSMAN
(valid geometry). Neither hand-authors a recipe.
"""
import argparse
import json
import math
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent          # CortexEngine/
EXE = ROOT / "build" / "bin" / "CortexEngine.exe"
LOGS = ROOT / "build" / "bin" / "logs"
TOOLS = ROOT / "tools"
CATALOG_CACHE = LOGS / "catalog_dump.json"

# ----------------------------------------------------------------------------
# Catalog: the real, resolvable asset library (id + coarse role + nominal size).
# ----------------------------------------------------------------------------

def load_catalog(refresh=False):
    """Return (assets, by_role, by_id). Cached; --dump-catalog --measure gives each
    asset its REAL bounds (native_horiz/native_height) so the solver can size meshes
    by a height cap instead of blowing up tiny/odd-proportioned Kenney meshes."""
    if refresh or not CATALOG_CACHE.exists():
        out = subprocess.run(
            [str(EXE), "--dump-catalog", "--measure", "--no-launcher"],
            capture_output=True, text=True, cwd=str(EXE.parent), timeout=240)
        # stdout has leading log lines before the JSON object; slice from first '{'.
        s = out.stdout
        i = s.find("{")
        if i < 0:
            raise RuntimeError(f"catalog dump produced no JSON:\n{out.stdout[-500:]}\n{out.stderr[-500:]}")
        CATALOG_CACHE.parent.mkdir(parents=True, exist_ok=True)
        CATALOG_CACHE.write_text(s[i:], encoding="utf-8")
    data = json.loads(CATALOG_CACHE.read_text(encoding="utf-8"))
    assets = data["assets"]
    by_role, by_id = {}, {}
    for a in assets:
        by_id[a["id"].lower()] = a
        by_role.setdefault(a["role"], []).append(a)
    return assets, by_role, by_id


def role_menu(by_role, per_role=12):
    """A compact role -> [ids] menu for the composer prompt."""
    lines = []
    for role in sorted(by_role):
        ids = [a["id"] for a in by_role[role]]
        shown = ", ".join(ids[:per_role])
        if len(ids) > per_role:
            shown += ", ..."
        lines.append(f"  {role}: {shown}")
    return "\n".join(lines)


# ----------------------------------------------------------------------------
# Compose: model -> relational plan. Backends tried in order; offline fallback.
# ----------------------------------------------------------------------------

PLAN_SCHEMA = """{
  "setting": "interior",               // "interior" = a room; "exterior" = an OPEN outdoor scene
  "room_type": "living_room|bedroom|office|kitchen|dining_room|bathroom|garden|studio|...",
  "width": 6.6, "depth": 6.4,          // metres, 4.0-9.0
  "style": "modern|rustic|industrial|classic|minimal|...",
  "mood": "short phrase, e.g. 'warm evening calm'",
  "palette": {                          // RGB 0..1. tint_strength 0..1: how strongly the
    "floor": [0.5,0.43,0.36],           // palette paints the room (0.3 subtle, 0.9 'ALL <colour> everywhere')
    "wall":  [0.84,0.81,0.76],
    "accent":[0.24,0.20,0.17],
    "tint_strength": 0.35
  },
  "objects": [                          // 5-11 FLOOR-STANDING items (furniture, plants, floor lamps, rug)
    {"asset":"<a real id from the menu>", "role":"<its role>",
     "anchor":"wall_back|wall_left|wall_right|wall_front|corner_back_left|corner_back_right|corner_front_left|corner_front_right|center|center_back|left_of_center|right_of_center",
     "facing":"in|center|out|camera|<degrees>",
     "count":1,
     "tint":[r,g,b]                     // OPTIONAL; omit to keep the asset's natural colour. Set to honour a colour prompt.
    }
  ],
  "lights": [                           // 1-3 warm point lights
    {"anchor":"ceiling_center|ceiling_back|ceiling_left|ceiling_right", "color":[1.0,0.9,0.8], "intensity":6.0, "range":9.0}
  ],
  "night": false,                       // true for evening/night/moody-dark prompts: the warm
                                        // lamps become the key light (set 2-3 of them!)
  "exposure": 1.0                       // brightness of the shot: 1.2-1.4 "bright airy",
                                        // 0.8 dim/moody, 1.0 neutral
}"""

EXTERIOR_SCHEMA = """{
  "setting": "exterior",
  "scene_type": "beach|garden|forest|lake|desert|campsite|meadow|...",
  "style": "lush|sparse|wild|manicured|...",
  "mood": "short phrase, e.g. 'sunny tropical noon'",
  "environment": {
    "sun": {"azimuth_deg": 130,        // 0=behind camera, 180=backlit over the horizon; 110-145 = pleasing 3/4 light
            "elevation_deg": 45,       // 8-15 sunset/golden hour, 35-60 midday
            "color": [1.0,0.96,0.86], "intensity": 3.4},   // intensity 1.5 dim .. 4 blazing
    "sky": "sky_day|sky_sunset|sky_partly_cloudy",          // match the mood
    "fog": {"density": 0.003, "start": 10.0},               // density 0.001 crisp .. 0.02 misty
    "exposure": 1.1,                    // 0.7 moody .. 1.4 bright airy
    "ground": {"kind": "sand|grass|dirt|rock|snow", "extent": 34,   // metres, 24-50
               "color": [r,g,b]},       // OPTIONAL tint
    "water": {"enabled": true,          // a sea/lake filling the far half of the scene
              "shallow": [0.10,0.52,0.36], "deep": [0.01,0.23,0.19],  // the water COLOUR (e.g. green water)
              "roughness": 0.13, "wave": 0.04}
  },
  "objects": [                          // 6-14 entries; count>1 scatters variants
    {"query": "palm tree",              // describe ANYTHING -- exact ids from the menu are used directly,
                                        // other names are matched to the closest real/fetched/generated model
     "zone": "water|shore|midground|background|foreground_edge",
       // water: IN the sea (rocks/boats only). shore: the waterline. midground: the main
       // dressing band. background: far flanks. foreground_edge: framing props near the camera.
     "count": 3, "cluster": true,       // cluster=true -> a natural group; false -> spread out
     "size_m": 5.5,                     // real-world HEIGHT of one instance, metres
     "tint": [r,g,b]                    // OPTIONAL colour override
    }
  ],
  "lights": []                          // usually empty outdoors; a campfire = one warm point light
}"""

COMPOSE_INSTRUCTIONS = """You are a scene designer. Compose ONE scene for the prompt below.
Return ONLY a single JSON object -- no prose, no markdown fence.

FIRST decide the SETTING: an enclosed room -> use the INTERIOR schema; an outdoor place
(beach, garden, forest, lake, desert, campsite, park...) -> use the EXTERIOR schema.

INTERIOR rules:
- Pick assets ONLY from the catalog menu (use the exact ids). Put each asset's role in "role".
- Choose a sensible room_type and a plausible set of 6-10 FLOOR-STANDING objects for it:
  a focal piece against a wall (sofa/bed/desk), seating, tables, storage, a rug, 1-2 plants, a floor lamp.
- Arrange RELATIONALLY with anchors + facing; a solver computes exact coordinates, so you do NOT
  give x/z. Put the big focal piece on wall_back facing "in"; flank with seating; rug at center.
- Do NOT place tabletop props (books, mugs, vases) -- only things that stand on the floor.
- HONOUR THE PROMPT'S COLOUR/MOOD. If it names a colour (e.g. "all pink everywhere"), set the palette
  to that colour with a HIGH tint_strength (0.8-0.95) AND tint the big soft furniture that colour too.
  If no colour is named, choose a tasteful palette with LOW tint_strength (0.25-0.4) and omit most tints.
- MOOD drives light: "warm evening"/"cozy night" -> night=true + 2-3 warm lamps (intensity 7-9) +
  a warm palette (walls toward [0.55,0.42,0.32], tint_strength 0.5). "bright airy"/"sun-drenched"
  -> exposure 1.25-1.4 + near-white walls (tint_strength 0.5). "moody/dark" -> exposure 0.75-0.85.
  "well lit" -> exposure 1.2 + 2-3 lights.
- Keep the front-centre of the room open (that's where the camera looks in from).

EXTERIOR rules:
- Design the ENVIRONMENT to match the prompt: sun angle/warmth + sky + exposure carry the
  mood (sunny = high warm sun + sky_day + exposure 1.1-1.3; sunset = low sun + sky_sunset;
  misty = fog 0.01-0.02). If the prompt names a water colour (e.g. "green water"), put it in
  water.shallow/deep saturated.
- "query" may name ANYTHING ("palm tree", "boulder", "rowing boat", "beach umbrella") -- exact
  menu ids are used directly, other names get matched or fetched. Prefer menu ids when they fit.
- Compose in DEPTH: water/shore props far, the main dressing (trees, big rocks) midground on the
  FLANKS (keep the centre view open to the horizon), a couple of framing props foreground_edge.
  Vegetation stays on land; only rocks/boats go in the water. Use count 2-5 + cluster for natural
  groups, with size variety.

CATALOG MENU (role: ids):
{menu}

INTERIOR SCHEMA:
{schema}

EXTERIOR SCHEMA:
{ext_schema}

PROMPT: {prompt}
"""


def _run(cmd, timeout, stdin_devnull=True):
    kw = dict(capture_output=True, text=True, timeout=timeout)
    if stdin_devnull:
        kw["stdin"] = subprocess.DEVNULL
    return subprocess.run(cmd, **kw)


def _extract_json(text):
    """Pull the first balanced {...} JSON object out of noisy model output."""
    if not text:
        return None
    # strip code fences
    t = text.replace("```json", "```").replace("```JSON", "```")
    if "```" in t:
        parts = t.split("```")
        # prefer a fenced block that looks like JSON
        for p in parts:
            if p.strip().startswith("{"):
                t = p
                break
    start = t.find("{")
    if start < 0:
        return None
    depth, instr, esc = 0, False, False
    for i in range(start, len(t)):
        c = t[i]
        if instr:
            if esc:
                esc = False
            elif c == "\\":
                esc = True
            elif c == '"':
                instr = False
            continue
        if c == '"':
            instr = True
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                blob = t[start:i + 1]
                try:
                    return json.loads(blob)
                except Exception:
                    return None
    return None


def compose_codex(prompt_text, timeout=120):
    r = _run(["codex", "exec", "--skip-git-repo-check", prompt_text], timeout)
    if r.returncode != 0:
        return None, f"codex rc={r.returncode}: {r.stderr[-200:]}"
    return _extract_json(r.stdout), "codex"


def compose_claude(prompt_text, timeout=120):
    r = _run(["claude", "-p", prompt_text], timeout)
    if r.returncode != 0:
        return None, f"claude rc={r.returncode}: {r.stderr[-200:]}"
    return _extract_json(r.stdout), "claude"


def compose_deepseek(prompt_text, timeout=120):
    key = os.environ.get("DEEPSEEK_API_KEY")
    if not key:
        return None, "no DEEPSEEK_API_KEY"
    try:
        import urllib.request
        body = json.dumps({
            "model": "deepseek-chat",
            "messages": [{"role": "user", "content": prompt_text}],
            "temperature": 0.4,
            "stream": False,
        }).encode()
        req = urllib.request.Request(
            "https://api.deepseek.com/chat/completions", data=body,
            headers={"Authorization": f"Bearer {key}", "Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            data = json.loads(resp.read().decode())
        txt = data["choices"][0]["message"]["content"]
        return _extract_json(txt), "deepseek"
    except Exception as e:
        return None, f"deepseek err: {e}"


# --- offline heuristic composer: the model-unavailable graceful-degrade path ---

ROOM_KIT = {
    "living_room": ["sofa", "seating", "coffee_table", "rug", "storage", "plant", "lamp"],
    "bedroom":     ["bed", "table", "table", "storage", "rug", "plant", "lamp"],
    "office":      ["desk", "seating", "storage", "rug", "plant", "lamp"],
    "kitchen":     ["appliance", "appliance", "appliance", "storage", "storage", "table"],
    "dining_room": ["table", "seating", "seating", "storage", "rug", "plant"],
    "bathroom":    ["bathroom", "bathroom", "storage", "plant"],
    "studio":      ["sofa", "seating", "coffee_table", "desk", "rug", "plant", "lamp"],
}
ROOM_ANCHORS = {
    "sofa": "wall_back", "bed": "wall_back", "desk": "wall_back",
    "coffee_table": "center", "rug": "center", "table": "center",
    "seating": "flank", "storage": "wall_left", "appliance": "wall_back",
    "plant": "corner", "lamp": "corner", "bathroom": "wall_right",
}
COLOR_WORDS = {
    "pink": [0.95, 0.45, 0.62], "red": [0.75, 0.18, 0.18], "crimson": [0.7, 0.1, 0.15],
    "blue": [0.30, 0.42, 0.72], "navy": [0.16, 0.22, 0.44], "teal": [0.18, 0.52, 0.55],
    "green": [0.30, 0.55, 0.32], "emerald": [0.13, 0.55, 0.38], "sage": [0.55, 0.62, 0.48],
    "yellow": [0.90, 0.78, 0.30], "gold": [0.80, 0.66, 0.30], "orange": [0.90, 0.55, 0.25],
    "purple": [0.55, 0.35, 0.68], "violet": [0.52, 0.40, 0.72], "lavender": [0.72, 0.66, 0.85],
    "white": [0.90, 0.90, 0.92], "cream": [0.90, 0.85, 0.74], "black": [0.14, 0.14, 0.16],
    "grey": [0.55, 0.55, 0.58], "gray": [0.55, 0.55, 0.58], "brown": [0.42, 0.30, 0.20],
    "beige": [0.80, 0.72, 0.60], "turquoise": [0.25, 0.62, 0.62], "mint": [0.65, 0.85, 0.72],
}
STYLE_WORDS = ["modern", "rustic", "industrial", "classic", "minimal", "cozy", "luxury", "vintage"]


# --- exterior offline heuristics -------------------------------------------

EXTERIOR_WORDS = {"beach", "garden", "forest", "lake", "desert", "campsite", "camp",
                  "meadow", "park", "outdoor", "outside", "island", "jungle", "woods",
                  "shore", "coast", "seaside", "riverbank", "clearing", "backyard"}

EXT_SCENE_KITS = {
    # scene: (ground_kind, water, [(query, zone, count, cluster, size_m)])
    "beach": ("sand", True, [
        ("palm tree", "midground", 3, True, 5.5), ("palm tree", "midground", 2, True, 4.5),
        ("large rock", "water", 3, True, 1.6), ("rock", "shore", 2, False, 0.7),
        ("bush", "midground", 2, True, 1.1), ("grass tuft", "foreground_edge", 2, False, 0.6),
    ]),
    "garden": ("grass", False, [
        ("tree", "background", 3, False, 5.0), ("bush", "midground", 4, True, 1.1),
        ("flowers", "midground", 5, True, 0.45), ("fence", "background", 4, False, 1.0),
        ("rock", "foreground_edge", 2, False, 0.6),
    ]),
    "forest": ("dirt", False, [
        ("pine tree", "background", 4, True, 6.5), ("pine tree", "midground", 3, True, 5.5),
        ("tree stump", "midground", 2, False, 0.7), ("mushroom", "foreground_edge", 3, True, 0.35),
        ("rock", "midground", 3, True, 0.9), ("fern", "midground", 3, True, 0.8),
    ]),
    "lake": ("grass", True, [
        ("tree", "midground", 4, True, 5.5), ("rock", "water", 2, True, 1.2),
        ("bush", "midground", 3, True, 1.0), ("canoe", "shore", 1, False, 1.0),
        ("grass tuft", "foreground_edge", 3, False, 0.6),
    ]),
    "desert": ("sand", False, [
        ("large rock", "midground", 4, True, 1.8), ("rock", "foreground_edge", 3, False, 0.8),
        ("dead tree", "midground", 2, False, 3.5), ("grass tuft", "midground", 3, False, 0.5),
    ]),
    "campsite": ("grass", False, [
        ("tent", "midground", 1, False, 2.0), ("campfire", "midground", 1, False, 0.7),
        ("log", "midground", 2, True, 0.6), ("pine tree", "background", 4, True, 6.0),
        ("rock", "foreground_edge", 2, False, 0.7),
    ]),
    "meadow": ("grass", False, [
        ("tree", "background", 3, False, 5.5), ("flowers", "midground", 6, True, 0.45),
        ("grass tuft", "midground", 5, False, 0.6), ("rock", "midground", 2, False, 0.8),
    ]),
}


def prompt_is_exterior(prompt):
    words = set(w.strip(",.") for w in prompt.lower().split())
    return bool(words & EXTERIOR_WORDS)


def compose_offline_exterior(prompt, by_role):
    p = prompt.lower()
    scene = next((s for s in EXT_SCENE_KITS if s in p), None)
    if scene is None:
        scene = "beach" if ("water" in p or "sea" in p or "ocean" in p) else "meadow"
    ground, water_on, kit = EXT_SCENE_KITS[scene]
    named = next((COLOR_WORDS[w] for w in COLOR_WORDS if w in p), None)
    sunset = any(w in p for w in ("sunset", "evening", "golden", "dusk"))
    misty = any(w in p for w in ("fog", "mist", "misty", "hazy"))
    env = {
        "sun": {"azimuth_deg": 150 if sunset else 130,
                "elevation_deg": 11 if sunset else 46,
                "color": [1.0, 0.62, 0.34] if sunset else [1.0, 0.96, 0.86],
                "intensity": 2.4 if sunset else 3.4},
        "sky": "sky_sunset" if sunset else ("sky_partly_cloudy" if misty else "sky_day"),
        "fog": {"density": 0.014 if misty else 0.003, "start": 6.0 if misty else 10.0},
        "exposure": 0.95 if sunset else 1.1,
        "ground": {"kind": ground, "extent": 34},
        "water": {"enabled": water_on,
                  "shallow": (named if (named and water_on) else [0.10, 0.52, 0.36]),
                  "deep": [0.01, 0.23, 0.19], "roughness": 0.13, "wave": 0.04},
    }
    if named and water_on:
        env["water"]["deep"] = [named[0] * 0.12, named[1] * 0.45, named[2] * 0.4]
    objs = [{"query": q, "zone": z, "count": c, "cluster": cl, "size_m": s}
            for (q, z, c, cl, s) in kit]
    return {
        "setting": "exterior", "scene_type": scene, "style": "natural",
        "mood": "sunset" if sunset else ("misty" if misty else "sunny"),
        "environment": env, "objects": objs, "lights": [],
    }, "offline"


def compose_offline(prompt, by_role):
    if prompt_is_exterior(prompt):
        return compose_offline_exterior(prompt, by_role)
    p = prompt.lower()
    room = "living_room"
    for rt in ["living_room", "bedroom", "office", "kitchen", "dining_room", "bathroom", "studio"]:
        if rt.replace("_", " ") in p or rt.replace("_", "") in p or rt.split("_")[0] in p:
            room = rt
            break
    if "living" in p:
        room = "living_room"
    if "dining" in p:
        room = "dining_room"
    style = next((s for s in STYLE_WORDS if s in p), "modern")
    named = next((COLOR_WORDS[w] for w in COLOR_WORDS if w in p), None)
    words = set(p.replace(",", " ").split())
    strong = named is not None and bool(
        {"everywhere", "all", "entirely", "fully", "throughout", "completely"} & words)
    if named:
        c = named
        wall = [min(1, c[0] * 1.05 + 0.05), min(1, c[1] * 1.05 + 0.05), min(1, c[2] * 1.05 + 0.05)]
        floor = [c[0] * 0.9, c[1] * 0.9, c[2] * 0.9]
        accent = [c[0] * 0.7, c[1] * 0.6, c[2] * 0.65]
        ts = 0.9 if strong else 0.55
    else:
        wall, floor, accent = [0.84, 0.81, 0.76], [0.5, 0.43, 0.36], [0.24, 0.2, 0.17]
        ts = 0.3
    objs = []
    flank = 0
    for role in ROOM_KIT.get(room, ROOM_KIT["living_room"]):
        cand = by_role.get(role)
        if not cand:
            continue
        asset = cand[0]["id"]
        anchor = ROOM_ANCHORS.get(role, "center")
        facing = "in"
        count = 1
        if anchor == "flank":
            anchor = "corner_front_left" if flank == 0 else "corner_front_right"
            facing = "center"
            flank += 1
        elif anchor == "corner":
            anchor = "corner_back_right" if role == "plant" else "corner_back_left"
        obj = {"asset": asset, "role": role, "anchor": anchor, "facing": facing, "count": count}
        if named and role in ("sofa", "bed", "seating", "rug"):
            obj["tint"] = c
        objs.append(obj)
    night = any(w in p for w in ("evening", "night", "candlelit", "dusk"))
    bright = any(w in p for w in ("bright", "airy", "sunny", "well lit", "well-lit", "light-filled"))
    moody = any(w in p for w in ("moody", "dark", "dim"))
    lights = [{"anchor": "ceiling_center", "color": [1.0, 0.92, 0.82], "intensity": 6.0, "range": 9.0}]
    if night:
        lights = [{"anchor": a, "color": [1.0, 0.78, 0.52], "intensity": 8.0, "range": 8.0}
                  for a in ("ceiling_center", "ceiling_back", "ceiling_left")]
    return {
        "room_type": room, "width": 6.6, "depth": 6.4, "style": style,
        "mood": (f"{style} {list(COLOR_WORDS.keys())[0]}" if named else style),
        "palette": {"floor": floor, "wall": wall, "accent": accent, "tint_strength": ts},
        "night": night,
        "exposure": 1.3 if bright else (0.82 if moody else 1.0),
        "objects": objs,
        "lights": lights,
    }, "offline"


def compose(prompt, by_role, backends, verbose=True):
    menu = role_menu(by_role)
    prompt_text = COMPOSE_INSTRUCTIONS.format(menu=menu, schema=PLAN_SCHEMA,
                                              ext_schema=EXTERIOR_SCHEMA, prompt=prompt)
    fns = {"codex": compose_codex, "claude": compose_claude, "deepseek": compose_deepseek}
    for b in backends:
        if b == "offline":
            continue
        fn = fns.get(b)
        if not fn:
            continue
        t0 = time.time()
        plan, info = fn(prompt_text)
        dt = time.time() - t0
        if plan and isinstance(plan, dict) and plan.get("objects"):
            if verbose:
                print(f"[compose] {b} ok ({dt:.1f}s, {len(plan['objects'])} objects)")
            plan["_backend"] = b
            return plan
        if verbose:
            print(f"[compose] {b} failed: {info}")
    plan, _ = compose_offline(prompt, by_role)
    if verbose:
        kind = plan.get("room_type") or plan.get("scene_type", "scene")
        print(f"[compose] offline heuristic ({kind}, {len(plan['objects'])} objects)")
    plan["_backend"] = "offline"
    return plan


# ----------------------------------------------------------------------------
# Validate / repair: make every asset real; default palette + dims.
# ----------------------------------------------------------------------------

def _clamp(v, lo, hi):
    return max(lo, min(hi, v))


def _rgb(v, dflt):
    if isinstance(v, list) and len(v) >= 3:
        try:
            return [_clamp(float(v[0]), 0, 1), _clamp(float(v[1]), 0, 1), _clamp(float(v[2]), 0, 1)]
        except Exception:
            return dflt
    return dflt


def validate_plan(plan, by_role, by_id, verbose=True):
    if (plan.get("setting") or "").lower() == "exterior" or "environment" in plan:
        plan["setting"] = "exterior"
        return validate_plan_exterior(plan, by_role, by_id, verbose=verbose)
    plan["setting"] = "interior"
    return validate_plan_interior(plan, by_role, by_id, verbose=verbose)


def validate_plan_interior(plan, by_role, by_id, verbose=True):
    repairs = []
    plan.setdefault("room_type", "living_room")
    plan["width"] = _clamp(float(plan.get("width", 6.6) or 6.6), 4.0, 9.0)
    plan["depth"] = _clamp(float(plan.get("depth", 6.4) or 6.4), 4.0, 9.0)
    pal = plan.get("palette") or {}
    plan["palette"] = {
        "floor": _rgb(pal.get("floor"), [0.5, 0.43, 0.36]),
        "wall": _rgb(pal.get("wall"), [0.84, 0.81, 0.76]),
        "accent": _rgb(pal.get("accent"), [0.24, 0.2, 0.17]),
        "tint_strength": _clamp(float(pal.get("tint_strength", 0.35) or 0.35), 0.0, 0.97),
    }
    plan["night"] = bool(plan.get("night", False))
    try:
        plan["exposure"] = _clamp(float(plan.get("exposure", 1.0) or 1.0), 0.6, 1.5)
    except Exception:
        plan["exposure"] = 1.0
    good = []
    for o in plan.get("objects", []):
        if not isinstance(o, dict):
            continue
        asset = (o.get("asset") or "").strip()
        role = (o.get("role") or "").strip().lower()
        real = by_id.get(asset.lower())
        if not real:
            # fallback by role -> pick the first real asset of that role
            cands = by_role.get(role)
            if cands:
                real = cands[0]
                repairs.append(f"asset '{asset}' -> '{real['id']}' (by role {role})")
            else:
                # last resort: try to infer role from the requested name
                for rname, lst in by_role.items():
                    if rname in asset.lower():
                        real = lst[0]
                        repairs.append(f"asset '{asset}' -> '{real['id']}' (inferred {rname})")
                        break
        if not real:
            repairs.append(f"DROP unresolved asset '{asset}' (role '{role}')")
            continue
        o["asset"] = real["id"]
        o["role"] = real["role"]
        o["nominal"] = real.get("nominal_footprint_m", 0.8)
        o["native_horiz"] = real.get("native_horiz")
        o["native_height"] = real.get("native_height")
        try:
            o["count"] = int(_clamp(int(o.get("count", 1) or 1), 1, 5))
        except Exception:
            o["count"] = 1
        if "tint" in o:
            o["tint"] = _rgb(o.get("tint"), None)
            if o["tint"] is None:
                del o["tint"]
        good.append(o)
    plan["objects"] = good
    if verbose and repairs:
        print(f"[validate] {len(repairs)} repair(s):")
        for r in repairs[:12]:
            print(f"    - {r}")
    return plan, repairs


# ----------------------------------------------------------------------------
# Exterior validate: the asset-resolution LADDER. Every "query" becomes a real,
# loadable model: exact id -> catalog keyword match -> role synonym -> fetched
# cache / Sketchfab fetch -> procgen -> drop. Never crashes, always renders.
# ----------------------------------------------------------------------------

NATURE_SOURCES = {"kenney_nature_kit", "naturalistic_showcase", "fetched"}

# query keyword -> (role, preferred id substring)
QUERY_ROLE_MAP = [
    (("palm",), ("tree", "palm")),
    (("pine", "conifer", "fir", "spruce"), ("tree", "pine")),
    (("tree", "trunk", "stump", "log", "driftwood"), ("tree", None)),
    (("boulder", "rock", "stone", "pebble", "cliff"), ("rock", None)),
    (("bush", "shrub", "hedge", "fern", "foliage", "plant"), ("bush", None)),
    (("grass", "reed", "tuft"), ("grass", None)),
    (("flower", "bloom", "tulip", "rose", "lily"), ("flower", None)),
    (("fence", "gate", "wall"), ("fence", None)),
    (("path", "bridge", "walkway"), ("path", None)),
    (("tent", "campfire", "fire", "canoe", "boat", "kayak", "raft"), ("camp", None)),
    (("mushroom", "toadstool"), ("bush", "mushroom")),
    (("cactus",), ("bush", "cactus")),
]

_EXT_SIZE = {  # role -> (default height m, min h, max h)
    "tree": (5.0, 2.5, 8.5), "rock": (0.9, 0.3, 2.6), "bush": (1.0, 0.4, 1.8),
    "grass": (0.55, 0.3, 1.0), "flower": (0.45, 0.25, 0.9), "fence": (1.0, 0.6, 1.6),
    "path": (0.3, 0.1, 0.6), "camp": (1.6, 0.5, 3.0), "plant": (1.2, 0.4, 2.2),
    "decor": (0.8, 0.3, 1.8), "misc": (1.0, 0.3, 3.0),
}

# Max FOOTPRINT per role: flat meshes (a 0.26m-tall slab rock) would explode to 6m-wide
# pancakes if only the target HEIGHT drove the scale.
_EXT_FOOT_MAX = {
    "tree": 5.5, "rock": 2.3, "bush": 1.9, "grass": 1.2, "flower": 0.9,
    "fence": 2.6, "path": 2.2, "camp": 3.2, "plant": 2.2, "decor": 2.0, "misc": 2.6,
}


def _rng_for(*parts):
    import hashlib
    import random
    h = hashlib.md5("|".join(str(p) for p in parts).encode()).hexdigest()
    return random.Random(int(h[:12], 16))


def resolve_query(query, by_role, by_id, rng, verbose=True):
    """Ladder steps 1-3: match a free-text query to a real catalog asset."""
    q = (query or "").lower().strip()
    if not q:
        return None, "empty"
    if q in by_id:
        return by_id[q], "exact"
    words = [w for w in "".join(c if c.isalnum() else " " for c in q).split() if len(w) > 2]
    # keyword scoring over ids (nature sources preferred for exterior scenes)
    best, best_score = [], 0
    for a in by_id.values():
        idl = a["id"].lower()
        score = sum(2 for w in words if w in idl)
        if score and a.get("source") in NATURE_SOURCES:
            score += 1
        if score > best_score:
            best, best_score = [a], score
        elif score == best_score and score > 0:
            best.append(a)
    if best_score >= 2:
        # shortest ids first: 'rock_largeA' over 'cliff_cornerInnerLarge_rock';
        # rng picks among the ties for variety
        shortest = min(len(a["id"]) for a in best)
        best = [a for a in best if len(a["id"]) <= shortest + 2]
        return rng.choice(best), f"keyword({best_score})"
    # role synonyms
    for keys, (role, sub) in QUERY_ROLE_MAP:
        if any(k in q for k in keys):
            cands = by_role.get(role, [])
            if sub:
                subbed = [a for a in cands if sub in a["id"].lower()]
                cands = subbed or cands
            cands = [a for a in cands if a.get("source") in NATURE_SOURCES] or cands
            if cands:
                return rng.choice(cands), f"role({role})"
    return None, "unmatched"


def resolve_query_ladder(query, by_role, by_id, rng, fetch_budget, verbose=True):
    """Full ladder incl. fetch (Sketchfab) + procgen for out-of-corpus queries."""
    asset, how = resolve_query(query, by_role, by_id, rng, verbose=verbose)
    if asset is not None:
        return asset, how
    # ladder step 4: live fetch (tools/asset_fetch.py); refreshes the catalog on success
    if fetch_budget.get("left", 0) > 0:
        try:
            import asset_fetch
            fetched_id = asset_fetch.ensure(query, verbose=verbose)
        except Exception as e:
            fetched_id = None
            if verbose:
                print(f"[ladder] fetch '{query}' unavailable: {e}")
        if fetched_id:
            fetch_budget["left"] -= 1
            fetch_budget["dirty"] = True
            _, by_role2, by_id2 = load_catalog(refresh=True)
            by_role.clear(); by_role.update(by_role2)
            by_id.clear(); by_id.update(by_id2)
            if fetched_id.lower() in by_id:
                return by_id[fetched_id.lower()], "fetched"
    # ladder step 5: procedural generation (rocks and other simple nature forms)
    try:
        import procgen
        gen_id = procgen.ensure(query, verbose=verbose)
    except Exception:
        gen_id = None
    if gen_id:
        _, by_role2, by_id2 = load_catalog(refresh=True)
        by_role.clear(); by_role.update(by_role2)
        by_id.clear(); by_id.update(by_id2)
        if gen_id.lower() in by_id:
            return by_id[gen_id.lower()], "procgen"
    return None, "dropped"


def validate_plan_exterior(plan, by_role, by_id, verbose=True):
    repairs = []
    env = plan.get("environment") or {}
    sun = env.get("sun") or {}
    ground = env.get("ground") or {}
    water = env.get("water") or {}
    env = {
        "sun": {
            "azimuth_deg": _clamp(float(sun.get("azimuth_deg", 130) or 130), 0, 360),
            "elevation_deg": _clamp(float(sun.get("elevation_deg", 45) or 45), 4, 75),
            "color": _rgb(sun.get("color"), [1.0, 0.96, 0.86]),
            "intensity": _clamp(float(sun.get("intensity", 3.2) or 3.2), 0.5, 5.0),
        },
        "sky": env.get("sky") if env.get("sky") in ("sky_day", "sky_sunset", "sky_partly_cloudy") else None,
        "fog": {
            "density": _clamp(float((env.get("fog") or {}).get("density", 0.003) or 0.003), 0.0, 0.05),
            "start": _clamp(float((env.get("fog") or {}).get("start", 10.0) or 10.0), 0.0, 30.0),
        },
        "exposure": _clamp(float(env.get("exposure", 1.1) or 1.1), 0.5, 1.6),
        "ground": {
            "kind": ground.get("kind") if ground.get("kind") in ("sand", "grass", "dirt", "rock", "snow") else "grass",
            "extent": _clamp(float(ground.get("extent", 34) or 34), 22, 55),
        },
        "water": {
            "enabled": bool(water.get("enabled", False)),
            "shallow": _rgb(water.get("shallow"), [0.10, 0.52, 0.36]),
            "deep": _rgb(water.get("deep"), [0.01, 0.23, 0.19]),
            "roughness": _clamp(float(water.get("roughness", 0.13) or 0.13), 0.03, 0.4),
            "wave": _clamp(float(water.get("wave", 0.04) or 0.04), 0.01, 0.09),
        },
    }
    if (plan.get("environment") or {}).get("ground", {}).get("color"):
        env["ground"]["color"] = _rgb(plan["environment"]["ground"]["color"], None) or None
    if env["sky"] is None:
        env["sky"] = "sky_sunset" if env["sun"]["elevation_deg"] < 18 else "sky_day"
    plan["environment"] = env

    fetch_budget = {"left": 4, "dirty": False}
    good = []
    for i, o in enumerate(plan.get("objects", [])):
        if not isinstance(o, dict):
            continue
        query = (o.get("query") or o.get("asset") or "").strip()
        rng = _rng_for(plan.get("scene_type"), query, i)
        asset, how = resolve_query_ladder(query, by_role, by_id, rng, fetch_budget, verbose=verbose)
        if asset is None:
            repairs.append(f"DROP unresolvable '{query}'")
            continue
        if how != "exact":
            repairs.append(f"'{query}' -> '{asset['id']}' ({how})")
        role = asset["role"]
        dflt_h, min_h, max_h = _EXT_SIZE.get(role, _EXT_SIZE["misc"])
        try:
            size_m = _clamp(float(o.get("size_m", dflt_h) or dflt_h), min_h, max_h)
        except Exception:
            size_m = dflt_h
        entry = {
            "asset": asset["id"], "role": role, "zone": (o.get("zone") or "midground").lower(),
            "count": int(_clamp(int(o.get("count", 1) or 1), 1, 8)),
            "cluster": bool(o.get("cluster", False)), "size_m": size_m,
            "native_horiz": asset.get("native_horiz"), "native_height": asset.get("native_height"),
        }
        if "tint" in o:
            t = _rgb(o.get("tint"), None)
            if t:
                entry["tint"] = t
        good.append(entry)
    plan["objects"] = good
    if verbose and repairs:
        print(f"[validate-ext] {len(repairs)} note(s):")
        for r in repairs[:12]:
            print(f"    - {r}")
    return plan, repairs


# ----------------------------------------------------------------------------
# Solve: relational plan -> engine Scene IR with valid exact coordinates.
# ----------------------------------------------------------------------------

# Roles that don't participate in overlap (flat floor coverings) or that are decor
# small enough to sit anywhere.
FLAT_ROLES = {"rug"}
# Tall / view-blocking roles: kept out of the deep-centre so they don't wall off the shot.
TALL_ROLES = {"sofa", "bed", "storage", "appliance", "electronics"}

# Front-centre camera bay: the showcase hero camera stands at ~(0.15,2.05) and
# dollies back through here, so no object centre may land in this strip.
CAM_BAY_X = 0.95
CAM_BAY_Z = 1.15   # z >= this (towards +Z front) and |x| <= CAM_BAY_X is reserved

# Per-role real-world sizing: (target_max_height_m, min_footprint_m, max_footprint_m).
# The engine normalizes an asset's largest horizontal extent to the IR "foot", which
# blows up tiny meshes (a chair authored at 0.2 m -> 0.7 m foot = 3.5x = a 1.6 m giant).
# So the solver picks foot = min(role footprint, the foot that keeps HEIGHT <= max_h)
# using the measured aspect, then clamps to a sane footprint range.
ROLE_SIZE = {
    "sofa": (1.00, 1.6, 2.6), "bed": (1.15, 1.7, 2.4), "seating": (1.05, 0.45, 0.85),
    "desk": (1.10, 1.0, 1.6), "table": (0.85, 0.7, 1.4), "coffee_table": (0.55, 0.7, 1.2),
    "storage": (2.00, 0.7, 1.3), "appliance": (1.90, 0.6, 1.05), "electronics": (1.40, 0.6, 1.3),
    "plant": (1.70, 0.4, 0.9), "lamp": (1.75, 0.28, 0.6), "rug": (0.06, 1.6, 2.8),
    "bathroom": (1.20, 0.5, 1.05), "decor": (0.95, 0.3, 0.85), "accessory": (0.5, 0.2, 0.45),
    "misc": (1.20, 0.4, 1.05),
}


def size_foot(role, native_horiz, native_height, nominal):
    """Footprint (m) to pass the engine so the asset renders at a sane real size."""
    max_h, min_f, max_f = ROLE_SIZE.get(role, ROLE_SIZE["misc"])
    foot = min(nominal, max_f)
    if native_horiz and native_height and native_height > 1e-4:
        height_cap_foot = max_h * (native_horiz / native_height)   # foot that yields max_h tall
        foot = min(foot, height_cap_foot)
    return _clamp(foot, min_f, max_f)


def _yaw_to_face(px, pz, dirx, dirz):
    """Degrees so an object at (px,pz) faces the world direction (dirx,dirz)."""
    if abs(dirx) < 1e-6 and abs(dirz) < 1e-6:
        return 0.0
    return math.degrees(math.atan2(dirx, dirz))


def _resolve_anchor(anchor, hx, hz, foot, i, n):
    """Return (x, z, into_dir) base slot for an anchor. into_dir points into the room."""
    m = foot / 2.0 + 0.10                  # keep the body off the wall
    # spread multiple items along their wall's free axis
    def spread(span):
        if n <= 1:
            return 0.0
        return (-span + 2 * span * i / (n - 1))
    a = anchor
    if a == "wall_back":
        return spread(hx * 0.62), -(hz - m), (0.0, 1.0)
    if a == "wall_front":
        return spread(hx * 0.62), (hz - m), (0.0, -1.0)
    if a == "wall_left":
        return -(hx - m), spread(hz * 0.55), (1.0, 0.0)
    if a == "wall_right":
        return (hx - m), spread(hz * 0.55), (-1.0, 0.0)
    if a == "corner_back_left":
        return -(hx - m), -(hz - m), (0.6, 0.8)
    if a == "corner_back_right":
        return (hx - m), -(hz - m), (-0.6, 0.8)
    if a == "corner_front_left":
        return -(hx - m), (hz - m) * 0.7, (0.5, -0.5)
    if a == "corner_front_right":
        return (hx - m), (hz - m) * 0.7, (-0.5, -0.5)
    if a == "center_back":
        return spread(hx * 0.4), -hz * 0.42, (0.0, 1.0)
    if a == "left_of_center":
        return -hx * 0.40, spread(hz * 0.3), (1.0, 0.0)
    if a == "right_of_center":
        return hx * 0.40, spread(hz * 0.3), (-1.0, 0.0)
    # center (default)
    return spread(hx * 0.35), -hz * 0.05, (0.0, 1.0)


def _facing_yaw(facing, x, z, into_dir):
    f = str(facing).strip().lower()
    try:                                   # explicit degrees
        return float(f)
    except Exception:
        pass
    if f in ("center", "in", ""):
        if f == "in":
            return _yaw_to_face(x, z, into_dir[0], into_dir[1])
        return _yaw_to_face(x, z, -x, -z)          # face room centre
    if f == "out" or f == "wall":
        return _yaw_to_face(x, z, -into_dir[0], -into_dir[1])
    if f == "camera":
        return _yaw_to_face(x, z, 0.0, 1.0)        # face +Z (the camera)
    return _yaw_to_face(x, z, into_dir[0], into_dir[1])


def _in_cam_bay(x, z, r):
    return (abs(x) - r) < CAM_BAY_X and (z + r) > CAM_BAY_Z


def solve(plan):
    if plan.get("setting") == "exterior":
        return solve_exterior(plan)
    return solve_interior(plan)


# Exterior world frame: camera at (0, 3.0, 10.5) looking down -Z. Land surface y=0,
# the sea (when enabled) fills z <= shoreline. Zones are depth bands; the CENTRE view
# corridor (|x| small) stays clear of tall objects so the eye travels to the horizon.
TALL_EXT_M = 2.2          # objects at least this tall count as view-blockers
WATER_ROLES = {"rock", "camp"}   # the only things that belong IN the water


def _ext_zones(E, shoreZ, water_on):
    if water_on:
        return {
            "water": (shoreZ - 7.0, shoreZ - 1.2),
            "shore": (shoreZ - 0.3, shoreZ + 2.2),
            "midground": (shoreZ + 2.2, 1.5),             # ends well short of the camera
            "background": (shoreZ + 1.0, shoreZ + 4.5),   # far flanks (x pushed wide below)
            "foreground_edge": (3.0, 5.5),
        }
    return {
        "water": (-0.30 * E, -0.5),                       # no sea: degrade to midground
        "shore": (-0.30 * E, -0.5),
        "midground": (-0.30 * E, -0.5),
        "background": (-0.45 * E, -0.24 * E),
        "foreground_edge": (3.0, 5.5),
    }


def solve_exterior(plan):
    env = plan["environment"]
    E = env["ground"]["extent"]
    water_on = env["water"]["enabled"]
    shoreZ = -0.15 * E if water_on else None
    zones = _ext_zones(E, shoreZ if water_on else 0.0, water_on)
    xmax = 0.44 * E
    placed = []                     # (x, z, r)
    ir_objects = []
    dropped = []

    def est_height(o, foot):
        nh, nv = o.get("native_horiz"), o.get("native_height")
        if nh and nv and nh > 1e-4:
            return foot * (nv / nh)
        return foot

    def fits(x, z, r, tall, zone):
        if abs(x) + r > xmax or z < -0.46 * E or z > 6.8:
            return False
        if tall and z > (shoreZ + 1.0 if water_on else -0.40 * E) and abs(x) < 0.10 * E and zone != "water":
            return False                      # keep the centre corridor open to the horizon
        for (px, pz, pr) in placed:
            if (x - px) ** 2 + (z - pz) ** 2 < (r + pr) ** 2 * 0.92:
                return False
        return True

    groups = sorted(plan["objects"], key=lambda o: -(o.get("size_m", 1.0)))
    for gi, o in enumerate(groups):
        zone = o["zone"] if o["zone"] in zones else "midground"
        if zone == "water" and o["role"] not in WATER_ROLES:
            zone = "shore"                    # vegetation etc. never goes in the sea
        if zone == "foreground_edge" and o.get("size_m", 1.0) > 1.6:
            zone = "midground"                # nothing huge right in front of the lens
        z_lo, z_hi = zones[zone]
        rng = _rng_for("solve", plan.get("scene_type"), o["asset"], gi)
        side = 1 if (gi % 2 == 0) else -1
        n = o["count"]

        def pick_center():
            if zone in ("midground", "background", "foreground_edge"):
                return (side * rng.uniform(0.13 * E, 0.30 * E), rng.uniform(z_lo, z_hi))
            return (rng.uniform(-0.30 * E, 0.30 * E), rng.uniform(z_lo, z_hi))
        cx, cz = pick_center() if (o.get("cluster") and n > 1) else (0.0, 0.0)
        for i in range(n):
            size = o["size_m"] * rng.uniform(0.82, 1.18)
            nh, nv = o.get("native_horiz"), o.get("native_height")
            aspect = (nh / nv) if (nh and nv and nv > 1e-4) else 0.8
            aspect = _clamp(aspect, 0.2, 5.0)
            foot = _clamp(size * aspect, 0.25, _EXT_FOOT_MAX.get(o["role"], 2.6))
            # trees collide by trunk, not canopy: fronds may naturally overlap
            r = max(foot / 2.0, 0.3) * (0.55 if o["role"] == "tree" else 1.0)
            tall = est_height(o, foot) >= TALL_EXT_M
            ok = False
            for attempt in range(22):
                widen = 1.0 + attempt * 0.18       # progressively relax the search
                if o.get("cluster") and n > 1:
                    if attempt in (8, 15):
                        cx, cz = pick_center()      # crowded cluster: try a new spot
                    x = cx + rng.gauss(0, max(1.0, r * 1.6) * widen)
                    z = _clamp(cz + rng.gauss(0, max(0.9, r * 1.2) * widen), z_lo, z_hi)
                elif zone in ("midground", "background"):
                    ax = rng.uniform(0.10 * E if tall else 0.02 * E, min(0.36 * E * widen, 0.42 * E))
                    x = (side if rng.random() < 0.7 else -side) * ax
                    z = rng.uniform(z_lo, z_hi)
                elif zone == "foreground_edge":
                    x = (side if i % 2 == 0 else -side) * rng.uniform(0.16 * E, 0.34 * E)
                    z = rng.uniform(z_lo, z_hi)
                else:   # water / shore: spread across the view
                    x = rng.uniform(-0.34 * E, 0.34 * E)
                    z = rng.uniform(z_lo, z_hi)
                if water_on and o["role"] in VEGETATION_ROLES and z < shoreZ + 0.4:
                    z = shoreZ + 0.4 + abs(rng.gauss(0, 0.8))   # plants stay on dry land
                if fits(x, z, r, tall, zone):
                    ok = True
                    break
            if not ok:
                dropped.append(o["asset"])
                continue
            yaw = rng.uniform(-24, 24) if o["role"] == "camp" else rng.uniform(0, 360)
            entry = {"asset": o["asset"], "x": round(x, 3), "z": round(z, 3),
                     "yaw": round(yaw, 1), "foot": round(foot, 3)}
            if water_on and z < shoreZ:
                # under water the seabed ramps down (engine: shoreline y=0 -> -2.5 at the
                # far ground edge); sink the base onto it so rocks sit IN the sea, not
                # bobbing at the surface
                ground_far = -(1.9 * E + 10.0)
                slope = 2.5 / max(shoreZ - ground_far, 1e-3)
                entry["y"] = round(-min((shoreZ - z) * slope + 0.04, 2.2), 3)
            if "tint" in o:
                entry["tint"] = o["tint"]
            ir_objects.append(entry)
            placed.append((x, z, r))

    ir_env = json.loads(json.dumps(env))     # deep copy
    if water_on:
        ir_env["water"]["from_z"] = round(shoreZ, 2)
        ir_env["water"]["level"] = 0.05
    ir_lights = []
    for l in plan.get("lights", []) or []:
        if all(isinstance(l.get(k), (int, float)) for k in ("x", "y", "z")):
            ir_lights.append({
                "type": "point", "x": float(l["x"]), "y": float(l["y"]), "z": float(l["z"]),
                "color": _rgb(l.get("color"), [1.0, 0.75, 0.45]),
                "intensity": _clamp(float(l.get("intensity", 5.0) or 5.0), 1.0, 14.0),
                "range": _clamp(float(l.get("range", 7.0) or 7.0), 2.0, 16.0),
            })
    ir = {"setting": "exterior", "environment": ir_env, "objects": ir_objects, "lights": ir_lights}
    return ir, dropped


def solve_interior(plan):
    """Turn the validated relational plan into the engine Scene IR.
    Guarantees: in-bounds, non-overlapping, camera bay clear (drops as last resort)."""
    W, D = plan["width"], plan["depth"]
    cw = 0.35                               # wall clearance
    hx, hz = W / 2 - cw, D / 2 - cw
    placed = []                             # (x, z, r) circles for overlap tests
    ir_objects = []
    dropped = []

    def fits(x, z, r, flat, tall):
        if abs(x) + r > hx or abs(z) + r > hz:
            return False                    # out of bounds
        if not flat and _in_cam_bay(x, z, r):
            return False                    # camera bay
        if tall and z > hz * 0.35 and abs(x) < W * 0.22:
            return False                    # tall piece walling off the deep centre view
        if flat:
            return True                     # rugs don't collide
        for (px, pz, pr) in placed:
            if (x - px) ** 2 + (z - pz) ** 2 < (r + pr) ** 2:
                return False
        return True

    # place larger footprints first so small items fill the gaps
    objs = sorted(plan["objects"], key=lambda o: -(o.get("nominal", 0.8)))
    for o in objs:
        role = o["role"]
        nominal = float(o.get("footprint", o.get("nominal", 0.8)) or 0.8)
        foot = size_foot(role, o.get("native_horiz"), o.get("native_height"), nominal)
        foot = _clamp(foot, 0.2, min(3.5, W - 0.6, D - 0.6))
        flat = role in FLAT_ROLES
        tall = role in TALL_ROLES
        r = 0.20 if flat else max(foot / 2.0, 0.25)
        n = o.get("count", 1)
        for i in range(n):
            bx, bz, into = _resolve_anchor(o.get("anchor", "center"), hx, hz, foot, i, n)
            # honour explicit x/z if the model provided them
            if isinstance(o.get("x"), (int, float)) and isinstance(o.get("z"), (int, float)):
                bx, bz = float(o["x"]), float(o["z"])
            x, z, ok = bx, bz, False
            if fits(bx, bz, r, flat, tall):
                x, z, ok = bx, bz, True
            else:
                # search: slide along the room, then spiral outwards, then give up
                for step in [d * 0.25 for d in range(1, 13)]:
                    for (dx, dz) in [(step, 0), (-step, 0), (0, -step), (0, step),
                                     (step, -step), (-step, -step), (step, step), (-step, step)]:
                        if fits(bx + dx, bz + dz, r, flat, tall):
                            x, z, ok = bx + dx, bz + dz, True
                            break
                    if ok:
                        break
            if not ok:
                dropped.append(o["asset"])
                continue
            yaw = _facing_yaw(o.get("facing", "in"), x, z, into)
            entry = {"asset": o["asset"], "x": round(x, 3), "z": round(z, 3),
                     "yaw": round(yaw, 1), "foot": round(foot, 3)}
            if flat:
                entry["flat"] = True        # floor covering (rug): things sit ON it, no overlap
            if "tint" in o and o["tint"]:
                entry["tint"] = o["tint"]
            ir_objects.append(entry)
            if not flat:
                placed.append((x, z, r))

    # lights -> world positions near the ceiling
    ceil = 2.55
    light_pos = {
        "ceiling_center": (0.0, ceil, -D * 0.05),
        "ceiling_back": (0.0, ceil, -D * 0.30),
        "ceiling_left": (-W * 0.28, ceil, -D * 0.1),
        "ceiling_right": (W * 0.28, ceil, -D * 0.1),
    }
    ir_lights = []
    for l in plan.get("lights", []) or []:
        pos = light_pos.get(l.get("anchor", "ceiling_center"), light_pos["ceiling_center"])
        ir_lights.append({
            "type": "point", "x": pos[0], "y": pos[1], "z": pos[2],
            "color": _rgb(l.get("color"), [1.0, 0.92, 0.82]),
            "intensity": _clamp(float(l.get("intensity", 6.0) or 6.0), 1.0, 14.0),
            "range": _clamp(float(l.get("range", 9.0) or 9.0), 3.0, 14.0),
        })
    if not ir_lights:
        ir_lights = [{"type": "point", "x": 0.0, "y": ceil, "z": -D * 0.05,
                      "color": [1.0, 0.92, 0.82], "intensity": 6.0, "range": 9.0}]

    pal = plan["palette"]
    ir = {
        "room": {"w": round(W, 2), "d": round(D, 2),
                 "floor": pal["floor"], "wall": pal["wall"], "accent": pal["accent"],
                 "tile": bool(plan.get("tile", plan["room_type"] in ("kitchen", "bathroom"))),
                 "tint_strength": pal["tint_strength"]},
        "night": plan.get("night", False),
        "exposure": plan.get("exposure", 1.0),
        "objects": ir_objects,
        "lights": ir_lights,
    }
    return ir, dropped


# ----------------------------------------------------------------------------
# Validity check: an INDEPENDENT gate over the final IR (the battery pass/fail).
# ----------------------------------------------------------------------------

def validity_check(ir):
    if ir.get("setting") == "exterior":
        return validity_check_exterior(ir)
    return validity_check_interior(ir)


VEGETATION_ROLES = {"tree", "bush", "grass", "flower", "plant"}


def validity_check_exterior(ir):
    """Independent exterior gate: in-bounds, non-overlapping solids, no vegetation in
    the sea, and the centre view corridor clear of tall blockers."""
    problems = []
    env = ir.get("environment") or {}
    E = float((env.get("ground") or {}).get("extent", 34))
    water = env.get("water") or {}
    from_z = float(water.get("from_z", -0.15 * E)) if water.get("enabled") else None
    try:
        _, _, by_id = load_catalog()
    except Exception:
        by_id = {}
    circles = []
    for o in ir.get("objects", []):
        x, z, foot = o["x"], o["z"], float(o["foot"])
        r = max(foot / 2.0, 0.25)
        a = by_id.get(o["asset"].lower(), {})
        role = a.get("role", "misc")
        nh, nv = a.get("native_horiz"), a.get("native_height")
        est_h = foot * (nv / nh) if (nh and nv and nh > 1e-4) else foot
        if abs(x) + r > 0.95 * E or z < -0.5 * E or z > 8.0:
            problems.append(f"out_of_bounds: {o['asset']} at ({x:.1f},{z:.1f})")
        if from_z is not None and role in VEGETATION_ROLES and z < from_z - 0.8:
            problems.append(f"vegetation_in_water: {o['asset']} at z={z:.1f} (waterline {from_z:.1f})")
        if est_h >= TALL_EXT_M and abs(x) < 0.085 * E and \
           z > ((from_z + 1.0) if from_z is not None else -0.40 * E):
            problems.append(f"blocks_view: {o['asset']} tall in the centre corridor ({x:.1f},{z:.1f})")
        # trees collide by trunk (canopies may naturally interleave) -- same rule as the solver
        circles.append((x, z, r * (0.55 if role == "tree" else 1.0), o["asset"]))
    for i in range(len(circles)):
        for j in range(i + 1, len(circles)):
            x1, z1, r1, a1 = circles[i]
            x2, z2, r2, a2 = circles[j]
            d = math.hypot(x1 - x2, z1 - z2)
            if d < (r1 + r2) * 0.72 - 0.05:      # natural clusters may touch; flag real interpenetration
                problems.append(f"overlap: {a1} & {a2} (d={d:.2f} < {(r1+r2)*0.72:.2f})")
    return problems


def validity_check_interior(ir):
    problems = []
    W, D = ir["room"]["w"], ir["room"]["d"]
    cw = 0.30
    hx, hz = W / 2 - cw, D / 2 - cw
    circles = []
    for o in ir["objects"]:
        flat = bool(o.get("flat"))
        r = max(float(o["foot"]) / 2.0, 0.2)
        x, z = o["x"], o["z"]
        # bounds -- rugs are floor coverings and may run to the walls (extra slack)
        slack = min(r, 1.0) if flat else 0.25
        if abs(x) + r > hx + slack or abs(z) + r > hz + slack:
            problems.append(f"out_of_bounds: {o['asset']} at ({x:.1f},{z:.1f}) r={r:.1f}")
        # camera bay -- flat rugs don't obstruct the lens
        if not flat and _in_cam_bay(x, z, r):
            problems.append(f"blocks_camera: {o['asset']} in front-centre bay ({x:.1f},{z:.1f})")
        if not flat:
            circles.append((x, z, r, o["asset"]))
    # solid-object overlaps (flat floor coverings excluded)
    for i in range(len(circles)):
        for j in range(i + 1, len(circles)):
            x1, z1, r1, a1 = circles[i]
            x2, z2, r2, a2 = circles[j]
            d = math.hypot(x1 - x2, z1 - z2)
            if d < (r1 + r2) - 0.12:                # 12cm tolerance
                problems.append(f"overlap: {a1} & {a2} (d={d:.2f} < {r1+r2:.2f})")
    return problems


# ----------------------------------------------------------------------------
# Render + critique loop.
# ----------------------------------------------------------------------------

def render_ir(ir, out_name, camera="", night=False, timeout=220):
    ir_path = LOGS / f"{out_name}_ir.json"
    ir_path.parent.mkdir(parents=True, exist_ok=True)
    ir_path.write_text(json.dumps(ir), encoding="utf-8")
    args = ["powershell", "-NoProfile", "-File", str(TOOLS / "render_ir.ps1"),
            "-JsonFile", str(ir_path), "-OutName", out_name]
    if camera:
        args += ["-Camera", camera]
    if night:
        args += ["-Night"]
    r = subprocess.run(args, capture_output=True, text=True, timeout=timeout)
    lines = [l.strip() for l in r.stdout.splitlines() if l.strip()]
    png = lines[-1] if lines and lines[-1].lower().endswith(".png") else None
    if png and Path(png).exists():
        return png
    return None


CRITIC_PROMPT = """You are judging a rendered 3D scene for how well it matches an intent.
INTENT: "{prompt}"

Look at the image and return ONLY this JSON (no prose):
{{"score": <1-5 overall match of room + dominant colour + mood>,
  "room_ok": <true/false>, "color_ok": <true/false>, "dominant_color": "<what colour dominates>",
  "issue": "<the single biggest framing/exposure problem, or 'none'>",
  "fix": {{"dolly": <-1.5..3 m back>, "lift": <-0.5..1.5 m up>, "pan": <-25..25 deg>,
           "fov": <-10..15 deg>, "exposure": <0.6..1.6 x>}},
  "verdict": "<good|reframe|bad>"}}
Image: {img}"""


def critique(png, prompt, timeout=150, retries=3):
    """Vision verdict with retries + backoff -- claude -p rate-limits under rapid
    bursts (the cause of the None verdicts in early battery runs)."""
    for attempt in range(retries):
        try:
            r = _run(["claude", "-p", CRITIC_PROMPT.format(prompt=prompt, img=png)], timeout)
        except subprocess.TimeoutExpired:
            r = None
        crit = _extract_json(r.stdout) if (r and r.returncode == 0) else None
        if crit and isinstance(crit.get("score"), (int, float)):
            return crit
        if attempt < retries - 1:
            time.sleep(8 * (attempt + 1))
    return None


def run_pipeline(prompt, name, backends, iters=3, refresh_catalog=False, verbose=True):
    assets, by_role, by_id = load_catalog(refresh=refresh_catalog)
    plan = compose(prompt, by_role, backends, verbose=verbose)
    plan, repairs = validate_plan(plan, by_role, by_id, verbose=verbose)
    ir, dropped = solve(plan)
    if dropped and verbose:
        print(f"[solve] dropped {len(dropped)} un-placeable: {dropped}")
    problems = validity_check(ir)
    result = {
        "prompt": prompt, "name": name, "backend": plan.get("_backend"),
        "setting": plan.get("setting", "interior"),
        "room_type": plan.get("room_type") or plan.get("scene_type"),
        "objects": len(ir["objects"]),
        "dropped": dropped, "repairs": repairs, "validity": problems, "ir": ir,
    }
    if verbose:
        status = "VALID" if not problems else f"{len(problems)} PROBLEM(S)"
        where = (f"room {ir['room']['w']}x{ir['room']['d']}" if "room" in ir
                 else f"{ir['environment']['ground']['kind']} extent {ir['environment']['ground']['extent']}"
                       + (" + water" if ir['environment']['water']['enabled'] else ""))
        print(f"[solve] {len(ir['objects'])} objects, {where} -> {status}")
        for p in problems[:8]:
            print(f"    ! {p}")

    # render + critique loop (composition is already valid; the loop reframes)
    best = None
    dolly = lift = pan = fov = 0.0
    # the plan's mood exposure seeds the loop (interior: night flag uses the engine's
    # night-showcase light rig; exterior exposure rides in the IR itself)
    night = bool(ir.get("night", False))
    exposure = float(ir.get("exposure", 1.0)) if ir.get("setting") != "exterior" else 1.0
    camera = f"0,0,0,0,{exposure}" if abs(exposure - 1.0) > 1e-3 else ""
    for it in range(iters):
        png = render_ir(ir, f"{name}_{it}", camera=camera, night=night)
        if not png:
            if verbose:
                print(f"[render] iter {it}: FAILED (gpu/timeout) -- keeping best so far")
            break
        crit = critique(png, prompt) if backends != ["offline"] else None
        score = crit.get("score") if crit else None
        if verbose:
            extra = ""
            if crit:
                extra = f" score={score} color_ok={crit.get('color_ok')} dom={crit.get('dominant_color')} verdict={crit.get('verdict')}"
            print(f"[iter {it}] {Path(png).name}{extra}")
        cur = {"iter": it, "png": png, "crit": crit, "camera": camera}
        if best is None or (score or 0) >= (best.get("crit", {}).get("score", 0) if best.get("crit") else 0):
            best = cur
        if not crit or crit.get("verdict") == "good" or (score or 0) >= 5:
            break
        f = crit.get("fix", {}) or {}
        dolly = _clamp(dolly + float(f.get("dolly", 0) or 0), -1.5, 3.5)
        lift = _clamp(lift + float(f.get("lift", 0) or 0), -0.5, 2.0)
        pan = _clamp(pan + float(f.get("pan", 0) or 0), -40, 40)
        fov = _clamp(fov + float(f.get("fov", 0) or 0), -12, 20)
        exposure = _clamp(exposure * float(f.get("exposure", 1) or 1), 0.5, 1.8)
        camera = f"{dolly},{lift},{pan},{fov},{exposure}"
    result["best"] = best
    return result


def main():
    ap = argparse.ArgumentParser(description="Generative interior scene pipeline")
    ap.add_argument("prompt", help="natural-language interior prompt")
    ap.add_argument("--name", default=None, help="output basename")
    ap.add_argument("--backends", default="codex,claude,deepseek",
                    help="comma list; offline heuristic is always the final fallback")
    ap.add_argument("--iters", type=int, default=3, help="critique/reframe iterations")
    ap.add_argument("--refresh-catalog", action="store_true")
    ap.add_argument("--no-critic", action="store_true", help="skip the vision loop (1 render)")
    args = ap.parse_args()
    name = args.name or "gen_" + "".join(c if c.isalnum() else "_" for c in args.prompt.lower())[:32]
    backends = ["offline"] if args.no_critic and args.backends == "offline" else args.backends.split(",")
    res = run_pipeline(args.prompt, name, backends, iters=(1 if args.no_critic else args.iters),
                       refresh_catalog=args.refresh_catalog)
    print("\n=== RESULT ===")
    print(json.dumps({k: v for k, v in res.items() if k not in ("ir", "best")}, indent=2))
    if res.get("best"):
        print("best render:", res["best"]["png"])
    print("VALID" if not res["validity"] else "INVALID")
    sys.exit(0 if not res["validity"] else 1)


if __name__ == "__main__":
    main()
