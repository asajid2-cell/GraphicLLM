// VegetationBillboard.hlsl
// Billboard/impostor shader for distant vegetation rendering.
// Uses camera-facing quads with alpha cutout.

// Billboard instance data
struct BillboardInstance {
    float3 position;            // World center position
    float2 size;                // Width, height
    float4 uvBounds;            // xy = min, zw = max (atlas region)
    float4 colorTint;
    float rotation;             // Rotation around view axis
    float windPhase;
    float windStrength;
    float padding;
};

// Constant buffer
cbuffer BillboardConstants : register(b6) {
    float4x4 g_ViewProj;
    float4x4 g_View;
    float4 g_CameraPosition;
    float4 g_CameraRight;
    float4 g_CameraUp;
    float4 g_WindDirection;      // xy = dir, z = speed, w = time
    float4 g_WindParams;
};

// Instance buffer
StructuredBuffer<BillboardInstance> g_BillboardInstances : register(t20);

// Billboard atlas texture
Texture2D g_BillboardAtlas : register(t0);
Texture2D g_BillboardNormal : register(t1);
SamplerState g_Sampler : register(s0);

// Vertex output
struct VSOutput {
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
    float3 worldPos : WORLD_POS;
};

// ============================================================================
// VERTEX SHADER
// ============================================================================

