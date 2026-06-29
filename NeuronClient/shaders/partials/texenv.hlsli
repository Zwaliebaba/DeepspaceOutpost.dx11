#ifndef TEXENV_HLSLI
#define TEXENV_HLSLI

#include "common.hlsli"

// b8 -- fixed-function texture environment, per unit. .x = mode, .y = combineRGB.
// Stored as int4 per unit (array elements pad to 16 bytes); matches TexEnvConstants.
cbuffer TexEnv : register(b8)
{
    int4 u_TexEnv[2];
};

#define TEXENV_MODULATE 1
#define TEXENV_COMBINE 2
#define TEXENV_REPLACE 3
#define TEXENV_DECAL 4

#define TEXCOMBINE_REPLACE 1
#define TEXCOMBINE_MODULATE 2

// Combine an incoming color with a sampled texel according to the unit's mode.
float4 fcolormix(int unit, float4 previous, float4 tex)
{
    int mode = u_TexEnv[unit].x;
    int combineRGB = u_TexEnv[unit].y;

    if (mode == TEXENV_REPLACE)
        return tex;
    if (mode == TEXENV_DECAL)
        return float4(previous.rgb * (1.0 - tex.a) + tex.rgb * tex.a, previous.a);
    if (mode == TEXENV_COMBINE && combineRGB == TEXCOMBINE_REPLACE)
        return float4(tex.rgb, previous.a);

    // MODULATE (default) and COMBINE/MODULATE.
    return previous * tex;
}

#endif // TEXENV_HLSLI
