#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/fog.hlsli"
#include "partials/debug.hlsli"
#include "partials/time.hlsli"

Texture2D    u_Texture0         : register(t0); // color texture
SamplerState u_Texture0_sampler : register(s0);
Texture2D    u_Texture1         : register(t1); // light map texture
SamplerState u_Texture1_sampler : register(s1);
Texture2D    u_Texture2         : register(t2); // flow map texture
SamplerState u_Texture2_sampler : register(s2);

struct PSInput
{
    float4 pos          : SV_Position;
    float2 v_TexCoord0  : TEXCOORD0;
    float2 v_TexCoord1  : TEXCOORD1;
    float2 v_TexCoord2  : TEXCOORD2;
    float4 fogViewSpace : TEXCOORD3;
};

static const float c_ColorLODBias = 1.0;
static const float c_LightmapLODBias = 0.0;
static const float2 c_UVJump = float2(0.25, 0.25);
static const float c_FlowSpeed = 0.125;
static const float c_StaticWeight = 0.55;

float3 FlowUVW(float2 uv, float2 flowVector, float time, bool flowB)
{
    float phaseOffset = flowB ? 0.5 : 0.0;
    float progress = frac(time + phaseOffset);
    float3 uvw;
    uvw.xy = uv - flowVector * progress;
    uvw.xy += (time - progress) * c_UVJump;
    uvw.z = 1.0 - abs(1.0 - 2.0 * progress);
    return uvw;
}

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    float4 lightmap = u_Texture1.SampleBias(u_Texture1_sampler, input.v_TexCoord1, c_LightmapLODBias);
#if DISCARD_OPTIMAL
    if (lightmap.a < 0.005)
        discard;
#endif
    float3 uvwA = FlowUVW(input.v_TexCoord2, u_Texture2.Sample(u_Texture2_sampler, input.v_TexCoord2).xy, u_ShaderTime * c_FlowSpeed, false);
    float3 uvwB = FlowUVW(input.v_TexCoord2, u_Texture2.Sample(u_Texture2_sampler, input.v_TexCoord2).xy, u_ShaderTime * c_FlowSpeed, true);

    float noise = u_Texture2.Sample(u_Texture2_sampler, input.v_TexCoord2 * 0.5).a;

    float4 colorA = u_Texture0.SampleBias(u_Texture0_sampler, uvwA.xy * 0.5, c_ColorLODBias) * uvwA.z * noise;
    float4 colorB = u_Texture0.SampleBias(u_Texture0_sampler, uvwB.xy * 0.5, c_ColorLODBias) * uvwB.z * noise;

    float4 colorStatic = u_Texture0.SampleBias(u_Texture0_sampler, input.v_TexCoord0 * 0.35, c_ColorLODBias) * lightmap * 1.25;
    float4 colorFlow = (colorA + colorB) * lightmap * 2.25;

    float4 color = (colorStatic * c_StaticWeight) + (colorFlow * (1.0 - c_StaticWeight));

    float4 o_Color = ffog(color * 0.85, input.fogViewSpace);
    fdebugcolor(o_Color, isFrontFace);
    return o_Color;
}
