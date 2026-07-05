#pragma once

// MoveGizmo - the pure geometry behind Track I3's Homeworld-style move gizmo
// (docs/interaction.md §3.4). Turning a 2D command click into a 3D destination:
//
//   1. A command PLANE through the selected ship, normal = the camera's up
//      vector (so it always faces the view; there is no gravity plane in space).
//   2. The cursor RAY (unprojected by the client through DirectXMath) meets the
//      plane at the in-plane point; near-grazing rays clamp to a max command
//      range instead of shooting to infinity.
//   3. A drag before release slides the marker along the plane NORMAL (the
//      vertical stem) - the third axis.
//   4. The destination is Chebyshev-clamped to the server's Move reach so the
//      client request can never exceed the server gate (matches cursor_to_move_
//      point's +/-1e6 clamp).
//
// Pure C++ doubles, no DirectXMath, no Win32 - the client glue does the
// unproject (View*Projection inverse) and passes the resulting world-space ray
// in the floating-origin frame; this header does the plane math so it is pinned
// by the headless suite rather than verified only in-app.

#include <cmath>

namespace Neuron::Input
{
  struct GVec3
  {
    double x = 0.0, y = 0.0, z = 0.0;
  };

  inline GVec3   operator+(GVec3 _a, GVec3 _b) { return {_a.x + _b.x, _a.y + _b.y, _a.z + _b.z}; }
  inline GVec3   operator-(GVec3 _a, GVec3 _b) { return {_a.x - _b.x, _a.y - _b.y, _a.z - _b.z}; }
  inline GVec3   operator*(GVec3 _a, double _s) { return {_a.x * _s, _a.y * _s, _a.z * _s}; }
  inline double  Dot(GVec3 _a, GVec3 _b) { return _a.x * _b.x + _a.y * _b.y + _a.z * _b.z; }
  inline double  Length(GVec3 _a) { return std::sqrt(Dot(_a, _a)); }

  inline GVec3 Normalized(GVec3 _a)
  {
    const double n = Length(_a);
    return n > 0.0 ? GVec3{_a.x / n, _a.y / n, _a.z / n} : GVec3{0, 0, 0};
  }

  // Project a vector onto the plane whose normal is `_n` (assumed unit-ish).
  inline GVec3 ProjectOntoPlane(GVec3 _v, GVec3 _n)
  {
    const GVec3 nu = Normalized(_n);
    return _v - nu * Dot(_v, nu);
  }

  // Defaults matching the design + server gate.
  inline constexpr double GIZMO_MAX_RANGE   = 500000.0;  // §3.4 in-plane command range
  inline constexpr double SERVER_MOVE_REACH = 1000000.0; // server Chebyshev clamp (+/-1e6)
  inline constexpr double GIZMO_GRAZE_EPS   = 1e-4;       // |N.D| below this = grazing

  struct PlaneHit
  {
    GVec3 point{};              // the in-plane destination (relative frame)
    bool  clampedToRange = false; // grazing or too-far -> clamped to maxRange
  };

  // Intersect the cursor ray with the command plane through the ship. `_rayDir`
  // need not be normalized. A ray parallel to / pointing away from the plane, or
  // an intersection farther than `_maxRange` from the ship, clamps to a point at
  // `_maxRange` along the in-plane projection of the ray (never to infinity).
  inline PlaneHit RayPlanePoint(GVec3 _rayOrigin, GVec3 _rayDir, GVec3 _shipPos,
                                GVec3 _planeNormal, double _maxRange = GIZMO_MAX_RANGE)
  {
    const GVec3 n = Normalized(_planeNormal);
    const double denom = Dot(n, _rayDir);

    auto grazeFallback = [&]() -> PlaneHit {
      // Slide out along the ray's in-plane component to the max range.
      GVec3 inPlane = Normalized(ProjectOntoPlane(_rayDir, n));
      if (Length(inPlane) == 0.0) return {_shipPos, true}; // pathological: dead centre
      return {_shipPos + inPlane * _maxRange, true};
    };

    if (std::fabs(denom) < GIZMO_GRAZE_EPS) return grazeFallback();

    const double t = Dot(n, _shipPos - _rayOrigin) / denom;
    if (t <= 0.0) return grazeFallback(); // plane is behind the ray

    GVec3 hit = _rayOrigin + _rayDir * t;
    const GVec3 fromShip = hit - _shipPos;
    const double dist = Length(fromShip);
    if (dist > _maxRange)
      return {_shipPos + Normalized(fromShip) * _maxRange, true};
    return {hit, false};
  }

  // Slide a plane point along the plane normal by `_elevation` (the vertical
  // stem from the drag). Positive = along +normal.
  inline GVec3 ApplyElevation(GVec3 _planePoint, GVec3 _planeNormal, double _elevation)
  {
    return _planePoint + Normalized(_planeNormal) * _elevation;
  }

  // Chebyshev-clamp a destination to +/- `_reach` per axis RELATIVE to the ship,
  // so the client's UnitOrder{Move} can never exceed the server's range gate.
  inline GVec3 ClampReach(GVec3 _dest, GVec3 _shipPos, double _reach = SERVER_MOVE_REACH)
  {
    auto clamp = [_reach](double _v) { return _v < -_reach ? -_reach : (_v > _reach ? _reach : _v); };
    const GVec3 rel = _dest - _shipPos;
    return {_shipPos.x + clamp(rel.x), _shipPos.y + clamp(rel.y), _shipPos.z + clamp(rel.z)};
  }

  // The full pipeline: ray -> plane point -> elevation -> server-reach clamp.
  inline GVec3 ComputeMoveTarget(GVec3 _rayOrigin, GVec3 _rayDir, GVec3 _shipPos,
                                 GVec3 _planeNormal, double _elevation,
                                 double _maxRange = GIZMO_MAX_RANGE,
                                 double _reach = SERVER_MOVE_REACH)
  {
    const PlaneHit hit = RayPlanePoint(_rayOrigin, _rayDir, _shipPos, _planeNormal, _maxRange);
    const GVec3 raised = ApplyElevation(hit.point, _planeNormal, _elevation);
    return ClampReach(raised, _shipPos, _reach);
  }
}
