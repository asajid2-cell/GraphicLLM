// ParticleSort.hlsl
// Compute shader for GPU particle sorting using bitonic sort.
// Sorts particles back-to-front for correct alpha blending.
// Reference: "GPU Gems 2: High-Speed, Off-Screen Particles" - NVIDIA

#include "Common.hlsli"

// Sort key structure
struct SortKey {
    float depth;        // View-space depth (negative = further)
    uint index;         // Original particle index
};

// Sort constant buffer
cbuffer SortCB : register(b0) {
    uint g_NumParticles;
    uint g_GroupWidth;      // Bitonic sort stage parameter
    uint g_GroupHeight;     // Bitonic sort step parameter
    uint g_StepIndex;       // Current step within stage

    uint g_DispatchSize;    // Total dispatch count
    uint g_SortAscending;   // 0 = descending (back-to-front), 1 = ascending
    uint2 g_Padding;
};

// Input: particle depth values
StructuredBuffer<float> g_InputDepths : register(t0);

// Output: sorted indices
RWStructuredBuffer<uint> g_SortedIndices : register(u0);

// Intermediate sort keys (ping-pong buffers)
RWStructuredBuffer<SortKey> g_SortKeysA : register(u1);
RWStructuredBuffer<SortKey> g_SortKeysB : register(u2);

// Alive particle list (only sort alive particles)
StructuredBuffer<uint> g_AliveList : register(t1);
StructuredBuffer<uint> g_AliveCount : register(t2);

// Group shared memory for local sort
groupshared SortKey gs_SortKeys[1024];

// Compare function (returns true if a should come before b)
bool Compare(SortKey a, SortKey b, bool ascending) {
    if (ascending) {
        return a.depth < b.depth;
    } else {
        // Back-to-front for alpha blending
        return a.depth > b.depth;
    }
}

// Initialize sort keys from particle depths
[numthreads(256, 1, 1)]
void CSInitKeys(uint3 dispatchID : SV_DispatchThreadID) {
    uint index = dispatchID.x;

    if (index >= g_NumParticles) {
        // Pad with sentinel values
        SortKey key;
        key.depth = g_SortAscending > 0 ? 3.402823466e+38 : -3.402823466e+38;
        key.index = 0xFFFFFFFF;
        g_SortKeysA[index] = key;
        return;
    }

    // Get alive particle index
    uint aliveCount = g_AliveCount[0];
    if (index >= aliveCount) {
        SortKey key;
        key.depth = g_SortAscending > 0 ? 3.402823466e+38 : -3.402823466e+38;
        key.index = 0xFFFFFFFF;
        g_SortKeysA[index] = key;
        return;
    }

    uint particleIndex = g_AliveList[index];

    SortKey key;
    key.depth = g_InputDepths[particleIndex];
    key.index = particleIndex;

    g_SortKeysA[index] = key;
}

// Local bitonic sort within a thread group (up to 1024 elements)
[numthreads(512, 1, 1)]
void CSLocalSort(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID) {
    uint localID = groupThreadID.x;
    uint groupOffset = groupID.x * 1024;

    // Load two elements per thread into shared memory
    uint idx0 = groupOffset + localID * 2;
    uint idx1 = groupOffset + localID * 2 + 1;

    if (idx0 < g_NumParticles) {
        gs_SortKeys[localID * 2] = g_SortKeysA[idx0];
    } else {
        gs_SortKeys[localID * 2].depth = g_SortAscending > 0 ? 3.402823466e+38 : -3.402823466e+38;
        gs_SortKeys[localID * 2].index = 0xFFFFFFFF;
    }

    if (idx1 < g_NumParticles) {
        gs_SortKeys[localID * 2 + 1] = g_SortKeysA[idx1];
    } else {
        gs_SortKeys[localID * 2 + 1].depth = g_SortAscending > 0 ? 3.402823466e+38 : -3.402823466e+38;
        gs_SortKeys[localID * 2 + 1].index = 0xFFFFFFFF;
    }

    GroupMemoryBarrierWithGroupSync();

    // Bitonic sort in shared memory
    bool ascending = g_SortAscending > 0;

    // Build bitonic sequence
    for (uint size = 2; size <= 1024; size *= 2) {
        // Bitonic merge
        bool directionMask = (localID & (size / 2)) != 0;

        for (uint stride = size / 2; stride > 0; stride /= 2) {
            uint pos = localID;
            uint partner = pos ^ stride;

            if (partner > pos && partner < 1024) {
                SortKey a = gs_SortKeys[pos];
                SortKey b = gs_SortKeys[partner];

                bool shouldSwap;
                if (size == 2) {
                    shouldSwap = !Compare(a, b, ascending);
                } else {
                    // Alternating direction for bitonic merge
                    bool direction = ((pos / size) % 2 == 0) ? ascending : !ascending;
                    shouldSwap = !Compare(a, b, direction);
                }

                if (shouldSwap) {
                    gs_SortKeys[pos] = b;
                    gs_SortKeys[partner] = a;
                }
            }

            GroupMemoryBarrierWithGroupSync();
        }
    }

    // Write back to global memory
    if (idx0 < g_NumParticles) {
        g_SortKeysA[idx0] = gs_SortKeys[localID * 2];
    }
    if (idx1 < g_NumParticles) {
        g_SortKeysA[idx1] = gs_SortKeys[localID * 2 + 1];
    }
}

