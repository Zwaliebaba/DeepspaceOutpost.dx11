#pragma once

// StrategicView - aggregate the live world into per-system strategic counts
// (GameLogic, server-side, Track E3).
//
// The strategic tier's data source: sweep the combatants near a system's center and
// tally the player faction vs. hostiles. Pure (reads the world, returns counts), so
// it is unit-tested headlessly; the server calls it at the slow strategic cadence
// and ships the result as a Msg::StrategicSummary.

#include <cstdint>

#include "ECS.h"
#include "SimComponents.h"    // WorldTransform, Math::Vector3i64
#include "CombatSystem.h"     // Combatant, Team

namespace Neuron::GameLogic
{
  // Strategic radius: entities within this Chebyshev distance of a system's center
  // count toward that system's rollup. Much wider than the tactical AOI (a system's
  // worth of space), still far short of interstellar gaps, so systems don't bleed
  // into each other.
  inline constexpr int64_t STRATEGIC_RADIUS = 8'000'000;

  struct StrategicCounts
  {
    uint16_t friendly = 0;   // player-faction combatants
    uint16_t hostile = 0;    // pirate/hostile combatants
  };

  // Tally combatants within `_radius` of `_center` (per-axis Chebyshev, overflow-safe
  // on absolute int64 coords like the AOI). The player faction is friendly; pirates
  // are hostile; police, traders and stations are neutral and not counted. Counts
  // saturate at u16 (a system with 65k combatants is not a real scenario).
  [[nodiscard]] inline StrategicCounts SummarizeStrategic(ECS::Registry& _world,
                                                          const Math::Vector3i64& _center,
                                                          int64_t _radius = STRATEGIC_RADIUS)
  {
    StrategicCounts counts;
    _world.Each<WorldTransform, Combatant>([&](ECS::EntityId, WorldTransform& _t, Combatant& _c)
    {
      const int64_t dx = _t.position.x - _center.x;
      const int64_t dy = _t.position.y - _center.y;
      const int64_t dz = _t.position.z - _center.z;
      const int64_t ax = dx < 0 ? -dx : dx;
      const int64_t ay = dy < 0 ? -dy : dy;
      const int64_t az = dz < 0 ? -dz : dz;
      if (ax > _radius || ay > _radius || az > _radius)
        return;

      if (_c.team == Team::Player)
      {
        if (counts.friendly < 0xFFFF)
          ++counts.friendly;
      }
      else if (_c.team == Team::Pirate)
      {
        if (counts.hostile < 0xFFFF)
          ++counts.hostile;
      }
    });
    return counts;
  }
}
