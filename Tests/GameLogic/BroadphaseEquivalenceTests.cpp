// D1 equivalence goldens: the grid-narrowed systems must produce BIT-IDENTICAL
// outcomes to the old full pairwise sweeps. Each test builds two identical seeded
// worlds, runs the converted system on one and a faithful copy of the pre-D1
// brute-force algorithm (the oracle) on the other, and compares complete end
// state - kills in order, every energy/shield/fire-timer/hold. The worlds span
// negative coordinates and multiple broadphase cells on purpose, so cell-boundary
// and floor-division behaviour is exercised, not just the happy cluster.

#include <gtest/gtest.h>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "GameLogic.h"

using namespace Neuron;
using namespace Neuron::GameLogic;

namespace
{
  // Deterministic LCG (no std::rand, no clocks - the repo's determinism rules).
  struct Lcg
  {
    uint32_t s;
    explicit Lcg(uint32_t _seed) : s(_seed) {}
    uint32_t Next() { s = s * 1664525u + 1013904223u; return s; }
    int64_t Range(int64_t _lo, int64_t _hi)   // inclusive lo, exclusive hi
    {
      return _lo + static_cast<int64_t>(Next() % static_cast<uint32_t>(_hi - _lo));
    }
  };

  // A seeded battlefield: ships (mixed teams/energies, some shielded players, some
  // docked, some invulnerable), stations and planets - clustered around a NEGATIVE
  // origin so cells straddle zero, with a tight knot (in contact/combat range) and
  // a scattered remainder several cells away.
  void BuildBattlefield(ECS::Registry& _w, uint32_t _seed)
  {
    Lcg rng(_seed);
    const Math::Vector3i64 origin{ -50000, 30000, -20000 };

    for (int i = 0; i < 48; ++i)
    {
      const bool tight = (i % 2) == 0;   // half in a brawl, half spread wide
      const int64_t spread = tight ? 900 : 45000;
      const Math::Vector3i64 pos{ origin.x + rng.Range(-spread, spread),
                                  origin.y + rng.Range(-spread, spread),
                                  origin.z + rng.Range(-spread, spread) };
      const ECS::EntityId e = _w.Create();
      _w.Add<WorldTransform>(e, WorldTransform{ pos });
      const int team = static_cast<int>(rng.Next() % 5);   // all five factions
      Combatant c{ team, static_cast<int>(50 + rng.Next() % 300), 10, 6000, team != Team::Player };
      c.fireTimer = static_cast<int>(rng.Next() % 3);
      c.invulnTicks = (rng.Next() % 8 == 0) ? 5 : 0;
      _w.Add<Combatant>(e, c);
      if (team == Team::Player)
      {
        _w.Add<PlayerTag>(e, PlayerTag{});
        _w.Add<Wanted>(e, Wanted{ static_cast<int>(rng.Next() % 3) });
        if (rng.Next() % 2 == 0)
        {
          _w.Add<Flight>(e, Flight{});
          _w.Add<Shields>(e, Shields{});
        }
      }
      if (rng.Next() % 10 == 0)
        _w.Add<DockState>(e, DockState{ /*docked*/ true, 0 });
    }

    for (int i = 0; i < 2; ++i)   // station fortresses in the knot
    {
      const ECS::EntityId e = _w.Create();
      _w.Add<WorldTransform>(e, WorldTransform{ { origin.x + rng.Range(-1200, 1200),
                                                  origin.y + rng.Range(-1200, 1200),
                                                  origin.z + rng.Range(-1200, 1200) } });
      _w.Add<Combatant>(e, Combatant{ Team::Station, 1000000, 0, 1, false });
    }

    {
      const ECS::EntityId planet = _w.Create();   // a planet grazing the knot
      _w.Add<WorldTransform>(planet, WorldTransform{ { origin.x + 3500, origin.y, origin.z } });
      _w.Add<NetType>(planet, NetType{ ShipType::Planet });
    }
  }

