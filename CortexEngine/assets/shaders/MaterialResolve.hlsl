// MaterialResolve.hlsl
// Phase 2 of visibility buffer rendering: Material evaluation compute shader
// Input: Visibility buffer (triangle/instance IDs)
// Output: G-buffer textures (albedo, normal+roughness, emissive+metallic)

// Root signature:
// b0: Resolution constants (width, height)
// t0: Visibility buffer SRV
// t1: Instance data
// t2: Depth buffer SRV
// u0: Albedo UAV output
// u1: Normal+Roughness UAV output
// u2: Emissive+Metallic UAV output

cbuffer ResolutionConstants : register(b0) {
    uint g_Width;
    uint g_Height;
    float g_RcpWidth;
    float g_RcpHeight;
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

// Visibility buffer input
Texture2D<uint2> g_VisibilityBuffer : register(t0);
StructuredBuffer<VBInstanceData> g_Instances : register(t1);
Texture2D<float> g_DepthBuffer : register(t2);

// G-buffer UAV outputs
RWTexture2D<unorm float4> g_AlbedoOut : register(u0);        // RGBA8_SRGB
RWTexture2D<float4> g_NormalRoughnessOut : register(u1);     // RGBA16F
RWTexture2D<float4> g_EmissiveMetallicOut : register(u2);    // RGBA16F

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
        g_NormalRoughnessOut[pixelCoord] = float4(0, 0, 1, 1);  // Up vector + max roughness
        g_EmissiveMetallicOut[pixelCoord] = float4(0, 0, 0, 0);
        return;
    }

    // Fetch instance data
    VBInstanceData instance = g_Instances[instanceID];

    // TODO: Fetch triangle vertices from mesh buffer
    // For now, output debug visualization based on triangle ID
    float3 debugColor = float3(
        frac(float(triangleID) * 0.1),
        frac(float(triangleID) * 0.2),
        frac(float(instanceID) * 0.3)
    );

    // Write to G-buffers (temporary debug output)
    g_AlbedoOut[pixelCoord] = float4(debugColor, 1.0);
    g_NormalRoughnessOut[pixelCoord] = float4(0.5, 0.5, 1.0, 0.5);  // Encoded up normal + mid roughness
    g_EmissiveMetallicOut[pixelCoord] = float4(0, 0, 0, 0);  // No emissive, non-metallic

    // TODO: Full material evaluation pipeline:
    // 1. Fetch triangle vertices using triangleID and instance.meshIndex
    // 2. Reconstruct world position from depth
    // 3. Compute barycentric coordinates
    // 4. Interpolate vertex attributes (normal, tangent, UV)
    // 5. Sample material textures using instance.materialIndex
    // 6. Evaluate PBR material properties
    // 7. Write final values to G-buffers
}
