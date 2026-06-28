#pragma once

// CombatSystem - the server's realtime combat tick (GameLogic).
//
// Promotes the ported, unit-tested Combat.h primitives from "library functions"
// to a live authoritative system over the int64 world. Each tick every Combatant
// fires on the nearest ENEMY (different team) within range, damage resolves
// simultaneously through LaserDamageTo(), and anything driven to zero energy dies.
// StepCombat() returns the kills (victim + killer) so the server can broadcast
// reliable death events and despawn the wreck - the system itself stays pure (it
// only mutates energy), so it is unit-tested headlessly.

#include <cstdint>
#include <cmath>
#include <unordered_map>
#include <vector>

#include "ECS.h"
#include "Vector3i64.h"
#include "Vector3d.h"

#include "SimComponents.h"
#include "Combat.h"

namespace Neuron::GameLogic
{
  // Factions. Different teams are enemies; same team never fight. Players fire on
  // command (not auto), so they do not initiate even against enemy NPCs.
  namespace Team
  {
    inline constexpr int Player = 0;
    inline constexpr int Pirate = 1;
    inline constexpr int Police = 2;
    inline constexpr int Station = 3;
  }

  // A combatant in the realtime sim: its team, energy pool, weapon strength and
  // engagement range (world units, Chebyshev - overflow-safe on absolute coords).
  // `autoEngage` distinguishes NPCs (true: fire at the nearest enemy each tick)
  // from players (false: only fire on an explicit command), while both remain
  // valid TARGETS.
  struct Combatant
  {
    int team = 0;
    int energy = 100;
    int laserStrength = 10;
    int64_t range = 5000;
    bool autoEngage = true;
  };

  // Marks an entity controlled by a connected player.
  struct PlayerTag {};

  // A player's criminal record; > 0 means the police will hunt them.
  struct Wanted
  {
    int level = 0;
  };

  // A resolved kill this tick: the victim handle (for the caller to destroy) and
  // the killer's entity index (for the death event).
  struct Kill
  {
    ECS::EntityId victim;
    uint32_t killer = 0;
  };

  // Advance combat one tick. Returns the kills; the caller destroys the victims
  // and broadcasts death events.
  [[nodiscard]] inline std::vector<Kill> StepCombat(ECS::Registry& _world)
  {
    struct Unit
    {
      ECS::EntityId id;
      Math::Vector3i64 pos;
      Combatant* c;
    };

    std::vector<Unit> units;
    _world.Each<WorldTransform, Combatant>([&units](ECS::EntityId _id, WorldTransform& _t, Combatant& _c)
    {
      units.push_back(Unit{ _id, _t.position, &_c });
    });

    // Accumulate this tick's damage and the attacker that dealt it, so resolution
    // is simultaneous (firing order doesn't matter).
    std::unordered_map<uint32_t, int> damage;
    std::unordered_map<uint32_t, uint32_t> attacker;

    for (const Unit& a : units)
    {
      if (!a.c->autoEngage)
        continue;   // players (and inert objects) don't initiate fire

      const Unit* best = nullptr;
      int64_t bestDist2 = 0;
      for (const Unit& b : units)
      {
        if (b.c->team == a.c->team)
          continue;   // never target allies

        const int64_t dx = b.pos.x - a.pos.x;
        const int64_t dy = b.pos.y - a.pos.y;
        const int64_t dz = b.pos.z - a.pos.z;

        // In-range test is Chebyshev (no large multiplies on absolute coords);
        // only then is the squared distance computed, and only over the small
        // in-range delta, so it cannot overflow.
        const int64_t ax = dx < 0 ? -dx : dx;
        const int64_t ay = dy < 0 ? -dy : dy;
        const int64_t az = dz < 0 ? -dz : dz;
        if (ax > a.c->range || ay > a.c->range || az > a.c->range)
          continue;

        const int64_t dist2 = dx * dx + dy * dy + dz * dz;
        if (best == nullptr || dist2 < bestDist2)
        {
          best = &b;
          bestDist2 = dist2;
        }
      }

      if (best != nullptr)
      {
        damage[best->id.index] += LaserDamageTo(TargetClass::Normal, a.c->laserStrength);
        attacker[best->id.index] = a.id.index;
      }
    }

    // Apply damage, then collect deaths.
    std::vector<Kill> kills;
    for (const Unit& u : units)
    {
      const auto it = damage.find(u.id.index);
      if (it == damage.end())
        continue;

      u.c->energy -= it->second;
      if (u.c->energy <= 0)
        kills.push_back(Kill{ u.id, attacker[u.id.index] });
    }

    return kills;
  }

  // The outcome of a player firing their lasers.
  struct FireOutcome
  {
    bool hit = false;
    ECS::EntityId target;   // what was struck (valid only if hit)
    int targetTeam = -1;    // its team (so the caller can flag crimes)
    bool destroyed = false; // it died from this shot
  };

