#pragma once

// CombatMessages - the server's in-process combat facts/commands (GameLogic).
//
// Phase 1 of the message-system migration: the server stops hand-tangling combat
// effects (crime, death, respawn, despawn) inside the main loop and instead routes
// them as messages on an in-process MessageBus. The combat *math* is unchanged -
// ResolveFireWeapon still calls the existing, unit-tested ResolvePlayerFire /
// SpawnMissile - only its OUTCOMES become published facts that independent
// subscribers react to (damage/death, crime, presentation), the decoupling the
// design is built around.
//
// These are server-internal facts (MessageScope::LocalOnly, ids in the non-wire id
// half): EntityKilled drives the server's death handling, which broadcasts the
// catalog wire EntityDeath (Messages/Defs/CoreEvents.h) to clients. FireWeapon is
// the command the client will eventually send; in Phase 1 the server synthesises it
// from ClientInput so the same resolution path serves both today and tomorrow.
//
// Header-only and server-only (GameLogic): it builds on the header-only Msg
// mechanism in NeuronCore and the existing combat systems; the client links none
// of it.

#include <cstdint>

#include "ECS.h"

#include "Messages/MessageId.h"
#include "Messages/MessageTraits.h"
#include "Messages/MessageBus.h"

#include "SimComponents.h"
#include "CombatSystem.h"      // Combatant, Team, Wanted, FireOutcome, ResolvePlayerFire
#include "MissileSystem.h"     // SpawnMissile
#include "EquipmentSystem.h"   // ActivateEcm, DetonateEnergyBomb, UseEscapePod, SpendLaserShot

namespace Neuron::GameLogic
{
  // Combat message ids live in the LocalOnly / non-wire id half (bit 15 set), in a
  // dedicated combat sub-band, so the catalog's scope/id-range rule holds.
  namespace CombatMsgId
  {
    inline constexpr uint16_t FireWeapon   = 0x8101;
    inline constexpr uint16_t Crime        = 0x8102;
    inline constexpr uint16_t EntityKilled = 0x8103;
    inline constexpr uint16_t EcmFired     = 0x8104;
    inline constexpr uint16_t PodEjected   = 0x8105;
  }

  // What a FireWeapon command activates: the two projectile weapons, plus the
  // G8 equipment (each a discrete, server-validated activation).
  enum class Weapon : uint8_t
  {
    Laser = 0,
    Missile = 1,
    Ecm = 2,
    EnergyBomb = 3,
    EscapePod = 4,
  };

  // A request to fire a weapon. In Phase 1 the server publishes this from a
  // client's ClientInput; later it becomes the wire command the client sends.
  struct FireWeapon
  {
    static constexpr Msg::MessageId    Id    = static_cast<Msg::MessageId>(CombatMsgId::FireWeapon);
    static constexpr Msg::MessageScope Scope = Msg::MessageScope::LocalOnly;
    static constexpr Msg::MessageKind  Kind  = Msg::MessageKind::Command;
    static constexpr Msg::MessageLane  Lane  = Msg::MessageLane::Unreliable;
    static constexpr Msg::Direction    Dir   = Msg::Direction::None;

    ECS::EntityId shooter{};
    Weapon weapon = Weapon::Laser;
    uint32_t target = 0xFFFFFFFFu;   // missile lock (entity index), else sentinel

    auto Fields()       { return std::tie(shooter, weapon, target); }
    auto Fields() const { return std::tie(shooter, weapon, target); }
  };

  // A fact: an offender fired on a protected team (Station/Police). firstOffence is
  // true the first time a given offender transgresses, so a subscriber can dispatch
  // police exactly once.
  struct Crime
  {
    static constexpr Msg::MessageId    Id    = static_cast<Msg::MessageId>(CombatMsgId::Crime);
    static constexpr Msg::MessageScope Scope = Msg::MessageScope::LocalOnly;
    static constexpr Msg::MessageKind  Kind  = Msg::MessageKind::Event;
    static constexpr Msg::MessageLane  Lane  = Msg::MessageLane::Unreliable;
    static constexpr Msg::Direction    Dir   = Msg::Direction::None;

    ECS::EntityId offender{};
    int victimTeam = -1;
    bool firstOffence = false;

    auto Fields()       { return std::tie(offender, victimTeam, firstOffence); }
    auto Fields() const { return std::tie(offender, victimTeam, firstOffence); }
  };

  // A fact: an entity was driven to zero energy. The killer is an entity index (the
  // shooter / missile owner). A single subscriber decides what a death DOES
  // (respawn a player in place, or broadcast + destroy a wreck).
  struct EntityKilled
  {
    static constexpr Msg::MessageId    Id    = static_cast<Msg::MessageId>(CombatMsgId::EntityKilled);
    static constexpr Msg::MessageScope Scope = Msg::MessageScope::LocalOnly;
    static constexpr Msg::MessageKind  Kind  = Msg::MessageKind::Event;
    static constexpr Msg::MessageLane  Lane  = Msg::MessageLane::Unreliable;
    static constexpr Msg::Direction    Dir   = Msg::Direction::None;

    ECS::EntityId victim{};
    uint32_t killer = 0;

    auto Fields()       { return std::tie(victim, killer); }
    auto Fields() const { return std::tie(victim, killer); }
  };

  // A fact: a ship's ECM burst fired (G8). The server broadcasts the wire
  // EcmPulse cue; the downed missiles arrive as EntityKilled facts alongside.
  struct EcmFired
  {
    static constexpr Msg::MessageId    Id    = static_cast<Msg::MessageId>(CombatMsgId::EcmFired);
    static constexpr Msg::MessageScope Scope = Msg::MessageScope::LocalOnly;
    static constexpr Msg::MessageKind  Kind  = Msg::MessageKind::Event;
    static constexpr Msg::MessageLane  Lane  = Msg::MessageLane::Unreliable;
    static constexpr Msg::Direction    Dir   = Msg::Direction::None;

