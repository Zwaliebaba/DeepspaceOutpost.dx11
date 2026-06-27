#pragma once

// CombatSystem - the server's realtime combat tick (GameLogic).
//
// Promotes the ported, unit-tested Combat.h primitives from "library functions"
// to a live authoritative system over the int64 world. Each tick every Combatant
// fires on the nearest ENEMY (different team) within range, damage resolves
// simultaneously through LaserDamageTo(), and anything driven to zero energy dies.
// StepCombat() returns the kills (victim + killer) so the server can broadcast
// reliable death events and despawn the wreck - the system itself stays pure (it
// only mutates energy), so it is unit-tested headlessly.

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "ECS.h"
#include "Vector3i64.h"

#include "SimComponents.h"
#include "Combat.h"

namespace Neuron::GameLogic
{
  // A combatant in the realtime sim: its team, energy pool, weapon strength and
  // engagement range (world units, Chebyshev - overflow-safe on absolute coords).
  struct Combatant
  {
    int team = 0;
    int energy = 100;
    int laserStrength = 10;
    int64_t range = 5000;
  };

  // A resolved kill this tick: the victim handle (for the caller to destroy) and
  // the killer's entity index (for the death event).
  struct Kill
  {
    ECS::EntityId victim;
    uint32_t killer = 0;
  };

  // Advance combat one tick. Returns the kills; the caller destroys the victims
  // and broadcasts death events.
  [[nodiscard]] inline std::vector<Kill> StepCombat(ECS::Registry& _world)
  {
    struct Unit
    {
      ECS::EntityId id;
      Math::Vector3i64 pos;
      Combatant* c;
    };

    std::vector<Unit> units;
    _world.Each<WorldTransform, Combatant>([&units](ECS::EntityId _id, WorldTransform& _t, Combatant& _c)
    {
      units.push_back(Unit{ _id, _t.position, &_c });
    });

    // Accumulate this tick's damage and the attacker that dealt it, so resolution
    // is simultaneous (firing order doesn't matter).
    std::unordered_map<uint32_t, int> damage;
    std::unordered_map<uint32_t, uint32_t> attacker;

    for (const Unit& a : units)
    {
      const Unit* best = nullptr;
      int64_t bestDist2 = 0;
      for (const Unit& b : units)
      {
        if (b.c->team == a.c->team)
          continue;   // never target allies

        const int64_t dx = b.pos.x - a.pos.x;
        const int64_t dy = b.pos.y - a.pos.y;
        const int64_t dz = b.pos.z - a.pos.z;

        // In-range test is Chebyshev (no large multiplies on absolute coords);
        // only then is the squared distance computed, and only over the small
        // in-range delta, so it cannot overflow.
        const int64_t ax = dx < 0 ? -dx : dx;
        const int64_t ay = dy < 0 ? -dy : dy;
        const int64_t az = dz < 0 ? -dz : dz;
        if (ax > a.c->range || ay > a.c->range || az > a.c->range)
          continue;

        const int64_t dist2 = dx * dx + dy * dy + dz * dz;
        if (best == nullptr || dist2 < bestDist2)
        {
          best = &b;
          bestDist2 = dist2;
        }
      }

      if (best != nullptr)
      {
        damage[best->id.index] += LaserDamageTo(TargetClass::Normal, a.c->laserStrength);
        attacker[best->id.index] = a.id.index;
      }
    }

    // Apply damage, then collect deaths.
    std::vector<Kill> kills;
    for (const Unit& u : units)
    {
      const auto it = damage.find(u.id.index);
      if (it == damage.end())
        continue;

      u.c->energy -= it->second;
      if (u.c->energy <= 0)
        kills.push_back(Kill{ u.id, attacker[u.id.index] });
    }

    return kills;
  }
}
