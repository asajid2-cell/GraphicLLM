// Screen-space reflections (SSR) pass.
// Uses the shared FrameConstants layout (see ShaderTypes.h / Basic.hlsl).

cbuffer FrameConstants : register(b1)
{
    float4x4 g_ViewMatrix;
    float4x4 g_ProjectionMatrix;
    float4x4 g_ViewProjectionMatrix;
    float4x4 g_InvProjectionMatrix;
    float4   g_CameraPosition;
    // x = time, y = deltaTime, z = exposure, w = bloom intensity (unused here)
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
    // Directional + local light view-projection matrices (0-2 = cascades, 3-5 = local)
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

Texture2D g_SceneColor        : register(t0);
Texture2D g_Depth             : register(t1);
Texture2D g_NormalRoughness   : register(t2);
SamplerState g_Sampler        : register(s0);

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    VSOutput output;

    float2 pos;
    if (vertexId == 0)      pos = float2(-1.0f, -1.0f);
    else if (vertexId == 1) pos = float2(-1.0f,  3.0f);
    else                    pos = float2( 3.0f, -1.0f);

    output.position = float4(pos, 0.0f, 1.0f);
    output.uv = float2(0.5f * pos.x + 0.5f, -0.5f * pos.y + 0.5f);
    return output;
}

// Reconstruct world-space position from depth and UV using the inverse
// of the current view-projection matrix. Matches the NDC->UV mapping above.
float3 ReconstructWorldPosition(float2 uv, float depth)
{
    float x = uv.x * 2.0f - 1.0f;
    float y = 1.0f - 2.0f * uv.y;
    depth = saturate(depth);
    depth = min(depth, 1.0f - 1e-4f);
    float4 clip = float4(x, y, depth, 1.0f);
    float4 world = mul(g_InvViewProjMatrix, clip);
    if (!all(isfinite(world)))
    {
        return float3(0.0f, 0.0f, 0.0f);
    }
    return world.xyz / max(world.w, 1e-4f);
}

// Reconstruct view-space position from depth and UV using the inverse of the
// current (jittered) projection matrix. This is more numerically stable for
// SSR intersection tests than comparing post-projection depth in [0..1].
float3 ReconstructViewPosition(float2 uv, float depth)
{
    float x = uv.x * 2.0f - 1.0f;
    float y = 1.0f - 2.0f * uv.y;
    depth = saturate(depth);
    depth = min(depth, 1.0f - 1e-4f);
    float4 clip = float4(x, y, depth, 1.0f);
    float4 view = mul(g_InvProjectionMatrix, clip);
    if (!all(isfinite(view)))
    {
        return float3(0.0f, 0.0f, 0.0f);
    }
    return view.xyz / max(view.w, 1e-4f);
}

