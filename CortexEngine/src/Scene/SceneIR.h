#pragma once

#include "Scene/SceneTransaction.h"

#include <string>
#include <vector>

namespace Cortex::Scene {

enum class SceneIRSource : uint8_t {
    Text = 0,
    Speech,
    UI,
    Procedural
};

enum class SceneIROpType : uint8_t {
    CreateObject = 0,
    ModifyMaterialIntent,
    SelectSemanticGroup,
    FocusCamera
};

struct SceneIRCommand {
    SceneIRSource source = SceneIRSource::Text;
    SceneIROpType op = SceneIROpType::CreateObject;
    std::string requestId;
    std::string targetSemanticId;
    std::string targetGroup;
    std::string semanticType;
    std::string support;
    std::string region;
    std::string materialIntent;
    std::string prompt;
    uint64_t seed = 0;
    std::string generator;
};

struct SceneIRResolution {
    bool accepted = false;
    SceneTransaction transaction;
    std::vector<std::string> errors;
};

class SceneIRResolver {
public:
    [[nodiscard]] SceneIRResolution Resolve(const SceneIRCommand& command,
                                            const SemanticSceneGraph& graph) const;
};

[[nodiscard]] SceneIRCommand MakeTextSceneIR(std::string requestId,
                                             std::string targetGroup,
                                             std::string materialIntent);
[[nodiscard]] SceneIRCommand MakeSpeechSceneIR(std::string requestId,
                                               std::string targetGroup,
                                               std::string materialIntent);
[[nodiscard]] SceneIRCommand MakeUISceneIR(std::string requestId,
                                           std::string targetGroup,
                                           std::string materialIntent);
[[nodiscard]] SceneIRCommand MakeProceduralSceneIR(std::string requestId,
                                                   std::string targetGroup,
                                                   std::string materialIntent);

[[nodiscard]] const char* ToString(SceneIRSource source);
[[nodiscard]] const char* ToString(SceneIROpType op);
[[nodiscard]] std::string RunSceneIRSelfTestJson();

} // namespace Cortex::Scene
