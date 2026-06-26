#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/clipping.hlsli"
#include "partials/fog.hlsli"
#include "partials/debug.hlsli"

Texture2D    u_Texture0         : register(t0);
SamplerState u_Texture0_sampler : register(s0);
Texture2D    u_Texture1         : register(t1);
SamplerState u_Texture1_sampler : register(s1);

struct PSInput
{
    float4 pos          : SV_Position;
    float2 uv0          : TEXCOORD0;
    float2 uv1          : TEXCOORD1;
    float4 color        : TEXCOORD2;
    float4 fogViewSpace : TEXCOORD3;
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
#if !FEATURE_NATIVE_CLIPPING
    float2 clipDist     : TEXCOORD4;
#endif
#endif
};

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1) && !FEATURE_NATIVE_CLIPPING
    fclipping(input.clipDist);
#endif

    float4 color = input.color;
    color = color * u_Texture0.Sample(u_Texture0_sampler, input.uv0);
    color = color * u_Texture1.Sample(u_Texture1_sampler, input.uv1);
    color = ffog(color, input.fogViewSpace);
    fdebugcolor(color, isFrontFace);
    return color;
}
