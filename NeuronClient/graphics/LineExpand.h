#pragma once

// LineExpand - the pure geometry behind H4's screen-space line expansion + blur.
//
// H4 draws each wire EDGE as a screen-space QUAD (4 verts per segment, picked by
// SV_VertexID in the vertex shader) so line weight is a style parameter rather than
// the hardware's 1-pixel line. The expansion math - clip -> NDC -> pixel-space
// perpendicular offset -> back to clip - is subtle (aspect, the w divide), and the
// HLSL that does it can't run in CI, so it lives here as a pure function the vertex
// shader mirrors line-for-line and the headless suite pins. Same story for the
// separable Gaussian blur weights of the emissive glow pass.
//
// Deliberately free of D3D (plain floats), so it is unit-tested headlessly.

#include <cmath>
#include <cstdint>
#include <vector>

namespace Neuron::Graphics
{
  struct Clip4 { float x, y, z, w; };   // a homogeneous clip-space position

  // Expand one end of a clip-space segment into a screen-space quad corner. The quad
  // is four vertices (a triangle strip): vertexId 0,1 at clip0 (offset -/+ across the
  // line), 2,3 at clip1 - so 0..3 covers both ends and both sides. `_halfWidthPx` is
  // half the line weight in pixels; `_vpW/_vpH` the viewport size. Endpoints behind
  // the eye (w <= 0) are passed through unexpanded (the clipper handles them). Pure;
  // the H4 vertex shader is a transcription of this.
  [[nodiscard]] inline Clip4 ExpandLineCorner(const Clip4& _clip0, const Clip4& _clip1,
                                              int _vertexId, float _halfWidthPx,
                                              float _vpW, float _vpH)
  {
    const bool atStart = (_vertexId < 2);
    const Clip4& base  = atStart ? _clip0 : _clip1;
    if (_clip0.w <= 0.0f || _clip1.w <= 0.0f || _vpW <= 0.0f || _vpH <= 0.0f)
      return base;   // don't expand across the near plane / with no viewport

    // NDC of both ends.
    const float nx0 = _clip0.x / _clip0.w, ny0 = _clip0.y / _clip0.w;
    const float nx1 = _clip1.x / _clip1.w, ny1 = _clip1.y / _clip1.w;

    // Direction in PIXELS (NDC spans 2 over the viewport), then its perpendicular.
    float dx = (nx1 - nx0) * _vpW * 0.5f;
    float dy = (ny1 - ny0) * _vpH * 0.5f;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6f)
    {
      dx = 1.0f; dy = 0.0f;   // degenerate segment: pick an arbitrary axis
    }
    else
    {
      dx /= len; dy /= len;
    }
    const float perpX = -dy, perpY = dx;   // unit perpendicular, pixel space

    // Offset this corner across the line: sign by the odd/even vertex id.
    const float sign = (_vertexId & 1) ? 1.0f : -1.0f;
    // Pixel offset -> NDC offset (2 NDC over viewport px).
    const float offNdcX = perpX * _halfWidthPx * sign * (2.0f / _vpW);
    const float offNdcY = perpY * _halfWidthPx * sign * (2.0f / _vpH);

    const float baseNdcX = base.x / base.w;
    const float baseNdcY = base.y / base.w;
    Clip4 out;
    out.x = (baseNdcX + offNdcX) * base.w;   // back to clip space
    out.y = (baseNdcY + offNdcY) * base.w;
    out.z = base.z;
    out.w = base.w;
    return out;
  }

  // Normalised 1D Gaussian weights for a separable blur of the given radius (the
  // kernel has 2*radius+1 taps, symmetric, summing to 1). sigma defaults to
  // radius/2. Used by the emissive-glow blur pass; pure + unit-tested.
  [[nodiscard]] inline std::vector<float> GaussianKernel(int _radius, float _sigma = 0.0f)
  {
    std::vector<float> w;
    if (_radius < 0)
      return w;
    const float sigma = (_sigma > 0.0f) ? _sigma : (static_cast<float>(_radius) * 0.5f + 0.0001f);
    float sum = 0.0f;
    for (int i = -_radius; i <= _radius; ++i)
    {
      const float g = std::exp(-static_cast<float>(i * i) / (2.0f * sigma * sigma));
      w.push_back(g);
      sum += g;
    }
    for (float& v : w)
      v /= sum;
    return w;
  }
}
