#ifndef FFX_A_HLSLI
#define FFX_A_HLSLI

//==============================================================================================================================
//
//                                  [A] SHADER PORTABILITY 1.20210629  -- HLSL (SM5.0) PORT
//
// This is the HLSL (A_HLSL + A_GPU) code path of AMD FidelityFX "ffx_a.h", selected/emitted for
// DirectX 11 / Shader Model 5.0. The GLSL-only and CPU-only branches were dropped. The half-precision
// (A_HALF) paths are preserved behind their original #ifdef A_HALF guards (they use min16float and
// compile under SM5.0, though FSR's 32-bit (F) paths are what the ported shaders use by default).
//
// FidelityFX Super Resolution Sample
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// (MIT License -- see original ffx_a.h header.)
//==============================================================================================================================

// Activate the HLSL GPU code paths.
#ifndef A_GPU
 #define A_GPU 1
#endif
#ifndef A_HLSL
 #define A_HLSL 1
#endif

//==============================================================================================================================
//                                                           COMMON
//==============================================================================================================================
#define A_2PI 6.28318530718

//==============================================================================================================================
//
//                                                            HLSL
//
//==============================================================================================================================
#define AP1 bool
#define AP2 bool2
#define AP3 bool3
#define AP4 bool4
//------------------------------------------------------------------------------------------------------------------------------
#define AF1 float
#define AF2 float2
#define AF3 float3
#define AF4 float4
//------------------------------------------------------------------------------------------------------------------------------
#define AU1 uint
#define AU2 uint2
#define AU3 uint3
#define AU4 uint4
//------------------------------------------------------------------------------------------------------------------------------
#define ASU1 int
#define ASU2 int2
#define ASU3 int3
#define ASU4 int4
//==============================================================================================================================
#define AF1_AU1(x) asfloat(AU1(x))
#define AF2_AU2(x) asfloat(AU2(x))
#define AF3_AU3(x) asfloat(AU3(x))
#define AF4_AU4(x) asfloat(AU4(x))
//------------------------------------------------------------------------------------------------------------------------------
#define AU1_AF1(x) asuint(AF1(x))
#define AU2_AF2(x) asuint(AF2(x))
#define AU3_AF3(x) asuint(AF3(x))
#define AU4_AF4(x) asuint(AF4(x))
//------------------------------------------------------------------------------------------------------------------------------
AU1 AU1_AH1_AF1_x(AF1 a){return f32tof16(a);}
#define AU1_AH1_AF1(a) AU1_AH1_AF1_x(AF1(a))
//------------------------------------------------------------------------------------------------------------------------------
AU1 AU1_AH2_AF2_x(AF2 a){return f32tof16(a.x)|(f32tof16(a.y)<<16);}
#define AU1_AH2_AF2(a) AU1_AH2_AF2_x(AF2(a))
#define AU1_AB4Unorm_AF4(x) D3DCOLORtoUBYTE4(AF4(x))
//------------------------------------------------------------------------------------------------------------------------------
AF2 AF2_AH2_AU1_x(AU1 x){return AF2(f16tof32(x&0xFFFF),f16tof32(x>>16));}
#define AF2_AH2_AU1(x) AF2_AH2_AU1_x(AU1(x))
//==============================================================================================================================
AF1 AF1_x(AF1 a){return AF1(a);}
AF2 AF2_x(AF1 a){return AF2(a,a);}
AF3 AF3_x(AF1 a){return AF3(a,a,a);}
AF4 AF4_x(AF1 a){return AF4(a,a,a,a);}
#define AF1_(a) AF1_x(AF1(a))
#define AF2_(a) AF2_x(AF1(a))
#define AF3_(a) AF3_x(AF1(a))
#define AF4_(a) AF4_x(AF1(a))
//------------------------------------------------------------------------------------------------------------------------------
AU1 AU1_x(AU1 a){return AU1(a);}
AU2 AU2_x(AU1 a){return AU2(a,a);}
AU3 AU3_x(AU1 a){return AU3(a,a,a);}
AU4 AU4_x(AU1 a){return AU4(a,a,a,a);}
#define AU1_(a) AU1_x(AU1(a))
#define AU2_(a) AU2_x(AU1(a))
#define AU3_(a) AU3_x(AU1(a))
#define AU4_(a) AU4_x(AU1(a))
//==============================================================================================================================
AU1 AAbsSU1(AU1 a){return AU1(abs(ASU1(a)));}
AU2 AAbsSU2(AU2 a){return AU2(abs(ASU2(a)));}
AU3 AAbsSU3(AU3 a){return AU3(abs(ASU3(a)));}
AU4 AAbsSU4(AU4 a){return AU4(abs(ASU4(a)));}
//------------------------------------------------------------------------------------------------------------------------------
AF1 AClampF1(AF1 x,AF1 n,AF1 m){return max(n,min(x,m));}
AF2 AClampF2(AF2 x,AF2 n,AF2 m){return max(n,min(x,m));}
AF3 AClampF3(AF3 x,AF3 n,AF3 m){return max(n,min(x,m));}
AF4 AClampF4(AF4 x,AF4 n,AF4 m){return max(n,min(x,m));}
//------------------------------------------------------------------------------------------------------------------------------
AF1 AFractF1(AF1 x){return x-floor(x);}
AF2 AFractF2(AF2 x){return x-floor(x);}
AF3 AFractF3(AF3 x){return x-floor(x);}
AF4 AFractF4(AF4 x){return x-floor(x);}
//------------------------------------------------------------------------------------------------------------------------------
AF1 ALerpF1(AF1 x,AF1 y,AF1 a){return lerp(x,y,a);}
AF2 ALerpF2(AF2 x,AF2 y,AF2 a){return lerp(x,y,a);}
AF3 ALerpF3(AF3 x,AF3 y,AF3 a){return lerp(x,y,a);}
AF4 ALerpF4(AF4 x,AF4 y,AF4 a){return lerp(x,y,a);}
//------------------------------------------------------------------------------------------------------------------------------
AF1 AMax3F1(AF1 x,AF1 y,AF1 z){return max(x,max(y,z));}
AF2 AMax3F2(AF2 x,AF2 y,AF2 z){return max(x,max(y,z));}
AF3 AMax3F3(AF3 x,AF3 y,AF3 z){return max(x,max(y,z));}
AF4 AMax3F4(AF4 x,AF4 y,AF4 z){return max(x,max(y,z));}
//------------------------------------------------------------------------------------------------------------------------------
AU1 AMax3SU1(AU1 x,AU1 y,AU1 z){return AU1(max(ASU1(x),max(ASU1(y),ASU1(z))));}
AU2 AMax3SU2(AU2 x,AU2 y,AU2 z){return AU2(max(ASU2(x),max(ASU2(y),ASU2(z))));}
AU3 AMax3SU3(AU3 x,AU3 y,AU3 z){return AU3(max(ASU3(x),max(ASU3(y),ASU3(z))));}
AU4 AMax3SU4(AU4 x,AU4 y,AU4 z){return AU4(max(ASU4(x),max(ASU4(y),ASU4(z))));}
//------------------------------------------------------------------------------------------------------------------------------
AU1 AMax3U1(AU1 x,AU1 y,AU1 z){return max(x,max(y,z));}
AU2 AMax3U2(AU2 x,AU2 y,AU2 z){return max(x,max(y,z));}
AU3 AMax3U3(AU3 x,AU3 y,AU3 z){return max(x,max(y,z));}
AU4 AMax3U4(AU4 x,AU4 y,AU4 z){return max(x,max(y,z));}
//------------------------------------------------------------------------------------------------------------------------------
AU1 AMaxSU1(AU1 a,AU1 b){return AU1(max(ASU1(a),ASU1(b)));}
AU2 AMaxSU2(AU2 a,AU2 b){return AU2(max(ASU2(a),ASU2(b)));}
AU3 AMaxSU3(AU3 a,AU3 b){return AU3(max(ASU3(a),ASU3(b)));}
AU4 AMaxSU4(AU4 a,AU4 b){return AU4(max(ASU4(a),ASU4(b)));}
//------------------------------------------------------------------------------------------------------------------------------
AF1 AMed3F1(AF1 x,AF1 y,AF1 z){return max(min(x,y),min(max(x,y),z));}
AF2 AMed3F2(AF2 x,AF2 y,AF2 z){return max(min(x,y),min(max(x,y),z));}
AF3 AMed3F3(AF3 x,AF3 y,AF3 z){return max(min(x,y),min(max(x,y),z));}
AF4 AMed3F4(AF4 x,AF4 y,AF4 z){return max(min(x,y),min(max(x,y),z));}
//------------------------------------------------------------------------------------------------------------------------------
AF1 AMin3F1(AF1 x,AF1 y,AF1 z){return min(x,min(y,z));}
AF2 AMin3F2(AF2 x,AF2 y,AF2 z){return min(x,min(y,z));}
AF3 AMin3F3(AF3 x,AF3 y,AF3 z){return min(x,min(y,z));}
AF4 AMin3F4(AF4 x,AF4 y,AF4 z){return min(x,min(y,z));}
//------------------------------------------------------------------------------------------------------------------------------
AU1 AMin3SU1(AU1 x,AU1 y,AU1 z){return AU1(min(ASU1(x),min(ASU1(y),ASU1(z))));}
AU2 AMin3SU2(AU2 x,AU2 y,AU2 z){return AU2(min(ASU2(x),min(ASU2(y),ASU2(z))));}
AU3 AMin3SU3(AU3 x,AU3 y,AU3 z){return AU3(min(ASU3(x),min(ASU3(y),ASU3(z))));}
AU4 AMin3SU4(AU4 x,AU4 y,AU4 z){return AU4(min(ASU4(x),min(ASU4(y),ASU4(z))));}
//------------------------------------------------------------------------------------------------------------------------------
AU1 AMin3U1(AU1 x,AU1 y,AU1 z){return min(x,min(y,z));}
AU2 AMin3U2(AU2 x,AU2 y,AU2 z){return min(x,min(y,z));}
AU3 AMin3U3(AU3 x,AU3 y,AU3 z){return min(x,min(y,z));}
AU4 AMin3U4(AU4 x,AU4 y,AU4 z){return min(x,min(y,z));}
//------------------------------------------------------------------------------------------------------------------------------
AU1 AMinSU1(AU1 a,AU1 b){return AU1(min(ASU1(a),ASU1(b)));}
AU2 AMinSU2(AU2 a,AU2 b){return AU2(min(ASU2(a),ASU2(b)));}
AU3 AMinSU3(AU3 a,AU3 b){return AU3(min(ASU3(a),ASU3(b)));}
AU4 AMinSU4(AU4 a,AU4 b){return AU4(min(ASU4(a),ASU4(b)));}
//------------------------------------------------------------------------------------------------------------------------------
AF1 ANCosF1(AF1 x){return cos(x*AF1_(A_2PI));}
AF2 ANCosF2(AF2 x){return cos(x*AF2_(A_2PI));}
AF3 ANCosF3(AF3 x){return cos(x*AF3_(A_2PI));}
AF4 ANCosF4(AF4 x){return cos(x*AF4_(A_2PI));}
//------------------------------------------------------------------------------------------------------------------------------
AF1 ANSinF1(AF1 x){return sin(x*AF1_(A_2PI));}
AF2 ANSinF2(AF2 x){return sin(x*AF2_(A_2PI));}
AF3 ANSinF3(AF3 x){return sin(x*AF3_(A_2PI));}
AF4 ANSinF4(AF4 x){return sin(x*AF4_(A_2PI));}
//------------------------------------------------------------------------------------------------------------------------------
AF1 ARcpF1(AF1 x){return rcp(x);}
AF2 ARcpF2(AF2 x){return rcp(x);}
AF3 ARcpF3(AF3 x){return rcp(x);}
AF4 ARcpF4(AF4 x){return rcp(x);}
//------------------------------------------------------------------------------------------------------------------------------
AF1 ARsqF1(AF1 x){return rsqrt(x);}
AF2 ARsqF2(AF2 x){return rsqrt(x);}
AF3 ARsqF3(AF3 x){return rsqrt(x);}
AF4 ARsqF4(AF4 x){return rsqrt(x);}
//------------------------------------------------------------------------------------------------------------------------------
AF1 ASatF1(AF1 x){return saturate(x);}
AF2 ASatF2(AF2 x){return saturate(x);}
AF3 ASatF3(AF3 x){return saturate(x);}
AF4 ASatF4(AF4 x){return saturate(x);}
//------------------------------------------------------------------------------------------------------------------------------
AU1 AShrSU1(AU1 a,AU1 b){return AU1(ASU1(a)>>ASU1(b));}
AU2 AShrSU2(AU2 a,AU2 b){return AU2(ASU2(a)>>ASU2(b));}
AU3 AShrSU3(AU3 a,AU3 b){return AU3(ASU3(a)>>ASU3(b));}
AU4 AShrSU4(AU4 a,AU4 b){return AU4(ASU4(a)>>ASU4(b));}

