#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/clipping.hlsli"

Texture2D    u_Texture0         : register(t0);
SamplerState u_Texture0_sampler : register(s0);

cbuffer PerDraw : register(b9)
{
    float4 u_Color;
    float4 u_ColorEdge;
    float4 u_ColorCenter;
};

struct PSInput
{
    float4 pos             : SV_Position;
    float2 v_TexCoord      : TEXCOORD0;
    float2 v_GradientCoord : TEXCOORD1;
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
#if !FEATURE_NATIVE_CLIPPING
    float2 clipDist        : TEXCOORD2;
#endif
#endif
};

float4 PSMain(PSInput input) : SV_Target
{
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1) && !FEATURE_NATIVE_CLIPPING
    fclipping(input.clipDist);
#endif

    float t = 1.0 - abs(input.v_GradientCoord.y - 0.5) / 0.5;

    // Gradient is u_ColorEdge at the top and bottom, and u_ColorCenter at the center
    float3 gradientColor = mix(u_ColorEdge.rgb, u_ColorCenter.rgb, t);

    // Line texture overlay
    float3 texColor = u_Texture0.Sample(u_Texture0_sampler, input.v_TexCoord).rgb * 0.075;

    // Mix the colors together
    float4 finalColor = float4(gradientColor + texColor, 1.0);
    return clamp(finalColor * u_Color, 0.0, 1.0);
}
