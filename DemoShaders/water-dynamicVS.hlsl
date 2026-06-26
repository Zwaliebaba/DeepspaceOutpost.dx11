#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/fog.hlsli"
#include "partials/time.hlsli"
#include "partials/flipsurface.hlsli"

static const float PI = 3.1415926535;

struct VSInput
{
    float2 aPos        : POSITION;   // location 0
    float  aWaterDepth : TEXCOORD0;  // location 1
};

struct VSOutput
{
    float4 pos                       : SV_Position;
    nointerpolation float v_WaveBrightness : TEXCOORD0;
    float4 fogViewSpace              : TEXCOORD1;
};

float timeBase()
{
    return u_ShaderTime * PI;
}

float4 waveEffect(float2 pos, float aWaterDepth)
{
    const float scaleFactor = 7.0f;
    float time = timeBase();

    float xoffset = sin(pos.x * 0.01f + time * 0.20f) * 1.20f +
                    sin(pos.x * 0.03f + time * 0.47f) * 0.90f;
    float zoffset = sin(pos.y * 0.02f + time * 0.23f) * 0.90f +
                    sin(pos.y * 0.03f + time * 0.59f) * 0.65f;

    xoffset *= scaleFactor;
    zoffset *= scaleFactor;

    return float4(pos.x, -1.0f + aWaterDepth * (xoffset + zoffset), pos.y, 1.0f);
}

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    float4 thisVertex = waveEffect(input.aPos, input.aWaterDepth);

    float waveBrightness = (1.0 - input.aWaterDepth);
    waveBrightness *= (thisVertex.y + 40.0f) / 85.0f;
    o.v_WaveBrightness = clamp(waveBrightness, 0.0, 1.0);

    float4 mv_pos = mul(thisVertex, u_ViewMatrix);
    o.fogViewSpace = vfog(mv_pos);

    o.pos = mul(mv_pos, u_ProjectionMatrix);
    vflipsurface(o.pos);
    return o;
}
