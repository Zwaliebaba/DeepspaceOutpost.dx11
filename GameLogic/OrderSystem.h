#pragma once

// OrderSystem - execute unit orders through the intent flight model (GameLogic, I1).
//
// The server half of the pointer-first command interface (docs/interaction.md,
// Track I). The player no longer pilots a hull; they order a unit they own, and the
// order becomes an ActiveOrder component. StepOrders() runs once per tick BEFORE
// StepAi/StepFlightInput and translates each ActiveOrder into a FlightIntent using
// the SAME steering the NPC autopilot uses (Detail::SteerToward + throttle-by-
// alignment) - so ordered flight is clamped by the ship's FlightCaps exactly like
// piloted or AI flight was. This is the movement verb the free-camera migration
// retired: with the flight axes always zero, an order is the only way a hull moves.
//
// The order kinds:
//   Stop     - level off, cut throttle (hold station).
//   Move     - fly to a fixed world point, ease off and stop within the arrival
//              radius.
//   Approach - fly to a target entity's CURRENT position, then stop.
//   Dock     - fly to a target station; the SERVER completes the dock when the ship
//              reaches dock range (reusing the tested station path), then clears the
//              order. Here it just steers (like Approach, no arrival stop).
//   Attack   - pursue a target combatant, hold a standoff rather than ram, and the
//              tick it is aligned + in laser range it is returned as "wants to fire"
//              (the caller publishes FireWeapon{Laser}, so lag compensation + crime
//              attribution stay the existing player-fire path).
//   Collect  - fly to a cargo canister; the existing ScoopSystem picks it up on
//              proximity. Completes when the canister is gone.
//   Escort   - follow a target (F1: the owner's ship); reserved formation semantics,
//              treated as a never-completing Approach for now.
//
// Pure apart from the world it mutates (intents + order completion flags) and the
// fire list it returns, so every rule is unit-tested headlessly. Validation
// (ownership, target legality, range, crime) lives in the server order handler;
// this system trusts an already-validated ActiveOrder.

#include <cmath>
#include <vector>

#include "ECS.h"
#include "Vector3i64.h"
#include "Vector3d.h"

#include "SimComponents.h"
#include "FlightInput.h"      // FlightIntent
#include "AiSystem.h"         // Detail::SteerToward + the shared steering constants
#include "CombatSystem.h"     // Combatant (Attack range + focus)
#include "StationServices.h"  // DockState / ServerStation (dock gating + target type)
#include "LootSystem.h"       // LootItem (Collect target type)
#include "Messages/Defs/UnitOrder.h"   // Msg::OrderKind / OrderStatus

namespace Neuron::GameLogic
{
  // The order a unit is currently executing (I1). Plain, serializable data (the §12
  // persistence invariant): an order survives a save/reload as order + target +
  // point. `target` is a bare entity INDEX (resolved live each tick, generation-
  // safe via LiveEntity); `targetPos` is the Move destination.
  struct ActiveOrder
  {
    Msg::OrderKind order = Msg::OrderKind::Stop;
    uint32_t target = ECS::INVALID_INDEX;   // entity index for entity-targeted orders
    Math::Vector3i64 targetPos{};           // world point (Move)
    bool complete = false;                  // arrived / target gone: holding station
  };

  // Within this Chebyshev-ish range of a Move/Approach destination the unit is
  // "arrived": it levels off and cuts throttle rather than circling the point.
  inline constexpr int64_t ORDER_ARRIVE_RADIUS = 200;

  // Start easing throttle down once inside this range of the destination, so the
  // unit decelerates into the arrival radius instead of overshooting.
  inline constexpr double ORDER_SLOW_RADIUS = 4000.0;

  // A floor on the eased throttle while still outside the arrival radius: without it
  // the linear ease-to-zero-at-the-radius asymptotes and the unit parks just short,
  // never actually crossing the radius (where StepOrders stops it). The floor keeps
  // it creeping so it always reaches - then holdStation cuts throttle to zero. Small
  // enough (speed a few units/tick) that it can't overshoot through the point.
  inline constexpr double ORDER_MIN_CREEP = 0.04;

