#pragma once

// LaunchSystem - the gentle undock fly-out (GameLogic).
//
// A freshly undocked hull carries a transient LaunchCruise component (added by the
// station Undock handler). While it is present, StepLaunchCruise forces a straight,
// level outward throttle - overriding the player's normally-zero FlightIntent - so
// the ship eases out of the station bay under power instead of teleporting clear.
// Once the hull has travelled its launch distance the component is removed and the
// throttle drops to zero, handing control back to the player.
//
// StepLaunchCruise() runs FIRST inside GameLogic::Tick, before StepFlightInput(),
// so the launch throttle it writes is the intent StepFlightInput integrates that
// same tick.

#include "ECS.h"

#include "SimComponents.h"
#include "FlightInput.h"

namespace Neuron::GameLogic
{
  // Drive every hull carrying a LaunchCruise outward until it clears the bay, then
  // release it. Overwrites the ship's FlightIntent (throttle up, wings level) for
  // the duration; StepFlightInput turns that into speed the same tick.
  inline void StepLaunchCruise(ECS::Registry& _world)
  {
    // Collect the hulls that have finished launching; removing a component mid-Each
    // would mutate the set being iterated. LaunchCruise is the rarest component, so
    // iterate it and probe the rest.
    std::vector<ECS::EntityId> done;

    _world.Each<LaunchCruise>([&](ECS::EntityId _id, LaunchCruise& _lc)
    {
      FlightIntent* intent = _world.TryGet<FlightIntent>(_id);
      const WorldTransform* t = _world.TryGet<WorldTransform>(_id);
      if (intent == nullptr || t == nullptr)
      {
        done.push_back(_id);   // hull lost its flight state: drop the launch
        return;
      }

      // Chebyshev distance from the launch origin (the ship flies straight out
      // along +nose, so this tracks how far it has cleared the station).
      const int64_t dx = t->position.x > _lc.origin.x ? t->position.x - _lc.origin.x : _lc.origin.x - t->position.x;
      const int64_t dy = t->position.y > _lc.origin.y ? t->position.y - _lc.origin.y : _lc.origin.y - t->position.y;
      const int64_t dz = t->position.z > _lc.origin.z ? t->position.z - _lc.origin.z : _lc.origin.z - t->position.z;
      const int64_t travelled = dx > dy ? (dx > dz ? dx : dz) : (dy > dz ? dy : dz);

      if (travelled >= _lc.distance)
      {
        intent->throttle = 0.0;   // arrived: coast to rest, restore player control
        done.push_back(_id);
        return;
      }

      // Straight, level cruise out of the bay.
      intent->rollAxis = 0.0;
      intent->pitchAxis = 0.0;
      intent->throttle = _lc.throttle;
    });

    for (const ECS::EntityId id : done)
      _world.Remove<LaunchCruise>(id);
  }
}
