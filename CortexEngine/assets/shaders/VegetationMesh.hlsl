// VegetationMesh.hlsl
// Instanced vegetation mesh shader with wind animation and LOD crossfade.

// Vegetation instance data (matches VegetationInstanceGPU in C++)
struct VegetationInstance {
    float4x4 worldMatrix;        // Full transform
    float4 colorTint;            // Per-instance color variation
    float4 windParams;           // x=phase, y=strength, z=frequency, w=unused
    uint prototypeIndex;
    uint lodLevel;
    float fadeAlpha;             // LOD crossfade
    float padding;
};

// Constant buffer for vegetation rendering
cbuffer VegetationConstants : register(b6) {
    float4x4 g_ViewProj;
    float4 g_CameraPosition;     // xyz = position, w = unused
    float4 g_WindDirection;      // xy = direction, z = speed, w = time
    float4 g_WindParams;         // x = gustStrength, y = gustFreq, z = turbulence, w = unused
    float4 g_LODDistances;       // x = lod0, y = lod1, z = lod2, w = cull
    float4 g_FadeParams;         // x = crossfadeRange, yzw = unused
};

// Instance buffer
StructuredBuffer<VegetationInstance> g_Instances : register(t20);

// Input vertex format
struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;        // Vertex color (for wind weight in alpha)
};

// Output to pixel shader
struct PSInput {
    float4 position : SV_POSITION;
    float3 worldPos : WORLD_POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
    float fadeAlpha : FADE_ALPHA;
    uint instanceID : INSTANCE_ID;
};

// ============================================================================
// WIND ANIMATION
// ============================================================================

// Simple noise for wind variation
float WindNoise(float2 pos, float time) {
    float2 p = pos * 0.1 + time * 0.5;
    return sin(p.x * 1.3 + p.y * 0.7) * cos(p.x * 0.9 + p.y * 1.1) * 0.5 + 0.5;
}

// Compute wind displacement
float3 ComputeWindDisplacement(float3 worldPos, float3 localPos, float windWeight, VegetationInstance inst) {
    float time = g_WindDirection.w;
    float2 windDir = normalize(g_WindDirection.xy);
    float windSpeed = g_WindDirection.z;

    // Per-instance phase offset
    float phase = inst.windParams.x;
    float instanceStrength = inst.windParams.y;

    // Wind gust
    float gustFreq = g_WindParams.y;
    float gustStrength = g_WindParams.x;
    float gust = sin(time * gustFreq + phase) * gustStrength;

    // Turbulence noise
    float turbulence = g_WindParams.z;
    float noise = WindNoise(worldPos.xz, time) * turbulence;

    // Primary wind wave
    float primaryWave = sin(time * windSpeed + worldPos.x * 0.1 + worldPos.z * 0.1 + phase);

    // Combine factors
    float totalStrength = (primaryWave + gust + noise) * windWeight * instanceStrength;

    // Displacement increases with height (trees sway more at top)
    float heightFactor = saturate(localPos.y * 0.1);
    totalStrength *= heightFactor;

    // Apply displacement in wind direction
    float3 displacement;
    displacement.x = windDir.x * totalStrength;
    displacement.y = abs(totalStrength) * -0.1;  // Slight downward pull
    displacement.z = windDir.y * totalStrength;

    return displacement;
}

// ============================================================================
// VERTEX SHADER
// ============================================================================

PSInput VSMain(VSInput input, uint instanceID : SV_InstanceID) {
    PSInput output;

    VegetationInstance inst = g_Instances[instanceID];

    // Transform to world space
    float4 worldPos = mul(inst.worldMatrix, float4(input.position, 1.0));

    // Apply wind animation (vertex color alpha = wind weight)
    float windWeight = input.color.a;
    if (windWeight > 0.01) {
        float3 windDisplacement = ComputeWindDisplacement(worldPos.xyz, input.position, windWeight, inst);
        worldPos.xyz += windDisplacement;
    }

    output.worldPos = worldPos.xyz;

    // Transform to clip space
    output.position = mul(g_ViewProj, worldPos);

    // Transform normal
    float3x3 normalMatrix = (float3x3)inst.worldMatrix;
    output.normal = normalize(mul(normalMatrix, input.normal));

    // Transform tangent
    output.tangent.xyz = normalize(mul(normalMatrix, input.tangent.xyz));
    output.tangent.w = input.tangent.w;

    // Pass through other attributes
    output.texCoord = input.texCoord;
    output.color = input.color * inst.colorTint;
    output.fadeAlpha = inst.fadeAlpha;
    output.instanceID = instanceID;

    return output;
}

