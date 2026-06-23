// VolumetricClouds.hlsl
// Raymarched volumetric cloud rendering.
// Reference: "Real-Time Volumetric Cloudscapes" - Horizon Zero Dawn GDC
// Reference: "The Real-Time Volumetric Cloudscapes of Horizon" - SIGGRAPH 2015

#include "FrameConstants.hlsli"

// Constants
static const float EARTH_RADIUS = 6371000.0;        // meters
static const float CLOUD_TOP_OFFSET = 10000.0;      // Cloud layer offset from viewer
static const int MAX_STEPS = 64;
static const int LIGHT_STEPS = 6;

static const float CLOUD_LOW_ALTITUDE = 1100.0f;
static const float CLOUD_HIGH_ALTITUDE = 3600.0f;
static const float CLOUD_COVERAGE = 0.58f;
static const float CLOUD_DENSITY = 0.00042f;
static const float CLOUD_BASE_SCALE = 0.74f;
static const float CLOUD_DETAIL_SCALE = 1.85f;
static const float CLOUD_EROSION = 0.46f;
static const float CLOUD_CURLINESS = 0.85f;
static const float CLOUD_ABSORPTION = 0.00018f;
static const float CLOUD_SCATTERING = 0.00024f;
static const float CLOUD_AMBIENT_MULT = 0.78f;
static const float CLOUD_SUN_MULT = 3.2f;
static const float CLOUD_STEP_SIZE = 155.0f;
static const float3 CLOUD_BASE_COLOR = float3(0.78f, 0.82f, 0.78f);
static const float2 CLOUD_WIND_DIR = normalize(float2(0.72f, 0.31f));
static const float CLOUD_WIND_SPEED = 38.0f;

// Remapping utility
float Remap(float value, float oldMin, float oldMax, float newMin, float newMax) {
    return newMin + (value - oldMin) / (oldMax - oldMin) * (newMax - newMin);
}

float Hash31(float3 p) {
    p = frac(p * float3(0.1031f, 0.11369f, 0.13787f));
    p += dot(p, p.yzx + 19.19f);
    return frac((p.x + p.y) * p.z);
}

float ValueNoise3D(float3 p) {
    float3 i = floor(p);
    float3 f = frac(p);
    float3 u = f * f * (3.0f - 2.0f * f);

    float n000 = Hash31(i + float3(0.0f, 0.0f, 0.0f));
    float n100 = Hash31(i + float3(1.0f, 0.0f, 0.0f));
    float n010 = Hash31(i + float3(0.0f, 1.0f, 0.0f));
    float n110 = Hash31(i + float3(1.0f, 1.0f, 0.0f));
    float n001 = Hash31(i + float3(0.0f, 0.0f, 1.0f));
    float n101 = Hash31(i + float3(1.0f, 0.0f, 1.0f));
    float n011 = Hash31(i + float3(0.0f, 1.0f, 1.0f));
    float n111 = Hash31(i + float3(1.0f, 1.0f, 1.0f));

    float nx00 = lerp(n000, n100, u.x);
    float nx10 = lerp(n010, n110, u.x);
    float nx01 = lerp(n001, n101, u.x);
    float nx11 = lerp(n011, n111, u.x);
    float nxy0 = lerp(nx00, nx10, u.y);
    float nxy1 = lerp(nx01, nx11, u.y);
    return lerp(nxy0, nxy1, u.z);
}

float FBM3D(float3 p) {
    float value = 0.0f;
    float amp = 0.5f;
    [unroll]
    for (int i = 0; i < 5; ++i) {
        value += ValueNoise3D(p) * amp;
        p = p * 2.03f + float3(17.1f, 31.7f, 11.3f);
        amp *= 0.5f;
    }
    return value;
}

float2 Hash22(float2 p) {
    float3 p3 = frac(float3(p.xyx) * float3(0.1031f, 0.1030f, 0.0973f));
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.xx + p3.yz) * p3.zy);
}

