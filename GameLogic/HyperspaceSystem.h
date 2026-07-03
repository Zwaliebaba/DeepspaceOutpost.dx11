#pragma once

// HyperspaceSystem - fuel-gated travel, witchspace, in-system jump (GameLogic, G7).
//
// Travel gets a cost and a risk, the authoritative server owning both:
//
//   Hyperspace   A jump to another system costs fuel by DISTANCE (legacy
//                complete_hyperspace): the server validates the tank covers it,
//                deducts it, halves the wanted record (running cools your rap
//                sheet), and relocates the ship near the destination station -
//                arriving in FLIGHT, not docked. A small chance the jump MISFIRES
//                into witchspace: deep interstellar space with a Thargoid ambush,
//                where kills pay no bounty (the Witchspace marker gates
//                KillRewards). A later successful jump clears it.
//
//   InSystemJump The legacy jump_warp: a fast hop toward the planet, BLOCKED
//                (mass-locked) when a ship, station or planet is close. Not a
//                teleport across systems - a big shove down the system so the long
//                cruise to the planet isn't real-time tedium.
//
// The light-year scale (UNITS_PER_TENTH_LY) is calibrated so a full 7.0 LY tank
// spans ~35M units - enough to reach a neighbour in the sparsely scattered galaxy
// (mean nearest-neighbour ~14.5M units), so refuelling gates onward travel rather
// than stranding everyone at spawn.
//
// Pure apart from the world it mutates and the caller-owned RNG; unit-tested
// headlessly. The server loop routes TravelRequest (hyperspace /
// in-system jump) through here; travel no longer rides the station protocol.

#include <cmath>
#include <cstdint>
#include <vector>

#include "ECS.h"
#include "Vector3i64.h"
#include "Vector3d.h"

#include "SimComponents.h"
#include "CombatSystem.h"      // Team, Combatant, Wanted, Witchspace, Bounty, PIRATE_BOUNTY
#include "StationServices.h"   // Fuel, DockState, NearestStation, FindStationBySystem, LAUNCH_OFFSET
#include "AiSystem.h"          // AiPilot, NpcFlightCaps, NPC_MAX_TURN_RATE, Detail::AiRand255
#include "Messages/Defs/Travel.h"   // Msg::TravelStatus (the travel wire outcomes)

namespace Neuron::GameLogic
{
  // World units per 0.1 light year. A full 70-tenth (7.0 LY) tank => 35M units.
  inline constexpr int64_t UNITS_PER_TENTH_LY = 500'000;

  // A jump misfires into witchspace when a rand255() clears this (legacy
  // rand255() > 253 => 2/256 ~= 0.8%).
  inline constexpr uint32_t WITCHSPACE_ROLL_THRESHOLD = 253;

  // How far off the destination a misjump strands you (deep space, clear of every
  // system - the galaxy's nearest-neighbour floor is ~5.5M units).
  inline constexpr int64_t WITCHSPACE_DISPLACEMENT = 20'000'000;

  // A Thargoid's reward (killing one normally pays; in witchspace it doesn't).
  inline constexpr int THARGOID_BOUNTY = 100;

  // In-system jump: mass-lock radius (legacy 75001) and how far one hop shoves the
  // ship down-system, kept clear of a planet's lethal radius on arrival.
  inline constexpr int64_t MASS_LOCK_RANGE = 75'000;
  inline constexpr int64_t IN_SYSTEM_JUMP_MAX = 200'000;
  inline constexpr int64_t IN_SYSTEM_SAFE_MARGIN = 6'000;

  // Fuel cost (tenths of a light year) to jump between two points, floored at 1 -
  // even a hop to a neighbouring system burns something.
  [[nodiscard]] inline int JumpCostTenths(const Math::Vector3i64& _from, const Math::Vector3i64& _to)
  {
    const double dx = static_cast<double>(_to.x - _from.x);
    const double dy = static_cast<double>(_to.y - _from.y);
    const double dz = static_cast<double>(_to.z - _from.z);
    const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    const int tenths = static_cast<int>(dist / static_cast<double>(UNITS_PER_TENTH_LY) + 0.5);
    return tenths < 1 ? 1 : tenths;
  }

