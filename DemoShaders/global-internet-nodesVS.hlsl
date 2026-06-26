#include "partials/common.hlsli"
#include "partials/camera.hlsli"
#include "partials/matrices.hlsli"
#include "partials/fog.hlsli"
#include "partials/flipsurface.hlsli"

cbuffer PerDraw : register(b9)
{
    float u_NodeSize;
};

struct VSInput
{
    float2 aPosOffset : TEXCOORD0; // location 0 (base)
    float3 aCenter    : TEXCOORD1; // location 1 (instance)
};

struct VSOutput
{
    float4 pos          : SV_Position;
    float2 v_TexCoord   : TEXCOORD0;
    float4 fogViewSpace : TEXCOORD1;
};

static const float2 texCoords[4] =
{
    float2(1.0, 0.0), // top right
    float2(0.0, 0.0), // top left
    float2(1.0, 1.0), // bottom right
    float2(0.0, 1.0)  // bottom left
};

VSOutput VSMain(VSInput input, uint vertexID : SV_VertexID)
{
    VSOutput o;

    float3 bbPos = input.aCenter + u_CameraRight * input.aPosOffset.x * u_NodeSize + u_CameraUp * input.aPosOffset.y * u_NodeSize;
    float4 mv_pos = mul(float4(bbPos, 1.0), u_ViewMatrix);
    o.v_TexCoord = texCoords[vertexID % 4];
    o.fogViewSpace = vfog(mv_pos);
    o.pos = mul(mv_pos, u_ProjectionMatrix);
    vflipsurface(o.pos);
    return o;
}
