// DXR diffuse global-illumination visibility pass.
// This shader library is designed to be used with a dedicated DXR pipeline
// that binds:
//   - TLAS and depth in SRV space2 (t0, t1)
//   - Compact RT material buffer in SRV space2 (t3), keyed by TLAS InstanceID()
//   - Previous-frame lit color in SRV space2 (t4), copied from TAA history
//   - GI color UAV in space2 (u0)
//   - FrameConstants in space0 (b0), matching ShaderTypes.h / Basic.hlsl.

#include "RaytracingMaterials.hlsli"

RaytracingAccelerationStructure g_TopLevel     : register(t0, space2);
Texture2D<float>               g_Depth         : register(t1, space2);
Texture2D<float4>              g_PrevFrameLit  : register(t4, space2);
RWTexture2D<float4>            g_GIOut         : register(u0, space2);
SamplerState                   g_LinearSampler : register(s0, space0);

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
    float4   g_PostGradeParams;
    float4   g_RTReflectionParams;
    uint4    g_ScreenAndCluster;
    uint4    g_ClusterParams;
    uint4    g_ClusterSRVIndices;
    float4   g_ProjectionParams;
    // x = tone-mapper mode, y = environment rotation radians, z = RT GI strength, w = RT GI ray distance.
    float4   g_CinematicParams;
};

struct GIPayload
{
    float3 radiance;
    // 0 = fully blocked by diffuse geometry, 1 = open along the sampled direction.
    float visibility;
};

static const uint LIGHT_TYPE_DIRECTIONAL = 0u;
static const uint LIGHT_TYPE_POINT       = 1u;
static const uint LIGHT_TYPE_SPOT        = 2u;
static const uint LIGHT_TYPE_AREA_RECT   = 3u;
static const float GI_PI = 3.14159265359f;

// Reconstruct world-space position from depth and dispatch index, matching the
// mapping used in the main PBR and post-process passes.
float3 ReconstructWorldPositionUV(float2 uv, float depth)
{
    float x = uv.x * 2.0f - 1.0f;
    float y = 1.0f - 2.0f * uv.y;

    // Clamp depth to a safe range so we do not accidentally treat clear
    // values as valid geometry.
    depth = saturate(depth);
    depth = min(depth, 1.0f - 1e-4f);

    float4 clip = float4(x, y, depth, 1.0f);
    // Use the jittered inverse view-projection so depth samples and world
    // reconstruction match the main rasterized path exactly. Stability is
    // provided via spatial/temporal filtering rather than mismatched math.
    float4 world = mul(g_InvViewProjMatrix, clip);
    return world.xyz / max(world.w, 1e-4f);
}

// Approximate a world-space normal using a small depth-based gradient. This
// avoids requiring the full G-buffer in the RT pipeline while still giving
// GI rays a plausible hemisphere orientation.
float3 ApproximateNormal(uint2 centerPix, uint2 depthDim)
{
    float depthCenter = g_Depth.Load(int3(centerPix, 0));
    if (depthCenter >= 1.0f - 1e-4f)
    {
        return float3(0.0f, 1.0f, 0.0f);
    }

    float2 uvCenter = (float2(centerPix) + 0.5f) / float2(depthDim);
    float3 pCenter = ReconstructWorldPositionUV(uvCenter, depthCenter);

    uint2 rightIndex = uint2(min(centerPix.x + 1, depthDim.x - 1), centerPix.y);
    uint2 upIndex    = uint2(centerPix.x, min(centerPix.y + 1, depthDim.y - 1));

    float depthRight = g_Depth.Load(int3(rightIndex, 0));
    float depthUp    = g_Depth.Load(int3(upIndex, 0));

    float2 uvRight = (float2(rightIndex) + 0.5f) / float2(depthDim);
    float2 uvUp    = (float2(upIndex) + 0.5f) / float2(depthDim);
    float3 pRight = ReconstructWorldPositionUV(uvRight, depthRight);
    float3 pUp    = ReconstructWorldPositionUV(uvUp, depthUp);

    float3 dx = pRight - pCenter;
    float3 dy = pUp - pCenter;

    float3 N = normalize(cross(dy, dx));

    if (!all(isfinite(N)) || length(N) < 1e-3f)
    {
        N = float3(0.0f, 1.0f, 0.0f);
    }

    return N;
}

void StoreGIBlock(uint2 launchIndex, uint2 launchDims, float4 value)
{
    [unroll]
    for (uint y = 0u; y < 2u; ++y)
    {
        [unroll]
        for (uint x = 0u; x < 2u; ++x)
        {
            uint2 p = launchIndex + uint2(x, y);
            if (p.x < launchDims.x && p.y < launchDims.y)
            {
                g_GIOut[p] = value;
            }
        }
    }
}

