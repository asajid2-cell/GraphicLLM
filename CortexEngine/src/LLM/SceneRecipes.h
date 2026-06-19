#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Cortex::Scene {
class AssetCatalog;
}

namespace Cortex::LLM {

struct SceneCommand;

// Procedural scene recipes: the deterministic, real-asset half of the scene
// creator. A recipe composes REAL catalog meshes (Kenney furniture + registry)
// into a believable scene (arranged furniture, optional room shell), emitting
// standard SceneCommands that flow through the normal CommandQueue execution
// path. This is the robust default — it needs no LLM and never falls back to
// primitive stand-ins.

// If the prompt clearly asks to build a known room/scene, return its canonical
// recipe key (e.g. "living_room", "kitchen", "bedroom", "office"); else nullopt.
[[nodiscard]] std::optional<std::string> MatchSceneRecipe(const std::string& prompt);

// Text-to-scene routing: maps ANY free-text prompt to the best-matching scene.
// `sceneString` is an engine scene key (e.g. "recipe", "beach", "forest_creek_shrine");
// `recipe` is the recipe name when sceneString=="recipe" (else empty). Always
// returns a usable scene (defaults to a living-room recipe).
struct ScenePromptRoute {
    std::string sceneString;
    std::string recipe;
};
[[nodiscard]] ScenePromptRoute RouteScenePrompt(const std::string& prompt);

// All recipe keys this module can build.
[[nodiscard]] std::vector<std::string> AvailableSceneRecipes();

// A style read from descriptive words in the prompt (modern/rustic/cozy/bright/
// moody/industrial...). Lets the same room recipe take on a different mood.
// warmth: -1 cool .. +1 warm; brightness: -1 moody .. +1 airy.
struct SceneStyle {
    float warmth = 0.0f;
    float brightness = 0.0f;
    std::string name; // human-readable style label ("modern", "rustic", ...) or empty
    // Furniture aesthetic: true -> high-poly classic/ornate set (Poly Haven, the
    // higher-quality default); false -> clean modern set (Kenney). Set false only
    // when the prompt explicitly asks for a modern/minimal look.
    bool classic = true;
};
[[nodiscard]] SceneStyle ParseSceneStyle(const std::string& prompt);

// Build the command batch that assembles a named scene from real catalog assets.
// Self-calibrates to each asset's measured footprint so layout stays correct as
// assets change. Returns empty if the recipe is unknown or the catalog cannot
// supply the needed assets.
[[nodiscard]] std::vector<std::shared_ptr<SceneCommand>> BuildSceneRecipe(const std::string& recipeName,
                                                                          const Scene::AssetCatalog& catalog,
                                                                          std::uint32_t seed = 0,
                                                                          const SceneStyle& style = {});

} // namespace Cortex::LLM
