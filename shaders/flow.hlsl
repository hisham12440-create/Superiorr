// Block-matching motion estimation. One thread group = one 16x16 block.
// flow.xy = backward motion (cur -> prev) in pixels, flow.z = best SAD, flow.w = zero-motion SAD.
Texture2D<float4> tPrev : register(t0);
Texture2D<float4> tCur : register(t1);
RWTexture2D<float4> uFlow : register(u0);
RWStructuredBuffer<uint> uStats : register(u1);

cbuffer CB : register(b0)
{
    float2 srcSize;
    float2 pad0;
};

groupshared float gCost[64];
groupshared int gIdx[64];
groupshared int2 gCoarse;
groupshared float gSad0;

float Luma(float3 c)
{
    return dot(c, float3(0.299, 0.587, 0.114));
}

float BlockSad(int2 base, int2 v)
{
    int2 hi = int2(srcSize) - 1;
    float s = 0;
    [unroll] for (int y = 0; y < 8; y++)
    {
        [unroll] for (int x = 0; x < 8; x++)
        {
            int2 p = min(base + int2(x, y) * 2, hi);
            int2 q = clamp(p + v, int2(0, 0), hi);
            s += abs(Luma(tCur.Load(int3(p, 0)).rgb) - Luma(tPrev.Load(int3(q, 0)).rgb));
        }
    }
    return s * (1.0 / 64.0);
}

[numthreads(64, 1, 1)]
void CSFlow(uint3 gid : SV_GroupID, uint tid : SV_GroupIndex)
{
    int2 base = int2(gid.xy) * 16;

    // Coarse search: +-12 px, step 2 (13x13 = 169 candidates spread over 64 threads).
    float best = 1e9;
    int bestIdx = 84;
    for (int idx = int(tid); idx < 169; idx += 64)
    {
        int2 v = int2(idx % 13, idx / 13) * 2 - 12;
        float c = BlockSad(base, v) + 0.0004 * float(abs(v.x) + abs(v.y));
        if (c < best)
        {
            best = c;
            bestIdx = idx;
        }
    }
    gCost[tid] = best;
    gIdx[tid] = bestIdx;
    GroupMemoryBarrierWithGroupSync();

    if (tid == 0)
    {
        float b = gCost[0];
        int bi = gIdx[0];
        for (int i = 1; i < 64; i++)
        {
            if (gCost[i] < b)
            {
                b = gCost[i];
                bi = gIdx[i];
            }
        }
        gCoarse = int2(bi % 13, bi / 13) * 2 - 12;
        gSad0 = BlockSad(base, int2(0, 0));
    }
    GroupMemoryBarrierWithGroupSync();

    // Refinement: +-1 px around the coarse winner.
    float rc = 1e9;
    if (tid < 9)
    {
        int2 v = gCoarse + int2(int(tid % 3), int(tid / 3)) - 1;
        rc = BlockSad(base, v) + 0.0004 * float(abs(v.x) + abs(v.y));
    }
    gCost[tid] = rc;
    GroupMemoryBarrierWithGroupSync();

    if (tid == 0)
    {
        float b = gCost[0];
        int bi = 0;
        for (int i = 1; i < 9; i++)
        {
            if (gCost[i] < b)
            {
                b = gCost[i];
                bi = i;
            }
        }
        int2 v = gCoarse + int2(bi % 3, bi / 3) - 1;
        float sad = BlockSad(base, v);
        uFlow[gid.xy] = float4(float(v.x), float(v.y), sad, gSad0);
        InterlockedAdd(uStats[1], 1);
        if (sad > 0.10) InterlockedAdd(uStats[0], 1);
        if (gSad0 < 0.003) InterlockedAdd(uStats[2], 1);
    }
}
