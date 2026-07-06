// SceneParticles pixel shader: a soft sprite (Particle.dds) tinted by the interpolated
// per-vertex colour. Drawn with additive blending (SrcBlend = SRC_ALPHA, DestBlend = ONE),
// so the returned colour is the light the particle contributes and overlapping particles
// glow rather than punching opaque quads. The sprite's black surround contributes nothing
// under additive blend, so the quad reads as a soft dot.

#include "partials/sceneparticle.hlsli"

Texture2D    partTex  : register(t0);
SamplerState partSamp : register(s0);

float4 PSMain(VSOut i) : SV_Target
{
    return partTex.Sample(partSamp, i.uv) * i.col;
}
