#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/fog.hlsli"
#include "partials/debug.hlsli"

Texture2D    u_Texture0         : register(t0); // low detail, linear
SamplerState u_Texture0_sampler : register(s0);
Texture2D    u_Texture1         : register(t1); // high detail, linear or point
SamplerState u_Texture1_sampler : register(s1);

struct PSInput
{
    float4 pos                 : SV_Position;
    float2 v_TexCoord          : TEXCOORD0;
    float4 v_Color             : TEXCOORD1;
    nointerpolation int v_TexType : TEXCOORD2;
    float4 fogViewSpace        : TEXCOORD3;
};

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    float4 color;
    if (input.v_TexType == 0)
        color = u_Texture1.Sample(u_Texture1_sampler, input.v_TexCoord);
    else
        color = u_Texture0.Sample(u_Texture0_sampler, input.v_TexCoord);
    float4 o_Color = ffog(input.v_Color * color, input.fogViewSpace);
    fdebugcolor(o_Color, isFrontFace);
    return o_Color;
}
