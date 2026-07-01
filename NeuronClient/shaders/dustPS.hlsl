// Dust pixel shader: a plain white point scaled by the per-vertex brightness.

#include "partials/dust.hlsli"

float4 PSMain(VSOut i) : SV_Target
{
    return float4(i.bright, i.bright, i.bright, 1.0);
}
