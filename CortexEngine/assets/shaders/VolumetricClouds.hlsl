// VolumetricClouds.hlsl
// Raymarched volumetric cloud rendering.
// Reference: "Real-Time Volumetric Cloudscapes" - Horizon Zero Dawn GDC
// Reference: "The Real-Time Volumetric Cloudscapes of Horizon" - SIGGRAPH 2015

#include "Common.hlsli"

// Cloud layer parameters
cbuffer CloudsCB : register(b6) {
    float4 g_CloudLayerParams;      // x=lowAltitude, y=highAltitude, z=coverage, w=density
    float4 g_CloudShapeParams;      // x=baseScale, y=detailScale, z=erosion, w=curliness
    float4 g_CloudLightParams;      // x=absorb, y=scatter, z=ambientMult, w=sunMult
    float4 g_CloudAnimParams;       // xy=windDir, z=windSpeed, w=time
    float4 g_SunDirection;          // xyz=direction, w=intensity
    float4 g_SunColor;              // xyz=color, w=unused
    float4 g_AmbientColor;          // xyz=ambient, w=unused
    float4 g_CloudColor;            // xyz=base cloud color, w=unused
    float4x4 g_InvViewProj;
    float4 g_CameraPosition;
    float4 g_RaymarchParams;        // x=maxSteps, y=stepSize, z=jitter, w=unused
};

// Noise textures
Texture3D<float> g_ShapeNoise : register(t10);      // Low-freq Worley/Perlin
Texture3D<float> g_DetailNoise : register(t11);     // High-freq detail
Texture2D<float2> g_CurlNoise : register(t12);      // 2D curl noise for distortion
Texture2D<float> g_WeatherMap : register(t13);      // Coverage/type map

SamplerState g_LinearWrap : register(s0);
SamplerState g_PointWrap : register(s1);

// Constants
static const float EARTH_RADIUS = 6371000.0;        // meters
static const float CLOUD_TOP_OFFSET = 10000.0;      // Cloud layer offset from viewer
static const int MAX_STEPS = 64;
static const int LIGHT_STEPS = 6;

// Remapping utility
float Remap(float value, float oldMin, float oldMax, float newMin, float newMax) {
    return newMin + (value - oldMin) / (oldMax - oldMin) * (newMax - newMin);
}

// Get height fraction within cloud layer (0 at bottom, 1 at top)
float GetHeightFraction(float altitude) {
    float lowAlt = g_CloudLayerParams.x;
    float highAlt = g_CloudLayerParams.y;
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
    float2 uv = worldXZ * 0.00001 + g_CloudAnimParams.xy * g_CloudAnimParams.z * g_CloudAnimParams.w * 0.001;
    float2 weather = g_WeatherMap.SampleLevel(g_LinearWrap, uv, 0);
    return weather;  // x=coverage, y=cloudType
}

// Sample low-frequency shape noise
float SampleShapeNoise(float3 position) {
    float3 uvw = position * g_CloudShapeParams.x * 0.0001;
    uvw.xy += g_CloudAnimParams.xy * g_CloudAnimParams.z * g_CloudAnimParams.w * 0.0001;

    float noise = g_ShapeNoise.SampleLevel(g_LinearWrap, uvw, 0);

    // Apply curl distortion
    float2 curlUV = position.xz * 0.00005;
    float2 curl = g_CurlNoise.SampleLevel(g_LinearWrap, curlUV, 0) * 2.0 - 1.0;
    uvw.xz += curl * g_CloudShapeParams.w * 0.1;

    return noise;
}

// Sample high-frequency detail noise
float SampleDetailNoise(float3 position, float mipLevel) {
    float3 uvw = position * g_CloudShapeParams.y * 0.001;
    uvw.xy += g_CloudAnimParams.xy * g_CloudAnimParams.z * g_CloudAnimParams.w * 0.0005;

    return g_DetailNoise.SampleLevel(g_LinearWrap, uvw, mipLevel);
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
    float coverage = weather.x * g_CloudLayerParams.z;
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
        float erosion = g_CloudShapeParams.z;
        float detailModifier = lerp(detail, 1.0 - detail, saturate(heightFraction * 5.0));
        density = saturate(Remap(density, detailModifier * erosion, 1.0, 0.0, 1.0));
    }

    return density * g_CloudLayerParams.w;
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
    float3 lightDir = normalize(g_SunDirection.xyz);
    float stepSize = (g_CloudLayerParams.y - g_CloudLayerParams.x) / float(LIGHT_STEPS);

    float totalDensity = 0.0;
    float3 lightPos = position;

    [unroll]
    for (int i = 0; i < LIGHT_STEPS; ++i) {
        lightPos += lightDir * stepSize;
        float density = SampleCloudDensity(lightPos, 2.0, false);
        totalDensity += density * stepSize;
    }

    float transmittance = BeerLambert(totalDensity, g_CloudLightParams.x);
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
    float lowAlt = g_CloudLayerParams.x;
    float highAlt = g_CloudLayerParams.y;

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
    float3 sunDir = normalize(g_SunDirection.xyz);
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
    float stepSize = g_RaymarchParams.y;
    int maxSteps = int(g_RaymarchParams.x);

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
            float3 ambient = g_AmbientColor.rgb * g_CloudLightParams.z;
            float3 sun = g_SunColor.rgb * lightTransmit * phase * g_CloudLightParams.w;

            float3 luminance = g_CloudColor.rgb * (ambient + sun);

            // Powder effect
            float powder = PowderEffect(density * stepSize, cosAngle);
            luminance *= powder;

            // Accumulate
            float sampleTransmit = BeerLambert(density * stepSize, g_CloudLightParams.y);
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
    float4 worldPos = mul(g_InvViewProj, clipPos);
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
    dither = lerp(dither, 0.5, 1.0 - g_RaymarchParams.z);

    // Maximum ray distance
    float maxDist = 50000.0;

    // Raymarch clouds
    float4 clouds = RaymarchClouds(rayOrigin, rayDir, maxDist, dither);

    // Output premultiplied alpha
    return float4(clouds.rgb, clouds.a);
}
