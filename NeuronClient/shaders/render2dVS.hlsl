// The shared 2D vertex shader for every Render2D program (default + text outline).
// Maps the virtual pixel position through the b0 ortho; passes uv + colour through.

#include "partials/render2d.hlsli"

VSOut VSMain(VSIn i)
{
    VSOut o;
    o.pos = mul(float4(i.pos, 0.0, 1.0), u_Proj);
    o.uv  = i.uv;
    o.col = i.col;
    return o;
}