  // Collect every combatant's (energy, shields, fireTimer, invuln) for comparison.
  struct HullState
  {
    int energy = 0, front = -1, aft = -1, fireTimer = 0, invuln = 0;
    bool operator==(const HullState&) const = default;
  };
  [[nodiscard]] std::unordered_map<uint32_t, HullState> Hulls(ECS::Registry& _w)
  {
    std::unordered_map<uint32_t, HullState> out;
    _w.Each<Combatant>([&](ECS::EntityId _id, Combatant& _c)
    {
      HullState h;
      h.energy = _c.energy;
      h.fireTimer = _c.fireTimer;
      h.invuln = _c.invulnTicks;
      if (const Shields* s = _w.TryGet<Shields>(_id)) { h.front = s->front; h.aft = s->aft; }
      out[_id.index] = h;
    });
    return out;
  }

  [[nodiscard]] bool SameKills(const std::vector<Kill>& _a, const std::vector<Kill>& _b)
  {
    if (_a.size() != _b.size())
      return false;
    for (std::size_t i = 0; i < _a.size(); ++i)
      if (!(_a[i].victim == _b[i].victim) || _a[i].killer != _b[i].killer)
        return false;
    return true;
  }

  // ---- ORACLES: faithful copies of the pre-D1 full sweeps -----------------------

  [[nodiscard]] std::vector<Kill> ReferenceStepCollisions(ECS::Registry& _world)
  {
    struct Unit { ECS::EntityId id; Math::Vector3i64 pos; Combatant* c; bool station; };
    std::vector<Unit> units;
    _world.Each<WorldTransform, Combatant>([&](ECS::EntityId _id, WorldTransform& _t, Combatant& _c)
    {
      const DockState* dock = _world.TryGet<DockState>(_id);
      if (dock != nullptr && dock->docked)
        return;
      units.push_back(Unit{ _id, _t.position, &_c, _c.team == Team::Station });
    });

    auto within = [](const Math::Vector3i64& _a, const Math::Vector3i64& _b, int64_t _r)
    {
      const int64_t ax = _a.x > _b.x ? _a.x - _b.x : _b.x - _a.x;
      const int64_t ay = _a.y > _b.y ? _a.y - _b.y : _b.y - _a.y;
      const int64_t az = _a.z > _b.z ? _a.z - _b.z : _b.z - _a.z;
      return ax <= _r && ay <= _r && az <= _r;
    };

    std::vector<Kill> kills;
    std::unordered_set<uint32_t> dead;
    auto report = [&](ECS::EntityId _v, uint32_t _k)
    {
      if (dead.insert(_v.index).second)
        kills.push_back(Kill{ _v, _k });
    };

    for (std::size_t i = 0; i < units.size(); ++i)
      for (std::size_t j = i + 1; j < units.size(); ++j)
      {
        Unit& a = units[i];
        Unit& b = units[j];
        if (a.station && b.station)
          continue;
        if (a.station || b.station)
        {
          Unit& ship = a.station ? b : a;
          const Unit& hull = a.station ? a : b;
          if (!within(ship.pos, hull.pos, STATION_CONTACT_RANGE))
            continue;
          if (ship.c->invulnTicks > 0 || dead.count(ship.id.index) != 0)
            continue;
          if (ApplyDamage(_world, ship.id, STATION_CRASH_DAMAGE, hull.pos))
            report(ship.id, hull.id.index);
          continue;
        }
        if (!within(a.pos, b.pos, SHIP_CONTACT_RANGE))
          continue;
        if (a.c->invulnTicks == 0 && dead.count(a.id.index) == 0)
          if (ApplyDamage(_world, a.id, SHIP_RAM_DAMAGE, b.pos))
            report(a.id, b.id.index);
        if (b.c->invulnTicks == 0 && dead.count(b.id.index) == 0)
          if (ApplyDamage(_world, b.id, SHIP_RAM_DAMAGE, a.pos))
            report(b.id, a.id.index);
      }

    std::vector<Unit> planets;
    _world.Each<WorldTransform, NetType>([&](ECS::EntityId _id, WorldTransform& _t, NetType& _nt)
    {
      if (_nt.type == ShipType::Planet)
        planets.push_back(Unit{ _id, _t.position, nullptr, false });
    });
    if (!planets.empty())
      for (Unit& u : units)
      {
        if (u.station || u.c->invulnTicks > 0 || dead.count(u.id.index) != 0)
          continue;
        for (const Unit& p : planets)
          if (within(u.pos, p.pos, PLANET_KILL_RADIUS))
          {
            u.c->energy = 0;
            report(u.id, p.id.index);
            break;
          }
      }
    return kills;
  }

