#include "partials/common.hlsli"
#include "partials/clipping-object.hlsli"
#include "partials/fog.hlsli"
#include "partials/matrices.hlsli"

Texture2D    u_Texture0         : register(t0);
SamplerState u_Texture0_sampler : register(s0);
Texture2D    u_Texture1         : register(t1);
SamplerState u_Texture1_sampler : register(s1);

struct PSInput
{
    float4 pos          : SV_Position;
    float2 v_TexCoord0  : TEXCOORD0;
    float2 v_TexCoord1  : TEXCOORD1;
    float4 v_Color      : TEXCOORD2;
    float4 fogViewSpace : TEXCOORD3;
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
#if !FEATURE_NATIVE_CLIPPING
    float2 clipDist     : TEXCOORD4;
#endif
#endif
};

float4 PSMain(PSInput input) : SV_Target
{
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1) && !FEATURE_NATIVE_CLIPPING
    fclipping(input.clipDist);
#endif
    float4 color = input.v_Color;
    color *= u_Texture0.Sample(u_Texture0_sampler, input.v_TexCoord0);
    color *= u_Texture1.Sample(u_Texture1_sampler, input.v_TexCoord1);
    return ffog(color, input.fogViewSpace);
}
