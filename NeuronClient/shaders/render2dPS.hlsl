// Default 2D program: per-vertex colour times the bound texture. Colored primitives
// bind the 1x1 white texture, so the result is just the colour.

#include "partials/render2d.hlsli"

float4 PSMain(VSOut i) : SV_Target
{
    return i.col * u_Tex.Sample(u_Smp, i.uv);
}
