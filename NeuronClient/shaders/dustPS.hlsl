// Dust pixel shader: a star sprite (Starburst.dds) tinted by the per-star spectral colour
// and scaled by intensity (magnitude x distance falloff). Drawn with additive blending, so
// the returned colour is the light the star contributes; overlapping stars accumulate and
// the soft sprite feathers into the black background instead of reading as a hard square.
//
// The sprite's shape comes from rgb x alpha, which works whether the DDS carries the glow
// in its colour channels (alpha == 1) or in its alpha channel (white rgb) - either way the
// black surround contributes nothing under additive blend.

#include "partials/dust.hlsli"

Texture2D    starTex  : register(t0);
SamplerState starSamp : register(s0);

float4 PSMain(VSOut i) : SV_Target
{
    float4 tex = starTex.Sample(starSamp, i.uv);
    float3 shape = tex.rgb * tex.a;
    return float4(shape * i.color * i.intensity, 1.0);
}
