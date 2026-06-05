// DeferredLighting.hlsl
// Phase 2.2: Deferred lighting pass for visibility buffer
// Reads G-buffers and applies PBR lighting

#include "PBR_Lighting.hlsli"
#include "SurfaceClassification.hlsli"

// Temporary debug views (compile-time).
// 0 = off, 1 = normals, 2 = NdotL, 3 = clustered occupancy
#define DEFERRED_DEBUG_VIEW 0

static const uint LIGHT_TYPE_DIRECTIONAL = 0u;
static const uint LIGHT_TYPE_POINT       = 1u;
static const uint LIGHT_TYPE_SPOT        = 2u;
static const uint LIGHT_TYPE_AREA_RECT   = 3u;

static const uint FIXTURE_CLASS_GENERIC   = 0u;
static const uint FIXTURE_CLASS_SOFT      = 1u;
static const uint FIXTURE_CLASS_EMISSIVE  = 2u;
static const uint FIXTURE_CLASS_STAGE     = 3u;
static const uint FIXTURE_CLASS_PRACTICAL = 4u;

// G-buffer inputs
Texture2D<float4> g_GBufferAlbedo : register(t0);           // RGB = albedo (linear), A = AO
Texture2D<float4> g_GBufferNormalRoughness : register(t1);  // RGB = encoded normal (0..1), A = roughness
Texture2D<float4> g_GBufferEmissiveMetallic : register(t2); // RGB = emissive, A = metallic
Texture2D<float> g_DepthBuffer : register(t3);              // Depth for position reconstruction
Texture2D<float4> g_GBufferMaterialExt0 : register(t4);      // RGBA16F: x=clearcoat, y=coatRough, z=IOR, w=specFactor
Texture2D<float4> g_GBufferMaterialExt1 : register(t5);      // RGBA16F: rgb=specColor, a=transmission (unused in deferred)
Texture2D<float4> g_GBufferMaterialExt2 : register(t6);      // RGBA8: surface class, anisotropy, sheen, named scene material class

// Environment/shadow maps (matching forward renderer)
Texture2D<float4> g_EnvDiffuse : register(t7);  // lat-long (equirect) irradiance
Texture2D<float4> g_EnvSpecular : register(t8); // lat-long (equirect) prefiltered specular
Texture2DArray<float> g_ShadowMap : register(t9);
Texture2D<float2> g_BRDFLUT : register(t10);

struct Light {
    float4 position_type;
    float4 direction_cosInner;
    float4 color_range;
    float4 params;
};

StructuredBuffer<Light> g_LocalLights : register(t11);
StructuredBuffer<uint2> g_ClusterRanges : register(t12);
StructuredBuffer<uint> g_ClusterLightIndices : register(t13);

uint DecodeFixtureClass(uint type, Light light) {
    if (type == LIGHT_TYPE_AREA_RECT) {
        return (uint)max(0.0f, round(light.direction_cosInner.w));
    }
    if (type == LIGHT_TYPE_POINT || type == LIGHT_TYPE_SPOT) {
        return (uint)max(0.0f, round(light.params.z));
    }
    return FIXTURE_CLASS_GENERIC;
}

float FixtureRadianceScale(uint fixtureClass) {
    if (fixtureClass == FIXTURE_CLASS_EMISSIVE) {
        return 1.14f;
    }
    if (fixtureClass == FIXTURE_CLASS_STAGE) {
        return 1.08f;
    }
    if (fixtureClass == FIXTURE_CLASS_SOFT) {
        return 1.05f;
    }
    return 1.0f;
}

float FixtureWrappedNdotL(float rawNdotL, uint fixtureClass) {
    if (fixtureClass == FIXTURE_CLASS_SOFT) {
        return saturate((rawNdotL + 0.18f) / 1.18f);
    }
    if (fixtureClass == FIXTURE_CLASS_EMISSIVE) {
        return saturate((rawNdotL + 0.10f) / 1.10f);
    }
    return rawNdotL;
}

float FixtureRoughnessForSpecular(float roughness, uint fixtureClass, bool isAreaRect) {
    if (fixtureClass == FIXTURE_CLASS_SOFT) {
        return saturate(roughness * 1.70f + 0.08f);
    }
    if (fixtureClass == FIXTURE_CLASS_EMISSIVE) {
        return saturate(roughness * 1.35f + 0.04f);
    }
    if (isAreaRect) {
        return saturate(roughness * 1.5f + 0.05f);
    }
    return roughness;
}

SamplerState g_LinearSampler : register(s0);
SamplerState g_ShadowSampler : register(s1);

cbuffer PerFrameData : register(b0) {
    float4x4 g_InvViewProj;                // Inverse view-projection for position reconstruction
    float4x4 g_ViewMatrix;
    float4x4 g_LightViewProjection[6];     // 0..2 cascades, 3..5 local shadowed lights

    float4 g_CameraPosition;               // xyz = camera position (world)
    float4 g_SunDirection;                 // xyz = direction-to-light (world)
    float4 g_SunRadiance;                  // rgb = color * intensity
    float4 g_AmbientColor;                 // rgb = ambient color * intensity, w = background blur

    float4 g_CascadeSplits;                // x,y,z = split depths in view space, w = far plane
    float4 g_ShadowParams;                 // x=bias, y=pcfRadius(texels), z=enabled, w=pcssEnabled
    float4 g_EnvParams;                    // x=diffuse IBL, y=specular IBL, z=IBL enabled, w=background exposure
    float4 g_ShadowInvSizeAndSpecMaxMip;   // xy = 1/shadowMapDim, z = specular max mip, w = environment rotation radians
    float4 g_ProjectionParams;             // x=proj11, y=proj22, z=nearZ, w=farZ
    uint4  g_ScreenAndCluster;             // x=width, y=height, z=clusterCountX, w=clusterCountY
    uint4  g_ClusterParams;                // x=clusterCountZ, y=maxLightsPerCluster, z=localLightCount, w unused
    uint4  g_ReflectionProbeParams;        // x=probeTableSRVIndex, y=probeCount, z/w unused
    float4 g_LocalProbeParams;             // x=diffuse scale, y=specular scale, z=enabled, w unused
    float4 g_CinematicStabilityParams;     // x=specular damping, y=debug stability, z=shadow softness, w=highlight protection
};

static const uint INVALID_BINDLESS_INDEX = 0xFFFFFFFFu;

struct ReflectionProbe {
    float4 centerBlend; // xyz center (world), w blend distance
    float4 extents;     // xyz half extents (world)
    uint4  envIndices;  // x diffuse env SRV index, y specular env SRV index
};

float2 DirectionToLatLong(float3 dir)
{
    dir = normalize(dir);
    if (!all(isfinite(dir))) {
        dir = float3(0.0f, 0.0f, 1.0f);
    }

    float phi = atan2(-dir.z, dir.x);              // [-PI, PI]
    float theta = asin(clamp(dir.y, -1.0f, 1.0f)); // [-PI/2, PI/2]

    float2 uv;
    uv.x = 0.5f + phi / (2.0f * PI);
    uv.y = 0.5f - theta / PI;
    return uv;
}

float3 RotateEnvironmentDirection(float3 dir)
{
    float s, c;
    sincos(g_ShadowInvSizeAndSpecMaxMip.w, s, c);
    return normalize(float3(c * dir.x + s * dir.z, dir.y, -s * dir.x + c * dir.z));
}

float EnvReflectionFootprintMip(float2 uv, float width, float height, float maxMip)
{
    float2 dx = ddx(uv);
    float2 dy = ddy(uv);

    // Lat-long reflections wrap horizontally; avoid treating the seam as a
    // full-width derivative spike while still filtering real subpixel motion.
    dx.x = frac(dx.x + 0.5f) - 0.5f;
    dy.x = frac(dy.x + 0.5f) - 0.5f;

    float2 texelDx = dx * float2(width, height);
    float2 texelDy = dy * float2(width, height);
    float footprint = max(length(texelDx), length(texelDy));
    float mip = log2(max(footprint, 1.0f));
    return clamp(mip, 0.0f, maxMip);
}

float EnvReflectionFootprintMipFromDirection(float3 dir, float width, float height, float maxMip)
{
    float2 uv = DirectionToLatLong(RotateEnvironmentDirection(dir));
    return EnvReflectionFootprintMip(uv, width, height, maxMip);
}

float3 SampleEnvDiffuse(float3 dir, uint diffuseIndex, float mipLevel)
{
    float2 uv = DirectionToLatLong(RotateEnvironmentDirection(dir));
#ifdef ENABLE_BINDLESS
    if (diffuseIndex != INVALID_BINDLESS_INDEX) {
        Texture2D<float4> tex = ResourceDescriptorHeap[diffuseIndex];
        return tex.SampleLevel(g_LinearSampler, uv, mipLevel).rgb;
    }
#endif
    return g_EnvDiffuse.SampleLevel(g_LinearSampler, uv, mipLevel).rgb;
}

float3 SampleEnvSpecular(float3 dir, float mipLevel, uint specularIndex)
{
    float2 uv = DirectionToLatLong(RotateEnvironmentDirection(dir));
#ifdef ENABLE_BINDLESS
    if (specularIndex != INVALID_BINDLESS_INDEX) {
        Texture2D<float4> tex = ResourceDescriptorHeap[specularIndex];
        return tex.SampleLevel(g_LinearSampler, uv, mipLevel).rgb;
    }
#endif
    return g_EnvSpecular.SampleLevel(g_LinearSampler, uv, mipLevel).rgb;
}

float3 NormalizedSunColor()
{
    float3 sunColor = g_SunRadiance.rgb;
    float sunLum = max(dot(sunColor, float3(0.2126f, 0.7152f, 0.0722f)), 0.01f);
    return sunColor / sunLum;
}

