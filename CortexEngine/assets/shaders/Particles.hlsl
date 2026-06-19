// Simple CPU-driven particle shader rendered as camera-facing billboards.
// Shares the same FrameConstants layout as Basic.hlsl / PostProcess.hlsl so
// it can reuse the main root signature and frame constant buffer.

cbuffer ObjectConstants : register(b0)
{
    float4x4 g_ModelMatrix;
    float4x4 g_NormalMatrix;
    float g_DepthBiasNdc;
    float3 _pad0;
};

cbuffer FrameConstants : register(b1)
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
    Light    g_Lights[LIGHT_MAX];
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
};

struct VSInput
{
    // Base quad in local space (centered at origin, unit size)
    float3 position : POSITION;
    float2 uv       : TEXCOORD0;
    // Per-instance data
    float3 instancePos : TEXCOORD1;
    float  size        : TEXCOORD2;
    float4 color       : COLOR0;
    float4 params      : TEXCOORD3;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
    float3 worldPos : TEXCOORD1;
    float  viewDepth : TEXCOORD2;
    float4 params   : TEXCOORD3;
};

PSInput VSMain(VSInput input)
{
    PSInput output;

    // Derive camera right/up vectors from the view matrix so particles face
    // the camera regardless of its orientation.
    float3 camRight = normalize(float3(g_ViewMatrix[0].x, g_ViewMatrix[1].x, g_ViewMatrix[2].x));
    float3 camUp    = normalize(float3(g_ViewMatrix[0].y, g_ViewMatrix[1].y, g_ViewMatrix[2].y));

    float halfSize = 0.5f * input.size;
    float3 offset = (input.position.x * camRight + input.position.y * camUp) * halfSize * 2.0f;

    float3 worldPos = input.instancePos + offset;
    float4 clipPos = mul(g_ViewProjectionMatrix, float4(worldPos, 1.0f));
    clipPos.z += g_DepthBiasNdc * clipPos.w;

    output.position = clipPos;
    output.uv = input.uv;
    output.color = input.color;
    output.worldPos = worldPos;
    float4 viewPos = mul(g_ViewMatrix, float4(worldPos, 1.0f));
    output.viewDepth = max(viewPos.z, 0.0f);
    output.params = input.params;
    return output;
}

static const uint PARTICLE_SMOKE  = 0u;
static const uint PARTICLE_FIRE   = 1u;
static const uint PARTICLE_DUST   = 2u;
static const uint PARTICLE_SPARKS = 3u;
static const uint PARTICLE_EMBERS = 4u;
static const uint PARTICLE_MIST   = 5u;
static const uint LIGHT_TYPE_DIRECTIONAL = 0u;
static const uint LIGHT_TYPE_POINT       = 1u;
static const uint LIGHT_TYPE_SPOT        = 2u;
static const uint LIGHT_TYPE_AREA_RECT   = 3u;