  // Attack standoff: closer than this the unit brakes so an attack pass doesn't ram
  // (mirrors the AI's AI_CLOSE_DIST feel), and it fires from within its laser range.
  inline constexpr int64_t ORDER_ATTACK_STANDOFF = 900;

  // Clamp `_to` so it lies within `_maxDist` (Chebyshev) of `_from` on every axis -
  // the server-side gate on a Move destination, so a hostile client can't fling a
  // unit across the galaxy in one order. Pure + overflow-safe (clamps per axis
  // against the signed range), so it is unit-tested directly.
  [[nodiscard]] inline Math::Vector3i64 ClampToChebyshev(const Math::Vector3i64& _from,
                                                         const Math::Vector3i64& _to, int64_t _maxDist)
  {
    auto clampAxis = [_maxDist](int64_t _from1, int64_t _to1) -> int64_t
    {
      if (_to1 > _from1 + _maxDist) return _from1 + _maxDist;
      if (_to1 < _from1 - _maxDist) return _from1 - _maxDist;
      return _to1;
    };
    return Math::Vector3i64{ clampAxis(_from.x, _to.x), clampAxis(_from.y, _to.y), clampAxis(_from.z, _to.z) };
  }

  // The outcome of validating a UnitOrder: the status to ack back, and (on
  // Accepted) the ActiveOrder to record on `unit`. Pure result - the caller applies
  // the crime side-effects, records the order, and sends the ack.
  struct OrderPlan
  {
    Msg::OrderStatus status = Msg::OrderStatus::Rejected;
    ActiveOrder order{};
    ECS::EntityId unit{};   // the resolved ordered unit (valid iff Accepted)
  };

  // Validate a UnitOrder from player `_playerId` against `_world` and build the
  // ActiveOrder. PURE (no mutation, no ack, no crime): it only reads the world and
  // returns what the caller should do. Checks, in order: the unit is a live entity
  // this player owns (NotYours); a flight order on a docked unit is refused
  // (Docked); the order kind is supported (Illegal for reserved Patrol/Route); the
  // target exists and is the right type for the kind (BadTarget); a Move point is
  // clamped within `_maxMoveDist` of the unit. Unit-tested as the anti-cheat matrix.
  [[nodiscard]] inline OrderPlan PlanUnitOrder(ECS::Registry& _world, uint32_t _playerId,
                                               const Msg::UnitOrder& _req, int64_t _maxMoveDist)
  {
    OrderPlan plan;

    const ECS::EntityId unit = _world.LiveEntity(_req.unitId);
    const Owner* owner = _world.IsValid(unit) ? _world.TryGet<Owner>(unit) : nullptr;
    if (_playerId == 0 || owner == nullptr || owner->playerId != _playerId)
    {
      plan.status = Msg::OrderStatus::NotYours;
      return plan;
    }
    plan.unit = unit;

    if (_req.order != Msg::OrderKind::Stop)
      if (const DockState* d = _world.TryGet<DockState>(unit); d != nullptr && d->docked)
      {
        plan.status = Msg::OrderStatus::Docked;
        return plan;
      }

    plan.order.order = _req.order;
    plan.order.complete = false;

    switch (_req.order)
    {
      case Msg::OrderKind::Stop:
        break;

      case Msg::OrderKind::Move:
      {
        const Math::Vector3i64 from = _world.Get<WorldTransform>(unit).position;
        plan.order.targetPos = ClampToChebyshev(
            from, Math::Vector3i64{ _req.targetX, _req.targetY, _req.targetZ }, _maxMoveDist);
        break;
      }

      case Msg::OrderKind::Approach:
      case Msg::OrderKind::Dock:
      case Msg::OrderKind::Attack:
      case Msg::OrderKind::Collect:
      case Msg::OrderKind::Escort:
      {
        const ECS::EntityId tgt = _world.LiveEntity(_req.target);
        if (!_world.IsValid(tgt) || tgt.index == unit.index)
        {
          plan.status = Msg::OrderStatus::BadTarget;
          return plan;
        }
        bool ok = false;
        switch (_req.order)
        {
          case Msg::OrderKind::Dock:    ok = _world.Has<ServerStation>(tgt); break;
          case Msg::OrderKind::Attack:  ok = _world.Has<Combatant>(tgt);     break;
          case Msg::OrderKind::Collect: ok = _world.Has<LootItem>(tgt);      break;
          default:                      ok = _world.Has<WorldTransform>(tgt); break;
        }
        if (!ok)
        {
          plan.status = Msg::OrderStatus::BadTarget;
          return plan;
        }
        plan.order.target = _req.target;
        break;
      }

      default:
        plan.status = Msg::OrderStatus::Illegal;   // reserved (Patrol/Route) or garbage
        return plan;
    }

    plan.status = Msg::OrderStatus::Accepted;
    return plan;
  }