// Global bitonic merge step (for arrays larger than 1024)
[numthreads(256, 1, 1)]
void CSGlobalMerge(uint3 dispatchID : SV_DispatchThreadID) {
    uint index = dispatchID.x;

    if (index >= g_NumParticles) {
        return;
    }

    // Calculate partner index for this step
    uint partner = index ^ g_GroupHeight;

    // Only process if we're the lower index
    if (partner <= index || partner >= g_NumParticles) {
        return;
    }

    // Determine sort direction based on position in bitonic sequence
    bool ascending = g_SortAscending > 0;
    uint groupIndex = index / g_GroupWidth;
    bool direction = (groupIndex % 2 == 0) ? ascending : !ascending;

    SortKey a = g_SortKeysA[index];
    SortKey b = g_SortKeysA[partner];

    if (!Compare(a, b, direction)) {
        g_SortKeysA[index] = b;
        g_SortKeysA[partner] = a;
    }
}

// Alternative: Ping-pong global merge (reads from A, writes to B)
[numthreads(256, 1, 1)]
void CSGlobalMergePingPong(uint3 dispatchID : SV_DispatchThreadID) {
    uint index = dispatchID.x;

    if (index >= g_NumParticles) {
        // Copy sentinel to output
        g_SortKeysB[index] = g_SortKeysA[index];
        return;
    }

    uint partner = index ^ g_GroupHeight;

    bool ascending = g_SortAscending > 0;
    uint groupIndex = index / g_GroupWidth;
    bool direction = (groupIndex % 2 == 0) ? ascending : !ascending;

    SortKey self = g_SortKeysA[index];
    SortKey other = (partner < g_NumParticles) ? g_SortKeysA[partner] : self;

    SortKey result;
    if (partner > index) {
        // We're the lower index
        result = Compare(self, other, direction) ? self : other;
    } else {
        // We're the higher index
        result = Compare(other, self, direction) ? self : other;
    }

    g_SortKeysB[index] = result;
}

// Extract final sorted indices
[numthreads(256, 1, 1)]
void CSExtractIndices(uint3 dispatchID : SV_DispatchThreadID) {
    uint index = dispatchID.x;

    if (index >= g_NumParticles) {
        return;
    }

    uint aliveCount = g_AliveCount[0];
    if (index >= aliveCount) {
        g_SortedIndices[index] = 0xFFFFFFFF;
        return;
    }

    SortKey key = g_SortKeysA[index];
    g_SortedIndices[index] = key.index;
}

// Optimized bitonic sort for small arrays (single dispatch)
[numthreads(256, 1, 1)]
void CSBitonicSortSmall(uint3 dispatchID : SV_DispatchThreadID) {
    uint index = dispatchID.x;
    bool ascending = g_SortAscending > 0;

    // Full bitonic sort in global memory for arrays up to 65536
    for (uint size = 2; size <= g_NumParticles; size *= 2) {
        for (uint stride = size / 2; stride > 0; stride /= 2) {
            // Sync between steps (global memory fence)
            DeviceMemoryBarrier();

            uint partner = index ^ stride;

            if (partner > index && partner < g_NumParticles) {
                uint groupIndex = index / size;
                bool direction = (groupIndex % 2 == 0) ? ascending : !ascending;

                SortKey a = g_SortKeysA[index];
                SortKey b = g_SortKeysA[partner];

                if (!Compare(a, b, direction)) {
                    g_SortKeysA[index] = b;
                    g_SortKeysA[partner] = a;
                }
            }
        }
    }
}