//==============================================================================================================================
//                                                          HLSL HALF
//==============================================================================================================================
#ifdef A_HALF
 #define AH1 min16float
 #define AH2 min16float2
 #define AH3 min16float3
 #define AH4 min16float4
//------------------------------------------------------------------------------------------------------------------------------
 #define AW1 min16uint
 #define AW2 min16uint2
 #define AW3 min16uint3
 #define AW4 min16uint4
//------------------------------------------------------------------------------------------------------------------------------
 #define ASW1 min16int
 #define ASW2 min16int2
 #define ASW3 min16int3
 #define ASW4 min16int4
//==============================================================================================================================
 AH2 AH2_AU1_x(AU1 x){AF2 t=f16tof32(AU2(x&0xFFFF,x>>16));return AH2(t);}
 AH4 AH4_AU2_x(AU2 x){return AH4(AH2_AU1_x(x.x),AH2_AU1_x(x.y));}
 AW2 AW2_AU1_x(AU1 x){AU2 t=AU2(x&0xFFFF,x>>16);return AW2(t);}
 AW4 AW4_AU2_x(AU2 x){return AW4(AW2_AU1_x(x.x),AW2_AU1_x(x.y));}
 #define AH2_AU1(x) AH2_AU1_x(AU1(x))
 #define AH4_AU2(x) AH4_AU2_x(AU2(x))
 #define AW2_AU1(x) AW2_AU1_x(AU1(x))
 #define AW4_AU2(x) AW4_AU2_x(AU2(x))
