#include "partials/common.hlsli"

// SMAA edge detection (color) -- vertex stage.
#define SMAA_INCLUDE_VS 1
#define SMAA_INCLUDE_PS 0

#include "partials/smaa-header.hlsli"
#include "partials/smaa-util.hlsli"
#include "partials/smaa.hlsli"

struct VSOutput
{
    float4 pos      : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 offset0  : TEXCOORD1;
    float4 offset1  : TEXCOORD2;
    float4 offset2  : TEXCOORD3;
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    VSOutput o;
    float2 texcoord = float2((vertexID << 1) & 2, vertexID & 2);
    o.pos = float4(texcoord * 2.0 + -1.0, 0.0, 1.0);
#if FLIP_Y_BLIT
    texcoord = flipTexCoord(texcoord);
#endif
    o.texcoord = texcoord;

    float4 offset[3];
    SMAAEdgeDetectionVS(texcoord, offset);
    o.offset0 = offset[0];
    o.offset1 = offset[1];
    o.offset2 = offset[2];
    return o;
}
