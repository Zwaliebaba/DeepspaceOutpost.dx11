#ifndef CLIPPING_HLSLI
#define CLIPPING_HLSLI

#include "common.hlsli"

//
// Clipping planes
//
// Two cross-stage strategies, selected by FEATURE_NATIVE_CLIPPING:
//   - Native:    the VS writes SV_ClipDistance; the rasterizer clips. The PS
//                does nothing. The shader's VSOutput must declare
//                    float2 clipDist : SV_ClipDistance0;   (sized to planes used)
//   - Non-native: the VS writes the distances into an ordinary varying and the
//                 PS calls fclipping() to discard fragments with d < 0.
//
// Helpers are pure: vclipping() returns the per-plane signed distances; the
// shader stores them in the appropriate output field.
//
#define MAX_CLIP_PLANES 2

#if ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1
cbuffer Clipping : register(b5)
{
    float4 u_ClipPlane[MAX_CLIP_PLANES];   // .equation per plane
};
#endif

// Vertex stage: compute signed distances for the (view-space) position.
float2 vclipping(float4 mv_pos)
{
    float2 d = float2(0.0, 0.0);
#if ENABLE_CLIP_PLANE0
    d.x = dot(mv_pos, u_ClipPlane[0]);
#endif
#if ENABLE_CLIP_PLANE1
    d.y = dot(mv_pos, u_ClipPlane[1]);
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

#endif // CLIPPING_HLSLI
