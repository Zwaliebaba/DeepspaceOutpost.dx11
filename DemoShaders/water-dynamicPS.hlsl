#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/fog.hlsli"

Texture2D    u_Texture0         : register(t0);
SamplerState u_Texture0_sampler : register(s0);

struct PSInput
{
    float4 pos                       : SV_Position;
    nointerpolation float v_WaveBrightness : TEXCOORD0;
    float4 fogViewSpace              : TEXCOORD1;
};

float4 PSMain(PSInput input) : SV_Target
{
    float4 waveSample = u_Texture0.Sample(u_Texture0_sampler, float2(input.v_WaveBrightness, 0.0));
    float waveAlpha = clamp(input.v_WaveBrightness * 2.0, 0.0, 1.0);
#if USE_HDR
    waveAlpha *= 1.1;
#endif
    return float4(ffog(float4(waveSample.rgb, waveAlpha), input.fogViewSpace).rgb, waveAlpha);
}
