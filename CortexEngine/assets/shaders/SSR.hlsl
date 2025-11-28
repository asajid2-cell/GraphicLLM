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
    float4x4 g_PrevViewProjMatrix;
    float4x4 g_InvViewProjMatrix;
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

float4 SSRPS(VSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
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

    // Simple view-space ray-march along the reflection direction.
    float3 viewPos = mul(g_ViewMatrix, float4(worldPos, 1.0f)).xyz;
    float3 viewDir = mul((float3x3)g_ViewMatrix, R);

    float maxDistance = 50.0f;
    int   maxSteps    = 32;
    float stepSize    = maxDistance / maxSteps;

    float thickness = 0.02f;

    float3 hitColor = 0.0f;
    bool   hit      = false;

    float3 posVS = viewPos;

    [loop]
    for (int i = 0; i < maxSteps; ++i)
    {
        posVS += viewDir * stepSize;

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
        float rayDepth   = saturate(clip.z / clip.w);

        // Skip invalid/cleared depth
        if (sceneDepth <= 0.0f || sceneDepth >= 1.0f - 1e-4f)
        {
            continue;
        }

        // Basic thickness test in depth space
        if (abs(rayDepth - sceneDepth) < thickness)
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

    // Clamp extremely bright hits to reduce sudden color spikes
    float maxChannel = max(max(hitColor.r, hitColor.g), hitColor.b);
    const float kMaxHitIntensity = 64.0f;
    if (maxChannel > kMaxHitIntensity)
    {
        hitColor *= (kMaxHitIntensity / maxChannel);
    }

    return float4(hitColor, reflectionWeight);
}
