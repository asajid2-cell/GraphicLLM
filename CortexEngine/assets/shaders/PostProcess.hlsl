// Fullscreen post-process: exposure, ACES tonemapping, gamma, simple bloom stub

cbuffer FrameConstants : register(b1)
{
    float4x4 g_ViewMatrix;
    float4x4 g_ProjectionMatrix;
    float4x4 g_ViewProjectionMatrix;
    float4 g_CameraPosition;
    // x = time, y = deltaTime, z = exposure, w = bloom intensity
    float4 g_TimeAndExposure;
    // x = 1 / screenWidth, y = 1 / screenHeight, z = FXAA enabled (>0.5), w reserved
    float4 g_PostParams;
};

Texture2D g_SceneColor : register(t0);
Texture2D g_BloomSource : register(t1);
Texture2DArray g_ShadowMap : register(t4);
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
    float3 bright = max(hdr - 1.0f, 0.0f);
    return float4(bright, 1.0f);
}

// Horizontal blur of the bloom texture in g_BloomSource
float4 BloomBlurHPS(VSOutput input) : SV_TARGET
{
    float2 texel = float2(g_PostParams.x * 4.0f, 0.0f); // quarter-res approximation
    float3 sum = 0.0f;
    float weights[5] = {0.204164f, 0.304005f, 0.093913f, 0.010381f, 0.000837f};
    sum += g_BloomSource.Sample(g_Sampler, input.uv).rgb * weights[0];
    sum += g_BloomSource.Sample(g_Sampler, input.uv + texel).rgb * weights[1];
    sum += g_BloomSource.Sample(g_Sampler, input.uv - texel).rgb * weights[1];
    sum += g_BloomSource.Sample(g_Sampler, input.uv + texel * 2.0f).rgb * weights[2];
    sum += g_BloomSource.Sample(g_Sampler, input.uv - texel * 2.0f).rgb * weights[2];
    return float4(sum, 1.0f);
}

// Vertical blur of the bloom texture in g_BloomSource
float4 BloomBlurVPS(VSOutput input) : SV_TARGET
{
    float2 texel = float2(0.0f, g_PostParams.y * 4.0f); // quarter-res approximation
    float3 sum = 0.0f;
    float weights[5] = {0.204164f, 0.304005f, 0.093913f, 0.010381f, 0.000837f};
    sum += g_BloomSource.Sample(g_Sampler, input.uv).rgb * weights[0];
    sum += g_BloomSource.Sample(g_Sampler, input.uv + texel).rgb * weights[1];
    sum += g_BloomSource.Sample(g_Sampler, input.uv - texel).rgb * weights[1];
    sum += g_BloomSource.Sample(g_Sampler, input.uv + texel * 2.0f).rgb * weights[2];
    sum += g_BloomSource.Sample(g_Sampler, input.uv - texel * 2.0f).rgb * weights[2];
    return float4(sum, 1.0f);
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float3 hdrColor = g_SceneColor.Sample(g_Sampler, uv).rgb;

    // Bloom: sample blurred bloom texture if available
    float bloomIntensity = max(g_TimeAndExposure.w, 0.0f);
    float3 bloom = 0.0f;
    if (bloomIntensity > 0.001f) {
        bloom = g_BloomSource.Sample(g_Sampler, uv).rgb * bloomIntensity;
    }

    float exposure = max(g_TimeAndExposure.z, 0.01f);
    float3 color = (hdrColor + bloom) * exposure;

    color = ApplyACESFilm(color);
    color = pow(color, 1.0f / 2.2f);

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

    // Shadow map cascade visualization in the top-right corner.
    // We show all three cascades side-by-side in a 40% width x 40% height box.
    if (uv.x > 0.6f && uv.y < 0.4f)
    {
        float2 local = float2((uv.x - 0.6f) / 0.4f, uv.y / 0.4f);
        float tiles = 3.0f;
        float scaledX = local.x * tiles;
        uint cascadeIndex = (uint)scaledX;
        cascadeIndex = min(cascadeIndex, 2u);
        float2 tileUV = float2(frac(scaledX), local.y);

        float depth = g_ShadowMap.Sample(g_Sampler, float3(tileUV, cascadeIndex)).r;
        float3 depthVis = depth.xxx;
        color = depthVis;
    }

    return float4(saturate(color), 1.0f);
}
