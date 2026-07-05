#ifndef DUST_HLSLI
#define DUST_HLSLI

// Dust points: the streaming starfield rendered in the 3D scene pass (as the background,
// behind the ships) instead of the legacy 2D batch. The CPU projects each point with the
// same optics as the legacy starfield and hands it over already in clip-space XY, so the
// vertex shader is a pass-through. Each star is a small screen-space quad (depth-disabled,
// additive blend) textured with the Starburst sprite; the per-vertex uv samples the sprite,
// and the per-vertex colour (spectral tint) x intensity (magnitude x distance) shades it.

struct VSIn
{
    float2 pos       : POSITION;   // clip-space XY (CPU-projected)
    float2 uv        : TEXCOORD0;  // sprite uv, 0..1 across the quad
    float3 color     : COLOR0;     // spectral tint (blue-white .. white .. red)
    float  intensity : COLOR1;     // magnitude x distance falloff
};

struct VSOut
{
    float4 pos       : SV_Position;
    float2 uv        : TEXCOORD0;
    float3 color     : COLOR0;
    float  intensity : COLOR1;
};

#endif // DUST_HLSLI
