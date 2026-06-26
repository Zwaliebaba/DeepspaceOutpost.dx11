#ifndef TIME_HLSLI
#define TIME_HLSLI

#include "common.hlsli"

//
// Time sources
//
cbuffer Time : register(b2)
{
    float u_GameTime;
    float u_ShaderTime;
};

#endif // TIME_HLSLI
