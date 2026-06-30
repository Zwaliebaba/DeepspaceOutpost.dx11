#ifndef SCENEBB_HLSLI
#define SCENEBB_HLSLI

// Shared IO + bindings for the Scene3D billboard program - the GPU successor to the
// software planet/sun pixel rasterizers (render_planet / render_sun). A planet or sun
// is drawn as a camera-facing quad whose vertices are already in camera space (so the
// model->clip matrix is just the projection), depth-tested with the ships so it
// occludes correctly. The pixel shader reproduces the flat screen-space disk look.
//
// b0 (SceneCb) is shared with the ship program: u_MVP is the projection (camera->clip),
// u_Color is unused here. b1 carries the per-billboard parameters.

cbuffer SceneCb : register(b0)
{
    row_major float4x4 u_MVP;
    float4 u_Color;
};

cbuffer BillboardCb : register(b1)
{
    float4 u_ColorB;  // secondary colour (banded planet styles), rgba in 0..1
    float4 u_Params;  // x = mode (0 sun, 1 ring, 2 disk, 3 banded); y,z,w spare
};

struct VSIn  { float3 pos : POSITION; float3 nrm : NORMAL; float4 col : COLOR0; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; };

#endif // SCENEBB_HLSLI
