#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/flipsurface.hlsli"

struct VSInput
{
    float4 aPos       : POSITION;   // location 0
    float2 aTexCoords : TEXCOORD0;  // location 1
    float4 aColor     : COLOR0;     // location 2
};

struct VSOutput
{
    float4 pos        : SV_Position;
    float2 v_TexCoord : TEXCOORD0;
    float4 v_Color    : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.v_TexCoord = input.aTexCoords;
    o.v_Color = input.aColor;

    float4 mv_pos = mul(input.aPos, u_ViewMatrix);
    o.pos = mul(mv_pos, u_ProjectionMatrix);
    vflipsurface(o.pos);
    return o;
}
