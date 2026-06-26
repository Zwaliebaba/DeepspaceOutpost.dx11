#ifndef FXAA_HLSLI
#define FXAA_HLSLI

#include "common.hlsli"

/*============================================================================


                    NVIDIA FXAA 3.11 by TIMOTHY LOTTES


------------------------------------------------------------------------------
COPYRIGHT (C) 2010, 2011 NVIDIA CORPORATION. ALL RIGHTS RESERVED.
------------------------------------------------------------------------------
TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, THIS SOFTWARE IS PROVIDED
*AS IS* AND NVIDIA AND ITS SUPPLIERS DISCLAIM ALL WARRANTIES, EITHER EXPRESS
OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL NVIDIA
OR ITS SUPPLIERS BE LIABLE FOR ANY SPECIAL, INCIDENTAL, INDIRECT, OR
CONSEQUENTIAL DAMAGES WHATSOEVER (INCLUDING, WITHOUT LIMITATION, DAMAGES FOR
LOSS OF BUSINESS PROFITS, BUSINESS INTERRUPTION, LOSS OF BUSINESS INFORMATION,
OR ANY OTHER PECUNIARY LOSS) ARISING OUT OF THE USE OF OR INABILITY TO USE
THIS SOFTWARE, EVEN IF NVIDIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
DAMAGES.
*/

//==============================================================================
// HLSL (DirectX 11 / Shader Model 5.0) port of the engine's FXAA partial.
//
// The GLSL source is a flattened single-path FXAA Quality build (the
// FXAA_PC / FXAA_HLSL_5 / FXAA_GLSL_* macro branches of the original NVIDIA
// header were already resolved down to one path). This file preserves that
// path's math exactly; only the texture-access idioms change for HLSL:
//
//   GLSL sampler2D                       -> FxaaTex { Texture2D + SamplerState }
//   textureLod(t, p, 0.0)                -> t.tex.SampleLevel(t.smp, p, 0.0)
//   textureLod(t, p + off*rcp, 0.0)      -> same with explicit offset (no
//                                           FXAA_FAST_PIXEL_OFFSET path)
//
// The quality preset is selected at compile time via FXAA_QUALITY_PRESET and
// defaults to 12 (medium-dither, 5 search steps) which matches a typical
// "default preset" build. Override with -D FXAA_QUALITY_PRESET=NN.
//==============================================================================

#ifndef FXAA_QUALITY_PRESET
#define FXAA_QUALITY_PRESET 12
#endif

/*============================================================================
                     FXAA QUALITY - MEDIUM DITHER PRESETS
============================================================================*/
#if (FXAA_QUALITY_PRESET == 10)
#define FXAA_QUALITY_PS 3
static const float FXAA_QUALITY_P0 = 1.5;
static const float FXAA_QUALITY_P1 = 3.0;
static const float FXAA_QUALITY_P2 = 12.0;
#endif

#if (FXAA_QUALITY_PRESET == 11)
#define FXAA_QUALITY_PS 4
static const float FXAA_QUALITY_P0 = 1.0;
static const float FXAA_QUALITY_P1 = 1.5;
static const float FXAA_QUALITY_P2 = 3.0;
static const float FXAA_QUALITY_P3 = 12.0;
#endif

#if (FXAA_QUALITY_PRESET == 12)
#define FXAA_QUALITY_PS 5
static const float FXAA_QUALITY_P0 = 1.0;
static const float FXAA_QUALITY_P1 = 1.5;
static const float FXAA_QUALITY_P2 = 2.0;
static const float FXAA_QUALITY_P3 = 4.0;
static const float FXAA_QUALITY_P4 = 12.0;
#endif

#if (FXAA_QUALITY_PRESET == 13)
#define FXAA_QUALITY_PS 6
static const float FXAA_QUALITY_P0 = 1.0;
static const float FXAA_QUALITY_P1 = 1.5;
static const float FXAA_QUALITY_P2 = 2.0;
static const float FXAA_QUALITY_P3 = 2.0;
static const float FXAA_QUALITY_P4 = 4.0;
static const float FXAA_QUALITY_P5 = 12.0;
#endif

