#pragma once

// SpawnDirector - server-side dynamic ship spawning (GameLogic).
//
// Brings the legacy single-player "random encounters" and "police response" to
// the authoritative server: it periodically spawns NPC pirates near players (up
// to a cap) and, on demand, spawns police to hunt a criminal. Spawning is driven
// by a deterministic LCG (no wall-clock RNG, which the engine forbids), so the
// behaviour is reproducible and unit-testable.
//
// Spawned NPCs are Combatants with autoEngage = true, so the existing combat tick
// makes them attack their enemies; players (autoEngage = false) only fight back on
// command.

#include <cstdint>
#include <vector>

#include "ECS.h"
#include "Vector3i64.h"

#include "SimComponents.h"
#include "CombatSystem.h"

namespace Neuron::GameLogic
{
  class SpawnDirector
  {
  public:
    SpawnDirector(uint32_t _seed, int _intervalTicks, int _maxNpcs)
      : m_rng(_seed != 0 ? _seed : 1u), m_interval(_intervalTicks), m_maxNpcs(_maxNpcs)
    {
    }

    // Every `_intervalTicks`, spawn one pirate near a random player while under
    // the NPC cap. Returns the spawned entity (invalid if nothing spawned).
    ECS::EntityId Step(ECS::Registry& _world, uint32_t _tick)
    {
      if (m_interval <= 0 || (_tick % static_cast<uint32_t>(m_interval)) != 0)
        return ECS::EntityId{};

      std::vector<Math::Vector3i64> players;
      _world.Each<WorldTransform, PlayerTag>([&players](ECS::EntityId, WorldTransform& _t, PlayerTag&)
      {
        players.push_back(_t.position);
      });
      if (players.empty() || CountNpcs(_world) >= m_maxNpcs)
        return ECS::EntityId{};

      const Math::Vector3i64 anchor = players[NextRand() % players.size()];

      // Spawn at a distance, not on top of the player. Each axis offset is at least
      // +/-6000, so the Chebyshev distance always exceeds the pirate's 5000 engage range:
      // a fresh pirate appears as a dot and has to close in (and the player, with the
      // longer 6000 range, can fire first) instead of opening fire from point-blank the
      // instant it spawns. Spread is +/-[6000, 9000) per axis.
      auto axisOffset = [this]() -> int64_t {
        const uint32_t r = NextRand();
        const int64_t mag = 6000 + static_cast<int64_t>(r % 3000);   // [6000, 9000)
        return (r & 0x10000u) ? mag : -mag;                          // sign from a mid bit
      };
      const Math::Vector3i64 pos{
        anchor.x + axisOffset(),
        anchor.y + axisOffset(),
        anchor.z + axisOffset(),
      };

      const ECS::EntityId e = _world.Create();
      _world.Add<WorldTransform>(e, WorldTransform{ pos });
      _world.Add<Flight>(e, Flight{});
      // Range <= the player's laser range (6000) so the fight is symmetric: the
      // pirate can't shoot the player from outside the range the player can shoot
      // back. (Previously 8000 - the player got hit from where they couldn't reply.)
      _world.Add<Combatant>(e, Combatant{ Team::Pirate, /*energy*/ 80, /*laser*/ 3, /*range*/ 5000, /*autoEngage*/ true });
      return e;
    }

    // Spawn `_count` police near `_pos` to hunt a criminal (called when a player
    // commits a crime). Returns the spawned entities.
    std::vector<ECS::EntityId> SpawnPolice(ECS::Registry& _world, const Math::Vector3i64& _pos, int _count)
    {
      std::vector<ECS::EntityId> spawned;
      for (int i = 0; i < _count; ++i)
      {
        const ECS::EntityId e = _world.Create();
        _world.Add<WorldTransform>(e, WorldTransform{ { _pos.x + static_cast<int64_t>(i) * 300, _pos.y, _pos.z } });
        _world.Add<Flight>(e, Flight{});
        _world.Add<Combatant>(e, Combatant{ Team::Police, /*energy*/ 120, /*laser*/ 4, /*range*/ 6000, /*autoEngage*/ true });
        spawned.push_back(e);
      }
      return spawned;
    }

    // Number of auto-engaging NPCs currently alive (pirates + police).
    [[nodiscard]] int CountNpcs(ECS::Registry& _world) const
    {
      int n = 0;
      _world.Each<Combatant>([&n](ECS::EntityId, Combatant& _c)
      {
        if (_c.autoEngage)
          ++n;
      });
      return n;
    }

  private:
    uint32_t NextRand()
    {
      m_rng = m_rng * 1664525u + 1013904223u;   // Numerical Recipes LCG
      return m_rng;
    }

    uint32_t m_rng;
    int m_interval;
    int m_maxNpcs;
  };
}
