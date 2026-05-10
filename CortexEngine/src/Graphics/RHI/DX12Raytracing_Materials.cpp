#include "DX12Raytracing.h"

#include "../MaterialModel.h"

#include <algorithm>

#include <glm/glm.hpp>

namespace Cortex::Graphics {

DX12RaytracingContext::RTMaterialGPU DX12RaytracingContext::BuildRTMaterialGPU(
    const MaterialModel& material,
    uint32_t surfaceClassId) {
    RTMaterialGPU gpuMaterial{};
    const glm::vec3 albedo =
        glm::clamp(glm::vec3(material.albedo), glm::vec3(0.0f), glm::vec3(1.0f));
    const glm::vec3 emissive =
        glm::max(material.emissiveColor * std::max(material.emissiveStrength, 0.0f),
                 glm::vec3(0.0f));

    gpuMaterial.albedoMetallic = glm::vec4(albedo, material.metallic);
    gpuMaterial.emissiveRoughness =
        glm::vec4(emissive, std::clamp(material.roughness, 0.02f, 1.0f));
    gpuMaterial.params =
        glm::vec4(material.ao,
                  material.doubleSided ? 1.0f : 0.0f,
                  static_cast<float>(material.alphaMode == MaterialAlphaMode::Mask),
                  material.alphaCutoff);
    gpuMaterial.classification =
        glm::vec4(static_cast<float>(surfaceClassId),
                  material.transmissionFactor,
                  material.specularFactor,
                  material.clearcoatFactor);
    return gpuMaterial;
}


} // namespace Cortex::Graphics