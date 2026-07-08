#pragma once

// SceneSystem - materialize per-system scenes and regenerate their resources
// (GameLogic; design: scene.md 3.5, 3.7b).
//
// The DB stores POI ANCHORS + parameters (Server/SceneRows.h -> dbo.system_pois /
// dbo.poi_resources); this system turns each loaded POI row into live entities at
// boot and keeps a belt's visible rocks in step with its durable pool. Belt rocks
// are NOT stored - they are scattered from the POI seed, so the row count stays
// tiny while a mined-out belt renders sparser and a regenerating one refills.
//
// Runtime pieces:
//   SceneIndex        systemId -> anchor entities, poiId -> anchor (a maintained
//                     index, like OwnershipIndex - never a component scan).
//   MaterializeScenePoi  one POI row -> an anchor entity (+ belt rocks / pool).
//   MaintainBeltRocks    top a belt's live rocks up to its pool-scaled density.
//   StepSceneRegen       slow drift of every pool back toward baseline + refill.
//
// Header-only, deterministic (rock scatter hashes the POI seed + tick, no
// wall-clock RNG), headless-testable.

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "ECS.h"
#include "Vector3i64.h"

#include "SimComponents.h"   // WorldTransform, NetType, ShipType, Velocity
#include "SceneTypes.h"
#include "GalaxyGen.h"       // Detail::Mix64 / Detail::Spread

namespace Neuron::GameLogic
{
  // A maintained lookup from system/poi to the anchor entities materialized for a
  // scene. Built as MaterializeScenePoi runs; queried by the chart chunk builder
  // and the targeted-jump validator so neither scans components.
  struct SceneIndex
  {
    std::unordered_map<int32_t, std::vector<ECS::EntityId>> bySystem;   // systemId -> anchors
    std::unordered_map<uint32_t, ECS::EntityId> byPoi;                  // poiId    -> anchor

    void Register(int32_t _systemId, uint32_t _poiId, ECS::EntityId _anchor)
    {
      bySystem[_systemId].push_back(_anchor);
      byPoi[_poiId] = _anchor;
    }

    [[nodiscard]] ECS::EntityId Anchor(uint32_t _poiId) const
    {
      const auto it = byPoi.find(_poiId);
      return it != byPoi.end() ? it->second : ECS::EntityId{};
    }
  };

  namespace Detail
  {
    // How many live rocks a belt should show for its current pool: the full cap at
    // baseline, scaling down with the pool fraction, but at least one while any ore
    // remains (so a nearly-stripped belt still has a rock to cut), zero when dry.
    [[nodiscard]] inline int BeltRockTarget(const PoiResources& _pool)
    {
      if (_pool.units <= 0 || _pool.baseline <= 0)
        return 0;
      int64_t scaled = static_cast<int64_t>(BELT_MAX_ROCKS) * _pool.units / _pool.baseline;
      if (scaled < 1) scaled = 1;
      if (scaled > BELT_MAX_ROCKS) scaled = BELT_MAX_ROCKS;
      return static_cast<int>(scaled);
    }

    // Spawn one belt rock scattered inside `_radius` of `_anchorPos`, deterministic
    // from (_seed, _ordinal). Its OreBody chunk is capped at the pool so a rock can
    // never promise more ore than the belt holds (StepMining clamps to the pool too,
    // so conservation holds regardless). Returns the rock entity.
    inline ECS::EntityId SpawnBeltRock(ECS::Registry& _world, ECS::EntityId _anchor, const Math::Vector3i64& _anchorPos,
                                       int32_t _radius, uint8_t _commodity, int32_t _poolUnits,
                                       uint32_t _seed, uint32_t _ordinal)
    {
      const uint64_t h = Mix64((static_cast<uint64_t>(_seed) << 20) ^ Mix64(_ordinal + 1ull));
      const Math::Vector3i64 pos{
        _anchorPos.x + Spread(Mix64(h + 1ull), _radius),
        _anchorPos.y + Spread(Mix64(h + 2ull), _radius),
        _anchorPos.z + Spread(Mix64(h + 3ull), _radius),
      };
      int32_t units = BELT_ROCK_UNITS;
      if (units > _poolUnits) units = _poolUnits;
      const ECS::EntityId e = _world.Create();
      _world.Add<WorldTransform>(e, WorldTransform{ pos });
      _world.Add<NetType>(e, NetType{ ShipType::Rock });
      _world.Add<OreBody>(e, OreBody{ _commodity, units, _anchor.index });
      // A tiny tumble drift so a belt isn't perfectly static (rocks carry Velocity;
      // StepMotion advances them). Small vs MINING_RANGE so a parked miner keeps cutting.
      const Math::Vector3i64 drift{
        (Spread(Mix64(h + 4ull), 3)),
        (Spread(Mix64(h + 5ull), 3)),
        (Spread(Mix64(h + 6ull), 3)),
      };
      _world.Add<Velocity>(e, Velocity{ drift });
      return e;
    }

    // Count a belt's live rocks (rocks are cheap and few; a scan of OreBody within
    // the belt radius is bounded by BELT_MAX_ROCKS in practice).
    [[nodiscard]] inline int CountBeltRocks(ECS::Registry& _world, const Math::Vector3i64& _anchorPos, int32_t _radius)
    {
      const int64_t r = _radius;
      int n = 0;
      _world.Each<WorldTransform, OreBody>([&](ECS::EntityId, WorldTransform& _t, OreBody&)
      {
        const int64_t dx = _t.position.x - _anchorPos.x, dy = _t.position.y - _anchorPos.y, dz = _t.position.z - _anchorPos.z;
        const int64_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, az = dz < 0 ? -dz : dz;
        if (ax <= r && ay <= r && az <= r) ++n;
      });
      return n;
    }
  }

