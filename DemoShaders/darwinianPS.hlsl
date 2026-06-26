#include "partials/common.hlsli"
#include "partials/fog.hlsli"
#include "partials/matrices.hlsli"

struct PSInput
{
    float4 pos          : SV_Position;
    float2 v_TexCoord   : TEXCOORD0;
    float4 v_Color      : TEXCOORD1;
    float4 fogViewSpace : TEXCOORD2;
};

Texture2D    u_Texture0         : register(t0);
SamplerState u_Texture0_sampler : register(s0);

static const float c_Cutoff = 0.4;
static const float c_DiscardCutoff = 0.25;
static const float c_DarwinianLODBias = -0.5;

float4 PSMain(PSInput input) : SV_Target
{
    float4 color = input.v_Color * u_Texture0.SampleBias(u_Texture0_sampler, input.v_TexCoord, c_DarwinianLODBias);
    color.a = clamp((color.a - c_Cutoff) / max(fwidth(color.a), 0.0001), 0.0, 1.0);
    if (color.a < c_DiscardCutoff)
        discard;
    return ffog(color, input.fogViewSpace);
}
