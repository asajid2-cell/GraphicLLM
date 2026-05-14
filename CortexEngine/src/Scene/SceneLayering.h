#pragma once

#include "Scene/SceneTransaction.h"

#include <string>
#include <vector>

namespace Cortex::Scene {

enum class SceneLayerKind {
    AuthoredBaseline,
    GeneratedProposal,
    UserOverride,
    MaterialVariant,
    ValidationAnnotation
};

struct SceneLayerObject {
    SemanticObject object;
    std::string layerId;
    SceneLayerKind layerKind = SceneLayerKind::AuthoredBaseline;
};

struct SceneLayer {
    std::string id;
    SceneLayerKind kind = SceneLayerKind::AuthoredBaseline;
    uint32_t priority = 0;
    std::vector<SceneLayerObject> objects;
};

struct SceneLayerResolution {
    bool accepted = false;
    SceneTransaction transaction;
    std::vector<std::string> errors;
};

[[nodiscard]] SceneLayerResolution ResolveSceneLayersToTransaction(const std::vector<SceneLayer>& layers);
[[nodiscard]] const char* ToString(SceneLayerKind kind);
[[nodiscard]] std::string RunSceneLayeringSelfTestJson();

} // namespace Cortex::Scene
