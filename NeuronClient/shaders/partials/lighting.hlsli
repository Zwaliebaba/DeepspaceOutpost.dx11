#ifndef LIGHTING_HLSLI
#define LIGHTING_HLSLI

#include "common.hlsli"
#include "matrices.hlsli"

// b6 -- fixed-function-style directional lighting. The view stage produces per-light
// half-vectors and the transformed normal (or eye position) as varyings; the fragment
// stage applies ambient + diffuse + specular. Matches LightingConstants in
// NeuronClient/ConstantBuffers.h. Only present when compiled with MAX_LIGHTS > 0.

#if MAX_LIGHTS > 0
cbuffer Lighting : register(b6)
{
    float4 u_LightPos[MAX_LIGHTS];      // direction (w = 0) for directional lights
    float4 u_LightDiffuse[MAX_LIGHTS];
    float4 u_LightAmbient[MAX_LIGHTS];
    float4 u_LightSpecular[MAX_LIGHTS];
    int    u_LightEnable[MAX_LIGHTS];   // bool[] -> int[]; 16-byte stride per element
    int    u_LightingEnable;
    float  u_MaterialShininess;
    int    u_SpecularEnable;            // D3DRS_SPECULARENABLE analog; 0 = off (fixed-function default)
};

struct LightingVaryings
{
    float3 normalOrEye;            // transformed normal (NORMALS_PROVIDED) or eye position
    float3 halfway[MAX_LIGHTS];
};

// HLSL has no inverse(); 3x3 inverse via cofactors for the normal matrix.
float3x3 inverse3x3(float3x3 m)
{
    float3 r0 = m[0], r1 = m[1], r2 = m[2];
    float3 c0 = cross(r1, r2);
    float3 c1 = cross(r2, r0);
    float3 c2 = cross(r0, r1);
    float invDet = 1.0 / dot(r0, c0);
    return transpose(float3x3(c0 * invDet, c1 * invDet, c2 * invDet));
}

float3 vhalfway(float4 light, float4 mv_pos)
{
    return normalize(light.xyz - normalize(mv_pos.xyz));
}

#if defined(NORMALS_PROVIDED)
float3 vnormal(float3 normal, float3x3 model)
{
    float3x3 viewRot = (float3x3)u_View;
    float3x3 n = transpose(inverse3x3(mul(model, viewRot)));
    return normalize(mul(normal, n));
}
#endif

LightingVaryings vlight(float4 mv_pos, float3 normal, float3x3 model)
{
    LightingVaryings o;
#if defined(NORMALS_PROVIDED)
    o.normalOrEye = vnormal(normal, model);
#else
    o.normalOrEye = mv_pos.xyz;
#endif
    [unroll] for (int i = 0; i < MAX_LIGHTS; ++i)
        o.halfway[i] = (u_LightEnable[i] != 0) ? vhalfway(u_LightPos[i], mv_pos) : float3(0, 0, 0);
    return o;
}

#if defined(NORMALS_PROVIDED)
float3 flighting_normal(float3 normalOrEye, bool isFrontFace)
{
    return normalize(normalOrEye) * (isFrontFace ? 1.0 : -1.0);
}
#else
float3 flighting_normal(float3 eyePos, bool isFrontFace)
{
    float3 faceNorm = normalize(cross(ddx(eyePos), ddy(eyePos)));
    return faceNorm * (isFrontFace ? 1.0 : -1.0);
}
#endif

float4 flight_single(int i, float4 color, float3 normal, float3 halfwayIn)
{
    float3 halfway = normalize(halfwayIn);
    float nDotVP = max(0.0, dot(normal, u_LightPos[i].xyz));
    float nDotHV = max(0.0, dot(normal, halfway));
    float pf = (nDotHV > 0.0) ? pow(nDotHV, u_MaterialShininess) : 0.0;

    float4 ambient = u_LightAmbient[i];
    float4 diffuse = u_LightDiffuse[i] * nDotVP;
    // Specular highlights are gated like D3D9's D3DRS_SPECULARENABLE: off by default, so
    // surfaces that never enabled it (e.g. the global-world globe) are diffuse-lit only.
    float4 specular = (u_SpecularEnable != 0) ? u_LightSpecular[i] * pf : float4(0, 0, 0, 0);
    return ambient * color + diffuse * color + specular;
}

float4 flighting(float4 color, float3 normalOrEye, float3 halfway[MAX_LIGHTS], bool isFrontFace)
{
    if (u_LightingEnable == 0)
        return color;

    float3 normal = flighting_normal(normalOrEye, isFrontFace);
    float4 result = float4(0, 0, 0, 0);
    [unroll] for (int i = 0; i < MAX_LIGHTS; ++i)
        if (u_LightEnable[i] != 0)
            result += flight_single(i, color, normal, halfway[i]);

    return float4(result.rgb, color.a);
}
#endif // MAX_LIGHTS > 0

#endif // LIGHTING_HLSLI