#if (FXAA_QUALITY_PRESET == 14)
#define FXAA_QUALITY_PS 7
static const float FXAA_QUALITY_P0 = 1.0;
static const float FXAA_QUALITY_P1 = 1.5;
static const float FXAA_QUALITY_P2 = 2.0;
static const float FXAA_QUALITY_P3 = 2.0;
static const float FXAA_QUALITY_P4 = 2.0;
static const float FXAA_QUALITY_P5 = 4.0;
static const float FXAA_QUALITY_P6 = 12.0;
#endif

#if (FXAA_QUALITY_PRESET == 15)
#define FXAA_QUALITY_PS 8
static const float FXAA_QUALITY_P0 = 1.0;
static const float FXAA_QUALITY_P1 = 1.5;
static const float FXAA_QUALITY_P2 = 2.0;
static const float FXAA_QUALITY_P3 = 2.0;
static const float FXAA_QUALITY_P4 = 2.0;
static const float FXAA_QUALITY_P5 = 2.0;
static const float FXAA_QUALITY_P6 = 4.0;
static const float FXAA_QUALITY_P7 = 12.0;
#endif

/*============================================================================
                     FXAA QUALITY - LOW DITHER PRESETS
============================================================================*/
#if (FXAA_QUALITY_PRESET == 20)
#define FXAA_QUALITY_PS 3
static const float FXAA_QUALITY_P0 = 1.5;
static const float FXAA_QUALITY_P1 = 2.0;
static const float FXAA_QUALITY_P2 = 8.0;
#endif

#if (FXAA_QUALITY_PRESET == 21)
#define FXAA_QUALITY_PS 4
static const float FXAA_QUALITY_P0 = 1.0;
static const float FXAA_QUALITY_P1 = 1.5;
static const float FXAA_QUALITY_P2 = 2.0;
static const float FXAA_QUALITY_P3 = 8.0;
#endif

#if (FXAA_QUALITY_PRESET == 22)
#define FXAA_QUALITY_PS 5
static const float FXAA_QUALITY_P0 = 1.0;
static const float FXAA_QUALITY_P1 = 1.5;
static const float FXAA_QUALITY_P2 = 2.0;
static const float FXAA_QUALITY_P3 = 2.0;
static const float FXAA_QUALITY_P4 = 8.0;
#endif

#if (FXAA_QUALITY_PRESET == 23)
#define FXAA_QUALITY_PS 6
static const float FXAA_QUALITY_P0 = 1.0;
static const float FXAA_QUALITY_P1 = 1.5;
static const float FXAA_QUALITY_P2 = 2.0;
static const float FXAA_QUALITY_P3 = 2.0;
static const float FXAA_QUALITY_P4 = 2.0;
static const float FXAA_QUALITY_P5 = 8.0;
#endif

#if (FXAA_QUALITY_PRESET == 24)
#define FXAA_QUALITY_PS 7
static const float FXAA_QUALITY_P0 = 1.0;
static const float FXAA_QUALITY_P1 = 1.5;
static const float FXAA_QUALITY_P2 = 2.0;
static const float FXAA_QUALITY_P3 = 2.0;
static const float FXAA_QUALITY_P4 = 2.0;
static const float FXAA_QUALITY_P5 = 3.0;
static const float FXAA_QUALITY_P6 = 8.0;
#endif

#if (FXAA_QUALITY_PRESET == 25)
#define FXAA_QUALITY_PS 8
static const float FXAA_QUALITY_P0 = 1.0;
static const float FXAA_QUALITY_P1 = 1.5;
static const float FXAA_QUALITY_P2 = 2.0;
static const float FXAA_QUALITY_P3 = 2.0;
static const float FXAA_QUALITY_P4 = 2.0;
static const float FXAA_QUALITY_P5 = 2.0;
static const float FXAA_QUALITY_P6 = 4.0;
static const float FXAA_QUALITY_P7 = 8.0;
#endif

#if (FXAA_QUALITY_PRESET == 26)
#define FXAA_QUALITY_PS 9
static const float FXAA_QUALITY_P0 = 1.0;
static const float FXAA_QUALITY_P1 = 1.5;
static const float FXAA_QUALITY_P2 = 2.0;
static const float FXAA_QUALITY_P3 = 2.0;
static const float FXAA_QUALITY_P4 = 2.0;
static const float FXAA_QUALITY_P5 = 2.0;
static const float FXAA_QUALITY_P6 = 2.0;
static const float FXAA_QUALITY_P7 = 4.0;
static const float FXAA_QUALITY_P8 = 8.0;
#endif

