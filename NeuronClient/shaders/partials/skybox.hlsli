#ifndef SKYBOX_HLSLI
#define SKYBOX_HLSLI

// Procedural skybox (star migration, Phase 2): a full-screen background - a vertical
// gradient plus procedurally-placed stars - drawn behind the 3D scene, replacing the pure
// black clear. No vertex buffer: the VS builds a full-screen triangle from SV_VertexID.
// b0 carries the look parameters so Scene3D can tune it without recompiling.
//
// The star field is orientation-aware: u_SkyXform carries an accumulated roll rotation and
// a pan (fed from the player's roll/pitch), applied to the star-sampling coordinate only -
// so the stars drift and rotate with control input while the gradient stays screen-fixed.

cbuffer SkyboxCb : register(b0)
{
    float4 u_TopColor;    // gradient colour at the top of the view (rgb; a unused)
    float4 u_BottomColor; // gradient colour at the bottom of the view
    float4 u_Params;      // x = star threshold (0..1; higher = fewer stars),
                          // y = star brightness, z = star grid density, w = aspect (w/h)
    float4 u_SkyXform;    // x = cos(roll), y = sin(roll), z = pan X, w = pan Y (star coord)
};

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0; // 0..1 across the visible screen (bottom-left origin)
};

#endif // SKYBOX_HLSLI
