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

Texture2D g_EnvSpecular : register(t2, space1);
SamplerState g_Sampler : register(s0);

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
    float bodyThickness = saturate(_pad0.x);
    float sloshStrength = saturate(_pad0.y);
    float meniscusStrength = saturate(_pad0.z);
    float flowSpeed = max(speed, 0.0f);
    float viscosity = saturate(1.0f - sloshStrength * 2.5f);

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

    // Per-liquid body motion. Thin water can slosh quickly; viscous liquids
    // move slower and keep stronger edge mass so they do not read as flat
    // transparent sheets in contained gallery basins.
    float edgeDistance = min(min(input.texCoord.x, 1.0f - input.texCoord.x),
                             min(input.texCoord.y, 1.0f - input.texCoord.y));
    float edgeMass = pow(saturate(1.0f - edgeDistance * 4.5f), 2.0f);
    float viscousMotion = lerp(1.0f, 0.22f, viscosity);
    float localPhase =
        (input.texCoord.x - 0.5f) * 7.0f +
        (input.texCoord.y - 0.5f) * 5.0f +
        t * flowSpeed * viscousMotion;
    float slosh = sin(localPhase) * cos(localPhase * 0.63f + t * 0.37f);
    height += slosh * sloshStrength * (0.025f + 0.055f * bodyThickness) * viscousMotion;
    height += edgeMass * meniscusStrength * (0.018f + 0.070f * bodyThickness);

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
    float sloshSlope = sloshStrength * viscousMotion * (0.020f + 0.035f * bodyThickness);
    dhdx += cos(localPhase) * 7.0f * sloshSlope;
    dhdz += -sin(localPhase * 0.63f + t * 0.37f) * 5.0f * sloshSlope;
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

float2 DirectionToLatLong(float3 dir)
{
    dir = normalize(dir);
    if (!all(isfinite(dir)))
    {
        dir = float3(0.0f, 0.0f, 1.0f);
    }

    float phi = atan2(-dir.z, dir.x);
    float theta = asin(clamp(dir.y, -1.0f, 1.0f));
    return float2(0.5f + phi / (2.0f * PI), 0.5f - theta / PI);
}

float EnvReflectionFootprintMip(float2 uv, float width, float height, float maxMip)
{
    float2 dx = ddx(uv);
    float2 dy = ddy(uv);
    dx.x = frac(dx.x + 0.5f) - 0.5f;
    dy.x = frac(dy.x + 0.5f) - 0.5f;

    float2 texelDx = dx * float2(width, height);
    float2 texelDy = dy * float2(width, height);
    float footprint = max(length(texelDx), length(texelDy));
    return clamp(log2(max(footprint, 1.0f)), 0.0f, maxMip);
}

float3 SampleWaterEnvironment(float3 R, float roughness)
{
    uint width, height, mipCount;
    g_EnvSpecular.GetDimensions(0, width, height, mipCount);
    float maxMip = mipCount > 0u ? float(mipCount - 1u) : 0.0f;
    float2 uv = DirectionToLatLong(R);
    float footprintMip = EnvReflectionFootprintMip(uv, (float)max(width, 1u), (float)max(height, 1u), maxMip);
    float specMip = max(roughness * maxMip, footprintMip);
    return g_EnvSpecular.SampleLevel(g_Sampler, uv, specMip).rgb * max(g_EnvParams.y * g_EnvParams.w, 0.0f);
}

