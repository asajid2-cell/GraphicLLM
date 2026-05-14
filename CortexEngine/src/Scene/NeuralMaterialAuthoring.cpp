#include "Scene/NeuralMaterialAuthoring.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <set>
#include <utility>

namespace Cortex::Scene {
namespace {

bool Blank(const std::string& value) {
    return value.find_first_not_of(" \t\r\n") == std::string::npos;
}

void AddError(std::vector<std::string>& errors, const std::string& message) {
    errors.push_back(message);
}

bool HasSlot(const std::set<std::string>& slots, const std::string& usage) {
    return slots.find(usage) != slots.end();
}

} // namespace

NeuralMaterialAuthoringResult ValidateNeuralPBRMaterial(const NeuralPBRMaterialAsset& asset) {
    NeuralMaterialAuthoringResult result;
    if (Blank(asset.materialId)) AddError(result.errors, "material id is required");
    if (Blank(asset.prompt)) AddError(result.errors, "prompt provenance is required");
    if (Blank(asset.generator)) AddError(result.errors, "generator provenance is required");
    if (asset.version == 0) AddError(result.errors, "version must be non-zero");
    if (!asset.generated) AddError(result.errors, "material must be marked generated");
    if (!asset.editable) AddError(result.errors, "material must be editable");

    std::set<std::string> slots;
    for (const auto& slot : asset.textureSlots) {
        if (Blank(slot.usage)) AddError(result.errors, "texture usage is required");
        if (Blank(slot.assetUri)) AddError(result.errors, "texture asset uri is required");
        if (slot.width == 0 || slot.height == 0) AddError(result.errors, "texture dimensions are required");
        if (slot.seed == 0) AddError(result.errors, "texture seed is required");
        slots.insert(slot.usage);
    }
    for (const auto& required : {"albedo", "normal", "roughness", "metalness"}) {
        if (!HasSlot(slots, required)) {
            AddError(result.errors, std::string("required PBR texture slot missing: ") + required);
        }
    }

    result.accepted = result.errors.empty();
    result.asset = asset;
    return result;
}

NeuralMaterialAuthoringResult AuthorNeuralPBRMaterial(const NeuralMaterialAuthoringRequest& request) {
    NeuralPBRMaterialAsset asset;
    asset.materialId = request.materialId;
    asset.prompt = request.prompt;
    asset.generator = request.generator;
    asset.version = request.version;
    asset.generated = true;
    asset.editable = true;
    asset.validationReport = "neural_material_authoring.v1";
    asset.textureSlots = request.textureSlots;
    return ValidateNeuralPBRMaterial(asset);
}

std::string SerializeNeuralPBRMaterial(const NeuralPBRMaterialAsset& asset) {
    nlohmann::json json;
    json["schema"] = "cortex.neural_pbr_material_asset.v1";
    json["material_id"] = asset.materialId;
    json["prompt"] = asset.prompt;
    json["generator"] = asset.generator;
    json["version"] = asset.version;
    json["generated"] = asset.generated;
    json["editable"] = asset.editable;
    json["validation_report"] = asset.validationReport;
    json["texture_slots"] = nlohmann::json::array();
    for (const auto& slot : asset.textureSlots) {
        json["texture_slots"].push_back({
            {"usage", slot.usage},
            {"asset_uri", slot.assetUri},
            {"width", slot.width},
            {"height", slot.height},
            {"seed", slot.seed}
        });
    }
    return json.dump(2);
}

NeuralPBRMaterialAsset DeserializeNeuralPBRMaterial(const std::string& jsonText) {
    const auto json = nlohmann::json::parse(jsonText);
    NeuralPBRMaterialAsset asset;
    asset.materialId = json.value("material_id", "");
    asset.prompt = json.value("prompt", "");
    asset.generator = json.value("generator", "");
    asset.version = json.value("version", 0u);
    asset.generated = json.value("generated", false);
    asset.editable = json.value("editable", false);
    asset.validationReport = json.value("validation_report", "");
    for (const auto& slotJson : json.value("texture_slots", nlohmann::json::array())) {
        NeuralPBRTextureSlot slot;
        slot.usage = slotJson.value("usage", "");
        slot.assetUri = slotJson.value("asset_uri", "");
        slot.width = slotJson.value("width", 0u);
        slot.height = slotJson.value("height", 0u);
        slot.seed = slotJson.value("seed", 0u);
        asset.textureSlots.push_back(std::move(slot));
    }
    return asset;
}

std::string RunNeuralMaterialAuthoringSelfTestJson() {
    NeuralMaterialAuthoringRequest request;
    request.materialId = "neural_wet_basalt_v1";
    request.prompt = "wet black basalt with copper mineral flecks";
    request.generator = "dreamer_cpu_fixture";
    request.version = 1;
    request.textureSlots = {
        {"albedo", "generated/neural_wet_basalt/albedo.rgba8", 512, 512, 101},
        {"normal", "generated/neural_wet_basalt/normal.rgba8", 512, 512, 102},
        {"roughness", "generated/neural_wet_basalt/roughness.rgba8", 512, 512, 103},
        {"metalness", "generated/neural_wet_basalt/metalness.rgba8", 512, 512, 104}
    };

    const auto authored = AuthorNeuralPBRMaterial(request);
    const auto serialized = SerializeNeuralPBRMaterial(authored.asset);
    auto reloaded = DeserializeNeuralPBRMaterial(serialized);
    auto edited = reloaded;
    edited.version += 1;
    auto roughness = std::find_if(edited.textureSlots.begin(), edited.textureSlots.end(), [](const auto& slot) {
        return slot.usage == "roughness";
    });
    if (roughness != edited.textureSlots.end()) {
        roughness->assetUri = "generated/neural_wet_basalt/roughness_edit.rgba8";
        roughness->seed = 203;
    }
    const auto reloadedValidation = ValidateNeuralPBRMaterial(reloaded);
    const auto editedValidation = ValidateNeuralPBRMaterial(edited);

    auto missingNormal = request;
    missingNormal.materialId = "neural_incomplete_material";
    missingNormal.textureSlots.erase(
        std::remove_if(missingNormal.textureSlots.begin(), missingNormal.textureSlots.end(), [](const auto& slot) {
            return slot.usage == "normal";
        }),
        missingNormal.textureSlots.end());
    const auto rejected = AuthorNeuralPBRMaterial(missingNormal);

    const bool pass =
        authored.accepted &&
        reloadedValidation.accepted &&
        editedValidation.accepted &&
        edited.version == 2 &&
        !rejected.accepted;

    nlohmann::json report;
    report["schema"] = "cortex.neural_material_authoring.self_test.v1";
    report["pass"] = pass;
    report["authored"] = {
        {"accepted", authored.accepted},
        {"material_id", authored.asset.materialId},
        {"version", authored.asset.version},
        {"editable", authored.asset.editable},
        {"generated", authored.asset.generated},
        {"texture_slot_count", authored.asset.textureSlots.size()}
    };
    report["reloaded"] = {
        {"accepted", reloadedValidation.accepted},
        {"version", reloaded.version},
        {"texture_slot_count", reloaded.textureSlots.size()}
    };
    report["edited"] = {
        {"accepted", editedValidation.accepted},
        {"version", edited.version}
    };
    report["missing_normal"] = {
        {"accepted", rejected.accepted},
        {"errors", rejected.errors}
    };
    return report.dump(2);
}

} // namespace Cortex::Scene
