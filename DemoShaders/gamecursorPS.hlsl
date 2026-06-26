#include "partials/common.hlsli"
#include "partials/fog.hlsli"
#include "partials/matrices.hlsli"
#include "partials/hdr.hlsli"
#include "partials/debug.hlsli"

Texture2DArray u_Texture0         : register(t0);
SamplerState   u_Texture0_sampler : register(s0);

struct PSInput
{
    float4 pos          : SV_Position;
    float3 v_TexCoord   : TEXCOORD0;
    float4 v_Color      : TEXCOORD1;
    float4 fogViewSpace : TEXCOORD2;
};

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    float4 color = input.v_Color;
    color = color * u_Texture0.Sample(u_Texture0_sampler, input.v_TexCoord);

    // If the incoming alpha is larger than the color value, then this is the
    // blurred border image, and we want to make it stand out more strongly
    // than the gaussian blur actually did.
    if (input.v_Color.a > input.v_Color.r)
        color.a = clamp(color.a * 3.0, 0.0, 1.0);

#if !USE_HDR
    // Increase the cursor brightness
    color.rgb *= 1.15;
#else
    color = hdrcolor(color);
#endif

    color = ffog(color, input.fogViewSpace);
    fdebugcolor(color, isFrontFace);
    return color;
}
