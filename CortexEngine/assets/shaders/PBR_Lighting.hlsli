// PBR_Lighting.hlsli
// Shared PBR helpers used by both forward (Basic.hlsl) and VB deferred
// (DeferredLighting.hlsl) paths to avoid shading drift.

#pragma once

static const float PI = 3.14159265f;

float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
    denom = PI * denom * denom;
    return a2 / max(denom, 1e-6f);
}

// Convenience overload used by some shaders (accepts N/H vectors).
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float NdotH = max(dot(N, H), 0.0f);
    return DistributionGGX(NdotH, roughness);
}

float GeometrySchlickGGX(float NdotX, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return NdotX / max(NdotX * (1.0f - k) + k, 1e-6f);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

// Convenience overload used by some shaders (accepts N/V/L vectors).
float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);
    return GeometrySmith(NdotV, NdotL, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

// Fresnel variant commonly used for IBL. Rough surfaces reduce the
// grazing-angle boost to avoid overly bright/specular "plastic" look.
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    float3 oneMinusR = 1.0f - roughness;
    float3 F90 = max(oneMinusR.xxx, F0);
    return F0 + (F90 - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

float3 ComputeF0(float3 albedo, float metallic)
{
    return lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
}

// Cook-Torrance BRDF for a single light (radiance already includes attenuation).
float3 EvaluateCookTorranceBRDF(float3 N,
                                float3 V,
                                float3 L,
                                float3 albedo,
                                float metallic,
                                float roughness,
                                float3 F0)
{
    float3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0f);
    float NdotV = max(dot(N, V), 0.0f);
    float NdotH = max(dot(N, H), 0.0f);
    float VdotH = max(dot(V, H), 0.0f);

    float  D = DistributionGGX(NdotH, roughness);
    float  G = GeometrySmith(NdotV, NdotL, roughness);
    float3 F = FresnelSchlick(VdotH, F0);

    float3 numerator = D * G * F;
    float  denom = 4.0f * NdotV * NdotL;
    float3 spec = numerator / max(denom, 1e-6f);

    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metallic);

    return (kD * albedo / PI + spec) * NdotL;
}

// Shared IBL energy split helpers (sampling is handled by the caller).
float3 EvaluateDiffuseIBL(float3 irradiance,
                          float3 albedo,
                          float metallic,
                          float3 F0,
                          float roughness,
                          float NdotV)
{
    float3 Fibl = FresnelSchlickRoughness(NdotV, F0, roughness);
    float3 kD = (1.0f - metallic) * (1.0f - Fibl);
    return irradiance * kD * albedo;
}

float3 EvaluateSpecularIBL_FresnelOnly(float3 prefiltered,
                                       float3 F0,
                                       float roughness,
                                       float NdotV)
{
    float3 Fibl = FresnelSchlickRoughness(NdotV, F0, roughness);
    return prefiltered * Fibl;
}

float3 EvaluateSpecularIBL_SplitSum(float3 prefiltered,
                                    float2 brdf,
                                    float3 F0)
{
    return prefiltered * (F0 * brdf.x + brdf.y);
}