  // Top a belt's live rocks up toward its pool-scaled density. Only ever SPAWNS
  // (mining despawns emptied rocks); the ordinal salts positions by tick so refills
  // scatter fresh. Safe to call every regen step or after a rock empties.
  inline void MaintainBeltRocks(ECS::Registry& _world, ECS::EntityId _anchor, uint32_t _tick)
  {
    const ScenePoi* poi = _world.TryGet<ScenePoi>(_anchor);
    const PoiResources* pool = _world.TryGet<PoiResources>(_anchor);
    const WorldTransform* at = _world.TryGet<WorldTransform>(_anchor);
    if (poi == nullptr || pool == nullptr || at == nullptr)
      return;
    // COPY everything out of the component pools BEFORE spawning: SpawnBeltRock does
    // _world.Add<...>, which can reallocate the very pools these pointers index -
    // dangling them mid-loop. Value copies are stable across the spawns.
    const Math::Vector3i64 anchorPos = at->position;
    const int32_t radius = poi->radius;
    const uint32_t seed = poi->seed;
    const uint8_t commodity = pool->commodity;
    const int target = Detail::BeltRockTarget(*pool);
    // Each rock carries roughly its share of the pool (pool / target), capped at
    // the nominal chunk, floored at 1 - so the visible rocks PARTITION the pool
    // rather than each advertising the whole of it. The pool stays authoritative
    // (StepMining clamps every yield to it), so this only shapes the visible field.
    int32_t perRock = target > 0 ? pool->units / target : pool->units;
    if (perRock > BELT_ROCK_UNITS) perRock = BELT_ROCK_UNITS;
    if (perRock < 1) perRock = 1;
    int live = Detail::CountBeltRocks(_world, anchorPos, radius);
    uint32_t ordinal = Detail::Mix64(seed ^ Detail::Mix64(_tick + 1ull)) & 0xFFFFFFu;
    while (live < target)
    {
      Detail::SpawnBeltRock(_world, _anchor, anchorPos, radius, commodity, perRock, seed, ordinal++);
      ++live;
    }
  }

  // Materialize one loaded POI row into the world: an anchor entity (ScenePoi +
  // WorldTransform, plus a Beacon NetType for the visible kinds) and, for a belt,
  // its durable PoiResources pool and initial rocks. Registers the anchor in
  // `_index`. `_poolUnits` seeds the belt pool (from dbo.poi_resources, or the
  // richness baseline when there is no saved row). Returns the anchor entity.
  inline ECS::EntityId MaterializeScenePoi(ECS::Registry& _world, SceneIndex& _index,
      uint32_t _poiId, int32_t _systemId, PoiKind _kind, const Math::Vector3i64& _pos,
      int32_t _radius, uint32_t _seed, uint8_t _commodity, int32_t _baseline,
      int32_t _poolUnits, uint16_t _encounterDef, uint32_t _tick)
  {
    const ECS::EntityId anchor = _world.Create();
    _world.Add<WorldTransform>(anchor, WorldTransform{ _pos });
    _world.Add<ScenePoi>(anchor, ScenePoi{ _poiId, _systemId, _kind, _seed, _radius, _encounterDef });

    switch (_kind)
    {
      case PoiKind::AsteroidBelt:
      {
        int32_t units = _poolUnits > 0 ? _poolUnits : _baseline;
        if (units > _baseline) units = _baseline;
        _world.Add<PoiResources>(anchor, PoiResources{ units, _baseline, _commodity });
        MaintainBeltRocks(_world, anchor, _tick);   // scatter the initial rock field
        break;                                       // belt anchor itself is invisible
      }
      case PoiKind::NavBeacon:
      case PoiKind::EncounterSite:
      case PoiKind::SalvageField:
        _world.Add<NetType>(anchor, NetType{ ShipType::Beacon });
        // EncounterSite carries encounterDef on ScenePoi; binding it to an armed
        // EncounterDirector is designai.md 3.7 / phase A6 (not yet built) - the
        // anchor + def are the seam that work will consume.
        break;
      default:
        // Reserved EVE-tier kinds (MiningFactory/SentryGrid/JumpGate): materializers
        // land with those features (scene.md 3.8). Anchor exists; nothing scattered.
        _world.Add<NetType>(anchor, NetType{ ShipType::Beacon });
        break;
    }

    _index.Register(_systemId, _poiId, anchor);
    return anchor;
  }

  // Slow drift of every belt pool back toward its baseline, and a rock refill so a
  // regenerating belt visibly repopulates. Call every tick; it self-gates on
  // SCENE_REGEN_INTERVAL, staggered per POI so all belts don't step in lockstep.
  inline void StepSceneRegen(ECS::Registry& _world, uint32_t _tick)
  {
    _world.Each<ScenePoi, PoiResources>([&](ECS::EntityId _anchor, ScenePoi& _poi, PoiResources& _pool)
    {
      // Stagger: each POI regenerates on its own phase within the interval.
      if (((_poi.poiId ^ _tick) % static_cast<uint32_t>(SCENE_REGEN_INTERVAL)) != 0)
        return;
      if (_pool.units < _pool.baseline)
      {
        _pool.units += SCENE_REGEN_PER_STEP;
        if (_pool.units > _pool.baseline) _pool.units = _pool.baseline;
      }
      MaintainBeltRocks(_world, _anchor, _tick);
    });
  }
}