  // Spawn `_count` Thargoids around `_near` (the witchspace ambush). They are
  // hostile auto-engaging combatants that fly by intent like every other NPC, and
  // carry a bounty - though a kill made in witchspace pays nothing (KillRewards).
  inline std::vector<ECS::EntityId> SpawnThargoids(ECS::Registry& _world, const Math::Vector3i64& _near,
                                                   int _count, uint32_t& _rng)
  {
    std::vector<ECS::EntityId> spawned;
    for (int i = 0; i < _count; ++i)
    {
      auto axis = [&_rng]() -> int64_t
      {
        const uint32_t r = Detail::AiRand255(_rng);
        const int64_t mag = 6000 + static_cast<int64_t>(r % 4000);   // [6000, 10000)
        return (r & 1u) ? mag : -mag;
      };
      const Math::Vector3i64 pos{ _near.x + axis(), _near.y + axis(), _near.z + axis() };

      const ECS::EntityId e = _world.Create();
      _world.Add<WorldTransform>(e, WorldTransform{ pos });
      _world.Add<Flight>(e, Flight{});
      _world.Add<FlightIntent>(e, FlightIntent{});
      _world.Add<FlightCaps>(e, NpcFlightCaps());
      _world.Add<Combatant>(e, Combatant{ Team::Pirate, /*energy*/ 120, /*laser*/ 4, /*range*/ 5000, /*autoEngage*/ true });
      _world.Add<AiPilot>(e, AiPilot{ /*bravery*/ 120, /*missiles*/ 1, /*maxEnergy*/ 120 });
      _world.Add<NetType>(e, NetType{ ShipType::Thargoid });
      _world.Add<Bounty>(e, Bounty{ THARGOID_BOUNTY });
      spawned.push_back(e);
    }
    return spawned;
  }

  struct HyperspaceOutcome
  {
    Msg::TravelStatus status = Msg::TravelStatus::Rejected;
    bool jumped = false;       // did the ship move + spend fuel?
    bool witchspace = false;   // did it misfire into a witchspace ambush?
    bool wantedChanged = false;// did the jump cool the wanted record (needs a roster refresh)?
  };

  // Jump `_player` to the station of system `_destSystemId`. Validates the tank,
  // deducts fuel by distance, cools the wanted record, and either arrives near the
  // destination (in flight) or misfires into a witchspace Thargoid ambush. The RNG
  // is caller-owned (deterministic). Returns what happened for the caller to turn
  // into a TravelResponse and any broadcasts.
  inline HyperspaceOutcome Hyperspace(ECS::Registry& _world, ECS::EntityId _player,
                                      uint32_t _destSystemId, uint32_t& _rng)
  {
    HyperspaceOutcome out;

    WorldTransform* pt = _world.TryGet<WorldTransform>(_player);
    Fuel* fuel = _world.TryGet<Fuel>(_player);
    DockState* dock = _world.TryGet<DockState>(_player);
    if (pt == nullptr || fuel == nullptr)
      return out;   // not a jump-capable entity

    const ECS::EntityId destStation = FindStationBySystem(_world, static_cast<int>(_destSystemId));
    const WorldTransform* dt = (destStation.index != ECS::INVALID_INDEX)
      ? _world.TryGet<WorldTransform>(destStation) : nullptr;
    if (dt == nullptr)
    {
      out.status = Msg::TravelStatus::UnknownSystem;
      return out;
    }

    const int cost = JumpCostTenths(pt->position, dt->position);
    if (cost > fuel->max)
    {
      out.status = Msg::TravelStatus::OutOfRange;   // beyond even a full tank
      return out;
    }
    if (cost > fuel->tenths)
    {
      out.status = Msg::TravelStatus::NotEnoughFuel;
      return out;
    }

    // Commit: spend the fuel, cool the record, leave the pad.
    fuel->tenths -= cost;
    if (Wanted* w = _world.TryGet<Wanted>(_player); w != nullptr && w->level > 0)
    {
      w->level /= 2;   // legacy legal_status /= 2 on a jump
      out.wantedChanged = true;
    }
    if (dock != nullptr)
      dock->docked = false;
    out.jumped = true;

    // Misjump into witchspace? Deep space, undocked, ambushed.
    if (Detail::AiRand255(_rng) > WITCHSPACE_ROLL_THRESHOLD)
    {
      pt->position = dt->position + Math::Vector3i64{ WITCHSPACE_DISPLACEMENT, 0, 0 };
      if (!_world.Has<Witchspace>(_player))
        _world.Add<Witchspace>(_player, Witchspace{});
      const int nthg = 1 + static_cast<int>(Detail::AiRand255(_rng) & 3u);   // legacy (rand & 3) + 1
      SpawnThargoids(_world, pt->position, nthg, _rng);
      out.status = Msg::TravelStatus::Witchspace;
      out.witchspace = true;
      return out;
    }

    // Clean arrival: shake off any prior witchspace, drop in near the destination
    // station (in flight - fly in to dock).
    if (_world.Has<Witchspace>(_player))
      _world.Remove<Witchspace>(_player);
    pt->position = dt->position + Math::Vector3i64{ 0, 0, LAUNCH_OFFSET };
    out.status = Msg::TravelStatus::Arrived;
    return out;
  }

