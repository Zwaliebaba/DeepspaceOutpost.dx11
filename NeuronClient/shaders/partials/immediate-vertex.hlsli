#ifndef IMMEDIATE_VERTEX_HLSLI
#define IMMEDIATE_VERTEX_HLSLI

// The single interleaved vertex format the immediate-mode renderer emits. Every
// generic shader takes this layout and reads only the fields it needs, so one
// D3D11 input layout serves all immediate draws. Must match
// Neuron::Graphics::ImmediateVertex (NeuronClient/ImmediateRenderer.h).
struct VSInput
{
    float3 aPos       : POSITION;
    float3 aNormal    : NORMAL;
    float4 aColor     : COLOR0;     // R8G8B8A8_UNORM -> rgba in the shader
    float2 aTexCoord0 : TEXCOORD0;
    float2 aTexCoord1 : TEXCOORD1;
};

#endif // IMMEDIATE_VERTEX_HLSLI
