#pragma once

// ViewMetrics - runtime 3D projection optics for a full-window scene (client).
//
// The legacy renderer hard-codes the software wireframe projection to a fixed
// 256x192 logical field (focal 256, principal point 128,96) scaled x GFX_SCALE,
// i.e. in canvas pixels: focal 512, centre (256,192). That bakes the 3D view to
// a small 4:3 rectangle. To let the 3D scene follow the real window size we make
// the optics a function of the live viewport instead of constants.
//
// The conversion preserves the legacy VERTICAL field of view (so the world feels
// the same), and reuses the SAME focal length horizontally, so a wider window
// reveals MORE world sideways rather than stretching it (circles stay round). At
// a 4:3 viewport the result is bit-identical to the legacy optics.
//
// Pure math over POD (no D3D, no globals), so it is unit-tested headlessly.

namespace Neuron::Client
{
  // The legacy vertical optic: focal 512 over a 192px half-height in canvas
  // pixels => tan(fovY/2) = 192/512 = 0.375  (fovY ~ 41 degrees).
  inline constexpr double kLegacyTanHalfFovY = 192.0 / 512.0;

  struct ViewMetrics
  {
    int    width  = 512;     // 3D viewport size in (canvas) pixels
    int    height = 384;
    double focal  = 512.0;   // focal length in pixels
    double cx     = 256.0;   // principal point (pixels), screen x grows right
    double cy     = 192.0;   // principal point (pixels), screen y grows DOWN
  };

  // Optics for a w x h viewport that keep the legacy vertical field of view.
  // cx/cy are the viewport centre; the same focal is used on both axes so the
  // horizontal field of view widens with the aspect ratio.
  [[nodiscard]] inline ViewMetrics MakeViewMetrics(int _w, int _h)
  {
    ViewMetrics v;
    v.width  = _w;
    v.height = _h;
    v.cx     = _w * 0.5;
    v.cy     = _h * 0.5;
    v.focal  = (_h * 0.5) / kLegacyTanHalfFovY;
    return v;
  }

  // Project a camera-space point (x right, y up, z forward, z>0) to screen pixels
  // (x right, y DOWN). The y axis is flipped here, matching the legacy "sy = -sy"
  // step. The caller is responsible for the z>0 / field-of-view culling.
  inline void ProjectPoint(const ViewMetrics& _v, double _x, double _y, double _z,
                           double& _outSx, double& _outSy)
  {
    _outSx =  (_x * _v.focal) / _z + _v.cx;
    _outSy = -(_y * _v.focal) / _z + _v.cy;
  }

  // Half-width / half-height of the view frustum at depth z, in world units. A
  // point is on screen when |x| <= HalfExtentX(v,z) and |y| <= HalfExtentY(v,z).
  // Replaces the legacy "fabs(x) > z" 90-degree check with the real frustum, so
  // the cull matches the now aspect-aware projection.
  [[nodiscard]] inline double HalfExtentX(const ViewMetrics& _v, double _z)
  {
    return (_v.cx * _z) / _v.focal;
  }
  [[nodiscard]] inline double HalfExtentY(const ViewMetrics& _v, double _z)
  {
    return (_v.cy * _z) / _v.focal;
  }
}
