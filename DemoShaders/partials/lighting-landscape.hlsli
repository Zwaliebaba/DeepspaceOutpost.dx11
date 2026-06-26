#ifndef LIGHTING_LANDSCAPE_HLSLI
#define LIGHTING_LANDSCAPE_HLSLI

#include "common.hlsli"
#include "camera.hlsli"
#include "matrices.hlsli"

//
// Landscape lighting (directional only). Like lighting.hlsli but lit with
// per-material constant tables and an explicit material index. Self-contained:
// a landscape shader includes this, not lighting.hlsli.
//
// Cross-stage contract (when MAX_LIGHTS > 0), carried in the shader's I/O structs:
//   #if defined(NORMALS_PROVIDED)
//       float3 fragNormal : TEXCOORD<n>;
//   #else
//       float3 eyePos     : TEXCOORD<n>;            // v_EyeCoordinatePos
//   #endif
//       float3 halfway[MAX_LIGHTS] : TEXCOORD<n+1>;
//   VS:  LightingVaryings lv = vlight(mv_pos, normal, model);
//   PS:  float3 nrm = flighting_normal_landscape(input.<normalOrEye>);
//        color = flighting(materialIdx, nrm, color, input.halfway);
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
};

static const float4 c_Specular[2] =
{
    float4(0.0, 0.0, 0.0, 0.0),
    float4(0.5, 0.5, 0.5, 1.0)
};
static const float4 c_Ambient[2] =
{
    float4(1.0, 1.0, 1.0, 0.0),
    float4(1.2, 1.2, 1.2, 0.0)
};
static const float4 c_Diffuse[2] =
{
    float4(1.0, 1.0, 1.0, 0.0),
    float4(1.2, 1.2, 1.2, 0.0)
};
static const float c_Shininess[2] = { 100.0, 40.0 };

float3x3 inverse3x3_ls(float3x3 m)
{
    float3 r0 = m[0], r1 = m[1], r2 = m[2];
    float3 c0 = cross(r1, r2);
    float3 c1 = cross(r2, r0);
    float3 c2 = cross(r0, r1);
    float invDet = 1.0 / dot(r0, c0);
    return transpose(float3x3(c0 * invDet, c1 * invDet, c2 * invDet));
}

struct LightingVaryings
{
    float3 normalOrEye;
    float3 halfway[MAX_LIGHTS];
};

float3 vlightdir(float4 light, float4 mv_pos) { return light.xyz; }

float3 vhalfway(float4 light, float4 mv_pos)
{
    return normalize(vlightdir(light, mv_pos) - normalize(mv_pos.xyz));
}

#if defined(NORMALS_PROVIDED)
float3 vnormal(float3 normal, float3x3 model)
{
    float3x3 viewRot = (float3x3)u_ViewMatrix;
    float3x3 A = mul(model, viewRot);
    float3x3 N = transpose(inverse3x3_ls(A));
    return normalize(mul(normal, N));
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

// No front-face flip in the landscape path (matches the GLSL source).
float3 flighting_normal_landscape(float3 normalOrEye)
{
    if (u_LightingEnable != 0)
    {
#if defined(NORMALS_PROVIDED)
        return normalize(normalOrEye);
#else
        return flighting_face_normal(normalOrEye);
#endif
    }
    return float3(0.0, 1.0, 0.0);
}

float4 flight_single(int lightIdx, int materialIdx, float4 color, float3 normal, float3 halfwayIn)
{
    float3 halfway = normalize(halfwayIn);
    float3 position = flightvec(lightIdx);

    float nDotVP = max(0.0, dot(normal, position));
    float nDotHV = max(0.0, dot(normal, halfway));
    float pf = pow(nDotHV, c_Shininess[materialIdx]);

    float4 ambient  = u_LightAmbient[lightIdx]  * c_Ambient[materialIdx];
    float4 diffuse  = u_LightDiffuse[lightIdx]  * c_Diffuse[materialIdx] * nDotVP;
    float4 specular = u_LightSpecular[lightIdx] * c_Specular[materialIdx] * pf;

    return ambient * color + diffuse * color + specular;
}

float4 flighting(int materialIdx, float3 normal, float4 color, float3 halfway[MAX_LIGHTS])
{
    if (u_LightingEnable != 0)
    {
        float4 result = float4(0.0, 0.0, 0.0, 0.0);
        if (u_LightEnable[0] != 0) result += flight_single(0, materialIdx, color, normal, halfway[0]);
#if MAX_LIGHTS > 1
        if (u_LightEnable[1] != 0) result += flight_single(1, materialIdx, color, normal, halfway[1]);
#endif
#if MAX_LIGHTS > 2
        if (u_LightEnable[2] != 0) result += flight_single(2, materialIdx, color, normal, halfway[2]);
#endif
#if MAX_LIGHTS > 3
        if (u_LightEnable[3] != 0) result += flight_single(3, materialIdx, color, normal, halfway[3]);
#endif
        return float4(result.rgb, color.a);
    }
    return color;
}
#else  // MAX_LIGHTS == 0
struct LightingVaryings { float3 normalOrEye; };
LightingVaryings vlight(float4 mv_pos, float3 normal, float3x3 model)
{
    LightingVaryings o; o.normalOrEye = float3(0.0, 1.0, 0.0); return o;
}
float3 flighting_normal_landscape(float3 normalOrEye) { return float3(0.0, 1.0, 0.0); }
float4 flighting(int materialIdx, float3 normal, float4 color) { return color; }
#endif

#endif // LIGHTING_LANDSCAPE_HLSLI
