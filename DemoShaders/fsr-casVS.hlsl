#include "partials/common.hlsli"

// Fullscreen triangle generated from the vertex id (no vertex buffer bound).
// FSR RCAS is a fullscreen post pass.

struct VSOutput
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    VSOutput o;
    o.uv  = float2((vertexID << 1) & 2, vertexID & 2);
    o.pos = float4(o.uv * 2.0 + -1.0, 0.0, 1.0);
#if FLIP_Y_BLIT
    o.uv.y = 1.0 - o.uv.y;
#endif
    return o;
}
