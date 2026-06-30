#ifndef RENDER2D_HLSLI
#define RENDER2D_HLSLI

// Shared vertex IO + bindings for the Neuron::Graphics::Render2D programs.
//
// b0 is the row-major orthographic matrix (row-vector convention: clip = mul(pos, M)),
// uploaded by Render2D::Begin. One interleaved vertex (POSITION/TEXCOORD0/COLOR0) and
// one texture at t0 / sampler at s0 serve every program; colored primitives bind a 1x1
// white texture so `col * tex` is just the per-vertex colour.

cbuffer Cb2D : register(b0)
{
    row_major float4x4 u_Proj;
};

struct VSIn  { float2 pos : POSITION;     float2 uv : TEXCOORD0; float4 col : COLOR0; };
struct VSOut { float4 pos : SV_Position;  float2 uv : TEXCOORD0; float4 col : COLOR0; };

Texture2D    u_Tex : register(t0);
SamplerState u_Smp : register(s0);

#endif // RENDER2D_HLSLI
