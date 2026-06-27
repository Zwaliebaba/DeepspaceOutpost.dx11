#pragma once

// MotionSystem - advance entity positions one fixed simulation tick.
//
// Every entity carrying both WorldTransform and Velocity moves by its per-tick
// velocity. Pure integer arithmetic, so the result is fully deterministic
// (essential for replays, golden tests, and reproducible server behaviour).

#include "ECS.h"

#include "SimComponents.h"

namespace Neuron::GameLogic
{
  inline void StepMotion(ECS::Registry& _world)
  {
    _world.Each<WorldTransform, Velocity>([](ECS::EntityId, WorldTransform& _t, Velocity& _v)
    {
      _t.position += _v.perTick;
    });
  }
}