  [[nodiscard]] std::vector<Kill> ReferenceStepCombat(ECS::Registry& _world)
  {
    struct Unit { ECS::EntityId id; Math::Vector3i64 pos; Combatant* c; };
    std::vector<Unit> units;
    _world.Each<WorldTransform, Combatant>([&](ECS::EntityId _id, WorldTransform& _t, Combatant& _c)
    {
      units.push_back(Unit{ _id, _t.position, &_c });
    });

    std::unordered_map<uint32_t, int> damage;
    std::unordered_map<uint32_t, uint32_t> attacker;
    std::unordered_map<uint32_t, Math::Vector3i64> attackerPos;

    for (const Unit& a : units)
    {
      if (!a.c->autoEngage)
        continue;
      if (a.c->fireTimer > 0)
      {
        --a.c->fireTimer;
        continue;
      }
      auto inRange = [&a](const Unit& _b)
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
            continue;
          if (a.c->team == Team::Police && !PoliceMayEngage(_world, b.id, b.c->team))
            continue;
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
        a.c->fireTimer = a.c->fireInterval;
      }
    }

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
      if (ApplyDamage(_world, u.id, it->second, attackerPos[u.id.index]))
        kills.push_back(Kill{ u.id, attacker[u.id.index] });
    }
    return kills;
  }

  [[nodiscard]] std::vector<uint32_t> ReferenceScoopSystem(ECS::Registry& _world)
  {
    struct Can { ECS::EntityId id; Math::Vector3i64 pos; LootItem* loot; };
    std::vector<Can> cans;
    _world.Each<WorldTransform, LootItem>([&](ECS::EntityId _id, WorldTransform& _t, LootItem& _l)
    {
      cans.push_back(Can{ _id, _t.position, &_l });
    });
    if (cans.empty())
      return {};

    std::vector<uint32_t> changed;
    std::vector<ECS::EntityId> consumed;
    _world.Each<WorldTransform, PlayerTag>([&](ECS::EntityId _pid, WorldTransform& _pt, PlayerTag&)
    {
      const DockState* dock = _world.TryGet<DockState>(_pid);
      if (dock != nullptr && dock->docked)
        return;
      CargoHold* hold = _world.TryGet<CargoHold>(_pid);
      if (hold == nullptr)
        return;
      const Equipment* eq = _world.TryGet<Equipment>(_pid);
      const bool hasScoop = (eq != nullptr && eq->fuelScoop);

      bool scooped = false;
      for (Can& can : cans)
      {
        if (can.loot == nullptr)
          continue;
        const int64_t dx = can.pos.x - _pt.position.x;
        const int64_t dy = can.pos.y - _pt.position.y;
        const int64_t dz = can.pos.z - _pt.position.z;
        const int64_t ax = dx < 0 ? -dx : dx;
        const int64_t ay = dy < 0 ? -dy : dy;
        const int64_t az = dz < 0 ? -dz : dz;
        if (ax > LOOT_SCOOP_RANGE || ay > LOOT_SCOOP_RANGE || az > LOOT_SCOOP_RANGE)
          continue;
        const bool room = !CountsAsTonnage(can.loot->commodity)
          || TotalTonnage(*hold) + can.loot->units <= hold->capacity;
        if (hasScoop && room)
        {
          hold->units[can.loot->commodity] += can.loot->units;
          scooped = true;
        }
        consumed.push_back(can.id);
        can.loot = nullptr;
      }
      if (scooped)
        changed.push_back(_pid.index);
    });

    for (ECS::EntityId id : consumed)
      _world.Destroy(id);
    return changed;
  }
}

