#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/fog.hlsli"
#include "partials/time.hlsli"
#include "partials/flipsurface.hlsli"

cbuffer PerDraw : register(b9)
{
    float2 u_TexScale;
};

struct VSInput
{
    float2 aPos       : POSITION;   // location 0
    float2 aTexCoord0 : TEXCOORD0;  // location 1
    float2 aTexCoord1 : TEXCOORD1;  // location 2
};

struct VSOutput
{
    float4 pos          : SV_Position;
    float2 v_TexCoord0  : TEXCOORD0;
    float2 v_TexCoord1  : TEXCOORD1;
    float2 v_TexCoord2  : TEXCOORD2;
    float4 fogViewSpace : TEXCOORD3;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    float offset = u_ShaderTime * 0.075;
    o.v_TexCoord0 = input.aTexCoord0 * u_TexScale + (float2)offset;
    o.v_TexCoord1 = input.aTexCoord1;
    o.v_TexCoord2 = input.aTexCoord0 * u_TexScale + float2(-offset, offset);
    float4 mv_pos = mul(float4(input.aPos.x, -12.0f, input.aPos.y, 1.0f), u_ViewMatrix);
    o.fogViewSpace = vfog(mv_pos);
    o.pos = mul(mv_pos, u_ProjectionMatrix);
    vflipsurface(o.pos);
    return o;
}
