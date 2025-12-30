// DeferredLighting.hlsl
// Phase 2.2: Deferred lighting pass for visibility buffer
// Reads G-buffers and applies PBR lighting

#include "PBR_Lighting.hlsli"

// Temporary debug views (compile-time).
// 0 = off, 1 = normals, 2 = NdotL, 3 = clustered occupancy
#define DEFERRED_DEBUG_VIEW 0

// G-buffer inputs
Texture2D<float4> g_GBufferAlbedo : register(t0);           // RGB = albedo (linear), A = AO
Texture2D<float4> g_GBufferNormalRoughness : register(t1);  // RGB = encoded normal (0..1), A = roughness
Texture2D<float4> g_GBufferEmissiveMetallic : register(t2); // RGB = emissive, A = metallic
Texture2D<float> g_DepthBuffer : register(t3);              // Depth for position reconstruction
Texture2D<float4> g_GBufferMaterialExt0 : register(t4);      // RGBA16F: x=clearcoat, y=coatRough, z=IOR, w=specFactor
Texture2D<float4> g_GBufferMaterialExt1 : register(t5);      // RGBA16F: rgb=specColor, a=transmission (unused in deferred)

// Environment/shadow maps (matching forward renderer)
Texture2D<float4> g_EnvDiffuse : register(t6);  // lat-long (equirect) irradiance
Texture2D<float4> g_EnvSpecular : register(t7); // lat-long (equirect) prefiltered specular
Texture2DArray<float> g_ShadowMap : register(t8);
Texture2D<float2> g_BRDFLUT : register(t9);

struct Light {
    float4 position_type;
    float4 direction_cosInner;
    float4 color_range;
    float4 params;
};

StructuredBuffer<Light> g_LocalLights : register(t10);
StructuredBuffer<uint2> g_ClusterRanges : register(t11);
StructuredBuffer<uint> g_ClusterLightIndices : register(t12);

SamplerState g_LinearSampler : register(s0);
SamplerState g_ShadowSampler : register(s1);

