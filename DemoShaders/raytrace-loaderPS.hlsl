#include "partials/common.hlsli"

Texture2D    u_Texture0         : register(t0);
SamplerState u_Texture0_sampler : register(s0);

cbuffer PerPass : register(b10)
{
    float u_Time;
    float u_WaveAmount;
    float u_PixelBlur;
    float u_MotionBlur;
    float u_Brightness;
    float2 u_Resolution;
    float2 u_ResolutionInv;
};

struct PSInput
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target
{
    float2 texCoord = input.uv;

    // Apply wave distortion effect
    if (u_WaveAmount > 0.0) {
        float dx = sin(u_Time * 5.0 + texCoord.y * u_Resolution.y * 0.1) * u_WaveAmount;
        float dy = sin(u_Time * 5.0 + texCoord.x * u_Resolution.x * 0.1) * u_WaveAmount;
        texCoord += float2(dx, dy) * u_ResolutionInv;
    }

    // Apply vertical blur effect (similar to the triangle strip approach)
    float4 finalColor = float4(0.0, 0.0, 0.0, 0.0);

    finalColor.a = 1.0;

    // Handle the zero-blur case specially
    if (u_PixelBlur <= 1.0) {
        finalColor = u_Texture0.Sample(u_Texture0_sampler, texCoord);
    } else {
        // Sample the original pixel with full weight
        finalColor += u_Texture0.Sample(u_Texture0_sampler, texCoord);

        // Add additional samples with decreasing weights
        for (float i = 1.0; i < u_PixelBlur; i += 1.0) {
            float weight = 1.0 - (i / u_PixelBlur);
            float2 offset = float2(0.0, i * u_ResolutionInv.y);
            finalColor += u_Texture0.Sample(u_Texture0_sampler, texCoord + offset) * weight;
        }
    }

    // Apply brightness
    float brightnessCompensation = 1.0 + (u_PixelBlur * 0.25);
    finalColor.rgb *= u_Brightness * brightnessCompensation;

    // Apply motion blur factor as alpha
    finalColor.a = (1.0 - u_MotionBlur);

    return finalColor;
}