//------------------------------------------------------------------------------------------------------------------------------
 AU1 AU1_AH2_x(AH2 x){return f32tof16(x.x)+(f32tof16(x.y)<<16);}
 AU2 AU2_AH4_x(AH4 x){return AU2(AU1_AH2_x(x.xy),AU1_AH2_x(x.zw));}
 AU1 AU1_AW2_x(AW2 x){return AU1(x.x)+(AU1(x.y)<<16);}
 AU2 AU2_AW4_x(AW4 x){return AU2(AU1_AW2_x(x.xy),AU1_AW2_x(x.zw));}
 #define AU1_AH2(x) AU1_AH2_x(AH2(x))
 #define AU2_AH4(x) AU2_AH4_x(AH4(x))
 #define AU1_AW2(x) AU1_AW2_x(AW2(x))
 #define AU2_AW4(x) AU2_AW4_x(AW4(x))
//==============================================================================================================================
 #define AW1_AH1(a) AW1(f32tof16(AF1(a)))
 #define AW2_AH2(a) AW2(AW1_AH1((a).x),AW1_AH1((a).y))
 #define AW3_AH3(a) AW3(AW1_AH1((a).x),AW1_AH1((a).y),AW1_AH1((a).z))
 #define AW4_AH4(a) AW4(AW1_AH1((a).x),AW1_AH1((a).y),AW1_AH1((a).z),AW1_AH1((a).w))