  // Resolve `_shooter` firing forward: damage the nearest enemy Combatant that
  // lies within `_range` AND inside the aiming cone around the nose (dot with the
  // unit direction >= `_cosCone`). One shot, one target - the legacy front laser.
  [[nodiscard]] inline FireOutcome ResolvePlayerFire(ECS::Registry& _world, ECS::EntityId _shooter,
                                                     int64_t _range, double _cosCone)
  {
    FireOutcome out;

    WorldTransform* st = _world.TryGet<WorldTransform>(_shooter);
    Flight* sf = _world.TryGet<Flight>(_shooter);
    Combatant* sc = _world.TryGet<Combatant>(_shooter);
    if (st == nullptr || sf == nullptr || sc == nullptr)
      return out;

    const Math::Vector3d nose = sf->nose;
    const Math::Vector3i64 origin = st->position;

    ECS::EntityId best;
    double bestLen = 0.0;
    bool found = false;

    _world.Each<WorldTransform, Combatant>([&](ECS::EntityId _id, WorldTransform& _t, Combatant& _c)
    {
      if (_id == _shooter || _c.team == sc->team)
        return;

      const int64_t dx = _t.position.x - origin.x;
      const int64_t dy = _t.position.y - origin.y;
      const int64_t dz = _t.position.z - origin.z;
      const int64_t ax = dx < 0 ? -dx : dx;
      const int64_t ay = dy < 0 ? -dy : dy;
      const int64_t az = dz < 0 ? -dz : dz;
      if (ax > _range || ay > _range || az > _range)
        return;

      const double ddx = static_cast<double>(dx);
      const double ddy = static_cast<double>(dy);
      const double ddz = static_cast<double>(dz);
      const double len = std::sqrt(ddx * ddx + ddy * ddy + ddz * ddz);
      if (len <= 0.0)
        return;

      const double dot = (ddx * nose.x + ddy * nose.y + ddz * nose.z) / len;
      if (dot < _cosCone)
        return;   // outside the aiming cone

      if (!found || len < bestLen)
      {
        found = true;
        bestLen = len;
        best = _id;
      }
    });

    if (!found)
      return out;

    Combatant* tc = _world.TryGet<Combatant>(best);
    if (tc == nullptr)
      return out;

    tc->energy -= LaserDamageTo(TargetClass::Normal, sc->laserStrength);

    out.hit = true;
    out.target = best;
    out.targetTeam = tc->team;
    out.destroyed = tc->energy <= 0;
    return out;
  }

  // Damage a single missile deals on impact. Far heavier than a laser pulse: it
  // one-shots typical NPCs (energy ~80-128) but barely scratches the near-
  // indestructible station, so launching at a station registers as a crime
  // without destroying it.
  inline constexpr int MISSILE_DAMAGE = 250;

  // Resolve `_shooter` launching a missile. A guided missile locks the nearest
  // enemy in the forward cone (same acquisition as the front laser) and detonates
  // on it for MISSILE_DAMAGE. Modelled as an instant guided hit - consistent with
  // the server's instant-laser combat - rather than a flying projectile entity.
  // Returns the same FireOutcome shape so the caller handles crimes/deaths exactly
  // as it does for laser fire.
  [[nodiscard]] inline FireOutcome ResolvePlayerMissile(ECS::Registry& _world, ECS::EntityId _shooter,
                                                        int64_t _range, double _cosCone)
  {
    FireOutcome out;

    WorldTransform* st = _world.TryGet<WorldTransform>(_shooter);
    Flight* sf = _world.TryGet<Flight>(_shooter);
    Combatant* sc = _world.TryGet<Combatant>(_shooter);
    if (st == nullptr || sf == nullptr || sc == nullptr)
      return out;

    const Math::Vector3d nose = sf->nose;
    const Math::Vector3i64 origin = st->position;

    ECS::EntityId best;
    double bestLen = 0.0;
    bool found = false;

    _world.Each<WorldTransform, Combatant>([&](ECS::EntityId _id, WorldTransform& _t, Combatant& _c)
    {
      if (_id == _shooter || _c.team == sc->team)
        return;

      const int64_t dx = _t.position.x - origin.x;
      const int64_t dy = _t.position.y - origin.y;
      const int64_t dz = _t.position.z - origin.z;
      const int64_t ax = dx < 0 ? -dx : dx;
      const int64_t ay = dy < 0 ? -dy : dy;
      const int64_t az = dz < 0 ? -dz : dz;
      if (ax > _range || ay > _range || az > _range)
        return;

      const double ddx = static_cast<double>(dx);
      const double ddy = static_cast<double>(dy);
      const double ddz = static_cast<double>(dz);
      const double len = std::sqrt(ddx * ddx + ddy * ddy + ddz * ddz);
      if (len <= 0.0)
        return;

      const double dot = (ddx * nose.x + ddy * nose.y + ddz * nose.z) / len;
      if (dot < _cosCone)
        return;   // outside the lock-on cone

      if (!found || len < bestLen)
      {
        found = true;
        bestLen = len;
        best = _id;
      }
    });

    if (!found)
      return out;

    Combatant* tc = _world.TryGet<Combatant>(best);
    if (tc == nullptr)
      return out;

    tc->energy -= MISSILE_DAMAGE;

    out.hit = true;
    out.target = best;
    out.targetTeam = tc->team;
    out.destroyed = tc->energy <= 0;
    return out;
  }
}