float ValueNoise2D(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0f - 2.0f * f);
    float a = Hash22(i).x;
    float b = Hash22(i + float2(1.0f, 0.0f)).x;
    float c = Hash22(i + float2(0.0f, 1.0f)).x;
    float d = Hash22(i + float2(1.0f, 1.0f)).x;
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float FBM2D(float2 p) {
    float value = 0.0f;
    float amp = 0.5f;
    [unroll]
    for (int i = 0; i < 4; ++i) {
        value += ValueNoise2D(p) * amp;
        p = p * 2.07f + 9.71f;
        amp *= 0.5f;
    }
    return value;
}

float2 CurlNoise2D(float2 p) {
    const float e = 0.15f;
    float dx = FBM2D(p + float2(e, 0.0f)) - FBM2D(p - float2(e, 0.0f));
    float dy = FBM2D(p + float2(0.0f, e)) - FBM2D(p - float2(0.0f, e));
    return float2(dy, -dx);
}

float3 GetCloudSunDirection() {
    if (g_LightCount.x > 0 && (uint)g_Lights[0].position_type.w == 0u) {
        return normalize(g_Lights[0].direction_cosInner.xyz);
    }
    return normalize(float3(0.35f, 0.70f, 0.55f));
}

float3 GetCloudSunColor() {
    if (g_LightCount.x > 0 && (uint)g_Lights[0].position_type.w == 0u) {
        return max(g_Lights[0].color_range.rgb, 0.0f.xxx);
    }
    return float3(1.0f, 0.86f, 0.62f);
}

// Get height fraction within cloud layer (0 at bottom, 1 at top)
float GetHeightFraction(float altitude) {
    float lowAlt = CLOUD_LOW_ALTITUDE;
    float highAlt = CLOUD_HIGH_ALTITUDE;
    return saturate((altitude - lowAlt) / (highAlt - lowAlt));
}

// Density altitude profile - clouds have specific vertical shapes
float GetDensityHeightGradient(float heightFraction, float cloudType) {
    // Cloud type: 0=stratus, 0.5=stratocumulus, 1=cumulus

    // Stratus: flat, low clouds
    float stratus = saturate(Remap(heightFraction, 0.0, 0.1, 0.0, 1.0)) *
                    saturate(Remap(heightFraction, 0.2, 0.3, 1.0, 0.0));

    // Stratocumulus: mid-level puffy
    float stratocumulus = saturate(Remap(heightFraction, 0.0, 0.2, 0.0, 1.0)) *
                          saturate(Remap(heightFraction, 0.4, 0.6, 1.0, 0.0));

    // Cumulus: tall, billowy
    float cumulus = saturate(Remap(heightFraction, 0.0, 0.1, 0.0, 1.0)) *
                    saturate(Remap(heightFraction, 0.6, 1.0, 1.0, 0.0));

    // Blend based on cloud type
    float gradient = lerp(lerp(stratus, stratocumulus, saturate(cloudType * 2.0)),
                          cumulus, saturate(cloudType * 2.0 - 1.0));

    return gradient;
}

// Sample weather map for coverage and cloud type
float2 SampleWeatherMap(float2 worldXZ) {
    float t = g_TimeAndExposure.x;
    float2 uv = worldXZ * 0.000010f + CLOUD_WIND_DIR * CLOUD_WIND_SPEED * t * 0.000025f;
    float coverage = smoothstep(0.30f, 0.78f, FBM2D(uv * 2.1f));
    float type = saturate(FBM2D(uv * 0.85f + 41.0f) * 1.15f);
    return float2(coverage, type);  // x=coverage, y=cloudType
}

// Sample low-frequency shape noise
float SampleShapeNoise(float3 position) {
    float t = g_TimeAndExposure.x;
    float3 uvw = position * CLOUD_BASE_SCALE * 0.00011f;
    uvw.xz += CLOUD_WIND_DIR * CLOUD_WIND_SPEED * t * 0.000030f;

    float2 curl = CurlNoise2D(position.xz * 0.000055f + t * 0.004f);
    uvw.xz += curl * CLOUD_CURLINESS * 0.22f;
    return FBM3D(uvw);
}

