#ifndef TEXENV_HLSLI
#define TEXENV_HLSLI

#include "common.hlsli"

//
// Texture environment (fragment stage)
//
struct TextureEnv
{
    int mode;
    int combineRGB;
};

cbuffer TextureEnvParams : register(b8)
{
    TextureEnv u_TextureEnv[2];
};

#define TEXENV_MODULATE 1
#define TEXENV_COMBINE 2
#define TEXENV_REPLACE 3
#define TEXENV_DECAL 4

#define TEXCOMBINE_REPLACE 1
#define TEXCOMBINE_MODULATE 2

float4 fcolormix(int texIdx, float4 previous, float4 tex)
{
#if defined(ASSUME_TEXENV_MODULATE)
    return previous * tex;
#elif defined(ASSUME_TEXENV_REPLACE)
    return tex;
#else
    if (u_TextureEnv[texIdx].mode == TEXENV_MODULATE)
    {
        return previous * tex;
    }
    else if (u_TextureEnv[texIdx].mode == TEXENV_REPLACE)
    {
        return tex;
    }
    else if (u_TextureEnv[texIdx].mode == TEXENV_COMBINE)
    {
        if (u_TextureEnv[texIdx].combineRGB == TEXCOMBINE_REPLACE)
        {
            return float4(tex.rgb, previous.a);
        }
        else /* TEXCOMBINE_MODULATE */
        {
            return previous * tex;
        }
    }
    else if (u_TextureEnv[texIdx].mode == TEXENV_DECAL)
    {
        return float4(previous.rgb * (1.0 - tex.a) + tex.rgb * tex.a, previous.a);
    }

    // Undefined
    return float4(1, 0, 1, 1);
#endif
}

#endif // TEXENV_HLSLI
