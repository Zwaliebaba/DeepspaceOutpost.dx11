// Dust vertex shader: pass-through. The CPU supplies clip-space XY (already projected
// with the scene optics); depth is a fixed mid value (the pass is depth-disabled).

#include "partials/dust.hlsli"

VSOut VSMain(VSIn i)
{
    VSOut o;
    o.pos = float4(i.pos, 0.5, 1.0);
    o.bright = i.bright;
    return o;
}
