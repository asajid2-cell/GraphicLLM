// Fullscreen post-process: exposure, ACES tonemapping, gamma, simple bloom stub

// Frame constants must match ShaderTypes.h / Basic.hlsl exactly
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
    // Forward lights (light 0 is the sun). Must match ShaderTypes.h.
    struct Light
    {
        float4 position_type;        // xyz = position (for point/spot), w = type
        float4 direction_cosInner;   // xyz = direction, w = inner cone cos (spot)
        float4 color_range;          // rgb = color * intensity, w = range (point/spot)
        float4 params;               // x = outer cone cos, y = shadow index, z,w reserved
    };
    static const uint LIGHT_MAX = 16;
    Light    g_Lights[LIGHT_MAX];
    // Directional + local light view-projection matrices (0-2 = cascades, 3-5 = local)
    float4x4 g_LightViewProjection[6];
    // x,y,z = cascade split depths in view space, w = far plane
    float4   g_CascadeSplits;
    // x = depth bias, y = PCF radius in texels, z = shadows enabled (>0.5), w = PCSS enabled (>0.5)
    float4   g_ShadowParams;
    // x = debug view mode (0 = shaded, 1 = normals, 2 = roughness, 3 = metallic,
    //                      4 = albedo, 5 = cascade index, 6 = debug screen,
    //                      7 = fractal height, 8 = IBL diffuse only,
    //                      9 = IBL specular only, 10 = env direction/UV,
    //                      11 = Fresnel (Fibl), 12 = specular mip,
    //                      13 = SSAO only, 14 = SSAO overlay), others reserved
    float4   g_DebugMode;
    // x = 1 / screenWidth, y = 1 / screenHeight, z = FXAA enabled (>0.5), w reserved
    float4   g_PostParams;
    // x = diffuse IBL intensity, y = specular IBL intensity,
    // z = IBL enabled (>0.5), w = environment index (0 = studio, 2 = night)
    float4   g_EnvParams;
    // x = warm tint (-1..1), y = cool tint (-1..1), z,w reserved
    float4   g_ColorGrade;
    // x = fog density, y = base height, z = height falloff, w = fog enabled (>0.5)
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

Texture2D g_SceneColor : register(t0);
Texture2D g_BloomSource : register(t1);
Texture2D g_SSAO : register(t2);
Texture2D g_HistoryColor : register(t3);
Texture2D g_Depth : register(t4);
Texture2D g_NormalRoughness : register(t5);
Texture2D g_SSRColor : register(t6);
Texture2D g_Velocity : register(t7);
// Shadow map array is accessed via a separate descriptor table (space1) so
// that t0-t5 in space0 can be used for post-process textures without aliasing.
Texture2DArray g_ShadowMap : register(t0, space1);
SamplerState g_Sampler : register(s0);

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
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

    // Map NDC to UV (0..1), flipping Y
    output.uv = float2(0.5f * pos.x + 0.5f, -0.5f * pos.y + 0.5f);

    return output;
}

static const float PI = 3.14159265f;

