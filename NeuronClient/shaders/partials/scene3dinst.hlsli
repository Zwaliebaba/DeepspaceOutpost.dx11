#ifndef SCENE3DINST_HLSLI
#define SCENE3DINST_HLSLI

// Shared vertex IO + bindings for the Neuron::Graphics::Scene3D INSTANCED ship program
// (H2). Every ship of one hull type is drawn in a single DrawIndexedInstanced: the
// per-vertex stream (slot 0) is the type's immutable solid geometry - the SAME low-poly
// mesh the per-model path uses, so the look is identical - and a per-instance stream
// (slot 1) carries each ship's world matrix and colour tint.
//
// Row-vector convention throughout (clip = mul(pos, M)), like Render2D and unlike the
// per-model Scene3D path: the per-instance world rows come straight from InstanceData
// (no transpose), and u_ViewProj / u_View are uploaded row-major without transpose too.

cbuffer InstFrameCb : register(b0)
{
    row_major float4x4 u_ViewProj; // world -> clip, row-vector (clip = p_world * VP)
    row_major float4x4 u_View;     // world -> view, for view-space normals when lit
    float4 u_LightDir;             // xyz: view-space direction toward the light
    float4 u_Ambient;              // rgb ambient term
    float4 u_Diffuse;              // rgb diffuse term
    float4 u_LightParams;          // x = lit (0 / 1)
};

// Per-vertex (slot 0) is the solid MeshVertex; per-instance (slot 1) is InstanceData:
// four world rows then the tint (tint.a < 0 -> keep the per-face colour).
struct InstIn
{
    float3 pos  : POSITION;
    float3 nrm  : NORMAL;
    float4 col  : COLOR0;
    float4 w0   : TEXCOORD0;
    float4 w1   : TEXCOORD1;
    float4 w2   : TEXCOORD2;
    float4 w3   : TEXCOORD3;
    float4 tint : TEXCOORD4;
};

struct InstOut { float4 pos : SV_Position; float3 nrm : NORMAL; float4 col : COLOR0; };

#endif // SCENE3DINST_HLSLI
