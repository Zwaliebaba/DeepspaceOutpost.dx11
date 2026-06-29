// Lit, coloured 3D primitives (immediate-mode triangles with per-vertex normal+colour).
// Replaces fixed-function lit geometry: Shape models and 3D colored draws. The model
// transform is baked into u_View (the immediate matrix stack), so an identity model is
// passed to the lighting helpers.
#include "partials/common.hlsli"
#include "partials/immediate-vertex.hlsli"
#include "partials/matrices.hlsli"
#include "partials/lighting.hlsli"
#include "partials/fog.hlsli"

struct VSOutput
{
    float4 pos          : SV_Position;
    float4 color        : TEXCOORD0;
    float4 fogViewSpace : TEXCOORD1;
#if MAX_LIGHTS > 0
    float3 fragNormal   : TEXCOORD2;
    float3 halfway[MAX_LIGHTS] : TEXCOORD3;
#endif
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.color = input.aColor;

    float4 viewPos = mul(float4(input.aPos, 1.0), u_View);
    o.fogViewSpace = vfog(viewPos);

#if MAX_LIGHTS > 0
    LightingVaryings lv = vlight(viewPos, input.aNormal, float3x3(1, 0, 0, 0, 1, 0, 0, 0, 1));
    o.fragNormal = lv.normalOrEye;
    o.halfway = lv.halfway;
#endif

    o.pos = mul(viewPos, u_Projection);
    return o;
}