//------------------------------------------------------------------------------------------------------------------------------
 #define AH1_AW1(a) AH1(f16tof32(AU1(a)))
 #define AH2_AW2(a) AH2(AH1_AW1((a).x),AH1_AW1((a).y))
 #define AH3_AW3(a) AH3(AH1_AW1((a).x),AH1_AW1((a).y),AH1_AW1((a).z))
 #define AH4_AW4(a) AH4(AH1_AW1((a).x),AH1_AW1((a).y),AH1_AW1((a).z),AH1_AW1((a).w))
//==============================================================================================================================
 AH1 AH1_x(AH1 a){return AH1(a);}
 AH2 AH2_x(AH1 a){return AH2(a,a);}
 AH3 AH3_x(AH1 a){return AH3(a,a,a);}
 AH4 AH4_x(AH1 a){return AH4(a,a,a,a);}
 #define AH1_(a) AH1_x(AH1(a))
 #define AH2_(a) AH2_x(AH1(a))
 #define AH3_(a) AH3_x(AH1(a))
 #define AH4_(a) AH4_x(AH1(a))
//------------------------------------------------------------------------------------------------------------------------------
 AW1 AW1_x(AW1 a){return AW1(a);}
 AW2 AW2_x(AW1 a){return AW2(a,a);}
 AW3 AW3_x(AW1 a){return AW3(a,a,a);}
 AW4 AW4_x(AW1 a){return AW4(a,a,a,a);}
 #define AW1_(a) AW1_x(AW1(a))
 #define AW2_(a) AW2_x(AW1(a))
 #define AW3_(a) AW3_x(AW1(a))
 #define AW4_(a) AW4_x(AW1(a))
