#ifndef BARYCENTRIC_HLSLI
#define BARYCENTRIC_HLSLI

#include "common.hlsli"

//
// Barycentric coordinates.
//
// SM 5.0 has no hardware barycentrics (SV_Barycentrics is SM 6.1+), so we use
// the per-triangle vertex-id fallback the GL path used for non-extension GPUs.
// The shader carries a `float3 baryCoord : TEXCOORD#` varying:
//   VS:  o.baryCoord = vbarycentric(vertexID);
//   PS:  ... uses input.baryCoord ...
//
static const float3 BaryCoords[3] =
{
    float3(1.0, 0.0, 0.0),
    float3(0.0, 1.0, 0.0),
    float3(0.0, 0.0, 1.0)
};

float3 vbarycentric(uint vertexID)
{
    return BaryCoords[vertexID % 3];
}

#endif // BARYCENTRIC_HLSLI
