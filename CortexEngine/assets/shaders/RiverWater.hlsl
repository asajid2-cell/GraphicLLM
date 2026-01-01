// RiverWater.hlsl
// Specialized water shader for rivers, streams, and lakes.
// Features: flow animation, foam at rapids, depth-based color, reflections.

// ============================================================================
// CONSTANT BUFFERS
// ============================================================================

cbuffer FrameConstants : register(b0) {
    float4x4 g_ViewMatrix;
    float4x4 g_ProjectionMatrix;
    float4x4 g_ViewProjectionMatrix;
    float4x4 g_InvProjectionMatrix;
    float4 g_CameraPosition;
    float4 g_TimeAndExposure;  // x = time, y = deltaTime, z = exposure
};

cbuffer ObjectConstants : register(b1) {
    float4x4 g_ModelMatrix;
    float4x4 g_NormalMatrix;
};

cbuffer RiverConstants : register(b6) {
    float4x4 g_RiverViewProj;
    float4 g_RiverCameraPos;     // xyz = position, w = time
    float4 g_ShallowColor;       // rgb = color, a = transparency
    float4 g_DeepColor;          // rgb = color, a = refraction strength
    float4 g_FoamParams;         // x = threshold, y = density, z = speed, w = scale
    float4 g_WaveParams;         // x = amplitude, y = frequency, z = speed, w = UV scale
    float4 g_FlowDirection;      // xy = primary dir, z = secondary strength, w = time offset
    float4 g_RippleParams;       // x = scale, y = speed, z = strength, w = unused
};

// ============================================================================
// TEXTURES AND SAMPLERS
// ============================================================================

Texture2D g_FlowMap : register(t0);           // Flow direction texture (optional)
Texture2D g_NormalMap : register(t1);         // Water normal map
Texture2D g_FoamTexture : register(t2);       // Foam/rapids texture
Texture2D g_DepthTexture : register(t3);      // Scene depth for shore fade
Texture2D g_SceneColor : register(t4);        // Scene color for refraction
TextureCube g_EnvironmentMap : register(t5);  // Cubemap for reflections

SamplerState g_LinearSampler : register(s0);
SamplerState g_WrapSampler : register(s1);

// ============================================================================
// STRUCTURES
// ============================================================================

struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD0;
    float4 vertexData : COLOR;     // x = flowSpeed, y = depth, z = distFromBank, w = turbulence
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 worldPos : WORLD_POS;
    float3 worldNormal : WORLD_NORMAL;
    float2 texCoord : TEXCOORD0;
    float4 projCoord : PROJ_COORD;
    float4 waterData : WATER_DATA;   // x = flowSpeed, y = depth, z = distFromBank, w = turbulence
    float3 viewDir : VIEW_DIR;
};

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Animated flow UV - creates flowing water effect
float2 FlowUV(float2 uv, float2 flowDir, float time, float speed) {
    // Two phase offset for seamless looping
    float phase0 = frac(time * speed);
    float phase1 = frac(time * speed + 0.5);

    // Weight for blending between phases
    float weight = abs(phase0 - 0.5) * 2.0;

    float2 uv0 = uv - flowDir * phase0;
    float2 uv1 = uv - flowDir * phase1;

    return lerp(uv0, uv1, weight);
}

// Two-layer animated flow for more complex water movement
float3 SampleFlowNormal(float2 uv, float2 flowDir, float time, float speed) {
    float phase0 = frac(time * speed);
    float phase1 = frac(time * speed + 0.5);
    float weight = abs(phase0 - 0.5) * 2.0;

    float2 uv0 = uv - flowDir * phase0;
    float2 uv1 = uv - flowDir * phase1;

    float3 normal0 = g_NormalMap.Sample(g_WrapSampler, uv0).xyz * 2.0 - 1.0;
    float3 normal1 = g_NormalMap.Sample(g_WrapSampler, uv1).xyz * 2.0 - 1.0;

    return normalize(lerp(normal0, normal1, weight));
}

// Gerstner wave for surface displacement
float3 GerstnerWave(float2 position, float2 direction, float steepness, float wavelength,
                     float amplitude, float speed, float time) {
    float k = 2.0 * 3.14159 / wavelength;
    float c = sqrt(9.8 / k);
    float2 d = normalize(direction);
    float f = k * (dot(d, position) - c * time * speed);

    float sinF = sin(f);
    float cosF = cos(f);

    return float3(
        d.x * amplitude * cosF,
        amplitude * sinF,
        d.y * amplitude * cosF
    );
}

