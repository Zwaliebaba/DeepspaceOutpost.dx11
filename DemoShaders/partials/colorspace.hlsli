#ifndef COLORSPACE_HLSLI
#define COLORSPACE_HLSLI

#include "common.hlsli"

/*
GLSL Color Space Utility Functions
(c) 2015 tobspr  -- MIT license (see original colorspace.glsl)

Ported to HLSL. The original mat3 constants were filled column-major in GLSL and
used as `M * v`. Keeping the identical 9 numbers in an HLSL row-major float3x3
and evaluating `mul(v, M)` reproduces the original `M * v` exactly.
*/

// Constants
static const float HCV_EPSILON = 1e-10;
static const float HSL_EPSILON = 1e-10;
static const float HCY_EPSILON = 1e-10;

static const float SRGB_GAMMA = 1.0 / 2.2;
static const float SRGB_INVERSE_GAMMA = 2.2;
static const float SRGB_ALPHA = 0.055;

static const float3x3 RGB_2_XYZ =
{
    0.4124564, 0.2126729, 0.0193339,
    0.3575761, 0.7151522, 0.1191920,
    0.1804375, 0.0721750, 0.9503041
};

static const float3x3 XYZ_2_RGB =
{
     3.2404542, -0.9692660,  0.0556434,
    -1.5371385,  1.8760108, -0.2040259,
    -0.4985314,  0.0415560,  1.0572252
};

static const float3 LUMA_COEFFS = float3(0.2126, 0.7152, 0.0722);

float get_luminance(float3 rgb) { return dot(LUMA_COEFFS, rgb); }

float3 rgb_to_srgb_approx(float3 rgb)  { return pow(rgb, (float3)SRGB_GAMMA); }
float3 srgb_to_rgb_approx(float3 srgb) { return pow(srgb, (float3)SRGB_INVERSE_GAMMA); }

float linear_to_srgb(float channel)
{
    if (channel <= 0.0031308)
        return 12.92 * channel;
    else
        return (1.0 + SRGB_ALPHA) * pow(channel, 1.0 / 2.4) - SRGB_ALPHA;
}

float srgb_to_linear(float channel)
{
    if (channel <= 0.04045)
        return channel / 12.92;
    else
        return pow((channel + SRGB_ALPHA) / (1.0 + SRGB_ALPHA), 2.4);
}

float3 rgb_to_srgb(float3 linearRGB)
{
    bool3 cutoff = linearRGB < (float3)0.0031308;
    float3 higher = (float3)(1.0 + SRGB_ALPHA) * pow(linearRGB, (float3)(1.0 / 2.4)) - (float3)SRGB_ALPHA;
    float3 lower = linearRGB * (float3)12.92;
    return lerp(higher, lower, (float3)cutoff);
}

float3 srgb_to_rgb(float3 sRGB)
{
    bool3 cutoff = sRGB < (float3)0.04045;
    float3 higher = pow((sRGB + (float3)SRGB_ALPHA) / (float3)(1.0 + SRGB_ALPHA), (float3)2.4);
    float3 lower = sRGB / (float3)12.92;
    return lerp(higher, lower, (float3)cutoff);
}

float3 rgb_to_xyz(float3 rgb) { return mul(rgb, RGB_2_XYZ); }
float3 xyz_to_rgb(float3 xyz) { return mul(xyz, XYZ_2_RGB); }

float3 xyz_to_xyY(float3 xyz)
{
    float Y = xyz.y;
    float x = xyz.x / (xyz.x + xyz.y + xyz.z);
    float y = xyz.y / (xyz.x + xyz.y + xyz.z);
    return float3(x, y, Y);
}

float3 xyY_to_xyz(float3 xyY)
{
    float Y = xyY.z;
    float x = Y * xyY.x / xyY.y;
    float z = Y * (1.0 - xyY.x - xyY.y) / xyY.y;
    return float3(x, Y, z);
}

float3 rgb_to_xyY(float3 rgb) { return xyz_to_xyY(rgb_to_xyz(rgb)); }
float3 xyY_to_rgb(float3 xyY) { return xyz_to_rgb(xyY_to_xyz(xyY)); }

float3 rgb_to_hcv(float3 rgb)
{
    // Based on work by Sam Hocevar and Emil Persson
    float4 P = (rgb.g < rgb.b) ? float4(rgb.bg, -1.0, 2.0 / 3.0) : float4(rgb.gb, 0.0, -1.0 / 3.0);
    float4 Q = (rgb.r < P.x) ? float4(P.xyw, rgb.r) : float4(rgb.r, P.yzx);
    float C = Q.x - min(Q.w, Q.y);
    float H = abs((Q.w - Q.y) / (6.0 * C + HCV_EPSILON) + Q.z);
    return float3(H, C, Q.x);
}