// Sample high-frequency detail noise
float SampleDetailNoise(float3 position, float mipLevel) {
    float t = g_TimeAndExposure.x;
    float3 uvw = position * CLOUD_DETAIL_SCALE * 0.00062f;
    uvw.xz += CLOUD_WIND_DIR * CLOUD_WIND_SPEED * t * 0.000075f;

    return FBM3D(uvw);
}

// Full density sample at position
float SampleCloudDensity(float3 position, float mipLevel, bool sampleDetail) {
    float altitude = position.y;
    float heightFraction = GetHeightFraction(altitude);

    // Outside cloud layer
    if (heightFraction <= 0.0 || heightFraction >= 1.0) {
        return 0.0;
    }

    // Sample weather map
    float2 weather = SampleWeatherMap(position.xz);
    float coverage = weather.x * CLOUD_COVERAGE;
    float cloudType = weather.y;

    // Height gradient
    float heightGradient = GetDensityHeightGradient(heightFraction, cloudType);

    // Base shape noise
    float shape = SampleShapeNoise(position);

    // Combine coverage and shape
    float baseCloud = saturate(Remap(shape * heightGradient, 1.0 - coverage, 1.0, 0.0, 1.0));

    if (baseCloud <= 0.0) {
        return 0.0;
    }

    // Add detail noise erosion
    float density = baseCloud;
    if (sampleDetail && baseCloud > 0.0) {
        float detail = SampleDetailNoise(position, mipLevel);

        // Erode edges with detail
        float erosion = CLOUD_EROSION;
        float detailModifier = lerp(detail, 1.0 - detail, saturate(heightFraction * 5.0));
        density = saturate(Remap(density, detailModifier * erosion, 1.0, 0.0, 1.0));
    }

    return density * CLOUD_DENSITY;
}

// Beer-Lambert light extinction
float BeerLambert(float density, float absorptionCoeff) {
    return exp(-density * absorptionCoeff);
}

// Powder effect (forward scattering brightening)
float PowderEffect(float density, float cosAngle) {
    float powder = 1.0 - exp(-density * 2.0);
    return lerp(1.0, powder, saturate(-cosAngle * 0.5 + 0.5));
}

// Henyey-Greenstein phase function
float HenyeyGreenstein(float cosAngle, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * 3.14159 * pow(1.0 + g2 - 2.0 * g * cosAngle, 1.5));
}

// Light marching toward sun
float LightMarch(float3 position) {
    float3 lightDir = GetCloudSunDirection();
    float stepSize = (CLOUD_HIGH_ALTITUDE - CLOUD_LOW_ALTITUDE) / float(LIGHT_STEPS);

    float totalDensity = 0.0;
    float3 lightPos = position;

    [unroll]
    for (int i = 0; i < LIGHT_STEPS; ++i) {
        lightPos += lightDir * stepSize;
        float density = SampleCloudDensity(lightPos, 2.0, false);
        totalDensity += density * stepSize;
    }

    float transmittance = BeerLambert(totalDensity, CLOUD_ABSORPTION);
    return transmittance;
}

// Ray-sphere intersection
bool RaySphereIntersect(float3 rayOrigin, float3 rayDir, float3 sphereCenter, float sphereRadius,
                         out float t0, out float t1) {
    float3 oc = rayOrigin - sphereCenter;
    float b = dot(oc, rayDir);
    float c = dot(oc, oc) - sphereRadius * sphereRadius;
    float h = b * b - c;

    if (h < 0.0) {
        t0 = t1 = 0.0;
        return false;
    }

    h = sqrt(h);
    t0 = -b - h;
    t1 = -b + h;
    return true;
}

// Get cloud layer intersection distances
void GetCloudLayerIntersection(float3 rayOrigin, float3 rayDir,
                                out float nearDist, out float farDist) {
    // Simplified planar layers for now
    float lowAlt = CLOUD_LOW_ALTITUDE;
    float highAlt = CLOUD_HIGH_ALTITUDE;

    // Ray-plane intersection
    if (abs(rayDir.y) < 0.0001) {
        nearDist = 0.0;
        farDist = 10000.0;
        return;
    }

    float tLow = (lowAlt - rayOrigin.y) / rayDir.y;
    float tHigh = (highAlt - rayOrigin.y) / rayDir.y;

    nearDist = max(0.0, min(tLow, tHigh));
    farDist = max(tLow, tHigh);
}