// Fresnel approximation
float Fresnel(float3 viewDir, float3 normal, float power) {
    float NdotV = saturate(dot(normal, viewDir));
    return pow(1.0 - NdotV, power);
}

// Screen-space refraction offset
float2 RefractionOffset(float3 normal, float strength) {
    return normal.xz * strength;
}

// ============================================================================
// VERTEX SHADER
// ============================================================================

VSOutput VSMain(VSInput input) {
    VSOutput output;

    float3 worldPos = mul(g_ModelMatrix, float4(input.position, 1.0)).xyz;
    float time = g_RiverCameraPos.w;

    // Apply wave displacement based on flow speed and position
    float flowSpeed = input.vertexData.x;
    float2 flowDir = normalize(g_FlowDirection.xy);

    // Multiple wave layers for natural appearance
    float3 wave1 = GerstnerWave(worldPos.xz, flowDir, 0.5, 4.0,
                                 g_WaveParams.x * flowSpeed, g_WaveParams.z, time);
    float3 wave2 = GerstnerWave(worldPos.xz, flowDir * 0.7 + float2(0.3, 0.2), 0.3, 2.5,
                                 g_WaveParams.x * 0.5 * flowSpeed, g_WaveParams.z * 1.3, time);

    // Reduce waves near banks
    float bankFactor = input.vertexData.z;  // 0 at bank, 1 at center
    float waveStrength = smoothstep(0.0, 0.5, bankFactor);

    worldPos.y += (wave1.y + wave2.y) * waveStrength;
    worldPos.xz += (wave1.xz + wave2.xz) * waveStrength * 0.2;

    output.worldPos = worldPos;
    output.position = mul(g_ViewProjectionMatrix, float4(worldPos, 1.0));
    output.projCoord = output.position;

    // Transform normal
    float3 worldNormal = normalize(mul((float3x3)g_NormalMatrix, input.normal));
    output.worldNormal = worldNormal;

    output.texCoord = input.texCoord;
    output.waterData = input.vertexData;

    // View direction
    output.viewDir = normalize(g_CameraPosition.xyz - worldPos);

    return output;
}

// ============================================================================
// PIXEL SHADER
// ============================================================================