float3 hue_to_rgb(float hue)
{
    float R = abs(hue * 6.0 - 3.0) - 1.0;
    float G = 2.0 - abs(hue * 6.0 - 2.0);
    float B = 2.0 - abs(hue * 6.0 - 4.0);
    return saturate(float3(R, G, B));
}

float3 hsv_to_rgb(float3 hsv)
{
    float3 rgb = hue_to_rgb(hsv.x);
    return ((rgb - 1.0) * hsv.y + 1.0) * hsv.z;
}

float3 hsl_to_rgb(float3 hsl)
{
    float3 rgb = hue_to_rgb(hsl.x);
    float C = (1.0 - abs(2.0 * hsl.z - 1.0)) * hsl.y;
    return (rgb - 0.5) * C + hsl.z;
}

float3 hcy_to_rgb(float3 hcy)
{
    const float3 HCYwts = float3(0.299, 0.587, 0.114);
    float3 RGB = hue_to_rgb(hcy.x);
    float Z = dot(RGB, HCYwts);
    if (hcy.z < Z)
        hcy.y *= hcy.z / Z;
    else if (Z < 1.0)
        hcy.y *= (1.0 - hcy.z) / (1.0 - Z);
    return (RGB - Z) * hcy.y + hcy.z;
}

float3 rgb_to_hsv(float3 rgb)
{
    float3 HCV = rgb_to_hcv(rgb);
    float S = HCV.y / (HCV.z + HCV_EPSILON);
    return float3(HCV.x, S, HCV.z);
}

float3 rgb_to_hsl(float3 rgb)
{
    float3 HCV = rgb_to_hcv(rgb);
    float L = HCV.z - HCV.y * 0.5;
    float S = HCV.y / (1.0 - abs(L * 2.0 - 1.0) + HSL_EPSILON);
    return float3(HCV.x, S, L);
}

float3 rgb_to_hcy(float3 rgb)
{
    const float3 HCYwts = float3(0.299, 0.587, 0.114);
    float3 HCV = rgb_to_hcv(rgb);
    float Y = dot(rgb, HCYwts);
    float Z = dot(hue_to_rgb(HCV.x), HCYwts);
    if (Y < Z)
        HCV.y *= Z / (HCY_EPSILON + Y);
    else
        HCV.y *= (1.0 - Z) / (HCY_EPSILON + 1.0 - Y);
    return float3(HCV.x, HCV.y, Y);
}

float3 rgb_to_ycbcr(float3 rgb)
{
    float y = 0.299 * rgb.r + 0.587 * rgb.g + 0.114 * rgb.b;
    float cb = (rgb.b - y) * 0.565;
    float cr = (rgb.r - y) * 0.713;
    return float3(y, cb, cr);
}

float3 ycbcr_to_rgb(float3 yuv)
{
    return float3(
        yuv.x + 1.403 * yuv.z,
        yuv.x - 0.344 * yuv.y - 0.714 * yuv.z,
        yuv.x + 1.770 * yuv.y);
}

// Additional conversions (to rgb first, then to target space).
float3 xyz_to_srgb(float3 xyz)  { return rgb_to_srgb(xyz_to_rgb(xyz)); }
float3 xyY_to_srgb(float3 xyY)  { return rgb_to_srgb(xyY_to_rgb(xyY)); }
float3 hue_to_srgb(float hue)   { return rgb_to_srgb(hue_to_rgb(hue)); }
float3 hsv_to_srgb(float3 hsv)  { return rgb_to_srgb(hsv_to_rgb(hsv)); }
float3 hsl_to_srgb(float3 hsl)  { return rgb_to_srgb(hsl_to_rgb(hsl)); }
float3 hcy_to_srgb(float3 hcy)  { return rgb_to_srgb(hcy_to_rgb(hcy)); }
float3 ycbcr_to_srgb(float3 yuv){ return rgb_to_srgb(ycbcr_to_rgb(yuv)); }

float3 srgb_to_xyz(float3 srgb) { return rgb_to_xyz(srgb_to_rgb(srgb)); }
float3 hue_to_xyz(float hue)    { return rgb_to_xyz(hue_to_rgb(hue)); }
float3 hsv_to_xyz(float3 hsv)   { return rgb_to_xyz(hsv_to_rgb(hsv)); }
float3 hsl_to_xyz(float3 hsl)   { return rgb_to_xyz(hsl_to_rgb(hsl)); }
float3 hcy_to_xyz(float3 hcy)   { return rgb_to_xyz(hcy_to_rgb(hcy)); }
float3 ycbcr_to_xyz(float3 yuv) { return rgb_to_xyz(ycbcr_to_rgb(yuv)); }

float3 srgb_to_xyY(float3 srgb) { return rgb_to_xyY(srgb_to_rgb(srgb)); }
float3 hue_to_xyY(float hue)    { return rgb_to_xyY(hue_to_rgb(hue)); }
float3 hsv_to_xyY(float3 hsv)   { return rgb_to_xyY(hsv_to_rgb(hsv)); }
float3 hsl_to_xyY(float3 hsl)   { return rgb_to_xyY(hsl_to_rgb(hsl)); }
float3 hcy_to_xyY(float3 hcy)   { return rgb_to_xyY(hcy_to_rgb(hcy)); }
float3 ycbcr_to_xyY(float3 yuv) { return rgb_to_xyY(ycbcr_to_rgb(yuv)); }

