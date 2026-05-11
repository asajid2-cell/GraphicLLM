// Minimal DXR ray-traced reflections pass.
// This shader library is designed to be used with a dedicated DXR pipeline
// that binds:
//   - TLAS and depth in SRV space2 (t0, t1)
//   - Reflection color UAV in space2 (u0)
//   - Shadow/IBL/RT textures in SRV space1 (t0-t6, matching Basic.hlsl)
//   - Compact RT material buffer in SRV space2 (t3), keyed by TLAS InstanceID()
//   - FrameConstants in space0 (b0), matching ShaderTypes.h / Basic.hlsl.

#include "RaytracingMaterials.hlsli"

RaytracingAccelerationStructure g_TopLevel      : register(t0, space2);
Texture2D<float>               g_Depth          : register(t1, space2);
Texture2D<float4>              g_NormalRoughness: register(t2, space2);
Texture2D<float4>              g_MaterialExt2   : register(t4, space2);
RWTexture2D<float4>            g_ReflectionOut  : register(u0, space2);

// Shared IBL environment (equirectangular) used by the main PBR path.
Texture2D                      g_EnvSpecular    : register(t2, space1);
SamplerState                   g_Sampler        : register(s0);

cbuffer FrameConstants : register(b0, space0)
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
    Light   g_Lights[LIGHT_MAX];
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
    // x = contrast, y = saturation, z = motion blur, w = depth of field
    float4   g_PostGradeParams;
    float4   g_RTReflectionParams;
};

struct ReflectionPayload
{
    float3 color;
    float3 hitNormal;   // Surface normal at hit point for proper reflection calculation
    float3 albedo;
    float3 emissive;
    float  hitDistance; // Ray travel distance for accurate hit point reconstruction
    float  roughness;
    float  metallic;
    uint   surfaceClass;
    float  transmission;
    float  specularFactor;
    float  clearcoat;
    bool   hit;
};

static const float PI = 3.14159265f;
static const uint LIGHT_TYPE_DIRECTIONAL = 0u;
static const uint LIGHT_TYPE_POINT       = 1u;
static const uint LIGHT_TYPE_SPOT        = 2u;
static const uint LIGHT_TYPE_AREA_RECT   = 3u;

// Convert a world-space direction into lat-long UVs, matching the IBL
// sampling used in Basic.hlsl so RT reflections align with the skybox
// and specular environment lighting.
float2 DirectionToLatLong(float3 dir)
{
    dir = normalize(dir);

    // Match Basic.hlsl: use -Z so looking down +Z maps to the center of
    // the panorama rather than the seam, and use asin for latitude to keep
    // the poles stable.
    if (!all(isfinite(dir)))
    {
        dir = float3(0.0f, 0.0f, 1.0f);
    }

    float phi   = atan2(-dir.z, dir.x);                     // [-PI, PI]
    float theta = asin(clamp(dir.y, -1.0f, 1.0f));          // [-PI/2, PI/2]

    float2 uv;
    uv.x = 0.5f + phi / (2.0f * PI);   // wrap around [0,1]
    uv.y = 0.5f - theta / PI;          // +Y up -> v decreasing
    return uv;
}

float3 SampleEnvironment(float3 dir, float roughness)
{
    dir = normalize(dir);

    // When IBL is disabled, fall back to flat ambient so RT reflections
    // do not fight the main shading.
    if (g_EnvParams.z <= 0.5f)
    {
        return g_AmbientColor.rgb;
    }

    float2 envUV = DirectionToLatLong(dir);
    // Match the raster PBR path more closely: rough reflection rays should
    // sample a blurrier environment lobe instead of injecting sharp HDR texels
    // that the post-process then has to hide.
    float perceptualRoughness = saturate(roughness);
    const float kApproxEnvMaxMip = 5.0f;
    float envMip = perceptualRoughness * perceptualRoughness * kApproxEnvMaxMip;
    float3 env = g_EnvSpecular.SampleLevel(g_Sampler, envUV, envMip).rgb;
    float envMax = max(max(env.r, env.g), env.b);
    const float kMaxEnvSample = 16.0f;
    if (envMax > kMaxEnvSample)
    {
        env *= kMaxEnvSample / envMax;
    }
    float  specIntensity = g_EnvParams.y;
    return env * specIntensity;
}

