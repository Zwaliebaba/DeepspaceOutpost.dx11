#pragma once

// CollisionSystem - contact damage and lethal environment (GameLogic, G6).
//
// The world gets consequences for touching things:
//
//   ship <-> ship     Contact grinds BOTH parties (legacy ramming): damage routes
//                     through ApplyDamage, so a player absorbs it on the shield
//                     facing the other hull while an NPC takes it on the energy
//                     pool. Contact range is the same calibration as scooping.
//   ship <-> station  The station hull is harder and bigger: contact damages the
//                     SHIP only (the station is a fortress). The legal way in is
//                     unchanged - request docking from inside the 5000-unit dock
//                     range and you are docked long before you reach the hull.
//   ship <-> planet   Lethal, full stop (legacy check_altitude: altitude 0 = game
//                     over). The kill radius sits well inside the 8000-unit
//                     station orbit, and trader lanes end at a gate offset above
//                     the planet so ambient traffic lands without burning up.
//
// Sun proximity / cabin temperature (update_cabin_temp) is deliberately NOT here:
// the authoritative world has no sun entities yet, and the legacy rule's payoff
// (sun-skimming fuel scooping) needs the G7 fuel resource - suns, heat and fuel
// land together in G7.
//
// Damage repeats every tick two hulls stay in contact - collisions are sustained
// grinding, not one-off taps, so lingering inside another ship is quickly fatal
// (the legacy feel: collisions killed). Docked ships are inside a station, not in
// space - exempt. Spawn/respawn grace (invulnTicks) blocks collision damage too,
// but its countdown stays owned by StepCombat. Missiles and cargo canisters have
// no Combatant, so they are naturally outside this system (missiles detonate via
// StepMissiles, canisters scoop/smash via ScoopSystem).
//
// Pure apart from the world it mutates; returns the kills for the caller's death
// pipeline. Unit-tested headlessly.

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "ECS.h"
#include "Vector3i64.h"

#include "SimComponents.h"     // WorldTransform, NetType, ShipType
#include "CombatSystem.h"      // Combatant, Team, Kill, ApplyDamage
#include "StationServices.h"   // DockState

namespace Neuron::GameLogic
{
  // Contact ranges (world units, Chebyshev - overflow-safe on absolute coords).
  // Ship contact matches the scoop calibration; the station hull is bigger, and
  // still comfortably inside the 2000-unit undock/launch offset so a fresh
  // launch or a station police launch never spawns in contact.
  inline constexpr int64_t SHIP_CONTACT_RANGE = 600;
  inline constexpr int64_t STATION_CONTACT_RANGE = 1000;

  // Inside this range of a planet's center you are in atmosphere/rock: instant
  // death. Well under the 8000-unit station orbit, so stations (and ships docked
  // at them) are never inside a planet.
  inline constexpr int64_t PLANET_KILL_RADIUS = 4000;

  // Where ambient traffic turns around: a lane gate ABOVE the planet, clear of
  // the kill radius by a launch offset, so traders "land" and despawn safely.
  inline constexpr int64_t PLANET_LANE_GATE = PLANET_KILL_RADIUS + 2000;

  // Contact damage per tick of overlap. A ram costs a player a big bite of one
  // shield and kills a stock pirate (80 energy) outright; a station scrape is
  // worse - two ticks of it kills most things.
  inline constexpr int SHIP_RAM_DAMAGE = 100;
  inline constexpr int STATION_CRASH_DAMAGE = 200;

  // Advance collision resolution one tick: grind overlapping hulls, crash ships
  // against station hulls, and kill anything inside a planet. Returns the kills
  // (each victim reported once); the caller feeds them to the death pipeline.
  [[nodiscard]] inline std::vector<Kill> StepCollisions(ECS::Registry& _world)
  {
    struct Unit
    {
      ECS::EntityId id;
      Math::Vector3i64 pos;
      Combatant* c;
      bool station;
    };

    // Everything with a hull that is actually out in space (docked = inside).
    std::vector<Unit> units;
    _world.Each<WorldTransform, Combatant>([&](ECS::EntityId _id, WorldTransform& _t, Combatant& _c)
    {
      const DockState* dock = _world.TryGet<DockState>(_id);
      if (dock != nullptr && dock->docked)
        return;
      units.push_back(Unit{ _id, _t.position, &_c, _c.team == Team::Station });
    });

    auto within = [](const Math::Vector3i64& _a, const Math::Vector3i64& _b, int64_t _range) -> bool
    {
      const int64_t ax = _a.x > _b.x ? _a.x - _b.x : _b.x - _a.x;
      const int64_t ay = _a.y > _b.y ? _a.y - _b.y : _b.y - _a.y;
      const int64_t az = _a.z > _b.z ? _a.z - _b.z : _b.z - _a.z;
      return ax <= _range && ay <= _range && az <= _range;
    };

    std::vector<Kill> kills;
    std::unordered_set<uint32_t> dead;   // each victim reported once
    auto report = [&kills, &dead](ECS::EntityId _victim, uint32_t _killer)
    {
      if (dead.insert(_victim.index).second)
        kills.push_back(Kill{ _victim, _killer });
    };

    // Ship <-> ship and ship <-> station, over all pairs (fleet sizes are small).
    for (std::size_t i = 0; i < units.size(); ++i)
      for (std::size_t j = i + 1; j < units.size(); ++j)
      {
        Unit& a = units[i];
        Unit& b = units[j];
        if (a.station && b.station)
          continue;

        if (a.station || b.station)
        {
          // A ship scraping the station hull: the ship alone takes the damage.
          Unit& ship = a.station ? b : a;
          const Unit& hull = a.station ? a : b;
          if (!within(ship.pos, hull.pos, STATION_CONTACT_RANGE))
            continue;
          if (ship.c->invulnTicks > 0 || dead.count(ship.id.index) != 0)
            continue;
          if (ApplyDamage(_world, ship.id, STATION_CRASH_DAMAGE, hull.pos))
            report(ship.id, hull.id.index);
          continue;
        }

        if (!within(a.pos, b.pos, SHIP_CONTACT_RANGE))
          continue;

        // Ramming grinds both hulls; each side's hit arrives from the OTHER's
        // position, so a player victim absorbs it on the facing shield.
        if (a.c->invulnTicks == 0 && dead.count(a.id.index) == 0)
          if (ApplyDamage(_world, a.id, SHIP_RAM_DAMAGE, b.pos))
            report(a.id, b.id.index);
        if (b.c->invulnTicks == 0 && dead.count(b.id.index) == 0)
          if (ApplyDamage(_world, b.id, SHIP_RAM_DAMAGE, a.pos))
            report(b.id, a.id.index);
      }

    // Ship <-> planet: no damage model, just death (legacy altitude-zero rule).
    std::vector<Unit> planets;
    _world.Each<WorldTransform, NetType>([&planets](ECS::EntityId _id, WorldTransform& _t, NetType& _nt)
    {
      if (_nt.type == ShipType::Planet)
        planets.push_back(Unit{ _id, _t.position, nullptr, false });
    });
    if (!planets.empty())
      for (Unit& u : units)
      {
        if (u.station || u.c->invulnTicks > 0 || dead.count(u.id.index) != 0)
          continue;
        for (const Unit& p : planets)
          if (within(u.pos, p.pos, PLANET_KILL_RADIUS))
          {
            u.c->energy = 0;
            report(u.id, p.id.index);
            break;
          }
      }

    return kills;
  }
}