    uint32_t ship = 0;

    auto Fields()       { return std::tie(ship); }
    auto Fields() const { return std::tie(ship); }
  };

  // A fact: a player abandoned ship in the escape pod (G8) - they are already
  // respawned docked; the server tells that one session (EscapePodUsed) and
  // refreshes the roster (the record was cleared).
  struct PodEjected
  {
    static constexpr Msg::MessageId    Id    = static_cast<Msg::MessageId>(CombatMsgId::PodEjected);
    static constexpr Msg::MessageScope Scope = Msg::MessageScope::LocalOnly;
    static constexpr Msg::MessageKind  Kind  = Msg::MessageKind::Event;
    static constexpr Msg::MessageLane  Lane  = Msg::MessageLane::Unreliable;
    static constexpr Msg::Direction    Dir   = Msg::Direction::None;

    uint32_t ship = 0;

    auto Fields()       { return std::tie(ship); }
    auto Fields() const { return std::tie(ship); }
  };

  // Flag a shot/blast against a PROTECTED victim as a crime: the Station, the
  // Police, a Trader, or a CLEAN player (Elite-style PvP consequence - attacking
  // an innocent makes you wanted; a player who is already wanted is fair game).
  // The offender's record advances and the fact is published (a subscriber
  // dispatches police on the first offence). Shared by the laser/missile path
  // and the energy bomb.
  inline void FlagIfCrime(ECS::Registry& _world, Msg::MessageBus& _bus,
                          ECS::EntityId _offender, ECS::EntityId _victim, int _victimTeam)
  {
    bool protectedVictim = (_victimTeam == Team::Station || _victimTeam == Team::Police
                            || _victimTeam == Team::Trader);   // civilians are protected too
    if (_victimTeam == Team::Player)
    {
      const Wanted* vw = _world.TryGet<Wanted>(_victim);
      protectedVictim = (vw != nullptr && vw->level == 0);   // only a clean player is protected
    }
    if (!protectedVictim)
      return;
    bool first = false;
    if (Wanted* w = _world.TryGet<Wanted>(_offender))
    {
      first = (w->level == 0);
      ++w->level;
    }
    _bus.Publish(Crime{ _offender, _victimTeam, first });
  }

  // Resolve a FireWeapon command against the world, publishing the resulting facts
  // (Crime / EntityKilled) onto the bus. PURE w.r.t. external systems - it only
  // reads/writes the world and publishes - so it is unit-tested headlessly; police
  // dispatch, death broadcast and logging live in the bus subscribers (server side).
  //
  // Combat geometry is delegated unchanged to ResolvePlayerFire / SpawnMissile.
  inline void ResolveFireWeapon(ECS::Registry& _world, Msg::MessageBus& _bus,
                                const FireWeapon& _fw, int64_t _fireRange, double _aimCone)
  {
    if (!_world.IsValid(_fw.shooter))
      return;

    switch (_fw.weapon)
    {
      case Weapon::Laser:
      {
        // G8 laser heat: an overheated trigger is locked; a fired shot heats the
        // laser and sips the energy bank (legacy fire_laser) whether it hits or not.
        if (!SpendLaserShot(_world, _fw.shooter))
          return;
        const FireOutcome shot = ResolvePlayerFire(_world, _fw.shooter, _fireRange, _aimCone);
        if (!shot.hit)
          return;
        FlagIfCrime(_world, _bus, _fw.shooter, shot.target, shot.targetTeam);
        if (shot.destroyed)
          _bus.Publish(EntityKilled{ shot.target, _fw.shooter.index });
        return;
      }

      case Weapon::Missile:
      {
        // Spawn the homing projectile (its detonation kill is resolved later by
        // StepMissiles). Launching at the Station/Police is a crime at launch,
        // just as the laser hit is.
        const ECS::EntityId missile = SpawnMissile(_world, _fw.shooter, _fw.target);
        if (!_world.IsValid(missile))
          return;
        const Missile* mc = _world.TryGet<Missile>(missile);
        if (mc != nullptr && _world.IsValid(mc->target))
          if (const Combatant* tc = _world.TryGet<Combatant>(mc->target))
            FlagIfCrime(_world, _bus, _fw.shooter, mc->target, tc->team);
        return;
      }

      case Weapon::Ecm:
      {
        // The burst downs every missile in range; the kills ride the normal
        // death pipeline (their pops show on every client), the cue rides EcmFired.
        const EcmOutcome ecm = ActivateEcm(_world, _fw.shooter);
        if (!ecm.fired)
          return;
        _bus.Publish(EcmFired{ _fw.shooter.index });
        for (const Kill& k : ecm.kills)
          _bus.Publish(EntityKilled{ k.victim, k.killer });
        return;
      }

      case Weapon::EnergyBomb:
      {
        // Flag the crimes (police/trader victims) BEFORE publishing the kills,
        // while the victims still exist to be inspected.
        const BombOutcome bomb = DetonateEnergyBomb(_world, _fw.shooter);
        if (!bomb.detonated)
          return;
        for (const Kill& k : bomb.kills)
          if (const Combatant* vc = _world.TryGet<Combatant>(k.victim))
            FlagIfCrime(_world, _bus, _fw.shooter, k.victim, vc->team);
        for (const Kill& k : bomb.kills)
          _bus.Publish(EntityKilled{ k.victim, k.killer });
        return;
      }

      case Weapon::EscapePod:
      {
        if (UseEscapePod(_world, _fw.shooter))
          _bus.Publish(PodEjected{ _fw.shooter.index });
        return;
      }
    }
  }
}