// ---- the equivalence goldens ----------------------------------------------------

TEST(BroadphaseEquivalence, CollisionsMatchTheFullSweepExactly)
{
  for (uint32_t seed : { 1u, 77u, 31337u })
  {
    ECS::Registry gridWorld;
    ECS::Registry refWorld;
    BuildBattlefield(gridWorld, seed);
    BuildBattlefield(refWorld, seed);

    uint64_t pairs = 0;
    const std::vector<Kill> gridKills = StepCollisions(gridWorld, &pairs);
    const std::vector<Kill> refKills = ReferenceStepCollisions(refWorld);

    EXPECT_TRUE(SameKills(gridKills, refKills)) << "seed " << seed;
    EXPECT_EQ(Hulls(gridWorld), Hulls(refWorld)) << "seed " << seed;
    // And the narrowing did real work: fewer candidates than the full sweep.
    EXPECT_LT(pairs, 50u * 49u / 2u) << "seed " << seed;
  }
}

TEST(BroadphaseEquivalence, CombatMatchesTheFullScanExactly)
{
  for (uint32_t seed : { 2u, 99u, 424242u })
  {
    ECS::Registry gridWorld;
    ECS::Registry refWorld;
    BuildBattlefield(gridWorld, seed);
    BuildBattlefield(refWorld, seed);

    uint64_t pairs = 0;
    const std::vector<Kill> gridKills = StepCombat(gridWorld, &pairs);
    const std::vector<Kill> refKills = ReferenceStepCombat(refWorld);

    EXPECT_TRUE(SameKills(gridKills, refKills)) << "seed " << seed;
    EXPECT_EQ(Hulls(gridWorld), Hulls(refWorld)) << "seed " << seed;
  }
}

TEST(BroadphaseEquivalence, ScoopMatchesTheFullSweepExactly)
{
  for (uint32_t seed : { 3u, 555u })
  {
    ECS::Registry gridWorld;
    ECS::Registry refWorld;
    for (ECS::Registry* w : { &gridWorld, &refWorld })
    {
      Lcg rng(seed);
      const Math::Vector3i64 origin{ -8000, -8000, 16000 };
      for (int p = 0; p < 4; ++p)   // players, two with scoops
      {
        const ECS::EntityId e = w->Create();
        w->Add<WorldTransform>(e, WorldTransform{ { origin.x + p * 700, origin.y, origin.z } });
        w->Add<PlayerTag>(e, PlayerTag{});
        w->Add<CargoHold>(e, CargoHold{});
        w->Add<Equipment>(e, Equipment{ 3, false, false, /*fuelScoop*/ (p % 2) == 0, false, false });
      }
      for (int c = 0; c < 24; ++c)   // canisters strewn across the player line + far
      {
        const ECS::EntityId e = w->Create();
        w->Add<WorldTransform>(e, WorldTransform{ { origin.x + rng.Range(-1500, 4000),
                                                    origin.y + rng.Range(-700, 700),
                                                    origin.z + rng.Range(-700, 700) } });
        w->Add<LootItem>(e, LootItem{ static_cast<int>(rng.Next() % 5), static_cast<int>(1 + rng.Next() % 3), 0 });
      }
    }

    const std::vector<uint32_t> gridChanged = ScoopSystem(gridWorld);
    const std::vector<uint32_t> refChanged = ReferenceScoopSystem(refWorld);

    EXPECT_EQ(gridChanged, refChanged) << "seed " << seed;
    EXPECT_EQ(gridWorld.AliveCount(), refWorld.AliveCount()) << "seed " << seed;   // same canisters consumed

    // Same holds on every player.
    gridWorld.Each<PlayerTag, CargoHold>([&](ECS::EntityId _id, PlayerTag&, CargoHold& _h)
    {
      const CargoHold& other = refWorld.Get<CargoHold>(_id);
      for (int c = 0; c < COMMODITY_COUNT; ++c)
        EXPECT_EQ(_h.units[c], other.units[c]) << "seed " << seed << " player " << _id.index << " commodity " << c;
    });
  }
}
