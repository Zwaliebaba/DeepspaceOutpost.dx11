#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/clipping.hlsli"
#include "partials/fog.hlsli"
#include "partials/hdr.hlsli"
#include "partials/debug.hlsli"

Texture2D    u_Texture0         : register(t0);
SamplerState u_Texture0_sampler : register(s0);

cbuffer PerDraw : register(b9)
{
    int u_TextureEnabled;
};

struct PSInput
{
    float4 pos          : SV_Position;
    float2 uv           : TEXCOORD0;
    float4 color        : TEXCOORD1;
    float4 fogViewSpace : TEXCOORD2;
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
#if !FEATURE_NATIVE_CLIPPING
    float2 clipDist     : TEXCOORD3;
#endif
#endif
};

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1) && !FEATURE_NATIVE_CLIPPING
    fclipping(input.clipDist);
#endif

    float4 color = input.color;
    if (u_TextureEnabled != 0)
        color = color * u_Texture0.Sample(u_Texture0_sampler, input.uv);
    color = hdrcolor(color);
    color = ffog(color, input.fogViewSpace);
    fdebugcolor(color, isFrontFace);
    return color;
}
