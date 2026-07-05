// Separable Gaussian blur (H4 emissive glow over the solid scene). One axis per pass;
// the direction is a texel step in u_Dir. Weights are the normalised radius-4 kernel that
// LineExpand.h GaussianKernel(4) produces (baked as literals so the shader needs no
// weight array). Sampled linear-clamp; the caller ping-pongs H then V.

Texture2D    u_Src : register(t0);
SamplerState u_Smp : register(s0);

cbuffer BlurCb : register(b0)
{
    float4 u_Dir;   // xy = one texel step along the blur axis (1/width, 0) or (0, 1/height)
};

struct PostVSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

float4 PSMain(PostVSOut i) : SV_Target
{
    // GaussianKernel(4): centre + 4 symmetric taps, normalised.
    const float w0 = 0.2042;
    const float w1 = 0.1802;
    const float w2 = 0.1238;
    const float w3 = 0.0663;
    const float w4 = 0.0276;
    float2 d = u_Dir.xy;

    float3 c = u_Src.Sample(u_Smp, i.uv).rgb * w0;
    c += u_Src.Sample(u_Smp, i.uv + d * 1.0).rgb * w1;
    c += u_Src.Sample(u_Smp, i.uv - d * 1.0).rgb * w1;
    c += u_Src.Sample(u_Smp, i.uv + d * 2.0).rgb * w2;
    c += u_Src.Sample(u_Smp, i.uv - d * 2.0).rgb * w2;
    c += u_Src.Sample(u_Smp, i.uv + d * 3.0).rgb * w3;
    c += u_Src.Sample(u_Smp, i.uv - d * 3.0).rgb * w3;
    c += u_Src.Sample(u_Smp, i.uv + d * 4.0).rgb * w4;
    c += u_Src.Sample(u_Smp, i.uv - d * 4.0).rgb * w4;
    return float4(c, 1.0);
}
