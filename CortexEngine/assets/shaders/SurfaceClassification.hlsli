// SurfaceClassification.hlsli
// Shared material class helpers for deferred and post-process passes.

#ifndef CORTEX_SURFACE_CLASSIFICATION_HLSLI
#define CORTEX_SURFACE_CLASSIFICATION_HLSLI

static const uint SURFACE_CLASS_DEFAULT = 0u;
static const uint SURFACE_CLASS_GLASS = 1u;
static const uint SURFACE_CLASS_MIRROR = 2u;
static const uint SURFACE_CLASS_PLASTIC = 3u;
static const uint SURFACE_CLASS_MASONRY = 4u;
static const uint SURFACE_CLASS_EMISSIVE = 5u;
static const uint SURFACE_CLASS_BRUSHED_METAL = 6u;
static const uint SURFACE_CLASS_WOOD = 7u;
static const uint SURFACE_CLASS_WATER = 8u;

static const uint SCENE_MATERIAL_DEFAULT = 0u;
static const uint SCENE_MATERIAL_PAINTED_WALL = 1u;
static const uint SCENE_MATERIAL_CERAMIC_TILE = 2u;
static const uint SCENE_MATERIAL_POLISHED_WOOD = 3u;
static const uint SCENE_MATERIAL_BRUSHED_METAL = 4u;
static const uint SCENE_MATERIAL_POLISHED_METAL = 5u;
static const uint SCENE_MATERIAL_GLASS_PANE = 6u;
static const uint SCENE_MATERIAL_FABRIC = 7u;
static const uint SCENE_MATERIAL_PLASTIC = 8u;
static const uint SCENE_MATERIAL_WET_SURFACE = 9u;
static const uint SCENE_MATERIAL_EMISSIVE_NEON = 10u;
static const uint SCENE_MATERIAL_SCREEN_PANEL = 11u;
static const uint SCENE_MATERIAL_CONCRETE = 12u;
static const uint SCENE_MATERIAL_RUBBER = 13u;
static const uint SCENE_MATERIAL_WATER = 14u;
static const uint SCENE_MATERIAL_MIRROR = 15u;

uint DecodeSurfaceClass(float encodedClass)
{
    return (uint)round(saturate(encodedClass) * 255.0f);
}

float EncodeSurfaceClass(uint surfaceClass)
{
    return saturate((float)surfaceClass / 255.0f);
}

uint DecodeSceneMaterialClass(float encodedClass)
{
    return (uint)round(saturate(encodedClass) * 255.0f);
}

float EncodeSceneMaterialClass(uint sceneMaterialClass)
{
    return saturate((float)sceneMaterialClass / 255.0f);
}

bool SurfaceIsTransmissive(uint surfaceClass, float transmission, float opacity)
{
    // Do not infer transmissive material from scene color alpha alone.
    // Transparent overlays are composited after the opaque G-buffer and do not
    // publish matching normal/material data. Treating their alpha as a material
    // signal makes post refraction sample the opaque surface behind them, which
    // causes large color pops when overlapping glass draw order changes.
    return surfaceClass == SURFACE_CLASS_GLASS ||
           surfaceClass == SURFACE_CLASS_WATER ||
           transmission > 0.01f;
}

bool SurfaceIsWater(uint surfaceClass)
{
    return surfaceClass == SURFACE_CLASS_WATER;
}

bool SurfaceIsMirrorClass(uint surfaceClass)
{
    return surfaceClass == SURFACE_CLASS_MIRROR;
}

bool SurfaceIsPolishedConductor(uint surfaceClass, float metallic, float roughness)
{
    return surfaceClass == SURFACE_CLASS_MIRROR ||
           surfaceClass == SURFACE_CLASS_BRUSHED_METAL ||
           (metallic > 0.85f && roughness < 0.18f);
}

