#pragma once

// AiSystem - NPC combat tactics over the intent flight model (GameLogic, G5).
//
// A faithful port of the legacy tactics()/track_object() pair (swat.cpp:612/465)
// to the authoritative server. NPCs stop being stationary turrets: an AiPilot
// entity steers and throttles by writing its own FlightIntent - the SAME
// authority boundary as a connected client (everything flies by intent; only
// StepFlightInput turns intent into motion through the ship's FlightCaps).
//
// The legacy rotation model needs one translation. There, rotx/rotz were
// COUNTDOWN TIMERS, not rates: rotate_x_first() turned by a fixed ~1/19 rad per
// frame and the value just decayed by 1 per frame, while tactics() re-decided
// every 8th frame. So "rotx = 3" meant pitch-for-3-of-8-frames - an effective
// rate of 3/8 * 1/19 - and the hard turn 7 meant 7/8 * 1/19. Our intents hold
// between AI steps, so the same feel falls out exactly: NPC FlightCaps get a max
// turn rate of 7/8 * 1/19 = 7/152 rad/tick and the AI writes axis = legacyRate/7.
// Legacy speed worked the same way (acceleration nudged velocity, clamped to the
// hull, floored at 1 - NPCs never quite stop), ported as throttle nudges.
//
// Behaviour ported (stages 1-3 of the G5 plan):
//   - pursue: steer nose onto the target (roll-then-pitch with the legacy
//     deadzones/couplings), throttle by range and alignment;
//   - break off: inside the legacy close box a random pitch + full throttle
//     peels away instead of ramming; the bravery roll gates attack runs;
//   - jink: the 1-in-25 random roll latch (legacy rotz | 0x68);
//   - self-preservation: energy regen, the panic missile under half energy, and
//     the legacy eject (escape capsule + INACTIVE) translated to "flee": stop
//     firing and run - a fled ship despawns once clear of every enemy;
//   - scheduling: each ship thinks every 8th tick ((index ^ tick) & 7), so a
//     fleet staggers its decisions exactly like the legacy update loop.
//
// Pure apart from the world it mutates and the caller-owned RNG stream, so every
// rule is unit-tested headlessly; the server loop calls StepAi() before Tick().

#include <cmath>
#include <cstdint>
#include <vector>

#include "ECS.h"
#include "Vector3i64.h"
#include "Vector3d.h"

#include "SimComponents.h"
#include "FlightInput.h"      // FlightIntent, FlightCaps
#include "CombatSystem.h"     // Combatant, Team, PlayerTag, Wanted
#include "MissileSystem.h"    // SpawnMissile (the panic launch)

namespace Neuron::GameLogic
{
  // --- Ported constants (legacy swat.cpp values; see file comment) ------------

  // The fixed legacy turn step was 1/19 rad per frame, applied for rotx-of-8
  // frames. Max legacy magnitude 7 => effective max rate 7/8 * 1/19 rad/tick.
  inline constexpr double NPC_MAX_TURN_RATE = 7.0 / 152.0;

  // Normal tracking used magnitude 3, the behind-you hard turn 7; as intent axes
  // (fractions of the max) that is 3/7 and 1.
  inline constexpr double AI_TRACK_AXIS = 3.0 / 7.0;

  // track_object's steering deadzone: an axis only engages when |dot| * 2
  // clears 0.111; below that the nose is considered on-axis.
  inline constexpr double AI_DEADZONE = 0.111;

  // Target more than ~149 deg off the nose (direction < -0.861): hard pitch.
  inline constexpr double AI_BEHIND = -0.861;

  // The attack-run block engages when the target sits within ~33.6 deg of the
  // nose (legacy direction <= -0.833 on the away vector) and inside 8192 units.
  inline constexpr double AI_FIRE_ALIGN = 0.833;
  inline constexpr int64_t AI_FIRE_DIST = 8192;

  // The legacy close box (player-frame |z| < 768, |x| < 512, |y| < 512): inside
  // it the ship breaks off instead of pressing; outside it the bravery roll can
  // commit an attack run. Ported into the TARGET's frame.
  inline constexpr double AI_BOX_NOSE = 768.0;
  inline constexpr double AI_BOX_SIDE = 512.0;

  // Attacking inside this range uses the brake-not-overshoot throttle rules.
  inline constexpr int64_t AI_CLOSE_DIST = 2048;