//==============================================================================================================================
 AW1 AAbsSW1(AW1 a){return AW1(abs(ASW1(a)));}
 AW2 AAbsSW2(AW2 a){return AW2(abs(ASW2(a)));}
 AW3 AAbsSW3(AW3 a){return AW3(abs(ASW3(a)));}
 AW4 AAbsSW4(AW4 a){return AW4(abs(ASW4(a)));}
//------------------------------------------------------------------------------------------------------------------------------
 AH1 AClampH1(AH1 x,AH1 n,AH1 m){return max(n,min(x,m));}
 AH2 AClampH2(AH2 x,AH2 n,AH2 m){return max(n,min(x,m));}
 AH3 AClampH3(AH3 x,AH3 n,AH3 m){return max(n,min(x,m));}
 AH4 AClampH4(AH4 x,AH4 n,AH4 m){return max(n,min(x,m));}
//------------------------------------------------------------------------------------------------------------------------------
 AH1 AFractH1(AH1 x){return x-floor(x);}
 AH2 AFractH2(AH2 x){return x-floor(x);}
 AH3 AFractH3(AH3 x){return x-floor(x);}
 AH4 AFractH4(AH4 x){return x-floor(x);}
//------------------------------------------------------------------------------------------------------------------------------
 AH1 ALerpH1(AH1 x,AH1 y,AH1 a){return lerp(x,y,a);}
 AH2 ALerpH2(AH2 x,AH2 y,AH2 a){return lerp(x,y,a);}
 AH3 ALerpH3(AH3 x,AH3 y,AH3 a){return lerp(x,y,a);}
 AH4 ALerpH4(AH4 x,AH4 y,AH4 a){return lerp(x,y,a);}
//------------------------------------------------------------------------------------------------------------------------------
 AH1 AMax3H1(AH1 x,AH1 y,AH1 z){return max(x,max(y,z));}
 AH2 AMax3H2(AH2 x,AH2 y,AH2 z){return max(x,max(y,z));}
 AH3 AMax3H3(AH3 x,AH3 y,AH3 z){return max(x,max(y,z));}
 AH4 AMax3H4(AH4 x,AH4 y,AH4 z){return max(x,max(y,z));}
//------------------------------------------------------------------------------------------------------------------------------
 AW1 AMaxSW1(AW1 a,AW1 b){return AW1(max(ASU1(a),ASU1(b)));}
 AW2 AMaxSW2(AW2 a,AW2 b){return AW2(max(ASU2(a),ASU2(b)));}
 AW3 AMaxSW3(AW3 a,AW3 b){return AW3(max(ASU3(a),ASU3(b)));}
 AW4 AMaxSW4(AW4 a,AW4 b){return AW4(max(ASU4(a),ASU4(b)));}
