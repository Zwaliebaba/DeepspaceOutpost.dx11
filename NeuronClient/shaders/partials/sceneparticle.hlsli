#ifndef SCENEPARTICLE_HLSLI
#define SCENEPARTICLE_HLSLI

// Shared IO + bindings for the Neuron::Graphics::SceneParticles additive billboard program -
// the glowing fireball / spark / trail particles drawn after the opaque ship pass (see
// explosion.md). Each particle is a camera-facing textured quad; the client effects
// subsystem (Neuron::Client::Effects) builds the four corners in the render frame
// (floating-origin-relative) and hands over the whole vertex batch, so the vertex shader
// only transforms by the shared view-projection and passes the sprite uv and per-vertex
// colour through. The pixel shader tints the sprite by that colour and the pass blends it
// additively, so overlapping particles accumulate into a glow.
//
// b0 is row-major (column-vector convention: clip = mul(u_MVP, pos)); u_MVP is the camera
// view-projection, uploaded transposed by SceneParticles::Render - the same convention as
// the scene3d / scenebb programs.

cbuffer ParticleCb : register(b0)
{
    row_major float4x4 u_MVP;
};

struct VSIn  { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; };

#endif // SCENEPARTICLE_HLSLI
