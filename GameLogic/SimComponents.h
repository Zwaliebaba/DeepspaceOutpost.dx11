#pragma once

// Authoritative server-side simulation components (GameLogic, A4).
//
// These live in absolute int64 world space (the A3 model) - the server is
// authoritative on them. They are intentionally separate from the client's
// render-oriented components: the server simulates, the client renders
// replicated state. Plain data aggregates.

#include "Vector3i64.h"

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
}
