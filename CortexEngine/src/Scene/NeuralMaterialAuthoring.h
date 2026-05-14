#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Cortex::Scene {

struct NeuralPBRTextureSlot {
    std::string usage;
    std::string assetUri;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t seed = 0;
};

struct NeuralMaterialAuthoringRequest {
    std::string materialId;
    std::string prompt;
    std::string generator;
    uint32_t version = 1;
    std::vector<NeuralPBRTextureSlot> textureSlots;
};

struct NeuralPBRMaterialAsset {
    std::string materialId;
    std::string prompt;
    std::string generator;
    uint32_t version = 0;
    bool generated = false;
    bool editable = false;
    std::string validationReport;
    std::vector<NeuralPBRTextureSlot> textureSlots;
};

struct NeuralMaterialAuthoringResult {
    bool accepted = false;
    NeuralPBRMaterialAsset asset;
    std::vector<std::string> errors;
};

[[nodiscard]] NeuralMaterialAuthoringResult AuthorNeuralPBRMaterial(const NeuralMaterialAuthoringRequest& request);
[[nodiscard]] NeuralMaterialAuthoringResult ValidateNeuralPBRMaterial(const NeuralPBRMaterialAsset& asset);
[[nodiscard]] std::string SerializeNeuralPBRMaterial(const NeuralPBRMaterialAsset& asset);
[[nodiscard]] NeuralPBRMaterialAsset DeserializeNeuralPBRMaterial(const std::string& jsonText);
[[nodiscard]] std::string RunNeuralMaterialAuthoringSelfTestJson();

} // namespace Cortex::Scene
