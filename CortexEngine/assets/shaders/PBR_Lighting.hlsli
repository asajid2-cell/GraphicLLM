// PBR_Lighting.hlsli
// Shared PBR helpers used by both forward (Basic.hlsl) and VB deferred
// (DeferredLighting.hlsl) paths to avoid shading drift.

#ifndef CORTEX_PBR_LIGHTING_HLSLI
#define CORTEX_PBR_LIGHTING_HLSLI

static const float PI = 3.14159265f;

static const float CORTEX_SPECULAR_AA_NORMAL_SIGMA = 1.25f;
static const float CORTEX_SPECULAR_AA_KAPPA = 0.70f;
static const float CORTEX_SPECULAR_AA_TOKSVIG_STRENGTH = 0.18f;
static const float CORTEX_SPECULAR_AA_TOKSVIG_SLOPE_SCALE = 0.35f;
static const float CORTEX_SPECULAR_AA_TOKSVIG_KAPPA = 0.55f;

float SpecularAAApplyAlpha2Variance(float roughness, float alpha2Variance)
{
    float r = saturate(roughness);
    float alpha = r * r;
    float a2 = alpha * alpha;
    a2 = saturate(a2 + max(alpha2Variance, 0.0f));
    return sqrt(sqrt(max(a2, 1e-8f)));
}

float SpecularAAGeometricAlpha2Variance(float3 shadingNormal, float varianceScale)
{
    float scale = saturate(varianceScale);
    float3 N = normalize(shadingNormal);
    float3 dNdx = ddx(N);
    float3 dNdy = ddy(N);
    float variance = CORTEX_SPECULAR_AA_NORMAL_SIGMA *
                     scale *
                     (dot(dNdx, dNdx) + dot(dNdy, dNdy));
    return min(2.0f * variance, CORTEX_SPECULAR_AA_KAPPA * scale * scale);
}

float SpecularAAGeometricRoughness(float3 shadingNormal, float roughness, float varianceScale)
{
    return SpecularAAApplyAlpha2Variance(
        roughness,
        SpecularAAGeometricAlpha2Variance(shadingNormal, varianceScale));
}

float SpecularAAToksvigAlpha2Variance(float3 sampledNormalTS, float2 scaledNormalXY, float varianceScale)
{
    float scale = saturate(varianceScale);
    float normalLength = clamp(length(sampledNormalTS), 1e-3f, 1.0f);
    float lengthVariance = (1.0f - normalLength) / normalLength;
    float slopeVariance = dot(scaledNormalXY, scaledNormalXY) * CORTEX_SPECULAR_AA_TOKSVIG_SLOPE_SCALE;
    float variance = scale * (lengthVariance * CORTEX_SPECULAR_AA_TOKSVIG_STRENGTH + slopeVariance);
    return min(variance, CORTEX_SPECULAR_AA_TOKSVIG_KAPPA * scale * scale);
}

float SpecularAAToksvigRoughness(float roughness, float normalMapAlpha2Variance)
{
    return SpecularAAApplyAlpha2Variance(roughness, normalMapAlpha2Variance);
}

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

// IBL-specific geometry term: k = r^2 / 2 (different from direct lighting k = (r+1)^2 / 8)
// This provides better energy conservation for image-based lighting.
float GeometrySchlickGGX_IBL(float NdotX, float roughness)
{
    float k = (roughness * roughness) / 2.0f;
    return NdotX / max(NdotX * (1.0f - k) + k, 1e-6f);
}