  // Throttle thresholds by alignment: ease off when the goal is behind
  // (<= -0.167), speed up when comfortably ahead (>= 0.223).
  inline constexpr double AI_SLOW_ALIGN = -0.167;
  inline constexpr double AI_FAST_ALIGN = 0.223;

  // Legacy acceleration nudged velocity by +3 / -1 against a Viper-class max of
  // 32, floored at 1 (an NPC never fully stops), with a cruise floor of 6.
  inline constexpr double AI_ACCEL_STEP = 3.0 / 32.0;
  inline constexpr double AI_BRAKE_STEP = 1.0 / 32.0;
  inline constexpr double AI_MIN_THROTTLE = 1.0 / 32.0;
  inline constexpr double AI_CRUISE_THROTTLE = 6.0 / 32.0;

  // A Viper outruns the default player hull by the legacy ratio (32 vs the
  // Cobra's 28, scaled onto our 100-unit player cap): the police CAN catch you.
  inline constexpr double NPC_MAX_SPEED = 114.0;

  // Beyond this Chebyshev range an NPC has no target and just cruises. Wider
  // than the 6000-9000 spawn spread, so a fresh pirate hunts immediately.
  inline constexpr int64_t AI_ENGAGE_RANGE = 16384;

  // --- Autopilot (trader lane) constants: the fly_to_vector() variant ---------
  // (pilot.cpp:33) - a wider deadzone than combat tracking, dropped entirely when
  // the waypoint is far behind, and full throttle only once well-aimed.
  inline constexpr double AP_DEADZONE = 0.1666;
  inline constexpr double AP_RELAX = -0.6666;      // waypoint behind: deadzone off
  inline constexpr double AP_FAST_ALIGN = 0.8055;  // legacy cnt2 for the autopilot

  // A trader "docks" (despawns) within this range of its lane endpoint, and
  // lumbers along at shuttle speed (legacy shuttle velocity 8 vs the Viper's 32,
  // on our 114-unit Viper scale).
  inline constexpr int64_t TRADER_DOCK_RANGE = 1500;
  inline constexpr double TRADER_MAX_SPEED = 30.0;

  // An ambient trader's flight plan: fly to `dest`, dock (despawn) on arrival.
  // Traders are cowards - hurt below half energy they abandon the lane and flee.
  struct TradeLane
  {
    Math::Vector3i64 dest{};
  };

  // The NPC flight envelope (see the rate derivation above).
  [[nodiscard]] inline FlightCaps NpcFlightCaps()
  {
    return FlightCaps{ NPC_MAX_TURN_RATE, NPC_MAX_TURN_RATE, NPC_MAX_SPEED };
  }

  // The AI's per-ship state: disposition + the ported legacy fields. `throttle`
  // is the persistent speed setting the legacy velocity integrated toward;
  // `jinkTicks` latches the random evasive roll (legacy |rotz| >= 16 locked the
  // roll out of track_object until it decayed).
  struct AiPilot
  {
    int bravery = 64;         // 0..127; gates attack-run commitment (legacy bravery)
    int missiles = 0;         // panic-missile ammunition
    int maxEnergy = 80;       // hull max, for regen + the flee thresholds
    double throttle = AI_CRUISE_THROTTLE;
    bool fleeing = false;     // the legacy eject, translated: run, don't fight
    int jinkTicks = 0;        // AI steps the current jink roll stays latched
    double jinkRoll = 0.0;    // and its full-deflection direction
  };

  namespace Detail
  {
    // Numerical-Recipes LCG (the engine forbids wall-clock RNG); caller owns the
    // stream, so NPC behaviour is reproducible and unit-testable.
    [[nodiscard]] inline uint32_t AiRand255(uint32_t& _rng)
    {
      _rng = _rng * 1664525u + 1013904223u;
      return (_rng >> 8) & 0xFFu;   // higher bits: the LCG's low bits alternate
    }