#if (FXAA_QUALITY_PRESET == 27)
#define FXAA_QUALITY_PS 10
static const float FXAA_QUALITY_P0 = 1.0;
static const float FXAA_QUALITY_P1 = 1.5;
static const float FXAA_QUALITY_P2 = 2.0;
static const float FXAA_QUALITY_P3 = 2.0;
static const float FXAA_QUALITY_P4 = 2.0;
static const float FXAA_QUALITY_P5 = 2.0;
static const float FXAA_QUALITY_P6 = 2.0;
static const float FXAA_QUALITY_P7 = 2.0;
static const float FXAA_QUALITY_P8 = 4.0;
static const float FXAA_QUALITY_P9 = 8.0;
#endif

#if (FXAA_QUALITY_PRESET == 28)
#define FXAA_QUALITY_PS 11
static const float FXAA_QUALITY_P0 = 1.0;
static const float FXAA_QUALITY_P1 = 1.5;
static const float FXAA_QUALITY_P2 = 2.0;
static const float FXAA_QUALITY_P3 = 2.0;
static const float FXAA_QUALITY_P4 = 2.0;
static const float FXAA_QUALITY_P5 = 2.0;
static const float FXAA_QUALITY_P6 = 2.0;
static const float FXAA_QUALITY_P7 = 2.0;
static const float FXAA_QUALITY_P8 = 2.0;
static const float FXAA_QUALITY_P9 = 4.0;
static const float FXAA_QUALITY_P10 = 8.0;
#endif

#if (FXAA_QUALITY_PRESET == 29)
#define FXAA_QUALITY_PS 12
static const float FXAA_QUALITY_P0 = 1.0;
static const float FXAA_QUALITY_P1 = 1.5;
static const float FXAA_QUALITY_P2 = 2.0;
static const float FXAA_QUALITY_P3 = 2.0;
static const float FXAA_QUALITY_P4 = 2.0;
static const float FXAA_QUALITY_P5 = 2.0;
static const float FXAA_QUALITY_P6 = 2.0;
static const float FXAA_QUALITY_P7 = 2.0;
static const float FXAA_QUALITY_P8 = 2.0;
static const float FXAA_QUALITY_P9 = 2.0;
static const float FXAA_QUALITY_P10 = 4.0;
static const float FXAA_QUALITY_P11 = 8.0;
#endif

/*============================================================================
                     FXAA QUALITY - EXTREME QUALITY
============================================================================*/
#if (FXAA_QUALITY_PRESET == 39)
#define FXAA_QUALITY_PS 12
static const float FXAA_QUALITY_P0 = 1.0;
static const float FXAA_QUALITY_P1 = 1.0;
static const float FXAA_QUALITY_P2 = 1.0;
static const float FXAA_QUALITY_P3 = 1.0;
static const float FXAA_QUALITY_P4 = 1.0;
static const float FXAA_QUALITY_P5 = 1.5;
static const float FXAA_QUALITY_P6 = 2.0;
static const float FXAA_QUALITY_P7 = 2.0;
static const float FXAA_QUALITY_P8 = 2.0;
static const float FXAA_QUALITY_P9 = 2.0;
static const float FXAA_QUALITY_P10 = 4.0;
static const float FXAA_QUALITY_P11 = 8.0;
#endif

//==============================================================================
// HLSL texture-access shim.
//
// GLSL passes a sampler2D by value; HLSL can't bundle a Texture2D + SamplerState
// in a single argument, so we wrap them in a small struct (the FxaaTex struct
// the original NVIDIA HLSL_4/HLSL_5 paths use). The calling shader fills it with
// its register-bound texture+sampler.
//==============================================================================
struct FxaaTex { SamplerState smp; Texture2D tex; };

float4 FxaaTexTop(FxaaTex t, float2 p) { return t.tex.SampleLevel(t.smp, p, 0.0); }
float4 FxaaTexOff(FxaaTex t, float2 p, float2 o, float2 r)
{
    return t.tex.SampleLevel(t.smp, p + o * r, 0.0);
}

float FxaaSat(float x) { return clamp(x, 0.0, 1.0); }
float FxaaLuma(float4 rgba) { return dot(rgba.rgb, float3(0.299, 0.587, 0.114)); }

