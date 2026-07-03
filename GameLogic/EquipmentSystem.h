#pragma once

// EquipmentSystem - the purchased equipment actually works (GameLogic, G8).
//
// Ports the last legacy combat gadgets to the authoritative server:
//
//   ECM          (activate_ecm/time_ecm) A burst that downs EVERY in-flight
//                missile in range - anyone's, including your own. Legacy ran a
//                32-frame window draining 1 energy per frame and blocking
//                re-triggering; ported as an instant pulse costing the same 32
//                energy with a 32-tick cooldown. NPCs get the legacy automatic
//                defence instead: each tick a missile homes on an ECM-fitted
//                target it has a 16/256 chance of being jammed (see
//                MissileSystem::StepMissiles).
//
//   Energy bomb  (detonate_bomb) One shot, consumed on use: kills every NPC
//                hull and missile in a big radius. Stations are immune (the
//                legacy Coriolis/Dodec exemption) and so are PLAYERS - the
//                legacy bubble never contained one, and an area one-shot on
//                players would be pure grief. Bombing police or traders is a
//                crime per victim.
//
//   Escape pod   (abandon_ship) Consumed on use: the ship is lost - cargo gone
//                (no canisters; the hull vanished, nothing spilled), record
//                CLEARED, tank refilled, hull/shields restored - and you wake
//                docked at the nearest station. The legacy insurance flavour,
//                without persistence. Blocked in witchspace (legacy gated the
//                key) - no free ride home from a misjump.
//
//   Laser heat   (fire_laser) Each player shot heats the laser +8 and costs 1
//                energy; at 242+ the trigger locks until it cools (1/tick,
//                StepEquipment). NPCs' lasers stay heat-free, as in the legacy.
//
// Pure apart from the world it mutates; returns outcome structs (kills flow
// through the caller's death pipeline so clients see the explosions). All rules
// unit-tested headlessly. ResolveFireWeapon dispatches the activations.

#include <cstdint>
#include <vector>

#include "ECS.h"
#include "Vector3i64.h"

#include "SimComponents.h"     // Missile, WorldTransform
#include "CombatSystem.h"      // Combatant, Team, PlayerTag, Wanted, Witchspace, EcmFitted, Kill, Shields
#include "StationServices.h"   // Equipment, CargoHold, DockState, Fuel

namespace Neuron::GameLogic
{
  // --- Laser temperature (legacy fire_laser) ---------------------------------
  inline constexpr int LASER_HEAT_PER_SHOT = 8;     // +8 per trigger pull
  inline constexpr int LASER_HEAT_BLOCK = 242;      // at or above: the trigger locks
  inline constexpr int LASER_HEAT_CAP = 255;        // the dial's ceiling
  inline constexpr int LASER_SHOT_ENERGY = 1;       // each shot drains the bank by one

  // --- ECM (legacy activate_ecm: 32 frames x 1 energy, one at a time) --------
  inline constexpr int ECM_ENERGY_COST = 32;
  inline constexpr int ECM_COOLDOWN_TICKS = 32;
  // Wide enough to cover the whole missile engagement bubble (spawn offset +
  // homing run); the legacy burst cleared the entire local bubble.
  inline constexpr int64_t ECM_RANGE = 12000;

  // --- Energy bomb (legacy detonate_bomb: cleared the local bubble) ----------
  inline constexpr int64_t ENERGY_BOMB_RADIUS = 16384;

  // Per-ship equipment state: the laser's temperature and the ECM's recharge.
  // Players get one at spawn; NPCs don't need it (no heat, auto-ECM has no
  // cooldown in the legacy either).
  struct ShipGear
  {
    int laserHeat = 0;
    int ecmCooldown = 0;
  };

