#pragma once

#include "Graphics/MaterialModel.h"

#include <array>
#include <cstdint>
#include <cstring>

#include <glm/glm.hpp>

namespace Cortex::Graphics {

struct VisibilityBufferMaterialKey {
    std::array<uint32_t, 48> words{};

    bool operator==(const VisibilityBufferMaterialKey& other) const noexcept {
        return words == other.words;
    }
};

struct VisibilityBufferMaterialKeyHasher {
    size_t operator()(const VisibilityBufferMaterialKey& key) const noexcept {
        uint64_t h = 1469598103934665603ull;
        for (uint32_t word : key.words) {
            h ^= static_cast<uint64_t>(word);
            h *= 1099511628211ull;
        }
        return static_cast<size_t>(h);
    }
};

inline uint32_t FloatBitsForMaterialKey(float value) {
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "FloatBitsForMaterialKey expects 32-bit float");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline VisibilityBufferMaterialKey MakeVisibilityBufferMaterialKey(
    const MaterialModel& material,
    const glm::uvec4& textureIndices0,
    const glm::uvec4& textureIndices2,
    const glm::uvec4& textureIndices3,
    const glm::uvec4& textureIndices4,
    uint32_t materialClass) {
    VisibilityBufferMaterialKey key{};
    key.words[0] = FloatBitsForMaterialKey(material.albedo.x);
    key.words[1] = FloatBitsForMaterialKey(material.albedo.y);
    key.words[2] = FloatBitsForMaterialKey(material.albedo.z);
    key.words[3] = FloatBitsForMaterialKey(material.albedo.w);
    key.words[4] = FloatBitsForMaterialKey(material.metallic);
    key.words[5] = FloatBitsForMaterialKey(material.roughness);
    key.words[6] = FloatBitsForMaterialKey(material.ao);
    key.words[7] = textureIndices0.x;
    key.words[8] = textureIndices0.y;
    key.words[9] = textureIndices0.z;
    key.words[10] = textureIndices0.w;
    key.words[11] = textureIndices2.x;
    key.words[12] = textureIndices2.y;
    key.words[13] = FloatBitsForMaterialKey(material.alphaCutoff);
    key.words[14] = static_cast<uint32_t>(material.alphaMode);
    key.words[15] = material.doubleSided ? 1u : 0u;
    key.words[16] = FloatBitsForMaterialKey(material.emissiveColor.x);
    key.words[17] = FloatBitsForMaterialKey(material.emissiveColor.y);
    key.words[18] = FloatBitsForMaterialKey(material.emissiveColor.z);
    key.words[19] = FloatBitsForMaterialKey(material.emissiveStrength);
    key.words[20] = FloatBitsForMaterialKey(material.occlusionStrength);
    key.words[21] = FloatBitsForMaterialKey(material.normalScale);
    key.words[22] = FloatBitsForMaterialKey(material.clearcoatFactor);
    key.words[23] = FloatBitsForMaterialKey(material.clearcoatRoughnessFactor);
    key.words[24] = FloatBitsForMaterialKey(material.sheenWeight);
    key.words[25] = FloatBitsForMaterialKey(material.subsurfaceWrap);
    key.words[26] = FloatBitsForMaterialKey(material.transmissionFactor);
    key.words[27] = FloatBitsForMaterialKey(material.ior);
    key.words[28] = 0u;
    key.words[29] = 0u;
    key.words[30] = FloatBitsForMaterialKey(material.specularColorFactor.x);
    key.words[31] = FloatBitsForMaterialKey(material.specularColorFactor.y);
    key.words[32] = FloatBitsForMaterialKey(material.specularColorFactor.z);
    key.words[33] = FloatBitsForMaterialKey(material.specularFactor);
    key.words[34] = textureIndices3.x;
    key.words[35] = textureIndices3.y;
    key.words[36] = textureIndices3.z;
    key.words[37] = textureIndices3.w;
    key.words[38] = textureIndices4.x;
    key.words[39] = textureIndices4.y;
    key.words[40] = textureIndices4.z;
    key.words[41] = textureIndices4.w;
    key.words[42] = materialClass;
    return key;
}

} // namespace Cortex::Graphics
