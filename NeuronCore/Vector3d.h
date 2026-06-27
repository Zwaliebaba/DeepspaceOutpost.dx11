#pragma once

// Vector3d - a double-precision 3D vector for the local (small-magnitude) frame.
//
// Absolute world positions are int64 (Vector3i64); orientation, velocities and
// the floating-origin render delta are naturally fractional and small, so they
// use doubles. This is the minimal vector algebra the server flight model and
// the client render frame need: it intentionally mirrors the legacy `struct
// vector` (three doubles) so ported math reads the same.

#include <cmath>

namespace Neuron::Math
{
  struct Vector3d
  {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    [[nodiscard]] Vector3d operator+(const Vector3d& _o) const { return { x + _o.x, y + _o.y, z + _o.z }; }
    [[nodiscard]] Vector3d operator-(const Vector3d& _o) const { return { x - _o.x, y - _o.y, z - _o.z }; }
    [[nodiscard]] Vector3d operator*(double _s) const { return { x * _s, y * _s, z * _s }; }

    Vector3d& operator+=(const Vector3d& _o) { x += _o.x; y += _o.y; z += _o.z; return *this; }
    Vector3d& operator-=(const Vector3d& _o) { x -= _o.x; y -= _o.y; z -= _o.z; return *this; }
  };

  [[nodiscard]] inline double Dot(const Vector3d& _a, const Vector3d& _b)
  {
    return _a.x * _b.x + _a.y * _b.y + _a.z * _b.z;
  }

  [[nodiscard]] inline Vector3d Cross(const Vector3d& _a, const Vector3d& _b)
  {
    return {
      _a.y * _b.z - _a.z * _b.y,
      _a.z * _b.x - _a.x * _b.z,
      _a.x * _b.y - _a.y * _b.x,
    };
  }

  [[nodiscard]] inline double Length(const Vector3d& _v)
  {
    return std::sqrt(_v.x * _v.x + _v.y * _v.y + _v.z * _v.z);
  }

  // Unit vector in the direction of `_v`. `_v` must be non-zero.
  [[nodiscard]] inline Vector3d Normalized(const Vector3d& _v)
  {
    const double len = Length(_v);
    return { _v.x / len, _v.y / len, _v.z / len };
  }
}
