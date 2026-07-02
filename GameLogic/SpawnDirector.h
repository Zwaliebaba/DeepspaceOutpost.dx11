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
#include "FlightInput.h"     // FlightIntent, FlightCaps (NPCs fly by intent, G5)
#include "CombatSystem.h"
#include "AiSystem.h"        // AiPilot, NpcFlightCaps, TradeLane
#include "StationServices.h" // NearestStation (trader lanes run station <-> planet)
#include "CollisionSystem.h" // PLANET_LANE_GATE (lanes stop clear of the kill radius)

namespace Neuron::GameLogic
{
  // Ambient-traffic knobs (stage 5): how often a trader may launch onto a lane,
  // and how many fly at once (they despawn on docking, so traffic trickles).
  inline constexpr uint32_t TRADER_SPAWN_INTERVAL = 900;   // ~30s at 30 Hz
  inline constexpr int MAX_TRADERS = 2;

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
      // Render as a ship (not the default type-0 model) and carry a bounty so a
      // kill pays out.
      _world.Add<NetType>(e, NetType{ ShipType::Viper });
      _world.Add<Bounty>(e, Bounty{ PIRATE_BOUNTY });
      // G5: pirates fly by intent like everyone else - the AI writes FlightIntent,
      // the shared input/flight systems integrate it. Bravery in the legacy
      // hunter band [64, 127]; a couple of panic missiles.
      _world.Add<FlightIntent>(e, FlightIntent{});
      _world.Add<FlightCaps>(e, NpcFlightCaps());
      _world.Add<AiPilot>(e, AiPilot{ /*bravery*/ 64 + static_cast<int>(NextRand() % 64u),
                                      /*missiles*/ 2, /*maxEnergy*/ 80 });
      return e;
    }

    // Spawn `_count` police near `_pos` to hunt a criminal (called when a player
    // commits a crime). Each spawned Viper is FIXED on `_offenderIndex` (stage 4
    // target memory): it chases and fires on the actual offender, not whoever is
    // nearest, until that lock dies clean or escapes. Returns the spawned entities.
    std::vector<ECS::EntityId> SpawnPolice(ECS::Registry& _world, const Math::Vector3i64& _pos, int _count,
                                           uint32_t _offenderIndex = ECS::INVALID_INDEX)
    {
      std::vector<ECS::EntityId> spawned;
      for (int i = 0; i < _count; ++i)
      {
        const ECS::EntityId e = _world.Create();
        _world.Add<WorldTransform>(e, WorldTransform{ { _pos.x + static_cast<int64_t>(i) * 300, _pos.y, _pos.z } });
        _world.Add<Flight>(e, Flight{});
        _world.Add<Combatant>(e, Combatant{ Team::Police, /*energy*/ 120, /*laser*/ 4, /*range*/ 6000, /*autoEngage*/ true });
        _world.Get<Combatant>(e).focus = _offenderIndex;   // the dispatch warrant
        // Render as a ship (not the default type-0 model). No bounty: killing the
        // police is a crime, not a payday.
        _world.Add<NetType>(e, NetType{ ShipType::Viper });
        // G5: police fly by intent too - legacy station-Viper bravery (113), one
        // missile in the rack.
        _world.Add<FlightIntent>(e, FlightIntent{});
        _world.Add<FlightCaps>(e, NpcFlightCaps());
        _world.Add<AiPilot>(e, AiPilot{ /*bravery*/ 113, /*missiles*/ 1, /*maxEnergy*/ 120 });
        spawned.push_back(e);
      }
      return spawned;
    }

    // Spawn one ambient trader at `_from` flying the lane to `_to` (stage 5): a
    // slow, unarmed civilian that docks (despawns) at the far end and flees when
    // hurt. Attacking it is a crime; killing it drops loot but pays no bounty.
    ECS::EntityId SpawnTrader(ECS::Registry& _world, const Math::Vector3i64& _from,
                              const Math::Vector3i64& _to, int _hull)
    {
      const ECS::EntityId e = _world.Create();
      _world.Add<WorldTransform>(e, WorldTransform{ _from });
      _world.Add<Flight>(e, Flight{});
      _world.Add<FlightIntent>(e, FlightIntent{});
      _world.Add<FlightCaps>(e, FlightCaps{ NPC_MAX_TURN_RATE, NPC_MAX_TURN_RATE, TRADER_MAX_SPEED });
      _world.Add<Combatant>(e, Combatant{ Team::Trader, /*energy*/ 60, /*laser*/ 0, /*range*/ 1, /*autoEngage*/ false });
      _world.Add<AiPilot>(e, AiPilot{ /*bravery*/ 0, /*missiles*/ 0, /*maxEnergy*/ 60 });
      _world.Add<TradeLane>(e, TradeLane{ _to });
      _world.Add<NetType>(e, NetType{ _hull });
      return e;
    }

    // Every TRADER_SPAWN_INTERVAL ticks, put one shuttle/transporter on the
    // station <-> planet lane of a random player's system, while under the cap
    // (legacy launch_shuttle: stations trickled out civilian traffic). Returns
    // the spawned entity (invalid if nothing spawned).
    ECS::EntityId StepTraders(ECS::Registry& _world, uint32_t _tick)
    {
      if ((_tick % TRADER_SPAWN_INTERVAL) != 0 || CountTraders(_world) >= MAX_TRADERS)
        return ECS::EntityId{};

      std::vector<Math::Vector3i64> players;
      _world.Each<WorldTransform, PlayerTag>([&players](ECS::EntityId, WorldTransform& _t, PlayerTag&)
      {
        players.push_back(_t.position);
      });
      if (players.empty())
        return ECS::EntityId{};
      const Math::Vector3i64 anchor = players[NextRand() % players.size()];

      // The lane: the anchor's system station and the planet nearest to it.
      const ECS::EntityId station = NearestStation(_world, anchor, INT64_MAX / 4);
      const WorldTransform* st = (station.index != ECS::INVALID_INDEX) ? _world.TryGet<WorldTransform>(station) : nullptr;
      if (st == nullptr)
        return ECS::EntityId{};
      const Math::Vector3i64 stationPos = st->position;

      bool foundPlanet = false;
      Math::Vector3i64 planetPos{};
      int64_t bestDist = 0;
      _world.Each<WorldTransform, NetType>([&](ECS::EntityId, WorldTransform& _t, NetType& _nt)
      {
        if (_nt.type != ShipType::Planet)
          return;
        const int64_t ax = _t.position.x > stationPos.x ? _t.position.x - stationPos.x : stationPos.x - _t.position.x;
        const int64_t ay = _t.position.y > stationPos.y ? _t.position.y - stationPos.y : stationPos.y - _t.position.y;
        const int64_t az = _t.position.z > stationPos.z ? _t.position.z - stationPos.z : stationPos.z - _t.position.z;
        const int64_t d = ax / 2 + ay / 2 + az / 2;   // halved Manhattan: no overflow on far systems
        if (!foundPlanet || d < bestDist)
        {
          foundPlanet = true;
          planetPos = _t.position;
          bestDist = d;
        }
      });
      if (!foundPlanet)
        return ECS::EntityId{};

      // The planet end of the lane is a GATE above the surface, clear of the G6
      // kill radius - traders "land" there and despawn instead of burning up.
      const Math::Vector3i64 planetGate = planetPos + Math::Vector3i64{ 0, PLANET_LANE_GATE, 0 };

      // Launch clear of the endpoint (matching the station LAUNCH_OFFSET) so a
      // fresh trader is not already "docked" at its origin.
      const bool outbound = (NextRand() & 1u) != 0;    // station -> planet or back
      Math::Vector3i64 from = outbound ? stationPos : planetGate;
      const Math::Vector3i64 to = outbound ? planetGate : stationPos;
      from += Math::Vector3i64{ 0, LAUNCH_OFFSET, 0 };

      const int hull = (NextRand() & 1u) ? ShipType::Shuttle : ShipType::Transporter;
      return SpawnTrader(_world, from, to, hull);
    }

    // Live ambient traders (for the spawn cap).
    [[nodiscard]] int CountTraders(ECS::Registry& _world) const
    {
      int n = 0;
      _world.Each<TradeLane>([&n](ECS::EntityId, TradeLane&) { ++n; });
      return n;
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
