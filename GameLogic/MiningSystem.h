#pragma once

// MiningSystem - the Mine order's beam cycle (GameLogic; design: scene.md 3.7).
//
// A unit carrying a mining laser and running an ActiveOrder{Mine} flies to a rock
// and cuts it: every MINING_CYCLE_TICKS in range it extracts a few units of the
// rock's commodity straight into the hold (EVE-style, no shoot-and-scoop). The
// rock's OreBody AND its belt's durable pool (PoiResources) both drain 1:1, so
// total ore ever mined equals pool ever drained - the conservation invariant
// (scene.md 3.7b). An emptied rock despawns and the order auto-retargets the
// nearest live rock in the belt; mining ends when the hold fills or the belt runs
// dry. StepMining OWNS a Mine order end to end - it writes the FlightIntent
// (steering + park-in-range) as well as running the cycle, so StepOrders skips
// Mine (a shared FlightIntent can only have one author). Runs in the same
// pre-Tick slot as StepOrders.
//
// Pure apart from the world it mutates and the events it returns, so every rule is
// unit-tested headlessly (the pool-conservation golden test is the crown jewel).

#include <cstdint>
#include <vector>

#include "ECS.h"
#include "Vector3i64.h"
#include "Vector3d.h"

#include "SimComponents.h"    // Flight, FlightIntent, WorldTransform, NetType
#include "SceneTypes.h"       // ScenePoi, OreBody, PoiResources, MiningState + constants
#include "SceneSystem.h"      // MaintainBeltRocks (refill after a rock empties)
#include "OrderSystem.h"      // ActiveOrder + Detail::SteerToward / PointDistance / MoveThrottle
#include "StationServices.h"  // CargoHold, TotalTonnage, CountsAsTonnage
#include "Messages/Defs/UnitOrder.h"   // Msg::OrderKind

namespace Neuron::GameLogic
{
  // One extraction this tick: the miner, the rock it cut, and what it yielded. The
  // caller publishes a MiningTick (beam VFX + pickup) and resends the miner's cargo
  // manifest. `unitIndex` also appears once per cargo change (dedupe if desired).
  struct MiningEvent
  {
    uint32_t unitIndex = 0;
    uint32_t rockIndex = 0;
    uint8_t commodity = 0;
    int units = 0;
  };

  namespace Detail
  {
    // Chebyshev distance between two world points (integer, overflow-safe for the
    // in-belt ranges mining works over).
    [[nodiscard]] inline int64_t ChebyDist(const Math::Vector3i64& _a, const Math::Vector3i64& _b)
    {
      const int64_t dx = _a.x > _b.x ? _a.x - _b.x : _b.x - _a.x;
      const int64_t dy = _a.y > _b.y ? _a.y - _b.y : _b.y - _a.y;
      const int64_t dz = _a.z > _b.z ? _a.z - _b.z : _b.z - _a.z;
      return dx > dy ? (dx > dz ? dx : dz) : (dy > dz ? dy : dz);
    }

    // The belt anchor a Mine order works on: the ordered target if it IS a belt
    // anchor, else (the target is a specific rock) that rock's beltAnchor. Invalid
    // if neither (a standalone rock with no belt - drain the rock only).
    [[nodiscard]] inline ECS::EntityId MineBelt(ECS::Registry& _world, const ActiveOrder& _o)
    {
      const ECS::EntityId tgt = _world.LiveEntity(_o.target);
      if (!_world.IsValid(tgt))
        return ECS::EntityId{};
      if (const ScenePoi* sp = _world.TryGet<ScenePoi>(tgt); sp != nullptr && sp->kind == PoiKind::AsteroidBelt)
        return tgt;
      if (const OreBody* ob = _world.TryGet<OreBody>(tgt); ob != nullptr)
        return _world.LiveEntity(ob->beltAnchor);
      return ECS::EntityId{};
    }

