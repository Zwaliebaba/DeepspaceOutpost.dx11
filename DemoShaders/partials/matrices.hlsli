#ifndef MATRICES_HLSLI
#define MATRICES_HLSLI

#include "common.hlsli"

//
// Matrices (row-major; transforms use row vectors: mul(v, M))
//
cbuffer Matrices : register(b0)
{
    row_major float4x4 u_ViewMatrix;
    row_major float4x4 u_ProjectionMatrix;
    row_major float4x4 u_ViewProjectionMatrix;
    row_major float3x3 u_NormalMatrix;
};

#endif // MATRICES_HLSLI