  namespace Detail
  {
    // Straight-line distance between two int64 points as a double. In-order deltas
    // are small enough (a unit is steering toward something in its own region) that
    // the double conversion is exact-enough; the caller only uses it for throttle
    // shaping and arrival, never for authoritative position.
    [[nodiscard]] inline double PointDistance(const Math::Vector3i64& _a, const Math::Vector3i64& _b)
    {
      const double dx = static_cast<double>(_b.x - _a.x);
      const double dy = static_cast<double>(_b.y - _a.y);
      const double dz = static_cast<double>(_b.z - _a.z);
      return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // Arrival-aware throttle for a move-type order: a tier by alignment (creep while
    // the nose swings around, cruise when roughly on, full when well aimed) scaled
    // down as the unit nears its destination so it eases to a stop.
    [[nodiscard]] inline double MoveThrottle(double _dist, double _ahead)
    {
      if (_dist <= static_cast<double>(ORDER_ARRIVE_RADIUS))
        return 0.0;
      const double base = (_ahead <= AI_SLOW_ALIGN) ? 0.12          // dest well behind: creep + turn
                        : (_ahead >= AP_FAST_ALIGN) ? 1.0           // well aimed: full
                                                    : 0.5;          // roughly on: cruise
      const double slow = (_dist >= ORDER_SLOW_RADIUS) ? 1.0
                        : (_dist - static_cast<double>(ORDER_ARRIVE_RADIUS))
                          / (ORDER_SLOW_RADIUS - static_cast<double>(ORDER_ARRIVE_RADIUS));
      double t = base * (slow < 0.0 ? 0.0 : slow);
      if (t < ORDER_MIN_CREEP)   // never asymptotically stall short of the arrival radius
        t = ORDER_MIN_CREEP;
      return t > 1.0 ? 1.0 : t;
    }
  }

