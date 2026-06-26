#include "partials/common.hlsli"
#include "partials/fog.hlsli"
#include "partials/matrices.hlsli"
#include "partials/debug.hlsli"

struct PSInput
{
    float4 pos          : SV_Position;
    float2 v_TexCoord   : TEXCOORD0;
    float4 v_Color      : TEXCOORD1;
    float4 fogViewSpace : TEXCOORD2;
};

Texture2D    u_Texture0         : register(t0);
SamplerState u_Texture0_sampler : register(s0);

static const float c_ViriiLODBias = 0.75;

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    float4 color = u_Texture0.SampleBias(u_Texture0_sampler, input.v_TexCoord, c_ViriiLODBias);
#if USE_HDR
    color.rgb *= 1.5;
#endif
    float4 o_Color = ffog(color * input.v_Color, input.fogViewSpace);
    fdebugcolor(o_Color, isFrontFace);
    return o_Color;
}
