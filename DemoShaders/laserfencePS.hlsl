#include "partials/common.hlsli"
#include "partials/clipping.hlsli"
#include "partials/fog.hlsli"
#include "partials/matrices.hlsli"
#include "partials/debug.hlsli"

Texture2D    u_Texture0         : register(t0);
SamplerState u_Texture0_sampler : register(s0);
Texture2D    u_Texture1         : register(t1);
SamplerState u_Texture1_sampler : register(s1);

static const float c_FenceLODBias = 1.5;
static const float c_Fence2LODBias = 2.0;

struct PSInput
{
    float4 pos          : SV_Position;
    float2 v_TexCoord0  : TEXCOORD0;
    float2 v_TexCoord1  : TEXCOORD1;
    float2 v_TexCoordBurn : TEXCOORD2;
    float4 v_Color      : TEXCOORD3;
    float4 fogViewSpace : TEXCOORD4;
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
#if !FEATURE_NATIVE_CLIPPING
    float2 clipDist     : TEXCOORD5;
#endif
#endif
};

float4 sampleFence2(float2 coords)
{
    // This texture has an ugly seam, average out nearby samples
    float4 sample0 = u_Texture1.SampleBias(u_Texture1_sampler, coords, c_Fence2LODBias);
    float4 sample1 = u_Texture1.SampleBias(u_Texture1_sampler, coords - float2(0.03, 0.0), c_Fence2LODBias);
    float4 sample2 = u_Texture1.SampleBias(u_Texture1_sampler, coords + float2(0.03, 0.0), c_Fence2LODBias);
    return (sample0 + sample1 + sample2) * 0.333;
}

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1) && !FEATURE_NATIVE_CLIPPING
    fclipping(input.clipDist);
#endif
    //
    // fence1 = X-scrolling noise texture
    // fence2 = X-scrolling gradient texture
    // fence3 = static gradient texture (brightness near pylons)
    //
    float4 color;
    float4 fence1 = u_Texture0.SampleBias(u_Texture0_sampler, input.v_TexCoord0, c_FenceLODBias);
    float4 fence2 = sampleFence2(input.v_TexCoord1);
    float4 fence3 = sampleFence2(input.v_TexCoordBurn);

    // Textures are grayscale, with full opacity. Set the alpha channel to be
    // equal to any of the other channels to get transparency in the dark
    // areas.
    fence1.a = fence1.r;
    fence2.a = fence2.r;
    fence3.a = fence3.r;

    // Dampen the brightness near the pylon edges
    fence3 *= 0.5;

    color = float4(fence3.rgb * input.v_Color.rgb * 1.15, min(fence3.a, input.v_Color.a));

    float4 fenceMotion;
    fenceMotion  = fence1;
    fenceMotion *= fence2;

    fenceMotion.a = (1.0 - color.a) * 0.5;

    color.rgb += fenceMotion.rgb * fenceMotion.a;

    color.a = input.v_Color.a * 0.85;

#if USE_HDR
    color.rgb *= 1.25;
#else
    color = clamp(color, 0.0, 1.0);
#endif

    color = ffog(color, input.fogViewSpace);
    fdebugcolor(color, isFrontFace);
    return color;
}