// ============================================================================
// PIXEL SHADER
// ============================================================================

// Textures
Texture2D g_AlbedoTex : register(t0);
Texture2D g_NormalTex : register(t1);
Texture2D g_RoughnessTex : register(t2);
SamplerState g_Sampler : register(s0);

struct PSOutput {
    float4 albedo : SV_TARGET0;
    float4 normalRoughness : SV_TARGET1;
    float4 emissiveMetallic : SV_TARGET2;
};

// Dithered alpha for LOD crossfade
float DitherAlpha(float2 screenPos, float alpha) {
    // 4x4 Bayer matrix for ordered dithering
    const float bayerMatrix[16] = {
        0.0/16.0, 8.0/16.0, 2.0/16.0, 10.0/16.0,
        12.0/16.0, 4.0/16.0, 14.0/16.0, 6.0/16.0,
        3.0/16.0, 11.0/16.0, 1.0/16.0, 9.0/16.0,
        15.0/16.0, 7.0/16.0, 13.0/16.0, 5.0/16.0
    };

    int2 pixel = int2(screenPos) % 4;
    float threshold = bayerMatrix[pixel.y * 4 + pixel.x];

    return alpha > threshold ? 1.0 : 0.0;
}

PSOutput PSMain(PSInput input) {
    PSOutput output;

    // Sample textures
    float4 albedo = g_AlbedoTex.Sample(g_Sampler, input.texCoord);

    // Alpha test
    if (albedo.a < 0.5) {
        discard;
    }

    // LOD crossfade using dithering
    if (input.fadeAlpha < 1.0) {
        float dither = DitherAlpha(input.position.xy, input.fadeAlpha);
        if (dither < 0.5) {
            discard;
        }
    }

    // Apply vertex color tint
    albedo.rgb *= input.color.rgb;

    // Sample normal map
    float3 normalTS = g_NormalTex.Sample(g_Sampler, input.texCoord).xyz * 2.0 - 1.0;

    // Build TBN matrix
    float3 N = normalize(input.normal);
    float3 T = normalize(input.tangent.xyz);
    T = normalize(T - N * dot(N, T));  // Gram-Schmidt orthogonalize
    float3 B = cross(N, T) * input.tangent.w;
    float3x3 TBN = float3x3(T, B, N);

    // Transform normal to world space
    float3 normalWS = normalize(mul(normalTS, TBN));

    // Sample roughness
    float roughness = g_RoughnessTex.Sample(g_Sampler, input.texCoord).r;

    // Output to G-buffer
    output.albedo = float4(albedo.rgb, 1.0);
    output.normalRoughness = float4(normalWS * 0.5 + 0.5, roughness);
    output.emissiveMetallic = float4(0, 0, 0, 0);  // Vegetation is non-metallic

    return output;
}

// ============================================================================
// SHADOW PASS
// ============================================================================

struct ShadowVSOutput {
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD;
};

cbuffer ShadowConstants : register(b7) {
    float4x4 g_LightViewProj;
};

ShadowVSOutput VSShadow(VSInput input, uint instanceID : SV_InstanceID) {
    ShadowVSOutput output;

    VegetationInstance inst = g_Instances[instanceID];

    // Transform to world space
    float4 worldPos = mul(inst.worldMatrix, float4(input.position, 1.0));

    // Apply wind (simplified for shadow pass)
    float windWeight = input.color.a;
    if (windWeight > 0.01) {
        float3 windDisplacement = ComputeWindDisplacement(worldPos.xyz, input.position, windWeight, inst);
        worldPos.xyz += windDisplacement;
    }

    // Transform to light clip space
    output.position = mul(g_LightViewProj, worldPos);
    output.texCoord = input.texCoord;

    return output;
}

void PSShadow(ShadowVSOutput input) {
    float4 albedo = g_AlbedoTex.Sample(g_Sampler, input.texCoord);
    if (albedo.a < 0.5) {
        discard;
    }
}
