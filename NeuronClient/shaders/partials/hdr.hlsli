#ifndef HDR_HLSLI
#define HDR_HLSLI

#include "common.hlsli"

// b4 -- fragment-stage HDR colour handling. No-op unless compiled with USE_HDR.
#define HDR_COLOR_DEFAULT 0
#define HDR_COLOR_AMPLIFY_2X 1

#if USE_HDR
cbuffer Hdr : register(b4)
{
    int u_HdrColorMode;
};
#endif

float4 hdrcolor(float4 color)
{
#if USE_HDR
    if (u_HdrColorMode == HDR_COLOR_AMPLIFY_2X)
        return float4(color.rgb * 2.0, color.a);
    return color;
#else
    return color;
#endif
}

#endif // HDR_HLSLI
