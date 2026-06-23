// Scene-local reflection radiance resolve.
//
// This is the render-graph-ready version of the V2 post candidate source:
// it resolves a stable, scene-owned reflection radiance buffer from depth,
// material classes, local lighting, and authorized environment/specular input.

#include "SurfaceClassification.hlsli"

cbuffer FrameConstants : register(b1)
{
    float4x4 g_ViewMatrix;
    float4x4 g_ProjectionMatrix;
    float4x4 g_ViewProjectionMatrix;
    float4x4 g_InvProjectionMatrix;
    float4   g_CameraPosition;
    float4   g_TimeAndExposure;
    float4   g_AmbientColor;
    uint4    g_LightCount;
    struct Light
    {
        float4 position_type;
        float4 direction_cosInner;
        float4 color_range;
        float4 params;
    };
    static const uint LIGHT_MAX = 16;
    Light    g_Lights[LIGHT_MAX];
    float4x4 g_LightViewProjection[6];
    float4   g_CascadeSplits;
    float4   g_ShadowParams;
    float4   g_DebugMode;
    float4   g_PostParams;
    float4   g_EnvParams;
    float4   g_ColorGrade;
    float4   g_FogParams;
    float4   g_FogExtraParams;
    float4   g_AOParams;
    float4   g_BloomParams;
    float4   g_TAAParams;
    float4x4 g_ViewProjectionNoJitter;
    float4x4 g_InvViewProjectionNoJitter;
    float4x4 g_PrevViewProjMatrix;
    float4x4 g_InvViewProjMatrix;
    float4   g_WaterParams0;
    float4   g_WaterParams1;
    float4   g_SSRParams;
    float4   g_PostGradeParams;
    float4   g_RTReflectionParams;
    uint4    g_ScreenAndCluster;
    uint4    g_ClusterParams;
    uint4    g_ClusterSRVIndices;
    float4   g_ProjectionParams;
    float4   g_CinematicParams;
    float4   g_CinematicDofParams;
    float4   g_CinematicStabilityParams;
    float4   g_CinematicLookParams;
    float4   g_CinematicExposureParams;
    // x = scene-local probe diffuse scale, y = scene-local probe specular scale,
    // z = scene-local probe radiance enabled (>0.5), w = reserved
    float4   g_LocalProbeParams;
    // xyz = active local reflection probe center, w = valid flag.
    float4   g_LocalProbeCenter;
    // xyz = active local reflection probe half extents, w = blend distance.
    float4   g_LocalProbeExtents;
};

Texture2D<float>  g_Depth            : register(t0);
Texture2D<float4> g_NormalRoughness  : register(t1);
Texture2D<float4> g_EmissiveMetallic : register(t2);
Texture2D<float4> g_MaterialExt1     : register(t3);
Texture2D<float4> g_MaterialExt2     : register(t4);
Texture2D<float4> g_SceneColor       : register(t5);
Texture2D<float4> g_EnvSpecular      : register(t6);
TextureCube<float4> g_LocalReflectionCubemap : register(t7);
RWTexture2D<float4> g_OutputRadiance : register(u0);
SamplerState g_Sampler               : register(s0);

float3 ReconstructWorldPosition(float2 uv, float depth)
{
    float x = uv.x * 2.0f - 1.0f;
    float y = 1.0f - 2.0f * uv.y;
    depth = min(saturate(depth), 1.0f - 1e-4f);
    float4 clip = float4(x, y, depth, 1.0f);
    float4 world = mul(g_InvViewProjMatrix, clip);
    if (!all(isfinite(world)) || abs(world.w) <= 1e-4f) {
        return 0.0f.xxx;
    }
    return world.xyz / world.w;
}