float3 EstimateHitSurfaceRadiance(ReflectionPayload payload, float3 hitPoint, float3 incomingRayDir)
{
    float3 N = normalize(payload.hitNormal);
    if (!all(isfinite(N)) || length(N) < 0.1f)
    {
        N = normalize(-incomingRayDir);
    }

    float3 V = normalize(-incomingRayDir);
    float roughness = saturate(payload.roughness);
    float metallic = saturate(payload.metallic);
    float3 albedo = saturate(payload.albedo);
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    F0 = saturate(F0 * max(payload.specularFactor, 0.0f));

    float3 diffuseWeight = (1.0f - metallic).xxx;
    if (SurfaceIsTransmissive(payload.surfaceClass, payload.transmission, 1.0f))
    {
        diffuseWeight *= 0.18f;
    }

    float3 radiance = payload.emissive;
    float hemi = saturate(N.y * 0.5f + 0.5f);
    float3 skyAmbient = g_AmbientColor.rgb * lerp(0.45f, 1.25f, hemi);
    radiance += skyAmbient * albedo * diffuseWeight;

    float3 envDir = reflect(incomingRayDir, N);
    float3 env = SampleEnvironment(envDir, roughness);
    float specGloss = pow(saturate(1.0f - roughness), 2.0f);
    float3 Fenv = F0 + (1.0f - F0) * pow(1.0f - saturate(dot(N, V)), 5.0f);
    radiance += env * Fenv * lerp(0.18f, 0.95f, metallic) * specGloss;

    uint lightCount = min(g_LightCount.x, LIGHT_MAX);
    [loop]
    for (uint i = 0u; i < lightCount; ++i)
    {
        Light light = g_Lights[i];
        uint type = (uint)light.position_type.w;

        float3 L;
        float attenuation = 1.0f;
        float3 lightRadiance = light.color_range.rgb;

        if (type == LIGHT_TYPE_POINT || type == LIGHT_TYPE_SPOT || type == LIGHT_TYPE_AREA_RECT)
        {
            float3 toLight = light.position_type.xyz - hitPoint;
            float dist = length(toLight);
            if (dist <= 1e-4f)
            {
                continue;
            }
            L = toLight / dist;

            float range = max(light.color_range.w, 0.001f);
            float falloff = saturate(1.0f - dist / range);
            attenuation = falloff * falloff;

            if (type == LIGHT_TYPE_SPOT)
            {
                float3 spotDir = normalize(light.direction_cosInner.xyz);
                float cosTheta = dot(-L, spotDir);
                float cosInner = light.direction_cosInner.w;
                float cosOuter = light.params.x;
                float spotFactor = saturate((cosTheta - cosOuter) / max(cosInner - cosOuter, 1e-4f));
                attenuation *= spotFactor * spotFactor;
            }
            else if (type == LIGHT_TYPE_AREA_RECT)
            {
                attenuation = max(attenuation, 0.28f);
            }
        }
        else
        {
            L = normalize(light.direction_cosInner.xyz);
        }

        float NdotL = saturate(dot(N, L));
        if (NdotL <= 1e-4f)
        {
            continue;
        }

        float3 H = normalize(L + V);
        float NdotH = saturate(dot(N, H));
        float VdotH = saturate(dot(V, H));
        float3 F = F0 + (1.0f - F0) * pow(1.0f - VdotH, 5.0f);

        float specPower = lerp(96.0f, 8.0f, roughness);
        if (type == LIGHT_TYPE_AREA_RECT)
        {
            specPower = min(specPower, 36.0f);
        }
        float specNorm = (specPower + 2.0f) / (8.0f * PI);
        float3 specular = F * specNorm * pow(NdotH, specPower);
        specular = min(specular, 3.0f.xxx);

        float3 diffuse = diffuseWeight * albedo / PI;
        radiance += (diffuse + specular) * lightRadiance * attenuation * NdotL;
    }

    float maxChannel = max(max(radiance.r, radiance.g), radiance.b);
    const float kMaxHitRadiance = 24.0f;
    if (maxChannel > kMaxHitRadiance)
    {
        radiance *= kMaxHitRadiance / maxChannel;
    }
    return max(radiance, 0.0f.xxx);
}

