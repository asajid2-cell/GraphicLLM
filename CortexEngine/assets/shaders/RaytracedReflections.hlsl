// Minimal DXR ray-traced reflections pass.
// This shader library is designed to be used with a dedicated DXR pipeline
// that binds:
//   - TLAS and depth in SRV space2 (t0, t1)
//   - Reflection color UAV in space2 (u0)
//   - Shadow/IBL/RT textures in SRV space1 (t0-t6, matching Basic.hlsl)
//   - FrameConstants in space0 (b0), matching ShaderTypes.h / Basic.hlsl.

RaytracingAccelerationStructure g_TopLevel      : register(t0, space2);
Texture2D<float>               g_Depth          : register(t1, space2);
Texture2D<float4>              g_NormalRoughness: register(t2, space2);
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
    float4   g_AOParams;
    float4   g_BloomParams;
    float4   g_TAAParams;
    float4x4 g_ViewProjectionNoJitter;
    float4x4 g_InvViewProjectionNoJitter;
    float4x4 g_PrevViewProjMatrix;
    float4x4 g_InvViewProjMatrix;
    float4   g_WaterParams0;
    float4   g_WaterParams1;
};

struct ReflectionPayload
{
    float3 color;
    float3 hitNormal;   // Surface normal at hit point for proper reflection calculation
    float  hitDistance; // Ray travel distance for accurate hit point reconstruction
    bool   hit;
};

static const float PI = 3.14159265f;

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

float3 SampleEnvironment(float3 dir)
{
    dir = normalize(dir);

    // When IBL is disabled, fall back to flat ambient so RT reflections
    // do not fight the main shading.
    if (g_EnvParams.z <= 0.5f)
    {
        return g_AmbientColor.rgb;
    }

    float2 envUV = DirectionToLatLong(dir);
    // For now sample the sharp environment; roughness-based mip selection
    // is handled in the main PBR shader and the post-process blend.
    float3 env = g_EnvSpecular.SampleLevel(g_Sampler, envUV, 0.0f).rgb;
    float  specIntensity = g_EnvParams.y;
    return env * specIntensity;
}

// Simple helper to reconstruct world position from depth and launch index.
float3 ReconstructWorldPosition(uint2 launchIndex, uint2 launchDims, float depth)
{
    float2 uv;
    uv.x = (float(launchIndex.x) + 0.5f) / float(launchDims.x);
    uv.y = (float(launchIndex.y) + 0.5f) / float(launchDims.y);

    float x = uv.x * 2.0f - 1.0f;
    float y = 1.0f - 2.0f * uv.y;
    float4 clip = float4(x, y, depth, 1.0f);

    // Use the jittered inverse view-projection so depth reconstruction matches
    // the main G-buffer/depth path. This avoids banding from inconsistent
    // matrices; temporal filtering handles jitter-induced noise.
    float4 world = mul(g_InvViewProjMatrix, clip);
    return world.xyz / max(world.w, 1e-4f);
}

// Approximate a world-space normal using a small depth-based gradient so that
// reflection rays follow the dominant surface orientation without requiring
// the full G-buffer in the RT pipeline.
float3 ApproximateNormal(uint2 launchIndex, uint2 launchDims)
{
    float depthCenter = g_Depth.Load(int3(launchIndex, 0));
    if (depthCenter >= 1.0f - 1e-4f)
    {
        return float3(0.0f, 1.0f, 0.0f);
    }

    float3 pCenter = ReconstructWorldPosition(launchIndex, launchDims, depthCenter);

    uint2 rightIndex = uint2(min(launchIndex.x + 1, launchDims.x - 1), launchIndex.y);
    uint2 upIndex    = uint2(launchIndex.x, min(launchIndex.y + 1, launchDims.y - 1));

    float depthRight = g_Depth.Load(int3(rightIndex, 0));
    float depthUp    = g_Depth.Load(int3(upIndex, 0));

    float3 pRight = ReconstructWorldPosition(rightIndex, launchDims, depthRight);
    float3 pUp    = ReconstructWorldPosition(upIndex, launchDims, depthUp);

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
    payload.color = SampleEnvironment(dir);
    payload.hitNormal = float3(0.0f, 0.0f, 0.0f);
    payload.hitDistance = 0.0f;
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
        // Sample G-buffer normal (stored in RGB as [0,1] encoding [-1,1])
        float4 normalSample = g_NormalRoughness.SampleLevel(g_Sampler, screenUV, 0);
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
    payload.color = SampleEnvironment(worldRayDir);
    payload.hit = true;
}

[shader("raygeneration")]
void RayGen_Reflection()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDims  = DispatchRaysDimensions().xy;

    float depth = g_Depth.Load(int3(launchIndex, 0));
    if (depth >= 1.0f - 1e-4f)
    {
        // Background / far plane: no reflection information. Encode as black.
        g_ReflectionOut[launchIndex] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    float3 worldPos = ReconstructWorldPosition(launchIndex, launchDims, depth);
    float3 camPos   = g_CameraPosition.xyz;

    float3 V = normalize(camPos - worldPos);
    float3 N = ApproximateNormal(launchIndex, launchDims);
    float3 initialR = normalize(reflect(-V, N));

    // Single-bounce ray tracing. Multi-bounce is disabled because we don't have
    // access to vertex normals in the hit shader, which are required to compute
    // proper reflection directions for secondary bounces.
    static const int MAX_BOUNCES = 1;
    static const float REFLECTIVITY = 0.95f;
    static const float THROUGHPUT_CUTOFF = 0.05f;

    float3 accumulatedColor = float3(0.0f, 0.0f, 0.0f);
    float3 throughput = float3(1.0f, 1.0f, 1.0f);  // How much light survives each bounce

    float3 currentOrigin = worldPos + N * 0.05f;
    float3 currentDir = initialR;
    bool finalHit = false;

    for (int bounce = 0; bounce < MAX_BOUNCES; bounce++)
    {
        // Early exit if throughput is too low (performance optimization)
        if (max(throughput.r, max(throughput.g, throughput.b)) < THROUGHPUT_CUTOFF)
        {
            break;
        }

        ReflectionPayload payload;
        payload.color = float3(0.0f, 0.0f, 0.0f);
        payload.hitNormal = float3(0.0f, 0.0f, 0.0f);
        payload.hitDistance = 0.0f;
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

        // Hit a surface - treat it as a mirror for multi-bounce reflections
        // In a full implementation, you'd check material properties here
        // and break early if the surface is rough (roughness > 0.1)

        // Accumulate the environment contribution at this hit point
        accumulatedColor += payload.color * throughput;
        finalHit = true;

        // Prepare for next bounce using the hit information from the payload
        float3 hitPoint = ray.Origin + ray.Direction * payload.hitDistance;

        // Calculate the proper reflection direction using the surface normal
        // returned by the ClosestHit shader
        float3 reflectedDir = reflect(currentDir, payload.hitNormal);

        // Each bounce loses some energy (mirrors aren't 100% reflective)
        throughput *= REFLECTIVITY;

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
    uint debugView = (uint)g_DebugMode.x;
    float3 outColor =
        (debugView == 24u)
        ? (initialR * 0.5f + 0.5f)
        : accumulatedColor;

    g_ReflectionOut[launchIndex] = float4(outColor, finalHit ? 1.0f : 0.0f);
}