  // Cool the gear one tick: the laser sheds a degree (legacy cooled 1/frame),
  // the ECM recharges.
  inline void StepEquipment(ECS::Registry& _world)
  {
    _world.Each<ShipGear>([](ECS::EntityId, ShipGear& _g)
    {
      if (_g.laserHeat > 0)   --_g.laserHeat;
      if (_g.ecmCooldown > 0) --_g.ecmCooldown;
    });
  }

  // Does this entity carry an ECM? Players via purchased Equipment, NPCs via
  // the EcmFitted marker (police and traders ship with one, most pirates not).
  [[nodiscard]] inline bool HasEcm(ECS::Registry& _world, ECS::EntityId _e)
  {
    if (const Equipment* eq = _world.TryGet<Equipment>(_e))
      return eq->ecm;
    return _world.Has<EcmFitted>(_e);
  }

  struct EcmOutcome
  {
    bool fired = false;
    std::vector<Kill> kills;   // every missile downed (killer = the pulser)
  };

  // Fire `_ship`'s ECM: requires the fitting, a recharged unit and the energy.
  // Downs EVERY missile within ECM_RANGE - friend or foe, yours included (the
  // legacy burst was indiscriminate). The kills flow through the caller's death
  // pipeline so every client sees the missiles pop.
  [[nodiscard]] inline EcmOutcome ActivateEcm(ECS::Registry& _world, ECS::EntityId _ship)
  {
    EcmOutcome out;

    const WorldTransform* t = _world.TryGet<WorldTransform>(_ship);
    Combatant* c = _world.TryGet<Combatant>(_ship);
    ShipGear* gear = _world.TryGet<ShipGear>(_ship);
    if (t == nullptr || c == nullptr || gear == nullptr || !HasEcm(_world, _ship))
      return out;
    if (gear->ecmCooldown > 0 || c->energy <= ECM_ENERGY_COST)
      return out;

    c->energy -= ECM_ENERGY_COST;
    gear->ecmCooldown = ECM_COOLDOWN_TICKS;
    out.fired = true;

    const Math::Vector3i64 origin = t->position;
    _world.Each<Missile, WorldTransform>([&](ECS::EntityId _id, Missile&, WorldTransform& _mt)
    {
      const int64_t ax = _mt.position.x > origin.x ? _mt.position.x - origin.x : origin.x - _mt.position.x;
      const int64_t ay = _mt.position.y > origin.y ? _mt.position.y - origin.y : origin.y - _mt.position.y;
      const int64_t az = _mt.position.z > origin.z ? _mt.position.z - origin.z : origin.z - _mt.position.z;
      if (ax <= ECM_RANGE && ay <= ECM_RANGE && az <= ECM_RANGE)
        out.kills.push_back(Kill{ _id, _ship.index });
    });
    return out;
  }

  struct BombOutcome
  {
    bool detonated = false;
    std::vector<Kill> kills;   // every hull/missile caught in the blast
  };

  // Detonate `_ship`'s energy bomb: one shot, consumed on use, in flight only.
  // Kills every NPC Combatant and every missile within the radius; stations and
  // players are immune (see the file comment). The caller flags the crimes
  // (police/trader victims) before feeding the kills to the death pipeline.
  [[nodiscard]] inline BombOutcome DetonateEnergyBomb(ECS::Registry& _world, ECS::EntityId _ship)
  {
    BombOutcome out;

    const WorldTransform* t = _world.TryGet<WorldTransform>(_ship);
    Equipment* eq = _world.TryGet<Equipment>(_ship);
    const DockState* dock = _world.TryGet<DockState>(_ship);
    if (t == nullptr || eq == nullptr || !eq->energyBomb)
      return out;
    if (dock != nullptr && dock->docked)
      return out;

    eq->energyBomb = false;   // consumed
    out.detonated = true;

    const Math::Vector3i64 origin = t->position;
    auto inBlast = [&origin](const Math::Vector3i64& _p) -> bool
    {
      const int64_t ax = _p.x > origin.x ? _p.x - origin.x : origin.x - _p.x;
      const int64_t ay = _p.y > origin.y ? _p.y - origin.y : origin.y - _p.y;
      const int64_t az = _p.z > origin.z ? _p.z - origin.z : origin.z - _p.z;
      return ax <= ENERGY_BOMB_RADIUS && ay <= ENERGY_BOMB_RADIUS && az <= ENERGY_BOMB_RADIUS;
    };

    _world.Each<WorldTransform, Combatant>([&](ECS::EntityId _id, WorldTransform& _wt, Combatant& _c)
    {
      if (_id == _ship || _c.team == Team::Station)
        return;   // the fortress shrugs it off (legacy Coriolis/Dodec exemption)
      if (_world.TryGet<PlayerTag>(_id) != nullptr)
        return;   // players are immune - no area one-shots on people
      if (inBlast(_wt.position))
        out.kills.push_back(Kill{ _id, _ship.index });
    });
    _world.Each<Missile, WorldTransform>([&](ECS::EntityId _id, Missile&, WorldTransform& _mt)
    {
      if (inBlast(_mt.position))
        out.kills.push_back(Kill{ _id, _ship.index });
    });
    return out;
  }

