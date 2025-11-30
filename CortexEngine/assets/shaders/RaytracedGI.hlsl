// Minimal DXR diffuse global illumination pass.
// This shader library is designed to be used with a dedicated DXR pipeline
// that binds:
//   - TLAS and depth in SRV space2 (t0, t1)
//   - GI color UAV in space2 (u0)
//   - FrameConstants in space0 (b0), matching ShaderTypes.h / Basic.hlsl.

RaytracingAccelerationStructure g_TopLevel     : register(t0, space2);
Texture2D<float>               g_Depth         : register(t1, space2);
RWTexture2D<float4>            g_GIOut         : register(u0, space2);

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

struct GIPayload
{
    // 0 = unoccluded, 1 = fully occluded along the sampled direction.
    float occlusion;
};

// Reconstruct world-space position from depth and dispatch index, matching the
// mapping used in the main PBR and post-process passes.
float3 ReconstructWorldPosition(uint2 launchIndex, uint2 launchDims, float depth)
{
    float2 uv;
    uv.x = (float(launchIndex.x) + 0.5f) / float(launchDims.x);
    uv.y = (float(launchIndex.y) + 0.5f) / float(launchDims.y);

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
void Miss_GI(inout GIPayload payload)
{
    // No geometry encountered along the sampled ray: treat as unoccluded.
    payload.occlusion = 0.0f;
}

[shader("closesthit")]
void ClosestHit_GI(inout GIPayload payload, in BuiltInTriangleIntersectionAttributes /*attribs*/)
{
    // Any hit along the ray is treated as fully occluding diffuse ambient
    // from that direction for this simple first pass.
    payload.occlusion = 1.0f;
}

[shader("raygeneration")]
void RayGen_GI()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDims  = DispatchRaysDimensions().xy;

    float depth = g_Depth.Load(int3(launchIndex, 0));

    // Background / far plane: no GI contribution.
    if (depth >= 1.0f - 1e-4f)
    {
        g_GIOut[launchIndex] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    float3 worldPos = ReconstructWorldPosition(launchIndex, launchDims, depth);
    float3 N = ApproximateNormal(launchIndex, launchDims);

    // Cast a small number of rays in the local hemisphere around the
    // approximate normal and treat the result as a simple visibility term
    // for ambient/diffuse IBL. This is closer to ray-traced ambient
    // occlusion than full multi-bounce GI, but with two directions per
    // pixel it produces smoother, more stable results than a single ray.
    const float bias = 0.05f;
    float3 origin = worldPos + N * bias;

    // Build a simple orthonormal basis around N for secondary directions.
    float3 up = (abs(N.y) < 0.99f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);

    float occlusionSum = 0.0f;
    const int kRayCount = 2;

    // Ray 0: along the normal.
    {
        GIPayload payload;
        payload.occlusion = 0.0f;

        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = N;
        ray.TMin = 0.0f;
        ray.TMax = 5.0f; // local occlusion radius

        TraceRay(
            g_TopLevel,
            RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
            /*InstanceInclusionMask*/ 0xFF,
            /*RayContributionToHitGroupIndex*/ 0,
            /*MultiplierForGeometryContributionToHitGroupIndex*/ 0,
            /*MissShaderIndex*/ 0,
            ray,
            payload);

        occlusionSum += payload.occlusion;
    }

    // Ray 1: a slightly tilted direction in the tangent/bitangent plane.
    {
        float3 dir1 = normalize(N + 0.5f * T + 0.25f * B);

        GIPayload payload;
        payload.occlusion = 0.0f;

        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = dir1;
        ray.TMin = 0.0f;
        ray.TMax = 5.0f;

        TraceRay(
            g_TopLevel,
            RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
            /*InstanceInclusionMask*/ 0xFF,
            /*RayContributionToHitGroupIndex*/ 0,
            /*MultiplierForGeometryContributionToHitGroupIndex*/ 0,
            /*MissShaderIndex*/ 0,
            ray,
            payload);

        occlusionSum += payload.occlusion;
    }

    float visibility = saturate(1.0f - occlusionSum / (float)kRayCount);

    // Encode visibility in both rgb (for debug visualization) and alpha so
    // the main PBR shader can treat this as an ambient/IBL occlusion factor.
    float3 giColor = g_AmbientColor.rgb * visibility;
    g_GIOut[launchIndex] = float4(giColor, visibility);
}
