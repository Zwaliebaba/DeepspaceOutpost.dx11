#pragma once

// LootSystem - cargo canisters: wrecks shed loot, players scoop it (GameLogic, G4).
//
// A faithful port of the legacy launch_loot / scoop_item pair to the authoritative
// server. When a ship dies it scatters cargo/alloy canisters (DropLoot); a dead
// player's own hold spills the same way (DropPlayerCargo). Each canister is a real
// entity - WorldTransform + a small drift Velocity + LootItem + a NetType so the
// client draws it as the legacy cargo/alloy/rock model - that lives for a while then
// despawns (StepLoot). A player flying into one with a fuel scoop and hold space
// vacuums it up (ScoopSystem, legacy scoop_item); without a scoop, or with a full
// hold, contact just smashes it. Pure apart from the world it mutates (spawns/
// destroys canisters, fills holds), so every rule is unit-tested headlessly; the
// server loop calls DropLoot on a kill and ScoopSystem/StepLoot each tick.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ECS.h"
#include "Vector3i64.h"

#include "SimComponents.h"      // WorldTransform, Velocity, NetType, ShipType
#include "Economy.h"           // COMMODITY_COUNT
#include "StationServices.h"   // CargoHold, Equipment, DockState, CountsAsTonnage, TotalTonnage
#include "CombatSystem.h"      // PlayerTag
#include "Broadphase.h"        // BROADPHASE_CELL + the sorted-candidates discipline (D1)
#include "FrameScratch.h"      // LootCan + persistent scratch storage (D2)

namespace Neuron::GameLogic
{
  // Named commodity slots this system produces (indices into the legacy stock
  // market / CargoHold.units): alloy splinters and asteroid minerals get their own
  // mesh; a cargo canister carries any of the first eight tradeable goods.
  inline constexpr int ALLOYS_COMMODITY   = 9;    // "Alloys"   - splintered-alloy loot
  inline constexpr int MINERALS_COMMODITY = 12;   // "Minerals" - mined-rock loot
  inline constexpr int CARGO_COMMODITY_MASK = 7;  // a cargo canister rolls goods 0..7 (legacy rand & 7)

  // Ticks a dropped canister survives before it despawns (~2 min at 30 Hz), so a
  // debris field left by a big fight eventually clears instead of accumulating.
  inline constexpr int LOOT_LIFE_TICKS = 3600;

  // Contact range (world units, Chebyshev) at which a passing player scoops or
  // smashes a canister. Small relative to the >=2000-unit spawn spacing, so you
  // must actually fly up to the loot.
  inline constexpr int64_t LOOT_SCOOP_RANGE = 600;

  // How many canisters one batch (alloy, or cargo) can shed: legacy masked the
  // random count with the wreck's max_loot & 15; our NPCs carry no per-ship max, so
  // a fixed 2-bit mask gives 0..3 per batch (up to 6 per kill across both batches).
  inline constexpr uint32_t LOOT_DROP_MASK = 3;

  // A drifting cargo canister: the commodity it yields when scooped, how many units,
  // and the ticks it has left to live. Rendered via its NetType (Cargo/Alloy/Rock).
  struct LootItem
  {
    int commodity = 0;   // CargoHold.units index a scoop deposits into
    int units = 1;       // amount deposited (a fresh drop is one unit; a spilled stack more)
    int life = LOOT_LIFE_TICKS;
  };

  // Which legacy mesh a canister of `_commodity` is drawn as: alloys splinter,
  // minerals are rock, everything else is a cargo pod.
  [[nodiscard]] inline int LootMeshFor(int _commodity)
  {
    if (_commodity == ALLOYS_COMMODITY)   return ShipType::Alloy;
    if (_commodity == MINERALS_COMMODITY) return ShipType::Rock;
    return ShipType::Cargo;
  }

  namespace Detail
  {
    // Numerical-Recipes LCG advance (the engine forbids wall-clock RNG). The caller
    // owns the state, so drops are deterministic and unit-testable.
    [[nodiscard]] inline uint32_t LootNext(uint32_t& _rng)
    {
      _rng = _rng * 1664525u + 1013904223u;
      return _rng;
    }