float GeometrySmith_IBL(float NdotV, float NdotL, float roughness)
{
    float ggx2 = GeometrySchlickGGX_IBL(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX_IBL(NdotL, roughness);
    return ggx1 * ggx2;
}

// Convenience overload for IBL (accepts N/V/L vectors).
float GeometrySmith_IBL(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);
    return GeometrySmith_IBL(NdotV, NdotL, roughness);
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

float Pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

float DistributionCharlie(float NdotH, float roughness)
{
    float r = max(saturate(roughness), 0.25f);
    float invAlpha = rcp(max(r * r, 0.08f));
    float sin2h = max(1.0f - saturate(NdotH) * saturate(NdotH), 0.0f);
    return (2.0f + invAlpha) * pow(sin2h, invAlpha * 0.5f) / (2.0f * PI);
}

float CharlieSheenVisibility(float NdotV, float NdotL)
{
    float denom = 4.0f * max(NdotL + NdotV - NdotL * NdotV, 1e-4f);
    return rcp(denom);
}

float3 SheenTintFromAlbedo(float3 albedo)
{
    return lerp(1.0f.xxx, sqrt(saturate(albedo)), 0.72f);
}

float FabricSheenEnergy(float sheenWeight, float roughness)
{
    float r = saturate(roughness);
    return saturate(saturate(sheenWeight) * lerp(0.08f, 0.16f, r));
}

float3 ApplySheenEnergyConservation(float3 kD, float sheenWeight, float roughness)
{
    return kD * (1.0f - FabricSheenEnergy(sheenWeight, roughness));
}

float3 EvaluateCharlieSheenBRDF(float3 N,
                                float3 V,
                                float3 L,
                                float3 albedo,
                                float roughness,
                                float sheenWeight)
{
    float weight = saturate(sheenWeight);
    if (weight <= 0.001f) {
        return 0.0f.xxx;
    }

    float3 H = normalize(V + L);
    float NdotV = saturate(dot(N, V));
    float NdotL = saturate(dot(N, L));
    float NdotH = saturate(dot(N, H));
    float LdotH = saturate(dot(L, H));
    if (NdotV <= 1e-4f || NdotL <= 1e-4f) {
        return 0.0f.xxx;
    }

    float sheenRoughness = saturate(max(roughness, 0.45f));
    float D = DistributionCharlie(NdotH, sheenRoughness);
    float Vis = CharlieSheenVisibility(NdotV, NdotL);
    float grazing = Pow5(1.0f - LdotH);
    float sheenF = lerp(0.24f, 1.0f, grazing);
    float3 tint = SheenTintFromAlbedo(albedo);
    return tint * weight * D * Vis * sheenF * 0.34f;
}

float CharlieSheenDFG(float NdotV, float roughness)
{
    float r = saturate(roughness);
    float grazing = Pow5(1.0f - saturate(NdotV));
    return saturate((0.05f + 0.10f * r) + grazing * lerp(0.28f, 0.48f, r));
}

float3 EvaluateCharlieSheenIBL(float3 ambientRadiance,
                               float3 albedo,
                               float NdotV,
                               float roughness,
                               float sheenWeight)
{
    float weight = saturate(sheenWeight);
    if (weight <= 0.001f) {
        return 0.0f.xxx;
    }

    float3 tint = SheenTintFromAlbedo(albedo);
    return ambientRadiance * tint * weight * CharlieSheenDFG(NdotV, roughness) * 0.32f;
}

float3 GGXMultiscatterEnergyCompensation(float3 F0, float roughness)
{
    // Fdez-Aguera style analytic multiple-scatter compensation. It restores
    // the energy that Smith-masked single-scatter GGX loses on rough lobes
    // without needing another runtime texture in the forward path.
    float r = saturate(roughness);
    float r2 = r * r;
    float3 Favg = F0 + (1.0f - F0) * (1.0f / 21.0f);
    float singleScatterLoss = saturate(r2 * (0.52f + 0.16f * r));
    float3 denom = max(1.0f.xxx - Favg * singleScatterLoss, 0.35f.xxx);
    return 1.0f.xxx + (Favg * singleScatterLoss) / denom;
}

float3 RoughSpecularEnergyCompensation(float3 F0, float roughness)
{
    return GGXMultiscatterEnergyCompensation(F0, roughness);
}

float SpecularOcclusion(float NdotV, float ao, float roughness)
{
    // Frostbite/UE-style specular AO: cavities attenuate glossy reflection
    // harder than broad rough reflection, preventing over-bright corners.
    float exponent = exp2(-16.0f * saturate(roughness) - 1.0f);
    return saturate(pow(saturate(NdotV + ao), exponent) - 1.0f + ao);
}

float HorizonSpecularOcclusion(float3 N, float3 V, float ao, float roughness)
{
    float NdotV = saturate(dot(N, V));
    float3 R = reflect(-V, N);
    float horizon = saturate(1.0f + dot(R, N));
    return SpecularOcclusion(NdotV, ao, roughness) * horizon * horizon;
}

float BurleyDiffuseFactor(float NdotV, float NdotL, float LdotH, float roughness)
{
    float fd90 = 0.5f + 2.0f * roughness * LdotH * LdotH;
    float lightScatter = 1.0f + (fd90 - 1.0f) * Pow5(1.0f - saturate(NdotL));
    float viewScatter = 1.0f + (fd90 - 1.0f) * Pow5(1.0f - saturate(NdotV));
    return lightScatter * viewScatter;
}

struct RectAreaLightSample
{
    float3 diffuseDir;
    float3 specularDir;
    float diffuseNdotL;
    float specularNdotL;
    float attenuation;
    float solidAngle;
    float perceptualRoughness;
};

RectAreaLightSample MakeEmptyRectAreaLightSample()
{
    RectAreaLightSample s;
    s.diffuseDir = 0.0f.xxx;
    s.specularDir = 0.0f.xxx;
    s.diffuseNdotL = 0.0f;
    s.specularNdotL = 0.0f;
    s.attenuation = 0.0f;
    s.solidAngle = 0.0f;
    s.perceptualRoughness = 0.0f;
    return s;
}

void BuildRectAreaBasis(float3 lightNormal, out float3 axisX, out float3 axisY)
{
    lightNormal = normalize(lightNormal);
    float3 up = (abs(lightNormal.y) < 0.96f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    axisX = normalize(cross(up, lightNormal));
    axisY = normalize(cross(lightNormal, axisX));
}

float3 ClosestPointOnRect(float3 p, float3 center, float3 axisX, float3 axisY, float2 halfSize)
{
    float3 local = p - center;
    float x = clamp(dot(local, axisX), -halfSize.x, halfSize.x);
    float y = clamp(dot(local, axisY), -halfSize.y, halfSize.y);
    return center + axisX * x + axisY * y;
}

float3 RepresentativeSpecularPointOnRect(float3 worldPos,
                                         float3 N,
                                         float3 V,
                                         float3 center,
                                         float3 lightNormal,
                                         float3 axisX,
                                         float3 axisY,
                                         float2 halfSize)
{
    float3 R = reflect(-V, N);
    float denom = dot(R, -lightNormal);
    if (abs(denom) > 1e-4f)
    {
        float t = dot(center - worldPos, -lightNormal) / denom;
        if (t > 0.0f)
        {
            return ClosestPointOnRect(worldPos + R * t, center, axisX, axisY, halfSize);
        }
    }
    return ClosestPointOnRect(worldPos, center, axisX, axisY, halfSize);
}

RectAreaLightSample EvaluateRectAreaLight(float3 worldPos,
                                          float3 N,
                                          float3 V,
                                          float3 center,
                                          float3 lightNormal,
                                          float2 halfSize,
                                          float rangeMeters,
                                          float roughness)
{
    RectAreaLightSample s;
    halfSize = max(halfSize, 0.001f.xx);
    rangeMeters = max(rangeMeters, 0.001f);

    float3 axisX;
    float3 axisY;
    BuildRectAreaBasis(lightNormal, axisX, axisY);

    float3 diffusePoint = ClosestPointOnRect(worldPos, center, axisX, axisY, halfSize);
    float3 specularPoint = RepresentativeSpecularPointOnRect(worldPos, N, V, center, lightNormal, axisX, axisY, halfSize);

    float3 diffuseVec = diffusePoint - worldPos;
    float3 specularVec = specularPoint - worldPos;
    float diffuseDist = max(length(diffuseVec), 1e-4f);
    float specularDist = max(length(specularVec), 1e-4f);
    s.diffuseDir = diffuseVec / diffuseDist;
    s.specularDir = specularVec / specularDist;
    s.diffuseNdotL = saturate(dot(N, s.diffuseDir));
    s.specularNdotL = saturate(dot(N, s.specularDir));

    float facing = saturate(dot(-s.diffuseDir, normalize(lightNormal)));
    float area = max((halfSize.x * 2.0f) * (halfSize.y * 2.0f), 1e-4f);
    float solidAngle = area * facing / max(diffuseDist * diffuseDist + area, 1e-4f);
    s.solidAngle = saturate(solidAngle);

    float rangeFalloff = saturate(1.0f - diffuseDist / rangeMeters);
    rangeFalloff *= rangeFalloff;
    float angularSize = sqrt(s.solidAngle);
    s.attenuation = rangeFalloff * saturate(0.25f + angularSize * 2.75f);
    s.perceptualRoughness = saturate(sqrt(roughness * roughness + angularSize * angularSize));
    return s;
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

float ApplyContactShadowVisibility(float visibility, float contactOcclusion, float strength)
{
    float contactVisibility = 1.0f - saturate(contactOcclusion) * saturate(strength);
    return saturate(min(visibility, contactVisibility));
}

#endif // CORTEX_PBR_LIGHTING_HLSLI
