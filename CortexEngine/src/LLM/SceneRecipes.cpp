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
    if (has("sofa") || has("couch") || has("chair") || has("cushion") || has("stool") || has("bed") ||
        has("lounge")) {
        return glm::vec4(0.43f, 0.46f, 0.52f, 1.0f); // upholstery blue-gray
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
        return glm::vec4(0.88f, 0.80f, 0.55f, 1.0f); // warm shade
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
    cmd->color = (color == glm::vec4(1.0f)) ? ColorForKey(ToLower(key)) : color;
    cmd->roughness = 0.7f;
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

// A simple ground plane sized to the room (primitive, sits at y=0).
void PlaceFloor(std::vector<std::shared_ptr<SceneCommand>>& out, float width, float depth, const glm::vec4& color) {
    auto cmd = std::make_shared<AddEntityCommand>();
    cmd->entityType = AddEntityCommand::EntityType::Plane; // CreatePlane is 2x2 in XZ
    cmd->name = "Floor";
    cmd->position = glm::vec3(0.0f, 0.0f, 0.0f);
    cmd->scale = glm::vec3(width * 0.5f, 1.0f, depth * 0.5f);
    cmd->color = color;
    cmd->roughness = 0.9f;
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
}

// ---- recipes ---------------------------------------------------------------
// Facing convention: yaw 0 faces +Z. These yaws are an initial layout; a visual
// pass may flip a piece 180 deg if a Kenney asset's authored front differs.

void BuildLivingRoom(std::vector<std::shared_ptr<SceneCommand>>& out, const Scene::AssetCatalog& cat, FootprintCache& c) {
    // Cozier footprint so the seating group fills the room instead of floating.
    BuildRoomShell(out, cat, c, 6.8f, 6.6f, glm::vec4(0.50f, 0.43f, 0.36f, 1.0f)); // warm wood floor
    Place(out, cat, c, "rugRectangle", 2.9f, 0.0f, -0.3f, 0.0f, glm::vec4(0.47f, 0.30f, 0.26f, 1.0f));
    Place(out, cat, c, "loungeSofa", 2.3f, 0.0f, -2.0f, 0.0f);          // back wall, faces +Z
    PlaceOn(out, cat, c, "pillowBlue", 0.45f, -0.55f, 0.48f, -2.15f, 8.0f);  // throw pillows on the sofa
    PlaceOn(out, cat, c, "pillow", 0.42f, 0.55f, 0.48f, -2.15f, -10.0f);
    Place(out, cat, c, "loungeChair", 0.85f, -2.05f, -0.3f, 50.0f);     // angled into the group
    Place(out, cat, c, "loungeChair", 0.85f, 2.05f, -0.3f, -50.0f);
    Place(out, cat, c, "tableCoffee", 1.2f, 0.0f, -0.7f, 0.0f);         // centre of the seating
    PlaceOn(out, cat, c, "books", 0.35f, 0.15f, 0.42f, -0.7f, 20.0f);   // on the coffee table
    Place(out, cat, c, "lampRoundFloor", 0.4f, -2.7f, -2.1f, 0.0f);     // floor lamp beside the sofa
    Place(out, cat, c, "sideTable", 0.5f, 2.7f, -2.1f, 0.0f);           // side table other end
    PlaceOn(out, cat, c, "lampRoundTable", 0.28f, 2.7f, 0.46f, -2.1f, 0.0f); // table lamp on it
    Place(out, cat, c, "bookcaseOpen", 1.1f, -3.0f, 1.0f, 90.0f);       // side wall
    Place(out, cat, c, "cabinetTelevision", 1.6f, 0.0f, 2.7f, 180.0f);  // front wall (behind camera)
    Place(out, cat, c, "televisionModern", 1.2f, 0.0f, 2.5f, 180.0f);
    Place(out, cat, c, "pottedPlant", 0.6f, 2.9f, 1.1f, 0.0f);          // corner greenery
    Place(out, cat, c, "pottedPlant", 0.55f, -3.0f, -2.7f, 0.0f);
}

void BuildBedroom(std::vector<std::shared_ptr<SceneCommand>>& out, const Scene::AssetCatalog& cat, FootprintCache& c) {
    BuildRoomShell(out, cat, c, 6.4f, 6.2f, glm::vec4(0.48f, 0.44f, 0.46f, 1.0f));
    Place(out, cat, c, "rugRound", 2.7f, -0.1f, 0.2f, 0.0f, glm::vec4(0.46f, 0.40f, 0.48f, 1.0f));
    Place(out, cat, c, "bedDouble", 2.7f, -0.2f, -1.5f, 0.0f);          // focal: big, back-centre, faces camera
    PlaceOn(out, cat, c, "pillowLong", 0.75f, -0.6f, 0.54f, -2.35f, 0.0f);   // pillows at the head
    PlaceOn(out, cat, c, "pillowBlue", 0.4f, 0.35f, 0.58f, -2.3f, 6.0f);
    Place(out, cat, c, "sideTable", 0.5f, -1.9f, -2.3f, 0.0f);
    Place(out, cat, c, "sideTable", 0.5f, 1.5f, -2.3f, 0.0f);
    PlaceOn(out, cat, c, "lampSquareTable", 0.3f, 1.5f, 0.46f, -2.3f, 0.0f);  // lamp on nightstand
    PlaceOn(out, cat, c, "books", 0.26f, -1.9f, 0.46f, -2.3f, 15.0f);          // books on the other
    Place(out, cat, c, "bookcaseClosed", 1.1f, -2.85f, 1.1f, 90.0f);    // left wall, away from camera
    Place(out, cat, c, "coatRackStanding", 0.45f, -2.7f, -2.5f, 0.0f);
    Place(out, cat, c, "pottedPlant", 0.5f, 2.6f, -2.4f, 0.0f);         // back-right corner accent
}

void BuildOffice(std::vector<std::shared_ptr<SceneCommand>>& out, const Scene::AssetCatalog& cat, FootprintCache& c) {
    BuildRoomShell(out, cat, c, 6.4f, 6.2f, glm::vec4(0.46f, 0.46f, 0.48f, 1.0f));
    Place(out, cat, c, "rugSquare", 2.4f, -0.4f, -0.3f, 0.0f, glm::vec4(0.33f, 0.34f, 0.40f, 1.0f));
    Place(out, cat, c, "deskCorner", 1.9f, -0.5f, -1.5f, 8.0f);          // focal: back-centre
    Place(out, cat, c, "chairDesk", 0.8f, -0.5f, -0.55f, 180.0f);        // seated at the desk, faces it
    PlaceOn(out, cat, c, "computerScreen", 0.55f, -0.85f, 0.74f, -1.75f, 4.0f);  // monitor on the desk
    PlaceOn(out, cat, c, "computerKeyboard", 0.4f, -0.7f, 0.74f, -1.25f, 4.0f);  // keyboard
    PlaceOn(out, cat, c, "lampSquareTable", 0.26f, 0.35f, 0.74f, -1.7f, 0.0f);   // desk lamp
    Place(out, cat, c, "bookcaseOpen", 1.2f, -2.85f, 0.9f, 90.0f);       // left wall
    Place(out, cat, c, "bookcaseClosedWide", 1.5f, -1.3f, -2.95f, 0.0f); // back wall
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
    return std::nullopt;
}

std::vector<std::string> AvailableSceneRecipes() {
    return {"living_room", "bedroom", "office", "kitchen", "dining_room", "bathroom"};
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
    Place(out, cat, c, "tableCloth", 1.7f, 0.0f, tz, 0.0f);                     // focal: dining table
    // Six chairs pulled up to the table, each facing in.
    Place(out, cat, c, "chairCushion", 0.55f, -0.95f, tz - 0.5f, -90.0f);       // left side
    Place(out, cat, c, "chairCushion", 0.55f, -0.95f, tz + 0.5f, -90.0f);
    Place(out, cat, c, "chairCushion", 0.55f, 0.95f, tz - 0.5f, 90.0f);         // right side
    Place(out, cat, c, "chairCushion", 0.55f, 0.95f, tz + 0.5f, 90.0f);
    Place(out, cat, c, "chairCushion", 0.55f, 0.0f, tz - 1.05f, 0.0f);          // head
    Place(out, cat, c, "chairCushion", 0.55f, 0.0f, tz + 1.05f, 180.0f);        // foot
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
    } else {
        spdlog::warn("SceneRecipes: unknown recipe '{}'", recipeName);
        return out;
    }
    spdlog::info("SceneRecipes: '{}' produced {} commands from real catalog assets", recipeName, out.size());
    return out;
}

} // namespace Cortex::LLM