float SkyHash21(float2 p)
{
    p = frac(p * float2(127.1f, 311.7f));
    p += dot(p, p + 41.23f);
    return frac(p.x * p.y);
}

float SkyValueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0f - 2.0f * f);
    float a = SkyHash21(i);
    float b = SkyHash21(i + float2(1.0f, 0.0f));
    float c = SkyHash21(i + float2(0.0f, 1.0f));
    float d = SkyHash21(i + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float SkyFBM(float2 p)
{
    float value = 0.0f;
    float amp = 0.5f;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        value += SkyValueNoise(p) * amp;
        p = p * 2.05f + 13.17f;
        amp *= 0.5f;
    }
    return value;
}

float LocalSkyCloudMask(float3 viewDir, float horizon, float up)
{
    float2 cloudUv = float2(viewDir.x * 2.3f + viewDir.z * 0.55f,
                            up * 1.85f + viewDir.z * 0.18f);
    float streaks = SkyFBM(cloudUv * float2(1.25f, 0.55f));
    float wisps = SkyFBM(cloudUv * float2(3.40f, 1.05f) + 23.0f);
    float layer = smoothstep(0.38f, 0.66f, streaks * 0.78f + wisps * 0.30f);
    return layer * saturate(up * 1.35f + 0.28f) * saturate(1.0f - horizon * 0.18f);
}

float3 ComputeLocalOutdoorSky(float3 viewDir)
{
    viewDir = normalize(viewDir);
    float3 sunDir = normalize(g_SunDirection.xyz);
    float viewY = viewDir.y;
    float sunY = sunDir.y;
    float sunDot = saturate(dot(viewDir, sunDir));
    float horizon = pow(saturate(1.0f - abs(viewY) * 1.15f), 1.45f);
    float up = saturate(viewY);
    float down = saturate(-viewY);
    float lowSun = saturate(1.0f - sunY * 1.45f);

    float3 zenith = lerp(float3(0.18f, 0.31f, 0.47f),
                         float3(0.09f, 0.16f, 0.28f),
                         lowSun * 0.45f);
    float3 upperHaze = lerp(float3(0.47f, 0.57f, 0.63f),
                            float3(0.72f, 0.47f, 0.28f),
                            lowSun * 0.55f);
    float3 wetHorizon = lerp(float3(0.19f, 0.30f, 0.29f),
                             float3(0.42f, 0.27f, 0.16f),
                             lowSun * 0.50f);
    float3 belowHorizon = float3(0.035f, 0.075f, 0.068f);

    float3 skyColor = lerp(upperHaze, zenith, pow(up, 0.55f));
    skyColor = lerp(skyColor, wetHorizon, horizon * 0.80f);
    skyColor = lerp(skyColor, belowHorizon, pow(down, 0.65f));

    float3 sunColor = NormalizedSunColor();
    float atmosphereVisibility = saturate(sunY + 0.28f);
    float sunDisk = smoothstep(0.99945f, 0.99982f, sunDot);
    float sunCore = pow(sunDot, 420.0f) * 1.35f;
    float sunHalo = pow(sunDot, 18.0f) * 0.55f;
    float broadGlow = pow(sunDot, 4.0f) * 0.18f;
    skyColor += sunColor * (sunDisk * 4.0f + sunCore + sunHalo + broadGlow) * atmosphereVisibility;

    float waterMist = horizon * saturate(0.75f - sunY * 0.25f);
    skyColor = lerp(skyColor, float3(0.55f, 0.61f, 0.55f), waterMist * 0.18f);
    float clouds = LocalSkyCloudMask(viewDir, horizon, up);
    float3 cloudColor = lerp(float3(0.50f, 0.56f, 0.52f),
                             float3(0.84f, 0.80f, 0.70f),
                             lowSun * 0.38f);
    skyColor = lerp(skyColor, cloudColor, clouds * 0.42f);
    skyColor *= lerp(0.60f, 1.12f, saturate(sunY + 0.18f));
    return max(skyColor, 0.0f);
}

float3 ComputeSceneLocalProbeDiffuse(float3 normal,
                                     uint surfaceClass,
                                     uint sceneMaterialClass)
{
    float3 ambientBase = max(g_AmbientColor.rgb, 0.018f.xxx);
    float3 sunColor = NormalizedSunColor();
    float up = saturate(normal.y * 0.5f + 0.5f);
    float side = saturate(1.0f - abs(normal.y));

    float3 ceilingBounce = lerp(ambientBase, sunColor * max(ambientBase, 0.055f.xxx), 0.28f);
    float3 wallBounce = ambientBase * 1.18f + sunColor * 0.018f;
    float3 floorBounce = ambientBase * 0.72f + float3(0.055f, 0.048f, 0.040f);
    float3 local = lerp(floorBounce, ceilingBounce, up);
    local = lerp(local, wallBounce, side * 0.45f);

    if (sceneMaterialClass == SCENE_MATERIAL_EMISSIVE_NEON ||
        sceneMaterialClass == SCENE_MATERIAL_SCREEN_PANEL) {
        local += float3(0.020f, 0.055f, 0.095f);
    } else if (sceneMaterialClass == SCENE_MATERIAL_CERAMIC_TILE ||
               sceneMaterialClass == SCENE_MATERIAL_POLISHED_WOOD) {
        local *= 1.08f;
    } else if (surfaceClass == SURFACE_CLASS_BRUSHED_METAL ||
               surfaceClass == SURFACE_CLASS_GLASS ||
               surfaceClass == SURFACE_CLASS_MIRROR) {
        local *= 0.82f;
    }

    return max(local, 0.0f.xxx);
}

float3 ComputeSceneLocalProbeSpecular(float3 reflectionDir,
                                      uint surfaceClass,
                                      uint sceneMaterialClass,
                                      float roughness)
{
    float3 ambientBase = max(g_AmbientColor.rgb, 0.018f.xxx);
    float3 sunColor = NormalizedSunColor();
    float up = saturate(reflectionDir.y * 0.5f + 0.5f);
    float2 reflXZ = reflectionDir.xz;
    float2 sunXZ = g_SunDirection.xz;
    float front = saturate(dot(normalize(reflXZ + 1e-4f.xx), normalize(sunXZ + 1e-4f.xx)) * 0.5f + 0.5f);

    float3 roomLow = ambientBase * 0.70f + float3(0.040f, 0.036f, 0.032f);
    float3 roomHigh = ambientBase * 1.28f + sunColor * 0.035f;
    float3 local = lerp(roomLow, roomHigh, up);
    local += sunColor * pow(front, 12.0f) * lerp(0.010f, 0.055f, saturate(1.0f - roughness));

    if (sceneMaterialClass == SCENE_MATERIAL_EMISSIVE_NEON ||
        sceneMaterialClass == SCENE_MATERIAL_SCREEN_PANEL) {
        local += float3(0.030f, 0.090f, 0.150f) * saturate(1.0f - roughness * 0.55f);
    } else if (surfaceClass == SURFACE_CLASS_WATER ||
               surfaceClass == SURFACE_CLASS_GLASS ||
               surfaceClass == SURFACE_CLASS_MIRROR) {
        local *= 1.18f;
    }

    return max(local, 0.0f.xxx);
}

float Pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

float BurleyDiffuseFactor(float NdotV, float NdotL, float LdotH, float roughness)
{
    // Disney/Burley diffuse: rough dielectrics gain a subtle grazing response
    // instead of the flat Lambert look. Keep it as a factor over albedo / PI
    // so existing lighting energy remains predictable.
    float fd90 = 0.5f + 2.0f * roughness * LdotH * LdotH;
    float lightScatter = 1.0f + (fd90 - 1.0f) * Pow5(1.0f - saturate(NdotL));
    float viewScatter = 1.0f + (fd90 - 1.0f) * Pow5(1.0f - saturate(NdotV));
    return lightScatter * viewScatter;
}

float3 RoughSpecularEnergyCompensation(float3 F0, float roughness)
{
    // Mild multiple-scattering compensation for rough GGX lobes. This avoids
    // chalky, under-energized rough metals without pushing mirrors or glass.
    float f0Avg = saturate(dot(F0, float3(0.333333f, 0.333333f, 0.333333f)));
    float rough = saturate(roughness);
    float boost = (1.0f - f0Avg) * rough * rough * 0.32f;
    return (1.0f + boost).xxx;
}

float SpecularOcclusion(float NdotV, float ao, float roughness)
{
    // Common specular AO approximation: tight glossy lobes are occluded more
    // strongly in creases, while rough lobes keep softer ambient reflection.
    float exponent = exp2(-16.0f * saturate(roughness) - 1.0f);
    return saturate(pow(saturate(NdotV + ao), exponent) - 1.0f + ao);
}

float3 SceneMaterialCinematicDirectDiffuseTint(uint sceneMaterialClass,
                                               uint surfaceClass)
{
    switch (sceneMaterialClass) {
        case SCENE_MATERIAL_PAINTED_WALL:   return float3(1.025f, 1.014f, 0.992f);
        case SCENE_MATERIAL_CERAMIC_TILE:   return float3(0.960f, 1.010f, 1.040f);
        case SCENE_MATERIAL_POLISHED_WOOD:  return float3(1.075f, 1.030f, 0.935f);
        case SCENE_MATERIAL_BRUSHED_METAL:  return float3(0.980f, 0.998f, 1.025f);
        case SCENE_MATERIAL_POLISHED_METAL: return float3(0.990f, 1.000f, 1.018f);
        case SCENE_MATERIAL_GLASS_PANE:     return float3(0.925f, 1.018f, 1.075f);
        case SCENE_MATERIAL_FABRIC:         return float3(1.038f, 0.996f, 0.970f);
        case SCENE_MATERIAL_PLASTIC:        return float3(1.012f, 1.000f, 0.990f);
        case SCENE_MATERIAL_WET_SURFACE:    return float3(0.948f, 1.012f, 1.042f);
        case SCENE_MATERIAL_CONCRETE:       return float3(1.015f, 1.008f, 0.982f);
        case SCENE_MATERIAL_RUBBER:         return float3(0.988f, 0.988f, 0.986f);
        default:
            if (surfaceClass == SURFACE_CLASS_WOOD) {
                return float3(1.060f, 1.025f, 0.945f);
            }
            if (surfaceClass == SURFACE_CLASS_MASONRY) {
                return float3(1.015f, 1.008f, 0.982f);
            }
            if (surfaceClass == SURFACE_CLASS_BRUSHED_METAL) {
                return float3(0.980f, 0.998f, 1.025f);
            }
            return 1.0f.xxx;
    }
}

