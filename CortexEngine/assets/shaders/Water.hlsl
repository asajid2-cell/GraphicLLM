// Basic water surface shader.
// This uses the same constant buffer layout as Basic.hlsl so it can share
// the main root signature and frame constants.

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
    float4   g_FogExtraParams;
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
    // z = secondary amplitude, w = steepness (0..1)
    float4   g_WaterParams1;
};

cbuffer MaterialConstants : register(b2)
{
    float4 g_Albedo;
    float  g_Metallic;
    float  g_Roughness;
    float  g_AO;
    float  g_MaterialPad0;
    uint4  g_TextureIndices;
    uint4  g_MapFlags;
    uint4  g_TextureIndices2;
    uint4  g_MapFlags2;
    float4 g_EmissiveFactorStrength;
    float4 g_ExtraParams;
    float4 g_FractalParams0;
    float4 g_FractalParams1;
    float4 g_FractalParams2;
    float4 g_CoatParams;
    float4 g_TransmissionParams;
    float4 g_SpecularParams;
    uint4  g_TextureIndices3;
    uint4  g_TextureIndices4;
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

    // Directional Gerstner-style waves using the shared water parameters.
    float amplitude = g_WaterParams0.x;
    float waveLen   = max(g_WaterParams0.y, 0.1f);
    float speed     = g_WaterParams0.z;
    float waterY    = g_WaterParams0.w;

    float2 dir      = normalize(float2(g_WaterParams1.x, g_WaterParams1.y));
    float secondaryAmp = g_WaterParams1.z;
    // Overall steepness for horizontal displacement (0 = purely vertical
    // sine waves, ~0.6 = moderately choppy). This is kept modest so that
    // buoyancy queries using the CPU mirror remain visually consistent.
    float steepness = saturate(g_WaterParams1.w);

    float k = 2.0f * PI / waveLen;
    float t = g_TimeAndExposure.x;

    // Base (undisplaced) horizontal position in world space.
    float2 xzBase = worldPos.xz;
    float phase0 = dot(dir, xzBase) * k + speed * t;

    float2 dir2 = float2(-dir.y, dir.x);
    float phase1 = dot(dir2, xzBase) * k * 1.3f + speed * 0.8f * t;

    // Vertical displacement preserves the authored surface transform and adds
    // the global water level as a scene-wide offset.
    float h0 = amplitude * sin(phase0);
    float h1 = secondaryAmp * sin(phase1);
    float height = h0 + h1;

    // Gerstner-style horizontal chop for richer silhouettes. We keep the
    // steepness relatively low and base the displacement on xzBase so the
    // surface still behaves like a height field for gameplay.
    float Qa0 = steepness * amplitude;
    float Qa1 = steepness * secondaryAmp;

    float2 disp0 = Qa0 * float2(dir.x * cos(phase0), dir.y * cos(phase0));
    float2 disp1 = Qa1 * float2(dir2.x * cos(phase1), dir2.y * cos(phase1));
    float2 xzDisplaced = xzBase + disp0 + disp1;

    worldPos.xz = xzDisplaced;
    worldPos.y = worldPos.y + waterY + height;
    output.worldPos = worldPos.xyz;

    float4 clipPos = mul(g_ViewProjectionMatrix, worldPos);
    clipPos.z += g_DepthBiasNdc * clipPos.w;
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

float Hash21(float2 p)
{
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

float ValueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0f - 2.0f * f);
    float a = Hash21(i);
    float b = Hash21(i + float2(1.0f, 0.0f));
    float c = Hash21(i + float2(0.0f, 1.0f));
    float d = Hash21(i + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float FBM(float2 p)
{
    float value = 0.0f;
    float amp = 0.5f;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        value += ValueNoise(p) * amp;
        p = p * 2.07f + 17.13f;
        amp *= 0.5f;
    }
    return value;
}

float4 WaterPS(PSInput input) : SV_TARGET
{
    float3 N = normalize(input.normal);
    float3 V = normalize(g_CameraPosition.xyz - input.worldPos);

    uint debugView = (uint)g_DebugMode.x;
    if (debugView == 29u)
    {
        // Water debug: visualize wave height, slope magnitude, and foam ramp.
        float heightVis = saturate(input.waveHeight * 0.5f + 0.5f);
        float slope = input.slopeMag;
        float foamRamp = saturate((slope - 0.08f) * 4.0f);
        return float4(heightVis, slope, foamRamp, 1.0f);
    }

    uint liquidType = (uint)round(g_ExtraParams.w);
    float absorption = saturate(g_ExtraParams.x);
    float foamStrength = saturate(g_ExtraParams.y);
    float viscosity = saturate(g_ExtraParams.z);
    float emissiveHeat = max(g_FractalParams1.w, g_TransmissionParams.z);

    // Keep water relatively smooth by default; the hybrid SSR/RT reflection
    // pass adds the high-frequency mirror component. Viscous liquids keep broad,
    // slower highlights instead of noisy ripples.
    float roughness = max(g_Roughness, lerp(0.03f, 0.11f, viscosity));
    float metallic  = 0.0f;

    float3 shallowProfile = max(g_FractalParams0.rgb, 0.0f);
    float3 deepProfile = max(g_FractalParams1.rgb, 0.0f);
    if (dot(shallowProfile, float3(1.0f, 1.0f, 1.0f)) <= 0.001f)
    {
        shallowProfile = float3(0.10f, 0.50f, 0.78f);
    }
    if (dot(deepProfile, float3(1.0f, 1.0f, 1.0f)) <= 0.001f)
    {
        deepProfile = float3(0.005f, 0.07f, 0.22f);
    }

    float2 liquidUv = input.texCoord;
    float edgeDistance = min(min(liquidUv.x, 1.0f - liquidUv.x), min(liquidUv.y, 1.0f - liquidUv.y));
    float edgeFoam = saturate((0.085f - edgeDistance) * 13.0f);
    float basinDepth = saturate(edgeDistance * 2.6f);
    float waveDepth = saturate(input.waveHeight * 0.30f + 0.55f);
    float depthMix = saturate(lerp(basinDepth, waveDepth, 0.28f) + absorption * 0.22f);

    float t = g_TimeAndExposure.x;
    float flowNoise = FBM(input.worldPos.xz * lerp(0.17f, 0.32f, 1.0f - viscosity) +
                          float2(t * 0.035f, -t * 0.025f));
    float3 baseColor = lerp(shallowProfile, deepProfile, depthMix);
    baseColor = lerp(baseColor, g_Albedo.rgb, liquidType == 0u ? 0.16f : 0.08f);

    float fresnelStrength = clamp(g_SpecularParams.x, 0.0f, 3.0f);
    if (fresnelStrength <= 0.0f)
    {
        fresnelStrength = 1.0f;
    }
    float3 F0 = lerp(float3(0.02f, 0.02f, 0.02f) * fresnelStrength, baseColor, metallic);

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

    float3 depthTint = baseColor;
    if (liquidType == 0u)
    {
        float skyFacing = saturate(N.y * 0.65f + pow(1.0f - NdotV, 2.0f) * 0.35f);
        depthTint = lerp(baseColor, float3(0.06f, 0.34f, 0.68f), 0.20f * skyFacing);
    }
    else if (liquidType == 1u)
    {
        float veins = smoothstep(0.48f, 0.92f, FBM(input.worldPos.xz * 1.15f + float2(t * 0.10f, -t * 0.06f)));
        float crust = smoothstep(0.22f, 0.72f, FBM(input.worldPos.xz * 2.35f - float2(t * 0.035f, t * 0.025f)));
        float3 hot = float3(1.0f, 0.42f, 0.055f) * (1.0f + veins * 1.45f);
        float3 crustColor = float3(0.09f, 0.025f, 0.012f);
        depthTint = lerp(hot, crustColor, crust * 0.72f);
    }
    else if (liquidType == 2u)
    {
        float swirl = FBM(input.worldPos.xz * 0.82f + float2(t * 0.018f, t * 0.012f));
        depthTint = lerp(float3(1.0f, 0.68f, 0.16f), float3(0.56f, 0.25f, 0.035f), depthMix);
        depthTint = lerp(depthTint, float3(1.0f, 0.86f, 0.32f), swirl * 0.25f);
    }
    else
    {
        float ribbon = FBM(input.worldPos.xz * 0.60f + float2(-t * 0.010f, t * 0.016f));
        depthTint = lerp(float3(0.18f, 0.075f, 0.025f), float3(0.018f, 0.008f, 0.004f), depthMix);
        depthTint += ribbon * 0.035f;
    }

    // Foam: use local slope magnitude as a heuristic for wave crests. Higher
    // slopes get more foam; edge foam makes pools and shorelines read clearly.
    float slope = input.slopeMag;
    float foamRamp = saturate((slope - 0.06f) * 5.0f);
    float3 foamColor = liquidType == 0u ? float3(0.90f, 0.97f, 1.0f) :
                       (liquidType == 2u ? float3(1.0f, 0.82f, 0.34f) : depthTint);

    float3 ambient = g_AmbientColor.rgb * depthTint;
    float3 color = ambient + lighting;

    // Blend foam over the lit surface; modulate slightly by viewing angle so
    // foam is more visible at grazing angles.
    float foamViewBoost = pow(1.0f - NdotV, 2.0f);
    float foamAmount = saturate((foamRamp + edgeFoam * 0.85f) * foamStrength * (0.6f + 0.4f * foamViewBoost));
    color = lerp(color, foamColor, foamAmount);

    float3 fresnelGlow = F * pow(1.0f - NdotV, 1.5f) * (liquidType == 0u ? float3(0.14f, 0.32f, 0.55f) : depthTint);
    color += fresnelGlow;

    if (liquidType == 0u)
    {
        float3 blueBody = depthTint * (0.95f + 0.25f * NdotL) + float3(0.0f, 0.055f, 0.14f);
        color = lerp(color, blueBody + specular * 0.35f, 0.48f);
    }
    else if (liquidType == 1u)
    {
        float heatPulse = 0.65f + 0.35f * sin(t * 1.7f + flowNoise * 6.28318f);
        color += depthTint * max(emissiveHeat, 1.0f) * heatPulse;
    }
    else if (liquidType == 2u)
    {
        color = lerp(color, depthTint * 1.22f + specular * 0.25f, 0.62f);
        color += float3(0.18f, 0.11f, 0.02f) * (1.0f - depthMix) * (0.35f + flowNoise * 0.25f);
    }
    else if (liquidType == 3u)
    {
        color = max(color, float3(0.018f, 0.010f, 0.006f));
        color += specular * 0.75f;
    }

    float alpha = g_Albedo.a;
    if (liquidType == 0u)
    {
        alpha = max(alpha, 0.88f);
    }
    else if (liquidType == 1u)
    {
        alpha = 1.0f;
    }
    else if (liquidType == 2u)
    {
        alpha = max(alpha, 0.90f);
    }
    else
    {
        alpha = max(alpha, 0.95f);
    }

    return float4(color, alpha);
}