float SurfaceReflectionCeiling(uint surfaceClass)
{
    if (surfaceClass == SURFACE_CLASS_WATER) {
        return 0.68f;
    }
    if (surfaceClass == SURFACE_CLASS_MIRROR) {
        return 0.48f;
    }
    if (surfaceClass == SURFACE_CLASS_GLASS) {
        return 0.30f;
    }
    if (surfaceClass == SURFACE_CLASS_BRUSHED_METAL) {
        return 0.24f;
    }
    return 0.14f;
}

float SurfaceReflectionCeiling(uint surfaceClass,
                               float roughness,
                               float metallic,
                               float transmission,
                               float fresnel)
{
    float smooth = saturate(1.0f - roughness);
    float conductor = saturate(metallic);
    float dielectricEdge = saturate(fresnel);

    if (surfaceClass == SURFACE_CLASS_MIRROR) {
        return lerp(0.68f, 0.88f, smooth);
    }
    if (surfaceClass == SURFACE_CLASS_WATER) {
        return lerp(0.52f, 0.74f, smooth);
    }
    if (surfaceClass == SURFACE_CLASS_GLASS) {
        float glassReflectance = max(saturate(transmission), dielectricEdge);
        return lerp(0.18f, 0.50f, glassReflectance) * lerp(0.70f, 1.0f, smooth);
    }
    if (surfaceClass == SURFACE_CLASS_BRUSHED_METAL) {
        return lerp(0.24f, 0.56f, smooth) * lerp(0.70f, 1.0f, conductor);
    }
    if (surfaceClass == SURFACE_CLASS_PLASTIC) {
        return lerp(0.10f, 0.22f, smooth);
    }
    if (surfaceClass == SURFACE_CLASS_MASONRY || surfaceClass == SURFACE_CLASS_WOOD) {
        return lerp(0.06f, 0.12f, smooth);
    }
    if (conductor > 0.85f) {
        return lerp(0.22f, 0.48f, smooth);
    }
    return lerp(0.08f, 0.16f, smooth);
}

float SurfaceRoughnessFloor(uint surfaceClass, float metallic)
{
    if (surfaceClass == SURFACE_CLASS_MIRROR || surfaceClass == SURFACE_CLASS_GLASS) {
        return 0.02f;
    }
    if (surfaceClass == SURFACE_CLASS_WATER) {
        return 0.03f;
    }
    if (surfaceClass == SURFACE_CLASS_PLASTIC) {
        return 0.25f;
    }
    if (surfaceClass == SURFACE_CLASS_MASONRY) {
        return 0.45f;
    }
    if (metallic > 0.8f) {
        return 0.02f;
    }
    return 0.20f;
}

float SurfaceNormalScaleCeiling(uint surfaceClass, float roughness, float metallic)
{
    if (surfaceClass == SURFACE_CLASS_MIRROR) {
        return 0.10f;
    }
    if (surfaceClass == SURFACE_CLASS_GLASS) {
        return 0.18f;
    }
    if (surfaceClass == SURFACE_CLASS_WATER) {
        return 0.45f;
    }
    if (surfaceClass == SURFACE_CLASS_BRUSHED_METAL) {
        return 0.36f;
    }
    if (surfaceClass == SURFACE_CLASS_MASONRY || surfaceClass == SURFACE_CLASS_WOOD) {
        return 0.68f;
    }
    if (surfaceClass == SURFACE_CLASS_PLASTIC) {
        return 0.45f;
    }
    if (metallic > 0.85f || roughness < 0.18f) {
        return 0.28f;
    }
    return 0.42f;
}

float SurfaceProceduralDetailCeiling(uint surfaceClass, float roughness, float metallic)
{
    if (surfaceClass == SURFACE_CLASS_MIRROR ||
        surfaceClass == SURFACE_CLASS_GLASS ||
        surfaceClass == SURFACE_CLASS_EMISSIVE) {
        return 0.18f;
    }
    if (surfaceClass == SURFACE_CLASS_WATER) {
        return 0.24f;
    }
    if (surfaceClass == SURFACE_CLASS_BRUSHED_METAL) {
        return 0.36f;
    }
    if (surfaceClass == SURFACE_CLASS_MASONRY || surfaceClass == SURFACE_CLASS_WOOD) {
        return 0.78f;
    }
    if (surfaceClass == SURFACE_CLASS_PLASTIC) {
        return 0.48f;
    }
    if (metallic > 0.85f || roughness < 0.18f) {
        return 0.28f;
    }
    return 0.38f;
}