// Expand point to quad in vertex shader (4 vertices per billboard)
VSOutput VSMain(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {
    VSOutput output;

    BillboardInstance inst = g_BillboardInstances[instanceID];

    // Determine corner (0-3 for quad)
    uint corner = vertexID % 4;

    // Local offset for this corner
    float2 cornerOffset;
    cornerOffset.x = (corner == 0 || corner == 3) ? -0.5 : 0.5;
    cornerOffset.y = (corner == 0 || corner == 1) ? 0.0 : 1.0;

    // Apply billboard size
    cornerOffset *= inst.size;

    // Rotation around view axis
    float s = sin(inst.rotation);
    float c = cos(inst.rotation);
    float2 rotatedOffset;
    rotatedOffset.x = cornerOffset.x * c - cornerOffset.y * s;
    rotatedOffset.y = cornerOffset.x * s + cornerOffset.y * c;

    // Camera-facing billboard
    float3 right = g_CameraRight.xyz;
    float3 up = g_CameraUp.xyz;

    // Wind animation (simplified for billboards)
    float time = g_WindDirection.w;
    float windOffset = sin(time * g_WindDirection.z + inst.windPhase) * inst.windStrength;
    float3 windDisplacement = float3(g_WindDirection.x, 0, g_WindDirection.y) * windOffset * cornerOffset.y;

    // Build world position
    float3 worldPos = inst.position +
                      right * rotatedOffset.x +
                      up * rotatedOffset.y +
                      windDisplacement;

    output.worldPos = worldPos;
    output.position = mul(g_ViewProj, float4(worldPos, 1.0));

    // Texture coordinates from atlas
    float2 texCoordLocal;
    texCoordLocal.x = (corner == 0 || corner == 3) ? 0.0 : 1.0;
    texCoordLocal.y = (corner == 0 || corner == 1) ? 1.0 : 0.0;

    output.texCoord = lerp(inst.uvBounds.xy, inst.uvBounds.zw, texCoordLocal);

    output.color = inst.colorTint;

    return output;
}

// ============================================================================
// PIXEL SHADER
// ============================================================================

struct PSOutput {
    float4 albedo : SV_TARGET0;
    float4 normalRoughness : SV_TARGET1;
    float4 emissiveMetallic : SV_TARGET2;
};

PSOutput PSMain(VSOutput input) {
    PSOutput output;

    // Sample billboard texture
    float4 albedo = g_BillboardAtlas.Sample(g_Sampler, input.texCoord);

    // Alpha test
    if (albedo.a < 0.5) {
        discard;
    }

    // Apply color tint
    albedo.rgb *= input.color.rgb;

    // Sample normal from billboard normal map
    float3 normalTS = g_BillboardNormal.Sample(g_Sampler, input.texCoord).xyz;
    normalTS = normalTS * 2.0 - 1.0;

    // Reconstruct world-space normal from billboard
    // Billboards always face camera, so we can approximate the normal
    float3 viewDir = normalize(g_CameraPosition.xyz - input.worldPos);
    float3 right = normalize(g_CameraRight.xyz);
    float3 up = normalize(g_CameraUp.xyz);

    // TBN for billboard (tangent = right, bitangent = up, normal = toward camera)
    float3 normalWS = normalize(right * normalTS.x + up * normalTS.y + viewDir * normalTS.z);

    // Fixed roughness for billboards
    float roughness = 0.7;

    // Output
    output.albedo = float4(albedo.rgb, 1.0);
    output.normalRoughness = float4(normalWS * 0.5 + 0.5, roughness);
    output.emissiveMetallic = float4(0, 0, 0, 0);

    return output;
}

// ============================================================================
// SIMPLE FORWARD PASS (for transparent blending)
// ============================================================================

float4 PSForward(VSOutput input) : SV_TARGET {
    float4 albedo = g_BillboardAtlas.Sample(g_Sampler, input.texCoord);

    if (albedo.a < 0.1) {
        discard;
    }

    albedo.rgb *= input.color.rgb;

    // Simple lighting for forward pass
    float3 viewDir = normalize(g_CameraPosition.xyz - input.worldPos);
    float3 lightDir = normalize(float3(0.5, 1.0, 0.3));  // Hardcoded sun direction

    float3 normalWS = viewDir;  // Approximate
    float NdotL = saturate(dot(normalWS, lightDir));

    float3 ambient = 0.3;
    float3 diffuse = NdotL * 0.7;

    float3 finalColor = albedo.rgb * (ambient + diffuse);

    return float4(finalColor, albedo.a);
}

// ============================================================================
// GRASS CARD SHADER (Specialized for grass patches)
// ============================================================================

struct GrassInstance {
    float4x4 worldMatrix;
    float4 colorTint;
    float windPhase;
    float windStrength;
    float2 padding;
};

StructuredBuffer<GrassInstance> g_GrassInstances : register(t21);

struct GrassVSOutput {
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
    float3 worldPos : WORLD_POS;
    float3 normal : NORMAL;
};

GrassVSOutput VSGrass(float3 position : POSITION, float2 texCoord : TEXCOORD,
                       float3 normal : NORMAL, uint instanceID : SV_InstanceID) {
    GrassVSOutput output;

    GrassInstance inst = g_GrassInstances[instanceID];

    // Transform to world space
    float4 worldPos = mul(inst.worldMatrix, float4(position, 1.0));

    // Wind animation - stronger at top of grass blade
    float windWeight = texCoord.y;  // Y=0 at base, Y=1 at tip
    float time = g_WindDirection.w;

    float primaryWave = sin(time * g_WindDirection.z + inst.windPhase + worldPos.x * 0.3);
    float secondaryWave = sin(time * g_WindDirection.z * 2.3 + inst.windPhase * 1.7 + worldPos.z * 0.5) * 0.3;

    float totalWind = (primaryWave + secondaryWave) * inst.windStrength * windWeight * windWeight;

    worldPos.x += g_WindDirection.x * totalWind;
    worldPos.z += g_WindDirection.y * totalWind;
    worldPos.y -= abs(totalWind) * 0.1;  // Slight droop

    output.worldPos = worldPos.xyz;
    output.position = mul(g_ViewProj, worldPos);
    output.texCoord = texCoord;
    output.color = inst.colorTint;
    output.normal = normalize(mul((float3x3)inst.worldMatrix, normal));

    return output;
}

float4 PSGrass(GrassVSOutput input) : SV_TARGET {
    float4 albedo = g_BillboardAtlas.Sample(g_Sampler, input.texCoord);

    if (albedo.a < 0.3) {
        discard;
    }

    albedo.rgb *= input.color.rgb;

    // Simple subsurface scattering approximation for grass
    float3 lightDir = normalize(float3(0.5, 1.0, 0.3));
    float3 viewDir = normalize(g_CameraPosition.xyz - input.worldPos);

    float NdotL = saturate(dot(input.normal, lightDir));
    float backLight = saturate(dot(-input.normal, lightDir)) * 0.3;  // Light through blade

    float3 ambient = 0.25;
    float3 diffuse = (NdotL + backLight) * 0.75;

    float3 finalColor = albedo.rgb * (ambient + diffuse);

    return float4(finalColor, albedo.a);
}
