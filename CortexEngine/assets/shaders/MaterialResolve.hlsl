// MaterialResolve.hlsl
// Phase 2.2: Full material evaluation compute shader
// Input: Visibility buffer (triangle/instance IDs)
// Output: G-buffer textures (albedo, normal+roughness, emissive+metallic)

// Root signature:
// b0: Resolution constants
// t0: Visibility buffer SRV
// t1: Instance data
// t2: Depth buffer SRV
// t3: Global vertex buffer (ByteAddressBuffer)
// t4: Global index buffer (ByteAddressBuffer)
// u0: Albedo UAV output
// u1: Normal+Roughness UAV output
// u2: Emissive+Metallic UAV output

cbuffer ResolutionConstants : register(b0) {
    uint g_Width;
    uint g_Height;
    float g_RcpWidth;
    float g_RcpHeight;
    float4x4 g_ViewProj;  // View-projection matrix for computing clip-space barycentrics
    uint g_CurrentMeshIndex;  // Current mesh being processed (for multi-mesh support)
    uint3 _pad2;
};

// Instance data structure (matches VBInstanceData in C++)
struct VBInstanceData {
    float4x4 worldMatrix;
    uint meshIndex;
    uint materialIndex;
    uint firstIndex;
    uint indexCount;
    uint baseVertex;
    uint3 _pad;
};

// Vertex structure (matches C++ vertex layout: 48 bytes)
struct Vertex {
    float3 position;    // 12 bytes
    float3 normal;      // 12 bytes
    float4 tangent;     // 16 bytes
    float2 texCoord;    // 8 bytes
    // Total: 48 bytes
};

// Visibility buffer input
Texture2D<uint2> g_VisibilityBuffer : register(t0);
StructuredBuffer<VBInstanceData> g_Instances : register(t1);
Texture2D<float> g_DepthBuffer : register(t2);
ByteAddressBuffer g_VertexBuffer : register(t3);
ByteAddressBuffer g_IndexBuffer : register(t4);

// G-buffer UAV outputs
RWTexture2D<unorm float4> g_AlbedoOut : register(u0);        // RGBA8_SRGB
RWTexture2D<float4> g_NormalRoughnessOut : register(u1);     // RGBA16F
RWTexture2D<float4> g_EmissiveMetallicOut : register(u2);    // RGBA16F

// Load a vertex from the global vertex buffer
Vertex LoadVertex(uint vertexIndex) {
    // Vertex stride = 48 bytes (12+12+16+8)
    uint offset = vertexIndex * 48;

    Vertex v;
    v.position = asfloat(g_VertexBuffer.Load3(offset + 0));
    v.normal = asfloat(g_VertexBuffer.Load3(offset + 12));
    v.tangent = asfloat(g_VertexBuffer.Load4(offset + 24));
    v.texCoord = asfloat(g_VertexBuffer.Load2(offset + 40));

    return v;
}

// Load triangle indices
uint3 LoadTriangleIndices(uint triangleID, uint firstIndex, uint baseVertex) {
    uint indexOffset = (firstIndex + triangleID * 3) * 4; // 4 bytes per uint32 index

    uint3 indices;
    indices.x = g_IndexBuffer.Load(indexOffset + 0) + baseVertex;
    indices.y = g_IndexBuffer.Load(indexOffset + 4) + baseVertex;
    indices.z = g_IndexBuffer.Load(indexOffset + 8) + baseVertex;

    return indices;
}

// Reconstruct world position from depth buffer
float3 ReconstructWorldPosition(float2 uv, float depth, float4x4 invViewProj) {
    // Convert to NDC
    float4 ndc = float4(
        uv.x * 2.0 - 1.0,
        (1.0 - uv.y) * 2.0 - 1.0,
        depth,
        1.0
    );

    // Transform to world space
    float4 worldPos = mul(invViewProj, ndc);
    return worldPos.xyz / worldPos.w;
}

// Compute barycentric coordinates for a point inside a triangle (2D screen space version)
float3 ComputeScreenSpaceBarycentrics(float2 p, float2 v0, float2 v1, float2 v2) {
    float2 e0 = v1 - v0;
    float2 e1 = v2 - v0;
    float2 e2 = p - v0;

    float d00 = dot(e0, e0);
    float d01 = dot(e0, e1);
    float d11 = dot(e1, e1);
    float d20 = dot(e2, e0);
    float d21 = dot(e2, e1);

    float denom = d00 * d11 - d01 * d01;

    // Handle degenerate triangles
    if (abs(denom) < 1e-7) {
        return float3(0.33, 0.33, 0.34);
    }

    float v = (d11 * d20 - d01 * d21) / denom;
    float w = (d00 * d21 - d01 * d20) / denom;
    float u = 1.0 - v - w;

    return float3(u, v, w);
}

// Compute screen-space derivatives for texture sampling
void ComputeScreenSpaceDerivatives(
    float2 uv0, float2 uv1, float2 uv2,
    float3 bary,
    out float2 ddx, out float2 ddy
) {
    // Approximate derivatives using barycentric gradients
    // This is a simplified version - proper implementation would use screen-space gradients
    ddx = (uv1 - uv0) * g_RcpWidth * 2.0;
    ddy = (uv2 - uv0) * g_RcpHeight * 2.0;
}