float SurfaceNormalVarianceRoughnessBoost(uint surfaceClass, float roughness, float metallic)
{
    if (surfaceClass == SURFACE_CLASS_MIRROR ||
        surfaceClass == SURFACE_CLASS_GLASS ||
        surfaceClass == SURFACE_CLASS_WATER) {
        return 0.30f;
    }
    if (surfaceClass == SURFACE_CLASS_BRUSHED_METAL) {
        return 0.65f;
    }
    if (surfaceClass == SURFACE_CLASS_MASONRY || surfaceClass == SURFACE_CLASS_WOOD) {
        return 1.00f;
    }
    if (surfaceClass == SURFACE_CLASS_PLASTIC) {
        return 0.75f;
    }
    if (metallic > 0.85f || roughness < 0.18f) {
        return 0.55f;
    }
    return 0.85f;
}

float SurfaceReflectionStabilityScale(uint surfaceClass, float roughness, float metallic)
{
    if (surfaceClass == SURFACE_CLASS_MIRROR || surfaceClass == SURFACE_CLASS_WATER) {
        return 1.00f;
    }
    if (surfaceClass == SURFACE_CLASS_GLASS) {
        return 0.78f;
    }
    if (surfaceClass == SURFACE_CLASS_BRUSHED_METAL) {
        return 0.66f;
    }
    if (surfaceClass == SURFACE_CLASS_PLASTIC) {
        return 0.50f;
    }
    if (surfaceClass == SURFACE_CLASS_MASONRY || surfaceClass == SURFACE_CLASS_WOOD) {
        return 0.36f;
    }
    if (metallic > 0.85f) {
        return 0.58f;
    }
    return lerp(0.34f, 0.52f, saturate(1.0f - roughness));
}

float SurfaceIblMipRoughness(float roughness, uint surfaceClass, float metallic)
{
    if (surfaceClass == SURFACE_CLASS_MIRROR) {
        return roughness;
    }
    if (surfaceClass == SURFACE_CLASS_GLASS || surfaceClass == SURFACE_CLASS_WATER) {
        return max(roughness, 0.06f);
    }
    if (surfaceClass == SURFACE_CLASS_BRUSHED_METAL) {
        return max(roughness, 0.28f);
    }
    if (metallic > 0.85f) {
        return max(roughness, 0.24f);
    }
    return roughness;
}

float3 SurfacePolicyDebugColor(uint surfaceClass, float roughness, float metallic)
{
    return float3(
        SurfaceNormalScaleCeiling(surfaceClass, roughness, metallic),
        SurfaceReflectionStabilityScale(surfaceClass, roughness, metallic),
        SurfaceRoughnessFloor(surfaceClass, metallic));
}