float SceneMaterialCinematicDirectSpecularGain(uint sceneMaterialClass,
                                               uint surfaceClass,
                                               float roughness,
                                               float metallic,
                                               float clearCoatWeight)
{
    float gain = 1.0f;
    switch (sceneMaterialClass) {
        case SCENE_MATERIAL_PAINTED_WALL:   gain = 0.86f; break;
        case SCENE_MATERIAL_CERAMIC_TILE:   gain = 1.12f; break;
        case SCENE_MATERIAL_POLISHED_WOOD:  gain = 1.06f; break;
        case SCENE_MATERIAL_BRUSHED_METAL:  gain = 1.08f; break;
        case SCENE_MATERIAL_POLISHED_METAL: gain = 1.16f; break;
        case SCENE_MATERIAL_GLASS_PANE:     gain = 1.10f; break;
        case SCENE_MATERIAL_FABRIC:         gain = 0.68f; break;
        case SCENE_MATERIAL_PLASTIC:        gain = 0.92f; break;
        case SCENE_MATERIAL_WET_SURFACE:    gain = 1.18f; break;
        case SCENE_MATERIAL_CONCRETE:       gain = 0.78f; break;
        case SCENE_MATERIAL_RUBBER:         gain = 0.62f; break;
        case SCENE_MATERIAL_WATER:          gain = 1.16f; break;
        default:
            if (surfaceClass == SURFACE_CLASS_BRUSHED_METAL ||
                surfaceClass == SURFACE_CLASS_GLASS ||
                surfaceClass == SURFACE_CLASS_WATER) {
                gain = 1.08f;
            } else if (surfaceClass == SURFACE_CLASS_MASONRY) {
                gain = 0.82f;
            }
            break;
    }

    float gloss = saturate(1.0f - roughness);
    gain = lerp(1.0f, gain, saturate(gloss * 0.65f + metallic * 0.35f + clearCoatWeight * 0.45f));
    return saturate(gain);
}

float3 ApplySceneMaterialCinematicDirectBRDF(float3 diffuseTerm,
                                             float3 specularTerm,
                                             float3 albedo,
                                             uint sceneMaterialClass,
                                             uint surfaceClass,
                                             float roughness,
                                             float metallic,
                                             float clearCoatWeight,
                                             float sheenWeight,
                                             float NdotV,
                                             float NdotL,
                                             float LdotH)
{
    float3 base = max(diffuseTerm + specularTerm, 0.0f.xxx);
    float cinematicActive = saturate(max(g_CinematicStabilityParams.z - 1.0f, 0.0f) * 2.5f +
                                     g_CinematicStabilityParams.w * 3.0f);
    if (cinematicActive <= 0.001f) {
        return base;
    }

    float3 diffuseTint = SceneMaterialCinematicDirectDiffuseTint(sceneMaterialClass, surfaceClass);
    float3 shapedDiffuse = diffuseTerm * lerp(1.0f.xxx, diffuseTint, cinematicActive * 0.42f);

    float specularGain = SceneMaterialCinematicDirectSpecularGain(
        sceneMaterialClass, surfaceClass, roughness, metallic, clearCoatWeight);
    float3 shapedSpecular = specularTerm * lerp(1.0f, specularGain, cinematicActive);

    float fabricLike =
        (sceneMaterialClass == SCENE_MATERIAL_FABRIC ||
         sceneMaterialClass == SCENE_MATERIAL_PAINTED_WALL ||
         sceneMaterialClass == SCENE_MATERIAL_RUBBER) ? 1.0f : 0.0f;
    float velvet = pow(saturate(1.0f - NdotV), 2.2f) *
                   pow(saturate(1.0f - NdotL), 0.65f) *
                   (0.045f + sheenWeight * 0.075f) *
                   fabricLike *
                   cinematicActive;
    float3 sheenLayer = albedo * velvet * (1.0f - saturate(metallic));

    float grazingSparkleGuard = 1.0f - smoothstep(0.985f, 1.0f, LdotH) * saturate(roughness * 0.65f);
    float3 shaped = shapedDiffuse + shapedSpecular * grazingSparkleGuard + sheenLayer;

    float baseLuma = max(dot(base, float3(0.2126f, 0.7152f, 0.0722f)), 1e-4f);
    float shapedLuma = dot(shaped, float3(0.2126f, 0.7152f, 0.0722f));
    float lumaCeiling = baseLuma * lerp(1.06f, 1.26f, saturate((1.0f - roughness) + metallic + clearCoatWeight));
    if (shapedLuma > lumaCeiling) {
        shaped *= lumaCeiling / max(shapedLuma, 1e-4f);
    }

    return max(shaped, 0.0f.xxx);
}

float SceneMaterialCinematicIndirectContactStrength(uint sceneMaterialClass,
                                                    uint surfaceClass,
                                                    float roughness,
                                                    float metallic)
{
    if (sceneMaterialClass == SCENE_MATERIAL_EMISSIVE_NEON ||
        sceneMaterialClass == SCENE_MATERIAL_SCREEN_PANEL ||
        sceneMaterialClass == SCENE_MATERIAL_MIRROR ||
        surfaceClass == SURFACE_CLASS_EMISSIVE ||
        surfaceClass == SURFACE_CLASS_MIRROR) {
        return 0.0f;
    }

    float strength = 0.12f;
    switch (sceneMaterialClass) {
        case SCENE_MATERIAL_PAINTED_WALL:   strength = 0.15f; break;
        case SCENE_MATERIAL_CERAMIC_TILE:   strength = 0.13f; break;
        case SCENE_MATERIAL_POLISHED_WOOD:  strength = 0.15f; break;
        case SCENE_MATERIAL_BRUSHED_METAL:  strength = 0.075f; break;
        case SCENE_MATERIAL_POLISHED_METAL: strength = 0.055f; break;
        case SCENE_MATERIAL_GLASS_PANE:     strength = 0.040f; break;
        case SCENE_MATERIAL_FABRIC:         strength = 0.20f; break;
        case SCENE_MATERIAL_PLASTIC:        strength = 0.12f; break;
        case SCENE_MATERIAL_WET_SURFACE:    strength = 0.10f; break;
        case SCENE_MATERIAL_CONCRETE:       strength = 0.20f; break;
        case SCENE_MATERIAL_RUBBER:         strength = 0.22f; break;
        case SCENE_MATERIAL_WATER:          strength = 0.045f; break;
        default:
            if (surfaceClass == SURFACE_CLASS_MASONRY) {
                strength = 0.18f;
            } else if (surfaceClass == SURFACE_CLASS_WOOD) {
                strength = 0.15f;
            } else if (surfaceClass == SURFACE_CLASS_BRUSHED_METAL ||
                       surfaceClass == SURFACE_CLASS_GLASS ||
                       surfaceClass == SURFACE_CLASS_WATER) {
                strength = 0.055f;
            }
            break;
    }

    float roughReceiver = lerp(0.55f, 1.18f, saturate(roughness));
    float dielectricReceiver = lerp(1.0f, 0.35f, saturate(metallic));
    return strength * roughReceiver * dielectricReceiver;
}

float3 SceneMaterialCinematicIndirectBounceTint(uint sceneMaterialClass,
                                                uint surfaceClass,
                                                float3 albedo)
{
    float3 tint = float3(1.0f, 1.0f, 1.0f);
    switch (sceneMaterialClass) {
        case SCENE_MATERIAL_PAINTED_WALL:   tint = float3(1.035f, 1.018f, 0.990f); break;
        case SCENE_MATERIAL_CERAMIC_TILE:   tint = float3(0.965f, 1.012f, 1.045f); break;
        case SCENE_MATERIAL_POLISHED_WOOD:  tint = float3(1.095f, 1.035f, 0.940f); break;
        case SCENE_MATERIAL_BRUSHED_METAL:  tint = float3(0.975f, 0.995f, 1.030f); break;
        case SCENE_MATERIAL_POLISHED_METAL: tint = float3(0.985f, 0.995f, 1.020f); break;
        case SCENE_MATERIAL_GLASS_PANE:     tint = float3(0.930f, 1.020f, 1.090f); break;
        case SCENE_MATERIAL_FABRIC:         tint = float3(1.045f, 1.000f, 0.975f); break;
        case SCENE_MATERIAL_PLASTIC:        tint = float3(1.020f, 1.000f, 0.990f); break;
        case SCENE_MATERIAL_WET_SURFACE:    tint = float3(0.955f, 1.012f, 1.045f); break;
        case SCENE_MATERIAL_CONCRETE:       tint = float3(1.018f, 1.010f, 0.980f); break;
        case SCENE_MATERIAL_RUBBER:         tint = float3(0.990f, 0.990f, 0.985f); break;
        case SCENE_MATERIAL_WATER:          tint = float3(0.900f, 1.025f, 1.095f); break;
        default:
            if (surfaceClass == SURFACE_CLASS_WOOD) {
                tint = float3(1.080f, 1.030f, 0.945f);
            } else if (surfaceClass == SURFACE_CLASS_MASONRY) {
                tint = float3(1.020f, 1.010f, 0.980f);
            }
            break;
    }

    const float albedoLuma = dot(saturate(albedo), float3(0.2126f, 0.7152f, 0.0722f));
    return lerp(tint, tint * lerp(0.88f, 1.10f, albedoLuma), 0.45f);
}

