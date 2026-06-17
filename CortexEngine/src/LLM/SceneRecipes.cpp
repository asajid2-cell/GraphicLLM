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
    cmd->color = color;
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

// ---- recipes ---------------------------------------------------------------
// Facing convention: yaw 0 faces +Z. These yaws are an initial layout; a visual
// pass may flip a piece 180 deg if a Kenney asset's authored front differs.

void BuildLivingRoom(std::vector<std::shared_ptr<SceneCommand>>& out, const Scene::AssetCatalog& cat, FootprintCache& c) {
    PlaceFloor(out, 7.5f, 7.5f, glm::vec4(0.55f, 0.5f, 0.46f, 1.0f));
    Place(out, cat, c, "rugRectangle", 3.0f, 0.0f, -0.4f, 0.0f, glm::vec4(0.4f, 0.32f, 0.3f, 1.0f));
    Place(out, cat, c, "loungeSofa", 2.1f, 0.0f, -2.3f, 0.0f);     // back, faces +Z
    Place(out, cat, c, "tableCoffee", 1.1f, 0.0f, -0.5f, 0.0f);
    Place(out, cat, c, "loungeChair", 0.8f, -2.2f, -0.6f, 60.0f);
    Place(out, cat, c, "loungeChair", 0.8f, 2.2f, -0.6f, -60.0f);
    Place(out, cat, c, "cabinetTelevision", 1.6f, 0.0f, 2.6f, 180.0f); // front, faces -Z
    Place(out, cat, c, "televisionModern", 1.2f, 0.0f, 2.4f, 180.0f);
    Place(out, cat, c, "bookcaseOpen", 1.0f, -3.2f, 1.5f, 90.0f);
    Place(out, cat, c, "lampRoundFloor", 0.4f, 3.0f, -2.6f, 0.0f);
    Place(out, cat, c, "pottedPlant", 0.5f, -3.0f, -2.6f, 0.0f);
}

void BuildBedroom(std::vector<std::shared_ptr<SceneCommand>>& out, const Scene::AssetCatalog& cat, FootprintCache& c) {
    PlaceFloor(out, 7.0f, 7.0f, glm::vec4(0.5f, 0.46f, 0.5f, 1.0f));
    Place(out, cat, c, "bedDouble", 2.2f, 0.0f, -2.0f, 0.0f);
    Place(out, cat, c, "sideTable", 0.5f, -1.6f, -2.8f, 0.0f);
    Place(out, cat, c, "sideTable", 0.5f, 1.6f, -2.8f, 0.0f);
    Place(out, cat, c, "lampSquareTable", 0.3f, -1.6f, -2.8f, 0.0f);
    Place(out, cat, c, "lampSquareTable", 0.3f, 1.6f, -2.8f, 0.0f);
    Place(out, cat, c, "bookcaseClosed", 1.0f, 3.0f, 0.0f, -90.0f);
    Place(out, cat, c, "coatRackStanding", 0.5f, -3.0f, 2.4f, 0.0f);
    Place(out, cat, c, "rugRound", 2.2f, 0.0f, 1.2f, 0.0f, glm::vec4(0.45f, 0.4f, 0.5f, 1.0f));
}

void BuildOffice(std::vector<std::shared_ptr<SceneCommand>>& out, const Scene::AssetCatalog& cat, FootprintCache& c) {
    PlaceFloor(out, 7.0f, 7.0f, glm::vec4(0.5f, 0.5f, 0.52f, 1.0f));
    Place(out, cat, c, "deskCorner", 1.6f, -1.0f, -2.0f, 0.0f);
    Place(out, cat, c, "chairDesk", 0.7f, -1.0f, -1.0f, 180.0f);
    Place(out, cat, c, "computerScreen", 0.5f, -1.4f, -2.4f, 180.0f);
    Place(out, cat, c, "computerKeyboard", 0.45f, -1.0f, -2.0f, 180.0f);
    Place(out, cat, c, "bookcaseOpen", 1.0f, 3.0f, 0.0f, -90.0f);
    Place(out, cat, c, "bookcaseClosedWide", 1.4f, 0.5f, 3.0f, 180.0f);
    Place(out, cat, c, "pottedPlant", 0.5f, 3.0f, -2.8f, 0.0f);
    Place(out, cat, c, "trashcan", 0.3f, 0.2f, -1.8f, 0.0f);
}

void BuildKitchen(std::vector<std::shared_ptr<SceneCommand>>& out, const Scene::AssetCatalog& cat, FootprintCache& c) {
    PlaceFloor(out, 7.0f, 7.0f, glm::vec4(0.6f, 0.58f, 0.55f, 1.0f));
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