float ReflectionLuma(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 SoftLimitReflectionLuma(float3 color, float maxLuma)
{
    color = max(color, 0.0f.xxx);
    float luma = max(ReflectionLuma(color), 0.0f);
    if (luma <= maxLuma) {
        return color;
    }
    float compressed = maxLuma + log2(max(luma - maxLuma + 1.0f, 1.0f));
    return color * (compressed / max(luma, 1e-4f));
}

float2 DirectionToLatLong(float3 dir)
{
    dir = normalize(dir);
    if (!all(isfinite(dir))) {
        dir = float3(0.0f, 0.0f, 1.0f);
    }

    float phi = atan2(-dir.z, dir.x);
    float theta = asin(clamp(dir.y, -1.0f, 1.0f));
    return float2(0.5f + phi / 6.28318530718f, 0.5f - theta / 3.14159265359f);
}

float3 RotateEnvironmentDirection(float3 dir)
{
    float s, c;
    sincos(g_CinematicParams.y, s, c);
    return normalize(float3(c * dir.x + s * dir.z, dir.y, -s * dir.x + c * dir.z));
}

float SafeDirectionComponent(float v)
{
    return abs(v) < 1e-4f ? (v < 0.0f ? -1e-4f : 1e-4f) : v;
}

float3 BoxProjectReflectionDirection(float3 worldPos, float3 reflectionDir)
{
    if (g_LocalProbeCenter.w <= 0.5f ||
        g_LocalProbeExtents.x <= 0.0f ||
        g_LocalProbeExtents.y <= 0.0f ||
        g_LocalProbeExtents.z <= 0.0f) {
        return normalize(reflectionDir);
    }

    float3 dir = normalize(reflectionDir);
    dir.x = SafeDirectionComponent(dir.x);
    dir.y = SafeDirectionComponent(dir.y);
    dir.z = SafeDirectionComponent(dir.z);

    float3 boxMin = g_LocalProbeCenter.xyz - g_LocalProbeExtents.xyz;
    float3 boxMax = g_LocalProbeCenter.xyz + g_LocalProbeExtents.xyz;
    float3 tMax = (boxMax - worldPos) / dir;
    float3 tMin = (boxMin - worldPos) / dir;
    float3 tHit = float3(
        dir.x >= 0.0f ? tMax.x : tMin.x,
        dir.y >= 0.0f ? tMax.y : tMin.y,
        dir.z >= 0.0f ? tMax.z : tMin.z);
    float travel = min(tHit.x, min(tHit.y, tHit.z));
    if (!isfinite(travel) || travel <= 1e-4f) {
        return normalize(reflectionDir);
    }

    float3 localHit = worldPos + dir * travel;
    return normalize(localHit - g_LocalProbeCenter.xyz);
}

float3 NormalizedKeyLightColor()
{
    float3 lightColor = (g_LightCount.x > 0u) ? g_Lights[0].color_range.rgb : 1.0f.xxx;
    float lightLuma = max(ReflectionLuma(lightColor), 0.01f);
    return lightColor / lightLuma;
}

float3 KeyLightDirection()
{
    if (g_LightCount.x == 0u) {
        return normalize(float3(0.35f, 0.85f, 0.25f));
    }
    return normalize(-g_Lights[0].direction_cosInner.xyz);
}

float StableIblMipRoughness(float roughness,
                            uint surfaceClass,
                            uint sceneMaterialClass,
                            float metallic)
{
    if (surfaceClass == SURFACE_CLASS_MIRROR || sceneMaterialClass == SCENE_MATERIAL_MIRROR) {
        return roughness;
    }
    if (surfaceClass == SURFACE_CLASS_GLASS ||
        surfaceClass == SURFACE_CLASS_WATER ||
        sceneMaterialClass == SCENE_MATERIAL_GLASS_PANE ||
        sceneMaterialClass == SCENE_MATERIAL_WATER) {
        return max(roughness, 0.06f);
    }
    if (surfaceClass == SURFACE_CLASS_BRUSHED_METAL ||
        sceneMaterialClass == SCENE_MATERIAL_BRUSHED_METAL) {
        return max(roughness, 0.28f);
    }
    if (metallic > 0.85f) {
        return max(roughness, 0.24f);
    }
    return roughness;
}

bool SampleCapturedLocalReflectionCubemap(float3 worldPos,
                                          float3 reflectionDir,
                                          float roughness,
                                          uint surfaceClass,
                                          uint sceneMaterialClass,
                                          float metallic,
                                          out float3 cubeRadiance)
{
    cubeRadiance = 0.0f.xxx;
    if (g_LocalProbeParams.z <= 0.5f || g_LocalProbeCenter.w <= 0.5f) {
        return false;
    }

    uint cubeWidth = 0u;
    uint cubeHeight = 0u;
    uint cubeMipCount = 0u;
    g_LocalReflectionCubemap.GetDimensions(0, cubeWidth, cubeHeight, cubeMipCount);
    if (cubeWidth == 0u || cubeHeight == 0u || cubeMipCount == 0u) {
        return false;
    }

    float maxMip = max((cubeMipCount > 0u) ? (float)(cubeMipCount - 1u) : 0.0f, 0.0f);
    float mip = StableIblMipRoughness(roughness, surfaceClass, sceneMaterialClass, metallic) * maxMip;
    float3 cubeDir = BoxProjectReflectionDirection(worldPos, reflectionDir);
    float3 sampled = g_LocalReflectionCubemap.SampleLevel(g_Sampler, cubeDir, mip).rgb;
    if (!all(isfinite(sampled)) || ReflectionLuma(sampled) <= 1e-4f) {
        return false;
    }

    cubeRadiance = max(sampled, 0.0f.xxx);
    return true;
}

float3 ComputeLocalStructure(float3 reflectionDir,
                             float3 worldPos,
                             float3 normal,
                             uint surfaceClass,
                             uint sceneMaterialClass,
                             float roughness,
                             float metallic)
{
    reflectionDir = normalize(reflectionDir);
    normal = normalize(normal);

    float3 keyColor = NormalizedKeyLightColor();
    float3 keyDir = KeyLightDirection();
    float3 ambientBase = max(g_AmbientColor.rgb, 0.012f.xxx);
    float gloss = saturate(1.0f - roughness);

    bool glassLike =
        surfaceClass == SURFACE_CLASS_GLASS ||
        surfaceClass == SURFACE_CLASS_MIRROR ||
        sceneMaterialClass == SCENE_MATERIAL_GLASS_PANE ||
        sceneMaterialClass == SCENE_MATERIAL_MIRROR;
    bool waterLike =
        SurfaceIsWater(surfaceClass) ||
        sceneMaterialClass == SCENE_MATERIAL_WATER ||
        sceneMaterialClass == SCENE_MATERIAL_WET_SURFACE;
    bool metalLike =
        surfaceClass == SURFACE_CLASS_BRUSHED_METAL ||
        sceneMaterialClass == SCENE_MATERIAL_BRUSHED_METAL ||
        sceneMaterialClass == SCENE_MATERIAL_POLISHED_METAL ||
        metallic > 0.62f;

    float horizon = 1.0f - abs(reflectionDir.y);
    float ceiling = saturate(reflectionDir.y * 0.5f + 0.5f);
    float floorBounce = saturate(-reflectionDir.y * 0.5f + 0.5f);
    float frontKey = saturate(
        dot(normalize(reflectionDir.xz + 1e-4f.xx), normalize(keyDir.xz + 1e-4f.xx)) * 0.5f + 0.5f);

    float broadWindow = pow(frontKey, lerp(8.0f, 22.0f, gloss));
    float horizonLine = pow(saturate(horizon), lerp(3.0f, 10.0f, gloss));
    float floorLine = pow(floorBounce, 5.0f) * horizonLine;
    float roomStripeA = 0.5f + 0.5f * sin(dot(worldPos.xz, float2(0.115f, 0.073f)) + g_CinematicParams.y * 0.03f);
    float roomStripeB = 0.5f + 0.5f * sin(worldPos.y * 0.170f + worldPos.x * 0.045f);
    float architecturalBreakup = smoothstep(0.18f, 0.92f, lerp(roomStripeA, roomStripeA * roomStripeB, 0.35f));

    float3 upperTint = ambientBase * 0.55f + keyColor * 0.050f;
    float3 lowerTint = ambientBase * 0.42f + float3(0.040f, 0.034f, 0.028f);
    if (glassLike || waterLike) {
        upperTint += float3(0.020f, 0.060f, 0.100f);
    }
    if (metalLike) {
        upperTint = lerp(upperTint, float3(0.090f, 0.100f, 0.115f), 0.35f);
        lowerTint = lerp(lowerTint, float3(0.050f, 0.045f, 0.040f), 0.35f);
    }

    float3 structured = lerp(lowerTint, upperTint, ceiling);
    structured += keyColor * broadWindow * lerp(0.035f, 0.180f, gloss);
    structured += lerp(float3(0.050f, 0.070f, 0.090f), keyColor, 0.35f) *
                  horizonLine *
                  lerp(0.014f, 0.080f, gloss) *
                  lerp(0.65f, 1.25f, architecturalBreakup);
    structured += float3(0.070f, 0.052f, 0.034f) *
                  floorLine *
                  lerp(0.015f, 0.070f, gloss);

    float normalFacing = saturate(abs(dot(normal, reflectionDir)) * 0.35f + 0.65f);
    float materialBoost = glassLike ? 1.16f : (waterLike ? 1.24f : (metalLike ? 1.10f : 1.0f));
    return max(structured * normalFacing * materialBoost, 0.0f.xxx);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint width;
    uint height;
    g_OutputRadiance.GetDimensions(width, height);
    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height) {
        return;
    }

    float2 uv = (float2(dispatchThreadId.xy) + 0.5f) / float2(width, height);
    float depth = g_Depth.SampleLevel(g_Sampler, uv, 0).r;
    if (depth >= 1.0f - 1e-4f) {
        g_OutputRadiance[dispatchThreadId.xy] = 0.0f.xxxx;
        return;
    }

    float3 worldPos = ReconstructWorldPosition(uv, depth);
    int3 loadCoord = int3(dispatchThreadId.xy, 0);
    float4 nr = g_NormalRoughness.Load(loadCoord);
    float3 normal = normalize(nr.xyz * 2.0f - 1.0f);
    float roughness = saturate(nr.w);
    float metallic = saturate(g_EmissiveMetallic.Load(loadCoord).a);
    float transmission = saturate(g_MaterialExt1.Load(loadCoord).a);
    float4 materialExt2 = g_MaterialExt2.Load(loadCoord);
    uint surfaceClass = DecodeSurfaceClass(materialExt2.r);
    uint sceneMaterialClass = DecodeSceneMaterialClass(materialExt2.a);

    float3 viewDir = normalize(g_CameraPosition.xyz - worldPos);
    float nDotV = saturate(dot(normal, viewDir));
    float dielectricFresnel = 0.04f + 0.96f * pow(1.0f - nDotV, 5.0f);
    float roughFresnelDamp = lerp(1.0f, 0.25f, roughness);
    float materialReflectance = lerp(saturate(dielectricFresnel * roughFresnelDamp), 1.0f, metallic);
    float gloss = saturate(1.0f - roughness);
    gloss *= gloss;

    float localProbeSpecularPotential =
        (g_LocalProbeParams.z > 0.5f) ? max(g_LocalProbeParams.y, 0.0f) : 0.0f;
    float iblPotential = saturate(materialReflectance * gloss * max(g_EnvParams.y, 0.0f));
    float localPotential = saturate(materialReflectance * gloss * localProbeSpecularPotential);
    float authorizedPotential = saturate(max(iblPotential, localPotential));
    float ceiling = SurfaceReflectionCeiling(
        surfaceClass,
        roughness,
        metallic,
        transmission,
        dielectricFresnel);

    if (authorizedPotential <= 1e-4f || ceiling <= 1e-4f) {
        g_OutputRadiance[dispatchThreadId.xy] = 0.0f.xxxx;
        return;
    }

    float3 reflectionDir = normalize(reflect(-viewDir, normal));
    float3 localStructure = ComputeLocalStructure(
        reflectionDir,
        worldPos,
        normal,
        surfaceClass,
        sceneMaterialClass,
        roughness,
        metallic);

    float3 capturedCubeRadiance = 0.0f.xxx;
    bool hasCapturedCube = SampleCapturedLocalReflectionCubemap(
        worldPos,
        reflectionDir,
        roughness,
        surfaceClass,
        sceneMaterialClass,
        metallic,
        capturedCubeRadiance);

    float3 source = localStructure;
    if (hasCapturedCube) {
        float cubeWeight = lerp(0.42f, 0.86f, gloss) * saturate(localProbeSpecularPotential * 5.0f);
        source = lerp(localStructure, capturedCubeRadiance, saturate(cubeWeight));
    }
    if (g_EnvParams.z > 0.5f && g_EnvParams.y > 0.001f) {
        uint specWidth = 1u;
        uint specHeight = 1u;
        uint specMipCount = 1u;
        g_EnvSpecular.GetDimensions(0, specWidth, specHeight, specMipCount);
        float specMaxMip = max((specMipCount > 0u) ? (float)(specMipCount - 1u) : 0.0f, 0.0f);
        float mip = StableIblMipRoughness(roughness, surfaceClass, sceneMaterialClass, metallic) * specMaxMip;
        float3 env = g_EnvSpecular.SampleLevel(g_Sampler, DirectionToLatLong(RotateEnvironmentDirection(reflectionDir)), mip).rgb;
        float envBlend = hasCapturedCube ? saturate(iblPotential * 0.25f) : saturate(iblPotential);
        source = lerp(max(source, 0.0f.xxx), env * max(g_EnvParams.y, 0.0f), envBlend);
    }

    float stability = SurfaceReflectionStabilityScale(surfaceClass, roughness, metallic);
    stability *= lerp(1.0f, 0.82f, saturate(g_CinematicStabilityParams.x) * saturate(materialReflectance * gloss));
    float alpha = saturate(authorizedPotential * ceiling * stability);
    float fireflyClamp = min(clamp(g_RTReflectionParams.z, 0.65f, 8.0f),
                             lerp(5.0f, 0.65f, smoothstep(0.25f, 0.85f, roughness)));
    g_OutputRadiance[dispatchThreadId.xy] = float4(SoftLimitReflectionLuma(source, fireflyClamp), alpha);
}
