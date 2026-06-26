#include "partials/common.hlsli"

Texture2D    u_Texture0         : register(t0);
SamplerState u_Texture0_sampler : register(s0);

cbuffer PerDraw : register(b9)
{
    float u_Aspect;
};

struct PSInput
{
    float4 pos        : SV_Position;
    float2 v_TexCoord : TEXCOORD0;
    float2 v_Center   : TEXCOORD1;
    float  v_InvSize  : TEXCOORD2;
    float  v_Height   : TEXCOORD3;
    float  v_Time     : TEXCOORD4;
};

float2 PSMain(PSInput input) : SV_Target
{
    float soften = max(1.0 - input.v_Time, 0.0);

    float2 dir = (input.v_TexCoord - input.v_Center);
    float dist = length(dir * float2(u_Aspect, 1.0f));
    if (dist != 0.0)
    {
        float2 delta = u_Texture0.Sample(u_Texture0_sampler, float2(input.v_Time, dist * input.v_InvSize)).x * input.v_Height / dist * dir;

        // Soften over time
        delta *= soften;

        return delta.xy;
    }
    else
    {
        return float2(0.0, 0.0);
    }
}