float4 PSMain(VSOutput input) : SV_TARGET {
    float time = g_RiverCameraPos.w;
    float2 uv = input.texCoord * g_WaveParams.w;

    // Flow parameters
    float2 flowDir = normalize(g_FlowDirection.xy);
    float flowSpeed = input.waterData.x;
    float waterDepth = input.waterData.y;
    float distFromBank = input.waterData.z;
    float turbulence = input.waterData.w;

    // Sample animated normal map
    float3 normalTS = SampleFlowNormal(uv, flowDir * flowSpeed, time, g_WaveParams.z);

    // Add ripple layer
    float2 rippleUV = uv * g_RippleParams.x;
    float3 rippleNormal = g_NormalMap.Sample(g_WrapSampler, rippleUV + time * g_RippleParams.y * 0.1).xyz * 2.0 - 1.0;
    normalTS = normalize(normalTS + rippleNormal * g_RippleParams.z * 0.3);

    // Build TBN (simplified - assumes mostly flat water)
    float3 N = normalize(input.worldNormal);
    float3 T = normalize(float3(1, 0, 0) - N * N.x);
    float3 B = cross(N, T);
    float3x3 TBN = float3x3(T, B, N);

    float3 worldNormal = normalize(mul(normalTS, TBN));

    // View direction
    float3 viewDir = normalize(input.viewDir);

    // Fresnel for reflection/refraction blend
    float fresnel = Fresnel(viewDir, worldNormal, 5.0);

    // Water color based on depth
    float depthFactor = saturate(waterDepth / 5.0);  // Normalize to 0-1 over 5 units
    float3 waterColor = lerp(g_ShallowColor.rgb, g_DeepColor.rgb, depthFactor);

    // Foam at rapids and near banks
    float foamFactor = 0.0;

    // Foam from turbulence (rapids, waterfalls)
    float2 foamUV = uv * g_FoamParams.w;
    float2 foamFlow = FlowUV(foamUV, flowDir * flowSpeed, time, g_FoamParams.z);
    float foamSample = g_FoamTexture.Sample(g_WrapSampler, foamFlow).r;

    // More foam in turbulent areas
    foamFactor += step(g_FoamParams.x, turbulence) * foamSample * g_FoamParams.y;

    // Foam at banks (shore foam)
    float bankFoam = (1.0 - smoothstep(0.0, 0.2, distFromBank)) * 0.5;
    float2 bankFoamUV = uv * 2.0 + time * 0.3;
    float bankFoamSample = g_FoamTexture.Sample(g_WrapSampler, bankFoamUV).r;
    foamFactor += bankFoam * bankFoamSample;

    // Speed-based foam (fast water creates whitecaps)
    float speedFoam = smoothstep(1.5, 3.0, flowSpeed) * foamSample * 0.7;
    foamFactor += speedFoam;

    foamFactor = saturate(foamFactor);

    // Reflection
    float3 reflectionDir = reflect(-viewDir, worldNormal);
    float3 reflectionColor = g_EnvironmentMap.Sample(g_LinearSampler, reflectionDir).rgb;

    // Refraction (screen-space)
    float2 screenUV = input.projCoord.xy / input.projCoord.w * 0.5 + 0.5;
    screenUV.y = 1.0 - screenUV.y;

    float2 refractionOffset = RefractionOffset(worldNormal, g_DeepColor.a);
    float2 refractionUV = screenUV + refractionOffset;
    refractionUV = clamp(refractionUV, 0.001, 0.999);

    float3 refractionColor = g_SceneColor.Sample(g_LinearSampler, refractionUV).rgb;
    refractionColor = lerp(refractionColor, waterColor, depthFactor * 0.5);

    // Combine reflection and refraction
    float3 baseColor = lerp(refractionColor, reflectionColor, fresnel * g_ShallowColor.a);

    // Add water tint
    baseColor *= waterColor;

    // Add foam
    float3 foamColor = float3(0.95, 0.98, 1.0);
    baseColor = lerp(baseColor, foamColor, foamFactor);

    // Simple specular highlight
    float3 lightDir = normalize(float3(0.5, 1.0, 0.3));
    float3 halfVec = normalize(lightDir + viewDir);
    float spec = pow(saturate(dot(worldNormal, halfVec)), 256.0);
    baseColor += spec * float3(1.0, 0.95, 0.9) * 0.5;

    // Transparency based on depth and foam
    float alpha = lerp(g_ShallowColor.a, 1.0, depthFactor * 0.3 + foamFactor);

    return float4(baseColor, alpha);
}

// ============================================================================
// SIMPLIFIED FORWARD PASS (for lower quality settings)
// ============================================================================

float4 PSForward(VSOutput input) : SV_TARGET {
    float time = g_RiverCameraPos.w;
    float2 uv = input.texCoord * g_WaveParams.w;

    float2 flowDir = normalize(g_FlowDirection.xy);
    float flowSpeed = input.waterData.x;
    float waterDepth = input.waterData.y;
    float distFromBank = input.waterData.z;

    // Simple animated normal
    float2 flowUV = FlowUV(uv, flowDir * flowSpeed, time, g_WaveParams.z);
    float3 normalTS = g_NormalMap.Sample(g_WrapSampler, flowUV).xyz * 2.0 - 1.0;

    float3 N = normalize(input.worldNormal);
    float3 T = normalize(float3(1, 0, 0) - N * N.x);
    float3 B = cross(N, T);
    float3x3 TBN = float3x3(T, B, N);
    float3 worldNormal = normalize(mul(normalTS, TBN));

    float3 viewDir = normalize(input.viewDir);
    float fresnel = Fresnel(viewDir, worldNormal, 4.0);

    // Depth-based color
    float depthFactor = saturate(waterDepth / 5.0);
    float3 waterColor = lerp(g_ShallowColor.rgb, g_DeepColor.rgb, depthFactor);

    // Simple lighting
    float3 lightDir = normalize(float3(0.5, 1.0, 0.3));
    float NdotL = saturate(dot(worldNormal, lightDir));

    float3 ambient = waterColor * 0.3;
    float3 diffuse = waterColor * NdotL * 0.7;

    float3 finalColor = ambient + diffuse;

    // Specular
    float3 halfVec = normalize(lightDir + viewDir);
    float spec = pow(saturate(dot(worldNormal, halfVec)), 64.0);
    finalColor += spec * 0.3;

    // Simple foam
    float2 foamUV = uv * g_FoamParams.w;
    float2 foamFlow = FlowUV(foamUV, flowDir * flowSpeed, time, g_FoamParams.z);
    float foam = g_FoamTexture.Sample(g_WrapSampler, foamFlow).r;
    foam *= (1.0 - smoothstep(0.0, 0.3, distFromBank)) + smoothstep(2.0, 3.0, flowSpeed);
    foam = saturate(foam * 0.5);

    finalColor = lerp(finalColor, float3(1, 1, 1), foam);

    return float4(finalColor, g_ShallowColor.a);
}