// Main raymarching
float4 RaymarchClouds(float3 rayOrigin, float3 rayDir, float maxDist, float dither) {
    float3 sunDir = GetCloudSunDirection();
    float cosAngle = dot(rayDir, sunDir);

    // Phase functions
    float phaseForward = HenyeyGreenstein(cosAngle, 0.6);
    float phaseBackward = HenyeyGreenstein(cosAngle, -0.3);
    float phase = lerp(phaseBackward, phaseForward, 0.7);

    // Cloud layer intersection
    float nearDist, farDist;
    GetCloudLayerIntersection(rayOrigin, rayDir, nearDist, farDist);

    if (farDist <= nearDist || nearDist > maxDist) {
        return float4(0.0, 0.0, 0.0, 1.0);  // No clouds
    }

    farDist = min(farDist, maxDist);

    // Raymarch parameters
    float stepSize = CLOUD_STEP_SIZE;
    int maxSteps = MAX_STEPS;

    // Dithered start position
    float t = nearDist + dither * stepSize;

    // Accumulation
    float3 scatteredLight = 0.0;
    float transmittance = 1.0;

    int zeroCount = 0;

    [loop]
    for (int i = 0; i < maxSteps && t < farDist; ++i) {
        if (transmittance < 0.01) break;

        float3 pos = rayOrigin + rayDir * t;
        float density = SampleCloudDensity(pos, 0.0, true);

        if (density > 0.001) {
            zeroCount = 0;

            // Light contribution
            float lightTransmit = LightMarch(pos);

            // Scattering
            float3 ambient = g_AmbientColor.rgb * CLOUD_AMBIENT_MULT;
            float3 sun = GetCloudSunColor() * lightTransmit * phase * CLOUD_SUN_MULT;

            float3 luminance = CLOUD_BASE_COLOR * (ambient + sun);

            // Powder effect
            float powder = PowderEffect(density * stepSize, cosAngle);
            luminance *= powder;

            // Accumulate
            float sampleTransmit = BeerLambert(density * stepSize, CLOUD_SCATTERING);
            float3 integScatter = luminance * (1.0 - sampleTransmit);

            scatteredLight += integScatter * transmittance;
            transmittance *= sampleTransmit;
        } else {
            zeroCount++;
            // Adaptive step size - take larger steps in empty space
            if (zeroCount > 3) {
                t += stepSize;  // Double step
            }
        }

        t += stepSize;
    }

    float alpha = 1.0 - transmittance;
    return float4(scatteredLight, alpha);
}

// Vertex shader (fullscreen quad)
struct VSOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
    float3 rayDir : TEXCOORD1;
};

VSOutput VSMain(uint vertexID : SV_VertexID) {
    VSOutput output;

    // Fullscreen triangle
    output.texCoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(output.texCoord * 2.0 - 1.0, 0.0, 1.0);
    output.position.y = -output.position.y;

    // Reconstruct world ray
    float4 clipPos = float4(output.texCoord * 2.0 - 1.0, 1.0, 1.0);
    clipPos.y = -clipPos.y;
    float4 worldPos = mul(g_InvViewProjMatrix, clipPos);
    worldPos.xyz /= worldPos.w;

    output.rayDir = normalize(worldPos.xyz - g_CameraPosition.xyz);

    return output;
}

// Pixel shader
float4 PSMain(VSOutput input) : SV_Target {
    float3 rayOrigin = g_CameraPosition.xyz;
    float3 rayDir = normalize(input.rayDir);

    // Blue noise dithering for raymarching
    float dither = frac(sin(dot(input.position.xy, float2(12.9898, 78.233))) * 43758.5453);
    dither = lerp(dither, 0.5, 0.18);

    // Maximum ray distance
    float maxDist = 50000.0;

    // Raymarch clouds
    float4 clouds = RaymarchClouds(rayOrigin, rayDir, maxDist, dither);

    // Output premultiplied alpha
    return float4(clouds.rgb, clouds.a);
}
