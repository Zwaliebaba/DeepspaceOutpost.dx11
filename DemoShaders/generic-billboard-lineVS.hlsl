#include "partials/common.hlsli"
#include "partials/camera.hlsli"
#include "partials/clipping.hlsli"
#include "partials/matrices.hlsli"
#include "partials/fog.hlsli"
#include "partials/flipsurface.hlsli"

struct VSInput
{
    float3 aStartPos : POSITION;    // location 0 (instance)
    float3 aEndPos   : TEXCOORD0;   // location 1 (instance)
    float  aWidth    : TEXCOORD1;   // location 2 (instance)
    float4 aColor    : COLOR0;      // location 3 (instance)
    uint   vertexID  : SV_VertexID;
};

struct VSOutput
{
    float4 pos          : SV_Position;
    float4 color        : TEXCOORD0;
    float2 uv           : TEXCOORD1;
    float4 fogViewSpace : TEXCOORD2;
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
#if FEATURE_NATIVE_CLIPPING
    float2 clipDist     : SV_ClipDistance0;
#else
    float2 clipDist     : TEXCOORD3;
#endif
#endif
};

static const float2 texCoordBase[4] =
{
    float2(0.1, 0.0),
    float2(0.1, 1.0),
    float2(0.9, 0.0),
    float2(0.9, 1.0)
};

float2 texCoords(uint vertexID)
{
    return texCoordBase[vertexID];
}

float3 position(VSInput input)
{
    float isRight = float(input.vertexID & 1);        // 0,1,0,1
    float isTop   = float((input.vertexID >> 1) & 1); // 0,0,1,1

    float3 midPoint = (input.aEndPos + input.aStartPos) * 0.5;
    float3 camToMidPoint = normalize(u_CameraPos - midPoint);
    float3 lineDir = normalize(input.aEndPos - midPoint);

    float3 perpDir = normalize(cross(camToMidPoint, lineDir));
    float3 offset = perpDir * input.aWidth * -(isRight - 0.5);

    float3 basePos = lerp(input.aStartPos, input.aEndPos, isTop);
    float3 finalPos = basePos + offset;

    return finalPos;
}

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.color = input.aColor;
    o.uv    = texCoords(input.vertexID);

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
