#include "partials/common.hlsli"
#include "partials/camera.hlsli"
#include "partials/clipping.hlsli"
#include "partials/matrices.hlsli"
#include "partials/fog.hlsli"
#include "partials/flipsurface.hlsli"

cbuffer PerDraw : register(b9)
{
    int u_TexCoordType;
};

struct VSInput
{
    float2 aPosOffset : POSITION;    // location 0 (base)
    float3 aCenter    : TEXCOORD0;   // location 1 (instance)
    float  aScale     : TEXCOORD1;   // location 2 (instance)
    float4 aColor     : COLOR0;      // location 3 (instance)
    uint   vertexID   : SV_VertexID;
};

struct VSOutput
{
    float4 pos          : SV_Position;
    float2 uv           : TEXCOORD0;
    float4 color        : TEXCOORD1;
    float4 fogViewSpace : TEXCOORD2;
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
#if FEATURE_NATIVE_CLIPPING
    float2 clipDist     : SV_ClipDistance0;
#else
    float2 clipDist     : TEXCOORD3;
#endif
#endif
};

static const float2 texCoordBase[8] =
{
    // u_TexCoordType == 0, "up"
    float2(1.0, 0.0), // top right
    float2(0.0, 0.0), // top left
    float2(1.0, 1.0), // bottom right
    float2(0.0, 1.0), // bottom left

    // u_TexCoordType == 1, "center"
    float2(0.5, 0.5),
    float2(0.5, 0.5),
    float2(0.5, 0.5),
    float2(0.5, 0.5)
};

float2 texCoords(uint vertexID)
{
    return texCoordBase[u_TexCoordType * 4 + vertexID];
}

float3 position(VSInput input)
{
    return input.aCenter
         + u_CameraRight * input.aPosOffset.x * input.aScale
         + u_CameraUp    * input.aPosOffset.y * input.aScale;
}

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.uv    = texCoords(input.vertexID);
    o.color = input.aColor;

    float3 bbPos = position(input);
    float4 mv_pos = mul(float4(bbPos, 1.0), u_ViewMatrix);
    o.fogViewSpace = vfog(mv_pos);
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
    o.clipDist = vclipping(mv_pos);
#endif
    o.pos = mul(mv_pos, u_ProjectionMatrix);
    vflipsurface(o.pos);
    return o;
}
