#include "partials/common.hlsli"

// Color texture to be supersampled + its sampler (shared index 0).
Texture2D    u_Texture0         : register(t0);
SamplerState u_Texture0_sampler : register(s0);

// Fullscreen post-pass parameters.
//   u_ScreenSize.xy = (width, height)
//   u_ScreenSize.zw = (1/width, 1/height)  -> texelSize
cbuffer PerPass : register(b10)
{
    float4 u_ScreenSize;
};

struct PSInput
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float4 ApplySSAA(Texture2D texture0, SamplerState texture0_sampler, float4 screenSize, float2 texCoord)
{
    float2 texelSize = screenSize.zw;
    float4 fragColour;

    //Sample Patterns Used from https://mynameismjp.wordpress.com/2012/10/24/msaa-overview/

    //4x4 Grid
    //Column 1
    float4 col = texture0.Sample(texture0_sampler, texCoord + texelSize * float2(-0.75f,0.75f));
    col += texture0.Sample(texture0_sampler, texCoord + texelSize * float2(-0.75f,0.25f));
    col += texture0.Sample(texture0_sampler, texCoord + texelSize * float2(-0.75f,-0.25f));
    col += texture0.Sample(texture0_sampler, texCoord + texelSize * float2(-0.75f,-0.75f));
    //Column 2
    col += texture0.Sample(texture0_sampler, texCoord + texelSize * float2(-0.25f,0.75f));
    col += texture0.Sample(texture0_sampler, texCoord + texelSize * float2(-0.25f,0.25f));
    col += texture0.Sample(texture0_sampler, texCoord + texelSize * float2(-0.25f,-0.25f));
    col += texture0.Sample(texture0_sampler, texCoord + texelSize * float2(-0.25f,-0.75f));
    //Column 3
    col += texture0.Sample(texture0_sampler, texCoord + texelSize * float2(0.25f,0.75f));
    col += texture0.Sample(texture0_sampler, texCoord + texelSize * float2(0.25f,0.25f));
    col += texture0.Sample(texture0_sampler, texCoord + texelSize * float2(0.25f,-0.25f));
    col += texture0.Sample(texture0_sampler, texCoord + texelSize * float2(0.25f,-0.75f));
    //Column 4
    col += texture0.Sample(texture0_sampler, texCoord + texelSize * float2(0.75f,0.75f));
    col += texture0.Sample(texture0_sampler, texCoord + texelSize * float2(0.75f,0.25f));
    col += texture0.Sample(texture0_sampler, texCoord + texelSize * float2(0.75f,-0.25f));
    col += texture0.Sample(texture0_sampler, texCoord + texelSize * float2(0.75f,-0.75f));

    fragColour = col * 0.0625f;

    return fragColour;
}

float4 PSMain(PSInput input) : SV_Target
{
    return ApplySSAA(u_Texture0, u_Texture0_sampler, u_ScreenSize, input.uv);
}
