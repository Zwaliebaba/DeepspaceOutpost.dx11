#ifndef MATRICES_HLSLI
#define MATRICES_HLSLI

#include "common.hlsli"

// b0 -- view/projection state (row-major, row-vector). See ConstantBuffers.h.
// u_View plays the role of the legacy GL modelview (object -> eye); vertices are
// submitted in object space.
cbuffer PerView : register(b0)
{
    row_major float4x4 u_View;
    row_major float4x4 u_Projection;
    row_major float4x4 u_ViewProjection;
    row_major float3x3 u_NormalMatrix;
};

#endif // MATRICES_HLSLI
