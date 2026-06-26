#ifndef CLIPPING_OBJECT_HLSLI
#define CLIPPING_OBJECT_HLSLI

#include "common.hlsli"

//
// Clipping planes (object-space variant)
//
// Same as clipping.hlsli but the clip distance is computed against an
// object/world-space position the caller supplies (rather than view space).
// Shares the b5 Clipping cbuffer; a shader uses either this or clipping.hlsli.
//
#define MAX_CLIP_PLANES 2

#if ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1
cbuffer Clipping : register(b5)
{
    float4 u_ClipPlane[MAX_CLIP_PLANES];   // .equation per plane
};
#endif

// Vertex stage: compute signed distances for the supplied position.
float2 vclipping(float4 pos)
{
    float2 d = float2(0.0, 0.0);
#if ENABLE_CLIP_PLANE0
    d.x = dot(pos, u_ClipPlane[0]);
#endif
#if ENABLE_CLIP_PLANE1
    d.y = dot(pos, u_ClipPlane[1]);
#endif
    return d;
}

// Fragment stage (non-native path only): discard clipped fragments.
void fclipping(float2 clipDist)
{
#if !FEATURE_NATIVE_CLIPPING
#if ENABLE_CLIP_PLANE0
    if (clipDist.x < 0.0)
        discard;
#endif
#if ENABLE_CLIP_PLANE1
    if (clipDist.y < 0.0)
        discard;
#endif
#endif
}

#endif // CLIPPING_OBJECT_HLSLI
