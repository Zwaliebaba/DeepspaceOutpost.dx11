#ifndef DUST_HLSLI
#define DUST_HLSLI

// Dust points: the streaming starfield rendered in the 3D scene pass (as the background,
// behind the ships) instead of the legacy 2D batch. The CPU
// projects each point with the same optics as the legacy starfield and hands it over
// already in clip-space XY, so the vertex shader is a pass-through; brightness rides in
// the vertex. Drawn as small screen-space quads (depth-disabled).

struct VSIn  { float2 pos : POSITION; float bright : COLOR0; };
struct VSOut { float4 pos : SV_Position; float bright : COLOR0; };

#endif // DUST_HLSLI