    // A small per-axis drift (world units per tick) so canisters scatter off the
    // wreck instead of stacking in one exact spot. Range [-24, 24) per axis.
    [[nodiscard]] inline Math::Vector3i64 LootDrift(uint32_t& _rng)
    {
      auto axis = [&]() -> int64_t { return static_cast<int64_t>(LootNext(_rng) % 48u) - 24; };
      return { axis(), axis(), axis() };
    }
  }

  // Spawn one cargo canister at `_pos` drifting by `_drift`, yielding `_units` of
  // `_commodity`, drawn as `_mesh`. Returns the new entity.
  inline ECS::EntityId SpawnCanister(ECS::Registry& _world, const Math::Vector3i64& _pos,
                                     const Math::Vector3i64& _drift, int _commodity, int _units, int _mesh)
  {
    const ECS::EntityId e = _world.Create();
    _world.Add<WorldTransform>(e, WorldTransform{ _pos });
    _world.Add<Velocity>(e, Velocity{ _drift });
    _world.Add<LootItem>(e, LootItem{ _commodity, _units, LOOT_LIFE_TICKS });
    _world.Add<NetType>(e, NetType{ _mesh });
    return e;
  }

  namespace Detail
  {
    // One legacy launch_loot batch: half the time nothing, else 0..LOOT_DROP_MASK
    // single-unit canisters scattered off `_origin`. When `_commodity` is negative
    // each canister rolls its own tradeable good (legacy SHIP_CARGO -> rand & 7);
    // otherwise every canister carries the fixed commodity (alloy splinters). Returns
    // the number spawned.
    inline int DropBatch(ECS::Registry& _world, const Math::Vector3i64& _origin, int _commodity, uint32_t& _rng)
    {
      const uint32_t roll = LootNext(_rng) & 0xFFu;   // legacy rand255()
      if (roll >= 128u)
        return 0;                                     // legacy: high bit -> no drop this batch
      const int count = static_cast<int>(roll & LOOT_DROP_MASK);
      for (int i = 0; i < count; ++i)
      {
        const int commodity = (_commodity >= 0)
          ? _commodity
          : static_cast<int>(LootNext(_rng) & static_cast<uint32_t>(CARGO_COMMODITY_MASK));
        SpawnCanister(_world, _origin, LootDrift(_rng), commodity, /*units*/ 1, LootMeshFor(commodity));
      }
      return count;
    }
  }

  // A destroyed ship sheds loot (legacy check_target -> launch_loot for the non-
  // asteroid case): an alloy-splinter batch and a cargo batch off the wreck's
  // position. Returns the number of canisters spawned. No-op if the wreck has no
  // transform. `_rng` is advanced (caller owns the state).
  inline int DropLoot(ECS::Registry& _world, ECS::EntityId _wreck, uint32_t& _rng)
  {
    const WorldTransform* wt = _world.TryGet<WorldTransform>(_wreck);
    if (wt == nullptr)
      return 0;
    const Math::Vector3i64 origin = wt->position;

    int spawned = 0;
    spawned += Detail::DropBatch(_world, origin, ALLOYS_COMMODITY, _rng);   // alloy splinters
    spawned += Detail::DropBatch(_world, origin, /*random good*/ -1, _rng); // cargo pods
    return spawned;
  }

  // A dead player's hold spills as scoopable canisters (G3 death rule): one canister
  // per non-empty commodity stack, carrying that whole stack, scattered off `_pos`.
  // The hold is NOT emptied here - RespawnAtNearestStation does that; this reads it
  // first. Returns the number of canisters spawned.
  inline int DropPlayerCargo(ECS::Registry& _world, ECS::EntityId _player, const Math::Vector3i64& _pos, uint32_t& _rng)
  {
    CargoHold* hold = _world.TryGet<CargoHold>(_player);
    if (hold == nullptr)
      return 0;

    int spawned = 0;
    for (int c = 0; c < COMMODITY_COUNT; ++c)
    {
      const int units = hold->units[c];
      if (units <= 0)
        continue;
      SpawnCanister(_world, _pos, Detail::LootDrift(_rng), c, units, LootMeshFor(c));
      ++spawned;
    }
    return spawned;
  }

