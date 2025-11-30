// Minimal DXR ray-traced sun shadow pass.
// This shader library is designed to be used with a dedicated DXR pipeline
// that binds:
//   - TLAS and depth in SRV space2 (t0, t1)
//   - Shadow mask UAV in space2 (u0)
//   - FrameConstants in space0 (b0), matching ShaderTypes.h / Basic.hlsl.

RaytracingAccelerationStructure g_TopLevel : register(t0, space2);
Texture2D<float>               g_Depth     : register(t1, space2);
RWTexture2D<float>             g_ShadowMask: register(u0, space2);

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

struct ShadowPayload
{
    bool occluded;
};

[shader("miss")]
void Miss_Shadow(inout ShadowPayload payload)
{
    payload.occluded = false;
}

[shader("closesthit")]
void ClosestHit_Shadow(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes /*attribs*/)
{
    // Any hit along the ray counts as occlusion for sun shadows.
    payload.occluded = true;
}

[shader("raygeneration")]
void RayGen_Shadow()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDims  = DispatchRaysDimensions().xy;

    // Load depth; if depth is invalid, treat as unshadowed.
    float depth = g_Depth.Load(int3(launchIndex, 0));
    if (depth <= 0.0f || depth >= 1.0f)
    {
        g_ShadowMask[launchIndex] = 1.0f;
        return;
    }

    // Reconstruct clip-space position (DirectX 0..1 depth).
    float2 ndc;
    ndc.x = ( (float(launchIndex.x) + 0.5f) / float(launchDims.x) ) * 2.0f - 1.0f;
    ndc.y = 1.0f - ( (float(launchIndex.y) + 0.5f) / float(launchDims.y) ) * 2.0f;
    float4 clipPos = float4(ndc.x, ndc.y, depth, 1.0f);

    // Unproject to world space using the same inverse view-projection matrix
    // that was used to write the depth buffer (jittered). This keeps screen-
    // space depth and TLAS queries in lock-step and avoids systematic
    // distortions/banding from mismatched matrices.
    float4 worldH = mul(g_InvViewProjMatrix, clipPos);
    if (worldH.w == 0.0f)
    {
        g_ShadowMask[launchIndex] = 1.0f;
        return;
    }
    float3 worldPos = worldH.xyz / worldH.w;

    // Use light 0 as the sun (directional). direction_cosInner.xyz stores
    // the direction from the shaded point *toward* the light in the main
    // PBR path, and we cast the shadow ray along the same direction.
    float3 sunDir = float3(0.0f, -1.0f, 0.0f);
    if (g_LightCount.x > 0)
    {
        sunDir = normalize(g_Lights[0].direction_cosInner.xyz);
    }

    // Small bias along light direction to reduce self-shadowing.
    const float bias = 0.01f;
    float3 origin = worldPos + sunDir * bias;

    ShadowPayload payload;
    payload.occluded = false;

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = sunDir;
    ray.TMin = 0.0f;
    ray.TMax = 10000.0f;

    TraceRay(
        g_TopLevel,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        /*InstanceInclusionMask*/ 0xFF,
        /*RayContributionToHitGroupIndex*/ 0,
        /*MultiplierForGeometryContributionToHitGroupIndex*/ 0,
        /*MissShaderIndex*/ 0,
        ray,
        payload);

    g_ShadowMask[launchIndex] = payload.occluded ? 0.0f : 1.0f;
}
