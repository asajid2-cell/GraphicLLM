#pragma once

#include <string_view>

namespace Cortex::Graphics::Generated {

struct SceneLocalProxyContractRecord {
    const char* textureSetId;
    const char* derivationMethod;
    const char* roomShell;
    float roomOcclusion;
    const char* lightRig;
    float lightAccentStrength;
};

inline constexpr const char* kSceneLocalProxyDerivationMethod = "profile_payload_material_room_light_v1";

inline constexpr SceneLocalProxyContractRecord kSceneLocalProxyContracts[] = {
    {"basketball_gym_day", kSceneLocalProxyDerivationMethod, "tall_gym_volume", 0.340000f, "high_bay_day_fill", 0.120000f},
    {"home_kitchen_lantern", kSceneLocalProxyDerivationMethod, "warm_enclosed_room", 0.460000f, "warm_practical_plus_fill", 0.240000f},
    {"home_office_evening", kSceneLocalProxyDerivationMethod, "evening_enclosed_room", 0.520000f, "soft_warm_desk_fill", 0.180000f},
    {"neon_streamer_concert", kSceneLocalProxyDerivationMethod, "dark_stage_volume", 0.720000f, "cyan_magenta_stage", 0.420000f},
    {"red_light_room", kSceneLocalProxyDerivationMethod, "dark_red_room", 0.660000f, "red_practical_accent", 0.360000f},
    {"rt_showcase_gallery", kSceneLocalProxyDerivationMethod, "gallery_partial", 0.220000f, "neutral_gallery_key", 0.100000f},
    {"school_classroom_day", kSceneLocalProxyDerivationMethod, "bright_enclosed_room", 0.280000f, "cool_daylight_windows", 0.080000f},
    {"stadium_night_match", kSceneLocalProxyDerivationMethod, "open_exterior_bowl", 0.180000f, "cool_floodlights", 0.200000f},
};

inline const SceneLocalProxyContractRecord* FindSceneLocalProxyContract(std::string_view textureSetId) {
    for (const SceneLocalProxyContractRecord& contract : kSceneLocalProxyContracts) {
        if (textureSetId == contract.textureSetId) {
            return &contract;
        }
    }
    return nullptr;
}

} // namespace Cortex::Graphics::Generated
