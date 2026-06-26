#ifndef ALPHATEST_HLSLI
#define ALPHATEST_HLSLI

#include "common.hlsli"

//
// Alpha testing
//
struct AlphaTest
{
    int function;
    float clampValue;   // 'clamp' is a reserved intrinsic name in HLSL
};

cbuffer AlphaTestParams : register(b7)
{
    AlphaTest u_AlphaTest;
};

static const int ALPHA_TEST_NEVER = 0;
static const int ALPHA_TEST_LESS = 1;
static const int ALPHA_TEST_EQUAL = 2;
static const int ALPHA_TEST_LESS_EQUAL = 3;
static const int ALPHA_TEST_GREATER = 4;
static const int ALPHA_TEST_NOT_EQUAL = 5;
static const int ALPHA_TEST_GREATER_EQUAL = 6;
static const int ALPHA_TEST_ALWAYS = 7;

float4 falphatest(float4 color)
{
#ifdef ENABLE_ALPHA_TEST
    if (u_AlphaTest.function == ALPHA_TEST_ALWAYS)
        return color;

    if (u_AlphaTest.function == ALPHA_TEST_NEVER)
        discard;

    if (u_AlphaTest.function == ALPHA_TEST_GREATER)
    {
        if (color.a > u_AlphaTest.clampValue) return color; else discard;
    }
    if (u_AlphaTest.function == ALPHA_TEST_LESS)
    {
        if (color.a < u_AlphaTest.clampValue) return color; else discard;
    }
    if (u_AlphaTest.function == ALPHA_TEST_LESS_EQUAL)
    {
        if (color.a <= u_AlphaTest.clampValue) return color; else discard;
    }
    if (u_AlphaTest.function == ALPHA_TEST_GREATER_EQUAL)
    {
        if (color.a >= u_AlphaTest.clampValue) return color; else discard;
    }
    if (u_AlphaTest.function == ALPHA_TEST_NOT_EQUAL)
    {
        if (color.a != u_AlphaTest.clampValue) return color; else discard;
    }
    if (u_AlphaTest.function == ALPHA_TEST_EQUAL)
    {
        if (color.a == u_AlphaTest.clampValue) return color; else discard;
    }

    return color;
#else
    return color;
#endif
}

#endif // ALPHATEST_HLSLI
