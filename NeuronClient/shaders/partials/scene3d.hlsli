#ifndef SCENE3D_HLSLI
#define SCENE3D_HLSLI

// Shared vertex IO + bindings for the Neuron::Graphics::Scene3D program - the GPU
// successor to the CPU-projected flight wireframe. One interleaved 3D vertex
// (model-space position + normal + RGBA8 face colour) is transformed by a single
// model->clip matrix per draw.
//
// b0 is row-major (column-vector convention: clip = mul(u_MVP, pos)), uploaded per
// model by Scene3D::RenderModels. u_Color is a flat tint: when its alpha is < 0 the
// per-vertex (face) colour is used; otherwise u_Color.rgb overrides it.

cbuffer SceneCb : register(b0)
{
    row_major float4x4 u_MVP;
    float4 u_Color;
};

struct VSIn  { float3 pos : POSITION; float3 nrm : NORMAL; float4 col : COLOR0; };
struct VSOut { float4 pos : SV_Position; float3 nrm : NORMAL; float4 col : COLOR0; };

#endif // SCENE3D_HLSLI
