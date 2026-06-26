#ifndef LIGHTING_HLSLI
#define LIGHTING_HLSLI

#include "common.hlsli"
#include "camera.hlsli"
#include "matrices.hlsli"

//
// Lighting (directional lights only -- ASSUME_DIRECTIONAL_LIGHTS).
//
// Cross-stage data. The original GLSL declared these varyings; in HLSL the
// SHADER owns them in its I/O structs and passes them to the helpers:
//
//   VSOutput (and matching PSInput) must contain, when MAX_LIGHTS > 0:
//   #if defined(NORMALS_PROVIDED)
//       float3 fragNormal : TEXCOORD<n>;            // v_FragNormal
//   #else
//       float3 eyePos     : TEXCOORD<n>;            // v_EyeCoordinatePos
//   #endif
//       float3 halfway[MAX_LIGHTS] : TEXCOORD<n+1>; // v_LightHalfway[]
//
//   VS:   LightingVaryings lv = vlight(mv_pos, normal, model);
//         o.fragNormal/eyePos = lv.normalOrEye; o.halfway = lv.halfway;
//   PS:   color = flighting(color, input.<normalOrEye>, input.halfway, isFrontFace);
//

#if MAX_LIGHTS > 0
cbuffer Lighting : register(b6)
{
    float4 u_LightPos[MAX_LIGHTS];
    float4 u_LightDiffuse[MAX_LIGHTS];
    float4 u_LightAmbient[MAX_LIGHTS];
    float4 u_LightSpecular[MAX_LIGHTS];
    int    u_LightEnable[MAX_LIGHTS];   // bool[] -> int[]; 16-byte stride per element
    int    u_LightingEnable;
    float  u_MaterialShininess;
};
#endif

// 3x3 matrix inverse (HLSL has no inverse()).
float3x3 inverse3x3(float3x3 m)
{
    float3 r0 = m[0], r1 = m[1], r2 = m[2];
    float3 c0 = cross(r1, r2);
    float3 c1 = cross(r2, r0);
    float3 c2 = cross(r0, r1);
    float det = dot(r0, c0);
    float invDet = 1.0 / det;
    // rows of the inverse are the cofactor columns scaled by 1/det
    return transpose(float3x3(c0 * invDet, c1 * invDet, c2 * invDet));
}

#if MAX_LIGHTS > 0

struct LightingVaryings
{
    float3 normalOrEye;            // fragNormal (NORMALS_PROVIDED) or eyePos
    float3 halfway[MAX_LIGHTS];
};

// directional light: position field holds the direction directly
float3 vlightdir(float4 light, float4 mv_pos) { return light.xyz; }

float3 vhalfway(float4 light, float4 mv_pos)
{
    return normalize(vlightdir(light, mv_pos) - normalize(mv_pos.xyz));
}

#if defined(NORMALS_PROVIDED)
// GLSL: normalize(transpose(inverse(mat3(u_ViewMatrix) * model)) * normal)
// NOTE: verify against engine layout; u_NormalMatrix (b0) is also available.
float3 vnormal(float3 normal, float3x3 model)
{
    float3x3 viewRot = (float3x3)u_ViewMatrix;
    float3x3 A = mul(model, viewRot);                 // GL: mat3(view) * model
    float3x3 N = transpose(inverse3x3(A));
    return normalize(mul(normal, N));                 // GL: N * normal
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
        o.halfway[i] = float3(0.0, 0.0, 0.0);

    if (u_LightEnable[0] != 0) o.halfway[0] = vhalfway(u_LightPos[0], mv_pos);
#if MAX_LIGHTS > 1
    if (u_LightEnable[1] != 0) o.halfway[1] = vhalfway(u_LightPos[1], mv_pos);
#endif
#if MAX_LIGHTS > 2
    if (u_LightEnable[2] != 0) o.halfway[2] = vhalfway(u_LightPos[2], mv_pos);
#endif
#if MAX_LIGHTS > 3
    if (u_LightEnable[3] != 0) o.halfway[3] = vhalfway(u_LightPos[3], mv_pos);
#endif
    return o;
}

// ---- Fragment stage ----

#if !defined(NORMALS_PROVIDED)
float3 flighting_face_normal(float3 eyePos)
{
    float3 dy = ddy(eyePos);
    float3 dx = ddx(eyePos);
#if FLIP_Y_WORLD
    float3 faceNorm = cross(dy, dx);
#else
    float3 faceNorm = cross(dx, dy);
#endif
    return normalize(faceNorm);
}
#endif

float3 flightvec(int index) { return u_LightPos[index].xyz; }

float3 flighting_normal_base(float3 normalOrEye)
{
#if defined(NORMALS_PROVIDED)
    return normalize(normalOrEye);
#else
    return flighting_face_normal(normalOrEye);
#endif
}

float3 flighting_normal(float3 normalOrEye, bool isFrontFace)
{
    if (u_LightingEnable != 0)
        return flighting_normal_base(normalOrEye) * (isFrontFace ? 1.0 : -1.0);
    else
        return float3(0.0, 1.0, 0.0);
}

float4 flight_single(int index, float4 color, float3 normal, float3 halfwayIn)
{
    float3 halfway = normalize(halfwayIn);
    float3 position = flightvec(index);

    float nDotVP = max(0.0, dot(normal, position));
    float nDotHV = max(0.0, dot(normal, halfway));
    float pf = pow(nDotHV, u_MaterialShininess);

    float4 ambient  = u_LightAmbient[index];
    float4 diffuse  = u_LightDiffuse[index] * nDotVP;
    float4 specular = u_LightSpecular[index] * pf;

    return ambient * color + diffuse * color + specular;
}

float4 flighting(float4 color, float3 normalOrEye, float3 halfway[MAX_LIGHTS], bool isFrontFace)
{
    if (u_LightingEnable != 0)
    {
        float3 normal = flighting_normal(normalOrEye, isFrontFace);
        float4 result = float4(0.0, 0.0, 0.0, 0.0);
        if (u_LightEnable[0] != 0) result += flight_single(0, color, normal, halfway[0]);
#if MAX_LIGHTS > 1
        if (u_LightEnable[1] != 0) result += flight_single(1, color, normal, halfway[1]);
#endif
#if MAX_LIGHTS > 2
        if (u_LightEnable[2] != 0) result += flight_single(2, color, normal, halfway[2]);
#endif
#if MAX_LIGHTS > 3
        if (u_LightEnable[3] != 0) result += flight_single(3, color, normal, halfway[3]);
#endif
        return float4(result.rgb, color.a);
    }
    else
    {
        return color;
    }
}
#else  // MAX_LIGHTS == 0
struct LightingVaryings { float3 normalOrEye; };
LightingVaryings vlight(float4 mv_pos, float3 normal, float3x3 model)
{
    LightingVaryings o; o.normalOrEye = float3(0.0, 1.0, 0.0); return o;
}
float3 flighting_normal(float3 normalOrEye, bool isFrontFace) { return float3(0.0, 1.0, 0.0); }
#endif

#endif // LIGHTING_HLSLI