  struct JumpDriveOutcome
  {
    Msg::TravelStatus status = Msg::TravelStatus::MassLocked;
    bool jumped = false;
  };

  // In-system fast jump (legacy jump_warp): shove `_player` toward the nearest
  // planet, UNLESS mass-locked - any other combatant (ship or station) or planet
  // within MASS_LOCK_RANGE blocks it. Loot/missiles carry no Combatant, so they
  // never mass-lock, matching the legacy cargo/rock exemption.
  inline JumpDriveOutcome InSystemJump(ECS::Registry& _world, ECS::EntityId _player)
  {
    JumpDriveOutcome out;
    WorldTransform* pt = _world.TryGet<WorldTransform>(_player);
    if (pt == nullptr)
      return out;
    const Math::Vector3i64 self = pt->position;

    auto closeBy = [&self](const Math::Vector3i64& _p) -> bool
    {
      const int64_t ax = _p.x > self.x ? _p.x - self.x : self.x - _p.x;
      const int64_t ay = _p.y > self.y ? _p.y - self.y : self.y - _p.y;
      const int64_t az = _p.z > self.z ? _p.z - self.z : self.z - _p.z;
      return ax <= MASS_LOCK_RANGE && ay <= MASS_LOCK_RANGE && az <= MASS_LOCK_RANGE;
    };

    bool locked = false;
    _world.Each<WorldTransform, Combatant>([&](ECS::EntityId _id, WorldTransform& _t, Combatant&)
    {
      if (_id != _player && closeBy(_t.position))
        locked = true;   // another hull (ship or station) too close
    });

    // Nearest planet: it both mass-locks (if close) and is the jump target.
    bool foundPlanet = false;
    Math::Vector3i64 planetPos{};
    int64_t bestDist = 0;
    _world.Each<WorldTransform, NetType>([&](ECS::EntityId, WorldTransform& _t, NetType& _nt)
    {
      if (_nt.type != ShipType::Planet)
        return;
      if (closeBy(_t.position))
        locked = true;
      const int64_t ax = _t.position.x > self.x ? _t.position.x - self.x : self.x - _t.position.x;
      const int64_t ay = _t.position.y > self.y ? _t.position.y - self.y : self.y - _t.position.y;
      const int64_t az = _t.position.z > self.z ? _t.position.z - self.z : self.z - _t.position.z;
      const int64_t d = ax / 2 + ay / 2 + az / 2;   // halved Manhattan: no overflow far out
      if (!foundPlanet || d < bestDist)
      {
        foundPlanet = true;
        planetPos = _t.position;
        bestDist = d;
      }
    });

    if (locked)
    {
      out.status = Msg::TravelStatus::MassLocked;
      return out;
    }
    if (!foundPlanet)
    {
      out.status = Msg::TravelStatus::Rejected;   // nowhere to jump toward
      return out;
    }

    // Hop toward the planet, stopping clear of its lethal radius.
    const double dx = static_cast<double>(planetPos.x - self.x);
    const double dy = static_cast<double>(planetPos.y - self.y);
    const double dz = static_cast<double>(planetPos.z - self.z);
    const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    int64_t hop = IN_SYSTEM_JUMP_MAX;
    const int64_t reachable = static_cast<int64_t>(dist) - IN_SYSTEM_SAFE_MARGIN;
    if (reachable < hop)
      hop = reachable;
    if (hop <= 0)
    {
      out.status = Msg::TravelStatus::MassLocked;   // already on top of the planet
      return out;
    }

    const double s = static_cast<double>(hop) / dist;
    pt->position += Math::Vector3i64{
      static_cast<int64_t>(dx * s),
      static_cast<int64_t>(dy * s),
      static_cast<int64_t>(dz * s),
    };
    out.status = Msg::TravelStatus::Jumped;
    out.jumped = true;
    return out;
  }
}
