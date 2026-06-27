#pragma once

// Authoritative server-side simulation components (GameLogic, A4).
//
// These live in absolute int64 world space (the A3 model) - the server is
// authoritative on them. They are intentionally separate from the client's
// render-oriented components: the server simulates, the client renders
// replicated state. Plain data aggregates.

#include "Vector3i64.h"
#include "Vector3d.h"

namespace Neuron::GameLogic
{
  // Absolute position in the int64 world.
  struct WorldTransform
  {
    Math::Vector3i64 position{};
  };

  // World units added to the position each fixed simulation tick.
  struct Velocity
  {
    Math::Vector3i64 perTick{};
  };

  // Renderable ship-type ids, matching the client's legacy SHIP_* values so the
  // replicated type maps straight onto a model.
  namespace ShipType
  {
    inline constexpr int Sun = -2;
    inline constexpr int Planet = -1;
    inline constexpr int Coriolis = 2;
    inline constexpr int Viper = 16;
  }

  // The model an entity is drawn as (replicated to the client). Absent => 0,
  // which the client draws as a default ship.
  struct NetType
  {
    int type = 0;
  };

  // Authoritative flight state of a steerable craft, in absolute world space.
  //
  // This is the server-side inverse of the legacy player-relative model: instead
  // of rotating the whole world past a fixed ship, the ship carries its own
  // orthonormal basis (side/roof/nose, nose = the forward travel direction) and
  // moves through a static world. roll/pitch are the per-tick rotation rates
  // (legacy alpha = roll/256, beta = climb/256) and speed is world units per tick
  // along the nose. `carry` holds the sub-unit movement remainder so a slow ship
  // still advances deterministically against the integer position.
  struct Flight
  {
    Math::Vector3d side{ 1.0, 0.0, 0.0 };   // right
    Math::Vector3d roof{ 0.0, 1.0, 0.0 };   // up
    Math::Vector3d nose{ 0.0, 0.0, 1.0 };   // forward (direction of travel)

    double roll = 0.0;     // per-tick roll increment  (legacy alpha)
    double pitch = 0.0;    // per-tick pitch increment  (legacy beta)
    double speed = 0.0;    // world units per tick along nose

    Math::Vector3d carry{ 0.0, 0.0, 0.0 };  // sub-unit position remainder
  };
}
