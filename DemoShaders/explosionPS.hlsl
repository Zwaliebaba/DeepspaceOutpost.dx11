#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/lighting.hlsli"
#include "partials/fog.hlsli"

Texture2D    u_Texture0         : register(t0);
SamplerState u_Texture0_sampler : register(s0);

struct PSInput
{
    float4 pos          : SV_Position;
    float4 color        : TEXCOORD0;
    float2 uv           : TEXCOORD1;
    float4 fogViewSpace : TEXCOORD2;
#if MAX_LIGHTS > 0
#if defined(NORMALS_PROVIDED)
    float3 fragNormal   : TEXCOORD3;
#else
    float3 eyePos       : TEXCOORD3;
#endif
    float3 halfway[MAX_LIGHTS] : TEXCOORD4;
#endif
};

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    float4 color = input.color;
    float4 texColor = u_Texture0.Sample(u_Texture0_sampler, input.uv);
    color = float4(color.rgb * (1.0 - texColor.a) + texColor.rgb * texColor.a, color.a);

#if MAX_LIGHTS > 0
#if defined(NORMALS_PROVIDED)
    color = flighting(color, input.fragNormal, input.halfway, isFrontFace);
#else
    color = flighting(color, input.eyePos, input.halfway, isFrontFace);
#endif
#endif

    color = ffog(color, input.fogViewSpace);
    return color;
}
