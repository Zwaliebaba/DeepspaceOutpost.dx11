#pragma once

// CombatSystem - the server's realtime combat tick (GameLogic).
//
// Promotes the ported, unit-tested Combat.h primitives from "library functions"
// to a live authoritative system over the int64 world. Each tick every Combatant
// fires on the nearest ENEMY (different team) within range for its laser
// strength, damage resolves simultaneously, and anything driven to zero energy dies.
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
    inline constexpr int Trader = 4;   // ambient civilians; attacking one is a crime
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

    // Target memory (G5 stage 4): the entity index this combatant is fixed on -
    // police lock onto the actual offender, the AI keeps flight and fire on the
    // same prey. While it resolves to a live, in-range enemy it outranks the
    // nearest-enemy scan; INVALID_INDEX means "no memory, scan as before".
    uint32_t focus = ECS::INVALID_INDEX;
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

  // Marks a player stranded in WITCHSPACE (G7): a hyperspace misjump dumped them
  // into interstellar deep space to fight off a Thargoid ambush. While present,
  // kills they make pay NO bounty (legacy: witchspace bounties are withheld -
  // see KillRewards); a successful onward jump clears it.
  struct Witchspace {};

  // Marks an NPC hull fitted with an ECM (G8, the legacy FLG_HAS_ECM): a homing
  // missile has a per-tick chance of being jammed by it (StepMissiles). Players
  // carry theirs on Equipment.ecm instead.
  struct EcmFitted {};

  // At or above this wanted level a player is a FUGITIVE: stations refuse them
  // docking (StationServices), and they are fair game to attack without penalty.
  // A clean player (level 0) is protected - shooting one is a crime. The 1..threshold-1
  // band is an "offender": legal to attack, still allowed to dock. Tunable.
  inline constexpr int FUGITIVE_THRESHOLD = 8;

  // The reward paid to whoever destroys this entity (legacy tenths-of-a-credit).
  // Set at spawn on NPCs that carry a price on their head (pirates); absent => 0.
  // A wanted PLAYER's bounty is derived from their record instead (see KillRewards).
  struct Bounty
  {
    int value = 0;
  };

  // Bounty for a pirate kill (5.0 Cr) and the per-wanted-level reward for downing a
  // fugitive player (2.0 Cr each). Tunable; police/stations carry no bounty.
  inline constexpr int PIRATE_BOUNTY = 50;
  inline constexpr int WANTED_BOUNTY_PER_LEVEL = 20;

  // Legacy defensive caps: two directional shields and the energy bank (255 each).
  inline constexpr int MAX_SHIELD = 255;
  inline constexpr int MAX_ENERGY = 255;

  // A player's directional shields (legacy front_shield / aft_shield). The energy
  // bank is the entity's Combatant.energy; a hit strikes the facing shield first and
  // only the overflow drains the bank (ApplyDamageToShields). NPCs have no Shields
  // component - they take damage straight on the energy pool, as before.
  struct Shields
  {
    int front = MAX_SHIELD;
    int aft = MAX_SHIELD;
  };

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

  // Target discipline for the law (G5 stage 4): police engage only pirates and
  // WANTED players - never traders, clean players, or other civilians. Shared by
  // the combat tick's fire selection and the AI's flight targeting, so the two
  // can't disagree about who the police are allowed to hunt.
  [[nodiscard]] inline bool PoliceMayEngage(ECS::Registry& _world, ECS::EntityId _candidate, int _candidateTeam)
  {
    if (_candidateTeam == Team::Pirate)
      return true;
    if (_world.TryGet<PlayerTag>(_candidate) != nullptr)
    {
      const Wanted* w = _world.TryGet<Wanted>(_candidate);
      return w != nullptr && w->level > 0;
    }
    return false;
  }

  // Which shield a hit lands on: true = FRONT (the attacker lies ahead of the
  // victim's nose), false = aft. Unshielded/facing-less victims default to front.
  [[nodiscard]] inline bool HitOnFront(ECS::Registry& _world, ECS::EntityId _victim, const Math::Vector3i64& _attackerPos)
  {
    const Flight* f = _world.TryGet<Flight>(_victim);
    const WorldTransform* t = _world.TryGet<WorldTransform>(_victim);
    if (f == nullptr || t == nullptr)
      return true;
    const double dx = static_cast<double>(_attackerPos.x - t->position.x);
    const double dy = static_cast<double>(_attackerPos.y - t->position.y);
    const double dz = static_cast<double>(_attackerPos.z - t->position.z);
    return (dx * f->nose.x + dy * f->nose.y + dz * f->nose.z) >= 0.0;   // ahead => front
  }

  // Apply `_damage` to `_target` from an attacker at `_attackerPos`. A shielded
  // player absorbs it through the facing directional shield, overflow draining the
  // energy bank (legacy damage_ship); an unshielded NPC takes it straight on the
  // energy pool. Returns true if driven to death (energy <= 0). Does NOT check
  // invuln - the caller gates that.
  [[nodiscard]] inline bool ApplyDamage(ECS::Registry& _world, ECS::EntityId _target, int _damage, const Math::Vector3i64& _attackerPos)
  {
    Combatant* c = _world.TryGet<Combatant>(_target);
    if (c == nullptr)
      return false;

    Shields* sh = _world.TryGet<Shields>(_target);
    if (sh == nullptr)
    {
      c->energy -= _damage;   // unshielded: straight to the energy pool
      return c->energy <= 0;
    }

    const bool hitFront = HitOnFront(_world, _target, _attackerPos);
    const ShieldHitResult r = ApplyDamageToShields(ShieldState{ sh->front, sh->aft, c->energy }, _damage, hitFront);
    sh->front = r.state.frontShield;
    sh->aft = r.state.aftShield;
    c->energy = r.state.energy;
    return r.destroyed;
  }

  // Recharge players' shields/energy one regen step (legacy regenerate_shields):
  // while the energy bank is over half, bleed a point into each not-full shield;
  // then the bank itself recovers by one, capped. Only entities with Shields (i.e.
  // players) regen; the caller gates the cadence. Pure - unit-tested headlessly.
  inline void StepShieldRegen(ECS::Registry& _world)
  {
    _world.Each<Shields, Combatant>([](ECS::EntityId, Shields& _s, Combatant& _c)
    {
      if (_c.energy > MAX_ENERGY / 2)
      {
        if (_s.front < MAX_SHIELD) { ++_s.front; --_c.energy; }
        if (_s.aft < MAX_SHIELD)   { ++_s.aft;   --_c.energy; }
      }
      ++_c.energy;
      if (_c.energy > MAX_ENERGY)
        _c.energy = MAX_ENERGY;
    });
  }

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
    // is simultaneous (firing order doesn't matter). The attacker's position is
    // kept too, so a player victim's directional shields know which side was hit.
    std::unordered_map<uint32_t, int> damage;
    std::unordered_map<uint32_t, uint32_t> attacker;
    std::unordered_map<uint32_t, Math::Vector3i64> attackerPos;

    for (const Unit& a : units)
    {
      if (!a.c->autoEngage)
        continue;   // players (and inert objects) don't initiate fire

      if (a.c->fireTimer > 0)
      {
        --a.c->fireTimer;   // weapon still cooling down this tick
        continue;
      }

      // In-range test is Chebyshev (no large multiplies on absolute coords);
      // only then is the squared distance computed, and only over the small
      // in-range delta, so it cannot overflow.
      auto inRange = [&a](const Unit& _b) -> bool
      {
        const int64_t dx = _b.pos.x - a.pos.x;
        const int64_t dy = _b.pos.y - a.pos.y;
        const int64_t dz = _b.pos.z - a.pos.z;
        const int64_t ax = dx < 0 ? -dx : dx;
        const int64_t ay = dy < 0 ? -dy : dy;
        const int64_t az = dz < 0 ? -dz : dz;
        return ax <= a.c->range && ay <= a.c->range && az <= a.c->range;
      };

      const Unit* best = nullptr;
      int64_t bestDist2 = 0;

      // Target memory first (stage 4): a live, in-range, still-legitimate focus
      // outranks the nearest scan, so fire follows the AI's flight target (the
      // police shoot the offender they are chasing, not whoever drifts closest).
      if (a.c->focus != ECS::INVALID_INDEX)
        for (const Unit& b : units)
          if (b.id.index == a.c->focus)
          {
            if (b.c->team != a.c->team && inRange(b)
                && (a.c->team != Team::Police || PoliceMayEngage(_world, b.id, b.c->team)))
              best = &b;
            break;
          }

      if (best == nullptr)
        for (const Unit& b : units)
        {
          if (b.c->team == a.c->team)
            continue;   // never target allies
          if (a.c->team == Team::Police && !PoliceMayEngage(_world, b.id, b.c->team))
            continue;   // the law spares traders and the innocent
          if (!inRange(b))
            continue;

          const int64_t dx = b.pos.x - a.pos.x;
          const int64_t dy = b.pos.y - a.pos.y;
          const int64_t dz = b.pos.z - a.pos.z;
          const int64_t dist2 = dx * dx + dy * dy + dz * dz;
          if (best == nullptr || dist2 < bestDist2)
          {
            best = &b;
            bestDist2 = dist2;
          }
        }

      if (best != nullptr)
      {
        damage[best->id.index] += a.c->laserStrength;
        attacker[best->id.index] = a.id.index;
        attackerPos[best->id.index] = a.pos;
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

      // Route through ApplyDamage so a player victim absorbs the hit on the shield
      // facing the attacker; an unshielded NPC still takes it flat on energy.
      if (ApplyDamage(_world, u.id, it->second, attackerPos[u.id.index]))
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

    const int dmg = sc->laserStrength;
    const bool destroyed = ApplyDamage(_world, best, dmg, origin);   // origin = shooter position

    out.hit = true;
    out.target = best;
    out.targetTeam = tc->team;
    out.destroyed = destroyed;
    return out;
  }
}