    // Steer the nose onto `_want` (unit, world frame) - the exact track_object()
    // port. Pitch engages off the roof-axis error, roll off the side-axis error
    // (each behind `_deadzone` - combat tracking's 0.111, or the autopilot's
    // wider 0.1666), the roll sign couples to the pitch sign, and a target
    // behind the ship gets the full-rate pitch with wings level. `_lockRoll`
    // preserves the legacy jink latch (track_object skipped the roll while
    // |rotz| >= 16). Writes only the rotation axes.
    inline void SteerToward(const Flight& _f, const Math::Vector3d& _want, FlightIntent& _out,
                            bool _lockRoll, double _deadzone = AI_DEADZONE)
    {
      const double ahead = Math::Dot(_want, _f.nose);
      const double up = Math::Dot(_want, _f.roof);

      if (ahead < AI_BEHIND)
      {
        _out.pitchAxis = (up < 0.0) ? 1.0 : -1.0;   // hard pitch (legacy rotx = +/-7)
        if (!_lockRoll)
          _out.rollAxis = 0.0;
        return;
      }

      // Legacy sign map: positive rotx pitched the nose toward -roof, exactly our
      // positive Flight.pitch - so the signs carry over unchanged.
      _out.pitchAxis = (std::fabs(up) * 2.0 >= _deadzone)
        ? ((up < 0.0) ? AI_TRACK_AXIS : -AI_TRACK_AXIS)
        : 0.0;

      if (_lockRoll)
        return;

      const double side = Math::Dot(_want, _f.side);
      _out.rollAxis = 0.0;
      if (std::fabs(side) * 2.0 > _deadzone)
      {
        _out.rollAxis = (side < 0.0) ? AI_TRACK_AXIS : -AI_TRACK_AXIS;
        if (_out.pitchAxis < 0.0)
          _out.rollAxis = -_out.rollAxis;   // legacy roll/pitch sign coupling
      }
    }

    // Nudge the persistent throttle by a legacy acceleration step, clamped to
    // the hull's [never-stop, full] band.
    inline void Nudge(AiPilot& _ai, double _step)
    {
      _ai.throttle += _step;
      if (_ai.throttle > 1.0) _ai.throttle = 1.0;
      if (_ai.throttle < AI_MIN_THROTTLE) _ai.throttle = AI_MIN_THROTTLE;
    }

    // The legacy default throttle rules, shared by the chase and evade paths:
    // ease off when the goal is behind, push when it is ahead, otherwise hold
    // near the cruise floor with the occasional random ease-off.
    inline void ThrottleByAlignment(AiPilot& _ai, double _ahead, uint32_t& _rng)
    {
      if (_ahead <= AI_SLOW_ALIGN)      { Nudge(_ai, -AI_BRAKE_STEP); return; }
      if (_ahead >= AI_FAST_ALIGN)      { Nudge(_ai, AI_ACCEL_STEP);  return; }
      if (_ai.throttle < AI_CRUISE_THROTTLE) Nudge(_ai, AI_ACCEL_STEP);
      else if (AiRand255(_rng) >= 200)       Nudge(_ai, -AI_BRAKE_STEP);
    }

    // The AI's chosen prey plus the target memory it maintains (stage 4).
    struct AiTarget
    {
      ECS::EntityId id;
      Math::Vector3i64 pos;
      bool found = false;
    };

