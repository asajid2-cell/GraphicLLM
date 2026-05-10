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

uint DecodeSurfaceClass(float encodedClass)
{
    return (uint)round(saturate(encodedClass) * 255.0f);
}

float EncodeSurfaceClass(uint surfaceClass)
{
    return saturate((float)surfaceClass / 255.0f);
}

bool SurfaceIsTransmissive(uint surfaceClass, float transmission, float opacity)
{
    return surfaceClass == SURFACE_CLASS_GLASS ||
           surfaceClass == SURFACE_CLASS_WATER ||
           transmission > 0.01f ||
           opacity < 0.99f;
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