float3 SceneMaterialClassDebugColor(uint sceneMaterialClass)
{
    switch (sceneMaterialClass) {
        case SCENE_MATERIAL_PAINTED_WALL:   return float3(0.66f, 0.72f, 0.78f);
        case SCENE_MATERIAL_CERAMIC_TILE:   return float3(0.10f, 0.80f, 0.95f);
        case SCENE_MATERIAL_POLISHED_WOOD:  return float3(0.72f, 0.45f, 0.20f);
        case SCENE_MATERIAL_BRUSHED_METAL:  return float3(0.58f, 0.64f, 0.70f);
        case SCENE_MATERIAL_POLISHED_METAL: return float3(0.88f, 0.90f, 0.95f);
        case SCENE_MATERIAL_GLASS_PANE:     return float3(0.45f, 0.85f, 1.00f);
        case SCENE_MATERIAL_FABRIC:         return float3(0.72f, 0.28f, 0.68f);
        case SCENE_MATERIAL_PLASTIC:        return float3(0.95f, 0.48f, 0.95f);
        case SCENE_MATERIAL_WET_SURFACE:    return float3(0.08f, 0.32f, 0.95f);
        case SCENE_MATERIAL_EMISSIVE_NEON:  return float3(1.00f, 0.32f, 0.12f);
        case SCENE_MATERIAL_SCREEN_PANEL:   return float3(0.20f, 0.95f, 0.45f);
        case SCENE_MATERIAL_CONCRETE:       return float3(0.48f, 0.48f, 0.42f);
        case SCENE_MATERIAL_RUBBER:         return float3(0.08f, 0.08f, 0.08f);
        case SCENE_MATERIAL_WATER:          return float3(0.05f, 0.42f, 0.95f);
        case SCENE_MATERIAL_MIRROR:         return float3(0.95f, 0.95f, 1.00f);
        default:                            return float3(0.32f, 0.32f, 0.32f);
    }
}

float SceneMaterialReflectionStabilityScale(uint sceneMaterialClass,
                                            uint surfaceClass,
                                            float roughness,
                                            float metallic)
{
    if (sceneMaterialClass == SCENE_MATERIAL_MIRROR ||
        sceneMaterialClass == SCENE_MATERIAL_WATER) {
        return 1.00f;
    }
    if (sceneMaterialClass == SCENE_MATERIAL_WET_SURFACE) {
        return 0.88f;
    }
    if (sceneMaterialClass == SCENE_MATERIAL_POLISHED_METAL ||
        sceneMaterialClass == SCENE_MATERIAL_GLASS_PANE) {
        return 0.78f;
    }
    if (sceneMaterialClass == SCENE_MATERIAL_BRUSHED_METAL ||
        sceneMaterialClass == SCENE_MATERIAL_CERAMIC_TILE) {
        return 0.62f;
    }
    if (sceneMaterialClass == SCENE_MATERIAL_EMISSIVE_NEON ||
        sceneMaterialClass == SCENE_MATERIAL_SCREEN_PANEL) {
        return 0.42f;
    }
    if (sceneMaterialClass == SCENE_MATERIAL_PAINTED_WALL ||
        sceneMaterialClass == SCENE_MATERIAL_FABRIC ||
        sceneMaterialClass == SCENE_MATERIAL_RUBBER) {
        return 0.28f;
    }
    return SurfaceReflectionStabilityScale(surfaceClass, roughness, metallic);
}

float SceneMaterialSubsurfaceWrap(uint sceneMaterialClass)
{
    if (sceneMaterialClass == SCENE_MATERIAL_FABRIC) {
        return 0.30f;
    }
    return 0.0f;
}

float SceneMaterialCinematicDetailFloor(uint sceneMaterialClass,
                                        uint surfaceClass)
{
    if (sceneMaterialClass == SCENE_MATERIAL_PAINTED_WALL) {
        return 0.16f;
    }
    if (sceneMaterialClass == SCENE_MATERIAL_CERAMIC_TILE ||
        sceneMaterialClass == SCENE_MATERIAL_CONCRETE) {
        return 0.18f;
    }
    if (sceneMaterialClass == SCENE_MATERIAL_POLISHED_WOOD ||
        sceneMaterialClass == SCENE_MATERIAL_FABRIC) {
        return 0.20f;
    }
    if (sceneMaterialClass == SCENE_MATERIAL_BRUSHED_METAL ||
        sceneMaterialClass == SCENE_MATERIAL_RUBBER) {
        return 0.12f;
    }
    if (sceneMaterialClass == SCENE_MATERIAL_WET_SURFACE ||
        sceneMaterialClass == SCENE_MATERIAL_WATER) {
        return 0.10f;
    }
    if (surfaceClass == SURFACE_CLASS_MASONRY ||
        surfaceClass == SURFACE_CLASS_WOOD) {
        return 0.14f;
    }
    return 0.0f;
}

