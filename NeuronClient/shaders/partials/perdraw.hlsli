#ifndef PERDRAW_HLSLI
#define PERDRAW_HLSLI

#include "common.hlsli"

// b9 -- per-draw material parameters shared by the dedicated 2D/GUI programs.
// Matches PerDrawConstants in NeuronClient/ConstantBuffers.h. u_Color is a global
// tint; u_ColorEdge / u_ColorCenter drive the window-background vertical gradient.
cbuffer PerDraw : register(b9)
{
    float4 u_Color;
    float4 u_ColorEdge;
    float4 u_ColorCenter;
};

#endif // PERDRAW_HLSLI
