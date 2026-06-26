#ifndef FOG_HLSLI
#define FOG_HLSLI

#include "common.hlsli"

//
// Fog (linear only, since that's all we use)
//
// Cross-stage data: the view-space position is computed in the VS (vfog) and
// carried to the PS as a varying, where ffog() consumes it. The shader owns the
// varying field; these helpers are pure.
//
struct Fog
{
    float4 color;
    float start;
    float end;
    int enable;   // bool in GLSL; 4-byte int in a cbuffer
};

cbuffer FogParams : register(b3)
{
    Fog u_Fog;
    int u_InPixelEffect;
};

// Vertex stage: produce the value to store in the fog varying.
float4 vfog(float4 mv_pos)
{
    return mv_pos;
}

// Fragment stage: apply fog to a color given the interpolated view-space pos.
float4 ffog(float4 color, float4 fogViewSpace)
{
    if (u_Fog.enable != 0)
    {
        float dist = length(fogViewSpace);
        float fogFactor = clamp((u_Fog.end - dist) / (u_Fog.end - u_Fog.start), 0.0, 1.0);
        float4 outColor;
        if (u_InPixelEffect != 0)
        {
            outColor = float4(color.rgb, fogFactor);
        }
        else
        {
            outColor = fogFactor * color + (1.0 - fogFactor) * u_Fog.color;
        }
        return outColor;
    }
    else
    {
        return color;
    }
}

#endif // FOG_HLSLI
