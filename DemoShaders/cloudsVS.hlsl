#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/fog.hlsli"
#include "partials/time.hlsli"
#include "partials/flipsurface.hlsli"

struct VSInput
{
    float4 aPos       : POSITION;   // location 0
    float2 aTexCoord  : TEXCOORD0;  // location 1
    float4 aColor     : COLOR0;     // location 2
    int2   aLayerInfo : TEXCOORD1;  // location 3
};

struct VSOutput
{
    float4 pos                 : SV_Position;
    float2 v_TexCoord          : TEXCOORD0;
    float4 v_Color             : TEXCOORD1;
    nointerpolation int v_TexType : TEXCOORD2;
    float4 fogViewSpace        : TEXCOORD3;
};

static const float2 CloudVelocity = float2(0.03f, -0.01f);

float2 layerTime()
{
    return float2(u_ShaderTime * 0.8, u_ShaderTime * 0.8);
}

float layerResolution(int2 aLayerInfo)
{
    float detail = 9.0f;

    if (aLayerInfo[0] >= 1)
        detail *= 0.5f;
    if (aLayerInfo[0] >= 2)
        detail *= 0.4f;

    return 1.0f + detail;
}

float2 layerVelocity(int2 aLayerInfo)
{
    if (aLayerInfo[0] == 0)
        return CloudVelocity.xy;
    else
        return float2(CloudVelocity.x, CloudVelocity.x + 0.01);
}

float2 baseTexOffset(int2 aLayerInfo)
{
    return layerVelocity(aLayerInfo) * layerTime();
}

float2 cloudTexOffset(int2 aLayerInfo)
{
    float multiplier = 1.0f;
    if (aLayerInfo[0] == 1)
        multiplier = 0.5f;
    return baseTexOffset(aLayerInfo) * multiplier;
}

float cloudY(int2 aLayerInfo)
{
    return 1200.0 - float(aLayerInfo[0]) * 200.0;
}

VSOutput VSMain(VSInput input)
{
    VSOutput o;

    float size = layerResolution(input.aLayerInfo);
    float2 offset = cloudTexOffset(input.aLayerInfo);

    o.v_TexType = input.aLayerInfo[1];
    o.v_Color = input.aColor;
    o.v_TexCoord = input.aTexCoord * size + offset;

    float4 mv_pos = mul(float4(input.aPos.x, cloudY(input.aLayerInfo), input.aPos.zw), u_ViewMatrix);
    o.fogViewSpace = vfog(mv_pos);
    o.pos = mul(mv_pos, u_ProjectionMatrix);
    vflipsurface(o.pos);
    return o;
}
