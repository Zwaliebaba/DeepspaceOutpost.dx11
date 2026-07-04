#pragma once

// MissileSystem - server-authoritative homing missiles (GameLogic).
//
// The legacy missile was a flying SHIP_MISSILE object that homed onto a locked
// target and detonated on contact; this restores that as a real ECS entity rather
// than the earlier instant-hit shortcut. SpawnMissile() launches one from a
// shooter toward the best enemy in its forward lock cone; StepMissiles() (run each
// tick, like StepCombat) steers every live missile at its target, advances it, and
// detonates on contact - returning the kills so the server broadcasts deaths and
// despawns the wreck exactly as it does for laser fire. Spent missiles are
// destroyed directly; their disappearance rides the normal despawn diff.
//
// Missiles carry a Flight purely so their orientation replicates (the client draws
// them pointing along travel); StepFlight skips Missile entities, so StepMissiles
// is their sole integrator.

#include <cmath>
#include <cstdint>
#include <vector>

#include "ECS.h"
#include "Vector3i64.h"
#include "Vector3d.h"

#include "SimComponents.h"
#include "CombatSystem.h"      // Combatant, Kill, Team, EcmFitted
#include "StationServices.h"   // Equipment (a player target's ECM fitting)

namespace Neuron::GameLogic
{
  inline constexpr int64_t MISSILE_SPEED = 180;            // world units per tick (outruns ships)
  inline constexpr int MISSILE_LIFE = 240;                 // ticks before self-destruct (~8s at 30 Hz)
  inline constexpr int MISSILE_HIT_DAMAGE = 250;           // detonation damage (one-shots typical NPCs)
  inline constexpr int64_t MISSILE_DETONATE_RANGE = 400;   // contact radius around the target
  inline constexpr int64_t MISSILE_SPAWN_OFFSET = 250;     // launch this far ahead of the shooter

  // The legacy automatic ECM defence (missile_tactics): each tick a missile is
  // homing on an ECM-fitted target, a rand255() below this jams it (16/256 =
  // 6.25% per tick - a homing run rarely survives a fitted target).
  inline constexpr uint32_t MISSILE_ECM_JAM_CHANCE = 16;

  // Launch a homing missile from `_shooter` at the target the player locked (its
  // entity index, as identified from the replicated view; resolved here to a live
  // handle). The missile is a real entity (drawn as SHIP_MISSILE) that chases that
  // specific target over several ticks; if the locked target is gone it flies
  // straight ahead and self-destructs. There is NO auto-targeting - the lock is the
  // player's, taken with the T key. Returns the missile entity (invalid only if the
  // shooter lacks a transform/flight/combatant).
  inline ECS::EntityId SpawnMissile(ECS::Registry& _world, ECS::EntityId _shooter, uint32_t _targetIndex)
  {
    WorldTransform* st = _world.TryGet<WorldTransform>(_shooter);
    Flight* sf = _world.TryGet<Flight>(_shooter);
    Combatant* sc = _world.TryGet<Combatant>(_shooter);
    if (st == nullptr || sf == nullptr || sc == nullptr)
      return ECS::EntityId{};

    const Math::Vector3d nose = sf->nose;
    const Math::Vector3i64 origin = st->position;

    // The target the player locked. Never the shooter itself; invalid -> dumb-fire.
    ECS::EntityId lock = _world.LiveEntity(_targetIndex);
    if (lock == _shooter)
      lock = ECS::EntityId{};
    const bool found = _world.IsValid(lock);

    const ECS::EntityId m = _world.Create();

    Flight mf;
    mf.nose = nose;
    mf.roof = sf->roof;
    mf.side = sf->side;
    mf.speed = static_cast<double>(MISSILE_SPEED);
    _world.Add<Flight>(m, mf);

    const Math::Vector3i64 spawnPos{
      origin.x + static_cast<int64_t>(nose.x * static_cast<double>(MISSILE_SPAWN_OFFSET)),
      origin.y + static_cast<int64_t>(nose.y * static_cast<double>(MISSILE_SPAWN_OFFSET)),
      origin.z + static_cast<int64_t>(nose.z * static_cast<double>(MISSILE_SPAWN_OFFSET)),
    };
    _world.Add<WorldTransform>(m, WorldTransform{ spawnPos });
    _world.Add<NetType>(m, NetType{ ShipType::Missile });

    Missile mc;
    mc.target = found ? lock : ECS::EntityId{};
    mc.owner = _shooter.index;
    mc.damage = MISSILE_HIT_DAMAGE;
    mc.life = MISSILE_LIFE;
    mc.speed = static_cast<double>(MISSILE_SPEED);
    _world.Add<Missile>(m, mc);

    return m;
  }