    // Pick `_me`'s target and keep its focus memory honest. A live, in-range,
    // still-legitimate focus wins outright - that is the memory (police stay on
    // the offender they were dispatched for; the lock self-heals the moment the
    // offender dies clean or escapes). Otherwise scan: police only engage lawful
    // prey (PoliceMayEngage), pirates prefer CIVILIANS (players/traders) over
    // the police shooting at them, and nobody attack-runs a station. The chosen
    // index is written back to `_me.focus`, so StepCombat fires at the same prey
    // the pilot is flying against.
    [[nodiscard]] inline AiTarget FindTarget(ECS::Registry& _world, ECS::EntityId _self,
                                             const Math::Vector3i64& _pos, Combatant& _me)
    {
      auto inRange = [&_pos](const Math::Vector3i64& _p) -> bool
      {
        const int64_t ax = _p.x > _pos.x ? _p.x - _pos.x : _pos.x - _p.x;
        const int64_t ay = _p.y > _pos.y ? _p.y - _pos.y : _pos.y - _p.y;
        const int64_t az = _p.z > _pos.z ? _p.z - _pos.z : _pos.z - _p.z;
        return ax <= AI_ENGAGE_RANGE && ay <= AI_ENGAGE_RANGE && az <= AI_ENGAGE_RANGE;
      };

      // 1. Honor the memory while it stays legitimate.
      if (_me.focus != ECS::INVALID_INDEX)
      {
        const ECS::EntityId locked = _world.LiveEntity(_me.focus);
        const WorldTransform* lt = _world.IsValid(locked) ? _world.TryGet<WorldTransform>(locked) : nullptr;
        const Combatant* lc = _world.IsValid(locked) ? _world.TryGet<Combatant>(locked) : nullptr;
        if (lt != nullptr && lc != nullptr && lc->team != _me.team && inRange(lt->position)
            && (_me.team != Team::Police || PoliceMayEngage(_world, locked, lc->team)))
          return AiTarget{ locked, lt->position, true };
        _me.focus = ECS::INVALID_INDEX;   // dead, escaped, or no longer lawful prey
      }

      // 2. Scan - tracking the nearest civilian and the nearest anything
      //    separately so a pirate's preference costs one pass.
      AiTarget bestAny, bestCivilian;
      int64_t bestAnyDist = 0, bestCivilianDist = 0;
      _world.Each<WorldTransform, Combatant>([&](ECS::EntityId _id, WorldTransform& _t, Combatant& _c)
      {
        if (_id == _self || _c.team == _me.team)
          return;
        if (_c.team == Team::Station)
          return;   // stations are crime tripwires, not prey - nobody attack-runs one
        if (_me.team == Team::Police && !PoliceMayEngage(_world, _id, _c.team))
          return;   // the law does not hunt the innocent
        if (!inRange(_t.position))
          return;

        const int64_t ax = _t.position.x > _pos.x ? _t.position.x - _pos.x : _pos.x - _t.position.x;
        const int64_t ay = _t.position.y > _pos.y ? _t.position.y - _pos.y : _pos.y - _t.position.y;
        const int64_t az = _t.position.z > _pos.z ? _t.position.z - _pos.z : _pos.z - _t.position.z;
        const int64_t d = ax + ay + az;   // overflow-safe (small in-range deltas)

        if (!bestAny.found || d < bestAnyDist)
        {
          bestAny = AiTarget{ _id, _t.position, true };
          bestAnyDist = d;
        }
        const bool civilian = (_c.team == Team::Trader) || (_world.TryGet<PlayerTag>(_id) != nullptr);
        if (civilian && (!bestCivilian.found || d < bestCivilianDist))
        {
          bestCivilian = AiTarget{ _id, _t.position, true };
          bestCivilianDist = d;
        }
      });

      const AiTarget& chosen = (_me.team == Team::Pirate && bestCivilian.found) ? bestCivilian : bestAny;
      _me.focus = chosen.found ? chosen.id.index : ECS::INVALID_INDEX;
      return chosen;
    }
  }

