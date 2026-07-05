// Dust vertex shader: pass-through. The CPU supplies clip-space XY (already projected
// with the scene optics); depth is a fixed mid value (the pass is depth-disabled). The
// sprite uv, spectral tint and intensity ride the vertex through to the pixel shader.

#include "partials/dust.hlsli"

VSOut VSMain(VSIn i)
{
    VSOut o;
    o.pos = float4(i.pos, 0.5, 1.0);
    o.uv = i.uv;
    o.color = i.color;
    o.intensity = i.intensity;
    return o;
}
