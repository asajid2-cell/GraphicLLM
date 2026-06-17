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
           const glm::vec4& color = glm::vec4(1.0f)) {
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
    cmd->autoPlace = false;
    cmd->allowPlacementJitter = false;
    cmd->disableCollisionAvoidance = true; // recipe positions are deliberate
    out.push_back(std::move(cmd));
    return true;
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
    const glm::vec4 wallColor(0.82f, 0.78f, 0.72f, 1.0f);
    const float wallH = 2.8f;
    const float wallTh = 0.16f;
    const float hw = width * 0.5f;
    const float hd = depth * 0.5f;

    auto boxWall = [&](const std::string& tag, float cx, float cz, float sx, float sz) {
        auto cmd = std::make_shared<AddEntityCommand>();
        cmd->entityType = AddEntityCommand::EntityType::Cube; // unit cube (+/-0.5)
        cmd->name = tag;
        cmd->position = glm::vec3(cx, wallH * 0.5f, cz); // base on the floor
        cmd->scale = glm::vec3(sx, wallH, sz);
        cmd->color = wallColor;
        cmd->metallic = 0.0f;
        cmd->roughness = 0.92f;
        cmd->allowPlacementJitter = false;
        cmd->disableCollisionAvoidance = true;
        out.push_back(std::move(cmd));
    };

    boxWall("Wall_Back", 0.0f, -hd, width, wallTh);
    boxWall("Wall_Left", -hw, 0.0f, wallTh, depth);
    boxWall("Wall_Right", hw, 0.0f, wallTh, depth);

    const float doorW = 2.4f;
    const float segW = (width - doorW) * 0.5f;
    if (segW > 0.1f) {
        const float off = doorW * 0.5f + segW * 0.5f;
        boxWall("Wall_FrontL", -off, hd, segW, wallTh);
        boxWall("Wall_FrontR", off, hd, segW, wallTh);
    }
}

// ---- recipes ---------------------------------------------------------------
// Facing convention: yaw 0 faces +Z. These yaws are an initial layout; a visual
// pass may flip a piece 180 deg if a Kenney asset's authored front differs.

void BuildLivingRoom(std::vector<std::shared_ptr<SceneCommand>>& out, const Scene::AssetCatalog& cat, FootprintCache& c) {
    // Cozier footprint so the seating group fills the room instead of floating.
    BuildRoomShell(out, cat, c, 6.8f, 6.6f, glm::vec4(0.50f, 0.43f, 0.36f, 1.0f)); // warm wood floor
    Place(out, cat, c, "rugRectangle", 2.9f, 0.0f, -0.3f, 0.0f, glm::vec4(0.47f, 0.30f, 0.26f, 1.0f));
    Place(out, cat, c, "loungeSofa", 2.3f, 0.0f, -2.0f, 0.0f);          // back wall, faces +Z
    Place(out, cat, c, "loungeChair", 0.85f, -2.05f, -0.3f, 50.0f);     // angled into the group
    Place(out, cat, c, "loungeChair", 0.85f, 2.05f, -0.3f, -50.0f);
    Place(out, cat, c, "tableCoffee", 1.2f, 0.0f, -0.7f, 0.0f);         // centre of the seating
    Place(out, cat, c, "lampRoundFloor", 0.4f, -2.7f, -2.1f, 0.0f);     // floor lamp beside the sofa
    Place(out, cat, c, "sideTable", 0.5f, 2.7f, -2.1f, 0.0f);           // side table other end
    Place(out, cat, c, "bookcaseOpen", 1.1f, -3.0f, 1.0f, 90.0f);       // side wall
    Place(out, cat, c, "cabinetTelevision", 1.6f, 0.0f, 2.7f, 180.0f);  // front wall (behind camera)
    Place(out, cat, c, "televisionModern", 1.2f, 0.0f, 2.5f, 180.0f);
    Place(out, cat, c, "pottedPlant", 0.6f, 2.9f, 1.1f, 0.0f);          // corner greenery
    Place(out, cat, c, "pottedPlant", 0.55f, -3.0f, -2.7f, 0.0f);
    Place(out, cat, c, "plantSmall1", 0.3f, 2.7f, -2.1f, 0.0f);         // small plant on side-table spot
}