  // Eject in `_ship`'s escape pod (legacy abandon_ship): consumed on use, in
  // flight only, never in witchspace. The ship is lost - hold emptied (nothing
  // spilled), record CLEARED, tank refilled, hull/shields restored with respawn
  // grace - and the survivor wakes docked at the nearest station. Returns true
  // when the pod fired (the caller notifies the owning client).
  [[nodiscard]] inline bool UseEscapePod(ECS::Registry& _world, ECS::EntityId _ship)
  {
    Equipment* eq = _world.TryGet<Equipment>(_ship);
    const DockState* dock = _world.TryGet<DockState>(_ship);
    if (eq == nullptr || !eq->escapePod)
      return false;
    if (dock != nullptr && dock->docked)
      return false;
    if (_world.Has<Witchspace>(_ship))
      return false;   // no free ride home from a misjump (legacy key gate)

    eq->escapePod = false;   // consumed

    if (CargoHold* hold = _world.TryGet<CargoHold>(_ship))
      for (int& units : hold->units)
        units = 0;           // the cargo went down with the ship
    if (Wanted* w = _world.TryGet<Wanted>(_ship))
      w->level = 0;          // legacy: legal_status = 0
    if (Fuel* fuel = _world.TryGet<Fuel>(_ship))
      fuel->tenths = fuel->max;   // legacy: full tank in the new hull
    if (Combatant* c = _world.TryGet<Combatant>(_ship))
    {
      c->energy = MAX_ENERGY;
      c->invulnTicks = RESPAWN_GRACE_TICKS;
    }
    if (Shields* sh = _world.TryGet<Shields>(_ship))
    {
      sh->front = MAX_SHIELD;
      sh->aft = MAX_SHIELD;
    }

    RespawnAtNearestStation(_world, _ship);   // wake docked (falls back to in-place)
    return true;
  }

  // Gate + bookkeeping for one player laser shot (legacy fire_laser): blocked at
  // 242+, else +8 heat and -1 energy (the bank never drops below 1 from firing).
  // Returns whether the shot may fire. NPCs have no ShipGear and always may.
  [[nodiscard]] inline bool SpendLaserShot(ECS::Registry& _world, ECS::EntityId _ship)
  {
    ShipGear* gear = _world.TryGet<ShipGear>(_ship);
    if (gear == nullptr)
      return true;
    if (gear->laserHeat >= LASER_HEAT_BLOCK)
      return false;

    gear->laserHeat += LASER_HEAT_PER_SHOT;
    if (gear->laserHeat > LASER_HEAT_CAP)
      gear->laserHeat = LASER_HEAT_CAP;
    if (Combatant* c = _world.TryGet<Combatant>(_ship); c != nullptr && c->energy > 1)
      c->energy -= LASER_SHOT_ENERGY;
    return true;
  }
}