uint HashGI(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float RandomGI(inout uint state)
{
    state = HashGI(state);
    return (float)(state & 0x00FFFFFFu) / 16777216.0f;
}

float3 CosineHemisphereDirection(float2 u, float3 N, float3 T, float3 B)
{
    float phi = 2.0f * GI_PI * u.x;
    float r = sqrt(saturate(u.y));
    float x = r * cos(phi);
    float y = r * sin(phi);
    float z = sqrt(saturate(1.0f - u.y));
    return normalize(T * x + B * y + N * z);
}

float LumaGI(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 ClampRadianceGI(float3 color, float maxLuma)
{
    color = max(color, 0.0f.xxx);
    if (!all(isfinite(color)))
    {
        return 0.0f.xxx;
    }

    float luma = LumaGI(color);
    if (luma > maxLuma)
    {
        color *= maxLuma / max(luma, 1.0e-4f);
    }
    return min(color, 24.0f.xxx);
}

float3 AmbientFallbackIncident()
{
    return max(g_AmbientColor.rgb, 0.0f.xxx) * 0.65f;
}

float3 SamplePreviousFrameIncident(float3 hitPos, out bool valid)
{
    valid = false;

    float4 prevClip = mul(g_PrevViewProjMatrix, float4(hitPos, 1.0f));
    if (prevClip.w <= 1.0e-4f || !all(isfinite(prevClip)))
    {
        return AmbientFallbackIncident();
    }

    float2 ndc = prevClip.xy / prevClip.w;
    float2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
    if (uv.x <= 0.0f || uv.x >= 1.0f || uv.y <= 0.0f || uv.y >= 1.0f)
    {
        return AmbientFallbackIncident();
    }

    float3 previousLit = g_PrevFrameLit.SampleLevel(g_LinearSampler, uv, 0.0f).rgb;
    previousLit = ClampRadianceGI(previousLit, 10.0f);

    // Null/unseeded history reads as black. Treat that as invalid so the first
    // few frames converge from ambient instead of extinguishing GI.
    valid = LumaGI(previousLit) > 1.0e-5f;
    return valid ? previousLit : AmbientFallbackIncident();
}

float3 EstimateHitRadiance(RTMaterial material, float3 hitPos)
{
    float3 albedo = saturate(material.albedoMetallic.rgb);
    float metallic = saturate(material.albedoMetallic.a);
    float diffuseWeight = RTMaterialDiffuseGIVisibility(material) * (1.0f - metallic);
    float3 emissive = max(material.emissiveRoughness.rgb, 0.0f.xxx);

    bool previousValid = false;
    float3 incident = SamplePreviousFrameIncident(hitPos, previousValid);

    float3 bounced = albedo * incident * diffuseWeight;
    return ClampRadianceGI(emissive + bounced, previousValid ? 12.0f : 4.0f);
}

[shader("miss")]
void Miss_GI(inout GIPayload payload)
{
    payload.radiance = 0.0f.xxx;
    payload.visibility = 1.0f;
}

[shader("closesthit")]
void ClosestHit_GI(inout GIPayload payload, in BuiltInTriangleIntersectionAttributes /*attribs*/)
{
    RTMaterial material = g_RTMaterials[InstanceID()];
    float occlusion = RTMaterialDiffuseGIVisibility(material);
    float3 hitPos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    payload.radiance = EstimateHitRadiance(material, hitPos);
    payload.visibility = saturate(1.0f - occlusion);
}

[shader("raygeneration")]
void RayGen_GI()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDims  = DispatchRaysDimensions().xy;

    // GI is intentionally low frequency. Trace one ray group for each 2x2
    // output block and broadcast the result; this keeps RT GI enabled while
    // cutting TraceRay calls by roughly 75%.
    if (((launchIndex.x | launchIndex.y) & 1u) != 0u)
    {
        return;
    }

    uint depthW, depthH;
    g_Depth.GetDimensions(depthW, depthH);
    uint2 depthDim = uint2(max(depthW, 1u), max(depthH, 1u));
    int2 depthMax = int2(depthDim) - 1;

    uint2 safeLaunchDims = uint2(max(launchDims.x, 1u), max(launchDims.y, 1u));
    float2 uv = (float2(launchIndex) + 0.5f) / float2(safeLaunchDims);
    int2 centerPix = clamp((int2)(uv * float2(depthDim)), int2(0, 0), depthMax);
    float depth = g_Depth.Load(int3(centerPix, 0));

    // Background / far plane: no GI contribution.
    if (depth >= 1.0f - 1e-4f)
    {
        StoreGIBlock(launchIndex, launchDims, float4(0.0f, 0.0f, 0.0f, 0.0f));
        return;
    }

    float3 worldPos = ReconstructWorldPositionUV(uv, depth);
    float3 N = ApproximateNormal((uint2)centerPix, depthDim);

    const float bias = 0.05f;
    float3 origin = worldPos + N * bias;
    const float rayDistance = clamp(g_CinematicParams.w, 0.5f, 20.0f);

    // Build a simple orthonormal basis around N for secondary directions.
    float3 up = (abs(N.y) < 0.99f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);

    float3 radianceSum = 0.0f.xxx;
    float visibilitySum = 0.0f;
    const int kRayCount = 5;
    uint rng = HashGI(launchIndex.x * 1973u ^
                      launchIndex.y * 9277u ^
                      (uint)(g_TimeAndExposure.x * 60.0f) * 26699u);

    [loop]
    for (int i = 0; i < kRayCount; ++i)
    {
        float2 u = float2(RandomGI(rng), RandomGI(rng));
        u.x = frac(u.x + ((float)i + 0.5f) / (float)kRayCount);
        float3 dir = CosineHemisphereDirection(u, N, T, B);

        GIPayload payload;
        payload.radiance = 0.0f.xxx;
        payload.visibility = 1.0f;

        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = dir;
        ray.TMin = 0.0f;
        ray.TMax = rayDistance;

        TraceRay(
            g_TopLevel,
            RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
            /*InstanceInclusionMask*/ 0xFF,
            /*RayContributionToHitGroupIndex*/ 0,
            /*MultiplierForGeometryContributionToHitGroupIndex*/ 0,
            /*MissShaderIndex*/ 0,
            ray,
            payload);

        radianceSum += payload.radiance;
        visibilitySum += payload.visibility;
    }

    float3 irradiance = max(radianceSum / (float)kRayCount, 0.0f.xxx);
    float visibility = saturate(visibilitySum / (float)kRayCount);
    StoreGIBlock(launchIndex, launchDims, float4(min(irradiance, 32.0f.xxx), visibility));
}
