#include "partials/common.hlsli"
#include "partials/perdraw.hlsli"

Texture2D u_Texture0 : register(t0);
SamplerState u_Texture0_sampler : register(s0);

struct PSInput
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target
{
    float4 color = u_Color * u_Texture0.Sample(u_Texture0_sampler, input.uv);
    // 4x boost matches the sample Text shader (the glyph atlas is stored dim).
    color *= 4.0;
    return clamp(color, 0.0, 1.0);
}