//------------------------------------------------------------------------------------------------------------------------------
 AH1 AMin3H1(AH1 x,AH1 y,AH1 z){return min(x,min(y,z));}
 AH2 AMin3H2(AH2 x,AH2 y,AH2 z){return min(x,min(y,z));}
 AH3 AMin3H3(AH3 x,AH3 y,AH3 z){return min(x,min(y,z));}
 AH4 AMin3H4(AH4 x,AH4 y,AH4 z){return min(x,min(y,z));}
//------------------------------------------------------------------------------------------------------------------------------
 AW1 AMinSW1(AW1 a,AW1 b){return AW1(min(ASU1(a),ASU1(b)));}
 AW2 AMinSW2(AW2 a,AW2 b){return AW2(min(ASU2(a),ASU2(b)));}
 AW3 AMinSW3(AW3 a,AW3 b){return AW3(min(ASU3(a),ASU3(b)));}
 AW4 AMinSW4(AW4 a,AW4 b){return AW4(min(ASU4(a),ASU4(b)));}
//------------------------------------------------------------------------------------------------------------------------------
 AH1 ARcpH1(AH1 x){return rcp(x);}
 AH2 ARcpH2(AH2 x){return rcp(x);}
 AH3 ARcpH3(AH3 x){return rcp(x);}
 AH4 ARcpH4(AH4 x){return rcp(x);}
//------------------------------------------------------------------------------------------------------------------------------
 AH1 ARsqH1(AH1 x){return rsqrt(x);}
 AH2 ARsqH2(AH2 x){return rsqrt(x);}
 AH3 ARsqH3(AH3 x){return rsqrt(x);}
 AH4 ARsqH4(AH4 x){return rsqrt(x);}
//------------------------------------------------------------------------------------------------------------------------------
 AH1 ASatH1(AH1 x){return saturate(x);}
 AH2 ASatH2(AH2 x){return saturate(x);}
 AH3 ASatH3(AH3 x){return saturate(x);}
 AH4 ASatH4(AH4 x){return saturate(x);}
//------------------------------------------------------------------------------------------------------------------------------
 AW1 AShrSW1(AW1 a,AW1 b){return AW1(ASW1(a)>>ASW1(b));}
 AW2 AShrSW2(AW2 a,AW2 b){return AW2(ASW2(a)>>ASW2(b));}
 AW3 AShrSW3(AW3 a,AW3 b){return AW3(ASW3(a)>>ASW3(b));}
 AW4 AShrSW4(AW4 a,AW4 b){return AW4(ASW4(a)>>ASW4(b));}
#endif // A_HALF

//==============================================================================================================================
//                                                          GPU COMMON
//==============================================================================================================================
// Negative and positive infinity.
#define A_INFP_F AF1_AU1(0x7f800000u)
#define A_INFN_F AF1_AU1(0xff800000u)
//------------------------------------------------------------------------------------------------------------------------------
// Copy sign from 's' to positive 'd'.
AF1 ACpySgnF1(AF1 d,AF1 s){return AF1_AU1(AU1_AF1(d)|(AU1_AF1(s)&AU1_(0x80000000u)));}
AF2 ACpySgnF2(AF2 d,AF2 s){return AF2_AU2(AU2_AF2(d)|(AU2_AF2(s)&AU2_(0x80000000u)));}
AF3 ACpySgnF3(AF3 d,AF3 s){return AF3_AU3(AU3_AF3(d)|(AU3_AF3(s)&AU3_(0x80000000u)));}
AF4 ACpySgnF4(AF4 d,AF4 s){return AF4_AU4(AU4_AF4(d)|(AU4_AF4(s)&AU4_(0x80000000u)));}
//------------------------------------------------------------------------------------------------------------------------------
AF1 ASignedF1(AF1 m){return ASatF1(m*AF1_(A_INFN_F));}
AF2 ASignedF2(AF2 m){return ASatF2(m*AF2_(A_INFN_F));}
AF3 ASignedF3(AF3 m){return ASatF3(m*AF3_(A_INFN_F));}
AF4 ASignedF4(AF4 m){return ASatF4(m*AF4_(A_INFN_F));}
//------------------------------------------------------------------------------------------------------------------------------
AF1 AGtZeroF1(AF1 m){return ASatF1(m*AF1_(A_INFP_F));}
AF2 AGtZeroF2(AF2 m){return ASatF2(m*AF2_(A_INFP_F));}
AF3 AGtZeroF3(AF3 m){return ASatF3(m*AF3_(A_INFP_F));}
AF4 AGtZeroF4(AF4 m){return ASatF4(m*AF4_(A_INFP_F));}
//==============================================================================================================================
#ifdef A_HALF
 #define A_INFP_H AH1_AW1(0x7c00u)
 #define A_INFN_H AH1_AW1(0xfc00u)