void BuildBedroom(std::vector<std::shared_ptr<SceneCommand>>& out, const Scene::AssetCatalog& cat, FootprintCache& c) {
    BuildRoomShell(out, cat, c, 6.4f, 6.2f, glm::vec4(0.48f, 0.44f, 0.46f, 1.0f));
    Place(out, cat, c, "rugRound", 2.7f, -0.1f, 0.2f, 0.0f, glm::vec4(0.46f, 0.40f, 0.48f, 1.0f));
    Place(out, cat, c, "bedDouble", 2.7f, -0.2f, -1.5f, 0.0f);          // focal: big, back-centre, faces camera
    Place(out, cat, c, "sideTable", 0.5f, -1.9f, -2.3f, 0.0f);
    Place(out, cat, c, "sideTable", 0.5f, 1.5f, -2.3f, 0.0f);
    Place(out, cat, c, "lampSquareTable", 0.3f, 1.5f, -2.3f, 0.0f);
    Place(out, cat, c, "bookcaseClosed", 1.1f, -2.85f, 1.1f, 90.0f);    // left wall, away from camera
    Place(out, cat, c, "coatRackStanding", 0.45f, -2.7f, -2.5f, 0.0f);
    Place(out, cat, c, "pottedPlant", 0.5f, 2.6f, -2.4f, 0.0f);         // back-right corner accent
}

void BuildOffice(std::vector<std::shared_ptr<SceneCommand>>& out, const Scene::AssetCatalog& cat, FootprintCache& c) {
    BuildRoomShell(out, cat, c, 6.4f, 6.2f, glm::vec4(0.46f, 0.46f, 0.48f, 1.0f));
    Place(out, cat, c, "rugSquare", 2.4f, -0.4f, -0.3f, 0.0f, glm::vec4(0.33f, 0.34f, 0.40f, 1.0f));
    Place(out, cat, c, "deskCorner", 1.9f, -0.5f, -1.5f, 8.0f);          // focal: back-centre
    Place(out, cat, c, "chairDesk", 0.8f, -0.5f, -0.5f, 180.0f);         // seated at the desk
    Place(out, cat, c, "computerScreen", 0.5f, -0.9f, -1.9f, 170.0f);
    Place(out, cat, c, "bookcaseOpen", 1.2f, -2.85f, 0.9f, 90.0f);       // left wall
    Place(out, cat, c, "bookcaseClosedWide", 1.5f, -1.3f, -2.95f, 0.0f); // back wall
    Place(out, cat, c, "pottedPlant", 0.55f, 2.6f, -2.3f, 0.0f);         // back-right corner
    Place(out, cat, c, "trashcan", 0.3f, 0.8f, -1.1f, 0.0f);
}

void BuildKitchen(std::vector<std::shared_ptr<SceneCommand>>& out, const Scene::AssetCatalog& cat, FootprintCache& c) {
    BuildRoomShell(out, cat, c, 7.0f, 7.0f, glm::vec4(0.6f, 0.58f, 0.55f, 1.0f));
    // Counter run along the back wall.
    const float backZ = -2.8f;
    Place(out, cat, c, "kitchenCabinet", 0.9f, -2.0f, backZ, 0.0f);
    Place(out, cat, c, "kitchenSink", 0.9f, -1.0f, backZ, 0.0f);
    Place(out, cat, c, "kitchenStove", 0.9f, 0.0f, backZ, 0.0f);
    Place(out, cat, c, "kitchenCabinet", 0.9f, 1.0f, backZ, 0.0f);
    Place(out, cat, c, "kitchenFridge", 0.9f, 2.2f, backZ, 0.0f);
    Place(out, cat, c, "hoodModern", 0.9f, 0.0f, backZ, 0.0f);
    // Island bar with stools.
    Place(out, cat, c, "kitchenBar", 1.8f, 0.0f, 0.2f, 0.0f);
    Place(out, cat, c, "stoolBar", 0.4f, -0.6f, 1.1f, 180.0f);
    Place(out, cat, c, "stoolBar", 0.4f, 0.6f, 1.1f, 180.0f);
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
    return std::nullopt;
}

std::vector<std::string> AvailableSceneRecipes() {
    return {"living_room", "bedroom", "office", "kitchen"};
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
    if (has("office") || has("study") || has("workspace") || has("desk")) {
        return {"recipe", "office"};
    }
    if (has("living") || has("lounge") || has("sofa") || has("couch") || has("room")) {
        return {"recipe", "living_room"};
    }

    // Default: a furnished living room.
    return {"recipe", "living_room"};
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
    } else {
        spdlog::warn("SceneRecipes: unknown recipe '{}'", recipeName);
        return out;
    }
    spdlog::info("SceneRecipes: '{}' produced {} commands from real catalog assets", recipeName, out.size());
    return out;
}

} // namespace Cortex::LLM
