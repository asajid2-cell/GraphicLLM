// Screen-space ambient occlusion pass.
// Uses the shared FrameConstants layout (see ShaderTypes.h / PostProcess.hlsl)
// and samples the hardware depth buffer to estimate local occlusion.

cbuffer FrameConstants : register(b1)
{
    float4x4 g_ViewMatrix;
    float4x4 g_ProjectionMatrix;
    float4x4 g_ViewProjectionMatrix;
    float4x4 g_InvProjectionMatrix;
    float4   g_CameraPosition;
    // x = time, y = deltaTime, z = exposure, w = bloom intensity
    float4   g_TimeAndExposure;
    // rgb: ambient color * intensity, w unused
    float4   g_AmbientColor;
    uint4    g_LightCount;
    struct Light
    {
        float4 position_type;
        float4 direction_cosInner;
        float4 color_range;
        float4 params;
    };
    Light    g_Lights[4];
    // Directional + local light view-projection matrices (0-2 = cascades, 3-5 = local)
    float4x4 g_LightViewProjection[6];
    float4   g_CascadeSplits;
    float4   g_ShadowParams;
    float4   g_DebugMode;
    float4   g_PostParams;
    float4   g_EnvParams;
    float4   g_ColorGrade;
    float4   g_FogParams;
    // x = SSAO enabled (>0.5), y = radius, z = bias, w = intensity
    float4   g_AOParams;
    // x = bloom threshold, y = soft-knee factor, z = max bloom contribution, w reserved
    float4   g_BloomParams;
    // x = jitterX, y = jitterY, z = TAA blend factor, w = TAA enabled (>0.5)
    float4   g_TAAParams;
    float4x4 g_PrevViewProjMatrix;
    float4x4 g_InvViewProjMatrix;
};

Texture2D g_Depth : register(t0);
SamplerState g_Sampler : register(s0);

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    VSOutput output;

    // Fullscreen triangle in NDC
    float2 pos;
    if (vertexId == 0)      pos = float2(-1.0f, -1.0f);
    else if (vertexId == 1) pos = float2(-1.0f,  3.0f);
    else                    pos = float2( 3.0f, -1.0f);

    output.position = float4(pos, 0.0f, 1.0f);

    // Map NDC to UV (0..1), flipping Y (matches PostProcess)
    output.uv = float2(0.5f * pos.x + 0.5f, -0.5f * pos.y + 0.5f);
    return output;
}

float3 ReconstructViewPos(float2 uv, float depth, float4x4 invProj)
{
    // Invert the UV mapping used in VSMain to get clip space.
    float x = uv.x * 2.0f - 1.0f;
    float y = 1.0f - 2.0f * uv.y;
    // Clamp depth to a safe range inside (0,1) to avoid numerical issues
    depth = saturate(depth);
    depth = min(depth, 1.0f - 1e-4f);
    float4 clip = float4(x, y, depth, 1.0f);
    float4 view = mul(invProj, clip);
    return view.xyz / max(view.w, 1e-4f);
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    // If SSAO is disabled, output no occlusion (1.0).
    if (g_AOParams.x <= 0.5f)
    {
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    float2 uv = input.uv;
    float depthCenter = g_Depth.SampleLevel(g_Sampler, uv, 0).r;
    if (depthCenter >= 1.0f - 1e-4f)
    {
        // Far plane / background: treat as unoccluded.
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    // SSAO is rendered at half resolution; scale texel size accordingly
    float2 texel = g_PostParams.xy * 2.0f;

    // Use inverse projection matrix provided by CPU via FrameConstants.
    float4x4 invProj = g_InvProjectionMatrix;
    float3 posCenter = ReconstructViewPos(uv, depthCenter, invProj);
    if (!all(isfinite(posCenter)))
    {
        // Invalid reconstruction; treat as unoccluded to avoid flashing
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    // Approximate normal from neighboring depth samples.
    float depthRight = g_Depth.SampleLevel(g_Sampler, uv + float2(texel.x, 0.0f), 0).r;
    float depthUp    = g_Depth.SampleLevel(g_Sampler, uv - float2(0.0f, texel.y), 0).r;

    float3 posRight = ReconstructViewPos(uv + float2(texel.x, 0.0f), depthRight, invProj);
    float3 posUp    = ReconstructViewPos(uv - float2(0.0f, texel.y), depthUp,    invProj);

    float3 vRight = posRight - posCenter;
    float3 vUp    = posUp    - posCenter;
    float3 normal = normalize(cross(vRight, vUp));

    // Guard against degenerate normals.
    if (!all(isfinite(normal)) || length(normal) < 1e-4f)
    {
        normal = float3(0.0f, 1.0f, 0.0f);
    }

    float baseRadius = max(g_AOParams.y, 0.01f);
    float bias      = max(g_AOParams.z, 0.0f);
    float intensity = max(g_AOParams.w, 0.0f);

    // Scale sampling radius with view-space depth so AO stays
    // visually consistent across near and far geometry.
    float depthScale = max(posCenter.z, 1.0f);
    float radius = baseRadius * depthScale;

    // Simple fixed sample kernel in view space.
    static const int   kSampleCount = 8;
    static const float3 kKernel[8] = {
        float3( 1,  0,  1),
        float3(-1,  0,  1),
        float3( 0,  1,  1),
        float3( 0, -1,  1),
        float3( 1,  1,  1),
        float3(-1,  1,  1),
        float3( 1, -1,  1),
        float3(-1, -1, 1)
    };

    float occlusion = 0.0f;

    // Build a stable orthonormal basis around the normal for the sample
    // kernel to avoid numerical issues when the normal is near world axes.
    float3 up = (abs(normal.y) < 0.99f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent = normalize(cross(up, normal));
    float3 bitangent = cross(normal, tangent);

    for (int i = 0; i < kSampleCount; ++i)
    {
        // Orient sample around the normal using the local tangent frame.
        float3 dir = normalize(kKernel[i]);
        dir = normalize(dir.x * tangent + dir.y * bitangent + dir.z * normal);

        float3 samplePos = posCenter + dir * radius;

        // Project sample position back to clip space and then UV.
        float4 sampleClip = mul(g_ProjectionMatrix, float4(samplePos, 1.0f));
        sampleClip.xyz /= max(sampleClip.w, 1e-4f);

        float2 sampleUV;
        sampleUV.x = sampleClip.x * 0.5f + 0.5f;
        sampleUV.y = 0.5f - sampleClip.y * 0.5f;

        // Skip samples outside the screen.
        if (sampleUV.x < 0.0f || sampleUV.x > 1.0f || sampleUV.y < 0.0f || sampleUV.y > 1.0f)
            continue;

        float sampleDepth = g_Depth.SampleLevel(g_Sampler, sampleUV, 0).r;

        // Convert clip-space z back to [0,1] depth for comparison.
        float sampleZ = saturate(sampleClip.z);

        float rangeCheck = smoothstep(0.0f, 1.0f, radius / (abs(posCenter.z - samplePos.z) + 1e-3f));
        float delta = sampleDepth - sampleZ;

        // Count occlusion when geometry is closer than the sample point by more than the bias.
        if (delta < -bias)
        {
            occlusion += rangeCheck;
        }
    }

    float ao = 1.0f - (occlusion / (float)kSampleCount);
    ao = saturate(ao);

    // Apply overall intensity; higher intensity darkens occluded regions more.
    ao = lerp(1.0f, ao, saturate(intensity));

    return float4(ao, ao, ao, 1.0f);
}
