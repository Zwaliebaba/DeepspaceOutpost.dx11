#include "partials/common.hlsli"
#include "partials/camera.hlsli"
#include "partials/matrices.hlsli"
#include "partials/fog.hlsli"
#include "partials/flipsurface.hlsli"

struct VSInput
{
    float4 aColor  : COLOR0;    // location 0
    float  aSin    : TEXCOORD0; // location 1
    float  aCos    : TEXCOORD1; // location 2
    float3 aCenter : TEXCOORD2; // location 3 (instance)
    float  aSize   : TEXCOORD3; // location 4 (instance)
    float  aLife   : TEXCOORD4; // location 5 (instance)
};

struct VSOutput
{
    float4 pos          : SV_Position;
    float4 v_Color      : TEXCOORD0;
    float4 fogViewSpace : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;

    float radius = 35.0f + 40.0f * (input.aSize - input.aLife);
    float alpha = clamp(0.6f * input.aLife / input.aSize, 0.0f, 1.0f);

    o.v_Color = float4(input.aColor.rgb, input.aColor.a * alpha);

    float4 pos = float4(input.aCenter + float3(radius * input.aSin, 5.0f, radius * input.aCos), 1.0);
    float4 mv_pos = mul(pos, u_ViewMatrix);

    o.fogViewSpace = vfog(mv_pos);
    o.pos = mul(mv_pos, u_ProjectionMatrix);
    vflipsurface(o.pos);
    return o;
}