// Radix sort key generation (alternative to bitonic for very large arrays)
[numthreads(256, 1, 1)]
void CSRadixKeyGen(uint3 dispatchID : SV_DispatchThreadID) {
    uint index = dispatchID.x;

    if (index >= g_NumParticles) {
        return;
    }

    uint aliveCount = g_AliveCount[0];
    if (index >= aliveCount) {
        // Invalid entry
        g_SortKeysA[index].depth = g_SortAscending > 0 ? 3.402823466e+38 : -3.402823466e+38;
        g_SortKeysA[index].index = 0xFFFFFFFF;
        return;
    }

    uint particleIndex = g_AliveList[index];
    float depth = g_InputDepths[particleIndex];

    // Convert float to sortable uint
    // IEEE 754: negative floats sort incorrectly, so we flip bits
    uint uintDepth = asuint(depth);
    uint mask = -(int)(uintDepth >> 31) | 0x80000000;
    uint sortableDepth = uintDepth ^ mask;

    // For back-to-front (descending), invert
    if (g_SortAscending == 0) {
        sortableDepth = ~sortableDepth;
    }

    g_SortKeysA[index].depth = asfloat(sortableDepth);
    g_SortKeysA[index].index = particleIndex;
}

// Counting sort histogram (for radix sort)
RWStructuredBuffer<uint> g_Histogram : register(u3);

groupshared uint gs_Histogram[256];

[numthreads(256, 1, 1)]
void CSRadixHistogram(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID) {
    uint localID = groupThreadID.x;
    uint groupOffset = groupID.x * 256;

    // Clear local histogram
    gs_Histogram[localID] = 0;
    GroupMemoryBarrierWithGroupSync();

    // Count keys in this group
    uint index = groupOffset + localID;
    if (index < g_NumParticles) {
        uint key = asuint(g_SortKeysA[index].depth);
        uint radixDigit = (key >> (g_StepIndex * 8)) & 0xFF;
        InterlockedAdd(gs_Histogram[radixDigit], 1);
    }

    GroupMemoryBarrierWithGroupSync();

    // Write to global histogram
    InterlockedAdd(g_Histogram[groupID.x * 256 + localID], gs_Histogram[localID]);
}

// Prefix sum for radix sort (simple serial version)
[numthreads(1, 1, 1)]
void CSPrefixSum(uint3 dispatchID : SV_DispatchThreadID) {
    uint sum = 0;
    for (uint i = 0; i < 256; i++) {
        uint count = g_Histogram[i];
        g_Histogram[i] = sum;
        sum += count;
    }
}

// Scatter phase of radix sort
[numthreads(256, 1, 1)]
void CSRadixScatter(uint3 dispatchID : SV_DispatchThreadID) {
    uint index = dispatchID.x;

    if (index >= g_NumParticles) {
        return;
    }

    SortKey key = g_SortKeysA[index];
    uint radixDigit = (asuint(key.depth) >> (g_StepIndex * 8)) & 0xFF;

    // Get destination index
    uint destIndex;
    InterlockedAdd(g_Histogram[radixDigit], 1, destIndex);

    g_SortKeysB[destIndex] = key;
}

// Copy sorted keys back (after ping-pong)
[numthreads(256, 1, 1)]
void CSCopyKeys(uint3 dispatchID : SV_DispatchThreadID) {
    uint index = dispatchID.x;

    if (index >= g_NumParticles) {
        return;
    }

    g_SortKeysA[index] = g_SortKeysB[index];
}

// Indirect draw args preparation
struct DrawArgs {
    uint IndexCountPerInstance;
    uint InstanceCount;
    uint StartIndexLocation;
    int BaseVertexLocation;
    uint StartInstanceLocation;
};

RWStructuredBuffer<DrawArgs> g_DrawArgs : register(u4);

[numthreads(1, 1, 1)]
void CSPrepareDrawArgs(uint3 dispatchID : SV_DispatchThreadID) {
    uint aliveCount = g_AliveCount[0];

    // Billboard particles: 6 indices per particle (2 triangles)
    g_DrawArgs[0].IndexCountPerInstance = 6;
    g_DrawArgs[0].InstanceCount = aliveCount;
    g_DrawArgs[0].StartIndexLocation = 0;
    g_DrawArgs[0].BaseVertexLocation = 0;
    g_DrawArgs[0].StartInstanceLocation = 0;
}

// Dispatch indirect args preparation
struct DispatchArgs {
    uint ThreadGroupCountX;
    uint ThreadGroupCountY;
    uint ThreadGroupCountZ;
};

RWStructuredBuffer<DispatchArgs> g_DispatchArgs : register(u5);

[numthreads(1, 1, 1)]
void CSPrepareDispatchArgs(uint3 dispatchID : SV_DispatchThreadID) {
    uint aliveCount = g_AliveCount[0];

    // 256 threads per group
    g_DispatchArgs[0].ThreadGroupCountX = (aliveCount + 255) / 256;
    g_DispatchArgs[0].ThreadGroupCountY = 1;
    g_DispatchArgs[0].ThreadGroupCountZ = 1;
}
