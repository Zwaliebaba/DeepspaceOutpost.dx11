// Scene3D billboard vertex shader: the quad corners arrive already in camera space,
// so u_MVP is just the projection. The "normal" slot carries the quad-local uv (-1..1)
// used by the pixel shader to find the radius from the billboard centre.

#include "partials/scenebb.hlsli"

VSOut VSMain(VSIn i)
{
    VSOut o;
    o.pos = mul(u_MVP, float4(i.pos, 1.0));
    o.uv = i.nrm.xy;
    o.col = i.col;
    return o;
}