float SceneMaterialCinematicColorLayerStrength(uint sceneMaterialClass,
                                               uint surfaceClass)
{
    switch (sceneMaterialClass) {
        case SCENE_MATERIAL_PAINTED_WALL:   return 0.030f;
        case SCENE_MATERIAL_CERAMIC_TILE:   return 0.040f;
        case SCENE_MATERIAL_POLISHED_WOOD:  return 0.070f;
        case SCENE_MATERIAL_BRUSHED_METAL:  return 0.026f;
        case SCENE_MATERIAL_POLISHED_METAL: return 0.018f;
        case SCENE_MATERIAL_GLASS_PANE:     return 0.018f;
        case SCENE_MATERIAL_FABRIC:         return 0.060f;
        case SCENE_MATERIAL_PLASTIC:        return 0.036f;
        case SCENE_MATERIAL_WET_SURFACE:    return 0.032f;
        case SCENE_MATERIAL_CONCRETE:       return 0.048f;
        case SCENE_MATERIAL_RUBBER:         return 0.026f;
        case SCENE_MATERIAL_WATER:          return 0.024f;
        case SCENE_MATERIAL_MIRROR:         return 0.0f;
        case SCENE_MATERIAL_EMISSIVE_NEON:
        case SCENE_MATERIAL_SCREEN_PANEL:   return 0.0f;
        default:
            if (surfaceClass == SURFACE_CLASS_MASONRY ||
                surfaceClass == SURFACE_CLASS_WOOD) {
                return 0.040f;
            }
            return 0.0f;
    }
}

float3 SceneMaterialCinematicColorLayerAxis(uint sceneMaterialClass,
                                            uint surfaceClass)
{
    switch (sceneMaterialClass) {
        case SCENE_MATERIAL_PAINTED_WALL:   return float3(0.55f, 0.24f, -0.18f);
        case SCENE_MATERIAL_CERAMIC_TILE:   return float3(-0.15f, 0.18f, 0.38f);
        case SCENE_MATERIAL_POLISHED_WOOD:  return float3(0.74f, 0.22f, -0.34f);
        case SCENE_MATERIAL_BRUSHED_METAL:  return float3(-0.16f, 0.05f, 0.28f);
        case SCENE_MATERIAL_POLISHED_METAL: return float3(-0.10f, 0.02f, 0.18f);
        case SCENE_MATERIAL_GLASS_PANE:     return float3(-0.22f, 0.10f, 0.44f);
        case SCENE_MATERIAL_FABRIC:         return float3(0.34f, -0.06f, -0.20f);
        case SCENE_MATERIAL_PLASTIC:        return float3(0.12f, 0.04f, -0.18f);
        case SCENE_MATERIAL_WET_SURFACE:    return float3(-0.18f, 0.10f, 0.24f);
        case SCENE_MATERIAL_CONCRETE:       return float3(0.18f, 0.10f, -0.24f);
        case SCENE_MATERIAL_RUBBER:         return float3(0.08f, -0.02f, -0.10f);
        case SCENE_MATERIAL_WATER:          return float3(-0.24f, 0.10f, 0.42f);
        default:
            if (surfaceClass == SURFACE_CLASS_WOOD) {
                return float3(0.60f, 0.18f, -0.28f);
            }
            if (surfaceClass == SURFACE_CLASS_MASONRY) {
                return float3(0.24f, 0.10f, -0.22f);
            }
            return float3(0.0f, 0.0f, 0.0f);
    }
}

