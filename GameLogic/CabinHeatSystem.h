#pragma once

// CabinHeatSystem - stars, cabin heat, and fuel scooping (GameLogic, Track G4).
//
// Each system carries a Sun (a NetType::Sun body, placed by the world builder).
// StepCabinHeat() runs each tick: a ship inside a star's heat band warms up, cools
// down away from it, and - held at maximum - cooks: the hull loses energy straight
// off the bank (heat bypasses shields, the legacy zero-altitude analogue) until it
// dies. The payoff the fuel scoop was always missing: a scoop-fitted ship skimming
// the same band tops its tank, so "sun-skimming for fuel" finally works.
//
// Cabin heat is a player-borne stat (CabinHeat added at spawn), so the HUD can
// mirror it; NPCs, which never loiter by a star, are simply not heat-bearing.
// Pure GameLogic (mutates heat/energy/fuel, returns the cooked-to-death kills) and
// unit-tested headlessly like every other system.

#include <cstdint>
#include <vector>

#include "ECS.h"
#include "Vector3i64.h"

#include "SimComponents.h"
#include "CombatSystem.h"      // Combatant, Kill
#include "StationServices.h"   // Equipment (fuel scoop), Fuel

namespace Neuron::GameLogic
{
  // A star: a heat source. Its position is its WorldTransform; NetType::Sun draws it.
  struct Sun {};

  // Per-ship accumulated cabin temperature (0..CABIN_HEAT_MAX). Player-borne.
  struct CabinHeat { int temp = 0; };

  inline constexpr int64_t SUN_SYSTEM_OFFSET = 300000; // how far a system's sun sits from its planet
  inline constexpr int64_t SUN_HEAT_RADIUS   = 60000;  // Chebyshev band around a sun that heats
  inline constexpr int     CABIN_HEAT_MAX    = 255;    // legacy cabin_temp full-scale
  inline constexpr int     CABIN_HEAT_RISE   = 6;      // temp gained per tick inside the band
  inline constexpr int     CABIN_HEAT_FALL   = 3;      // temp shed per tick outside it
  inline constexpr int     CABIN_HEAT_DAMAGE = 4;      // energy cooked off per tick at max temp
  inline constexpr int     FUEL_SCOOP_GAIN   = 1;      // fuel tenths gained per tick scooping the band

  // Chebyshev distance between two int64 points (no large multiplies - safe on the
  // unbounded world coords).
  [[nodiscard]] inline int64_t ChebyshevDist(const Math::Vector3i64& _a, const Math::Vector3i64& _b)
  {
    const int64_t dx = _a.x > _b.x ? _a.x - _b.x : _b.x - _a.x;
    const int64_t dy = _a.y > _b.y ? _a.y - _b.y : _b.y - _a.y;
    const int64_t dz = _a.z > _b.z ? _a.z - _b.z : _b.z - _a.z;
    const int64_t m = dx > dy ? dx : dy;
    return m > dz ? m : dz;
  }

  // Advance cabin heat one tick. Rise toward MAX near any sun, fall otherwise; at MAX
  // cook the hull (energy off the bank, respecting spawn/respawn grace) and collect
  // any deaths; a scoop-fitted ship in the band tops its fuel. Returns the kills for
  // the caller to broadcast + destroy, exactly like StepCombat / StepMissiles.
  [[nodiscard]] inline std::vector<Kill> StepCabinHeat(ECS::Registry& _world)
  {
    std::vector<Kill> kills;

    // The stars (one per system; a handful in play at once). Gathered once so the
    // per-ship test is a small Chebyshev loop.
    std::vector<Math::Vector3i64> suns;
    _world.Each<Sun, WorldTransform>([&suns](ECS::EntityId, Sun&, WorldTransform& _t)
    {
      suns.push_back(_t.position);
    });
    if (suns.empty())
      return kills;

    _world.Each<CabinHeat, WorldTransform>([&](ECS::EntityId _id, CabinHeat& _h, WorldTransform& _t)
    {
      int64_t nearest = INT64_MAX;
      for (const Math::Vector3i64& s : suns)
      {
        const int64_t d = ChebyshevDist(s, _t.position);
        if (d < nearest)
          nearest = d;
      }
      const bool inBand = nearest <= SUN_HEAT_RADIUS;

      if (inBand)
      {
        _h.temp += CABIN_HEAT_RISE;
        if (_h.temp > CABIN_HEAT_MAX) _h.temp = CABIN_HEAT_MAX;

        // Fuel scoop payoff: a scoop-fitted ship skimming the band tops its tank.
        if (const Equipment* eq = _world.TryGet<Equipment>(_id); eq != nullptr && eq->fuelScoop)
          if (Fuel* f = _world.TryGet<Fuel>(_id); f != nullptr && f->tenths < f->max)
          {
            f->tenths += FUEL_SCOOP_GAIN;
            if (f->tenths > f->max) f->tenths = f->max;
          }
      }
      else
      {
        _h.temp -= CABIN_HEAT_FALL;
        if (_h.temp < 0) _h.temp = 0;
      }

      // Cook the hull at sustained maximum: heat bypasses shields and drains the
      // energy bank directly (the legacy analogue), respecting spawn grace.
      if (_h.temp >= CABIN_HEAT_MAX)
        if (Combatant* c = _world.TryGet<Combatant>(_id); c != nullptr && c->invulnTicks <= 0)
        {
          c->energy -= CABIN_HEAT_DAMAGE;
          if (c->energy <= 0)
            kills.push_back(Kill{ _id, _id.index });   // environment death, self-credited
        }
    });

    return kills;
  }
}
