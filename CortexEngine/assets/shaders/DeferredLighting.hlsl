// DeferredLighting.hlsl
// Phase 2.2: Deferred lighting pass for visibility buffer
// Reads G-buffers and applies PBR lighting

// G-buffer inputs
Texture2D<float4> g_GBufferAlbedo : register(t0);           // RGB = albedo, A = alpha
Texture2D<float4> g_GBufferNormalRoughness : register(t1);  // RGB = normal, A = roughness
Texture2D<float4> g_GBufferEmissiveMetallic : register(t2); // RGB = emissive, A = metallic
Texture2D<float> g_DepthBuffer : register(t3);              // Depth for position reconstruction

// Environment/shadow maps (matching forward renderer)
TextureCube<float4> g_EnvironmentMap : register(t4);
Texture2D<float> g_ShadowMap : register(t5);

SamplerState g_LinearSampler : register(s0);
SamplerComparisonState g_ShadowSampler : register(s1);

cbuffer PerFrameData : register(b0) {
    float4x4 g_InvViewProj;          // Inverse view-projection for position reconstruction
    float4x4 g_ViewMatrix;
    float4x4 g_LightViewProj;        // Shadow map view-projection

    float3 g_CameraPos;
    float _pad0;

    float3 g_SunDirection;           // Directional light direction
    float _pad1;

    float3 g_SunColor;               // Directional light color
    float g_SunIntensity;

    float g_IBLDiffuseIntensity;     // Image-based lighting diffuse
    float g_IBLSpecularIntensity;    // Image-based lighting specular
    float2 _pad2;
};

static const float PI = 3.14159265f;

// --- PBR Helper Functions ---

// GGX Normal Distribution Function
float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / max(denom, 0.0001);
}

// Schlick-GGX Geometry Function
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

// Fresnel-Schlick approximation
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// Simple PCF shadow sampling
float SampleShadow(float3 worldPos) {
    // Transform to shadow map space
    float4 shadowPos = mul(g_LightViewProj, float4(worldPos, 1.0));
    shadowPos.xyz /= shadowPos.w;

    // Convert to texture coordinates
    float2 shadowUV = shadowPos.xy * float2(0.5, -0.5) + 0.5;

    // Out of shadow map bounds = fully lit
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 || shadowUV.y < 0.0 || shadowUV.y > 1.0 || shadowPos.z < 0.0) {
        return 1.0;
    }

    // Simple PCF 3x3
    float shadow = 0.0;
    float2 texelSize = 1.0 / float2(2048.0, 2048.0);

    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float2 offset = float2(x, y) * texelSize;
            shadow += g_ShadowMap.SampleCmpLevelZero(g_ShadowSampler, shadowUV + offset, shadowPos.z - 0.001);
        }
    }

    return shadow / 9.0;
}

// Reconstruct world position from depth
float3 ReconstructWorldPosition(float2 uv, float depth) {
    // Convert UV and depth to NDC
    float4 ndc = float4(
        uv.x * 2.0 - 1.0,
        (1.0 - uv.y) * 2.0 - 1.0,
        depth,
        1.0
    );

    // Transform to world space
    float4 worldPos = mul(g_InvViewProj, ndc);
    return worldPos.xyz / worldPos.w;
}

struct VSOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

// Fullscreen triangle vertex shader
VSOutput VSMain(uint vertexID : SV_VertexID) {
    VSOutput output;

    // Generate fullscreen triangle
    float2 texCoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(texCoord * float2(2, -2) + float2(-1, 1), 0, 1);
    output.texCoord = texCoord;

    return output;
}

// Deferred lighting pixel shader
float4 PSMain(VSOutput input) : SV_Target0 {
    uint2 pixelCoord = uint2(input.position.xy);

    // Sample G-buffers
    float4 albedo = g_GBufferAlbedo.Load(int3(pixelCoord, 0));
    float4 normalRoughness = g_GBufferNormalRoughness.Load(int3(pixelCoord, 0));
    float4 emissiveMetallic = g_GBufferEmissiveMetallic.Load(int3(pixelCoord, 0));
    float depth = g_DepthBuffer.Load(int3(pixelCoord, 0));

    // Unpack G-buffer data
    float3 albedoColor = albedo.rgb;
    float3 normal = normalize(normalRoughness.xyz);
    float roughness = normalRoughness.w;
    float3 emissive = emissiveMetallic.rgb;
    float metallic = emissiveMetallic.a;

    // Check for background pixels (depth = 1.0)
    if (depth >= 0.9999) {
        // Sky/background - could sample environment map here
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    // Reconstruct world position from depth
    float3 worldPos = ReconstructWorldPosition(input.texCoord, depth);

    // View direction
    float3 V = normalize(g_CameraPos - worldPos);

    // PBR material properties
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedoColor, metallic);

    // --- Directional Light (Sun) ---
    float3 L = normalize(-g_SunDirection);
    float3 H = normalize(V + L);

    float NdotL = max(dot(normal, L), 0.0);

    // Cook-Torrance BRDF
    float NDF = DistributionGGX(normal, H, roughness);
    float G = GeometrySmith(normal, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(normal, V), 0.0) * NdotL;
    float3 specular = numerator / max(denominator, 0.001);

    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);

    // Shadow
    float shadow = SampleShadow(worldPos);

    // Direct lighting
    float3 directLight = (kD * albedoColor / PI + specular) * g_SunColor * g_SunIntensity * NdotL * shadow;

    // --- Image-Based Lighting (IBL) ---
    float3 ambient = float3(0.03, 0.03, 0.03); // Fallback ambient

    // Simple diffuse IBL (could be improved with irradiance map)
    if (g_IBLDiffuseIntensity > 0.0) {
        float3 irradiance = g_EnvironmentMap.SampleLevel(g_LinearSampler, normal, 5.0).rgb; // High mip = diffuse
        ambient = irradiance * albedoColor * g_IBLDiffuseIntensity;
    }

    // Specular IBL (reflection)
    if (g_IBLSpecularIntensity > 0.0) {
        float3 R = reflect(-V, normal);
        float mipLevel = roughness * 5.0; // Rougher = higher mip
        float3 prefilteredColor = g_EnvironmentMap.SampleLevel(g_LinearSampler, R, mipLevel).rgb;
        float3 specularIBL = prefilteredColor * (F * g_IBLSpecularIntensity);
        ambient += specularIBL;
    }

    // Final color
    float3 color = directLight + ambient + emissive;

    return float4(color, 1.0);
}
