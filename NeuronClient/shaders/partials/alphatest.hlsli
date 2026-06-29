#ifndef ALPHATEST_HLSLI
#define ALPHATEST_HLSLI

#include "common.hlsli"

// b7 -- fixed-function alpha test. Flattened to match AlphaTestConstants.
cbuffer AlphaTest : register(b7)
{
    int u_AlphaFunc;
    float u_AlphaRef;
    int2 _alphaTestPad;
};

#define ALPHA_TEST_NEVER 0
#define ALPHA_TEST_LESS 1
#define ALPHA_TEST_EQUAL 2
#define ALPHA_TEST_LESS_EQUAL 3
#define ALPHA_TEST_GREATER 4
#define ALPHA_TEST_NOT_EQUAL 5
#define ALPHA_TEST_GREATER_EQUAL 6
#define ALPHA_TEST_ALWAYS 7

// Discards the fragment when the alpha comparison fails. A no-op unless the shader
// is compiled with ENABLE_ALPHA_TEST.
void falphatest(float4 color)
{
#if ENABLE_ALPHA_TEST
    float a = color.a;
    float r = u_AlphaRef;
    bool pass =
        (u_AlphaFunc == ALPHA_TEST_ALWAYS) ||
        (u_AlphaFunc == ALPHA_TEST_LESS && a < r) ||
        (u_AlphaFunc == ALPHA_TEST_LESS_EQUAL && a <= r) ||
        (u_AlphaFunc == ALPHA_TEST_GREATER && a > r) ||
        (u_AlphaFunc == ALPHA_TEST_GREATER_EQUAL && a >= r) ||
        (u_AlphaFunc == ALPHA_TEST_EQUAL && a == r) ||
        (u_AlphaFunc == ALPHA_TEST_NOT_EQUAL && a != r);
    if (!pass)
        discard;
#endif
}

#endif // ALPHATEST_HLSLI