  // Age every canister one tick and destroy the expired ones (collect-then-destroy,
  // so it is safe to call while nothing else iterates the loot pool). Returns the
  // number despawned. `_scratch` (D2) is reusable per-tick working storage; the
  // default lets every existing call site (tests) omit it.
  inline int StepLoot(ECS::Registry& _world, FrameScratch& _scratch = Detail::DefaultScratch())
  {
    std::vector<ECS::EntityId>& expired = _scratch.lootExpired;
    expired.clear();
    _world.Each<LootItem>([&expired](ECS::EntityId _id, LootItem& _l)
    {
      if (--_l.life <= 0)
        expired.push_back(_id);
    });
    for (ECS::EntityId id : expired)
      _world.Destroy(id);
    return static_cast<int>(expired.size());
  }

  // Players sweep up canisters they touch (legacy scoop_item). For each non-docked
  // player, any canister within LOOT_SCOOP_RANGE is consumed: if the player has a
  // fuel scoop and the hold has room it is scooped into the hold (respecting tonnage
  // capacity); otherwise contact just smashes it. A canister is claimed by the first
  // player that reaches it (no double-scoop). Returns the entity indices of players
  // whose hold changed, so the caller can resend their cargo/status. Collect-then-
  // destroy, safe to call standalone. `_scratch` (D2) is reusable per-tick working
  // storage; the default lets every existing call site (tests) omit it.
  [[nodiscard]] inline std::vector<uint32_t> ScoopSystem(ECS::Registry& _world,
                                                         FrameScratch& _scratch = Detail::DefaultScratch())
  {
    std::vector<LootCan>& cans = _scratch.scoopCans;
    cans.clear();
    _world.Each<WorldTransform, LootItem>([&cans](ECS::EntityId _id, WorldTransform& _t, LootItem& _l)
    {
      cans.push_back(LootCan{ _id, _t.position, &_l });
    });
    if (cans.empty())
      return {};

    // D1 broadphase: bucket the canisters by their cans-vector index; each player
    // walks only its +/-1-cell neighbourhood (cell >= the scoop range), sorted -
    // the same canister order (minus out-of-range ones) as the old full sweep, so
    // first-player-claims outcomes are bit-identical.
    Spatial::Grid& grid = _scratch.scoopGrid;
    grid.Clear();
    for (std::size_t i = 0; i < cans.size(); ++i)
      grid.Insert(i, cans[i].pos);
    std::vector<uint64_t>& nearby = _scratch.scoopNearby;

    std::vector<uint32_t> changed;
    std::vector<ECS::EntityId>& consumed = _scratch.scoopConsumed;
    consumed.clear();

    _world.Each<WorldTransform, PlayerTag>([&](ECS::EntityId _pid, WorldTransform& _pt, PlayerTag&)
    {
      const DockState* dock = _world.TryGet<DockState>(_pid);
      if (dock != nullptr && dock->docked)
        return;   // docked players sit inside a station, not out among the debris
      CargoHold* hold = _world.TryGet<CargoHold>(_pid);
      if (hold == nullptr)
        return;
      const Equipment* eq = _world.TryGet<Equipment>(_pid);
      const bool hasScoop = (eq != nullptr && eq->fuelScoop);

      bool scooped = false;
      QuerySortedNeighbours(grid, _pt.position, 1, nearby);
      for (const uint64_t ci : nearby)
      {
        LootCan& can = cans[static_cast<std::size_t>(ci)];
        if (can.loot == nullptr)
          continue;   // already claimed by an earlier player this tick

        const int64_t dx = can.pos.x - _pt.position.x;
        const int64_t dy = can.pos.y - _pt.position.y;
        const int64_t dz = can.pos.z - _pt.position.z;
        const int64_t ax = dx < 0 ? -dx : dx;
        const int64_t ay = dy < 0 ? -dy : dy;
        const int64_t az = dz < 0 ? -dz : dz;
        if (ax > LOOT_SCOOP_RANGE || ay > LOOT_SCOOP_RANGE || az > LOOT_SCOOP_RANGE)
          continue;

        // A tonnage good needs hold room; non-tonnage goods (gold/gems/...) never do.
        const bool room = !CountsAsTonnage(can.loot->commodity)
          || TotalTonnage(*hold) + can.loot->units <= hold->capacity;
        if (hasScoop && room)
        {
          hold->units[can.loot->commodity] += can.loot->units;
          scooped = true;
        }
        // Contact always removes the canister - scooped, or smashed for want of a
        // scoop / hold space.
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