    // Nearest live rock to `_from` belonging to `_belt` (or the ordered rock itself
    // when there is no belt). Returns an invalid id if the belt has no live rock.
    [[nodiscard]] inline ECS::EntityId NearestRock(ECS::Registry& _world, const Math::Vector3i64& _from,
                                                   ECS::EntityId _belt, const ActiveOrder& _o)
    {
      // Standalone rock (no belt): the ordered target is the only candidate.
      if (!_world.IsValid(_belt))
      {
        const ECS::EntityId tgt = _world.LiveEntity(_o.target);
        if (const OreBody* ob = _world.IsValid(tgt) ? _world.TryGet<OreBody>(tgt) : nullptr; ob != nullptr && ob->units > 0)
          return tgt;
        return ECS::EntityId{};
      }
      ECS::EntityId best;
      bool found = false;
      int64_t bestDist = 0;
      _world.Each<WorldTransform, OreBody>([&](ECS::EntityId _id, WorldTransform& _t, OreBody& _ob)
      {
        if (_ob.beltAnchor != _belt.index || _ob.units <= 0)
          return;
        const int64_t d = ChebyDist(_t.position, _from);
        if (!found || d < bestDist) { found = true; bestDist = d; best = _id; }
      });
      return found ? best : ECS::EntityId{};
    }
  }