float3 ApplyACESFilm(float3 x)
{
    // ACES fitted curve (Narkowicz 2015)
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// Downsample + bright-pass for bloom (runs at reduced resolution, sampling g_SceneColor)
float4 BloomDownsamplePS(VSOutput input) : SV_TARGET
{
    float3 hdr = g_SceneColor.Sample(g_Sampler, input.uv).rgb;

    float threshold = g_BloomParams.x;
    float softKnee  = g_BloomParams.y;

    // Soft-threshold bloom based on Unity-style formulation.
    float knee = threshold * softKnee + 1e-4f;
    float3 delta = max(hdr - threshold.xxx, 0.0f);
    float3 soft = delta * delta / (delta + knee);

    return float4(soft, 1.0f);
}

// Horizontal blur of the bloom texture (source bound at t0)
float4 BloomBlurHPS(VSOutput input) : SV_TARGET
{
    float2 texel = float2(g_PostParams.x * 4.0f, 0.0f); // quarter-res approximation
    float3 sum = 0.0f;
    float weights[5] = {0.204164f, 0.304005f, 0.093913f, 0.010381f, 0.000837f};
    sum += g_SceneColor.Sample(g_Sampler, input.uv).rgb * weights[0];
    sum += g_SceneColor.Sample(g_Sampler, input.uv + texel).rgb * weights[1];
    sum += g_SceneColor.Sample(g_Sampler, input.uv - texel).rgb * weights[1];
    sum += g_SceneColor.Sample(g_Sampler, input.uv + texel * 2.0f).rgb * weights[2];
    sum += g_SceneColor.Sample(g_Sampler, input.uv - texel * 2.0f).rgb * weights[2];
    return float4(sum, 1.0f);
}

// Vertical blur of the bloom texture (source bound at t0)
float4 BloomBlurVPS(VSOutput input) : SV_TARGET
{
    float2 texel = float2(0.0f, g_PostParams.y * 4.0f); // quarter-res approximation
    float3 sum = 0.0f;
    float weights[5] = {0.204164f, 0.304005f, 0.093913f, 0.010381f, 0.000837f};
    sum += g_SceneColor.Sample(g_Sampler, input.uv).rgb * weights[0];
    sum += g_SceneColor.Sample(g_Sampler, input.uv + texel).rgb * weights[1];
    sum += g_SceneColor.Sample(g_Sampler, input.uv - texel).rgb * weights[1];
    sum += g_SceneColor.Sample(g_Sampler, input.uv + texel * 2.0f).rgb * weights[2];
    sum += g_SceneColor.Sample(g_Sampler, input.uv - texel * 2.0f).rgb * weights[2];
    return float4(sum, 1.0f);
}

// Simple upsample/composite pass: reads from g_BloomSource and writes color
// directly. The pipeline uses additive blending when accumulating levels.
float4 BloomUpsamplePS(VSOutput input) : SV_TARGET
{
    float3 src = g_SceneColor.Sample(g_Sampler, input.uv).rgb;
    return float4(src, 1.0f);
}

// Reconstruct world-space position from depth and UV using the inverse of the
// current jittered view-projection matrix. This mirrors the mapping used in
// VSMain (NDC -> UV with Y flipped).
float3 ReconstructWorldPosition(float2 uv, float depth)
{
    // Convert UV back to clip space (matching VSMain's mapping).
    float x = uv.x * 2.0f - 1.0f;
    float y = 1.0f - 2.0f * uv.y;
    float4 clip = float4(x, y, depth, 1.0f);

    float4 world = mul(g_InvViewProjMatrix, clip);
    return world.xyz / max(world.w, 1e-4f);
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float3 hdrColor = g_SceneColor.Sample(g_Sampler, uv).rgb;

    // Screen-space reflection composite: SSR buffer stores reflection color in
    // rgb and a roughness-based weight in alpha. Apply it in HDR space so
    // reflections participate in bloom and tonemapping.
    float4 ssrSample = g_SSRColor.Sample(g_Sampler, uv);
    float ssrWeight = saturate(ssrSample.a);
    if (ssrWeight > 0.0f)
    {
        // Clamp extremely bright SSR highlights to avoid harsh color pops when
        // the ray marches across very hot pixels in the environment or scene.
        float3 ssrColor = ssrSample.rgb;
        float  ssrMax   = max(max(ssrColor.r, ssrColor.g), ssrColor.b);
        const float kMaxSSRIntensity = 32.0f;
        if (ssrMax > kMaxSSRIntensity)
        {
            ssrColor *= (kMaxSSRIntensity / ssrMax);
        }

        // Moderately limit SSR contribution so we blend towards reflections
        // without fully replacing the underlying specular/IBL term.
        const float kMaxSSRWeight = 0.4f;
        float  w = ssrWeight * kMaxSSRWeight;
        hdrColor = lerp(hdrColor, ssrColor, w);
    }

    // Bloom: sample blurred bloom texture if available
    float bloomIntensity = max(g_TimeAndExposure.w, 0.0f);
    float3 bloom = 0.0f;
    if (bloomIntensity > 0.001f) {
        bloom = g_BloomSource.Sample(g_Sampler, uv).rgb * bloomIntensity;

        // Clamp bloom contribution to avoid overly blown-out highlights.
        float maxBloom = max(g_BloomParams.z, 0.0f);
        if (maxBloom > 0.0f)
        {
            bloom = min(bloom, maxBloom.xxx);
        }
    }

    // Start from base HDR lighting (without bloom); motion blur (when enabled)
    // operates on this term only so bloom and grading stay stable.
    float3 hdrBlurred = hdrColor;

    // Simple motion blur based on velocity buffer (camera-only) in HDR space.
    // Currently gated by the same enable flag as TAA (g_TAAParams.w) so that
    // motion vectors and temporal filtering always move together.
    if (g_TAAParams.w > 0.5f)
    {
        float2 vel = g_Velocity.Sample(g_Sampler, uv).xy;
        float  speed = length(vel);
        // Keep blur radius modest to avoid sampling across large portions of
        // the screen; high-speed motion will still get some streaking, but
        // we bias towards stability over extremely strong blur.
        float  blurStrength = saturate(speed * 4.0f);

        if (blurStrength > 0.001f)
        {
            float2 dir = vel / max(speed, 1e-4f);
            const int blurSamples = 5;
            float3 accum = hdrBlurred;
            float  total = 1.0f;
            float3 lumaWeights = float3(0.299f, 0.587f, 0.114f);
            float  centerLum = dot(hdrBlurred, lumaWeights);

            [unroll]
            for (int i = 1; i < blurSamples; ++i)
            {
                float t = (float)i / (float)(blurSamples - 1);
                float2 offset = dir * blurStrength * (t - 0.5f);
                float2 sampleUV = saturate(uv + offset);
                float3 sampleHdr = g_SceneColor.Sample(g_Sampler, sampleUV).rgb;
                // Down-weight samples whose luminance differs strongly from
                // the center; this reduces hue shifts when crossing very
                // bright or very dark edges.
                float sampleLum = dot(sampleHdr, lumaWeights);
                float lumDiff = abs(sampleLum - centerLum);
                float  w = saturate(1.0f - lumDiff * 0.25f);
                accum += sampleHdr * w;
                total += w;
            }

            hdrBlurred = accum / max(total, 1e-4f);
        }
    }

    // Exponential height fog in HDR space, using the depth buffer and
    // reconstructed world position. Applied before bloom so fogged highlights
    // still contribute naturally to bloom.
    if (g_FogParams.w > 0.5f)
    {
        float depth = g_Depth.Sample(g_Sampler, uv).r;
        if (depth < 1.0f - 1e-4f)
        {
            float3 worldPos = ReconstructWorldPosition(uv, depth);
            float3 camPos = g_CameraPosition.xyz;

            float distance = length(worldPos - camPos);
            float density = max(g_FogParams.x, 0.0f);

            // Base exponential distance fog
            float fog = 1.0f - exp(-density * distance);

            // Optional height falloff so fog thickens near a reference plane.
            float baseHeight = g_FogParams.y;
            float falloff = max(g_FogParams.z, 0.0f);
            float h = worldPos.y - baseHeight;
            // Fog weaker above the base height, stronger below.
            float heightFactor = exp(-falloff * max(h, 0.0f));

            fog *= heightFactor;
            fog = saturate(fog);

            float3 fogColor = g_AmbientColor.rgb;
            hdrBlurred = lerp(hdrBlurred, fogColor, fog);
        }
    }

    // Sun god-rays (crepuscular rays) in HDR space. These are only applied
    // when fog is active so that beams have a plausible medium to scatter in.
    if (g_FogParams.w > 0.5f && g_LightCount.x > 0)
    {
        // Treat the first directional light as the sun.
        Light sun = g_Lights[0];
        uint sunType = (uint)sun.position_type.w;
        if (sunType == 0) // LIGHT_TYPE_DIRECTIONAL
        {
            float3 lightDirWS = normalize(sun.direction_cosInner.xyz);
            // Direction from camera towards the sun (opposite of light direction).
            float3 sunDirWS = -lightDirWS;

            float3 camPos = g_CameraPosition.xyz;
            float3 sunWorld = camPos + sunDirWS * 1000.0f;

            float4 sunClip = mul(g_ViewProjectionMatrix, float4(sunWorld, 1.0f));
            if (sunClip.w > 0.0f)
            {
                float2 sunNdc = sunClip.xy / sunClip.w;
                float2 sunUV;
                sunUV.x = sunNdc.x * 0.5f + 0.5f;
                sunUV.y = 0.5f - sunNdc.y * 0.5f;

                // Only bother if the projected sun is at least roughly on-screen.
                if (sunUV.x > -0.2f && sunUV.x < 1.2f &&
                    sunUV.y > -0.2f && sunUV.y < 1.2f)
                {
                    const int NUM_SAMPLES = 16;
                    float2 toSun = sunUV - uv;
                    float distToSun = length(toSun);

                    // Skip pixels very far from the sun projection to keep cost down.
                    if (distToSun > 0.02f)
                    {
                        float2 step = toSun / (float)NUM_SAMPLES;
                        float2 sampleUV = uv;

                        float3 godAccum = 0.0f;
                        float illumination = 1.0f;

                        float density = max(g_FogParams.x, 0.0f);
                        // Base intensity tied to fog density so thicker fog yields
                        // stronger, more visible beams.
                        float baseIntensity = saturate(density * 20.0f);
                        float decay = 0.92f;

                        [unroll]
                        for (int i = 0; i < NUM_SAMPLES; ++i)
                        {
                            sampleUV += step;
                            if (sampleUV.x < 0.0f || sampleUV.x > 1.0f ||
                                sampleUV.y < 0.0f || sampleUV.y > 1.0f)
                            {
                                break;
                            }

                            float d = g_Depth.SampleLevel(g_Sampler, sampleUV, 0).r;
                            // Treat fully-clear depth as sky; anything else occludes.
                            float unoccluded = (d >= 1.0f - 1e-3f) ? 1.0f : 0.0f;
                            illumination *= lerp(0.0f, 1.0f, unoccluded * decay);

                            float3 sampleHdr = g_SceneColor.SampleLevel(g_Sampler, sampleUV, 0).rgb;
                            float lum = dot(sampleHdr, float3(0.299f, 0.587f, 0.114f));
                            godAccum += lum.xxx * illumination;
                        }

                        float falloff = saturate(1.0f - distToSun * 1.5f);
                        float3 godColor = g_AmbientColor.rgb;
                        float3 godRays = godColor * godAccum * baseIntensity * falloff / (float)NUM_SAMPLES;

                        // Clamp to avoid excessive streak brightness.
                        godRays = min(godRays, 4.0f.xxx);

                        hdrBlurred += godRays;
                    }
                }
            }
        }
    }

    // Compose bloom after any motion blur and fog so blurred highlights remain
    // physically plausible and color-stable.
    float3 hdrCombined = hdrBlurred + bloom;

    // Clamp HDR before tonemapping to avoid extreme spikes that can show up
    // as sudden RGB flashes when moving the camera across very bright areas.
    const float kMaxHdrBeforeTonemap = 32.0f;
    hdrCombined = min(hdrCombined, kMaxHdrBeforeTonemap.xxx);

    float exposure = max(g_TimeAndExposure.z, 0.01f);
    float3 color = hdrCombined * exposure;

    color = ApplyACESFilm(color);
    color = pow(color, 1.0f / 2.2f);

    // Simple warm/cool grading driven by g_ColorGrade.xy.
    // Positive warm shifts towards orange, positive cool shifts towards blue.
    float warm = saturate(0.5f + g_ColorGrade.x * 0.5f); // map [-1,1] -> [0,1]
    float cool = saturate(0.5f + g_ColorGrade.y * 0.5f);
    float3 warmTint = lerp(float3(1.0f, 1.0f, 1.0f), float3(1.05f, 1.0f, 0.95f), warm);
    float3 coolTint = lerp(float3(1.0f, 1.0f, 1.0f), float3(0.96f, 1.0f, 1.05f), cool);
    color *= warmTint * coolTint;

    // Screen-space ambient occlusion modulation (applied after tonemapping/grading).
    float ao = 1.0f;
    if (g_AOParams.x > 0.5f)
    {
        // Simple 3x3 blur over the SSAO buffer to reduce noise and banding.
        float2 texel = g_PostParams.xy;
        float sum = 0.0f;
        float weight = 1.0f / 9.0f;
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            [unroll]
            for (int x = -1; x <= 1; ++x)
            {
                float2 offset = float2(x, y) * texel;
                sum += g_SSAO.Sample(g_Sampler, uv + offset).r * weight;
            }
        }
        ao = saturate(sum);
        float aoIntensity = saturate(g_AOParams.w);
        color *= lerp(1.0f, ao, aoIntensity);
    }

    // Temporal AA with depth-based reprojection. We reproject the current
    // pixel into the previous frame using g_PrevViewProjMatrix and the
    // inverse of the current view-projection, then apply the jitter delta
    // stored in g_TAAParams.xy. When depth is invalid, we fall back to a
    // simple jitter-based history lookup. History weighting is reduced in
    // regions with high motion to limit ghosting.
    float taaBlend = (g_TAAParams.w > 0.5f) ? g_TAAParams.z : 0.0f;
    if (taaBlend > 0.0f)
    {
        // Start with jitter-based UV as a robust fallback.
        float2 historyUV = uv + g_TAAParams.xy;

        // Sample depth; skip reprojection for background/cleared pixels.
        float depth = g_Depth.SampleLevel(g_Sampler, uv, 0).r;
        if (depth > 0.0f && depth < 1.0f - 1e-4f)
        {
            float3 worldPos = ReconstructWorldPosition(uv, depth);
            float4 prevClip = mul(g_PrevViewProjMatrix, float4(worldPos, 1.0f));

            if (abs(prevClip.w) > 1e-4f)
            {
                float invW = 1.0f / prevClip.w;
                float2 prevNdc = prevClip.xy * invW;

                // Map back to UV space, keeping the same NDC->UV convention
                // as VSMain (Y inverted).
                float2 prevUV;
                prevUV.x = prevNdc.x * 0.5f + 0.5f;
                prevUV.y = 0.5f - prevNdc.y * 0.5f;

                // Only accept reprojection when it lands on-screen.
                if (prevUV.x >= 0.0f && prevUV.x <= 1.0f &&
                    prevUV.y >= 0.0f && prevUV.y <= 1.0f)
                {
                    historyUV = prevUV + g_TAAParams.xy;
                }
            }
        }

        historyUV = saturate(historyUV);
        float3 history = g_HistoryColor.Sample(g_Sampler, historyUV).rgb;

        // Motion-aware blending: scale history contribution down when velocity is high.
        float2 velTaa = g_Velocity.Sample(g_Sampler, uv).xy;
        float speedTaa = length(velTaa);
        float motionFactor = saturate(speedTaa * 40.0f); // same scale as blur
        float finalBlend = saturate(taaBlend * (1.0f - motionFactor));

        // Reduce history weight when the color difference is very large; this
        // helps avoid strong hue "smears" when the view or lighting changes
        // abruptly between frames.
        float3 diff = abs(color - history);
        float maxDiff = max(max(diff.r, diff.g), diff.b);
        float diffFactor = saturate(1.0f - maxDiff * 2.0f);
        finalBlend *= diffFactor;

        color = lerp(color, history, finalBlend);
    }

    // Optional FXAA-like smoothing (very lightweight approximation)
    if (g_PostParams.z > 0.5f)
    {
        float2 texel = g_PostParams.xy;
        float3 cM = color;
        float3 cR = g_SceneColor.Sample(g_Sampler, uv + float2(texel.x, 0.0f)).rgb;
        float3 cL = g_SceneColor.Sample(g_Sampler, uv - float2(texel.x, 0.0f)).rgb;
        float3 cU = g_SceneColor.Sample(g_Sampler, uv - float2(0.0f, texel.y)).rgb;
        float3 cD = g_SceneColor.Sample(g_Sampler, uv + float2(0.0f, texel.y)).rgb;

        float3 lumaWeights = float3(0.299f, 0.587f, 0.114f);
        float lumM = dot(cM, lumaWeights);
        float lumR = dot(cR, lumaWeights);
        float lumL = dot(cL, lumaWeights);
        float lumU = dot(cU, lumaWeights);
        float lumD = dot(cD, lumaWeights);

        float contrast = max(max(abs(lumM - lumR), abs(lumM - lumL)),
                             max(abs(lumM - lumU), abs(lumM - lumD)));

        // Only smooth when local contrast is noticeable
        if (contrast > 0.05f)
        {
            float3 avg = (cM + cR + cL + cU + cD) * (1.0f / 5.0f);
            color = lerp(cM, avg, 0.6f);
        }
    }

    // SSAO / SSR debug views in post-process so tuning radius/bias/intensity is easier.
    if (g_DebugMode.x == 13.0f)
    {
        // AO only
        return float4(ao.xxx, 1.0f);
    }
    else if (g_DebugMode.x == 14.0f)
    {
        // AO overlay: visualize occlusion on top of final color
        float3 overlay = color * lerp(1.0f, ao, 0.75f);
        return float4(saturate(overlay), 1.0f);
    }
    else if (g_DebugMode.x == 15.0f)
    {
        // SSR-only view (pre-tonemap reflections buffer).
        float3 ssr = g_SSRColor.Sample(g_Sampler, uv).rgb;
        return float4(ssr, 1.0f);
    }
    else if (g_DebugMode.x == 16.0f)
    {
        // SSR overlay: visualize reflections on top of final color.
        float3 ssr = g_SSRColor.Sample(g_Sampler, uv).rgb;
        float3 overlay = color * 0.5f + ssr * 0.5f;
        return float4(saturate(overlay), 1.0f);
    }

    // Shadow map cascade visualization in the top-right corner, only when
    // debug screen mode is active (g_DebugMode.x == 6). We show all three
    // cascades side-by-side in a 40% width x 40% height box, with an
    // inverted depth mapping to make geometry stand out (near = bright).
    if (g_DebugMode.x == 6 && uv.x > 0.6f && uv.y < 0.4f)
    {
        float2 local = float2((uv.x - 0.6f) / 0.4f, uv.y / 0.4f);
        float tiles = 3.0f;
        float scaledX = local.x * tiles;
        uint cascadeIndex = (uint)scaledX;
        cascadeIndex = min(cascadeIndex, 2u);
        float2 tileUV = float2(frac(scaledX), local.y);

        float depth = g_ShadowMap.Sample(g_Sampler, float3(tileUV, cascadeIndex)).r;

        // Depth is in [0,1] with 1 = far plane (clear). Invert and scale
        // to emphasize surfaces near the light; empty space stays dark.
        float invDepth = saturate((1.0f - depth) * 6.0f);
        float3 depthVis = invDepth.xxx;
        color = depthVis;
    }

    return float4(saturate(color), 1.0f);
}
