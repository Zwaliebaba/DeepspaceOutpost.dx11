// Procedural skybox vertex shader: emit a full-screen triangle straight from the vertex
// id (no vertex buffer / input layout needed). The oversized triangle covers the screen;
// uv spans 0..1 over the visible region, with uv.y = 1 at the top.

#include "partials/skybox.hlsli"

VSOut VSMain(uint _vid : SV_VertexID)
{
    VSOut o;
    const float2 uv = float2(float((_vid << 1) & 2u), float(_vid & 2u)); // (0,0) (2,0) (0,2)
    o.uv = uv;
    o.pos = float4(uv * 2.0 - 1.0, 1.0, 1.0);            // covers clip [-1,1]; z = 1 (far)
    return o;
}
