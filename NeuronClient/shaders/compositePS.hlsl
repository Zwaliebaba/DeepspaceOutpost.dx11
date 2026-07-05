// Composite (H4 post chain): add the sharp solid scene and its blurred glow onto the
// back buffer. u_Sharp is the original scene target (dust background + solid ships),
// u_Glow the blurred one; the sum is the glowing solid-mesh look.

Texture2D    u_Sharp : register(t0);
Texture2D    u_Glow  : register(t1);
SamplerState u_Smp   : register(s0);

cbuffer CompositeCb : register(b0)
{
    float4 u_Params;   // x = glow intensity
};

struct PostVSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

float4 PSMain(PostVSOut i) : SV_Target
{
    float3 sharp = u_Sharp.Sample(u_Smp, i.uv).rgb;
    float3 glow  = u_Glow.Sample(u_Smp, i.uv).rgb * u_Params.x;
    return float4(sharp + glow, 1.0);
}