float4 SSRPS(VSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float2 texel = g_PostParams.xy;
    float depth = g_Depth.SampleLevel(g_Sampler, uv, 0).r;

    // Skip background / far plane
    if (depth >= 1.0f - 1e-4f)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    float3 worldPos = ReconstructWorldPosition(uv, depth);
    if (!all(isfinite(worldPos)))
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    float4 nr = g_NormalRoughness.SampleLevel(g_Sampler, uv, 0);
    float3 N = normalize(nr.xyz * 2.0f - 1.0f);
    float  roughness = saturate(nr.w);

    // Roughness-aware reflection weight (sharper falloff for mid/rough materials).
    float reflectionWeight = pow(saturate(1.0f - roughness), 2.0f);
    if (reflectionWeight <= 0.01f)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    float3 V = normalize(g_CameraPosition.xyz - worldPos);
    float3 R = normalize(reflect(-V, N));

    // Simple view-space ray-march along the reflection direction. Bias the
    // marcher towards more, shorter steps so near-field reflections are more
    // robust while still capping maximum distance to avoid long streaks.
    float3 viewPos = mul(g_ViewMatrix, float4(worldPos, 1.0f)).xyz;
    float3 viewDir = mul((float3x3)g_ViewMatrix, R);
    float  originZ = viewPos.z;

    float maxDistance = 30.0f;
    int   maxSteps    = 64;
    float stepSize    = maxDistance / maxSteps;

    // View-space thickness tolerance. Use a smaller thickness on glossy
    // surfaces to avoid early self-hits that can look like a nested "inner
    // copy" on chrome spheres, but relax it on rough surfaces to keep SSR
    // from disappearing entirely.
    float thicknessVS = lerp(0.03f, 0.20f, roughness);

    float3 hitColor = 0.0f;
    bool   hit      = false;

    float3 posVS = viewPos;
    float  traveled = 0.0f;

    // Avoid immediate self-intersection on highly reflective curved surfaces
    // (e.g., chrome spheres) which can manifest as a nested "inner copy"
    // artifact. Start the marcher a small distance along the ray.
    // Bias start away from the surface so the first few steps don't instantly
    // re-hit the same depth due to quantization/precision (most noticeable on
    // glossy curved surfaces).
    float startOffset = stepSize * 4.0f;
    startOffset += 0.02f * (1.0f - roughness);
    posVS += viewDir * startOffset;
    traveled += startOffset;

    // Require the ray to travel a minimum distance before accepting a hit.
    // This avoids near-origin self-hits that show up as a small nested copy
    // on glossy curved surfaces.
    float minHitDistance = lerp(0.25f, 0.05f, roughness);
    float minZSeparation = lerp(0.50f, 0.10f, roughness);

    [loop]
    for (int i = 0; i < maxSteps; ++i)
    {
        posVS += viewDir * stepSize;
        traveled += stepSize;

        float4 clip = mul(g_ProjectionMatrix, float4(posVS, 1.0f));
        if (clip.w <= 1e-4f || !all(isfinite(clip)))
            break;

        float2 ndc = clip.xy / clip.w;
        float2 hitUV;
        hitUV.x = ndc.x * 0.5f + 0.5f;
        hitUV.y = 0.5f - ndc.y * 0.5f;

        if (hitUV.x < 0.0f || hitUV.x > 1.0f || hitUV.y < 0.0f || hitUV.y > 1.0f)
            break;

        float sceneDepth = g_Depth.SampleLevel(g_Sampler, hitUV, 0).r;

        // Skip invalid/cleared depth
        if (sceneDepth <= 0.0f || sceneDepth >= 1.0f - 1e-4f)
        {
            continue;
        }

        // Avoid sampling almost the same pixel as the origin. This is a very
        // common source of "inner copy" artifacts on spheres due to depth
        // quantization and TAA jitter history.
        float2 deltaUV = abs(hitUV - uv);
        if (all(deltaUV < (texel * 2.0f)))
        {
            continue;
        }

        // View-space intersection test: compare the current ray position (posVS)
        // against the scene depth reconstructed into view space at hitUV.
        float3 sceneVS = ReconstructViewPosition(hitUV, sceneDepth);
        float dz = posVS.z - sceneVS.z;
        float zSep = abs(sceneVS.z - originZ);

        // Reject near-parallel / same-surface hits by comparing normals.
        float4 hitNR = g_NormalRoughness.SampleLevel(g_Sampler, hitUV, 0);
        float3 hitN = normalize(hitNR.xyz * 2.0f - 1.0f);
        bool sameSurface = dot(hitN, N) > 0.995f;

        if (traveled > minHitDistance &&
            zSep > minZSeparation &&
            !sameSurface &&
            dz > 0.0f && dz < thicknessVS)
        {
            hitColor = g_SceneColor.SampleLevel(g_Sampler, hitUV, 0).rgb;
            hit = true;
            break;
        }
    }

    if (!hit)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    // Clamp extremely bright hits to reduce sudden color spikes and gently
    // fade very long rays so far-field hits do not dominate the reflection.
    float distanceFactor = saturate(1.0f - traveled / maxDistance);
    hitColor *= distanceFactor;

    float maxChannel = max(max(hitColor.r, hitColor.g), hitColor.b);
    const float kMaxHitIntensity = 64.0f;
    if (maxChannel > kMaxHitIntensity)
    {
        hitColor *= (kMaxHitIntensity / maxChannel);
    }

    return float4(hitColor, reflectionWeight);
}
