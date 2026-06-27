#pragma once

// GameLogic - the server-only authoritative simulation library (A4).
//
// All game *behaviour* lives here (currently: motion; AI/tactics, combat,
// economy, missions, spawning land here as the ported logic is decomposed into
// ECS systems). It is headless (no DX11/audio/UI) and depends only on the
// header-only engine pieces in NeuronCore (ECS, Vector3i64). The client links
// NONE of this - it renders replicated state. There is no shared game-logic lib.

#include <cstdint>

#include "ECS.h"

#include "SimComponents.h"
#include "MotionSystem.h"
#include "Economy.h"
#include "Galaxy.h"

namespace Neuron::GameLogic
{
  // Library version (a real symbol so the static lib is non-trivial; defined in
  // GameLogic.cpp). Heavier, non-inline systems will live in the .cpp too.
  [[nodiscard]] uint32_t Version();

  // Advance the authoritative world by one fixed simulation tick. Systems run in
  // a fixed, deterministic order; more are appended here as behaviour moves in.
  inline void Tick(ECS::Registry& _world)
  {
    StepMotion(_world);
  }
}