cbuffer PerFrameData : register(b0) {
    float4x4 g_InvViewProj;                // Inverse view-projection for position reconstruction
    float4x4 g_ViewMatrix;
    float4x4 g_LightViewProjection[6];     // 0..2 cascades, 3..5 local shadowed lights

    float4 g_CameraPosition;               // xyz = camera position (world)
    float4 g_SunDirection;                 // xyz = direction-to-light (world)
    float4 g_SunRadiance;                  // rgb = color * intensity

    float4 g_CascadeSplits;                // x,y,z = split depths in view space, w = far plane
    float4 g_ShadowParams;                 // x=bias, y=pcfRadius(texels), z=enabled, w=pcssEnabled
    float4 g_EnvParams;                    // x=diffuse IBL, y=specular IBL, z=IBL enabled, w unused
    float4 g_ShadowInvSizeAndSpecMaxMip;   // xy = 1/shadowMapDim, z = specular max mip, w unused
    float4 g_ProjectionParams;             // x=proj11, y=proj22, z=nearZ, w=farZ
    uint4  g_ScreenAndCluster;             // x=width, y=height, z=clusterCountX, w=clusterCountY
    uint4  g_ClusterParams;                // x=clusterCountZ, y=maxLightsPerCluster, z=localLightCount, w unused
    uint4  g_ReflectionProbeParams;        // x=probeTableSRVIndex, y=probeCount, z/w unused
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

float3 SampleEnvDiffuse(float3 dir, uint diffuseIndex)
{
    float2 uv = DirectionToLatLong(dir);
#ifdef ENABLE_BINDLESS
    if (diffuseIndex != INVALID_BINDLESS_INDEX) {
        Texture2D<float4> tex = ResourceDescriptorHeap[diffuseIndex];
        return tex.SampleLevel(g_LinearSampler, uv, 0.0f).rgb;
    }
#endif
    return g_EnvDiffuse.SampleLevel(g_LinearSampler, uv, 0.0f).rgb;
}

float3 SampleEnvSpecular(float3 dir, float mipLevel, uint specularIndex)
{
    float2 uv = DirectionToLatLong(dir);
#ifdef ENABLE_BINDLESS
    if (specularIndex != INVALID_BINDLESS_INDEX) {
        Texture2D<float4> tex = ResourceDescriptorHeap[specularIndex];
        return tex.SampleLevel(g_LinearSampler, uv, mipLevel).rgb;
    }
#endif
    return g_EnvSpecular.SampleLevel(g_LinearSampler, uv, mipLevel).rgb;
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

float SamplePCF(float2 shadowUV, float currentDepth, float bias, float pcfRadius, uint cascadeIndex) {
    float2 texelSize = g_ShadowInvSizeAndSpecMaxMip.xy;

    float shadow = 0.0f;
    int samples = 0;

    [unroll]
    for (int x = -1; x <= 1; ++x) {
        [unroll]
        for (int y = -1; y <= 1; ++y) {
            float2 offset = float2(x, y) * texelSize * pcfRadius;
            float depthSample = g_ShadowMap.Sample(g_ShadowSampler, float3(shadowUV + offset, cascadeIndex)).r;
            shadow += (currentDepth - bias > depthSample) ? 0.0f : 1.0f;
            samples++;
        }
    }

    return shadow / max(samples, 1);
}

float ComputeShadowCascade(float3 worldPos, float3 normal, uint cascadeIndex) {
    cascadeIndex = min(cascadeIndex, 2u);

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

    float cascadeScale = 1.0f + (float)cascadeIndex * 0.5f;
    bias *= cascadeScale;

    float3 lightDirWS = normalize(g_SunDirection.xyz);
    float ndotl = saturate(dot(normal, lightDirWS));
    bias *= lerp(1.5f, 0.5f, ndotl);

    if (g_ShadowParams.w > 0.5f) {
        float2 texelSize = g_ShadowInvSizeAndSpecMaxMip.xy;
        float searchRadius = pcfRadius * 2.5f;

        float avgBlocker = 0.0f;
        int blockerCount = 0;

        [unroll]
        for (int x = -1; x <= 1; ++x) {
            [unroll]
            for (int y = -1; y <= 1; ++y) {
                float2 offset = float2(x, y) * texelSize * searchRadius;
                float depthSample = g_ShadowMap.Sample(g_ShadowSampler, float3(shadowUV + offset, cascadeIndex)).r;
                if (depthSample + bias < currentDepth) {
                    avgBlocker += depthSample;
                    blockerCount++;
                }
            }
        }

        if (blockerCount > 0) {
            avgBlocker /= blockerCount;
            float penumbra = saturate((currentDepth - avgBlocker) / max(avgBlocker, 1e-4f));
            pcfRadius *= (1.0f + penumbra * 4.0f);
        }
    }

    return SamplePCF(shadowUV, currentDepth, bias, pcfRadius, cascadeIndex);
}

float ComputeShadow(float3 worldPos, float3 normal) {
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

    float shadowPrimary = ComputeShadowCascade(worldPos, normal, primary);
    if (blend <= 0.001f || primary == secondary) {
        return shadowPrimary;
    }

    float shadowSecondary = ComputeShadowCascade(worldPos, normal, secondary);
    return lerp(shadowPrimary, shadowSecondary, blend);
}

// Local light shadow evaluation for a shadow-mapped spotlight. Matches the
// CPU convention used by Basic.hlsl: light.params.y holds the shadow-map
// slice index in the shared shadow-map array (3..5 for local lights).
float ComputeLocalLightShadow(float3 worldPos, float3 normal, float3 lightDir, float shadowIndex)
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
    float pcfRadius = g_ShadowParams.y * 0.75f;

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

// Deferred lighting pixel shader
float4 PSMain(VSOutput input) : SV_Target0 {
    uint2 pixelCoord = uint2(input.position.xy);

    // Sample G-buffers
    float4 albedo = g_GBufferAlbedo.Load(int3(pixelCoord, 0));
    float4 normalRoughness = g_GBufferNormalRoughness.Load(int3(pixelCoord, 0));
    float4 emissiveMetallic = g_GBufferEmissiveMetallic.Load(int3(pixelCoord, 0));
    float4 materialExt0 = g_GBufferMaterialExt0.Load(int3(pixelCoord, 0));
    float4 materialExt1 = g_GBufferMaterialExt1.Load(int3(pixelCoord, 0));
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

    // Check for background pixels (depth = 1.0)
    if (depth >= 0.9999) {
        float3 worldFar = ReconstructWorldPosition(input.texCoord, 1.0f);
        float3 viewDir = normalize(worldFar - g_CameraPosition.xyz);

        bool iblEnabled = (g_EnvParams.z > 0.5f);
        if (iblEnabled) {
            // Use IBL environment map for sky
            float3 sky = SampleEnvSpecular(viewDir, 0.0f, INVALID_BINDLESS_INDEX) * g_EnvParams.y;
            return float4(sky, 1.0f);
        } else {
            // Procedural sky gradient when IBL is disabled (outdoor/terrain mode)
            float3 sunDir = normalize(g_SunDirection.xyz);
            float sunAltitude = sunDir.y;

            // View direction Y component: 1=looking up, -1=looking down
            float viewY = viewDir.y;

            // Sky gradient: blue zenith, warm horizon
            float3 skyZenith = float3(0.3f, 0.5f, 0.9f);
            float3 skyHorizon = float3(0.7f, 0.75f, 0.85f);

            // Sunset/sunrise warm colors when sun is low
            float sunsetFactor = saturate(1.0f - abs(sunAltitude) * 3.0f);
            float3 sunsetColor = float3(1.0f, 0.5f, 0.2f);
            skyHorizon = lerp(skyHorizon, sunsetColor, sunsetFactor * 0.5f);

            // Blend based on view elevation
            float horizonBlend = saturate(1.0f - abs(viewY));
            float zenithBlend = saturate(viewY);
            float3 skyColor = lerp(skyHorizon, skyZenith, zenithBlend);

            // Add sun glow
            float sunDot = saturate(dot(viewDir, sunDir));
            float sunGlow = pow(sunDot, 64.0f) * 2.0f; // Tight sun disk
            float sunHalo = pow(sunDot, 8.0f) * 0.3f;  // Soft halo
            float3 sunColor = g_SunRadiance.rgb / max(dot(g_SunRadiance.rgb, float3(0.2126f, 0.7152f, 0.0722f)), 0.01f);
            skyColor += sunColor * (sunGlow + sunHalo);

            // Ground horizon fade (below horizon goes darker)
            float groundFade = saturate(-viewY * 2.0f);
            float3 groundColor = float3(0.3f, 0.25f, 0.2f);
            skyColor = lerp(skyColor, groundColor, groundFade);

            // Apply sun intensity scaling
            float sunLuminance = dot(g_SunRadiance.rgb, float3(0.2126f, 0.7152f, 0.0722f));
            skyColor *= min(sunLuminance * 0.15f + 0.3f, 1.2f);

            return float4(skyColor, 1.0f);
        }
    }

    // Reconstruct world position from depth
    float3 worldPos = ReconstructWorldPosition(input.texCoord, depth);

    // View direction
    float3 V = normalize(g_CameraPosition.xyz - worldPos);
    float NdotV = max(dot(normal, V), 0.0);

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
    float NDF = DistributionGGX(normal, H, roughness);
    float G = GeometrySmith(normal, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 numerator = NDF * G * F;
    float denominator = 4.0 * NdotV * NdotL;
    float3 specular = numerator / max(denominator, 0.001);

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

    // Shadow
    float shadow = ComputeShadow(worldPos, normal);

    // Direct lighting (sun)
    float3 directLight = (kD * albedoColor / PI + specular) * g_SunRadiance.rgb * NdotL * shadow;

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

        [loop]
        for (uint i = 0; i < count; ++i) {
            uint lightIndex = g_ClusterLightIndices[base + i];
            if (lightIndex >= g_ClusterParams.z) {
                continue;
            }

            Light light = g_LocalLights[lightIndex];
            float type = light.position_type.w;
            if (type < 0.5f) {
                continue;
            }

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
            float invDist2 = 1.0f / max(dist * dist, 1e-3f);

            // Spot cone attenuation (approx).
            if (type > 1.5f && type < 2.5f) {
                float3 spotDir = normalize(light.direction_cosInner.xyz);
                float cosInner = light.direction_cosInner.w;
                float cosOuter = light.params.x;
                float cosTheta = dot(spotDir, normalize(worldPos - lightPos));
                float spot = saturate((cosTheta - cosOuter) / max(cosInner - cosOuter, 1e-3f));
                spot = spot * spot;
                att *= spot;
            }

            float3 radiance = light.color_range.rgb * (att * invDist2);

            float3 Hl = normalize(V + Ll);
            float NDF_l = DistributionGGX(normal, Hl, roughness);
            float G_l = GeometrySmith(normal, V, Ll, roughness);
            float3 F_l = FresnelSchlick(max(dot(Hl, V), 0.0f), F0);

            float3 numerator_l = NDF_l * G_l * F_l;
            float denom_l = 4.0f * NdotV * NdotLl;
            float3 spec_l = numerator_l / max(denom_l, 0.001f);

            if (clearCoatWeight > 0.01f) {
                float coatBlend = clearCoatWeight * 0.8f;
                float3 F_coat = FresnelSchlick(max(dot(Hl, V), 0.0f), float3(0.04f, 0.04f, 0.04f));
                float  D_coat = DistributionGGX(normal, Hl, clearCoatRoughness);
                float  G_coat = GeometrySmith(normal, V, Ll, clearCoatRoughness);
                float3 specCoat = (D_coat * G_coat * F_coat) / max(denom_l, 0.001f);
                spec_l = lerp(spec_l, specCoat, coatBlend);
            }

            float3 kS_l = F_l;
            float3 kD_l = (1.0f - kS_l) * (1.0f - metallic);

            float shadowLocal = 1.0f;
            if (type == 2.0f && light.params.y >= 0.0f) {
                shadowLocal = ComputeLocalLightShadow(worldPos, normal, Ll, light.params.y);
            }
            directLight += (kD_l * albedoColor / PI + spec_l) * radiance * NdotLl * shadowLocal;
        }
    }

    // --- Image-Based Lighting (IBL) ---
    float3 ambient = float3(0.0f, 0.0f, 0.0f);
    const bool iblEnabled = (g_EnvParams.z > 0.5f);

    float3 diffuseIBL = 0.0f;
    float3 specularIBL = 0.0f;
    float3 Fibl = FresnelSchlickRoughness(NdotV, F0, roughness);
    float3 kD_ibl = (1.0 - metallic) * (1.0 - Fibl);

    // Hemisphere ambient fallback when IBL is disabled (outdoor/terrain lighting).
    // Uses sky-ground gradient based on normal direction for natural ambient.
    if (!iblEnabled) {
        // Derive sky color from sun radiance (approximate atmospheric scattering)
        float3 sunColor = g_SunRadiance.rgb;
        float sunLuminance = dot(sunColor, float3(0.2126f, 0.7152f, 0.0722f));
        float sunAltitude = g_SunDirection.y; // How high the sun is (0=horizon, 1=zenith)

        // Sky color: blue at zenith, warm at horizon/sunset
        float3 skyZenith = float3(0.2f, 0.4f, 0.8f) * saturate(sunAltitude + 0.3f);
        float3 skyHorizon = float3(0.6f, 0.5f, 0.4f);
        float3 skyColor = lerp(skyHorizon, skyZenith, saturate(sunAltitude)) * min(sunLuminance * 0.3f, 1.5f);

        // Ground color: darker, desaturated bounce light
        float3 groundColor = float3(0.15f, 0.12f, 0.08f) * min(sunLuminance * 0.15f, 0.5f);

        // Hemisphere blend based on world-space normal Y component
        float hemiBlend = normal.y * 0.5f + 0.5f; // Remap -1..1 to 0..1
        float3 hemisphereAmbient = lerp(groundColor, skyColor, hemiBlend);

        // Apply to diffuse (metallic surfaces get less diffuse ambient)
        ambient = hemisphereAmbient * albedoColor * kD_ibl;

        // Add rough specular approximation for non-IBL (sky reflection on shiny surfaces)
        float3 skyReflect = lerp(groundColor, skyColor, saturate(reflect(-V, normal).y * 0.5f + 0.5f));
        float roughFade = 1.0f - roughness; // Smooth surfaces get more sky reflection
        ambient += skyReflect * Fibl * roughFade * 0.3f;
    }

    uint diffuseEnvIndex = INVALID_BINDLESS_INDEX;
    uint specularEnvIndex = INVALID_BINDLESS_INDEX;
    float3 specDir = reflect(-V, normal);
    float3 specDirGlobal = specDir;
    float probeWeight = 0.0f;

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

    if (g_ReflectionProbeParams.z == 1u) {
        return float4(probeWeight.xxx, 1.0f);
    }

    // Diffuse IBL (irradiance)
    if (iblEnabled && g_EnvParams.x > 0.0f) {
        float3 irradianceGlobal = SampleEnvDiffuse(normal, INVALID_BINDLESS_INDEX);
        float3 irradianceLocal = SampleEnvDiffuse(normal, diffuseEnvIndex);
        float3 irradiance = lerp(irradianceGlobal, irradianceLocal, probeWeight);
        diffuseIBL = irradiance * albedoColor * kD_ibl;
    }

    // Specular IBL (split-sum)
    if (iblEnabled && g_EnvParams.y > 0.0f) {
        float maxMip = g_ShadowInvSizeAndSpecMaxMip.z;
        float mipLevel = roughness * maxMip;
        float3 specGlobal = SampleEnvSpecular(specDirGlobal, mipLevel, INVALID_BINDLESS_INDEX);
        float3 specLocal = SampleEnvSpecular(specDir, mipLevel, specularEnvIndex);
        float3 prefilteredColor = lerp(specGlobal, specLocal, probeWeight);
        float2 brdf = g_BRDFLUT.SampleLevel(g_LinearSampler, float2(saturate(NdotV), saturate(roughness)), 0.0f);
        specularIBL = prefilteredColor * (F0 * brdf.x + brdf.y);

        if (clearCoatWeight > 0.01f) {
            float coatBlend = clearCoatWeight * 0.8f;
            float coatMip = clearCoatRoughness * maxMip;
            float3 coatGlobal = SampleEnvSpecular(specDirGlobal, coatMip, INVALID_BINDLESS_INDEX);
            float3 coatLocal = SampleEnvSpecular(specDir, coatMip, specularEnvIndex);
            float3 coatPref = lerp(coatGlobal, coatLocal, probeWeight);
            float2 coatBrdf = g_BRDFLUT.SampleLevel(g_LinearSampler, float2(saturate(NdotV), saturate(clearCoatRoughness)), 0.0f);
            float3 coatF0 = float3(0.04f, 0.04f, 0.04f);
            float3 coatIBL = coatPref * (coatF0 * coatBrdf.x + coatBrdf.y);
            specularIBL = lerp(specularIBL, coatIBL, coatBlend);
        }
    }

    // Apply ambient occlusion to indirect lighting only (not direct sun).
    float aoDiffuse = ao;
    float aoSpec = lerp(ao, 1.0f, roughness);
    ambient *= aoDiffuse;
    ambient += diffuseIBL * g_EnvParams.x * aoDiffuse;
    ambient += specularIBL * g_EnvParams.y * aoSpec;

    // Final color
    float3 color = directLight + ambient + emissive;

    return float4(color, 1.0);
}