// Compute shader: One thread per pixel
[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 pixelCoord = dispatchThreadID.xy;

    // Early out if pixel is outside render target
    if (pixelCoord.x >= g_Width || pixelCoord.y >= g_Height) {
        return;
    }

    // Read visibility buffer
    uint2 visData = g_VisibilityBuffer[pixelCoord];
    uint packedTriangleAndDraw = visData.x;
    uint instanceID = visData.y;

    // Unpack triangle ID and draw ID
    uint triangleID = packedTriangleAndDraw & 0x00FFFFFF;
    uint drawID = (packedTriangleAndDraw >> 24) & 0xFF;

    // Check for background pixels (cleared to 0xFFFFFFFF)
    if (packedTriangleAndDraw == 0xFFFFFFFF && instanceID == 0xFFFFFFFF) {
        // Write black/default values for background
        g_AlbedoOut[pixelCoord] = float4(0, 0, 0, 1);
        g_NormalRoughnessOut[pixelCoord] = float4(0.5, 0.5, 1.0, 1.0);  // Encoded up normal + max roughness
        g_EmissiveMetallicOut[pixelCoord] = float4(0, 0, 0, 0);
        return;
    }

    // Fetch instance data
    VBInstanceData instance = g_Instances[instanceID];

    // Skip pixels that don't belong to the current mesh being processed
    // This allows us to dispatch material resolve once per mesh with correct vertex/index buffers
    if (instance.meshIndex != g_CurrentMeshIndex) {
        return;  // Skip this pixel - it belongs to a different mesh
    }

    // Load triangle indices
    uint3 indices = LoadTriangleIndices(triangleID, instance.firstIndex, instance.baseVertex);

    // Load vertices
    Vertex v0 = LoadVertex(indices.x);
    Vertex v1 = LoadVertex(indices.y);
    Vertex v2 = LoadVertex(indices.z);

    // Transform vertices to world space
    float3 worldPos0 = mul(instance.worldMatrix, float4(v0.position, 1.0)).xyz;
    float3 worldPos1 = mul(instance.worldMatrix, float4(v1.position, 1.0)).xyz;
    float3 worldPos2 = mul(instance.worldMatrix, float4(v2.position, 1.0)).xyz;

    // Read depth for current pixel
    float depth = g_DepthBuffer[pixelCoord];

    // Transform vertices to clip space
    float4 clipPos0 = mul(g_ViewProj, float4(worldPos0, 1.0));
    float4 clipPos1 = mul(g_ViewProj, float4(worldPos1, 1.0));
    float4 clipPos2 = mul(g_ViewProj, float4(worldPos2, 1.0));

    // Perspective divide to get NDC positions
    float3 ndc0 = clipPos0.xyz / clipPos0.w;
    float3 ndc1 = clipPos1.xyz / clipPos1.w;
    float3 ndc2 = clipPos2.xyz / clipPos2.w;

    // Convert to screen space [0, 1]
    float2 screen0 = float2(ndc0.x * 0.5 + 0.5, 0.5 - ndc0.y * 0.5);
    float2 screen1 = float2(ndc1.x * 0.5 + 0.5, 0.5 - ndc1.y * 0.5);
    float2 screen2 = float2(ndc2.x * 0.5 + 0.5, 0.5 - ndc2.y * 0.5);

    // Current pixel in screen space [0, 1]
    float2 pixelUV = (float2(pixelCoord) + 0.5) * float2(g_RcpWidth, g_RcpHeight);

    // Compute 2D screen-space barycentrics using edge function
    float3 bary = ComputeScreenSpaceBarycentrics(pixelUV, screen0, screen1, screen2);

    // Apply perspective correction using 1/w interpolation
    float3 baryPersp = bary / float3(clipPos0.w, clipPos1.w, clipPos2.w);
    float baryPerspSum = baryPersp.x + baryPersp.y + baryPersp.z;
    if (baryPerspSum > 1e-7) {
        bary = baryPersp / baryPerspSum;
    }

    // Clamp to valid range for safety
    bary = saturate(bary);

    // Interpolate vertex attributes using barycentric coordinates
    float2 texCoord = v0.texCoord * bary.x + v1.texCoord * bary.y + v2.texCoord * bary.z;

    float3 normal = v0.normal * bary.x + v1.normal * bary.y + v2.normal * bary.z;
    normal = normalize(mul((float3x3)instance.worldMatrix, normal));

    float4 tangent = v0.tangent * bary.x + v1.tangent * bary.y + v2.tangent * bary.z;
    tangent.xyz = normalize(mul((float3x3)instance.worldMatrix, tangent.xyz));

    // TODO: Sample material textures using instance.materialIndex
    // For now, use mesh index to show different meshes in different colors

    // Color each mesh differently to verify the pipeline is working
    float3 meshColors[8] = {
        float3(1.0, 0.2, 0.2),  // Red
        float3(0.2, 1.0, 0.2),  // Green
        float3(0.2, 0.2, 1.0),  // Blue
        float3(1.0, 1.0, 0.2),  // Yellow
        float3(1.0, 0.2, 1.0),  // Magenta
        float3(0.2, 1.0, 1.0),  // Cyan
        float3(1.0, 0.6, 0.2),  // Orange
        float3(0.6, 0.2, 1.0)   // Purple
    };
    float3 albedo = meshColors[instance.meshIndex % 8];

    // Use interpolated normal
    float3 normalEncoded = normal * 0.5 + 0.5;

    // Default PBR values (mid-roughness, non-metallic)
    float roughness = 0.5;
    float metallic = 0.0;
    float3 emissive = float3(0, 0, 0);

    // Write to G-buffers
    g_AlbedoOut[pixelCoord] = float4(albedo, 1.0);
    g_NormalRoughnessOut[pixelCoord] = float4(normalEncoded, roughness);
    g_EmissiveMetallicOut[pixelCoord] = float4(emissive, metallic);
}