float3 LiquidReflectionPalette(float3 R,
                               uint liquidType,
                               float flowNoise,
                               float3 ambientTint,
                               float3 sunTint,
                               float3 shallowTint)
{
    float up = saturate(R.y * 0.5f + 0.5f);
    float horizon = pow(saturate(1.0f - abs(R.y)), 2.0f);
    float3 safeAmbient = max(ambientTint, 0.015f.xxx);
    float3 safeSun = max(sunTint, 0.0f.xxx);
    float3 safeShallow = max(shallowTint, 0.0f.xxx);
    float3 skyBase = lerp(float3(0.055f, 0.120f, 0.115f), safeAmbient * 1.85f + safeShallow * 0.34f, 0.62f);
    float3 skyHigh = lerp(float3(0.32f, 0.46f, 0.54f), safeAmbient * 2.55f + safeSun * 0.16f, 0.42f);
    float3 coolCeiling = lerp(skyBase, skyHigh, up);
    float3 wetHorizon = lerp(float3(0.24f, 0.34f, 0.25f), safeShallow * 1.28f + safeAmbient * 1.10f, 0.42f);
    float3 warmHorizon = lerp(float3(1.0f, 0.58f, 0.25f), safeSun, 0.60f) * (0.20f + 0.18f * flowNoise);
    float3 reflectedRoom = lerp(coolCeiling, wetHorizon, horizon * 0.72f) + warmHorizon * horizon;

    if (liquidType == 1u)
    {
        return lerp(float3(0.36f, 0.055f, 0.015f), float3(1.0f, 0.42f, 0.055f), horizon * 0.65f + flowNoise * 0.20f);
    }
    if (liquidType == 2u)
    {
        return lerp(reflectedRoom, float3(1.0f, 0.74f, 0.20f), 0.48f);
    }
    if (liquidType == 3u)
    {
        return lerp(float3(0.045f, 0.026f, 0.014f), warmHorizon, horizon * 0.34f);
    }
    return reflectedRoom;
}

float LiquidSpecularGlint(float3 R, float3 L, float NdotV, float viscosity, float flowNoise)
{
    float alignment = saturate(dot(R, L));
    float sharpPower = lerp(760.0f, 78.0f, viscosity);
    float broadPower = lerp(48.0f, 15.0f, viscosity);
    float sharp = pow(alignment, sharpPower);
    float broad = pow(alignment, broadPower) * (0.35f + 0.65f * viscosity);
    float grazing = pow(saturate(1.0f - NdotV), 1.65f);
    float brokenSurface = 0.68f + 0.32f * flowNoise;
    return (sharp * 1.35f + broad * 0.48f + grazing * 0.06f) * brokenSurface;
}

float2 MultiOctaveWaterSlope(float2 worldXZ, float2 primaryDir, float time, float flowSpeed, float viscosity)
{
    float2 d0 = normalize(primaryDir);
    if (!all(isfinite(d0)) || dot(d0, d0) < 0.25f)
    {
        d0 = float2(0.82f, 0.57f);
    }

    float2 d1 = normalize(float2(-d0.y, d0.x) * 0.74f + d0 * 0.26f);
    float2 d2 = normalize(float2(d0.x * 0.38f - d0.y * 0.92f, d0.x * 0.92f + d0.y * 0.38f));
    float speed = max(flowSpeed, 0.12f);
    float calm = lerp(1.0f, 0.32f, viscosity);

    float2 slope = 0.0f.xx;
    float k0 = 0.42f;
    float k1 = 0.83f;
    float k2 = 1.72f;
    float p0 = dot(worldXZ, d0) * k0 + time * speed * 0.38f;
    float p1 = dot(worldXZ, d1) * k1 - time * speed * 0.27f + 1.7f;
    float p2 = dot(worldXZ, d2) * k2 + time * speed * 0.18f + 3.1f;

    slope += d0 * cos(p0) * (0.44f * k0);
    slope += d1 * cos(p1) * (0.20f * k1);
    slope += d2 * cos(p2) * (0.070f * k2);

    float2 noiseP = worldXZ * 0.23f + d0 * (time * speed * 0.035f);
    float2 brokenSwell = float2(FBM(noiseP), FBM(noiseP * 1.67f + 31.4f)) * 2.0f - 1.0f;
    slope += brokenSwell * 0.040f;
    return slope * calm;
}