float3 ApplySceneMaterialCinematicColorLayer(float3 albedo,
                                             uint sceneMaterialClass,
                                             uint surfaceClass,
                                             float mask,
                                             float proceduralStrength)
{
    const float layerStrength =
        SceneMaterialCinematicColorLayerStrength(sceneMaterialClass, surfaceClass) *
        saturate(proceduralStrength);
    if (layerStrength <= 0.0001f) {
        return albedo;
    }

    const float3 axis = SceneMaterialCinematicColorLayerAxis(sceneMaterialClass, surfaceClass);
    const float wave = (saturate(mask) - 0.5f) * 2.0f;
    float3 tinted = saturate(albedo * (1.0f + axis * wave * layerStrength));

    // Preserve local luminance so the color layer adds material richness
    // without undoing exposure/white-ratio stability policies.
    const float3 lumaWeights = float3(0.2126f, 0.7152f, 0.0722f);
    const float sourceLuma = dot(albedo, lumaWeights);
    const float tintedLuma = max(dot(tinted, lumaWeights), 1.0e-4f);
    tinted *= sourceLuma / tintedLuma;
    return saturate(tinted);
}

float SceneMaterialCinematicClearcoatBoost(uint sceneMaterialClass)
{
    if (sceneMaterialClass == SCENE_MATERIAL_CERAMIC_TILE) {
        return 0.20f;
    }
    if (sceneMaterialClass == SCENE_MATERIAL_POLISHED_WOOD) {
        return 0.16f;
    }
    if (sceneMaterialClass == SCENE_MATERIAL_POLISHED_METAL ||
        sceneMaterialClass == SCENE_MATERIAL_GLASS_PANE ||
        sceneMaterialClass == SCENE_MATERIAL_MIRROR) {
        return 0.10f;
    }
    if (sceneMaterialClass == SCENE_MATERIAL_WET_SURFACE) {
        return 0.34f;
    }
    return 0.0f;
}

float SceneMaterialCinematicWetnessBoost(uint sceneMaterialClass)
{
    if (sceneMaterialClass == SCENE_MATERIAL_WET_SURFACE ||
        sceneMaterialClass == SCENE_MATERIAL_WATER) {
        return 0.44f;
    }
    if (sceneMaterialClass == SCENE_MATERIAL_CERAMIC_TILE) {
        return 0.08f;
    }
    if (sceneMaterialClass == SCENE_MATERIAL_POLISHED_WOOD) {
        return 0.04f;
    }
    return 0.0f;
}

float SceneMaterialCinematicEmissiveBoost(uint sceneMaterialClass)
{
    if (sceneMaterialClass == SCENE_MATERIAL_EMISSIVE_NEON) {
        return 1.75f;
    }
    if (sceneMaterialClass == SCENE_MATERIAL_SCREEN_PANEL) {
        return 0.65f;
    }
    return 0.0f;
}

float SceneMaterialAlbedoLuminanceCeiling(uint sceneMaterialClass)
{
    switch (sceneMaterialClass) {
        case SCENE_MATERIAL_PAINTED_WALL:   return 0.56f;
        case SCENE_MATERIAL_CERAMIC_TILE:   return 0.68f;
        case SCENE_MATERIAL_POLISHED_WOOD:  return 0.58f;
        case SCENE_MATERIAL_BRUSHED_METAL:  return 0.60f;
        case SCENE_MATERIAL_POLISHED_METAL: return 0.70f;
        case SCENE_MATERIAL_GLASS_PANE:     return 0.72f;
        case SCENE_MATERIAL_FABRIC:         return 0.54f;
        case SCENE_MATERIAL_PLASTIC:        return 0.58f;
        case SCENE_MATERIAL_WET_SURFACE:    return 0.58f;
        case SCENE_MATERIAL_CONCRETE:       return 0.58f;
        case SCENE_MATERIAL_RUBBER:         return 0.42f;
        case SCENE_MATERIAL_WATER:          return 0.70f;
        case SCENE_MATERIAL_MIRROR:         return 0.78f;
        default:                            return 0.78f;
    }
}

