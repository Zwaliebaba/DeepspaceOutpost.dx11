#ifndef SMAA_UTIL_HLSLI
#define SMAA_UTIL_HLSLI

#include "common.hlsli"

float2 flipTexCoord(float2 tc)
{
    return tc * float2(1.0, -1.0) + float2(0.0, 1.0);
}

float4 flipTexCoord(float4 tc)
{
    return tc * float4(1.0, -1.0, 1.0, -1.0) + float4(0.0, 1.0, 0.0, 1.0);
}

#endif // SMAA_UTIL_HLSLI