float2 LaunchToUV(uint2 launchIndex, uint2 launchDims)
{
    float2 uv;
    uv.x = (float(launchIndex.x) + 0.5f) / float(launchDims.x);
    uv.y = (float(launchIndex.y) + 0.5f) / float(launchDims.y);
    return uv;
}

// Reconstruct world position from depth and UV using the jittered inverse
// view-projection so the result matches the main depth/G-buffer path.
float3 ReconstructWorldPositionUV(float2 uv, float depth)
{
    float x = uv.x * 2.0f - 1.0f;
    float y = 1.0f - 2.0f * uv.y;
    float4 clip = float4(x, y, depth, 1.0f);

    float4 world = mul(g_InvViewProjMatrix, clip);
    return world.xyz / max(world.w, 1e-4f);
}

// Simple helper to reconstruct world position from depth and launch index.
float3 ReconstructWorldPosition(uint2 launchIndex, uint2 launchDims, float depth)
{
    return ReconstructWorldPositionUV(LaunchToUV(launchIndex, launchDims), depth);
}

// Approximate a world-space normal using a small depth-based gradient so that
// reflection rays follow the dominant surface orientation without requiring
// the full G-buffer in the RT pipeline.
float3 ApproximateNormal(uint2 launchIndex, uint2 launchDims)
{
    float2 uv = LaunchToUV(launchIndex, launchDims);
    uint depthW, depthH;
    g_Depth.GetDimensions(depthW, depthH);
    uint2 depthDim = uint2(max(depthW, 1u), max(depthH, 1u));
    int2 depthMax = int2(depthDim) - 1;

    int2 centerPix = clamp((int2)(uv * float2(depthDim)), int2(0, 0), depthMax);
    float depthCenter = g_Depth.Load(int3(centerPix, 0));
    if (depthCenter >= 1.0f - 1e-4f)
    {
        return float3(0.0f, 1.0f, 0.0f);
    }

    float2 uvCenter = (float2(centerPix) + 0.5f) / float2(depthDim);
    float3 pCenter = ReconstructWorldPositionUV(uvCenter, depthCenter);

    int2 pixR = min(centerPix + int2(1, 0), depthMax);
    int2 pixU = min(centerPix + int2(0, 1), depthMax);

    float2 uvR = (float2(pixR) + 0.5f) / float2(depthDim);
    float2 uvU = (float2(pixU) + 0.5f) / float2(depthDim);

    float depthRight = g_Depth.Load(int3(pixR, 0));
    float depthUp    = g_Depth.Load(int3(pixU, 0));

    float3 pRight = ReconstructWorldPositionUV(uvR, depthRight);
    float3 pUp    = ReconstructWorldPositionUV(uvU, depthUp);

    float3 dx = pRight - pCenter;
    float3 dy = pUp - pCenter;

    float3 N = normalize(cross(dy, dx));
    if (!all(isfinite(N)) || length(N) < 1e-3f)
    {
        N = float3(0.0f, 1.0f, 0.0f);
    }

    return N;
}

[shader("miss")]
void Miss_Reflection(inout ReflectionPayload payload)
{
    // Miss: sample the same environment map used for IBL so RT reflections
    // match the skybox and specular environment lighting.
    float3 dir = normalize(WorldRayDirection());
    float missRoughness = saturate(payload.roughness);
    payload.color = SampleEnvironment(dir, missRoughness);
    payload.hitNormal = float3(0.0f, 0.0f, 0.0f);
    payload.albedo = float3(1.0f, 1.0f, 1.0f);
    payload.emissive = float3(0.0f, 0.0f, 0.0f);
    payload.hitDistance = 0.0f;
    payload.roughness = missRoughness;
    payload.metallic = 0.0f;
    payload.surfaceClass = SURFACE_CLASS_DEFAULT;
    payload.transmission = 0.0f;
    payload.specularFactor = 1.0f;
    payload.clearcoat = 0.0f;
    payload.hit = false;
}

