// Basic water surface shader.
// This uses the same constant buffer layout as Basic.hlsl so it can share
// the main root signature and frame constants.

cbuffer ObjectConstants : register(b0)
{
    float4x4 g_ModelMatrix;
    float4x4 g_NormalMatrix;
};

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
    float4   g_AOParams;
    float4   g_BloomParams;
    float4   g_TAAParams;
    float4x4 g_ViewProjectionNoJitter;
    float4x4 g_InvViewProjectionNoJitter;
    float4x4 g_PrevViewProjMatrix;
    float4x4 g_InvViewProjMatrix;
    // x = base wave amplitude, y = base wave length,
    // z = wave speed,          w = global water level (Y)
    float4   g_WaterParams0;
    // x = primary wave dir X,  y = primary wave dir Z,
    // z = secondary amplitude, w = reserved
    float4   g_WaterParams1;
};

cbuffer MaterialConstants : register(b2)
{
    float4 g_Albedo;
    float  g_Metallic;
    float  g_Roughness;
    float  g_AO;
    uint4  g_MapFlags;
    float4 g_FractalParams0;
    float4 g_FractalParams1;
    float4 g_FractalParams2;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float4 tangent  : TANGENT;
    float2 texCoord : TEXCOORD;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : WORLD_POSITION;
    float3 normal   : NORMAL;
    float2 texCoord : TEXCOORD;
    // Wave height and local slope magnitude carried to the pixel shader for
    // depth/foam-style shading cues.
    float  waveHeight : TEXCOORD1;
    float  slopeMag   : TEXCOORD2;
};

static const float PI = 3.14159265f;

PSInput WaterVS(VSInput input)
{
    PSInput output;

    // Base world position before displacement.
    float4 worldPos = mul(g_ModelMatrix, float4(input.position, 1.0f));

    // Simple directional sine wave using the shared water parameters.
    float amplitude = g_WaterParams0.x;
    float waveLen   = max(g_WaterParams0.y, 0.1f);
    float speed     = g_WaterParams0.z;
    float waterY    = g_WaterParams0.w;

    float2 dir      = normalize(float2(g_WaterParams1.x, g_WaterParams1.y));
    float secondaryAmp = g_WaterParams1.z;

    float k = 2.0f * PI / waveLen;
    float t = g_TimeAndExposure.x;

    float2 xz = worldPos.xz;
    float phase0 = dot(dir, xz) * k + speed * t;
    float h0 = amplitude * sin(phase0);

    float2 dir2 = float2(-dir.y, dir.x);
    float phase1 = dot(dir2, xz) * k * 1.3f + speed * 0.8f * t;
    float h1 = secondaryAmp * sin(phase1);

    float height = h0 + h1;
    worldPos.y = waterY + height;
    output.worldPos = worldPos.xyz;

    float4 clipPos = mul(g_ViewProjectionMatrix, worldPos);
    output.position = clipPos;

    // Approximate normal from analytic height derivatives and cache a simple
    // slope metric for foam shading in the pixel shader.
    float c0 = cos(phase0);
    float c1 = cos(phase1);
    float dhdx = amplitude * c0 * k * dir.x + secondaryAmp * c1 * k * 1.3f * dir2.x;
    float dhdz = amplitude * c0 * k * dir.y + secondaryAmp * c1 * k * 1.3f * dir2.y;
    float3 worldNormal = normalize(float3(-dhdx, 1.0f, -dhdz));
    output.normal = worldNormal;
    output.texCoord = input.texCoord;

    output.waveHeight = height;
    // Slope magnitude in XZ; clamp to a reasonable range so the foam ramp
    // behaves predictably across different wave amplitudes.
    output.slopeMag = saturate(sqrt(dhdx * dhdx + dhdz * dhdz));

    return output;
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}

float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = (NdotH * NdotH) * (a2 - 1.0f) + 1.0f;
    return a2 / max(PI * denom * denom, 1e-4f);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    float denom = NdotV * (1.0f - k) + k;
    return NdotV / max(denom, 1e-4f);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    float ggx1 = GeometrySchlickGGX(NdotV, roughness);
    float ggx2 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

float4 WaterPS(PSInput input) : SV_TARGET
{
    float3 N = normalize(input.normal);
    float3 V = normalize(g_CameraPosition.xyz - input.worldPos);

    // Keep water relatively smooth by default; the hybrid SSR/RT reflection
    // pass adds the high-frequency mirror component.
    float roughness = max(g_Roughness, 0.03f);
    float metallic  = 0.0f;

    // Base water color: slightly tinted, low albedo. Start from a neutral
    // deep color and bias towards the material albedo so artists can nudge
    // tint without fighting the shader.
    float3 deepColor  = float3(0.01f, 0.03f, 0.06f);
    float3 baseColor  = lerp(deepColor, g_Albedo.rgb, 0.4f);

    float3 F0 = lerp(float3(0.02f, 0.02f, 0.02f), baseColor, metallic);

    // Simple directional light 0 as sun when available.
    float3 L = float3(0.0f, 1.0f, 0.0f);
    float3 radiance = 0.0f;
    if (g_LightCount.x > 0)
    {
        L = -normalize(g_Lights[0].direction_cosInner.xyz);
        radiance = g_Lights[0].color_range.rgb;
    }

    float3 H = normalize(L + V);
    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    float3 F = FresnelSchlick(VdotH, F0);
    float  D = DistributionGGX(NdotH, roughness);
    float  G = GeometrySmith(NdotV, NdotL, roughness);

    float3 numerator = D * G * F;
    float  denom = max(4.0f * NdotV * NdotL, 1e-4f);
    float3 specular = numerator / denom;

    float3 kd = (1.0f - F) * (1.0f - metallic);
    float3 diffuse = kd * baseColor / PI;

    float3 lighting = (diffuse + specular) * radiance * NdotL;

    // Depth / distance-based tint: treat water further from the camera as
    // optically deeper and bias toward the deepColor. This is a lightweight
    // approximation that does not require sampling the scene depth buffer.
    float  camDist = length(input.worldPos - g_CameraPosition.xyz);
    float  depthFactor = saturate(camDist * 0.05f); // ~0..1 over a few dozen units
    float3 shallowColor = lerp(baseColor, float3(0.15f, 0.20f, 0.22f), 0.3f);
    float3 depthTint   = lerp(shallowColor, deepColor, depthFactor);

    // Foam: use local slope magnitude as a heuristic for wave crests. Higher
    // slopes get more foam; keep the ramp soft so it blends naturally.
    float slope = input.slopeMag;
    float foamRamp = saturate((slope - 0.08f) * 4.0f);
    float3 foamColor = float3(0.90f, 0.95f, 1.0f);

    float3 ambient = g_AmbientColor.rgb * depthTint;
    float3 color = ambient + lighting;

    // Blend foam over the lit surface; modulate slightly by viewing angle so
    // foam is more visible at grazing angles.
    float foamViewBoost = pow(1.0f - NdotV, 2.0f);
    float foamAmount = saturate(foamRamp * (0.6f + 0.4f * foamViewBoost));
    color = lerp(color, foamColor, foamAmount);

    return float4(color, g_Albedo.a);
}
