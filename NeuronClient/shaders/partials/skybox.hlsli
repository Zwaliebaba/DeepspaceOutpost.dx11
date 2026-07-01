#ifndef SKYBOX_HLSLI
#define SKYBOX_HLSLI

// Cube skybox (star migration): a real six-face environment cubemap drawn as the flight
// background, replacing the pure black clear. No vertex buffer - the VS builds a full-screen
// triangle from SV_VertexID and hands each pixel a view-space ray; the PS rotates that ray
// into world space by the camera orientation and samples the cubemap.
//
// View space is x-right, y-up, z-forward (matching SceneProjection). "World" is whatever
// orientation the cubemap art is authored in; u_Rot0..2 is the camera->world rotation the
// game accumulates from the player's roll/pitch (+ the per-view look direction), so the sky
// stays fixed in the world while the ship turns.

cbuffer SkyboxCb : register(b0)
{
    float4 u_Params; // x = tan(halfFovY), y = aspect (w/h), z = exposure (colour multiplier), w unused
    float4 u_Rot0;   // camera->world rotation, row 0 (xyz; w unused)
    float4 u_Rot1;   // row 1
    float4 u_Rot2;   // row 2
};

TextureCube  g_Sky        : register(t0);
SamplerState g_SkySampler : register(s0);

struct VSOut
{
    float4 pos     : SV_Position;
    float3 viewDir : TEXCOORD0; // view-space ray (x right, y up, z forward)
};

#endif // SKYBOX_HLSLI
