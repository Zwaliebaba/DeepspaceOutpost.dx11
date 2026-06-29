#ifndef FOG_HLSLI
#define FOG_HLSLI

#include "common.hlsli"

// b3 -- linear fog (the only mode the game uses). Flattened to match FogConstants.
// The view-space position is produced in the VS (vfog) and carried to the PS as a
// varying, where ffog() consumes it.
cbuffer Fog : register(b3)
{
    float4 u_FogColor;
    float u_FogStart;
    float u_FogEnd;
    int u_FogEnable;
    int u_InPixelEffect;
};

float4 vfog(float4 viewPos)
{
    return viewPos;
}

float4 ffog(float4 color, float4 fogViewSpace)
{
    if (u_FogEnable == 0)
        return color;

    float dist = length(fogViewSpace.xyz);
    float fogFactor = saturate((u_FogEnd - dist) / (u_FogEnd - u_FogStart));

    if (u_InPixelEffect != 0)
        return float4(color.rgb, fogFactor);

    return fogFactor * color + (1.0 - fogFactor) * u_FogColor;
}

#endif // FOG_HLSLI