float SunGlitterRoad(float3 worldPos,
                     float3 cameraPos,
                     float3 L,
                     float3 R,
                     float NdotV,
                     float viscosity,
                     float flowNoise,
                     float time)
{
    float2 sunXZ = normalize(L.xz);
    if (!all(isfinite(sunXZ)) || dot(sunXZ, sunXZ) < 0.25f)
    {
        sunXZ = float2(0.28f, 0.96f);
    }

    float2 viewXZ = worldPos.xz - cameraPos.xz;
    float viewLen = max(length(viewXZ), 1.0e-3f);
    float2 toWater = viewXZ / viewLen;
    float roadCenter = saturate(dot(toWater, sunXZ));
    float roadWidth = smoothstep(0.48f, 0.96f, roadCenter);
    float distanceLift = smoothstep(4.0f, 42.0f, viewLen) * (1.0f - smoothstep(88.0f, 150.0f, viewLen));

    float sparkleA = FBM(worldPos.xz * 10.5f + float2(time * 0.62f, -time * 0.34f));
    float sparkleB = FBM(worldPos.xz * 27.0f + float2(-time * 0.74f, time * 0.49f));
    float beadMask = smoothstep(0.58f, 0.92f, sparkleA) * smoothstep(0.50f, 0.88f, sparkleB);
    float sunAlignment = saturate(dot(R, L));
    float sharpBeads = pow(sunAlignment, lerp(1320.0f, 210.0f, viscosity)) * beadMask;
    float brokenTrack = pow(roadCenter, lerp(18.0f, 8.0f, viscosity)) * distanceLift;
    float fresnelTrack = pow(saturate(1.0f - NdotV), 1.15f);

    return (sharpBeads * 2.1f + brokenTrack * (0.08f + flowNoise * 0.15f) * (0.45f + fresnelTrack)) * roadWidth;
}

float3 GetAtmosphereSunDirection()
{
    if (g_LightCount.x > 0 && (uint)g_Lights[0].position_type.w == 0u)
    {
        return normalize(g_Lights[0].direction_cosInner.xyz);
    }
    return normalize(float3(0.35f, 0.70f, 0.55f));
}

float3 GetAtmosphereSunColor()
{
    if (g_LightCount.x > 0 && (uint)g_Lights[0].position_type.w == 0u)
    {
        return max(g_Lights[0].color_range.rgb, 0.0f.xxx);
    }
    return float3(1.0f, 0.86f, 0.62f);
}

float HazePhaseHG(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = max(1.0f + g2 - 2.0f * g * cosTheta, 1.0e-3f);
    return (1.0f - g2) / pow(denom, 1.5f);
}

float3 ApplyAerialPerspective(float3 hdrColor, float3 worldPos)
{
    if (g_FogParams.w <= 0.5f)
    {
        return hdrColor;
    }

    float density = max(g_FogParams.x, 0.0f);
    if (density <= 1.0e-5f)
    {
        return hdrColor;
    }

    float3 viewVec = worldPos - g_CameraPosition.xyz;
    float dist = length(viewVec);
    if (dist <= 1.0e-3f)
    {
        return hdrColor;
    }

    float3 rayDir = viewVec / dist;
    float baseHeight = g_FogParams.y;
    float falloff = max(g_FogParams.z, 0.0f);
    float heightFactor = exp(-falloff * max(worldPos.y - baseHeight, 0.0f));
    float nearFade = max(g_FogExtraParams.z, 0.35f);
    float nearMask = saturate((dist - 3.0f) / max(10.0f, nearFade * 12.0f));
    nearMask *= nearMask;

    float fogAmount = 1.0f - exp(-density * dist * heightFactor * nearMask);
    fogAmount = min(saturate(fogAmount * max(g_FogExtraParams.y, 0.0f) * 0.72f), 0.55f);

    float3 sunDir = GetAtmosphereSunDirection();
    float3 sunTint = GetAtmosphereSunColor();
    float anisotropy = clamp(g_FogExtraParams.x, -0.65f, 0.65f);
    float sunPhase = HazePhaseHG(saturate(dot(rayDir, sunDir)), anisotropy);
    float horizon = pow(saturate(1.0f - abs(rayDir.y)), 1.35f);
    float3 baseTint = lerp(float3(0.56f, 0.68f, 0.86f), max(g_AmbientColor.rgb, 0.0f.xxx) * 1.9f, 0.35f);
    float3 fogTint = baseTint + sunTint * sunPhase * (0.055f + horizon * 0.035f);
    fogTint = min(fogTint, clamp(g_FogExtraParams.w, 0.25f, 6.0f).xxx);

    return lerp(hdrColor, fogTint, fogAmount);
}