  // Advance every in-flight missile one tick: home toward its (still-living) target,
  // move along its nose, and detonate within MISSILE_DETONATE_RANGE - or self-
  // destruct when its life runs out. Returns kills for the server to broadcast +
  // destroy (exactly like StepCombat): on detonation, both the target (if it died)
  // AND the missile itself, so the missile's explosion shows on the client. A
  // missile that simply times out is destroyed here and vanishes silently.
  //
  // G8: while homing on an ECM-FITTED target (a player's purchased unit or an
  // NPC's EcmFitted marker), each tick the missile has the legacy 16/256 chance
  // of being jammed: it dies as a kill credited to the defender (so its pop shows
  // on every client) and the defender's index is appended to `_ecmPulses` so the
  // server can broadcast the classic ECM cue. `_rng` is a caller-owned
  // deterministic stream. `_scratch` (D2) is reusable per-tick working storage;
  // the default lets every existing call site (tests) omit it.
  [[nodiscard]] inline std::vector<Kill> StepMissiles(ECS::Registry& _world, uint32_t& _rng,
                                                      std::vector<uint32_t>& _ecmPulses,
                                                      FrameScratch& _scratch = Detail::DefaultScratch())
  {
    std::vector<Kill> kills;

    // Snapshot the live missile ids first, since we Destroy() as we go.
    std::vector<ECS::EntityId>& missiles = _scratch.missileIds;
    missiles.clear();
    _world.Each<Missile, WorldTransform>([&missiles](ECS::EntityId _id, Missile&, WorldTransform&)
    {
      missiles.push_back(_id);
    });

    auto targetHasEcm = [&_world](ECS::EntityId _t) -> bool
    {
      if (const Equipment* eq = _world.TryGet<Equipment>(_t))
        return eq->ecm;
      return _world.Has<EcmFitted>(_t);
    };

    for (const ECS::EntityId mid : missiles)
    {
      Missile* mc = _world.TryGet<Missile>(mid);
      WorldTransform* mt = _world.TryGet<WorldTransform>(mid);
      Flight* mf = _world.TryGet<Flight>(mid);
      if (mc == nullptr || mt == nullptr || mf == nullptr)
        continue;

      if (--mc->life <= 0)
      {
        _world.Destroy(mid);
        continue;
      }

      // Home toward the target while it is alive.
      if (_world.IsValid(mc->target) && _world.Has<WorldTransform>(mc->target))
      {
        // The target's automatic ECM defence rolls before the missile closes.
        if (targetHasEcm(mc->target))
        {
          _rng = _rng * 1664525u + 1013904223u;
          if (((_rng >> 8) & 0xFFu) < MISSILE_ECM_JAM_CHANCE)
          {
            kills.push_back(Kill{ mid, mc->target.index });   // jammed: the missile pops
            _ecmPulses.push_back(mc->target.index);
            continue;
          }
        }

        const Math::Vector3i64 tp = _world.Get<WorldTransform>(mc->target).position;
        const double dx = static_cast<double>(tp.x - mt->position.x);
        const double dy = static_cast<double>(tp.y - mt->position.y);
        const double dz = static_cast<double>(tp.z - mt->position.z);
        const double len = std::sqrt(dx * dx + dy * dy + dz * dz);

        // Detonate on contact: damage the target, record a kill if it died.
        if (len <= static_cast<double>(MISSILE_DETONATE_RANGE))
        {
          if (Combatant* tc = _world.TryGet<Combatant>(mc->target))
          {
            if (tc->invulnTicks <= 0)   // respect spawn/respawn grace
            {
              // A player target absorbs the blast through the shield facing the
              // incoming missile; an NPC takes it flat on energy.
              if (ApplyDamage(_world, mc->target, mc->damage, mt->position))
                kills.push_back(Kill{ mc->target, mc->owner });
            }
          }
          // Report the missile itself as a "kill" so the server broadcasts its
          // EntityDeath (the client plays the explosion + drops it) and destroys
          // the projectile - rather than a silent despawn that just vanishes.
          // (Don't Destroy() here; the server's kill loop does.)
          kills.push_back(Kill{ mid, mc->owner });
          continue;
        }

        // Steer the nose straight at the target (homing).
        if (len > 0.0)
          mf->nose = Math::Vector3d{ dx / len, dy / len, dz / len };
      }

      // Advance along the (possibly re-aimed) nose.
      mt->position += Math::Vector3i64{
        static_cast<int64_t>(mf->nose.x * mc->speed),
        static_cast<int64_t>(mf->nose.y * mc->speed),
        static_cast<int64_t>(mf->nose.z * mc->speed),
      };
    }

    return kills;
  }
}
