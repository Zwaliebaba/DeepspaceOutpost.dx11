#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/flipsurface.hlsli"

struct VSInput
{
    float4 aPos       : POSITION;   // location 0
    float2 aTexCoords : TEXCOORD0;  // location 1
    float  aAlpha     : TEXCOORD1;  // location 2
};

struct VSOutput
{
    float4 pos        : SV_Position;
    float  v_Alpha    : TEXCOORD0;
    float2 v_TexCoord : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.v_Alpha = input.aAlpha;
    o.v_TexCoord = input.aTexCoords;
    o.pos = mul(input.aPos, u_ViewProjectionMatrix);
    vflipsurface(o.pos);
    return o;
}
