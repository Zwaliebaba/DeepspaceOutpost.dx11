#ifndef SMAA_HEADER_HLSLI
#define SMAA_HEADER_HLSLI

//==============================================================================
// smaa-header.hlsli
//
// GLSL counterpart: partials/smaa-header.glsl, which set SMAA_INCLUDE_VS /
// SMAA_INCLUDE_PS per pipeline stage (and GLSL `precision` qualifiers).
//
// HLSL files are per-stage, so a single shared header cannot set both the VS
// and PS gating. Instead EACH shader stage file defines SMAA_INCLUDE_VS /
// SMAA_INCLUDE_PS (1 / 0 for the VS file, 0 / 1 for the PS file) BEFORE
// including "smaa.hlsli". GLSL `precision` qualifiers have no HLSL equivalent
// and are dropped. This header is therefore just shared common setup.
//==============================================================================

#include "common.hlsli"

#endif // SMAA_HEADER_HLSLI