  // Advance NPC tactics one tick: every AiPilot whose turn it is ((index ^ tick)
  // & 7 == 0, the legacy stagger) regenerates, picks its nearest enemy, and
  // decides intent - pursue/break-off/evade/flee - plus the panic missile.
  // Returns the number of missiles launched (for the caller's logging); fled
  // ships that shook every enemy are destroyed here (their removal rides the
  // caller's despawn diff). `_scratch` (D2) is reusable per-tick working storage;
  // the default lets every existing call site (tests) omit it.
  inline int StepAi(ECS::Registry& _world, uint32_t _tick, uint32_t& _rng,
                    FrameScratch& _scratch = Detail::DefaultScratch())
  {
    // Snapshot the thinkers first: tactics can spawn missiles / despawn fled
    // ships, and the pools must not be mutated mid-iteration.
    std::vector<ECS::EntityId>& pilots = _scratch.aiPilots;
    pilots.clear();
    _world.Each<AiPilot, Combatant>([&pilots, _tick](ECS::EntityId _id, AiPilot&, Combatant&)
    {
      if (((_id.index ^ _tick) & 7u) == 0u)
        pilots.push_back(_id);
    });

    int missilesLaunched = 0;

    for (const ECS::EntityId self : pilots)
    {
      AiPilot* ai = _world.TryGet<AiPilot>(self);
      Combatant* c = _world.TryGet<Combatant>(self);
      WorldTransform* t = _world.TryGet<WorldTransform>(self);
      Flight* f = _world.TryGet<Flight>(self);
      FlightIntent* intent = _world.TryGet<FlightIntent>(self);
      if (ai == nullptr || c == nullptr || t == nullptr || f == nullptr || intent == nullptr)
        continue;

      // Legacy hull regen: one point per think while below max.
      if (c->energy < ai->maxEnergy && c->energy > 0)
        ++c->energy;

      if (ai->jinkTicks > 0)
        --ai->jinkTicks;

      // Traders (stage 5): fly the lane, dock at the far end, and bolt the moment
      // they are hurt - no bravery rolls, no missiles, no attack runs.
      if (const TradeLane* lane = _world.TryGet<TradeLane>(self))
      {
        if (!ai->fleeing && c->energy < ai->maxEnergy / 2)
          ai->fleeing = true;   // cowards: abandon the lane, run for it

        if (ai->fleeing)
        {
          const Detail::AiTarget threat = Detail::FindTarget(_world, self, t->position, *c);
          if (!threat.found)
          {
            _world.Destroy(self);   // shaken the danger: gone for good
            continue;
          }
          const double dx = static_cast<double>(threat.pos.x - t->position.x);
          const double dy = static_cast<double>(threat.pos.y - t->position.y);
          const double dz = static_cast<double>(threat.pos.z - t->position.z);
          const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
          if (dist > 0.0)
          {
            const Math::Vector3d run{ -dx / dist, -dy / dist, -dz / dist };
            Detail::SteerToward(*f, run, *intent, /*lockRoll*/ false, AP_DEADZONE);
          }
          Detail::Nudge(*ai, AI_ACCEL_STEP);
          intent->throttle = ai->throttle;
          continue;
        }

        const double dx = static_cast<double>(lane->dest.x - t->position.x);
        const double dy = static_cast<double>(lane->dest.y - t->position.y);
        const double dz = static_cast<double>(lane->dest.z - t->position.z);
        const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist <= static_cast<double>(TRADER_DOCK_RANGE))
        {
          _world.Destroy(self);   // arrived: docked/landed, off the board
          continue;
        }

        // The autopilot steering (fly_to_vector): wider deadzone, dropped when
        // the waypoint is far behind; throttle up only once well-aimed.
        const Math::Vector3d toDest{ dx / dist, dy / dist, dz / dist };
        const double ahead = Math::Dot(toDest, f->nose);
        Detail::SteerToward(*f, toDest, *intent, /*lockRoll*/ false,
                            (ahead < AP_RELAX) ? 0.0 : AP_DEADZONE);
        if (ahead <= AI_SLOW_ALIGN)      Detail::Nudge(*ai, -AI_BRAKE_STEP);
        else if (ahead >= AP_FAST_ALIGN) Detail::Nudge(*ai, AI_ACCEL_STEP);
        intent->throttle = ai->throttle;
        continue;
      }

      const Detail::AiTarget target = Detail::FindTarget(_world, self, t->position, *c);
      if (!target.found)
      {
        if (ai->fleeing)
        {
          // A fled ship that shook every enemy leaves the theatre for good.
          _world.Destroy(self);
          continue;
        }
        // Nothing to fight: level off and cruise.
        intent->rollAxis = 0.0;
        intent->pitchAxis = 0.0;
        intent->throttle = ai->throttle;
        continue;
      }

      // Unit vector to the target (doubles over the small in-range delta).
      const double dx = static_cast<double>(target.pos.x - t->position.x);
      const double dy = static_cast<double>(target.pos.y - t->position.y);
      const double dz = static_cast<double>(target.pos.z - t->position.z);
      const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (dist <= 0.0)
        continue;
      const Math::Vector3d toTarget{ dx / dist, dy / dist, dz / dist };
      const Math::Vector3d away{ -toTarget.x, -toTarget.y, -toTarget.z };
      const bool jinking = ai->jinkTicks > 0;

      if (ai->fleeing)
      {
        // The translated eject: run flat out and never re-engage.
        Detail::SteerToward(*f, away, *intent, jinking);
        Detail::Nudge(*ai, AI_ACCEL_STEP);
        if (jinking) intent->rollAxis = ai->jinkRoll;
        intent->throttle = ai->throttle;
        continue;
      }

      // Self-preservation under half energy (legacy order: eject roll first,
      // then the panic missile; either consumes the whole think).
      if (c->energy < ai->maxEnergy / 2)
      {
        if (c->energy < ai->maxEnergy / 8 && Detail::AiRand255(_rng) > 230)
        {
          ai->fleeing = true;
          c->autoEngage = false;   // legacy FLG_INACTIVE: stops fighting
          Detail::SteerToward(*f, away, *intent, jinking);
          Detail::Nudge(*ai, AI_ACCEL_STEP);
          intent->throttle = ai->throttle;
          continue;
        }

        if (ai->missiles > 0 && static_cast<uint32_t>(ai->missiles) >= (Detail::AiRand255(_rng) & 31u))
        {
          --ai->missiles;
          if (_world.IsValid(SpawnMissile(_world, self, target.id.index)))
            ++missilesLaunched;
          continue;   // the launch consumes the think, like the legacy return
        }
      }

      // The 1-in-25 random jink: latch a full-deflection roll for a while
      // (legacy rotz = rand | 0x68 held the roll for ~13-16 thinks).
      if (Detail::AiRand255(_rng) >= 250)
      {
        const uint32_t r = Detail::AiRand255(_rng) | 0x68u;
        ai->jinkRoll = (r > 127u) ? -1.0 : 1.0;
        ai->jinkTicks = static_cast<int>((r & 127u) / 8u);
      }

      const double ahead = Math::Dot(toTarget, f->nose);

      // The target's local frame decides the legacy close box (its axes when it
      // flies, world axes for a facing-less prop).
      const Math::Vector3d delta{ -dx, -dy, -dz };   // target -> us
      double lz = -dz, lx = -dx, ly = -dy;
      if (const Flight* tf = _world.TryGet<Flight>(target.id))
      {
        lz = Math::Dot(delta, tf->nose);
        lx = Math::Dot(delta, tf->side);
        ly = Math::Dot(delta, tf->roof);
      }
      const bool insideBox = std::fabs(lz) < AI_BOX_NOSE
                          && std::fabs(lx) < AI_BOX_SIDE
                          && std::fabs(ly) < AI_BOX_SIDE;

      // Attack run: close and aligned. Break off rather than ram once inside
      // the box (random pitch, full throttle); otherwise keep tracking but
      // bleed speed so the pass doesn't overshoot.
      if (dist < static_cast<double>(AI_FIRE_DIST) && ahead >= AI_FIRE_ALIGN)
      {
        if (std::fabs(lz) < AI_BOX_NOSE)
        {
          const uint32_t r = Detail::AiRand255(_rng);
          const double mag = static_cast<double>(r & 7u) / 7.0;   // legacy rotx = rand & 0x87
          intent->pitchAxis = (r & 0x80u) ? -mag : mag;
          Detail::Nudge(*ai, AI_ACCEL_STEP);
          if (jinking) intent->rollAxis = ai->jinkRoll;
          intent->throttle = ai->throttle;
          continue;
        }

        Detail::SteerToward(*f, toTarget, *intent, jinking);
        Detail::Nudge(*ai, -AI_BRAKE_STEP);
        if (jinking) intent->rollAxis = ai->jinkRoll;
        intent->throttle = ai->throttle;
        continue;
      }

      // Not on an attack run: commit to one only from outside the close box and
      // on a bravery roll; a failed roll evades instead (the legacy un-flipped
      // track keeps the nose pointed away).
      const bool attacking = !insideBox && ai->bravery > static_cast<int>(Detail::AiRand255(_rng) & 127u);
      const Math::Vector3d& want = attacking ? toTarget : away;
      const double wantAhead = attacking ? ahead : -ahead;

      Detail::SteerToward(*f, want, *intent, jinking);
      if (jinking)
        intent->rollAxis = ai->jinkRoll;

      if (attacking && dist < static_cast<double>(AI_CLOSE_DIST))
      {
        // Point-blank: brake when boring in, else hold near cruise.
        if (wantAhead >= AI_FAST_ALIGN)
          Detail::Nudge(*ai, -AI_BRAKE_STEP);
        else if (ai->throttle < AI_CRUISE_THROTTLE)
          Detail::Nudge(*ai, AI_ACCEL_STEP);
        else if (Detail::AiRand255(_rng) >= 200)
          Detail::Nudge(*ai, -AI_BRAKE_STEP);
      }
      else
      {
        Detail::ThrottleByAlignment(*ai, wantAhead, _rng);
      }

      intent->throttle = ai->throttle;
    }

    return missilesLaunched;
  }
}
