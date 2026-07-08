#pragma once

// SceneRows - map between the scene TEMPLATES (GameLogic/SceneTemplates.h) and the
// durable POI rows (NeuronServer). The one place that turns "this system's
// character" into placed dbo.system_pois rows, shared by the seeding tool
// (tools/dbseed, which writes them) and testable in isolation. Keeping the
// conversion here guarantees the rows the seeder writes are exactly what
// PlaceScene produced - the template becomes a one-time way to author the table,
// not a second, drifting source of truth (the same discipline as GalaxyRows.h).
//
// Header-only; depends on GameLogic (GalaxyGen/SceneTemplates) + the plain Persist
// row structs. No sockets, no loop state.

#include <cstdint>
#include <vector>

#include "GalaxyGen.h"          // GenerateGalaxy / GalaxySystem / GalaxyConfig
#include "SceneTemplates.h"     // MatchTemplate / PlaceScene / PlacedPoi
#include "SceneTypes.h"         // PoiKind
#include "CabinHeatSystem.h"    // SUN_SYSTEM_OFFSET (sun position for near-sun placement)

#include "PersistenceStore.h"   // Neuron::Persist::PoiRow / PoiResourceRow

namespace DSOServer
{
  // Offset of a system's trader gate above its planet (a placement avoid-point;
  // matches the WorldBuilder trader-lane endpoint).
  inline constexpr int64_t SCENE_TRADER_GATE_OFFSET = 6000;

  // Generate every scene POI row for a whole galaxy, deterministically. Each
  // system is matched to its template and its recipe is placed around the planet
  // (near-sun specs band around the sun) clear of the station/sun/gate; POIs get a
  // GLOBALLY UNIQUE poiId assigned in scan order. Belt POIs carry their ore +
  // baseline; the pool rows come from BaselinePoiResourceRows below.
  [[nodiscard]] inline std::vector<Neuron::Persist::PoiRow> BuildPoiRows(const Neuron::GameLogic::GalaxyConfig& _cfg)
  {
    using namespace Neuron;
    std::vector<Persist::PoiRow> rows;
    const std::vector<GameLogic::GalaxySystem> systems = GameLogic::GenerateGalaxy(_cfg);

    int32_t nextId = 0;
    for (const GameLogic::GalaxySystem& s : systems)
    {
      const Math::Vector3i64 planet = s.planetPos;
      const Math::Vector3i64 sun{ planet.x, planet.y + GameLogic::SUN_SYSTEM_OFFSET, planet.z };
      const Math::Vector3i64 gate{ planet.x, planet.y + SCENE_TRADER_GATE_OFFSET, planet.z };
      const std::vector<Math::Vector3i64> avoid{ s.stationPos, sun, gate };

      const GameLogic::SceneTemplate& tmpl = GameLogic::MatchTemplate(s.planet);
      const std::vector<GameLogic::PlacedPoi> placed =
          GameLogic::PlaceScene(_cfg.seed, s.id, planet, sun, avoid, tmpl);

      for (const GameLogic::PlacedPoi& p : placed)
      {
        Persist::PoiRow r;
        r.poiId        = nextId++;
        r.systemId     = static_cast<int32_t>(s.id);
        r.kind         = static_cast<int32_t>(p.kind);
        r.x            = p.position.x;
        r.y            = p.position.y;
        r.z            = p.position.z;
        r.radius       = p.radius;
        r.seed         = static_cast<int32_t>(p.seed);
        const bool belt = (p.kind == GameLogic::PoiKind::AsteroidBelt);
        r.commodity    = belt ? static_cast<int32_t>(p.commodity) : -1;
        r.richness     = belt ? p.richness : -1;
        r.encounterDef = (p.kind == GameLogic::PoiKind::EncounterSite) ? static_cast<int32_t>(p.encounterDef) : -1;
        r.ownerEmpire  = -1;
        r.linkPoi      = -1;
        r.enabled      = true;
        r.templateId   = static_cast<int32_t>(p.templateId);
        r.authored     = false;
        rows.push_back(r);
      }
    }
    return rows;
  }

  // The baseline resource pool rows for the minable POIs among `_pois` (a full belt
  // at seed time), stamped `_tick`. Non-belt POIs get no pool row.
  [[nodiscard]] inline std::vector<Neuron::Persist::PoiResourceRow> BaselinePoiResourceRows(
      const std::vector<Neuron::Persist::PoiRow>& _pois, uint64_t _tick)
  {
    std::vector<Neuron::Persist::PoiResourceRow> rows;
    for (const Neuron::Persist::PoiRow& p : _pois)
    {
      if (p.kind != static_cast<int32_t>(Neuron::GameLogic::PoiKind::AsteroidBelt))
        continue;
      Neuron::Persist::PoiResourceRow r;
      r.poiId = p.poiId;
      r.units = p.richness > 0 ? p.richness : 0;
      r.baseline = r.units;
      r.updatedTick = _tick;
      rows.push_back(r);
    }
    return rows;
  }
}