[shader("closesthit")]
void ClosestHit_Reflection(inout ReflectionPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    // Sample the G-buffer normal at the hit point to compute proper reflection
    // directions for multi-bounce reflections. We reconstruct screen-space UVs
    // from the world-space hit point by projecting it back through the camera.

    float3 worldRayDir = WorldRayDirection();
    float3 worldRayOrigin = WorldRayOrigin();
    float3 hitPoint = worldRayOrigin + worldRayDir * RayTCurrent();

    // Project hit point to clip space to get screen-space UVs for G-buffer lookup
    float4 clipPos = mul(g_ViewProjectionMatrix, float4(hitPoint, 1.0f));
    clipPos /= max(clipPos.w, 1e-4f);

    float2 screenUV;
    screenUV.x = clipPos.x * 0.5f + 0.5f;
    screenUV.y = 0.5f - clipPos.y * 0.5f;  // Flip Y for D3D coordinates

    // Sample G-buffer normal if the hit point is on-screen, otherwise fall back
    // to the ray-direction approximation for off-screen geometry.
    float3 surfaceNormal;
    if (screenUV.x >= 0.0f && screenUV.x <= 1.0f && screenUV.y >= 0.0f && screenUV.y <= 1.0f)
    {
        // Sample G-buffer normal using point Load to avoid blending across
        // geometry edges, which shows up as outline bands in glossy RT
        // reflections when the reflections buffer is half-resolution.
        uint gbufW, gbufH;
        g_NormalRoughness.GetDimensions(gbufW, gbufH);
        uint2 gbufDim = uint2(max(gbufW, 1u), max(gbufH, 1u));
        int2 gbufMax = int2(gbufDim) - 1;
        int2 pix = clamp((int2)(screenUV * float2(gbufDim)), int2(0, 0), gbufMax);

        float4 normalSample = g_NormalRoughness.Load(int3(pix, 0));
        surfaceNormal = normalize(normalSample.rgb * 2.0f - 1.0f);

        // Validate the normal; if invalid (NaN/zero), fall back to approximation
        if (!all(isfinite(surfaceNormal)) || length(surfaceNormal) < 0.1f)
        {
            surfaceNormal = normalize(-worldRayDir);
        }
    }
    else
    {
        // Hit point is off-screen (behind camera or outside frustum), use
        // ray-direction approximation as fallback
        surfaceNormal = normalize(-worldRayDir);
    }

    payload.hitNormal = surfaceNormal;
    payload.hitDistance = RayTCurrent();
    RTMaterial material = g_RTMaterials[InstanceID()];
    payload.albedo = saturate(material.albedoMetallic.rgb);
    payload.emissive = max(material.emissiveRoughness.rgb, 0.0f);
    payload.roughness = clamp(material.emissiveRoughness.a, 0.02f, 1.0f);
    payload.metallic = saturate(material.albedoMetallic.a);
    payload.surfaceClass = RTMaterialSurfaceClass(material);
    payload.transmission = RTMaterialTransmission(material);
    payload.specularFactor = RTMaterialSpecularFactor(material);
    payload.clearcoat = RTMaterialClearcoat(material);
    // A geometry hit is not itself lighting. The ray generation shader keeps
    // tracing until the path escapes to the environment or reaches its bounce
    // budget; adding environment at every hit double-counts sky energy and
    // makes mirror tunnels flicker between over-bright samples.
    payload.color = float3(0.0f, 0.0f, 0.0f);
    payload.hit = true;
}