float SceneMaterialAlbedoChromaCeiling(uint sceneMaterialClass)
{
    switch (sceneMaterialClass) {
        case SCENE_MATERIAL_PAINTED_WALL:   return 0.62f;
        case SCENE_MATERIAL_CERAMIC_TILE:   return 0.66f;
        case SCENE_MATERIAL_POLISHED_WOOD:  return 0.65f;
        case SCENE_MATERIAL_BRUSHED_METAL:  return 0.52f;
        case SCENE_MATERIAL_POLISHED_METAL: return 0.44f;
        case SCENE_MATERIAL_GLASS_PANE:     return 0.55f;
        case SCENE_MATERIAL_FABRIC:         return 0.68f;
        case SCENE_MATERIAL_PLASTIC:        return 0.72f;
        case SCENE_MATERIAL_WET_SURFACE:    return 0.55f;
        case SCENE_MATERIAL_CONCRETE:       return 0.48f;
        case SCENE_MATERIAL_RUBBER:         return 0.50f;
        case SCENE_MATERIAL_WATER:          return 0.60f;
        case SCENE_MATERIAL_MIRROR:         return 0.40f;
        default:                            return 0.75f;
    }
}

float3 ApplySceneMaterialAlbedoPolicy(float3 albedo, uint sceneMaterialClass)
{
    float3 result = saturate(albedo);
    if (sceneMaterialClass == SCENE_MATERIAL_EMISSIVE_NEON ||
        sceneMaterialClass == SCENE_MATERIAL_SCREEN_PANEL) {
        return result;
    }

    const float lumCeiling = SceneMaterialAlbedoLuminanceCeiling(sceneMaterialClass);
    const float chromaCeiling = SceneMaterialAlbedoChromaCeiling(sceneMaterialClass);
    float luma = dot(result, float3(0.2126f, 0.7152f, 0.0722f));
    if (luma > lumCeiling && luma > 1.0e-4f) {
        result *= lumCeiling / luma;
        luma = dot(result, float3(0.2126f, 0.7152f, 0.0722f));
    }

    const float maxChannel = max(result.r, max(result.g, result.b));
    const float minChannel = min(result.r, min(result.g, result.b));
    const float chroma = maxChannel - minChannel;
    if (chroma > chromaCeiling) {
        const float t = saturate(1.0f - (chromaCeiling / max(chroma, 1.0e-4f)));
        result = lerp(result, float3(luma, luma, luma), t);
    }
    return saturate(result);
}

float3 SceneMaterialPolicyDebugColor(uint sceneMaterialClass,
                                     uint surfaceClass,
                                     float roughness,
                                     float metallic)
{
    float3 namedColor = SceneMaterialClassDebugColor(sceneMaterialClass);
    float stability = SceneMaterialReflectionStabilityScale(sceneMaterialClass,
                                                            surfaceClass,
                                                            roughness,
                                                            metallic);
    return lerp(SurfacePolicyDebugColor(surfaceClass, roughness, metallic),
                namedColor,
                saturate(0.35f + stability * 0.45f));
}

float3 SurfaceClassDebugColor(uint surfaceClass)
{
    switch (surfaceClass) {
        case SURFACE_CLASS_GLASS:         return float3(0.45f, 0.80f, 1.00f);
        case SURFACE_CLASS_MIRROR:        return float3(0.95f, 0.95f, 1.00f);
        case SURFACE_CLASS_PLASTIC:       return float3(0.90f, 0.45f, 0.95f);
        case SURFACE_CLASS_MASONRY:       return float3(0.75f, 0.32f, 0.18f);
        case SURFACE_CLASS_EMISSIVE:      return float3(1.00f, 0.88f, 0.18f);
        case SURFACE_CLASS_BRUSHED_METAL: return float3(0.70f, 0.72f, 0.76f);
        case SURFACE_CLASS_WOOD:          return float3(0.64f, 0.42f, 0.20f);
        case SURFACE_CLASS_WATER:         return float3(0.05f, 0.42f, 0.95f);
        default:                          return float3(0.35f, 0.35f, 0.35f);
    }
}

#endif