// ============================================================================
// LAKE SHADER (calmer water)
// ============================================================================

float4 PSLake(VSOutput input) : SV_TARGET {
    float time = g_RiverCameraPos.w;
    float2 uv = input.texCoord * g_WaveParams.w * 0.5;  // Larger UV scale for lakes

    // Gentle waves in wind direction
    float2 windDir = normalize(g_FlowDirection.zw);
    float windSpeed = 0.2;

    // Layer multiple gentle normal samples
    float2 uv1 = uv + windDir * time * windSpeed;
    float2 uv2 = uv * 0.7 + windDir * time * windSpeed * 1.3 + float2(0.5, 0.3);

    float3 normal1 = g_NormalMap.Sample(g_WrapSampler, uv1).xyz * 2.0 - 1.0;
    float3 normal2 = g_NormalMap.Sample(g_WrapSampler, uv2).xyz * 2.0 - 1.0;
    float3 normalTS = normalize(normal1 + normal2 * 0.5);
    normalTS.xy *= 0.3;  // Flatten for calmer appearance
    normalTS = normalize(normalTS);

    float3 N = normalize(input.worldNormal);
    float3 T = normalize(float3(1, 0, 0) - N * N.x);
    float3 B = cross(N, T);
    float3x3 TBN = float3x3(T, B, N);
    float3 worldNormal = normalize(mul(normalTS, TBN));

    float3 viewDir = normalize(input.viewDir);
    float fresnel = Fresnel(viewDir, worldNormal, 5.0);

    float waterDepth = input.waterData.y;
    float distFromBank = input.waterData.z;

    // Depth-based color (deeper for lakes)
    float depthFactor = saturate(waterDepth / 10.0);
    float3 waterColor = lerp(g_ShallowColor.rgb, g_DeepColor.rgb, depthFactor);

    // Reflection
    float3 reflectionDir = reflect(-viewDir, worldNormal);
    float3 reflectionColor = g_EnvironmentMap.Sample(g_LinearSampler, reflectionDir).rgb;

    // Screen-space refraction
    float2 screenUV = input.projCoord.xy / input.projCoord.w * 0.5 + 0.5;
    screenUV.y = 1.0 - screenUV.y;
    float2 refractionOffset = worldNormal.xz * g_DeepColor.a * 0.5;
    float2 refractionUV = clamp(screenUV + refractionOffset, 0.001, 0.999);
    float3 refractionColor = g_SceneColor.Sample(g_LinearSampler, refractionUV).rgb;
    refractionColor = lerp(refractionColor, waterColor, depthFactor * 0.6);

    // Blend reflection and refraction
    float3 baseColor = lerp(refractionColor, reflectionColor, fresnel * 0.8);
    baseColor *= waterColor;

    // Shore foam
    float shoreFoam = (1.0 - smoothstep(0.0, 0.15, distFromBank));
    float2 shoreUV = uv * 3.0 + time * 0.1;
    float shoreFoamSample = g_FoamTexture.Sample(g_WrapSampler, shoreUV).r;
    baseColor = lerp(baseColor, float3(0.95, 0.98, 1.0), shoreFoam * shoreFoamSample * 0.6);

    // Specular
    float3 lightDir = normalize(float3(0.5, 1.0, 0.3));
    float3 halfVec = normalize(lightDir + viewDir);
    float spec = pow(saturate(dot(worldNormal, halfVec)), 512.0);
    baseColor += spec * float3(1.0, 0.95, 0.9) * 0.4;

    float alpha = lerp(g_ShallowColor.a * 0.9, 1.0, depthFactor * 0.4);

    return float4(baseColor, alpha);
}

// ============================================================================
// DEPTH-ONLY PASS (for shadow maps)
// ============================================================================

void VSDepthOnly(
    float3 position : POSITION,
    float4 vertexData : COLOR,
    out float4 outPosition : SV_POSITION)
{
    float3 worldPos = mul(g_ModelMatrix, float4(position, 1.0)).xyz;
    outPosition = mul(g_ViewProjectionMatrix, float4(worldPos, 1.0));
}

void PSDepthOnly() {
    // Empty - just writes depth
}
