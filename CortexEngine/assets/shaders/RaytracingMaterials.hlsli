// RaytracingMaterials.hlsli
// Shared compact material record for DXR shadow, GI, and reflection shaders.

#ifndef CORTEX_RAYTRACING_MATERIALS_HLSLI
#define CORTEX_RAYTRACING_MATERIALS_HLSLI

#include "SurfaceClassification.hlsli"

struct RTMaterial
{
    float4 albedoMetallic;     // rgb = base reflectance, a = metallic
    float4 emissiveRoughness;  // rgb = emissive radiance, a = roughness
    float4 params;             // x = ao, y = double-sided, z = alpha-mask, w = alpha cutoff
    float4 classification;     // x = surface class, y = transmission, z = specular, w = clearcoat
};

StructuredBuffer<RTMaterial> g_RTMaterials : register(t3, space2);

uint RTMaterialSurfaceClass(RTMaterial material)
{
    return (uint)round(max(material.classification.x, 0.0f));
}

float RTMaterialTransmission(RTMaterial material)
{
    return saturate(material.classification.y);
}

float RTMaterialSpecularFactor(RTMaterial material)
{
    return clamp(material.classification.z, 0.0f, 2.0f);
}

float RTMaterialClearcoat(RTMaterial material)
{
    return saturate(material.classification.w);
}

float RTMaterialEmissiveLuma(RTMaterial material)
{
    return dot(max(material.emissiveRoughness.rgb, 0.0f),
               float3(0.2126f, 0.7152f, 0.0722f));
}

float RTMaterialDiffuseGIVisibility(RTMaterial material)
{
    uint surfaceClass = RTMaterialSurfaceClass(material);
    float metallic = saturate(material.albedoMetallic.a);
    float roughness = saturate(material.emissiveRoughness.a);
    float transmission = RTMaterialTransmission(material);
    float clearcoat = RTMaterialClearcoat(material);
    float emissiveLuma = RTMaterialEmissiveLuma(material);

    if (SurfaceIsWater(surfaceClass))
    {
        return 0.12f;
    }
    if (SurfaceIsTransmissive(surfaceClass, transmission, 1.0f))
    {
        return lerp(0.35f, 0.08f, transmission);
    }
    if (surfaceClass == SURFACE_CLASS_EMISSIVE || emissiveLuma > 0.05f)
    {
        return 0.18f;
    }
    if (SurfaceIsMirrorClass(surfaceClass))
    {
        return 0.55f;
    }
    if (surfaceClass == SURFACE_CLASS_BRUSHED_METAL || metallic > 0.8f)
    {
        return lerp(0.80f, 0.60f, clearcoat);
    }
    if (surfaceClass == SURFACE_CLASS_PLASTIC)
    {
        return 0.82f;
    }
    if (surfaceClass == SURFACE_CLASS_MASONRY ||
        surfaceClass == SURFACE_CLASS_WOOD)
    {
        return 1.0f;
    }

    return lerp(0.95f, 0.78f, saturate(1.0f - roughness));
}

float RTMaterialSunVisibility(RTMaterial material)
{
    uint surfaceClass = RTMaterialSurfaceClass(material);
    float transmission = RTMaterialTransmission(material);
    float alphaMask = saturate(material.params.z);
    float emissiveLuma = RTMaterialEmissiveLuma(material);

    if (SurfaceIsWater(surfaceClass))
    {
        return 0.70f;
    }
    if (SurfaceIsTransmissive(surfaceClass, transmission, 1.0f))
    {
        return lerp(0.45f, 0.78f, transmission);
    }
    if (surfaceClass == SURFACE_CLASS_EMISSIVE || emissiveLuma > 0.05f)
    {
        return 0.30f;
    }

    // The compact RT material does not yet contain per-texel alpha. Keep
    // alpha-tested geometry mostly solid without making every cutout a black
    // card until any-hit alpha testing is added.
    if (alphaMask > 0.5f)
    {
        return 0.18f;
    }

    return 0.0f;
}

#endif