  // Advance every mining unit one tick: steer toward its current rock, and when
  // parked in range run the beam cycle (extract -> drain rock + pool -> deposit ->
  // retarget/complete). Returns the extractions that happened this tick (empty when
  // nobody cut anything). A Mine order whose belt is dry / target is gone completes
  // (holds station). Deterministic; call once per tick before GameLogic::Tick.
  [[nodiscard]] inline std::vector<MiningEvent> StepMining(ECS::Registry& _world, uint32_t _tick)
  {
    std::vector<MiningEvent> events;
    std::vector<ECS::EntityId> emptiedBelts;   // refill after the Each (don't mutate mid-iterate)

    _world.Each<ActiveOrder>([&](ECS::EntityId _self, ActiveOrder& _o)
    {
      if (_o.order != Msg::OrderKind::Mine)
        return;

      Flight* fp = _world.TryGet<Flight>(_self);
      FlightIntent* ip = _world.TryGet<FlightIntent>(_self);
      WorldTransform* tp = _world.TryGet<WorldTransform>(_self);
      CargoHold* hold = _world.TryGet<CargoHold>(_self);
      if (fp == nullptr || ip == nullptr || tp == nullptr || hold == nullptr)
        return;

      auto holdStation = [&]()
      {
        *ip = FlightIntent{};
        _o.complete = true;
        if (_world.Has<MiningState>(_self))
          _world.Remove<MiningState>(_self);
      };

      // Full hold: nothing more to cut, park.
      if (TotalTonnage(*hold) >= hold->capacity)
      {
        holdStation();
        return;
      }

      const ECS::EntityId belt = Detail::MineBelt(_world, _o);

      // Ensure per-unit mining state and a valid current rock.
      if (!_world.Has<MiningState>(_self))
        _world.Add<MiningState>(_self, MiningState{});
      MiningState& ms = _world.Get<MiningState>(_self);

      auto rockLiveOre = [&](uint32_t _idx) -> OreBody*
      {
        const ECS::EntityId r = _world.LiveEntity(_idx);
        OreBody* ob = _world.IsValid(r) ? _world.TryGet<OreBody>(r) : nullptr;
        return (ob != nullptr && ob->units > 0) ? ob : nullptr;
      };

      if (rockLiveOre(ms.rock) == nullptr)
      {
        // Retarget the nearest live rock; if the belt has none but still has pool,
        // refill it and try once more, else the belt is exhausted. NOTE: a value
        // copy of the unit position is used here because MaintainBeltRocks creates
        // entities (reallocating pools), which would dangle tp/fp/ip/hold.
        const Math::Vector3i64 selfPos = tp->position;
        ECS::EntityId rock = Detail::NearestRock(_world, selfPos, belt, _o);
        if (!_world.IsValid(rock) && _world.IsValid(belt))
        {
          if (PoiResources* pool = _world.TryGet<PoiResources>(belt); pool != nullptr && pool->units > 0)
          {
            MaintainBeltRocks(_world, belt, _tick);
            // Pools may have reallocated - re-fetch the unit's components.
            fp = _world.TryGet<Flight>(_self);
            ip = _world.TryGet<FlightIntent>(_self);
            tp = _world.TryGet<WorldTransform>(_self);
            hold = _world.TryGet<CargoHold>(_self);
            if (fp == nullptr || ip == nullptr || tp == nullptr || hold == nullptr)
              return;
            rock = Detail::NearestRock(_world, selfPos, belt, _o);
          }
        }
        if (!_world.IsValid(rock))
        {
          holdStation();   // belt exhausted / target gone
          return;
        }
        ms.rock = rock.index;
        ms.progress = 0;
      }

      const ECS::EntityId rock = _world.LiveEntity(ms.rock);
      const WorldTransform* rt = _world.TryGet<WorldTransform>(rock);
      if (rt == nullptr) { holdStation(); return; }

      // Steer onto the rock (shared steering); park (cut throttle) once in range so
      // the pass doesn't drift off, otherwise close in.
      const double dist = Detail::PointDistance(tp->position, rt->position);
      const double dx = static_cast<double>(rt->position.x - tp->position.x);
      const double dy = static_cast<double>(rt->position.y - tp->position.y);
      const double dz = static_cast<double>(rt->position.z - tp->position.z);
      if (dist > 0.0)
      {
        const Math::Vector3d toRock{ dx / dist, dy / dist, dz / dist };
        const double ahead = Math::Dot(toRock, fp->nose);
        ip->rollAxis = 0.0;
        ip->pitchAxis = 0.0;
        Detail::SteerToward(*fp, toRock, *ip, /*lockRoll*/ false,
                            (ahead < AP_RELAX) ? 0.0 : AP_DEADZONE);
        ip->throttle = (dist <= static_cast<double>(MINING_RANGE)) ? 0.0 : Detail::MoveThrottle(dist, ahead);
      }
      _o.complete = false;

      // Not parked in range yet: fly, don't cut.
      if (Detail::ChebyDist(tp->position, rt->position) > MINING_RANGE)
      {
        ms.progress = 0;
        return;
      }

      // In range: advance the beam and extract on a completed cycle.
      if (++ms.progress < static_cast<uint16_t>(MINING_CYCLE_TICKS))
        return;
      ms.progress = 0;

      OreBody* ore = _world.TryGet<OreBody>(rock);
      if (ore == nullptr || ore->units <= 0) { ms.rock = ECS::INVALID_INDEX; return; }

      // Yield clamps: the cycle amount, the rock, the belt pool, and hold room.
      int yield = MINING_YIELD;
      if (yield > ore->units) yield = ore->units;
      PoiResources* pool = _world.IsValid(belt) ? _world.TryGet<PoiResources>(belt) : nullptr;
      if (pool != nullptr && yield > pool->units) yield = pool->units;
      if (CountsAsTonnage(ore->commodity))
      {
        const int room = hold->capacity - TotalTonnage(*hold);
        if (yield > room) yield = room;
      }
      if (yield <= 0) { holdStation(); return; }

      hold->units[ore->commodity] += yield;
      ore->units -= yield;
      if (pool != nullptr) pool->units -= yield;

      events.push_back(MiningEvent{ _self.index, rock.index, ore->commodity, yield });

      if (ore->units <= 0)
      {
        _world.Destroy(rock);
        ms.rock = ECS::INVALID_INDEX;
        if (_world.IsValid(belt))
          emptiedBelts.push_back(belt);
      }

      // Hold just filled: done.
      if (TotalTonnage(*hold) >= hold->capacity)
        holdStation();
    });

    // Refill emptied belts (spawns are safe now that the Each has finished).
    for (const ECS::EntityId belt : emptiedBelts)
      MaintainBeltRocks(_world, belt, _tick);

    return events;
  }
}