float4 FxaaPixelShader(
    // {xy} = center of pixel
    float2 pos,
    // Input color texture (wrapped Texture2D + SamplerState).
    FxaaTex tex,
    // {x} = 1.0/screenWidthInPixels, {y} = 1.0/screenHeightInPixels
    float2 fxaaQualityRcpFrame,
    // FXAA_QUALITY_SUBPIX
    float fxaaQualitySubpix,
    // FXAA_QUALITY_EDGE_THRESHOLD
    float fxaaQualityEdgeThreshold,
    // FXAA_QUALITY_EDGE_THRESHOLD_MIN
    float fxaaQualityEdgeThresholdMin
) {
    /*--------------------------------------------------------------------------*/
    float2 posM;
    posM.x = pos.x;
    posM.y = pos.y;

    float4 rgbyM = FxaaTexTop(tex, posM);
    float lumaM = FxaaLuma(rgbyM);

    float lumaS = FxaaLuma(FxaaTexOff(tex, posM, float2( 0.0, 1.0), fxaaQualityRcpFrame.xy));
    float lumaE = FxaaLuma(FxaaTexOff(tex, posM, float2( 1.0, 0.0), fxaaQualityRcpFrame.xy));
    float lumaN = FxaaLuma(FxaaTexOff(tex, posM, float2( 0.0, -1.0), fxaaQualityRcpFrame.xy));
    float lumaW = FxaaLuma(FxaaTexOff(tex, posM, float2(-1.0, 0.0), fxaaQualityRcpFrame.xy));

    float maxSM = max(lumaS, lumaM);
    float minSM = min(lumaS, lumaM);
    float maxESM = max(lumaE, maxSM);
    float minESM = min(lumaE, minSM);
    float maxWN = max(lumaN, lumaW);
    float minWN = min(lumaN, lumaW);
    float rangeMax = max(maxWN, maxESM);
    float rangeMin = min(minWN, minESM);
    float rangeMaxScaled = rangeMax * fxaaQualityEdgeThreshold;
    float range = rangeMax - rangeMin;
    float rangeMaxClamped = max(fxaaQualityEdgeThresholdMin, rangeMaxScaled);
    bool earlyExit = range < rangeMaxClamped;

    if (earlyExit)
#if 0
        // For debugging edge detection
        return float4(0.0, 0.0, 0.0, 1.0);
#else
        return rgbyM;
#endif

    float lumaNW = FxaaLuma(FxaaTexOff(tex, posM, float2(-1.0, -1.0), fxaaQualityRcpFrame.xy));
    float lumaSE = FxaaLuma(FxaaTexOff(tex, posM, float2( 1.0, 1.0), fxaaQualityRcpFrame.xy));
    float lumaNE = FxaaLuma(FxaaTexOff(tex, posM, float2( 1.0, -1.0), fxaaQualityRcpFrame.xy));
    float lumaSW = FxaaLuma(FxaaTexOff(tex, posM, float2(-1.0, 1.0), fxaaQualityRcpFrame.xy));

    float lumaNS = lumaN + lumaS;
    float lumaWE = lumaW + lumaE;
    float subpixRcpRange = 1.0 / range;
    float subpixNSWE = lumaNS + lumaWE;
    float edgeHorz1 = (-2.0 * lumaM) + lumaNS;
    float edgeVert1 = (-2.0 * lumaM) + lumaWE;

    float lumaNESE = lumaNE + lumaSE;
    float lumaNWNE = lumaNW + lumaNE;
    float edgeHorz2 = (-2.0 * lumaE) + lumaNESE;
    float edgeVert2 = (-2.0 * lumaN) + lumaNWNE;

    float lumaNWSW = lumaNW + lumaSW;
    float lumaSWSE = lumaSW + lumaSE;
    float edgeHorz4 = (abs(edgeHorz1) * 2.0) + abs(edgeHorz2);
    float edgeVert4 = (abs(edgeVert1) * 2.0) + abs(edgeVert2);
    float edgeHorz3 = (-2.0 * lumaW) + lumaNWSW;
    float edgeVert3 = (-2.0 * lumaS) + lumaSWSE;
    float edgeHorz = abs(edgeHorz3) + edgeHorz4;
    float edgeVert = abs(edgeVert3) + edgeVert4;

    float subpixNWSWNESE = lumaNWSW + lumaNESE;
    float lengthSign = fxaaQualityRcpFrame.x;
    bool horzSpan = edgeHorz >= edgeVert;
    float subpixA = subpixNSWE * 2.0 + subpixNWSWNESE;

    if (!horzSpan) lumaN = lumaW;
    if (!horzSpan) lumaS = lumaE;
    if (horzSpan) lengthSign = fxaaQualityRcpFrame.y;
    float subpixB = (subpixA * (1.0 / 12.0)) - lumaM;

    float gradientN = lumaN - lumaM;
    float gradientS = lumaS - lumaM;
    float lumaNN = lumaN + lumaM;
    float lumaSS = lumaS + lumaM;
    bool pairN = abs(gradientN) >= abs(gradientS);
    float gradient = max(abs(gradientN), abs(gradientS));
    if (pairN) lengthSign = -lengthSign;
    float subpixC = FxaaSat(abs(subpixB) * subpixRcpRange);

    float2 posB = posM.xy;
    float2 offNP;
    offNP.x = (!horzSpan) ? 0.0 : fxaaQualityRcpFrame.x;
    offNP.y = (horzSpan) ? 0.0 : fxaaQualityRcpFrame.y;
    if (!horzSpan) posB.x += lengthSign * 0.5;
    if (horzSpan) posB.y += lengthSign * 0.5;

    float2 posN;
    posN.x = posB.x - offNP.x * FXAA_QUALITY_P0;
    posN.y = posB.y - offNP.y * FXAA_QUALITY_P0;
    float2 posP;
    posP.x = posB.x + offNP.x * FXAA_QUALITY_P0;
    posP.y = posB.y + offNP.y * FXAA_QUALITY_P0;
    float subpixD = ((-2.0) * subpixC) + 3.0;
    float lumaEndN = FxaaLuma(FxaaTexTop(tex, posN));
    float subpixE = subpixC * subpixC;
    float lumaEndP = FxaaLuma(FxaaTexTop(tex, posP));

    if (!pairN) lumaNN = lumaSS;
    float gradientScaled = gradient * 1.0 / 4.0;
    float lumaMM = lumaM - lumaNN * 0.5;
    float subpixF = subpixD * subpixE;
    bool lumaMLTZero = lumaMM < 0.0;

    lumaEndN -= lumaNN * 0.5;
    lumaEndP -= lumaNN * 0.5;
    bool doneN = abs(lumaEndN) >= gradientScaled;
    bool doneP = abs(lumaEndP) >= gradientScaled;
    if (!doneN) posN.x -= offNP.x * FXAA_QUALITY_P1;
    if (!doneN) posN.y -= offNP.y * FXAA_QUALITY_P1;
    bool doneNP = (!doneN) || (!doneP);
    if (!doneP) posP.x += offNP.x * FXAA_QUALITY_P1;
    if (!doneP) posP.y += offNP.y * FXAA_QUALITY_P1;

    if (doneNP) {
        if (!doneN) lumaEndN = FxaaLuma(FxaaTexTop(tex, posN.xy));
        if (!doneP) lumaEndP = FxaaLuma(FxaaTexTop(tex, posP.xy));
        if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
        if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
        doneN = abs(lumaEndN) >= gradientScaled;
        doneP = abs(lumaEndP) >= gradientScaled;
        if (!doneN) posN.x -= offNP.x * FXAA_QUALITY_P2;
        if (!doneN) posN.y -= offNP.y * FXAA_QUALITY_P2;
        doneNP = (!doneN) || (!doneP);
        if (!doneP) posP.x += offNP.x * FXAA_QUALITY_P2;
        if (!doneP) posP.y += offNP.y * FXAA_QUALITY_P2;

#if (FXAA_QUALITY_PS > 3)
        if (doneNP) {
            if (!doneN) lumaEndN = FxaaLuma(FxaaTexTop(tex, posN.xy));
            if (!doneP) lumaEndP = FxaaLuma(FxaaTexTop(tex, posP.xy));
            if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
            if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
            doneN = abs(lumaEndN) >= gradientScaled;
            doneP = abs(lumaEndP) >= gradientScaled;
            if (!doneN) posN.x -= offNP.x * FXAA_QUALITY_P3;
            if (!doneN) posN.y -= offNP.y * FXAA_QUALITY_P3;
            doneNP = (!doneN) || (!doneP);
            if (!doneP) posP.x += offNP.x * FXAA_QUALITY_P3;
            if (!doneP) posP.y += offNP.y * FXAA_QUALITY_P3;

#if (FXAA_QUALITY_PS > 4)
            if (doneNP) {
                if (!doneN) lumaEndN = FxaaLuma(FxaaTexTop(tex, posN.xy));
                if (!doneP) lumaEndP = FxaaLuma(FxaaTexTop(tex, posP.xy));
                if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
                if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
                doneN = abs(lumaEndN) >= gradientScaled;
                doneP = abs(lumaEndP) >= gradientScaled;
                if (!doneN) posN.x -= offNP.x * FXAA_QUALITY_P4;
                if (!doneN) posN.y -= offNP.y * FXAA_QUALITY_P4;
                doneNP = (!doneN) || (!doneP);
                if (!doneP) posP.x += offNP.x * FXAA_QUALITY_P4;
                if (!doneP) posP.y += offNP.y * FXAA_QUALITY_P4;

#if (FXAA_QUALITY_PS > 5)
                if (doneNP) {
                    if (!doneN) lumaEndN = FxaaLuma(FxaaTexTop(tex, posN.xy));
                    if (!doneP) lumaEndP = FxaaLuma(FxaaTexTop(tex, posP.xy));
                    if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
                    if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
                    doneN = abs(lumaEndN) >= gradientScaled;
                    doneP = abs(lumaEndP) >= gradientScaled;
                    if (!doneN) posN.x -= offNP.x * FXAA_QUALITY_P5;
                    if (!doneN) posN.y -= offNP.y * FXAA_QUALITY_P5;
                    doneNP = (!doneN) || (!doneP);
                    if (!doneP) posP.x += offNP.x * FXAA_QUALITY_P5;
                    if (!doneP) posP.y += offNP.y * FXAA_QUALITY_P5;

#if (FXAA_QUALITY_PS > 6)
                    if (doneNP) {
                        if (!doneN) lumaEndN = FxaaLuma(FxaaTexTop(tex, posN.xy));
                        if (!doneP) lumaEndP = FxaaLuma(FxaaTexTop(tex, posP.xy));
                        if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
                        if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
                        doneN = abs(lumaEndN) >= gradientScaled;
                        doneP = abs(lumaEndP) >= gradientScaled;
                        if (!doneN) posN.x -= offNP.x * FXAA_QUALITY_P6;
                        if (!doneN) posN.y -= offNP.y * FXAA_QUALITY_P6;
                        doneNP = (!doneN) || (!doneP);
                        if (!doneP) posP.x += offNP.x * FXAA_QUALITY_P6;
                        if (!doneP) posP.y += offNP.y * FXAA_QUALITY_P6;

#if (FXAA_QUALITY_PS > 7)
                        if (doneNP) {
                            if (!doneN) lumaEndN = FxaaLuma(FxaaTexTop(tex, posN.xy));
                            if (!doneP) lumaEndP = FxaaLuma(FxaaTexTop(tex, posP.xy));
                            if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
                            if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
                            doneN = abs(lumaEndN) >= gradientScaled;
                            doneP = abs(lumaEndP) >= gradientScaled;
                            if (!doneN) posN.x -= offNP.x * FXAA_QUALITY_P7;
                            if (!doneN) posN.y -= offNP.y * FXAA_QUALITY_P7;
                            doneNP = (!doneN) || (!doneP);
                            if (!doneP) posP.x += offNP.x * FXAA_QUALITY_P7;
                            if (!doneP) posP.y += offNP.y * FXAA_QUALITY_P7;

#if (FXAA_QUALITY_PS > 8)
                            if (doneNP) {
                                if (!doneN) lumaEndN = FxaaLuma(FxaaTexTop(tex, posN.xy));
                                if (!doneP) lumaEndP = FxaaLuma(FxaaTexTop(tex, posP.xy));
                                if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
                                if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
                                doneN = abs(lumaEndN) >= gradientScaled;
                                doneP = abs(lumaEndP) >= gradientScaled;
                                if (!doneN) posN.x -= offNP.x * FXAA_QUALITY_P8;
                                if (!doneN) posN.y -= offNP.y * FXAA_QUALITY_P8;
                                doneNP = (!doneN) || (!doneP);
                                if (!doneP) posP.x += offNP.x * FXAA_QUALITY_P8;
                                if (!doneP) posP.y += offNP.y * FXAA_QUALITY_P8;

#if (FXAA_QUALITY_PS > 9)
                                if (doneNP) {
                                    if (!doneN) lumaEndN = FxaaLuma(FxaaTexTop(tex, posN.xy));
                                    if (!doneP) lumaEndP = FxaaLuma(FxaaTexTop(tex, posP.xy));
                                    if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
                                    if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
                                    doneN = abs(lumaEndN) >= gradientScaled;
                                    doneP = abs(lumaEndP) >= gradientScaled;
                                    if (!doneN) posN.x -= offNP.x * FXAA_QUALITY_P9;
                                    if (!doneN) posN.y -= offNP.y * FXAA_QUALITY_P9;
                                    doneNP = (!doneN) || (!doneP);
                                    if (!doneP) posP.x += offNP.x * FXAA_QUALITY_P9;
                                    if (!doneP) posP.y += offNP.y * FXAA_QUALITY_P9;

#if (FXAA_QUALITY_PS > 10)
                                    if (doneNP) {
                                        if (!doneN) lumaEndN = FxaaLuma(FxaaTexTop(tex, posN.xy));
                                        if (!doneP) lumaEndP = FxaaLuma(FxaaTexTop(tex, posP.xy));
                                        if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
                                        if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
                                        doneN = abs(lumaEndN) >= gradientScaled;
                                        doneP = abs(lumaEndP) >= gradientScaled;
                                        if (!doneN) posN.x -= offNP.x * FXAA_QUALITY_P10;
                                        if (!doneN) posN.y -= offNP.y * FXAA_QUALITY_P10;
                                        doneNP = (!doneN) || (!doneP);
                                        if (!doneP) posP.x += offNP.x * FXAA_QUALITY_P10;
                                        if (!doneP) posP.y += offNP.y * FXAA_QUALITY_P10;

#if (FXAA_QUALITY_PS > 11)
                                        if (doneNP) {
                                            if (!doneN) lumaEndN = FxaaLuma(FxaaTexTop(tex, posN.xy));
                                            if (!doneP) lumaEndP = FxaaLuma(FxaaTexTop(tex, posP.xy));
                                            if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
                                            if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
                                            doneN = abs(lumaEndN) >= gradientScaled;
                                            doneP = abs(lumaEndP) >= gradientScaled;
                                            if (!doneN) posN.x -= offNP.x * FXAA_QUALITY_P11;
                                            if (!doneN) posN.y -= offNP.y * FXAA_QUALITY_P11;
                                            doneNP = (!doneN) || (!doneP);
                                            if (!doneP) posP.x += offNP.x * FXAA_QUALITY_P11;
                                            if (!doneP) posP.y += offNP.y * FXAA_QUALITY_P11;
                                        }
#endif
                                    }
#endif
                                }
#endif
                            }
#endif
                        }
#endif
                    }
#endif
                }
#endif
            }
#endif
        }
#endif
    }
    float dstN = posM.x - posN.x;
    float dstP = posP.x - posM.x;
    if (!horzSpan) dstN = posM.y - posN.y;
    if (!horzSpan) dstP = posP.y - posM.y;

    bool goodSpanN = (lumaEndN < 0.0) != lumaMLTZero;
    float spanLength = (dstP + dstN);
    bool goodSpanP = (lumaEndP < 0.0) != lumaMLTZero;
    float spanLengthRcp = 1.0 / spanLength;

    bool directionN = dstN < dstP;
    float dst = min(dstN, dstP);
    bool goodSpan = directionN ? goodSpanN : goodSpanP;
    float subpixG = subpixF * subpixF;
    float pixelOffset = (dst * (-spanLengthRcp)) + 0.5;
    float subpixH = subpixG * fxaaQualitySubpix;

    float pixelOffsetGood = goodSpan ? pixelOffset : 0.0;
    float pixelOffsetSubpix = max(pixelOffsetGood, subpixH);
    if (!horzSpan) posM.x += pixelOffsetSubpix * lengthSign;
    if (horzSpan) posM.y += pixelOffsetSubpix * lengthSign;

    return float4(FxaaTexTop(tex, posM).xyz, lumaM);
}

#endif // FXAA_HLSLI
