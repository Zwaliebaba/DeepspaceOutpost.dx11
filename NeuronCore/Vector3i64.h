#pragma once

// Vector3i64 - absolute 64-bit integer world coordinate (A3).
//
// The MMO world is one continuous int64^3 space (no precision loss across a huge
// field). The authoritative server stores positions as Vector3i64. The client
// renders in 32-bit float relative to a *floating origin* near the camera:
// RelativeTo() produces the (small) offset of a world point from that origin, so
// existing 32-bit/float render math is reused without large-coordinate jitter.
//
// Header-only, std-only - usable from any module and trivially testable.

#include <cstdint>

namespace Neuron::Math
{
  struct Vector3i64
  {
    int64_t x = 0;
    int64_t y = 0;
    int64_t z = 0;

    [[nodiscard]] Vector3i64 operator+(const Vector3i64& _o) const { return { x + _o.x, y + _o.y, z + _o.z }; }
    [[nodiscard]] Vector3i64 operator-(const Vector3i64& _o) const { return { x - _o.x, y - _o.y, z - _o.z }; }

    Vector3i64& operator+=(const Vector3i64& _o) { x += _o.x; y += _o.y; z += _o.z; return *this; }
    Vector3i64& operator-=(const Vector3i64& _o) { x -= _o.x; y -= _o.y; z -= _o.z; return *this; }

    [[nodiscard]] bool operator==(const Vector3i64& _o) const { return x == _o.x && y == _o.y && z == _o.z; }
    [[nodiscard]] bool operator!=(const Vector3i64& _o) const { return !(*this == _o); }
  };

  // Offset of `_world` from `_origin` as doubles, for the client's local render
  // frame after rebasing to a floating origin near the camera. The delta is
  // expected to be small (within an interest cell), so it fits the float range
  // even though the absolute coordinates do not.
  inline void RelativeTo(const Vector3i64& _world, const Vector3i64& _origin,
                         double& _dx, double& _dy, double& _dz)
  {
    _dx = static_cast<double>(_world.x - _origin.x);
    _dy = static_cast<double>(_world.y - _origin.y);
    _dz = static_cast<double>(_world.z - _origin.z);
  }
}