float3 ApplyWaterHorizonBlend(float3 hdrColor,
                              float3 worldPos,
                              float farEdgeMask,
                              float3 shallowTint,
                              float3 deepTint,
                              float3 sunTint,
                              float flowNoise)
{
    float3 viewVec = worldPos - g_CameraPosition.xyz;
    float dist = length(viewVec);
    if (dist <= 1.0e-3f)
    {
        return hdrColor;
    }

    float3 rayDir = viewVec / dist;
    float horizonRay = 1.0f - smoothstep(0.025f, 0.20f, abs(rayDir.y));
    float farWater = smoothstep(5.0f, 22.0f, dist);
    float planeEdgeHaze = saturate(farEdgeMask) * smoothstep(7.0f, 16.0f, dist);
    float hazeAmount = saturate(horizonRay * farWater + planeEdgeHaze * 0.42f);
    if (hazeAmount <= 1.0e-4f)
    {
        return hdrColor;
    }

    float3 sunDir = GetAtmosphereSunDirection();
    float3 safeSunTint = saturate(sunTint);
    float sunPhase = HazePhaseHG(saturate(dot(rayDir, sunDir)), clamp(g_FogExtraParams.x, -0.45f, 0.55f));
    float3 warmSky = max(g_AmbientColor.rgb * 2.35f + safeSunTint * 0.14f,
                         shallowTint * float3(1.04f, 0.94f, 0.76f) + safeSunTint * 0.10f);
    float3 waterMist = lerp(deepTint * float3(1.05f, 1.14f, 1.08f), warmSky, 0.84f);
    waterMist += safeSunTint * (0.070f + sunPhase * 0.030f + flowNoise * 0.020f);
    waterMist = min(waterMist, clamp(g_FogExtraParams.w, 0.28f, 5.0f).xxx);

    float cappedHaze = min(hazeAmount * 0.50f, 0.48f);
    return lerp(hdrColor, waterMist, cappedHaze);
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
    float bodyThickness = saturate(g_CoatParams.x);
    float meniscusStrength = saturate(g_CoatParams.y);
    float sloshStrength = saturate(g_CoatParams.z);
    float flowSpeed = max(g_CoatParams.w, 0.0f);

    float t = g_TimeAndExposure.x;
    float2 primaryWaveDir = g_WaterParams1.xy;
    float2 largeWaveSlope = MultiOctaveWaterSlope(input.worldPos.xz, primaryWaveDir, t, max(flowSpeed, g_WaterParams0.z), viscosity);
    N = normalize(N + float3(-largeWaveSlope.x, 0.0f, -largeWaveSlope.y) * lerp(0.24f, 0.055f, viscosity));

    float microFreq = lerp(3.20f, 0.85f, viscosity);
    float microAmp = lerp(0.18f, 0.035f, viscosity) * (1.0f + sloshStrength * 0.65f);
    float2 microP = input.worldPos.xz * microFreq + float2(t * 0.06f, -t * 0.045f) * max(flowSpeed, 0.15f);
    float2 microRipple = float2(FBM(microP), FBM(microP + 19.37f)) * 2.0f - 1.0f;
    N = normalize(N + float3(microRipple.x, 0.0f, microRipple.y) * microAmp);

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
    float meniscus = pow(saturate(1.0f - edgeDistance * 5.0f), 2.0f) * meniscusStrength;
    float shorelineMask = saturate(1.0f - smoothstep(0.018f, 0.115f, edgeDistance));
    float edgeFoam = saturate(shorelineMask * (0.28f + meniscus * 0.34f));
    float basinDepth = saturate(edgeDistance * 2.6f);
    float waveDepth = saturate(input.waveHeight * 0.30f + 0.55f);
    float thicknessDepth = saturate(bodyThickness * 0.42f + meniscus * 0.35f);
    float viewDistance = length(g_CameraPosition.xyz - input.worldPos);
    float viewDepth = saturate((viewDistance - 10.0f) / 150.0f);
    float farDepth = smoothstep(18.0f, 120.0f, viewDistance);
    float depthMix = saturate(lerp(basinDepth, waveDepth, 0.28f) + absorption * 0.22f + thicknessDepth + farDepth * 0.13f);

    float flowNoise = FBM(input.worldPos.xz * lerp(0.17f, 0.32f, 1.0f - viscosity) +
                          float2(t * 0.035f, -t * 0.025f) * max(flowSpeed, 0.1f));
    float suspendedMatter = smoothstep(0.22f, 0.88f, FBM(input.worldPos.xz * 0.42f + float2(t * 0.012f, -t * 0.018f)));
    float bankNoise = FBM(input.worldPos.xz * 0.82f + float2(-t * 0.008f, t * 0.006f));
    float bankDarkening = saturate((0.18f - edgeDistance) * 4.2f) * (0.45f + 0.55f * bankNoise);
    float surfaceFilm = smoothstep(0.48f, 0.86f, FBM(input.worldPos.xz * 1.15f + float2(t * 0.010f, -t * 0.014f)));
    float3 localSilt = lerp(float3(0.12f, 0.15f, 0.085f), float3(0.24f, 0.21f, 0.13f), suspendedMatter);
    float3 baseColor = lerp(shallowProfile, deepProfile, depthMix);
    if (liquidType == 0u)
    {
        depthMix = saturate(depthMix + viewDepth * 0.32f);
        float3 clearWaterTint = lerp(shallowProfile * float3(1.10f, 1.22f, 1.18f),
                                     deepProfile * float3(0.85f, 1.05f, 1.35f),
                                     saturate(depthMix + viewDepth * 0.18f));
        float turbidity = saturate(absorption * 0.55f + bodyThickness * 0.40f);
        baseColor = lerp(baseColor, localSilt, turbidity * 0.34f * (1.0f - depthMix * 0.30f));
        baseColor = lerp(baseColor, deepProfile * 0.78f + localSilt * 0.20f, bankDarkening * 0.34f);
        baseColor = lerp(baseColor, baseColor * lerp(0.92f, 1.08f, surfaceFilm), 0.12f);
        baseColor = lerp(baseColor, clearWaterTint, 0.36f);
    }
    baseColor = lerp(baseColor, g_Albedo.rgb, liquidType == 0u ? 0.16f : 0.08f);
    float opticalDepth = absorption * (0.35f + depthMix * 1.15f + bodyThickness * 1.35f + meniscus * 0.55f);
    float3 channelAbsorption = lerp(1.0f.xxx, saturate(deepProfile + 0.08f.xxx), saturate(opticalDepth));
    baseColor *= channelAbsorption;

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
        L = normalize(g_Lights[0].direction_cosInner.xyz);
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
        float3 sceneWaterSky = lerp(shallowProfile, max(g_AmbientColor.rgb * 1.35f, shallowProfile), 0.45f);
        depthTint = lerp(baseColor, sceneWaterSky, 0.18f * skyFacing);
        float3 clearDepthTint = lerp(shallowProfile * float3(1.08f, 1.20f, 1.18f),
                                     deepProfile * float3(0.90f, 1.06f, 1.34f),
                                     saturate(depthMix + viewDepth * 0.20f));
        depthTint = lerp(depthTint, clearDepthTint, 0.32f);
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
    float foamRamp = saturate((slope - 0.16f) * 3.0f);
    float3 foamColor = liquidType == 0u ? float3(0.90f, 0.97f, 1.0f) :
                       (liquidType == 2u ? float3(1.0f, 0.82f, 0.34f) : depthTint);

    float3 ambient = (g_AmbientColor.rgb * 1.15f + shallowProfile * 0.10f) * depthTint;
    float3 color = ambient + lighting;

    float3 R = reflect(-V, N);
    float3 reflectedRoom = LiquidReflectionPalette(R, liquidType, flowNoise, g_AmbientColor.rgb, radiance, shallowProfile);
    float3 envReflection = SampleWaterEnvironment(R, roughness);
    float envBlend = saturate(g_EnvParams.z) * (liquidType == 0u ? 0.76f : 0.26f);
    reflectedRoom = lerp(reflectedRoom, envReflection, envBlend);
    float grazingReflect = pow(saturate(1.0f - NdotV), 2.1f);
    float reflectionWeight = lerp(0.68f, 0.24f, viscosity) * (0.52f + 0.64f * grazingReflect);
    if (liquidType == 1u)
    {
        reflectionWeight *= 0.34f;
    }
    else if (liquidType == 2u)
    {
        reflectionWeight *= 0.62f;
    }
    else if (liquidType == 3u)
    {
        reflectionWeight *= 0.46f;
    }
    reflectionWeight *= saturate(1.0f - depthMix * 0.28f + meniscus * 0.25f);
    if (liquidType == 0u)
    {
        reflectionWeight *= saturate(1.0f - bankDarkening * 0.22f + surfaceFilm * 0.16f);
        reflectedRoom = lerp(reflectedRoom, reflectedRoom * float3(0.78f, 0.88f, 0.74f), bankDarkening * 0.22f);
    }
    color += reflectedRoom * reflectionWeight;

    float glint = LiquidSpecularGlint(R, L, NdotV, viscosity, flowNoise);
    float sparkleNoiseA = FBM(input.worldPos.xz * 8.5f + float2(t * 0.42f, -t * 0.27f));
    float sparkleNoiseB = FBM(input.worldPos.xz * 19.0f + float2(-t * 0.58f, t * 0.36f));
    float sparkleMask = smoothstep(0.62f, 0.94f, sparkleNoiseA) * smoothstep(0.54f, 0.90f, sparkleNoiseB);
    float sunAlignment = saturate(dot(R, L));
    float sunSparkle = pow(sunAlignment, lerp(1180.0f, 210.0f, viscosity)) * sparkleMask;
    float sunRoad = SunGlitterRoad(input.worldPos, g_CameraPosition.xyz, L, R, NdotV, viscosity, flowNoise, t);
    float3 glintTint = (liquidType == 0u) ? lerp(float3(0.74f, 0.90f, 0.78f), max(radiance, float3(0.70f, 0.82f, 0.72f)), 0.45f) :
                       (liquidType == 1u) ? float3(1.0f, 0.52f, 0.12f) :
                       (liquidType == 2u) ? float3(1.0f, 0.80f, 0.26f) :
                                            float3(0.92f, 0.56f, 0.24f);
    float glitterDepthMask = lerp(0.54f, 0.20f, depthMix) * (1.0f - bankDarkening * 0.24f);
    color += (glint + sunSparkle * 1.90f + sunRoad) * glintTint * glitterDepthMask * (0.55f + 0.45f * fresnelStrength);

    float causticNoiseA = FBM(input.worldPos.xz * 2.15f + float2(t * 0.08f, -t * 0.05f));
    float causticNoiseB = FBM(input.worldPos.xz * 4.10f - float2(t * 0.035f, t * 0.045f));
    float caustic = smoothstep(0.58f, 0.92f, causticNoiseA) * smoothstep(0.36f, 0.82f, causticNoiseB);
    float causticStrength = (liquidType == 0u) ? 0.18f : (liquidType == 2u ? 0.10f : 0.04f);
    color += caustic * causticStrength * (1.0f - depthMix * 0.45f) * saturate(NdotL + 0.25f) * depthTint;
    float backScatter = pow(saturate(1.0f - NdotV), 2.5f) * saturate(absorption + bodyThickness * 0.65f);
    color += depthTint * backScatter * (liquidType == 1u ? 0.18f : 0.11f);

    // Blend foam over the lit surface; modulate slightly by viewing angle so
    // foam is more visible at grazing angles.
    float foamViewBoost = pow(1.0f - NdotV, 2.0f);
    float foamNoiseGate = smoothstep(0.46f, 0.86f, bankNoise);
    float foamAmount;
    if (liquidType == 0u)
    {
        float shorelineFoam = edgeFoam * foamNoiseGate;
        float crestFoam = foamRamp * smoothstep(0.70f, 0.94f, flowNoise) * 0.10f;
        foamAmount = saturate((shorelineFoam * 0.52f + crestFoam) * foamStrength * (0.45f + 0.28f * foamViewBoost));
    }
    else
    {
        foamAmount = saturate((foamRamp + edgeFoam * 0.85f) * foamStrength * (0.6f + 0.4f * foamViewBoost));
    }
    color = lerp(color, foamColor, foamAmount);
    color = lerp(color, color * (0.72f + depthTint * 0.55f), saturate(meniscus * 0.65f + bodyThickness * 0.12f));

    float3 fresnelGlow = F * pow(1.0f - NdotV, 1.5f) *
        (liquidType == 0u ? lerp(depthTint, max(g_AmbientColor.rgb * 1.8f, depthTint), 0.48f) : depthTint);
    color += fresnelGlow;

    if (liquidType == 0u)
    {
        float3 naturalBody = depthTint * (0.86f + 0.28f * NdotL) + localSilt * (0.10f + absorption * 0.11f);
        naturalBody = lerp(naturalBody, naturalBody * 0.72f + localSilt * 0.24f, bankDarkening * 0.42f);
        naturalBody += surfaceFilm * float3(0.020f, 0.034f, 0.018f) * (1.0f - depthMix * 0.35f);
        float3 sunSheen = glintTint * specular * (0.26f + 0.22f * grazingReflect);
        float3 fresnelDepthWash = lerp(shallowProfile * float3(1.10f, 1.04f, 0.86f),
                                       deepProfile * float3(0.82f, 1.06f, 1.24f),
                                       saturate(depthMix + grazingReflect * 0.22f + farDepth * 0.12f));
        naturalBody = lerp(naturalBody, fresnelDepthWash, 0.22f + grazingReflect * 0.12f);
        color = lerp(color, naturalBody + reflectedRoom * reflectionWeight * 0.92f + sunSheen, 0.42f);
    }
    else if (liquidType == 1u)
    {
        float heatPulse = 0.65f + 0.35f * sin(t * 1.7f + flowNoise * 6.28318f);
        float crustBreakup = smoothstep(0.34f, 0.86f, FBM(input.worldPos.xz * 3.25f + float2(t * 0.025f, -t * 0.018f)));
        float3 moltenCore = depthTint * (1.15f + max(emissiveHeat, 1.0f) * 0.42f) * heatPulse;
        float3 coolingSkin = depthTint * float3(0.40f, 0.16f, 0.075f);
        color = lerp(color + moltenCore, coolingSkin + moltenCore * 0.38f, crustBreakup * 0.42f);
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
    alpha = saturate(alpha + bodyThickness * 0.06f + meniscus * 0.08f);

    color = ApplyAerialPerspective(color, input.worldPos);
    if (liquidType == 0u)
    {
        float farEdgeMask = smoothstep(0.70f, 0.98f, input.texCoord.y);
        color = ApplyWaterHorizonBlend(color, input.worldPos, farEdgeMask, shallowProfile, deepProfile, radiance, flowNoise);
    }

    return float4(color, alpha);
}