float3 ApplySceneMaterialCinematicIndirectShaping(float3 indirect,
                                                  float3 albedo,
                                                  float3 normal,
                                                  uint sceneMaterialClass,
                                                  uint surfaceClass,
                                                  float roughness,
                                                  float metallic,
                                                  float ao,
                                                  float NdotV)
{
    // Default/non-cinematic frames publish z=1,w=0. Scene-local cinematic
    // profiles publish stable-shadow and highlight-protection values above
    // that baseline, which makes this effect profile-owned without needing
    // another constant-buffer field.
    float cinematicActive = saturate(max(g_CinematicStabilityParams.z - 1.0f, 0.0f) * 2.5f +
                                     g_CinematicStabilityParams.w * 3.0f);
    if (cinematicActive <= 0.001f) {
        return indirect;
    }

    float contactStrength = SceneMaterialCinematicIndirectContactStrength(
        sceneMaterialClass, surfaceClass, roughness, metallic);
    if (contactStrength <= 0.001f) {
        return indirect;
    }

    float cavity = pow(saturate(1.0f - ao), 1.25f);
    float grazing = pow(saturate(1.0f - NdotV), 2.0f);
    float receiver = lerp(0.82f, 1.10f, saturate(roughness));
    float verticalSoftening = lerp(1.04f, 0.88f, saturate(abs(normal.y)));
    float contact = saturate(cavity * receiver * verticalSoftening);

    float3 bounceTint = SceneMaterialCinematicIndirectBounceTint(sceneMaterialClass, surfaceClass, albedo);
    float3 shaped = indirect * (1.0f - contact * contactStrength * cinematicActive);
    float bounceLift = (1.0f - contact) * contactStrength * cinematicActive * 0.32f;
    shaped += indirect * (bounceTint - 1.0f.xxx) * (0.55f + grazing * 0.30f) * cinematicActive;
    shaped += albedo * bounceTint * bounceLift * (1.0f - saturate(metallic)) * 0.035f;
    return max(shaped, 0.0f.xxx);
}

float ComputeProbeWeight(float3 worldPos, float3 center, float3 extents, float blendDistance)
{
    float3 d = abs(worldPos - center) - extents;
    float3 outside = max(d, 0.0f);
    float distOutside = max(max(outside.x, outside.y), outside.z);
    if (distOutside <= 0.0f) {
        return 1.0f;
    }
    if (blendDistance <= 1e-5f) {
        return 0.0f;
    }
    return saturate(1.0f - (distOutside / blendDistance));
}

float3 BoxProjectReflection(float3 worldPos, float3 reflDir, float3 center, float3 extents)
{
    float3 dir = normalize(reflDir);
    if (!all(isfinite(dir))) {
        return float3(0.0f, 0.0f, 1.0f);
    }

    float3 boxMin = center - extents;
    float3 boxMax = center + extents;

    float3 invDir = rcp(max(abs(dir), 1e-6f)) * sign(dir);
    float3 t0 = (boxMin - worldPos) * invDir;
    float3 t1 = (boxMax - worldPos) * invDir;

    float3 tmin = min(t0, t1);
    float3 tmax = max(t0, t1);

    float tNear = max(max(tmin.x, tmin.y), tmin.z);
    float tFar = min(min(tmax.x, tmax.y), tmax.z);

    if (tNear > tFar) {
        return dir;
    }

    float tHit = (tNear > 0.0f) ? tNear : tFar;
    if (tHit <= 0.0f || !isfinite(tHit)) {
        return dir;
    }

    float3 hitPos = worldPos + dir * tHit;
    return normalize(hitPos - center);
}

uint ComputeClusterZ(float viewZ)
{
    float nearZ = g_ProjectionParams.z;
    float farZ = g_ProjectionParams.w;
    uint clusterCountZ = g_ClusterParams.x;

    viewZ = max(viewZ, nearZ);
    float denom = log(max(farZ / nearZ, 1.0001f));
    float t = (denom > 0.0f) ? (log(viewZ / nearZ) / denom) : 0.0f;
    t = saturate(t);

    uint z = (uint)floor(t * (float)clusterCountZ);
    return min(z, clusterCountZ - 1u);
}

uint2 StableShadowMapDimensions()
{
    float2 invSize = max(g_ShadowInvSizeAndSpecMaxMip.xy, 1e-7f.xx);
    return max((uint2)round(1.0f.xx / invSize), uint2(1u, 1u));
}

float LoadStableShadowDepth(float2 shadowUV, uint cascadeIndex, int2 texelOffset)
{
    uint2 dim = StableShadowMapDimensions();
    int2 maxCoord = int2(dim) - 1;
    int2 baseCoord = int2(floor(saturate(shadowUV) * float2(dim)));
    baseCoord = clamp(baseCoord, int2(0, 0), maxCoord);
    int2 coord = clamp(baseCoord + texelOffset, int2(0, 0), maxCoord);
    return g_ShadowMap.Load(int4(coord, (int)cascadeIndex, 0));
}

float ShadowDepthCompare(float depthSample, float currentDepth, float bias)
{
    return (currentDepth - bias > depthSample) ? 0.0f : 1.0f;
}

float QuantizeStableShadowRadius(float radius)
{
    radius = clamp(radius, 0.0f, 7.0f);
    if (radius <= 0.25f) {
        return 0.0f;
    }
    return max(1.0f, floor(radius * 2.0f + 0.5f) * 0.5f);
}

float SceneMaterialCinematicShadowReceiverSoftness(uint sceneMaterialClass,
                                                   uint surfaceClass,
                                                   float roughness,
                                                   float metallic)
{
    if (surfaceClass == SURFACE_CLASS_EMISSIVE ||
        surfaceClass == SURFACE_CLASS_GLASS ||
        surfaceClass == SURFACE_CLASS_MIRROR ||
        surfaceClass == SURFACE_CLASS_WATER ||
        sceneMaterialClass == SCENE_MATERIAL_EMISSIVE_NEON ||
        sceneMaterialClass == SCENE_MATERIAL_SCREEN_PANEL ||
        sceneMaterialClass == SCENE_MATERIAL_GLASS_PANE ||
        sceneMaterialClass == SCENE_MATERIAL_MIRROR ||
        sceneMaterialClass == SCENE_MATERIAL_WATER) {
        return 0.0f;
    }

    float softness = 0.44f;
    switch (sceneMaterialClass) {
        case SCENE_MATERIAL_PAINTED_WALL:  softness = 0.74f; break;
        case SCENE_MATERIAL_CERAMIC_TILE:  softness = 0.52f; break;
        case SCENE_MATERIAL_POLISHED_WOOD: softness = 0.58f; break;
        case SCENE_MATERIAL_FABRIC:        softness = 0.88f; break;
        case SCENE_MATERIAL_PLASTIC:       softness = 0.48f; break;
        case SCENE_MATERIAL_CONCRETE:      softness = 0.82f; break;
        case SCENE_MATERIAL_RUBBER:        softness = 0.86f; break;
        default:
            if (surfaceClass == SURFACE_CLASS_MASONRY) {
                softness = 0.78f;
            } else if (surfaceClass == SURFACE_CLASS_WOOD) {
                softness = 0.56f;
            } else if (surfaceClass == SURFACE_CLASS_BRUSHED_METAL) {
                softness = 0.22f;
            }
            break;
    }

    float roughReceiver = lerp(0.45f, 1.10f, saturate(roughness));
    float conductorSuppression = lerp(1.0f, 0.32f, saturate(metallic));
    return saturate(softness * roughReceiver * conductorSuppression);
}

float ApplySceneMaterialCinematicShadowRadius(float pcfRadius,
                                              uint sceneMaterialClass,
                                              uint surfaceClass,
                                              float roughness,
                                              float metallic,
                                              bool isLocalLight)
{
    float cinematicActive = saturate(max(g_CinematicStabilityParams.z - 1.0f, 0.0f) * 2.5f +
                                     g_CinematicStabilityParams.w * 3.0f);
    float receiverSoftness = SceneMaterialCinematicShadowReceiverSoftness(
        sceneMaterialClass, surfaceClass, roughness, metallic);
    float maxScale = isLocalLight ? 1.22f : 1.42f;
    float scaled = pcfRadius * lerp(1.0f, maxScale, receiverSoftness * cinematicActive);
    return QuantizeStableShadowRadius(scaled);
}

float SampleStableShadowPCF(float2 shadowUV, float currentDepth, float bias, float pcfRadius, uint cascadeIndex)
{
    float radius = QuantizeStableShadowRadius(pcfRadius);
    if (radius <= 0.0f) {
        float depthCenter = LoadStableShadowDepth(shadowUV, cascadeIndex, int2(0, 0));
        return ShadowDepthCompare(depthCenter, currentDepth, bias);
    }

    float shadow = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (int x = -1; x <= 1; ++x) {
        [unroll]
        for (int y = -1; y <= 1; ++y) {
            int2 offset = int2(round(float2(x, y) * radius));
            float wx = 2.0f - abs((float)x);
            float wy = 2.0f - abs((float)y);
            float weight = wx * wy;
            float depthSample = LoadStableShadowDepth(shadowUV, cascadeIndex, offset);
            shadow += ShadowDepthCompare(depthSample, currentDepth, bias) * weight;
            weightSum += weight;
        }
    }

    return shadow / max(weightSum, 1e-4f);
}

float SamplePCF(float2 shadowUV, float currentDepth, float bias, float pcfRadius, uint cascadeIndex) {
    return SampleStableShadowPCF(shadowUV, currentDepth, bias, pcfRadius, cascadeIndex);
}

