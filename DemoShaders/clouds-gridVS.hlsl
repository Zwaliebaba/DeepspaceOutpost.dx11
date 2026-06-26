#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/fog.hlsli"
#include "partials/flipsurface.hlsli"

cbuffer PerDraw : register(b9)
{
    float u_GridSize;
    float u_StartPos;
    float u_EndPos;
    int   u_Direction;
};

struct VSInput
{
    float4 aPos      : POSITION;   // location 0
    float2 aTexCoord : TEXCOORD0;  // location 1
};

struct VSOutput
{
    float4 pos          : SV_Position;
    float2 v_TexCoord   : TEXCOORD0;
    float4 fogViewSpace : TEXCOORD1;
};

VSOutput VSMain(VSInput input, uint instanceID : SV_InstanceID)
{
    VSOutput o;

    float4 position = input.aPos;

    float offset = u_StartPos + (float(instanceID) * u_GridSize);
    if (u_Direction == 0) {
        position.x += offset;
        position.z *= (u_EndPos - u_StartPos); // Scale to full grid size
    } else {
        float temp = position.x;
        position.x = -position.z * (u_EndPos - u_StartPos); // Scale to full grid size
        position.z = temp + offset;
    }

    o.v_TexCoord = input.aTexCoord;
    float4 mv_pos = mul(position, u_ViewMatrix);
    o.fogViewSpace = vfog(mv_pos);
    o.pos = mul(mv_pos, u_ProjectionMatrix);
    vflipsurface(o.pos);
    return o;
}
