#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/fog.hlsli"
#include "partials/debug.hlsli"

cbuffer PerDraw : register(b9)
{
    float4 u_Color;
};

struct PSInput
{
    float4 pos          : SV_Position;
    float4 fogViewSpace : TEXCOORD0;
};

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    float4 color = ffog(u_Color, input.fogViewSpace);
    fdebugcolor(color, isFrontFace);
    return color;
}
