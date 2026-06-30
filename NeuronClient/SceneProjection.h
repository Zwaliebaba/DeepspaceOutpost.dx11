#pragma once

// SceneProjection - a GPU perspective matrix that reproduces the legacy software
// projection (ViewMetrics / ProjectPoint) on the Direct3D 11 pipeline.
//
// The legacy 3D scene is projected on the CPU (threed.cpp -> ProjectPoint) into
// 2D pixels. The GPU migration (see 3Dmigration.md) instead feeds camera-space
// geometry to a vertex shader with a perspective matrix and lets the rasterizer +
// z-buffer do the work. For the "faithful" look to hold, that matrix - after the
// standard D3D11 viewport transform - must land each point on the SAME pixel
// ProjectPoint produces. This header is that matrix, derived directly from the
// view optics, and is pure math (no D3D, no DirectXMath) so it is unit-tested
// headlessly against ProjectPoint before any Windows/GPU run.
//
// Conventions:
//   - Camera space is x right, y up, z forward (z > 0 in front) - a left-handed
//     frame, which is D3D11's native NDC handedness.
//   - The matrix is row-major; element (r,c) = m[r*4 + c]. It maps a column
//     vector: clip = M * (x, y, z, 1)^T. When uploaded to HLSL, multiply as
//     mul(M, float4(pos,1)) with a row_major cbuffer, or transpose and use
//     mul(float4(pos,1), MT) - Scene3D (Phase 2) owns that detail.
//   - D3D11 viewport transform assumed by ProjectThroughMatrix:
//        px = (ndcx * 0.5 + 0.5) * width
//        py = (0.5 - ndcy * 0.5) * height     (y flips: +y up -> smaller py)
//        depth = ndcz in [0, 1]
//
// Values are double for test precision; Scene3D narrows to float when it builds
// the actual constant buffer.

#include <array>

#include "ViewMetrics.h"

namespace Neuron::Client
{
  // Row-major 4x4 matrix over doubles. element(r,c) = m[r*4 + c].
  struct Matrix4
  {
    std::array<double, 16> m{};

    [[nodiscard]] double  operator()(int _r, int _c) const { return m[static_cast<size_t>(_r) * 4 + _c]; }
    [[nodiscard]] double& operator()(int _r, int _c) { return m[static_cast<size_t>(_r) * 4 + _c]; }
  };

  // Build a left-handed perspective projection from the view optics. A camera
  // point projected through this matrix and the D3D11 viewport (see header notes)
  // lands on exactly ProjectPoint(_v, ...) in pixels, at any viewport size, and a
  // z in [_zNear, _zFar] maps monotonically to depth in [0, 1]. An off-centre
  // principal point (cx != width/2, cy != height/2) is handled via the (0,2)/(1,2)
  // skew terms; MakeViewMetrics centres it, so those terms are zero in practice.
  [[nodiscard]] inline Matrix4 MakeScenePerspective(const ViewMetrics& _v, double _zNear, double _zFar)
  {
    const double w = static_cast<double>(_v.width);
    const double h = static_cast<double>(_v.height);
    const double f = _v.focal;

    Matrix4 p; // zero-initialised
    p(0, 0) = 2.0 * f / w;
    p(0, 2) = 2.0 * _v.cx / w - 1.0;
    p(1, 1) = 2.0 * f / h;
    p(1, 2) = 1.0 - 2.0 * _v.cy / h;
    p(2, 2) = _zFar / (_zFar - _zNear);
    p(2, 3) = -(_zNear * _zFar) / (_zFar - _zNear);
    p(3, 2) = 1.0; // clip.w = z (perspective divide)
    return p;
  }

  // Matrix product _a * _b (row-major, column-vector convention): the result maps a
  // column vector as (_a * _b) * v == _a * (_b * v). Used to fold a model->view
  // matrix into the projection (MVP = proj * modelView).
  [[nodiscard]] inline Matrix4 Mul(const Matrix4& _a, const Matrix4& _b)
  {
    Matrix4 out; // zero
    for (int r = 0; r < 4; ++r)
      for (int c = 0; c < 4; ++c)
      {
        double sum = 0.0;
        for (int k = 0; k < 4; ++k)
          sum += _a(r, k) * _b(k, c);
        out(r, c) = sum;
      }
    return out;
  }

  // Project a camera-space point through _proj and the assumed D3D11 viewport,
  // yielding pixel (_outSx, _outSy) and NDC depth (_outDepth). This mirrors what
  // the GPU does; tests compare _outSx/_outSy against ProjectPoint. Caller ensures
  // the point is in front of the eye (clip.w = z > 0).
  inline void ProjectThroughMatrix(const Matrix4& _proj, const ViewMetrics& _v, double _x, double _y, double _z,
                                   double& _outSx, double& _outSy, double& _outDepth)
  {
    const double cx = _proj(0, 0) * _x + _proj(0, 1) * _y + _proj(0, 2) * _z + _proj(0, 3);
    const double cy = _proj(1, 0) * _x + _proj(1, 1) * _y + _proj(1, 2) * _z + _proj(1, 3);
    const double cz = _proj(2, 0) * _x + _proj(2, 1) * _y + _proj(2, 2) * _z + _proj(2, 3);
    const double cw = _proj(3, 0) * _x + _proj(3, 1) * _y + _proj(3, 2) * _z + _proj(3, 3);

    const double ndcx = cx / cw;
    const double ndcy = cy / cw;
    const double ndcz = cz / cw;

    _outSx = (ndcx * 0.5 + 0.5) * static_cast<double>(_v.width);
    _outSy = (0.5 - ndcy * 0.5) * static_cast<double>(_v.height);
    _outDepth = ndcz;
  }
}