//------------------------------------------------------------------------------------------------------------------------------
 AH1 ACpySgnH1(AH1 d,AH1 s){return AH1_AW1(AW1_AH1(d)|(AW1_AH1(s)&AW1_(0x8000u)));}
 AH2 ACpySgnH2(AH2 d,AH2 s){return AH2_AW2(AW2_AH2(d)|(AW2_AH2(s)&AW2_(0x8000u)));}
 AH3 ACpySgnH3(AH3 d,AH3 s){return AH3_AW3(AW3_AH3(d)|(AW3_AH3(s)&AW3_(0x8000u)));}
 AH4 ACpySgnH4(AH4 d,AH4 s){return AH4_AW4(AW4_AH4(d)|(AW4_AH4(s)&AW4_(0x8000u)));}
//------------------------------------------------------------------------------------------------------------------------------
 AH1 ASignedH1(AH1 m){return ASatH1(m*AH1_(A_INFN_H));}
 AH2 ASignedH2(AH2 m){return ASatH2(m*AH2_(A_INFN_H));}
 AH3 ASignedH3(AH3 m){return ASatH3(m*AH3_(A_INFN_H));}
 AH4 ASignedH4(AH4 m){return ASatH4(m*AH4_(A_INFN_H));}
//------------------------------------------------------------------------------------------------------------------------------
 AH1 AGtZeroH1(AH1 m){return ASatH1(m*AH1_(A_INFP_H));}
 AH2 AGtZeroH2(AH2 m){return ASatH2(m*AH2_(A_INFP_H));}
 AH3 AGtZeroH3(AH3 m){return ASatH3(m*AH3_(A_INFP_H));}
 AH4 AGtZeroH4(AH4 m){return ASatH4(m*AH4_(A_INFP_H));}
#endif // A_HALF

//==============================================================================================================================
//                                                     HALF APPROXIMATIONS
//==============================================================================================================================
#ifdef A_HALF
 // Minimize squared error across full positive range, 2 ops.
 AH1 APrxLoSqrtH1(AH1 a){return AH1_AW1((AW1_AH1(a)>>AW1_(1))+AW1_(0x1de2));}
 AH2 APrxLoSqrtH2(AH2 a){return AH2_AW2((AW2_AH2(a)>>AW2_(1))+AW2_(0x1de2));}
 AH3 APrxLoSqrtH3(AH3 a){return AH3_AW3((AW3_AH3(a)>>AW3_(1))+AW3_(0x1de2));}
 AH4 APrxLoSqrtH4(AH4 a){return AH4_AW4((AW4_AH4(a)>>AW4_(1))+AW4_(0x1de2));}
//------------------------------------------------------------------------------------------------------------------------------
 AH1 APrxLoRcpH1(AH1 a){return AH1_AW1(AW1_(0x7784)-AW1_AH1(a));}
 AH2 APrxLoRcpH2(AH2 a){return AH2_AW2(AW2_(0x7784)-AW2_AH2(a));}
 AH3 APrxLoRcpH3(AH3 a){return AH3_AW3(AW3_(0x7784)-AW3_AH3(a));}
 AH4 APrxLoRcpH4(AH4 a){return AH4_AW4(AW4_(0x7784)-AW4_AH4(a));}
//------------------------------------------------------------------------------------------------------------------------------
 AH1 APrxMedRcpH1(AH1 a){AH1 b=AH1_AW1(AW1_(0x778d)-AW1_AH1(a));return b*(-b*a+AH1_(2.0));}
 AH2 APrxMedRcpH2(AH2 a){AH2 b=AH2_AW2(AW2_(0x778d)-AW2_AH2(a));return b*(-b*a+AH2_(2.0));}
 AH3 APrxMedRcpH3(AH3 a){AH3 b=AH3_AW3(AW3_(0x778d)-AW3_AH3(a));return b*(-b*a+AH3_(2.0));}
 AH4 APrxMedRcpH4(AH4 a){AH4 b=AH4_AW4(AW4_(0x778d)-AW4_AH4(a));return b*(-b*a+AH4_(2.0));}