  // Advance every ordered unit one tick: translate its ActiveOrder into a
  // FlightIntent and (for Attack orders that are aligned + in range) collect it into
  // the returned "wants to fire this tick" list. The caller publishes a
  // FireWeapon{Laser} for each returned unit so firing reuses the lag-compensated,
  // crime-attributing player-fire path. A unit whose order references a dead target
  // holds station (zero intent, order marked complete). Runs before StepAi.
  [[nodiscard]] inline std::vector<ECS::EntityId> StepOrders(ECS::Registry& _world)
  {
    std::vector<ECS::EntityId> wantsFire;

    _world.Each<ActiveOrder>([&](ECS::EntityId _self, ActiveOrder& _o)
    {
      Flight* fp = _world.TryGet<Flight>(_self);
      FlightIntent* ip = _world.TryGet<FlightIntent>(_self);
      WorldTransform* tp = _world.TryGet<WorldTransform>(_self);
      if (fp == nullptr || ip == nullptr || tp == nullptr)
        return;
      Flight& _f = *fp;
      FlightIntent& _in = *ip;
      WorldTransform& _t = *tp;

      // A docked ship takes no flight order (the server rejects flight orders on a
      // docked unit; belt-and-braces here so a stale order can't fly it out).
      if (const DockState* d = _world.TryGet<DockState>(_self); d != nullptr && d->docked)
      {
        _in = FlightIntent{};
        return;
      }

      auto holdStation = [&]()
      {
        _in = FlightIntent{};   // level off, cut throttle
        _o.complete = true;
      };

      if (_o.order == Msg::OrderKind::Stop)
      {
        holdStation();
        return;
      }

      // Resolve the destination point. Entity-targeted orders read the target's
      // CURRENT position (generation-safe); a dead/removed target ends the order.
      Math::Vector3i64 dest{};
      bool haveDest = false;
      const bool entityTargeted = (_o.order == Msg::OrderKind::Approach
                                || _o.order == Msg::OrderKind::Dock
                                || _o.order == Msg::OrderKind::Attack
                                || _o.order == Msg::OrderKind::Collect
                                || _o.order == Msg::OrderKind::Escort);
      if (_o.order == Msg::OrderKind::Move)
      {
        dest = _o.targetPos;
        haveDest = true;
      }
      else if (entityTargeted)
      {
        const ECS::EntityId tgt = _world.LiveEntity(_o.target);
        if (const WorldTransform* tt = _world.IsValid(tgt) ? _world.TryGet<WorldTransform>(tgt) : nullptr)
        {
          dest = tt->position;
          haveDest = true;
        }
      }

      if (!haveDest)   // Move with no point can't happen; entity target gone: done
      {
        holdStation();
        return;
      }

      const double dist = Detail::PointDistance(_t.position, dest);

      // Move/Approach arrive-and-stop; Dock/Collect/Attack/Escort keep station-
      // keeping behaviour (the server completes a dock, the scoop completes a
      // collect, attack holds a standoff, escort keeps following).
      const bool arriveStop = (_o.order == Msg::OrderKind::Move || _o.order == Msg::OrderKind::Approach);
      if (arriveStop && dist <= static_cast<double>(ORDER_ARRIVE_RADIUS))
      {
        holdStation();
        return;
      }

      // Unit direction to the destination.
      const double dx = static_cast<double>(dest.x - _t.position.x);
      const double dy = static_cast<double>(dest.y - _t.position.y);
      const double dz = static_cast<double>(dest.z - _t.position.z);
      if (dist <= 0.0)
      {
        holdStation();
        return;
      }
      const Math::Vector3d toDest{ dx / dist, dy / dist, dz / dist };
      const double ahead = Math::Dot(toDest, _f.nose);

      // Steer the nose onto the destination with the autopilot's wider deadzone,
      // dropped entirely when the destination is far behind (a hard about-face).
      _in.rollAxis = 0.0;
      _in.pitchAxis = 0.0;
      Detail::SteerToward(_f, toDest, _in, /*lockRoll*/ false,
                          (ahead < AP_RELAX) ? 0.0 : AP_DEADZONE);
      _o.complete = false;

      if (_o.order == Msg::OrderKind::Attack)
      {
        Combatant* sc = _world.TryGet<Combatant>(_self);
        // Hold a standoff: close hard when far, brake inside the standoff so the
        // pass doesn't ram, and keep the focus honest for consumers.
        _in.throttle = (dist < static_cast<double>(ORDER_ATTACK_STANDOFF)) ? 0.15
                     : Detail::MoveThrottle(dist, ahead);
        if (sc != nullptr)
        {
          sc->focus = _o.target;   // the pilot's chosen prey (StepCombat / memory)
          // Aligned and inside laser range: fire this tick. The actual target
          // selection + hit stay ResolvePlayerFire's job (nearest in cone); heat
          // gating throttles the cadence exactly like a held trigger.
          if (ahead >= AI_FIRE_ALIGN && dist <= static_cast<double>(sc->range))
            wantsFire.push_back(_self);
        }
        return;
      }

      _in.throttle = Detail::MoveThrottle(dist, ahead);
    });

    return wantsFire;
  }
}