float ComputeShadowCascade(float3 worldPos,
                           float3 normal,
                           uint cascadeIndex,
                           uint sceneMaterialClass,
                           uint surfaceClass,
                           float roughness,
                           float metallic) {
    cascadeIndex = min(cascadeIndex, 2u);

    float3 lightDirWS = normalize(g_SunDirection.xyz);
    float4 lightClip = mul(g_LightViewProjection[cascadeIndex], float4(worldPos, 1.0f));
    if (lightClip.w <= 1e-4f || !all(isfinite(lightClip))) {
        return 1.0f;
    }

    float3 lightNDC = lightClip.xyz / lightClip.w;
    if (lightNDC.x < -1.0f || lightNDC.x > 1.0f ||
        lightNDC.y < -1.0f || lightNDC.y > 1.0f ||
        lightNDC.z < 0.0f || lightNDC.z > 1.0f) {
        return 1.0f;
    }

    float2 shadowUV;
    shadowUV.x = 0.5f * lightNDC.x + 0.5f;
    shadowUV.y = -0.5f * lightNDC.y + 0.5f;

    float currentDepth = lightNDC.z;

    float bias = g_ShadowParams.x;
    float pcfRadius = g_ShadowParams.y;
    float stableShadowScale = max(g_CinematicStabilityParams.z, 1.0f);
    pcfRadius *= stableShadowScale;
    pcfRadius = ApplySceneMaterialCinematicShadowRadius(
        pcfRadius, sceneMaterialClass, surfaceClass, roughness, metallic, false);

    float cascadeScale = 1.0f + (float)cascadeIndex * 0.5f;
    bias *= cascadeScale;
    bias *= lerp(1.0f, 1.12f, saturate(stableShadowScale - 1.0f));
    float receiverSoftness = SceneMaterialCinematicShadowReceiverSoftness(
        sceneMaterialClass, surfaceClass, roughness, metallic);
    bias *= lerp(1.0f, 1.08f, receiverSoftness * saturate(stableShadowScale - 1.0f));

    // Match the forward path's conservative slope-scaled bias. The old
    // deferred-only aggressive bias/receiver offset made broad shell planes
    // change shadow family during mouse-look instead of preserving parity.
    float ndotl = saturate(dot(normal, lightDirWS));
    float slopeBias = lerp(1.5f, 0.5f, ndotl);
    bias *= slopeBias;

    if (g_ShadowParams.w > 0.5f) {
        float searchRadius = QuantizeStableShadowRadius(pcfRadius * 2.0f);

        float avgBlocker = 0.0f;
        int blockerCount = 0;

        [unroll]
        for (int x = -1; x <= 1; ++x) {
            [unroll]
            for (int y = -1; y <= 1; ++y) {
                int2 offset = int2(round(float2(x, y) * searchRadius));
                float depthSample = LoadStableShadowDepth(shadowUV, cascadeIndex, offset);
                if (depthSample + bias < currentDepth) {
                    avgBlocker += depthSample;
                    blockerCount++;
                }
            }
        }

        if (blockerCount > 0) {
            avgBlocker /= blockerCount;
            float penumbra = saturate((currentDepth - avgBlocker) / max(avgBlocker, 1e-4f));
            pcfRadius = QuantizeStableShadowRadius(pcfRadius * (1.0f + penumbra * 3.0f));
        }
    }

    return SamplePCF(shadowUV, currentDepth, bias, pcfRadius, cascadeIndex);
}

float ComputeShadow(float3 worldPos,
                    float3 normal,
                    uint sceneMaterialClass,
                    uint surfaceClass,
                    float roughness,
                    float metallic) {
    if (g_ShadowParams.z < 0.5f) {
        return 1.0f;
    }

    float3 viewPos = mul(g_ViewMatrix, float4(worldPos, 1.0f)).xyz;
    float depth = viewPos.z;

    float split0 = g_CascadeSplits.x;
    float split1 = g_CascadeSplits.y;

    uint primary = 0;
    uint secondary = 0;
    float blend = 0.0f;

    float range0 = max(split0 * 0.2f, 4.0f);
    float range1 = max(split1 * 0.2f, 8.0f);

    if (depth <= split0) {
        primary = 0;
        secondary = 1;
        float d = split0 - depth;
        blend = saturate(1.0f - d / range0);
    } else if (depth <= split1) {
        float d0 = depth - split0;
        float d1 = split1 - depth;
        primary = 1;
        if (d0 < d1) {
            secondary = 0;
            blend = saturate(1.0f - d0 / range0);
        } else {
            secondary = 2;
            blend = saturate(1.0f - d1 / range1);
        }
    } else {
        primary = 2;
        secondary = 1;
        float d = depth - split1;
        blend = saturate(1.0f - d / range1);
    }

    primary = min(primary, 2u);
    secondary = min(secondary, 2u);

    float shadowPrimary = ComputeShadowCascade(
        worldPos, normal, primary, sceneMaterialClass, surfaceClass, roughness, metallic);
    if (blend <= 0.001f || primary == secondary) {
        return shadowPrimary;
    }

    float shadowSecondary = ComputeShadowCascade(
        worldPos, normal, secondary, sceneMaterialClass, surfaceClass, roughness, metallic);
    return lerp(shadowPrimary, shadowSecondary, blend);
}

// Local light shadow evaluation for a shadow-mapped spotlight. Matches the
// CPU convention used by Basic.hlsl: light.params.y holds the shadow-map
// slice index in the shared shadow-map array (3..5 for local lights).
float ComputeLocalLightShadow(float3 worldPos,
                              float3 normal,
                              float3 lightDir,
                              float shadowIndex,
                              uint sceneMaterialClass,
                              uint surfaceClass,
                              float roughness,
                              float metallic)
{
    if (g_ShadowParams.z < 0.5f) {
        return 1.0f;
    }
    if (shadowIndex < 0.0f) {
        return 1.0f;
    }

    uint slice = (uint)shadowIndex;
    slice = min(slice, 5u);

    float4 lightClip = mul(g_LightViewProjection[slice], float4(worldPos, 1.0f));
    if (lightClip.w <= 1e-4f || !all(isfinite(lightClip))) {
        return 1.0f;
    }

    float3 lightNDC = lightClip.xyz / lightClip.w;
    if (lightNDC.x < -1.0f || lightNDC.x > 1.0f ||
        lightNDC.y < -1.0f || lightNDC.y > 1.0f ||
        lightNDC.z < 0.0f  || lightNDC.z > 1.0f) {
        return 1.0f;
    }

    float2 shadowUV;
    shadowUV.x = 0.5f * lightNDC.x + 0.5f;
    shadowUV.y = -0.5f * lightNDC.y + 0.5f;

    float currentDepth = lightNDC.z;

    float bias = g_ShadowParams.x * 0.5f;
    float pcfRadius = g_ShadowParams.y * 0.75f * max(g_CinematicStabilityParams.z, 1.0f);
    pcfRadius = ApplySceneMaterialCinematicShadowRadius(
        pcfRadius, sceneMaterialClass, surfaceClass, roughness, metallic, true);

    float ndotl = saturate(dot(normalize(normal), normalize(lightDir)));
    bias *= lerp(1.5f, 0.5f, ndotl);

    return SamplePCF(shadowUV, currentDepth, bias, pcfRadius, slice);
}

// Reconstruct world position from depth
float3 ReconstructWorldPosition(float2 uv, float depth) {
    // Convert UV and depth to NDC
    float4 ndc = float4(
        uv.x * 2.0 - 1.0,
        (1.0 - uv.y) * 2.0 - 1.0,
        depth,
        1.0
    );

    // Transform to world space
    float4 worldPos = mul(g_InvViewProj, ndc);
    return worldPos.xyz / worldPos.w;
}

struct VSOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

// Fullscreen triangle vertex shader
VSOutput VSMain(uint vertexID : SV_VertexID) {
    VSOutput output;

    // Generate fullscreen triangle
    float2 texCoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(texCoord * float2(2, -2) + float2(-1, 1), 0, 1);
    output.texCoord = texCoord;

    return output;
}

