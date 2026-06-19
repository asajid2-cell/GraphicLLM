#include "SceneRecipes.h"

#include "SceneCommands.h"
#include "Scene/AssetCatalog.h"
#include "Scene/Components.h"
#include "Utils/GLTFLoader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

namespace Cortex::LLM {

namespace {

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool Contains(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}

// Near-white tint for high-poly PBR-textured assets: not pure white (so Place()
// doesn't substitute a flat ColorForKey), so the asset's own glTF albedo/normal/
// roughness textures drive the look.
const glm::vec4 kTex(0.97f, 0.97f, 0.97f, 1.0f);

// Measured object-space footprint of a catalogued asset (cached per build).
struct Footprint {
    glm::vec3 size{1.0f};
    bool valid = false;
};

using FootprintCache = std::unordered_map<std::string, Footprint>;

Footprint Measure(const Scene::AssetCatalog& catalog, FootprintCache& cache, const std::string& key) {
    if (auto it = cache.find(key); it != cache.end()) {
        return it->second;
    }
    Footprint fp;
    if (auto path = catalog.ResolvePath(key)) {
        auto meshResult = Utils::LoadGLTFMesh(*path);
        if (!meshResult.IsErr()) {
            auto mesh = meshResult.Value();
            if (mesh) {
                if (!mesh->hasBounds) {
                    mesh->UpdateBounds();
                }
                fp.size = glm::max(mesh->boundsMax - mesh->boundsMin, glm::vec3(1e-3f));
                fp.valid = true;
            }
        } else {
            spdlog::warn("SceneRecipes: could not measure '{}': {}", key, meshResult.Error());
        }
    }
    cache.emplace(key, fp);
    return fp;
}

// Plausible albedo for an asset by keyword, so untextured catalog meshes don't
// all render flat white. Used when a recipe doesn't pass an explicit color.
glm::vec4 ColorForKey(const std::string& k) {
    auto has = [&](const char* n) { return k.find(n) != std::string::npos; };
    if (has("pillow")) {
        if (has("blue")) return glm::vec4(0.30f, 0.42f, 0.58f, 1.0f); // blue accent
        return glm::vec4(0.74f, 0.42f, 0.33f, 1.0f);                  // terracotta accent
    }
    if (has("sofa") || has("couch")) {
        return glm::vec4(0.36f, 0.45f, 0.43f, 1.0f); // muted sage-teal sofa
    }
    if (has("bed")) {
        return glm::vec4(0.68f, 0.64f, 0.56f, 1.0f); // cream bedding
    }
    if (has("chair") || has("cushion") || has("stool") || has("lounge")) {
        return glm::vec4(0.55f, 0.48f, 0.40f, 1.0f); // warm taupe upholstery
    }
    if (has("television") || has("computer") || has("screen") || has("speaker") || has("laptop") ||
        has("radio") || has("monitor")) {
        return glm::vec4(0.09f, 0.09f, 0.11f, 1.0f); // dark electronics
    }
    if (has("fridge") || has("stove") || has("sink") || has("hood") || has("microwave") || has("oven") ||
        has("dishwasher") || has("washer") || has("dryer")) {
        return glm::vec4(0.78f, 0.79f, 0.82f, 1.0f); // brushed appliance
    }
    if (has("lamp")) {
        return glm::vec4(0.70f, 0.60f, 0.38f, 1.0f); // warm shade
    }
    if (has("rug") || has("doormat")) {
        return glm::vec4(0.45f, 0.32f, 0.28f, 1.0f);
    }
    if (has("plant") || has("fern") || has("grass") || has("bush") || has("rooibos")) {
        return glm::vec4(0.27f, 0.40f, 0.19f, 1.0f); // foliage
    }
    if (has("rock") || has("boulder") || has("stone")) {
        return glm::vec4(0.50f, 0.49f, 0.46f, 1.0f);
    }
    if (has("table") || has("desk") || has("cabinet") || has("bookcase") || has("shelf") || has("sidetable") ||
        has("coat") || has("bar") || has("wood") || has("stump") || has("trunk") || has("branch") ||
        has("bench") || has("dresser") || has("wardrobe") || has("drawer")) {
        return glm::vec4(0.40f, 0.28f, 0.17f, 1.0f); // wood
    }
    return glm::vec4(0.72f, 0.71f, 0.70f, 1.0f); // neutral
}

// Per-semantic-class PBR response (roughness, metallic). Flat uniform roughness
// makes everything read like the same plastic; real materials differ in how they
// catch light — glossy glass/metal, satin wood, matte fabric/plaster.
struct PbrResponse { float roughness; float metallic; };
PbrResponse PbrForKey(const std::string& k) {
    auto has = [&](const char* n) { return k.find(n) != std::string::npos; };
    if (has("mirror") || has("glass") || has("window")) return {0.06f, 0.0f};        // glossy glass
    if (has("screen") || has("television") || has("monitor")) return {0.18f, 0.0f};   // screen glass
    if (has("fridge") || has("stove") || has("sink") || has("hood") || has("microwave") ||
        has("oven") || has("toaster") || has("kettle") || has("dishwasher")) return {0.34f, 0.85f}; // brushed metal
    if (has("lamp") || has("lantern")) return {0.40f, 0.45f};                          // metal lamp body
    if (has("sofa") || has("couch") || has("chair") || has("cushion") || has("stool") ||
        has("bed") || has("pillow") || has("rug") || has("doormat") || has("lounge") ||
        has("bench") || has("coat")) return {0.90f, 0.0f};                             // matte fabric
    if (has("plant") || has("fern") || has("grass") || has("bush") || has("foliage")) return {0.65f, 0.0f};
    if (has("rock") || has("boulder") || has("stone")) return {0.85f, 0.0f};
    if (has("table") || has("desk") || has("cabinet") || has("bookcase") || has("shelf") ||
        has("wood") || has("stump") || has("trunk") || has("branch") || has("dresser") ||
        has("wardrobe") || has("drawer") || has("barrel")) return {0.55f, 0.0f};       // satin wood
    return {0.7f, 0.0f};
}

// Emit one real catalog asset, normalized so its largest horizontal extent ~=
// targetFootprint meters, placed at (x,0,z), yawed yawDeg about Y, ground-snapped
// by the executor. Returns false (and emits nothing) if the asset can't resolve.
bool Place(std::vector<std::shared_ptr<SceneCommand>>& out,
           const Scene::AssetCatalog& catalog,
           FootprintCache& cache,
           const std::string& key,
           float targetFootprint,
           float x,
           float z,
           float yawDeg,
           const glm::vec4& color = glm::vec4(1.0f),
           float supportHeight = 0.0f) {
    if (!catalog.ResolvePath(key)) {
        spdlog::info("SceneRecipes: skipping unresolved asset '{}'", key);
        return false;
    }
    const Footprint fp = Measure(catalog, cache, key);
    const float horiz = std::max(fp.size.x, fp.size.z);
    const float scale = (fp.valid && horiz > 1e-3f && targetFootprint > 0.0f) ? (targetFootprint / horiz) : 1.0f;

    auto cmd = std::make_shared<AddEntityCommand>();
    cmd->entityType = AddEntityCommand::EntityType::Model;
    cmd->asset = key;
    cmd->name = key;
    cmd->position = glm::vec3(x, 0.0f, z);
    cmd->scale = glm::vec3(scale);
    const std::string lk = ToLower(key);
    cmd->color = (color == glm::vec4(1.0f)) ? ColorForKey(lk) : color;
    // Deterministic per-instance tint so repeated items (a row of dining chairs,
    // stools, books) aren't identical clones. Hash of position -> small brightness
    // jitter; stable across rebuilds, no RNG.
    {
        const int hx = static_cast<int>(x * 100.0f);
        const int hz = static_cast<int>(z * 100.0f);
        unsigned h = static_cast<unsigned>(hx * 374761393 + hz * 668265263);
        h = (h ^ (h >> 13)) * 1274126177u;
        const float jitter = 0.93f + (h & 0xFFFFu) / 65535.0f * 0.14f; // 0.93..1.07
        cmd->color = glm::vec4(glm::clamp(glm::vec3(cmd->color) * jitter, 0.0f, 1.0f), cmd->color.a);
    }
    // Light sources + screens glow when "on" — a cheap, automatic realism touch.
    if ((lk.find("lamp") != std::string::npos || lk.find("lantern") != std::string::npos) &&
        lk.find("ceiling") == std::string::npos) {
        cmd->setEmissiveStrength = true;
        cmd->emissiveStrength = 0.55f;
        cmd->emissiveColor = glm::vec4(1.0f, 0.86f, 0.60f, 1.0f); // warm bulb
        cmd->setEmissiveBloom = true;
        cmd->emissiveBloom = 0.25f;
    } else if (lk.find("screen") != std::string::npos || lk.find("television") != std::string::npos ||
               lk.find("monitor") != std::string::npos) {
        cmd->setEmissiveStrength = true;
        cmd->emissiveStrength = 1.3f;
        cmd->emissiveColor = glm::vec4(0.45f, 0.62f, 0.85f, 1.0f); // screen glow
        cmd->setEmissiveBloom = true;
        cmd->emissiveBloom = 0.2f;
    }
    const PbrResponse pbr = PbrForKey(lk); // per-class material response (glossy/metal/satin/matte)
    cmd->roughness = pbr.roughness;
    cmd->metallic = pbr.metallic;
    cmd->rotationEuler = glm::vec3(0.0f, yawDeg, 0.0f);
    cmd->hasRotation = true;
    cmd->supportHeight = supportHeight; // base rests on this surface (0 = floor)
    cmd->autoPlace = false;
    cmd->allowPlacementJitter = false;
    cmd->disableCollisionAvoidance = true; // recipe positions are deliberate
    out.push_back(std::move(cmd));
    return true;
}

// Place a prop ON a surface: its base rests at world-Y `surfaceY` (counter top,
// table top, shelf) rather than the floor — for appliances, books, lamps, decor.
inline bool PlaceOn(std::vector<std::shared_ptr<SceneCommand>>& out,
                    const Scene::AssetCatalog& catalog, FootprintCache& cache,
                    const std::string& key, float targetFootprint, float x, float surfaceY,
                    float z, float yawDeg, const glm::vec4& color = glm::vec4(1.0f)) {
    return Place(out, catalog, cache, key, targetFootprint, x, z, yawDeg, color, surfaceY);
}

// Author a real point light (e.g. a lamp's bulb) so props actually light the room
// — motivated lighting, not just a glowing mesh.
inline void AddPointLight(std::vector<std::shared_ptr<SceneCommand>>& out, float x, float y, float z,
                          const glm::vec3& color, float intensity, float range) {
    auto cmd = std::make_shared<AddLightCommand>();
    cmd->lightType = AddLightCommand::LightType::Point;
    cmd->position = glm::vec3(x, y, z);
    cmd->color = color;
    cmd->intensity = std::min(intensity, 7.0f);
    cmd->range = std::min(range, 4.8f);
    cmd->name = "RecipeLampLight";
    out.push_back(std::move(cmd));
}

// A simple ground plane sized to the room (primitive, sits at y=0).
void PlaceFloor(std::vector<std::shared_ptr<SceneCommand>>& out, float width, float depth, const glm::vec4& color) {
    auto cmd = std::make_shared<AddEntityCommand>();
    cmd->entityType = AddEntityCommand::EntityType::Plane; // CreatePlane is 2x2 in XZ
    cmd->name = "Floor";
    cmd->position = glm::vec3(0.0f, 0.0f, 0.0f);
    cmd->scale = glm::vec3(width * 0.5f, 1.0f, depth * 0.5f);
    cmd->color = color;
    cmd->roughness = 0.52f; // satin floor: catches soft reflections of the room (SSR) instead of dead-matte
    cmd->metallic = 0.0f;
    cmd->allowPlacementJitter = false;
    cmd->disableCollisionAvoidance = true;
    out.push_back(std::move(cmd));
}

// Place a real asset at an EXPLICIT uniform scale (used for tiled walls, where
// every segment must share one scale to line up). Ground-snapped by the executor.
void PlaceExplicit(std::vector<std::shared_ptr<SceneCommand>>& out, const Scene::AssetCatalog& cat,
                   const std::string& key, float scale, float x, float z, float yawDeg,
                   const glm::vec4& color = glm::vec4(1.0f)) {
    if (!cat.ResolvePath(key)) {
        return;
    }
    auto cmd = std::make_shared<AddEntityCommand>();
    cmd->entityType = AddEntityCommand::EntityType::Model;
    cmd->asset = key;
    cmd->name = key;
    cmd->position = glm::vec3(x, 0.0f, z);
    cmd->scale = glm::vec3(scale);
    cmd->color = color;
    cmd->roughness = 0.85f;
    cmd->rotationEuler = glm::vec3(0.0f, yawDeg, 0.0f);
    cmd->hasRotation = true;
    cmd->autoPlace = false;
    cmd->allowPlacementJitter = false;
    cmd->disableCollisionAvoidance = true;
    out.push_back(std::move(cmd));
}

// Build a room shell: floor + a perimeter of tiled real wall segments (interior
// width x depth, centered at origin). Each wall is scaled to ~segLen m so the
// segments tile cleanly; a center gap is left on the +Z (front) side as a
// doorway. Falls back to a bare floor if no wall asset is available.
void BuildRoomShell(std::vector<std::shared_ptr<SceneCommand>>& out, const Scene::AssetCatalog& cat,
                    FootprintCache& c, float width, float depth, const glm::vec4& floorColor) {
    PlaceFloor(out, width, depth, floorColor);
    (void)cat;
    (void)c;

    // Solid box walls (one colored unit-cube per side, scaled) read far better
    // than tiled, gapped, untextured Kenney wall panels. The front (+Z) wall is
    // split around a central doorway so the room reads as enterable, and the
    // camera looks in over the tops.
    const glm::vec4 wallColor(0.84f, 0.81f, 0.76f, 1.0f);     // warm off-white
    const glm::vec4 backWallColor(0.52f, 0.53f, 0.58f, 1.0f); // muted feature wall for depth
    const glm::vec4 baseColor(0.24f, 0.20f, 0.17f, 1.0f);     // dark-wood baseboard + trim
    const float wallH = 2.8f;
    const float wallTh = 0.16f;
    const float baseH = 0.16f;  // baseboard height
    const float baseTh = 0.24f; // sits slightly proud of the wall plane
    const float hw = width * 0.5f;
    const float hd = depth * 0.5f;

    auto box = [&](const std::string& tag, float cx, float cy, float cz, float sx, float sy, float sz,
                   const glm::vec4& col) {
        auto cmd = std::make_shared<AddEntityCommand>();
        cmd->entityType = AddEntityCommand::EntityType::Cube; // unit cube (+/-0.5)
        cmd->name = tag;
        cmd->position = glm::vec3(cx, cy, cz);
        cmd->scale = glm::vec3(sx, sy, sz);
        cmd->color = col;
        cmd->metallic = 0.0f;
        cmd->roughness = 0.92f;
        cmd->allowPlacementJitter = false;
        cmd->disableCollisionAvoidance = true;
        out.push_back(std::move(cmd));
    };
    auto wall = [&](const std::string& tag, float cx, float cz, float sx, float sz, const glm::vec4& col) {
        box(tag, cx, wallH * 0.5f, cz, sx, wallH, sz, col); // base on the floor
    };

    wall("Wall_Back", 0.0f, -hd, width, wallTh, backWallColor);
    wall("Wall_Left", -hw, 0.0f, wallTh, depth, wallColor);
    wall("Wall_Right", hw, 0.0f, wallTh, depth, wallColor);

    const float doorW = 2.4f;
    const float segW = (width - doorW) * 0.5f;
    if (segW > 0.1f) {
        const float off = doorW * 0.5f + segW * 0.5f;
        wall("Wall_FrontL", -off, hd, segW, wallTh, wallColor);
        wall("Wall_FrontR", off, hd, segW, wallTh, wallColor);
    }

    // Baseboards along the foot of the walls — a small detail that makes the
    // floor/wall junction read as finished rather than a bare box.
    box("Base_Back", 0.0f, baseH * 0.5f, -hd, width, baseH, baseTh, baseColor);
    box("Base_Left", -hw, baseH * 0.5f, 0.0f, baseTh, baseH, depth, baseColor);
    box("Base_Right", hw, baseH * 0.5f, 0.0f, baseTh, baseH, depth, baseColor);

    // A glowing daylight window on the feature wall: a dark frame + an emissive
    // sky-blue pane. Breaks the blank plane, reads as a real window, and (via GI)
    // throws a little light into the room. Sits high enough to clear back-wall
    // furniture (counters/beds/sofas).
    const float winW = std::min(1.9f, width * 0.42f);
    const float winH = 1.25f;
    const float winY = 1.78f;
    const float wallFace = -hd + wallTh * 0.5f; // inner face of the back wall
    box("Window_Frame", 0.0f, winY, wallFace + 0.02f, winW + 0.18f, winH + 0.18f, 0.06f, baseColor);
    {
        auto pane = std::make_shared<AddEntityCommand>();
        pane->entityType = AddEntityCommand::EntityType::Cube;
        pane->name = "Window_Pane";
        pane->position = glm::vec3(0.0f, winY, wallFace + 0.05f);
        pane->scale = glm::vec3(winW, winH, 0.05f);
        pane->color = glm::vec4(0.70f, 0.83f, 0.98f, 1.0f); // daylight sky
        pane->metallic = 0.0f;
        pane->roughness = 0.2f;
        pane->setEmissiveStrength = true;
        pane->emissiveStrength = 0.78f;  // bright but below hard clip
        pane->setEmissiveBloom = true;
        pane->emissiveBloom = 0.28f;     // soft glow, not a flat white panel
        pane->allowPlacementJitter = false;
        pane->disableCollisionAvoidance = true;
        out.push_back(std::move(pane));
    }

    // Framed wall art on the left wall — fills the blank side wall the camera sees
    // (interiors only; the small bathroom's side walls hold fixtures).
    if (depth > 5.0f) {
        const float artY = 1.55f;
        const float leftFace = -hw + wallTh * 0.5f;
        auto artPanel = [&](const std::string& tag, float cz, const glm::vec4& canvas) {
            box(tag + "_F", leftFace + 0.02f, artY, cz, 0.06f, 0.94f, 0.74f, baseColor); // dark frame
            auto a = std::make_shared<AddEntityCommand>();
            a->entityType = AddEntityCommand::EntityType::Cube;
            a->name = tag;
            a->position = glm::vec3(leftFace + 0.05f, artY, cz);
            a->scale = glm::vec3(0.04f, 0.80f, 0.60f);
            a->color = canvas;
            a->metallic = 0.0f;
            a->roughness = 0.7f;
            a->allowPlacementJitter = false;
            a->disableCollisionAvoidance = true;
            out.push_back(std::move(a));
        };
        artPanel("WallArtA", -0.75f, glm::vec4(0.40f, 0.33f, 0.47f, 1.0f)); // dusty plum canvas
        artPanel("WallArtB", 0.70f, glm::vec4(0.28f, 0.40f, 0.42f, 1.0f));  // muted teal canvas
    }
}

// ---- recipes ---------------------------------------------------------------
// Facing convention: yaw 0 faces +Z. These yaws are an initial layout; a visual
// pass may flip a piece 180 deg if a Kenney asset's authored front differs.

void BuildLivingRoom(std::vector<std::shared_ptr<SceneCommand>>& out, const Scene::AssetCatalog& cat, FootprintCache& c) {
    // Cozier footprint so the seating group fills the room instead of floating.
    BuildRoomShell(out, cat, c, 6.8f, 6.6f, glm::vec4(0.50f, 0.43f, 0.36f, 1.0f)); // warm wood floor
    Place(out, cat, c, "rugRectangle", 2.9f, 0.0f, -0.3f, 0.0f, glm::vec4(0.47f, 0.30f, 0.26f, 1.0f));
    // High-poly PBR-textured furniture (Poly Haven CC0) — kTex lets the asset's own
    // albedo/normal/roughness textures drive the look (not a flat tint).
    Place(out, cat, c, "Sofa_01", 2.3f, 0.0f, -2.0f, 0.0f, kTex);        // back wall
    Place(out, cat, c, "ArmChair_01", 0.95f, -2.05f, -0.3f, 50.0f, kTex); // angled into the group
    Place(out, cat, c, "ArmChair_01", 0.95f, 2.05f, -0.3f, -50.0f, kTex);
    Place(out, cat, c, "CoffeeTable_01", 1.2f, 0.0f, -0.7f, 0.0f, kTex);  // centre of the seating
    PlaceOn(out, cat, c, "books", 0.35f, 0.15f, 0.42f, -0.7f, 20.0f);   // on the coffee table
    Place(out, cat, c, "lampRoundFloor", 0.4f, -2.7f, -2.1f, 0.0f);     // floor lamp beside the sofa
    AddPointLight(out, -2.55f, 1.45f, -2.1f, glm::vec3(1.0f, 0.78f, 0.48f), 5.4f, 4.3f);
    Place(out, cat, c, "sideTable", 0.5f, 2.7f, -2.1f, 0.0f);           // side table other end
    PlaceOn(out, cat, c, "lampRoundTable", 0.28f, 2.7f, 0.46f, -2.1f, 0.0f); // table lamp on it
    AddPointLight(out, 2.55f, 0.82f, -2.1f, glm::vec3(1.0f, 0.78f, 0.48f), 3.8f, 3.3f);
    Place(out, cat, c, "Shelf_01", 1.1f, -3.0f, 1.0f, 90.0f, kTex);     // side wall (high-poly)
    Place(out, cat, c, "cabinetTelevision", 1.6f, 0.0f, 2.7f, 180.0f);  // front wall (behind camera)
    Place(out, cat, c, "televisionModern", 1.2f, 0.0f, 2.5f, 180.0f);
    Place(out, cat, c, "pottedPlant", 0.6f, 2.9f, -2.6f, 0.0f);         // back-right corner greenery
    Place(out, cat, c, "pottedPlant", 0.55f, -3.0f, -2.7f, 0.0f);       // back-left corner
}

void BuildBedroom(std::vector<std::shared_ptr<SceneCommand>>& out, const Scene::AssetCatalog& cat, FootprintCache& c) {
    BuildRoomShell(out, cat, c, 6.4f, 6.2f, glm::vec4(0.48f, 0.44f, 0.46f, 1.0f));
    Place(out, cat, c, "rugRound", 2.7f, -0.1f, 0.2f, 0.0f, glm::vec4(0.46f, 0.40f, 0.48f, 1.0f));
    Place(out, cat, c, "GothicBed_01", 2.7f, -0.2f, -1.5f, 0.0f, kTex);  // focal: high-poly bed, faces camera
    Place(out, cat, c, "ClassicNightstand_01", 0.5f, -1.9f, -2.3f, 0.0f, kTex);
    Place(out, cat, c, "ClassicNightstand_01", 0.5f, 1.5f, -2.3f, 0.0f, kTex);
    PlaceOn(out, cat, c, "lampSquareTable", 0.3f, 1.5f, 0.52f, -2.3f, 0.0f);  // lamp on nightstand
    AddPointLight(out, 1.5f, 0.9f, -2.3f, glm::vec3(1.0f, 0.77f, 0.46f), 3.8f, 3.3f);
    PlaceOn(out, cat, c, "books", 0.26f, -1.9f, 0.52f, -2.3f, 15.0f);          // books on the other
    Place(out, cat, c, "Shelf_01", 1.1f, -2.85f, 1.1f, 90.0f, kTex);    // left wall (high-poly)
    Place(out, cat, c, "coatRackStanding", 0.45f, -2.7f, -2.5f, 0.0f);
    Place(out, cat, c, "pottedPlant", 0.5f, 2.6f, -2.4f, 0.0f);         // back-right corner accent
}

void BuildOffice(std::vector<std::shared_ptr<SceneCommand>>& out, const Scene::AssetCatalog& cat, FootprintCache& c) {
    BuildRoomShell(out, cat, c, 6.4f, 6.2f, glm::vec4(0.46f, 0.46f, 0.48f, 1.0f));
    Place(out, cat, c, "rugSquare", 2.4f, -0.4f, -0.3f, 0.0f, glm::vec4(0.33f, 0.34f, 0.40f, 1.0f));
    Place(out, cat, c, "ClassicConsole_01", 1.9f, -0.5f, -1.5f, 8.0f, kTex);  // focal: high-poly console desk
    Place(out, cat, c, "WoodenChair_01", 0.8f, -0.5f, -0.55f, 180.0f, kTex);  // high-poly chair, faces desk
    PlaceOn(out, cat, c, "computerScreen", 0.55f, -0.85f, 0.78f, -1.75f, 4.0f);  // monitor on the desk
    PlaceOn(out, cat, c, "computerKeyboard", 0.4f, -0.7f, 0.78f, -1.25f, 4.0f);  // keyboard
    PlaceOn(out, cat, c, "lampSquareTable", 0.26f, 0.35f, 0.78f, -1.7f, 0.0f);   // desk lamp
    AddPointLight(out, 0.35f, 1.06f, -1.7f, glm::vec3(1.0f, 0.80f, 0.52f), 5.0f, 3.4f);
    Place(out, cat, c, "Shelf_01", 1.2f, -2.85f, 0.9f, 90.0f, kTex);     // left wall (high-poly)
    Place(out, cat, c, "Shelf_01", 1.5f, -1.3f, -2.95f, 0.0f, kTex);     // back wall (high-poly)
    Place(out, cat, c, "pottedPlant", 0.55f, 2.6f, -2.3f, 0.0f);         // back-right corner
    Place(out, cat, c, "trashcan", 0.3f, 0.8f, -1.1f, 0.0f);
}

void BuildKitchen(std::vector<std::shared_ptr<SceneCommand>>& out, const Scene::AssetCatalog& cat, FootprintCache& c) {
    BuildRoomShell(out, cat, c, 6.6f, 6.4f, glm::vec4(0.34f, 0.30f, 0.25f, 1.0f)); // warm floor
    const float backZ = -2.7f;
    const float counterTop = 0.92f; // lower-cabinet height after scaling
    // Lower counter run along the back wall.
    Place(out, cat, c, "kitchenCabinet", 0.95f, -2.2f, backZ, 0.0f);
    Place(out, cat, c, "kitchenCabinetDrawer", 0.95f, -1.25f, backZ, 0.0f);
    Place(out, cat, c, "kitchenSink", 0.95f, -0.3f, backZ, 0.0f);
    Place(out, cat, c, "kitchenStove", 0.95f, 0.65f, backZ, 0.0f);
    Place(out, cat, c, "kitchenCabinet", 0.95f, 1.6f, backZ, 0.0f);
    Place(out, cat, c, "kitchenFridge", 1.05f, 2.7f, backZ, 0.0f);
    // Upper cabinets + range hood mounted on the wall above the run.
    PlaceOn(out, cat, c, "kitchenCabinetUpper", 0.95f, -2.2f, 1.65f, backZ - 0.06f, 0.0f);
    PlaceOn(out, cat, c, "kitchenCabinetUpperDouble", 1.3f, -0.9f, 1.65f, backZ - 0.06f, 0.0f);
    PlaceOn(out, cat, c, "hoodModern", 0.9f, 0.65f, 1.6f, backZ - 0.06f, 0.0f); // over the stove
    PlaceOn(out, cat, c, "kitchenCabinetUpper", 0.95f, 1.6f, 1.65f, backZ - 0.06f, 0.0f);
    // Counter-top appliances.
    PlaceOn(out, cat, c, "kitchenMicrowave", 0.5f, -2.2f, counterTop, backZ, 0.0f);
    PlaceOn(out, cat, c, "kitchenCoffeeMachine", 0.3f, -1.55f, counterTop, backZ + 0.05f, 0.0f);
    PlaceOn(out, cat, c, "toaster", 0.25f, 1.55f, counterTop, backZ + 0.05f, 0.0f);
    // Island bar with stools tucked at the near side (foreground).
    Place(out, cat, c, "kitchenBar", 1.9f, -0.2f, 0.3f, 0.0f);
    Place(out, cat, c, "stoolBar", 0.42f, -0.9f, 1.2f, 180.0f);
    Place(out, cat, c, "stoolBar", 0.42f, 0.5f, 1.2f, 180.0f);
}

} // namespace

std::optional<std::string> MatchSceneRecipe(const std::string& prompt) {
    const std::string p = ToLower(prompt);
    const bool buildIntent = Contains(p, "build") || Contains(p, "make") || Contains(p, "create") ||
                             Contains(p, "generate") || Contains(p, "set up") || Contains(p, "design") ||
                             Contains(p, "furnish");
    if (!buildIntent) {
        return std::nullopt;
    }
    if (Contains(p, "living room") || Contains(p, "livingroom") || Contains(p, "lounge")) {
        return std::string("living_room");
    }
    if (Contains(p, "bedroom") || Contains(p, "bed room")) {
        return std::string("bedroom");
    }
    if (Contains(p, "office") || Contains(p, "study") || Contains(p, "workspace")) {
        return std::string("office");
    }
    if (Contains(p, "kitchen")) {
        return std::string("kitchen");
    }
    if (Contains(p, "dining") || Contains(p, "dinner")) {
        return std::string("dining_room");
    }
    if (Contains(p, "bathroom") || Contains(p, "bath room") || Contains(p, "washroom") ||
        Contains(p, "restroom") || Contains(p, "toilet")) {
        return std::string("bathroom");
    }
    if (Contains(p, "garden") || Contains(p, "patio") || Contains(p, "backyard") ||
        Contains(p, "courtyard") || Contains(p, "terrace")) {
        return std::string("garden");
    }
    return std::nullopt;
}

std::vector<std::string> AvailableSceneRecipes() {
    return {"living_room", "bedroom", "office", "kitchen", "dining_room", "bathroom", "garden"};
}

SceneStyle ParseSceneStyle(const std::string& prompt) {
    const std::string p = ToLower(prompt);
    auto has = [&](const char* n) { return p.find(n) != std::string::npos; };
    SceneStyle s;
    if (has("modern") || has("minimalist") || has("minimal") || has("sleek") ||
        has("contemporary") || has("scandi")) {
        s.warmth = -0.55f;
        s.brightness = 0.2f;
        s.name = "modern";
    } else if (has("rustic") || has("cozy") || has("cosy") || has("farmhouse") ||
               has("vintage") || has("traditional") || has("warm ")) {
        s.warmth = 0.7f;
        s.brightness = -0.05f;
        s.name = "rustic";
    } else if (has("industrial") || has("loft") || has("concrete")) {
        s.warmth = -0.2f;
        s.brightness = -0.25f;
        s.name = "industrial";
    }
    // Brightness words stack on top of the base style.
    if (has("bright") || has("airy") || has("sunny") || has("luminous") || has("light-filled")) {
        s.brightness += 0.45f;
    }
    if (has("moody") || has("dark") || has("dim") || has("dramatic")) {
        s.brightness -= 0.45f;
    }
    s.warmth = std::clamp(s.warmth, -1.0f, 1.0f);
    s.brightness = std::clamp(s.brightness, -1.0f, 1.0f);
    return s;
}

ScenePromptRoute RouteScenePrompt(const std::string& prompt) {
    const std::string p = ToLower(prompt);
    auto has = [&](const char* n) { return p.find(n) != std::string::npos; };

    // Outdoor / biome prompts -> hero scenes (beach is the rebuilt real one).
    if (has("beach") || has("shore") || has("coast") || has("ocean") || has("seaside") || has("sand")) {
        return {"beach", ""};
    }
    if (has("forest") || has("woods") || has("creek") || has("jungle") || has("shrine")) {
        return {"forest_creek_shrine", ""};
    }
    if (has("desert") || has("dune") || has("relic")) {
        return {"desert_relic_gallery", ""};
    }
    if (has("neon") || has("cyberpunk") || has("alley") || has("market")) {
        return {"neon_alley_material_market", ""};
    }
    if (has("rain") || has("pavilion")) {
        return {"rain_glass_pavilion", ""};
    }

    // Indoor rooms -> procedural recipe.
    if (has("bedroom") || has("bed room") || has("bed")) {
        return {"recipe", "bedroom"};
    }
    if (has("kitchen") || has("cook")) {
        return {"recipe", "kitchen"};
    }
    if (has("dining") || has("dinner")) {
        return {"recipe", "dining_room"};
    }
    if (has("bathroom") || has("bath room") || has("washroom") || has("restroom") ||
        has("toilet") || has("shower")) {
        return {"recipe", "bathroom"};
    }
    if (has("garden") || has("patio") || has("backyard") || has("courtyard") ||
        has("terrace") || has("yard")) {
        return {"recipe", "garden"};
    }
    if (has("office") || has("study") || has("workspace") || has("desk")) {
        return {"recipe", "office"};
    }
    if (has("living") || has("lounge") || has("sofa") || has("couch") || has("room")) {
        return {"recipe", "living_room"};
    }

    // Default: a furnished living room.
    return {"recipe", "living_room"};
}

void BuildDiningRoom(std::vector<std::shared_ptr<SceneCommand>>& out, const Scene::AssetCatalog& cat, FootprintCache& c) {
    BuildRoomShell(out, cat, c, 6.6f, 6.4f, glm::vec4(0.46f, 0.40f, 0.34f, 1.0f)); // warm wood floor
    const float tz = -1.1f; // table centre
    Place(out, cat, c, "rugRounded", 3.0f, 0.0f, tz, 0.0f, glm::vec4(0.40f, 0.30f, 0.26f, 1.0f));
    Place(out, cat, c, "WoodenTable_02", 1.7f, 0.0f, tz, 0.0f, kTex);           // focal: high-poly dining table
    // Six high-poly chairs pulled up to the table, each facing in.
    Place(out, cat, c, "WoodenChair_01", 0.55f, -0.95f, tz - 0.5f, -90.0f, kTex);  // left side
    Place(out, cat, c, "WoodenChair_01", 0.55f, -0.95f, tz + 0.5f, -90.0f, kTex);
    Place(out, cat, c, "WoodenChair_01", 0.55f, 0.95f, tz - 0.5f, 90.0f, kTex);    // right side
    Place(out, cat, c, "WoodenChair_01", 0.55f, 0.95f, tz + 0.5f, 90.0f, kTex);
    Place(out, cat, c, "WoodenChair_01", 0.55f, 0.0f, tz - 1.05f, 0.0f, kTex);     // head
    Place(out, cat, c, "WoodenChair_01", 0.55f, 0.0f, tz + 1.05f, 180.0f, kTex);   // foot
    // Sideboard against the left wall with decor on top.
    Place(out, cat, c, "cabinetTelevisionDoors", 1.4f, -2.7f, 1.3f, 90.0f);
    PlaceOn(out, cat, c, "books", 0.3f, -2.7f, 0.62f, 1.3f, 12.0f);
    PlaceOn(out, cat, c, "plantSmall2", 0.25f, -2.7f, 0.62f, 0.7f, 0.0f);
    // Corner greenery, kept at the BACK so it doesn't crowd the camera.
    Place(out, cat, c, "pottedPlant", 0.55f, 2.7f, -2.4f, 0.0f);  // back-right corner
    Place(out, cat, c, "pottedPlant", 0.5f, -2.7f, -2.4f, 0.0f);  // back-left corner
}

void BuildBathroom(std::vector<std::shared_ptr<SceneCommand>>& out, const Scene::AssetCatalog& cat, FootprintCache& c) {
    BuildRoomShell(out, cat, c, 5.0f, 4.8f, glm::vec4(0.60f, 0.62f, 0.64f, 1.0f)); // tile floor
    Place(out, cat, c, "bathtub", 2.3f, 0.0f, -1.6f, 0.0f);                  // focal: tub on the back wall
    Place(out, cat, c, "toilet", 0.7f, -1.7f, -1.2f, 90.0f);                 // left wall
    Place(out, cat, c, "bathroomSink", 0.8f, 1.7f, -1.2f, -90.0f);           // right wall
    PlaceOn(out, cat, c, "bathroomMirror", 0.7f, 2.3f, 1.45f, -1.2f, -90.0f); // mirror above the sink
    Place(out, cat, c, "bathroomCabinet", 0.7f, -1.7f, 0.5f, 90.0f);
    Place(out, cat, c, "rugDoormat", 1.1f, 0.1f, 0.7f, 0.0f, glm::vec4(0.40f, 0.45f, 0.50f, 1.0f));
    Place(out, cat, c, "plantSmall3", 0.3f, 1.8f, 1.4f, 0.0f);
}

// Outdoor garden/patio: a lawn (no room shell) with a patio seating set framed by
// trees, bushes, rocks and grass tufts using the naturalistic nature meshes.
void BuildGarden(std::vector<std::shared_ptr<SceneCommand>>& out, const Scene::AssetCatalog& cat, FootprintCache& c) {
    // Lawn as a thin wide cube slab (renders reliably, like the room walls; the
    // Plane primitive did not show in the open outdoor view).
    {
        auto ground = std::make_shared<AddEntityCommand>();
        ground->entityType = AddEntityCommand::EntityType::Cube;
        ground->name = "Garden_Lawn";
        ground->position = glm::vec3(0.0f, -0.06f, 0.0f);
        ground->scale = glm::vec3(16.0f, 0.12f, 15.0f);
        ground->color = glm::vec4(0.22f, 0.33f, 0.15f, 1.0f); // deeper grass so props read
        ground->roughness = 0.95f;
        ground->metallic = 0.0f;
        ground->allowPlacementJitter = false;
        ground->disableCollisionAvoidance = true;
        out.push_back(std::move(ground));
    }
    Place(out, cat, c, "rugSquare", 3.6f, 0.3f, -0.6f, 0.0f, glm::vec4(0.58f, 0.56f, 0.52f, 1.0f)); // stone patio
    // Patio seating set (known-good Kenney pieces that scale correctly).
    Place(out, cat, c, "tableCloth", 1.4f, 0.3f, -0.7f, 0.0f);          // patio dining table
    Place(out, cat, c, "chairCushion", 0.6f, -0.7f, -0.7f, -90.0f);     // upright chairs around it
    Place(out, cat, c, "chairCushion", 0.6f, 1.3f, -0.7f, 90.0f);
    Place(out, cat, c, "chairCushion", 0.6f, 0.3f, 0.5f, 180.0f);
    Place(out, cat, c, "bench", 1.5f, 0.3f, -2.1f, 0.0f);               // bench behind
    Place(out, cat, c, "Barrel_01", 0.6f, 2.3f, -2.0f, 0.0f); // planter/decor (lantern blooms out in daylight)
    // Trees / greenery framing the garden.
    Place(out, cat, c, "dead_tree_trunk", 3.6f, -5.0f, -3.2f, 0.0f, glm::vec4(0.34f, 0.26f, 0.17f, 1.0f));
    Place(out, cat, c, "wild_rooibos_bush", 1.7f, -4.2f, -1.4f, 0.0f);
    Place(out, cat, c, "wild_rooibos_bush", 1.5f, 4.4f, -2.1f, 40.0f);
    Place(out, cat, c, "fern_02", 1.1f, 3.8f, 0.6f, 0.0f);
    Place(out, cat, c, "rock_moss_set_01", 1.3f, -3.4f, 1.4f, 20.0f);
    Place(out, cat, c, "boulder_01", 1.6f, 4.6f, 1.1f, -30.0f);
    Place(out, cat, c, "tree_stump_01", 0.8f, -5.0f, 0.6f, 0.0f);
    // Grass tufts scattered across the lawn.
    Place(out, cat, c, "grass_bermuda_01", 1.0f, -2.2f, 2.6f, 0.0f);
    Place(out, cat, c, "grass_bermuda_01", 0.9f, 2.5f, 2.7f, 90.0f);
    Place(out, cat, c, "grass_bermuda_01", 0.95f, 0.4f, 3.0f, 45.0f);
}

std::vector<std::shared_ptr<SceneCommand>> BuildSceneRecipe(const std::string& recipeName,
                                                            const Scene::AssetCatalog& catalog,
                                                            std::uint32_t /*seed*/) {
    std::vector<std::shared_ptr<SceneCommand>> out;
    if (!catalog.IsLoaded()) {
        spdlog::warn("SceneRecipes: catalog not loaded; cannot build '{}'", recipeName);
        return out;
    }
    FootprintCache cache;
    if (recipeName == "living_room") {
        BuildLivingRoom(out, catalog, cache);
    } else if (recipeName == "bedroom") {
        BuildBedroom(out, catalog, cache);
    } else if (recipeName == "office") {
        BuildOffice(out, catalog, cache);
    } else if (recipeName == "kitchen") {
        BuildKitchen(out, catalog, cache);
    } else if (recipeName == "dining_room") {
        BuildDiningRoom(out, catalog, cache);
    } else if (recipeName == "bathroom") {
        BuildBathroom(out, catalog, cache);
    } else if (recipeName == "garden") {
        BuildGarden(out, catalog, cache);
    } else {
        spdlog::warn("SceneRecipes: unknown recipe '{}'", recipeName);
        return out;
    }
    spdlog::info("SceneRecipes: '{}' produced {} commands from real catalog assets", recipeName, out.size());
    return out;
}

} // namespace Cortex::LLM