//------------------------------------------------------------------------------------------------------------------------------
 AH1 APrxLoRsqH1(AH1 a){return AH1_AW1(AW1_(0x59a3)-(AW1_AH1(a)>>AW1_(1)));}
 AH2 APrxLoRsqH2(AH2 a){return AH2_AW2(AW2_(0x59a3)-(AW2_AH2(a)>>AW2_(1)));}
 AH3 APrxLoRsqH3(AH3 a){return AH3_AW3(AW3_(0x59a3)-(AW3_AH3(a)>>AW3_(1)));}
 AH4 APrxLoRsqH4(AH4 a){return AH4_AW4(AW4_(0x59a3)-(AW4_AH4(a)>>AW4_(1)));}
#endif // A_HALF

//==============================================================================================================================
//                                                    FLOAT APPROXIMATIONS
//==============================================================================================================================
AF1 APrxLoSqrtF1(AF1 a){return AF1_AU1((AU1_AF1(a)>>AU1_(1))+AU1_(0x1fbc4639));}
AF1 APrxLoRcpF1(AF1 a){return AF1_AU1(AU1_(0x7ef07ebb)-AU1_AF1(a));}
AF1 APrxMedRcpF1(AF1 a){AF1 b=AF1_AU1(AU1_(0x7ef19fff)-AU1_AF1(a));return b*(-b*a+AF1_(2.0));}
AF1 APrxLoRsqF1(AF1 a){return AF1_AU1(AU1_(0x5f347d74)-(AU1_AF1(a)>>AU1_(1)));}
//------------------------------------------------------------------------------------------------------------------------------
AF2 APrxLoSqrtF2(AF2 a){return AF2_AU2((AU2_AF2(a)>>AU2_(1))+AU2_(0x1fbc4639));}
AF2 APrxLoRcpF2(AF2 a){return AF2_AU2(AU2_(0x7ef07ebb)-AU2_AF2(a));}
AF2 APrxMedRcpF2(AF2 a){AF2 b=AF2_AU2(AU2_(0x7ef19fff)-AU2_AF2(a));return b*(-b*a+AF2_(2.0));}
AF2 APrxLoRsqF2(AF2 a){return AF2_AU2(AU2_(0x5f347d74)-(AU2_AF2(a)>>AU2_(1)));}
//------------------------------------------------------------------------------------------------------------------------------
AF3 APrxLoSqrtF3(AF3 a){return AF3_AU3((AU3_AF3(a)>>AU3_(1))+AU3_(0x1fbc4639));}
AF3 APrxLoRcpF3(AF3 a){return AF3_AU3(AU3_(0x7ef07ebb)-AU3_AF3(a));}
AF3 APrxMedRcpF3(AF3 a){AF3 b=AF3_AU3(AU3_(0x7ef19fff)-AU3_AF3(a));return b*(-b*a+AF3_(2.0));}
AF3 APrxLoRsqF3(AF3 a){return AF3_AU3(AU3_(0x5f347d74)-(AU3_AF3(a)>>AU3_(1)));}
//------------------------------------------------------------------------------------------------------------------------------
AF4 APrxLoSqrtF4(AF4 a){return AF4_AU4((AU4_AF4(a)>>AU4_(1))+AU4_(0x1fbc4639));}
AF4 APrxLoRcpF4(AF4 a){return AF4_AU4(AU4_(0x7ef07ebb)-AU4_AF4(a));}
AF4 APrxMedRcpF4(AF4 a){AF4 b=AF4_AU4(AU4_(0x7ef19fff)-AU4_AF4(a));return b*(-b*a+AF4_(2.0));}
AF4 APrxLoRsqF4(AF4 a){return AF4_AU4(AU4_(0x5f347d74)-(AU4_AF4(a)>>AU4_(1)));}

#endif // FFX_A_HLSLI