float ApplyDeferredAnisotropy(
    float baseRoughness,
    float anisotropy,
    float3 normal,
    float3 halfVector,
    out float lobeScale)
{
    lobeScale = 1.0f;
    anisotropy = saturate(anisotropy);
    baseRoughness = max(baseRoughness, 0.02f);
    if (anisotropy <= 0.01f) {
        return baseRoughness;
    }

    float3 up = (abs(normal.y) < 0.95f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent = normalize(cross(up, normal));
    float tangentAlignment = abs(dot(normalize(halfVector), tangent));
    float anisotropicRoughness = max(0.02f, baseRoughness * (1.0f - 0.45f * anisotropy));

    lobeScale = lerp(1.0f, lerp(0.72f, 1.28f, tangentAlignment), anisotropy);
    return lerp(baseRoughness, anisotropicRoughness, anisotropy);
}

// Deferred lighting pixel shader
float4 PSMain(VSOutput input) : SV_Target0 {
    uint2 pixelCoord = uint2(input.position.xy);
    float2 screenSize = max(float2(g_ScreenAndCluster.xy), float2(1.0f, 1.0f));
    float2 pixelUv = (float2(pixelCoord) + 0.5f) / screenSize;

    // Sample G-buffers
    float4 albedo = g_GBufferAlbedo.Load(int3(pixelCoord, 0));
    float4 normalRoughness = g_GBufferNormalRoughness.Load(int3(pixelCoord, 0));
    float4 emissiveMetallic = g_GBufferEmissiveMetallic.Load(int3(pixelCoord, 0));
    float4 materialExt0 = g_GBufferMaterialExt0.Load(int3(pixelCoord, 0));
    float4 materialExt1 = g_GBufferMaterialExt1.Load(int3(pixelCoord, 0));
    float4 materialExt2 = g_GBufferMaterialExt2.Load(int3(pixelCoord, 0));
    float depth = g_DepthBuffer.Load(int3(pixelCoord, 0));

    // Unpack G-buffer data
    float3 albedoColor = albedo.rgb;
    float ao = saturate(albedo.a);
    float3 normal = normalize(normalRoughness.xyz * 2.0f - 1.0f);
    float roughness = normalRoughness.w;
    float3 emissive = emissiveMetallic.rgb;
    float metallic = emissiveMetallic.a;

    float clearCoatWeight = saturate(materialExt0.x);
    float clearCoatRoughness = saturate(materialExt0.y);
    float ior = max(materialExt0.z, 1.0f);
    float specularFactor = saturate(materialExt0.w);
    float3 specularColor = saturate(materialExt1.rgb);
    uint surfaceClass = DecodeSurfaceClass(materialExt2.r);
    uint sceneMaterialClass = DecodeSceneMaterialClass(materialExt2.a);
    float anisotropy = saturate(materialExt2.g);
    float sheenWeight = saturate(materialExt2.b);
    float subsurfaceWrap = SceneMaterialSubsurfaceWrap(sceneMaterialClass);

    // Check for background pixels (depth = 1.0)
    if (depth >= 0.9999) {
        float3 worldFar = ReconstructWorldPosition(pixelUv, 1.0f);
        float3 viewDir = normalize(worldFar - g_CameraPosition.xyz);

        bool iblEnabled = (g_EnvParams.z > 0.5f);
        float backgroundExposure = saturate(g_EnvParams.w);
        if (backgroundExposure <= 0.001f)
        {
            return float4(0.0f, 0.0f, 0.0f, 1.0f);
        }
        if (iblEnabled) {
            // Match the forward sky path: presentation blur samples higher
            // prefiltered mips so detailed HDRIs do not shimmer during
            // mouse-look.
            uint specWidth;
            uint specHeight;
            uint specMipCount;
            g_EnvSpecular.GetDimensions(0, specWidth, specHeight, specMipCount);
            float specMaxMip = max((specMipCount > 0u) ? (float)(specMipCount - 1u) : 0.0f,
                                   g_ShadowInvSizeAndSpecMaxMip.z);
            float backgroundMip = saturate(g_AmbientColor.w) * specMaxMip;
            float3 sky = SampleEnvSpecular(viewDir, backgroundMip, INVALID_BINDLESS_INDEX) *
                         g_EnvParams.y * backgroundExposure;
            return float4(sky, 1.0f);
        } else {
            return float4(ComputeLocalOutdoorSky(viewDir) * backgroundExposure, 1.0f);
        }
    }

    // Reconstruct world position from depth
    float3 worldPos = ReconstructWorldPosition(pixelUv, depth);

    // View direction
    float3 V = normalize(g_CameraPosition.xyz - worldPos);
    float NdotV = max(dot(normal, V), 0.0);
    roughness = max(saturate(roughness), SurfaceRoughnessFloor(surfaceClass, metallic));
    {
        // Forward shading widens specular lobes when the projected normal
        // field varies quickly. VB material resolve writes a per-pixel normal
        // buffer, so recover the same stabilizing signal here before lighting.
        float3 dnDx = ddx(normal);
        float3 dnDy = ddy(normal);
        float normalVariance = saturate(dot(dnDx, dnDx) + dot(dnDy, dnDy));
        float varianceBoost = SurfaceNormalVarianceRoughnessBoost(surfaceClass, roughness, metallic);
        roughness = saturate(roughness + normalVariance * varianceBoost);
    }

    // PBR material properties (KHR_materials_ior + KHR_materials_specular).
    float f0Ior = pow((ior - 1.0f) / max(ior + 1.0f, 1e-4f), 2.0f);
    float3 dielectricF0 = f0Ior.xxx * specularFactor * specularColor;
    float3 F0 = lerp(dielectricF0, albedoColor, metallic);

    // --- Directional Light (Sun) ---
    // Convention: g_SunDirection.xyz is direction-to-light (matches Basic.hlsl).
    float3 L = normalize(g_SunDirection.xyz);
    float3 H = normalize(V + L);

    float NdotL = max(dot(normal, L), 0.0);

#if DEFERRED_DEBUG_VIEW == 1
    return float4(normal * 0.5f + 0.5f, 1.0f);
#elif DEFERRED_DEBUG_VIEW == 2
    return float4(NdotL.xxx, 1.0f);
#endif

    // Cook-Torrance BRDF
    float anisotropicLobeScale = 1.0f;
    float specularRoughness = ApplyDeferredAnisotropy(roughness, anisotropy, normal, H, anisotropicLobeScale);
    float NDF = DistributionGGX(normal, H, specularRoughness);
    float G = GeometrySmith(normal, V, L, specularRoughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 numerator = NDF * G * F;
    float denominator = 4.0 * NdotV * NdotL;
    float3 specular = (numerator / max(denominator, 0.001)) *
                      anisotropicLobeScale *
                      RoughSpecularEnergyCompensation(F0, specularRoughness);

    // Optional clearcoat layer: match Basic.hlsl behavior (second dielectric lobe).
    if (clearCoatWeight > 0.01f) {
        float coatBlend = clearCoatWeight * 0.8f;
        float3 F_coat = FresnelSchlick(max(dot(H, V), 0.0f), float3(0.04f, 0.04f, 0.04f));
        float  D_coat = DistributionGGX(normal, H, clearCoatRoughness);
        float  G_coat = GeometrySmith(normal, V, L, clearCoatRoughness);
        float3 specCoat = (D_coat * G_coat * F_coat) / max(denominator, 0.001f);
        specular = lerp(specular, specCoat, coatBlend);
    }

    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);
    if (subsurfaceWrap > 0.01f) {
        float lambert = max(NdotL, 1e-4f);
        float wrapped = saturate((NdotL + subsurfaceWrap) / (1.0f + subsurfaceWrap));
        kD *= wrapped / lambert;
    }

    if (sheenWeight > 0.01f) {
        float sheen = pow(saturate(1.0f - NdotL), 4.0f) *
                      pow(saturate(1.0f - NdotV), 4.0f);
        specular += sheenWeight * sheen * albedoColor;
    }

    // Shadow
    float shadow = ComputeShadow(worldPos, normal, sceneMaterialClass, surfaceClass, roughness, metallic);
    if (g_ReflectionProbeParams.z == 43u) {
        return float4(shadow.xxx, 1.0f);
    }
    // Direct lighting (sun)
    float sunLdotH = saturate(dot(L, H));
    float diffuseBurley = BurleyDiffuseFactor(NdotV, NdotL, sunLdotH, roughness);
    float3 sunBrdf = ApplySceneMaterialCinematicDirectBRDF(
        kD * albedoColor * (diffuseBurley / PI),
        specular,
        albedoColor,
        sceneMaterialClass,
        surfaceClass,
        roughness,
        metallic,
        clearCoatWeight,
        sheenWeight,
        NdotV,
        NdotL,
        sunLdotH);
    float3 directLightUnshadowed = sunBrdf * g_SunRadiance.rgb * NdotL;
    float3 directLight = directLightUnshadowed * shadow;

    // Clustered local lights.
    if (g_ClusterParams.z > 0u) {
        uint width = g_ScreenAndCluster.x;
        uint height = g_ScreenAndCluster.y;
        uint clusterCountX = g_ScreenAndCluster.z;
        uint clusterCountY = g_ScreenAndCluster.w;

        uint tileW = (width + clusterCountX - 1u) / clusterCountX;
        uint tileH = (height + clusterCountY - 1u) / clusterCountY;

        uint cx = min(pixelCoord.x / max(tileW, 1u), clusterCountX - 1u);
        uint cy = min(pixelCoord.y / max(tileH, 1u), clusterCountY - 1u);

        float viewZ = mul(g_ViewMatrix, float4(worldPos, 1.0f)).z;
        uint cz = ComputeClusterZ(viewZ);

        uint clusterIndex = cx + cy * clusterCountX + cz * (clusterCountX * clusterCountY);
        uint2 range = g_ClusterRanges[clusterIndex];
        uint base = range.x;
        uint count = min(range.y, g_ClusterParams.y);

#if DEFERRED_DEBUG_VIEW == 3
        float occ = (g_ClusterParams.y > 0u) ? (float)count / (float)g_ClusterParams.y : 0.0f;
        return float4(occ.xxx, 1.0f);
#endif

        bool useAllLocalLightsFallback = false;
        uint lightLoopCount = count;
        if (g_ClusterParams.z > 0u && g_ClusterParams.z <= g_ClusterParams.y) {
            // Small showcase scenes have far fewer lights than the cluster
            // cap. Evaluating all of them is cheap and matches the forward
            // path, while avoiding cluster-boundary omissions that turn broad
            // shell receivers dark in forced-VB captures.
            useAllLocalLightsFallback = true;
            lightLoopCount = g_ClusterParams.z;
        } else if (lightLoopCount == 0u && g_ClusterParams.z > 0u) {
            useAllLocalLightsFallback = true;
            lightLoopCount = min(g_ClusterParams.z, g_ClusterParams.y);
        }

        [loop]
        for (uint i = 0; i < lightLoopCount; ++i) {
            uint lightIndex = useAllLocalLightsFallback ? i : g_ClusterLightIndices[base + i];
            if (lightIndex >= g_ClusterParams.z) {
                continue;
            }

            Light light = g_LocalLights[lightIndex];
            uint type = (uint)(light.position_type.w + 0.5f);
            if (type < 0.5f) {
                continue;
            }
            const bool isSpot = (type == LIGHT_TYPE_SPOT);
            const bool isAreaRect = (type == LIGHT_TYPE_AREA_RECT);
            const uint fixtureClass = DecodeFixtureClass(type, light);

            float3 lightPos = light.position_type.xyz;
            float3 toLight = lightPos - worldPos;
            float dist = length(toLight);
            if (dist < 1e-3f) {
                continue;
            }

            float rangeMeters = light.color_range.w;
            if (rangeMeters <= 0.0f || dist > rangeMeters) {
                continue;
            }

            float3 Ll = toLight / dist;
            float NdotLl = max(dot(normal, Ll), 0.0f);
            if (NdotLl <= 0.0f) {
                continue;
            }

            float att = saturate(1.0f - dist / rangeMeters);
            att *= att;
            if (isAreaRect) {
                // Match the forward path's softbox approximation: rectangular
                // area lights are broad emitters, not pin lights. Keeping a
                // minimum falloff preserves fill while the roughness adjustment
                // below prevents razor-sharp wet-floor highlights.
                att = max(att, fixtureClass == FIXTURE_CLASS_EMISSIVE ? 0.42f : 0.35f);
            }
            // Spot cone attenuation (approx).
            if (isSpot) {
                float3 spotDir = normalize(light.direction_cosInner.xyz);
                float cosInner = light.direction_cosInner.w;
                float cosOuter = light.params.x;
                float cosTheta = dot(spotDir, normalize(worldPos - lightPos));
                float spot = saturate((cosTheta - cosOuter) / max(cosInner - cosOuter, 1e-3f));
                spot = spot * spot;
                att *= spot;
            }

            // Match Basic.hlsl forward lighting. Scene-authored local lights
            // store practical radiance/intensity in color_range.rgb and use a
            // smooth range falloff, not physical inverse-square falloff. The
            // old deferred-only 1/dist^2 term effectively removed area/spot
            // fill from broad RT Showcase shell receivers, causing forced-VB
            // frames to be driven by high-contrast IBL and shadows instead.
            float3 radiance = light.color_range.rgb * att * FixtureRadianceScale(fixtureClass);

            float3 Hl = normalize(V + Ll);
            float roughForLight = FixtureRoughnessForSpecular(roughness, fixtureClass, isAreaRect);
            float localAnisotropicLobeScale = 1.0f;
            roughForLight = ApplyDeferredAnisotropy(roughForLight, anisotropy, normal, Hl, localAnisotropicLobeScale);
            float NDF_l = DistributionGGX(normal, Hl, roughForLight);
            float G_l = GeometrySmith(normal, V, Ll, roughForLight);
            float3 F_l = FresnelSchlick(max(dot(Hl, V), 0.0f), F0);

            float3 numerator_l = NDF_l * G_l * F_l;
            float denom_l = 4.0f * NdotV * NdotLl;
            float3 spec_l = (numerator_l / max(denom_l, 0.001f)) *
                            localAnisotropicLobeScale *
                            RoughSpecularEnergyCompensation(F0, roughForLight);

            if (clearCoatWeight > 0.01f) {
                float coatBlend = clearCoatWeight * 0.8f;
                float3 F_coat = FresnelSchlick(max(dot(Hl, V), 0.0f), float3(0.04f, 0.04f, 0.04f));
                float coatRoughForLight = isAreaRect ? saturate(clearCoatRoughness * 1.5f + 0.05f) : clearCoatRoughness;
                float  D_coat = DistributionGGX(normal, Hl, coatRoughForLight);
                float  G_coat = GeometrySmith(normal, V, Ll, coatRoughForLight);
                float3 specCoat = (D_coat * G_coat * F_coat) / max(denom_l, 0.001f);
                spec_l = lerp(spec_l, specCoat, coatBlend);
            }
            spec_l = min(spec_l, 4.0f.xxx);

            float3 kS_l = F_l;
            float3 kD_l = (1.0f - kS_l) * (1.0f - metallic);
            if (subsurfaceWrap > 0.01f) {
                float lambert_l = max(NdotLl, 1e-4f);
                float wrapped_l = saturate((NdotLl + subsurfaceWrap) / (1.0f + subsurfaceWrap));
                kD_l *= wrapped_l / lambert_l;
            }

            if (sheenWeight > 0.01f) {
                float sheen_l = pow(saturate(1.0f - NdotLl), 4.0f) *
                                pow(saturate(1.0f - NdotV), 4.0f);
                spec_l += sheenWeight * sheen_l * albedoColor;
            }

            float shadowLocal = 1.0f;
            if (isSpot && light.params.y >= 0.0f) {
                shadowLocal = ComputeLocalLightShadow(
                    worldPos,
                    normal,
                    Ll,
                    light.params.y,
                    sceneMaterialClass,
                    surfaceClass,
                    roughness,
                    metallic);
            }
            const float fixtureNdotLl = FixtureWrappedNdotL(NdotLl, fixtureClass);
            float localLdotH = saturate(dot(Ll, Hl));
            float localDiffuseBurley = BurleyDiffuseFactor(NdotV, fixtureNdotLl, localLdotH, roughness);
            float3 localBrdf = ApplySceneMaterialCinematicDirectBRDF(
                kD_l * albedoColor * (localDiffuseBurley / PI),
                spec_l,
                albedoColor,
                sceneMaterialClass,
                surfaceClass,
                roughness,
                metallic,
                clearCoatWeight,
                sheenWeight,
                NdotV,
                fixtureNdotLl,
                localLdotH);
            float3 localDirectUnshadowed = localBrdf * radiance * fixtureNdotLl;
            directLightUnshadowed += localDirectUnshadowed;
            directLight += localDirectUnshadowed * shadowLocal;
        }
    }

    // --- Image-Based Lighting (IBL) ---
    float3 ambient = float3(0.0f, 0.0f, 0.0f);
    const bool iblEnabled = (g_EnvParams.z > 0.5f);

    float3 diffuseIBL = 0.0f;
    float3 specularIBL = 0.0f;
    float3 Fibl = FresnelSchlickRoughness(NdotV, F0, roughness);
    float3 kD_ibl = (1.0 - metallic) * (1.0 - Fibl);
    if (subsurfaceWrap > 0.01f) {
        kD_ibl *= 1.0f + subsurfaceWrap * 0.35f;
    }
    const bool authoredInteriorNoEnvironment = (!iblEnabled && g_EnvParams.w <= 0.001f);

    // Hemisphere ambient fallback when IBL is disabled (outdoor/terrain lighting).
    // Enclosed authored scenes set background exposure to zero; in that mode,
    // do not synthesize sky/ground reflections because glossy indoor surfaces
    // should only carry scene-local light and SSR/RT hits.
    if (!iblEnabled) {
        if (authoredInteriorNoEnvironment) {
            ambient = g_AmbientColor.rgb * albedoColor * kD_ibl;
        } else {
            float3 skyColor = ComputeLocalOutdoorSky(float3(normal.x * 0.25f, 0.86f, normal.z * 0.25f));
            float3 groundColor = float3(0.045f, 0.080f, 0.060f) *
                                 saturate(dot(g_SunRadiance.rgb, float3(0.2126f, 0.7152f, 0.0722f)) * 0.20f + 0.20f);

            // Hemisphere blend based on world-space normal Y component
            float hemiBlend = normal.y * 0.5f + 0.5f; // Remap -1..1 to 0..1
            float3 hemisphereAmbient = lerp(groundColor, skyColor, hemiBlend);

            // Apply to diffuse (metallic surfaces get less diffuse ambient)
            ambient = hemisphereAmbient * albedoColor * kD_ibl;

            // Add rough specular approximation for non-IBL (sky reflection on shiny surfaces)
            float3 reflectionDir = reflect(-V, normal);
            float3 skyReflect = lerp(groundColor, ComputeLocalOutdoorSky(reflectionDir), saturate(reflectionDir.y * 0.5f + 0.5f));
            float roughFade = 1.0f - roughness; // Smooth surfaces get more sky reflection
            ambient += skyReflect * Fibl * roughFade * 0.34f;
        }
    }

    uint diffuseEnvIndex = INVALID_BINDLESS_INDEX;
    uint specularEnvIndex = INVALID_BINDLESS_INDEX;
    float3 specDir = reflect(-V, normal);
    float3 specDirGlobal = specDir;
    float probeWeight = 0.0f;

    uint specWidth = 1u;
    uint specHeight = 1u;
    uint specMipCount = 1u;
    g_EnvSpecular.GetDimensions(0, specWidth, specHeight, specMipCount);
    float specMaxMip = max((specMipCount > 0u) ? (float)(specMipCount - 1u) : g_ShadowInvSizeAndSpecMaxMip.z, 0.0f);
    if (specMaxMip <= 0.0f) {
        specMaxMip = max(g_ShadowInvSizeAndSpecMaxMip.z, 0.0f);
    }
    const float diffuseMip = specMaxMip;
    float specularMipLevelForDebug = roughness * specMaxMip;

#ifdef ENABLE_BINDLESS
    const uint probeCount = g_ReflectionProbeParams.y;
    const uint probeTableIndex = g_ReflectionProbeParams.x;
    if (probeCount > 0u && probeTableIndex != INVALID_BINDLESS_INDEX)
    {
        StructuredBuffer<ReflectionProbe> probes = ResourceDescriptorHeap[probeTableIndex];

        float bestW = 0.0f;
        uint bestI = 0u;

        const uint kMaxProbeIter = 64u;
        uint count = min(probeCount, kMaxProbeIter);
        [loop]
        for (uint i = 0u; i < count; ++i)
        {
            ReflectionProbe p = probes[i];
            float w = ComputeProbeWeight(worldPos, p.centerBlend.xyz, p.extents.xyz, p.centerBlend.w);
            if (w > bestW)
            {
                bestW = w;
                bestI = i;
            }
        }

        if (bestW > 0.0f)
        {
            ReflectionProbe p = probes[bestI];
            diffuseEnvIndex = p.envIndices.x;
            specularEnvIndex = p.envIndices.y;
            specDir = BoxProjectReflection(worldPos, specDir, p.centerBlend.xyz, p.extents.xyz);
            probeWeight = bestW;
        }
    }
#endif

    if (g_ReflectionProbeParams.z == 42u) {
        return float4(probeWeight.xxx, 1.0f);
    }

    const bool localProbeRadianceEnabled =
        (g_LocalProbeParams.z > 0.5f) &&
        (probeWeight > 0.0f);
    const bool localProbeTextureRadianceAllowed =
        localProbeRadianceEnabled &&
        !authoredInteriorNoEnvironment &&
        (diffuseEnvIndex != INVALID_BINDLESS_INDEX || specularEnvIndex != INVALID_BINDLESS_INDEX);
    const float localProbeDiffuseScale = localProbeRadianceEnabled ? max(g_LocalProbeParams.x, 0.0f) : 0.0f;
    const float localProbeSpecularScale = localProbeRadianceEnabled ? max(g_LocalProbeParams.y, 0.0f) : 0.0f;

    // Diffuse IBL (irradiance). Global IBL remains gated by the scene
    // environment, but local probes can contribute low-frequency room
    // radiance for enclosed scenes without making an external HDRI visible.
    if ((iblEnabled && g_EnvParams.x > 0.0f) || localProbeDiffuseScale > 0.0f) {
        // Match the forward path: the active diffuse environment is sampled at
        // its highest mip so raw equirectangular room detail does not project
        // sharply onto broad matte receivers.
        float3 irradianceGlobal = iblEnabled
            ? SampleEnvDiffuse(normal, INVALID_BINDLESS_INDEX, diffuseMip)
            : 0.0f.xxx;
        float3 irradianceLocal = localProbeTextureRadianceAllowed && diffuseEnvIndex != INVALID_BINDLESS_INDEX
            ? SampleEnvDiffuse(normal, diffuseEnvIndex, diffuseMip)
            : ComputeSceneLocalProbeDiffuse(normal, surfaceClass, sceneMaterialClass);
        diffuseIBL = irradianceGlobal * albedoColor * kD_ibl;
        diffuseIBL += irradianceLocal * albedoColor * kD_ibl * localProbeDiffuseScale * probeWeight;
    }

    // Specular IBL (split-sum)
    if ((iblEnabled && g_EnvParams.y > 0.0f) || localProbeSpecularScale > 0.0f) {
        // Match authored background presentation in glossy IBL as well:
        // blurred HDRI presentation should not remain mip-0 sharp in
        // reflections where it produces view-dependent shimmer.
        float reflectionSafeMipFloor = saturate(g_AmbientColor.w) * specMaxMip;
        float globalFootprintMip = EnvReflectionFootprintMipFromDirection(specDirGlobal, (float)specWidth, (float)specHeight, specMaxMip);
        float localFootprintMip = EnvReflectionFootprintMipFromDirection(specDir, (float)specWidth, (float)specHeight, specMaxMip);
        float reflectionFootprintMip = max(globalFootprintMip, localFootprintMip);
        float iblMipRoughness = SurfaceIblMipRoughness(roughness, surfaceClass, metallic);
        float mipLevel = max(max(iblMipRoughness * specMaxMip, reflectionSafeMipFloor), reflectionFootprintMip);
        specularMipLevelForDebug = mipLevel;
        float3 specGlobal = iblEnabled
            ? SampleEnvSpecular(specDirGlobal, mipLevel, INVALID_BINDLESS_INDEX)
            : 0.0f.xxx;
        float3 specLocal = localProbeTextureRadianceAllowed && specularEnvIndex != INVALID_BINDLESS_INDEX
            ? SampleEnvSpecular(specDir, mipLevel, specularEnvIndex)
            : ComputeSceneLocalProbeSpecular(specDir, surfaceClass, sceneMaterialClass, roughness);
        float3 prefilteredColor =
            specGlobal * max(g_EnvParams.y, 0.0f) +
            specLocal * localProbeSpecularScale * probeWeight;
        float3 iblSpecWeight = Fibl;
        if (surfaceClass != SURFACE_CLASS_GLASS &&
            surfaceClass != SURFACE_CLASS_WATER &&
            surfaceClass != SURFACE_CLASS_MIRROR &&
            metallic < 0.25f) {
            const float fresnelMax = max(max(Fibl.r, Fibl.g), Fibl.b);
            const float reflectionCeiling =
                SurfaceReflectionCeiling(surfaceClass, roughness, metallic, saturate(materialExt1.a), fresnelMax);
            iblSpecWeight = min(iblSpecWeight, reflectionCeiling.xxx);
        }
        specularIBL = prefilteredColor * iblSpecWeight;
        specularIBL *= RoughSpecularEnergyCompensation(F0, roughness);

        if (clearCoatWeight > 0.01f) {
            float coatBlend = clearCoatWeight * 0.8f;
            float coatMip = max(max(clearCoatRoughness * specMaxMip, reflectionSafeMipFloor), reflectionFootprintMip);
            float3 coatGlobal = iblEnabled
                ? SampleEnvSpecular(specDirGlobal, coatMip, INVALID_BINDLESS_INDEX)
                : 0.0f.xxx;
            float3 coatLocal = localProbeTextureRadianceAllowed && specularEnvIndex != INVALID_BINDLESS_INDEX
                ? SampleEnvSpecular(specDir, coatMip, specularEnvIndex)
                : ComputeSceneLocalProbeSpecular(specDir, surfaceClass, sceneMaterialClass, clearCoatRoughness);
            float3 coatPref =
                coatGlobal * max(g_EnvParams.y, 0.0f) +
                coatLocal * localProbeSpecularScale * probeWeight;
            float3 coatF0 = float3(0.04f, 0.04f, 0.04f);
            float3 coatSpecWeight = FresnelSchlickRoughness(NdotV, coatF0, clearCoatRoughness);
            if (surfaceClass != SURFACE_CLASS_GLASS &&
                surfaceClass != SURFACE_CLASS_WATER &&
                surfaceClass != SURFACE_CLASS_MIRROR &&
                metallic < 0.25f) {
                const float coatFresnelMax = max(max(coatSpecWeight.r, coatSpecWeight.g), coatSpecWeight.b);
                const float coatCeiling =
                    SurfaceReflectionCeiling(surfaceClass, max(clearCoatRoughness, roughness), metallic, saturate(materialExt1.a), coatFresnelMax);
                coatSpecWeight = min(coatSpecWeight, coatCeiling.xxx);
            }
            float3 coatIBL = coatPref * coatSpecWeight;
            specularIBL = lerp(specularIBL, coatIBL, coatBlend);
        }
    }

    // Apply ambient occlusion to indirect lighting only (not direct sun).
    float aoDiffuse = ao;
    float aoSpec = SpecularOcclusion(NdotV, ao, roughness);
    ambient *= aoDiffuse;
    ambient += diffuseIBL * (iblEnabled ? g_EnvParams.x : 1.0f) * aoDiffuse;
    ambient += specularIBL * aoSpec;
    if (iblEnabled &&
        surfaceClass != SURFACE_CLASS_GLASS &&
        surfaceClass != SURFACE_CLASS_WATER &&
        surfaceClass != SURFACE_CLASS_MIRROR &&
        metallic < 0.25f) {
        // VB deferred now matches the forward local-light contract, but broad
        // rough room-shell receivers still need the renderer's scene-local
        // ambient contract to keep visible HDRIs from becoming the only
        // indirect source. This is deliberately low-frequency and material
        // gated; it preserves IBL/reflection detail while preventing rough
        // white platforms and walls from falling into dark HDRI bands during
        // camera motion.
        float roughReceiver = smoothstep(0.42f, 0.88f, roughness);
        float3 localFillColor = max(g_AmbientColor.rgb, 0.12f.xxx);
        ambient += localFillColor * albedoColor * kD_ibl * aoDiffuse * (0.38f * roughReceiver);
    }
    ambient = ApplySceneMaterialCinematicIndirectShaping(
        ambient,
        albedoColor,
        normal,
        sceneMaterialClass,
        surfaceClass,
        roughness,
        metallic,
        ao,
        NdotV);
    if (sheenWeight > 0.01f) {
        float grazing = pow(saturate(1.0f - NdotV), 4.0f);
        ambient += albedoColor * sheenWeight * grazing * 0.08f;
    }

    if (g_ReflectionProbeParams.z == 44u) {
        return float4(saturate(directLight), 1.0f);
    }
    if (g_ReflectionProbeParams.z == 54u) {
        return float4(saturate(directLightUnshadowed), 1.0f);
    }
    if (g_ReflectionProbeParams.z == 55u) {
        return float4(saturate((directLightUnshadowed - directLight) * 2.0f), 1.0f);
    }
    if (g_ReflectionProbeParams.z == 45u) {
        return float4(saturate(ambient), 1.0f);
    }

    if (g_ReflectionProbeParams.z == 8u) {
        return float4(saturate(diffuseIBL * g_EnvParams.x * aoDiffuse), 1.0f);
    }
    if (g_ReflectionProbeParams.z == 9u) {
        return float4(saturate(specularIBL * g_EnvParams.y * aoSpec), 1.0f);
    }
    if (g_ReflectionProbeParams.z == 11u) {
        return float4(saturate(Fibl), 1.0f);
    }
    if (g_ReflectionProbeParams.z == 12u) {
        float mipVis = (specMaxMip > 0.0f) ? saturate(specularMipLevelForDebug / specMaxMip) : roughness;
        return float4(mipVis.xxx, 1.0f);
    }
    if (g_ReflectionProbeParams.z == 41u) {
        return float4(SurfaceClassDebugColor(surfaceClass), 1.0f);
    }
    if (g_ReflectionProbeParams.z == 47u) {
        return float4(SceneMaterialPolicyDebugColor(sceneMaterialClass, surfaceClass, roughness, metallic), 1.0f);
    }

    // Final color
    float3 color = directLight + ambient + emissive;

    return float4(color, 1.0);
}
