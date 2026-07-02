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
#include <string>
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

    // NPC weapon pacing: an auto-engaging combatant fires at most once every
    // `fireInterval` ticks (the timer counts down). This stops a single NPC from
    // being an every-tick damage stream. The timer starts ready (0) so the first
    // shot lands immediately. Players fire on command and ignore this.
    int fireInterval = 10;
    int fireTimer = 0;

    // Post-spawn / post-respawn damage immunity. While > 0 the combatant takes no
    // damage and it ticks down each combat step, so a respawn-in-place cannot be
    // instantly re-killed by hostiles that are still in range.
    int invulnTicks = 0;
  };

  // Ticks of damage-immunity granted on spawn and respawn (~5s at 30 Hz), giving
  // a respawned player time to flee or fight instead of being re-killed in place.
  inline constexpr int RESPAWN_GRACE_TICKS = 150;

  // Marks an entity controlled by a connected player.
  struct PlayerTag {};

  // A player's criminal record; > 0 means the police will hunt them.
  struct Wanted
  {
    int level = 0;
  };

  // At or above this wanted level a player is a FUGITIVE: stations refuse them
  // docking (StationServices), and they are fair game to attack without penalty.
  // A clean player (level 0) is protected - shooting one is a crime. The 1..threshold-1
  // band is an "offender": legal to attack, still allowed to dock. Tunable.
  inline constexpr int FUGITIVE_THRESHOLD = 8;

  // A connected player's identity/record: chosen display name + kill score. The
  // name is client-supplied at connect (ClientHello), sanitized/de-duplicated
  // server-side (ServerSessions). Session-scoped for now; Phase F will persist it.
  struct PlayerRecord
  {
    std::string name;
    int score = 0;
  };

  // Cool a player's wanted record down by one level (min 0), once per call; the
  // caller gates the cadence (e.g. every N ticks). Returns the entity indices whose
  // level actually changed so the server can refresh their roster entry.
  [[nodiscard]] inline std::vector<uint32_t> DecayWanted(ECS::Registry& _world)
  {
    std::vector<uint32_t> changed;
    _world.Each<PlayerTag, Wanted>([&changed](ECS::EntityId _id, PlayerTag&, Wanted& _w)
    {
      if (_w.level > 0)
      {
        --_w.level;
        changed.push_back(_id.index);
      }
    });
    return changed;
  }

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

      if (a.c->fireTimer > 0)
      {
        --a.c->fireTimer;   // weapon still cooling down this tick
        continue;
      }

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
        a.c->fireTimer = a.c->fireInterval;   // begin the cooldown after firing
      }
    }

    // Apply damage, then collect deaths. Invulnerable combatants take none (and
    // their grace ticks down here, once per combat step).
    std::vector<Kill> kills;
    for (const Unit& u : units)
    {
      if (u.c->invulnTicks > 0)
      {
        --u.c->invulnTicks;
        continue;
      }

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
      if (_id == _shooter)
        return;
      // Enemies (different team) are valid targets; so are OTHER PLAYERS, even
      // though every player shares the Player team - that is how PvP is possible
      // at all. Same-team NPC allies (none today) stay protected from friendly fire.
      const bool otherPlayer = (_world.TryGet<PlayerTag>(_id) != nullptr);
      if (_c.team == sc->team && !otherPlayer)
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

    if (tc->invulnTicks > 0)
      return out;   // target is in spawn/respawn grace - the shot passes through

    tc->energy -= LaserDamageTo(TargetClass::Normal, sc->laserStrength);

    out.hit = true;
    out.target = best;
    out.targetTeam = tc->team;
    out.destroyed = tc->energy <= 0;
    return out;
  }
}
