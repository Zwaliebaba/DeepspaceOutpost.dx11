#ifndef FLIPSURFACE_HLSLI
#define FLIPSURFACE_HLSLI

#include "common.hlsli"
#include "matrices.hlsli"

// Vertex/geometry-stage surface flip.
//
// Under FLIP_Y_WORLD, the GL path flips clip-space Y for orthographic (non-
// perspective) projections. Operates on the clip-space position in place.
//
// NOTE: matrix index remapped for the row-major (transposed) layout. GL tested
// the column-major element m[2][3]; the same logical element is m[3][2] here.
// Verify against the engine's actual projection layout.
#if FLIP_Y_WORLD
bool isPerspectiveMatrix(float4x4 m)
{
    return m[3][2] != 1.0;
}
#endif

void vflipsurface(inout float4 clipPos)
{
#if FLIP_Y_WORLD
    bool isPerspective = isPerspectiveMatrix(u_ProjectionMatrix);
    if (!isPerspective)
        clipPos.y = -clipPos.y;
#endif
}

#endif // FLIPSURFACE_HLSLI
