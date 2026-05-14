#pragma once

#include "Scene/SemanticGraph.h"

#include <string>
#include <vector>

namespace Cortex::Scene {

struct SemanticEcsEntityJob {
    std::string semanticId;
    std::string editableGroup;
    std::string semanticType;
    std::string materialIntent;
};

struct SemanticRendererResourceJob {
    std::string semanticId;
    uint64_t textureBytes = 0;
    uint32_t texturePages = 0;
    uint32_t psoSignatures = 0;
    uint32_t blasBuilds = 0;
    uint32_t tlasInstances = 0;
    SemanticInvalidation invalidation;
};

struct CompiledSemanticRuntimeScene {
    std::vector<SemanticEcsEntityJob> ecsEntityJobs;
    std::vector<SemanticRendererResourceJob> rendererResourceJobs;
    std::vector<std::string> errors;
};

[[nodiscard]] CompiledSemanticRuntimeScene CompileSemanticGraphForRuntime(const SemanticSceneGraph& graph);
[[nodiscard]] std::string RunSemanticRuntimeCompilerSelfTestJson();

} // namespace Cortex::Scene