float3 srgb_to_hcv(float3 srgb) { return rgb_to_hcv(srgb_to_rgb(srgb)); }
float3 xyz_to_hcv(float3 xyz)   { return rgb_to_hcv(xyz_to_rgb(xyz)); }
float3 xyY_to_hcv(float3 xyY)   { return rgb_to_hcv(xyY_to_rgb(xyY)); }
float3 hue_to_hcv(float hue)    { return rgb_to_hcv(hue_to_rgb(hue)); }
float3 hsv_to_hcv(float3 hsv)   { return rgb_to_hcv(hsv_to_rgb(hsv)); }
float3 hsl_to_hcv(float3 hsl)   { return rgb_to_hcv(hsl_to_rgb(hsl)); }
float3 hcy_to_hcv(float3 hcy)   { return rgb_to_hcv(hcy_to_rgb(hcy)); }
float3 ycbcr_to_hcv(float3 yuv) { return rgb_to_hcy(ycbcr_to_rgb(yuv)); }

float3 srgb_to_hsv(float3 srgb) { return rgb_to_hsv(srgb_to_rgb(srgb)); }
float3 xyz_to_hsv(float3 xyz)   { return rgb_to_hsv(xyz_to_rgb(xyz)); }
float3 xyY_to_hsv(float3 xyY)   { return rgb_to_hsv(xyY_to_rgb(xyY)); }
float3 hue_to_hsv(float hue)    { return rgb_to_hsv(hue_to_rgb(hue)); }
float3 hsl_to_hsv(float3 hsl)   { return rgb_to_hsv(hsl_to_rgb(hsl)); }
float3 hcy_to_hsv(float3 hcy)   { return rgb_to_hsv(hcy_to_rgb(hcy)); }
float3 ycbcr_to_hsv(float3 yuv) { return rgb_to_hsv(ycbcr_to_rgb(yuv)); }

float3 srgb_to_hsl(float3 srgb) { return rgb_to_hsl(srgb_to_rgb(srgb)); }
float3 xyz_to_hsl(float3 xyz)   { return rgb_to_hsl(xyz_to_rgb(xyz)); }
float3 xyY_to_hsl(float3 xyY)   { return rgb_to_hsl(xyY_to_rgb(xyY)); }
float3 hue_to_hsl(float hue)    { return rgb_to_hsl(hue_to_rgb(hue)); }
float3 hsv_to_hsl(float3 hsv)   { return rgb_to_hsl(hsv_to_rgb(hsv)); }
float3 hcy_to_hsl(float3 hcy)   { return rgb_to_hsl(hcy_to_rgb(hcy)); }
float3 ycbcr_to_hsl(float3 yuv) { return rgb_to_hsl(ycbcr_to_rgb(yuv)); }

float3 srgb_to_hcy(float3 srgb) { return rgb_to_hcy(srgb_to_rgb(srgb)); }
float3 xyz_to_hcy(float3 xyz)   { return rgb_to_hcy(xyz_to_rgb(xyz)); }
float3 xyY_to_hcy(float3 xyY)   { return rgb_to_hcy(xyY_to_rgb(xyY)); }
float3 hue_to_hcy(float hue)    { return rgb_to_hcy(hue_to_rgb(hue)); }
float3 hsv_to_hcy(float3 hsv)   { return rgb_to_hcy(hsv_to_rgb(hsv)); }
float3 hsl_to_hcy(float3 hsl)   { return rgb_to_hcy(hsl_to_rgb(hsl)); }
float3 ycbcr_to_hcy(float3 yuv) { return rgb_to_hcy(ycbcr_to_rgb(yuv)); }

float3 srgb_to_ycbcr(float3 srgb) { return rgb_to_ycbcr(srgb_to_rgb(srgb)); }
float3 xyz_to_ycbcr(float3 xyz)   { return rgb_to_ycbcr(xyz_to_rgb(xyz)); }
float3 xyY_to_ycbcr(float3 xyY)   { return rgb_to_ycbcr(xyY_to_rgb(xyY)); }
float3 hue_to_ycbcr(float hue)    { return rgb_to_ycbcr(hue_to_rgb(hue)); }
float3 hsv_to_ycbcr(float3 hsv)   { return rgb_to_ycbcr(hsv_to_rgb(hsv)); }
float3 hsl_to_ycbcr(float3 hsl)   { return rgb_to_ycbcr(hsl_to_rgb(hsl)); }
float3 hcy_to_ycbcr(float3 hcy)   { return rgb_to_ycbcr(hcy_to_rgb(hcy)); }

#endif // COLORSPACE_HLSLI
