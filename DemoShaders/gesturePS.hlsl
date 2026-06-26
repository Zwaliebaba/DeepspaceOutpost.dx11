#include "partials/common.hlsli"
#include "partials/clipping.hlsli"
#include "partials/fog.hlsli"
#include "partials/matrices.hlsli"
#include "partials/hdr.hlsli"
#include "partials/debug.hlsli"

Texture2D    u_Texture0         : register(t0);
SamplerState u_Texture0_sampler : register(s0);

struct PSInput
{
    float4 pos          : SV_Position;
    float2 v_TexCoord   : TEXCOORD0;
    float4 v_Color      : TEXCOORD1;
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

    float4 texColor = u_Texture0.SampleBias(u_Texture0_sampler, input.v_TexCoord, -0.25);
    float4 color = input.v_Color * texColor;
    color = hdrcolor(color);
    color = ffog(color, input.fogViewSpace);
    fdebugcolor(color, isFrontFace);
    return color;
}