float Luma(float3 c)
{
    return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

float Hash21(float2 p)
{
    return frac(sin(dot(p, float2(127.1f, 311.7f))) * 43758.5453f);
}

float LightVisibilityAtParticle(float3 worldPos, float3 viewDir, uint particleType, out float sparkle)
{
    float3 lighting = max(g_AmbientColor.rgb, 0.0f.xxx) * ((particleType == PARTICLE_DUST || particleType == PARTICLE_MIST) ? 0.65f : 0.28f);
    float forwardScatter = 0.0f;
    sparkle = 0.0f;

    const uint count = min(g_LightCount.x, LIGHT_MAX);
    [loop]
    for (uint i = 0u; i < count; ++i)
    {
        Light light = g_Lights[i];
        const uint type = (uint)round(light.position_type.w);
        float3 toLightDir = 0.0f.xxx;
        float attenuation = 1.0f;

        if (type == LIGHT_TYPE_POINT || type == LIGHT_TYPE_SPOT || type == LIGHT_TYPE_AREA_RECT)
        {
            float3 toLight = light.position_type.xyz - worldPos;
            float dist = length(toLight);
            if (dist <= 1.0e-4f)
            {
                continue;
            }
            toLightDir = toLight / dist;
            float range = max(light.color_range.w, 0.001f);
            float falloff = saturate(1.0f - dist / range);
            attenuation = falloff * falloff;

            if (type == LIGHT_TYPE_SPOT)
            {
                float3 spotDir = normalize(light.direction_cosInner.xyz);
                float cosTheta = dot(-toLightDir, spotDir);
                float cosInner = light.direction_cosInner.w;
                float cosOuter = light.params.x;
                float spotFactor = saturate((cosTheta - cosOuter) / max(cosInner - cosOuter, 1.0e-4f));
                attenuation *= spotFactor * spotFactor;
            }
            else if (type == LIGHT_TYPE_AREA_RECT)
            {
                attenuation = max(attenuation, 0.34f);
            }
        }
        else
        {
            toLightDir = normalize(light.direction_cosInner.xyz);
        }

        float phase = pow(saturate(dot(viewDir, -toLightDir)) * 0.5f + 0.5f, 4.0f);
        float3 radiance = max(light.color_range.rgb, 0.0f.xxx) * attenuation;
        lighting += radiance * lerp(0.10f, 0.55f, phase);
        forwardScatter += Luma(radiance) * phase;
    }

    float shaft = saturate(forwardScatter * ((particleType == PARTICLE_DUST) ? 0.26f : 0.12f));
    sparkle = shaft;
    return saturate(0.22f + Luma(lighting) * 0.20f + shaft);
}

float4 PSMain(PSInput input) : SV_TARGET
{
    const uint particleType = (uint)round(input.params.y);

    // Smooth procedural sprite: no square billboard edges, plus a soft core for motes.
    float2 centered = input.uv * 2.0f - 1.0f;
    float  r2 = dot(centered, centered);
    float  edge = 1.0f - smoothstep(0.42f, 1.0f, r2);
    float  core = exp2(-r2 * ((particleType == PARTICLE_DUST || particleType == PARTICLE_SPARKS) ? 7.5f : 2.2f));
    float  alphaFalloff = saturate(max(edge * 0.72f, core));

    float3 viewDir = normalize(g_CameraPosition.xyz - input.worldPos);
    float sparkle = 0.0f;
    float lightFactor = LightVisibilityAtParticle(input.worldPos, viewDir, particleType, sparkle);

    float4 color = input.color;
    color.a *= alphaFalloff;

    // View-depth fade keeps particles soft around the camera and distant fog band.
    float nearFade = saturate((input.viewDepth - 0.18f) / 1.15f);
    float farFade = saturate((220.0f - input.viewDepth) / 70.0f);
    color.a *= nearFade * farFade;

    if (particleType == PARTICLE_DUST)
    {
        float twinkle = 0.58f + 0.42f * sin(g_TimeAndExposure.x * 4.7f + input.params.w * 37.0f + Hash21(input.position.xy) * 6.28318f);
        float sparkleBoost = sparkle * twinkle;
        color.rgb *= 0.44f + lightFactor * 1.30f + sparkleBoost * 1.75f;
        color.a *= saturate(0.08f + lightFactor * 0.62f);
    }
    else if (particleType == PARTICLE_MIST || particleType == PARTICLE_SMOKE)
    {
        color.rgb *= 0.42f + lightFactor * 0.85f;
        color.a *= (particleType == PARTICLE_MIST) ? 0.56f : 0.72f;
    }
    else if (particleType == PARTICLE_SPARKS || particleType == PARTICLE_EMBERS || particleType == PARTICLE_FIRE)
    {
        float flicker = 0.70f + 0.30f * sin(g_TimeAndExposure.x * 18.0f + input.params.w * 43.0f);
        color.rgb *= flicker * ((particleType == PARTICLE_SPARKS) ? 2.6f : 1.7f);
        color.a *= (particleType == PARTICLE_SPARKS) ? 0.86f : 0.72f;
    }
    else
    {
        color.rgb *= 0.55f + lightFactor;
    }

    // Optional debug mode to visualize particles only.
    if ((uint)g_DebugMode.x == 31u)
    {
        return color;
    }

    return color;
}
