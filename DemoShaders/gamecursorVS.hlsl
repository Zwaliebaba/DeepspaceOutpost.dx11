#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/fog.hlsli"
#include "partials/flipsurface.hlsli"

struct VSInput
{
    float4 aPos       : POSITION;   // location 0
    float3 aTexCoords : TEXCOORD0;  // location 1
    float4 aColor     : COLOR0;     // location 2
};

struct VSOutput
{
    float4 pos          : SV_Position;
    float3 v_TexCoord   : TEXCOORD0;
    float4 v_Color      : TEXCOORD1;
    float4 fogViewSpace : TEXCOORD2;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.v_TexCoord = input.aTexCoords;
#if USE_HDR
    o.v_Color = float4(input.aColor.rgb * 1.25, input.aColor.a);
#else
    o.v_Color = input.aColor;
#endif

    float4 mv_pos = mul(input.aPos, u_ViewMatrix);
    o.fogViewSpace = vfog(mv_pos);
    o.pos = mul(mv_pos, u_ProjectionMatrix);
    vflipsurface(o.pos);
    return o;
}