[shader("raygeneration")]
void RayGen_Reflection()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDims  = DispatchRaysDimensions().xy;

    // The reflection output buffer is half-res, but depth/gbuffer are full-res.
    // Always sample depth by UV so the RT rays track the correct pixel footprint
    // and don't produce edge outlines from mismatched integer loads.
    float2 uv = LaunchToUV(launchIndex, launchDims);
    uint debugView = (uint)g_DebugMode.x;

    uint depthW, depthH;
    g_Depth.GetDimensions(depthW, depthH);
    uint2 depthDim = uint2(max(depthW, 1u), max(depthH, 1u));
    int2 depthMax = int2(depthDim) - 1;
    int2 pix = clamp((int2)(uv * float2(depthDim)), int2(0, 0), depthMax);

    float depth = g_Depth.Load(int3(pix, 0));
    if (depth >= 1.0f - 1e-4f)
    {
        // Background / far plane: no reflection information. Encode as black.
        g_ReflectionOut[launchIndex] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    float3 worldPos = ReconstructWorldPositionUV(uv, depth);
    float3 camPos   = g_CameraPosition.xyz;

    float3 V = normalize(camPos - worldPos);
    float4 nr = g_NormalRoughness.Load(int3(pix, 0));
    float3 N = normalize(nr.rgb * 2.0f - 1.0f);
    float roughness = saturate(nr.a);
    float4 materialExt2 = g_MaterialExt2.Load(int3(pix, 0));
    uint surfaceClass = DecodeSurfaceClass(materialExt2.r);
    if (!all(isfinite(N)) || length(N) < 0.1f)
    {
        N = ApproximateNormal(launchIndex, launchDims);
    }

    // The post composite gates RT reflections off for rough surfaces. Match
    // that contract here so matte walls/floors do not spend DXR work on data
    // that will be discarded later.
    bool reflectiveClass =
        SurfaceIsMirrorClass(surfaceClass) ||
        SurfaceIsWater(surfaceClass) ||
        SurfaceIsPolishedConductor(surfaceClass, 0.0f, roughness);
    float rtRoughnessThreshold = clamp(g_RTReflectionParams.x, 0.05f, 1.0f);
    if (debugView != 24u && roughness >= rtRoughnessThreshold && !reflectiveClass)
    {
        g_ReflectionOut[launchIndex] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    float3 initialR = normalize(reflect(-V, N));

    // Adaptive multi-bounce reflections. Very smooth surfaces can afford a few
    // mirror bounces at half resolution, while rough surfaces stay single-bounce
    // because temporal/spatial filtering dominates their final appearance.
    static const int MAX_BOUNCES = 3;
    static const float THROUGHPUT_CUTOFF = 0.05f;
    int activeBounces =
        SurfaceIsMirrorClass(surfaceClass) ? 3 :
        (SurfaceIsWater(surfaceClass) ? 2 :
        ((roughness < 0.06f) ? 3 : ((roughness < 0.16f) ? 2 : 1)));

    float3 accumulatedColor = float3(0.0f, 0.0f, 0.0f);
    float3 throughput = float3(1.0f, 1.0f, 1.0f);  // How much light survives each bounce

    float3 currentOrigin = worldPos + N * 0.05f;
    float3 currentDir = initialR;
    float currentRoughness = max(roughness, 0.02f);
    bool finalHit = false;

    for (int bounce = 0; bounce < MAX_BOUNCES; bounce++)
    {
        if (bounce >= activeBounces)
        {
            break;
        }

        // Early exit if throughput is too low (performance optimization)
        if (max(throughput.r, max(throughput.g, throughput.b)) < THROUGHPUT_CUTOFF)
        {
            break;
        }

        ReflectionPayload payload;
        payload.color = float3(0.0f, 0.0f, 0.0f);
        payload.hitNormal = float3(0.0f, 0.0f, 0.0f);
        payload.albedo = float3(1.0f, 1.0f, 1.0f);
        payload.emissive = float3(0.0f, 0.0f, 0.0f);
        payload.hitDistance = 0.0f;
        payload.roughness = currentRoughness;
        payload.metallic = 0.0f;
        payload.surfaceClass = SURFACE_CLASS_DEFAULT;
        payload.transmission = 0.0f;
        payload.specularFactor = 1.0f;
        payload.clearcoat = 0.0f;
        payload.hit = false;

        RayDesc ray;
        ray.Origin = currentOrigin;
        ray.Direction = currentDir;
        ray.TMin = 0.001f;  // Small offset to avoid self-intersection
        ray.TMax = 10000.0f;

        TraceRay(
            g_TopLevel,
            RAY_FLAG_NONE,  // Changed from ACCEPT_FIRST_HIT to allow proper bouncing
            /*InstanceInclusionMask*/ 0xFF,
            /*RayContributionToHitGroupIndex*/ 0,
            /*MultiplierForGeometryContributionToHitGroupIndex*/ 0,
            /*MissShaderIndex*/ 0,
            ray,
            payload);

        if (!payload.hit)
        {
            // Ray escaped to environment/sky - accumulate and stop
            accumulatedColor += payload.color * throughput;
            break;
        }

        // Hit a surface. Accumulate a cheap local-radiance estimate for that
        // material, then continue the specular path using the hit material's
        // F0. This keeps mirror/chrome bounces from collapsing to black when
        // they hit lit non-emissive scene geometry.
        finalHit = true;
        float3 hitPoint = ray.Origin + ray.Direction * payload.hitDistance;
        accumulatedColor += EstimateHitSurfaceRadiance(payload, hitPoint, currentDir) * throughput;

        // Calculate the proper reflection direction using the surface normal
        // returned by the ClosestHit shader
        float3 reflectedDir = reflect(currentDir, payload.hitNormal);

        float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), payload.albedo, payload.metallic);
        F0 = saturate(F0 * max(payload.specularFactor, 0.0f));
        F0 = lerp(F0, float3(0.04f, 0.04f, 0.04f), payload.transmission * 0.35f);
        float cosTheta = saturate(dot(-currentDir, payload.hitNormal));
        float3 fresnel = F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
        float hitRoughness = saturate(payload.roughness);
        float materialEnergy = lerp(0.86f, 0.14f, hitRoughness);
        if (SurfaceIsMirrorClass(payload.surfaceClass))
        {
            materialEnergy = 0.94f;
        }
        else if (SurfaceIsWater(payload.surfaceClass))
        {
            materialEnergy = 0.62f;
        }
        else if (payload.surfaceClass == SURFACE_CLASS_GLASS)
        {
            materialEnergy = 0.52f;
        }
        else if (payload.surfaceClass == SURFACE_CLASS_MASONRY ||
                 payload.surfaceClass == SURFACE_CLASS_PLASTIC ||
                 payload.surfaceClass == SURFACE_CLASS_WOOD)
        {
            materialEnergy *= 0.45f;
        }
        materialEnergy = lerp(materialEnergy, max(materialEnergy, 0.72f), payload.clearcoat);
        throughput *= saturate(fresnel) * materialEnergy;
        currentRoughness = saturate(max(currentRoughness, hitRoughness));

        // Update ray origin with small offset along the normal to avoid self-intersection
        currentOrigin = hitPoint + payload.hitNormal * 0.001f;
        currentDir = normalize(reflectedDir);
    }

    // If we exhausted all bounces without hitting sky, add fallback color
    // to avoid black center in infinite mirror tunnel
    if (max(throughput.r, max(throughput.g, throughput.b)) >= THROUGHPUT_CUTOFF)
    {
        accumulatedColor += g_AmbientColor.rgb * throughput * 0.5f;
    }

    // When the RT reflection ray-direction debug view is active (debug mode 24),
    // encode the reflection ray direction directly into RGB so the post-process
    // path can visualize the ray field. Otherwise, store the accumulated color.
    float3 outColor =
        (debugView == 24u)
        ? (initialR * 0.5f + 0.5f)
        : accumulatedColor;

    // The current reflection shader samples the environment on both hit and
    // miss, so a binary "hit/miss" validity flag creates visible discontinuity
    // bands in the post-process (half-res upsample + filtering sees abrupt
    // alpha edges). Treat the output as always valid for blending.
    g_ReflectionOut[launchIndex] = float4(outColor, 1.0f);
}
