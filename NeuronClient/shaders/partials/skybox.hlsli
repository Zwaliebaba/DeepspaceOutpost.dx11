#ifndef SKYBOX_HLSLI
#define SKYBOX_HLSLI

// Procedural skybox (star migration, Phase 2): a full-screen background - a vertical
// gradient plus procedurally-placed stars - drawn behind the 3D scene, replacing the pure
// black clear. No vertex buffer: the VS builds a full-screen triangle from SV_VertexID.
// b0 carries the look parameters so Scene3D can tune it without recompiling.
//
// This first pass places stars in screen space (they do not track the camera). An
// orientation-aware version that keeps stars fixed in world space lands with the dust
// migration, once the camera orientation is plumbed into the scene pass.

cbuffer SkyboxCb : register(b0)
{
    float4 u_TopColor;    // gradient colour at the top of the view (rgb; a unused)
    float4 u_BottomColor; // gradient colour at the bottom of the view
    float4 u_Params;      // x = star threshold (0..1; higher = fewer stars),
                          // y = star brightness, z = star grid density, w = unused
};

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0; // 0..1 across the visible screen (bottom-left origin)
};

#endif // SKYBOX_HLSLI
