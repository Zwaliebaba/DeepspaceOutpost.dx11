#pragma once

// SceneTemplates - criteria-to-recipe scene authoring (GameLogic; design:
// scene.md 3.2-3.3).
//
// A scene TEMPLATE maps a system's already-generated character (PlanetData:
// economy/government/tech, from Galaxy.h) to a recipe of POIs. dbseed matches
// each system to its highest-priority template and PLACES the recipe's POIs
// deterministically around the planet, honouring the anti-crowding band +
// separation rules (SceneTypes.h). The server never reads templates - it loads
// the placed rows (Server/SceneRows.h -> dbo.system_pois). So this header is the
// AUTHOR-of-first-draft: pure, deterministic, and unit-tested; the medium can
// later become JSON/an editor without touching any consumer.
//
// Everything is integer arithmetic over the same SplitMix64 hashing as
// GalaxyGen.h, so placement is reproducible forever and property-testable (no
// float, no RNG state).

#include <cstdint>
#include <vector>

#include "Vector3i64.h"
#include "Galaxy.h"        // PlanetData
#include "GalaxyGen.h"     // Detail::Mix64 / Detail::Spread
#include "SceneTypes.h"

namespace Neuron::GameLogic
{
  // Default ore a belt yields (legacy "Minerals" commodity index; mirrors
  // LootSystem's MINERALS_COMMODITY without pulling in the station headers).
  inline constexpr uint8_t DEFAULT_ORE_COMMODITY = 12;

  // One entry in a template's recipe: what to place and how much. `count*` is
  // rolled from the scene seed; belt entries carry the ore + baseline pool; an
  // EncounterSite carries its EncounterDef id; `nearSun` bands around the sun
  // instead of the planet (risk/reward against the cabin-heat mechanic).
  struct PoiSpec
  {
    PoiKind kind = PoiKind::NavBeacon;
    uint8_t countMin = 1;
    uint8_t countMax = 1;
    int32_t radius = 0;           // area extent (belt scatter / site trigger)
    uint8_t commodity = 0;        // AsteroidBelt: ore commodity
    int32_t richness = 0;         // AsteroidBelt: baseline pool (units of ore)
    uint16_t encounterDef = 0;    // EncounterSite: EncounterDef id
    bool nearSun = false;         // band around the sun, not the planet
  };

  // A scene template: criteria (inclusive attribute ranges) + a recipe. A system
  // matches when every attribute falls in range; ties break toward higher
  // `priority`. The Default template (priority 0, full ranges) guarantees every
  // system gets a scene.
  struct SceneTemplate
  {
    uint16_t id = 0;
    uint8_t econMin = 0, econMax = 7;
    uint8_t govMin = 0,  govMax = 7;
    uint8_t techMin = 0, techMax = 15;
    uint8_t priority = 0;
    PoiSpec pois[6] = {};
    uint8_t poiCount = 0;
  };

  // The authored seed set (scene.md 3.2 - tuning values, not law). Ordered by id;
  // MatchTemplate picks the highest-priority match, so overlap is fine.
  inline constexpr SceneTemplate SCENE_TEMPLATES[] =
  {
    // Default floor: always matches, lowest priority.
    SceneTemplate{ 0, 0,7, 0,7, 0,15, 0,
      { PoiSpec{ PoiKind::AsteroidBelt, 1,1, 12'000, DEFAULT_ORE_COMMODITY, 2000, 0, false },
        PoiSpec{ PoiKind::NavBeacon,    1,1,  4'000, 0, 0, 0, false } }, 2 },

    // Frontier belt: poor/agricultural systems are ore-rich and lightly policed.
    SceneTemplate{ 1, 5,7, 0,7, 0,15, 3,
      { PoiSpec{ PoiKind::AsteroidBelt, 2,3, 14'000, DEFAULT_ORE_COMMODITY, 4000, 0, false },
        PoiSpec{ PoiKind::NavBeacon,    1,1,  4'000, 0, 0, 0, false },
        PoiSpec{ PoiKind::EncounterSite,1,1, 20'000, 0, 0, /*def*/1, false } }, 3 },

    // Industrial heart: dense, high-tech - radioactive belts near the sun, wrecks.
    SceneTemplate{ 2, 0,2, 0,7, 9,15, 4,
      { PoiSpec{ PoiKind::AsteroidBelt, 1,1, 10'000, /*Radioactives*/ 1, 1500, 0, true },
        PoiSpec{ PoiKind::SalvageField, 1,1,  8'000, 0, 0, 0, false },
        PoiSpec{ PoiKind::NavBeacon,    1,1,  4'000, 0, 0, 0, false } }, 3 },

    // Anarchy den: lawless - lean belts wrapped in encounters.
    SceneTemplate{ 3, 0,7, 0,1, 0,15, 5,
      { PoiSpec{ PoiKind::AsteroidBelt, 1,2, 12'000, DEFAULT_ORE_COMMODITY, 1200, 0, false },
        PoiSpec{ PoiKind::EncounterSite,2,2, 20'000, 0, 0, /*def*/2, false } }, 2 },

    // Corporate lane: orderly, clean, a protect/courier scene.
    SceneTemplate{ 4, 0,7, 6,7, 0,15, 4,
      { PoiSpec{ PoiKind::AsteroidBelt, 1,1, 10'000, DEFAULT_ORE_COMMODITY, 900, 0, false },
        PoiSpec{ PoiKind::NavBeacon,    1,1,  4'000, 0, 0, 0, false },
        PoiSpec{ PoiKind::EncounterSite,1,1, 20'000, 0, 0, /*def*/3, false } }, 3 },
  };

  // Select the template for a system: the highest-priority one whose ranges all
  // contain the attributes; ties break to the lowest id (stable). Never null - the
  // Default template (id 0) matches everything.
  [[nodiscard]] inline const SceneTemplate& MatchTemplate(const PlanetData& _pl)
  {
    const auto in = [](int _v, uint8_t _lo, uint8_t _hi) { return _v >= _lo && _v <= _hi; };
    const SceneTemplate* best = &SCENE_TEMPLATES[0];   // Default floor
    for (const SceneTemplate& t : SCENE_TEMPLATES)
    {
      if (!in(_pl.economy, t.econMin, t.econMax)) continue;
      if (!in(_pl.government, t.govMin, t.govMax)) continue;
      if (!in(_pl.techLevel, t.techMin, t.techMax)) continue;
      if (t.priority > best->priority || (t.priority == best->priority && t.id < best->id))
        best = &t;
    }
    return *best;
  }

  // A placed POI (pre-row): a concrete area with a world position. Server/SceneRows.h
  // turns these into durable DB rows; the poiId is assigned by the caller across the
  // galaxy (globally unique), so it is left 0 here.
  struct PlacedPoi
  {
    PoiKind kind = PoiKind::NavBeacon;
    Math::Vector3i64 position{};
    int32_t radius = 0;
    uint32_t seed = 0;
    uint8_t commodity = 0;
    int32_t richness = 0;
    uint16_t encounterDef = 0;
    uint16_t templateId = 0;
  };

  namespace Detail
  {
    // Squared Euclidean distance as int64 (safe: each axis <= ~1.8M so the sum of
    // three squares stays well inside int64). Used only for band/separation gating.
    [[nodiscard]] inline int64_t DistSq(const Math::Vector3i64& _a, const Math::Vector3i64& _b)
    {
      const int64_t dx = _a.x - _b.x, dy = _a.y - _b.y, dz = _a.z - _b.z;
      return dx * dx + dy * dy + dz * dz;
    }
  }

  // Place a system's scene deterministically (scene.md 3.3). `_planet`/`_sun` are
  // the band centres; `_avoid` are extra points to keep clear of (station, gate).
  // Integer rejection sampling from the scene seed: each POI tries up to
  // POI_PLACE_ATTEMPTS candidate points in the [POI_BAND_MIN, POI_BAND_MAX] shell
  // and takes the first that clears POI_MIN_SEPARATION from every prior POI and
  // avoid point; a POI that never fits is dropped (the system stays valid). Pure.
  [[nodiscard]] inline std::vector<PlacedPoi> PlaceScene(uint64_t _galaxySeed, uint32_t _systemId,
      const Math::Vector3i64& _planet, const Math::Vector3i64& _sun,
      const std::vector<Math::Vector3i64>& _avoid, const SceneTemplate& _tmpl)
  {
    const uint64_t sceneSeed =
        Detail::Mix64(_galaxySeed ^ Detail::Mix64(static_cast<uint64_t>(_systemId) + 1ull) ^ SCENE_SALT);

    constexpr int64_t bandMinSq = POI_BAND_MIN * POI_BAND_MIN;
    constexpr int64_t bandMaxSq = POI_BAND_MAX * POI_BAND_MAX;
    constexpr int64_t sepSq     = POI_MIN_SEPARATION * POI_MIN_SEPARATION;

    std::vector<PlacedPoi> placed;
    uint64_t stream = sceneSeed;   // walks the hash sequence across all attempts

    for (uint8_t si = 0; si < _tmpl.poiCount; ++si)
    {
      const PoiSpec& spec = _tmpl.pois[si];
      const uint64_t countRoll = Detail::Mix64(sceneSeed + 0x100ull + si);
      const int span = static_cast<int>(spec.countMax - spec.countMin) + 1;
      const int count = spec.countMin + static_cast<int>(countRoll % static_cast<uint64_t>(span < 1 ? 1 : span));
      const Math::Vector3i64& centre = spec.nearSun ? _sun : _planet;

      for (int n = 0; n < count; ++n)
      {
        bool ok = false;
        for (int attempt = 0; attempt < POI_PLACE_ATTEMPTS; ++attempt)
        {
          const uint64_t h = Detail::Mix64(++stream);
          const Math::Vector3i64 cand{
            centre.x + Detail::Spread(Detail::Mix64(h + 1ull), POI_BAND_MAX),
            centre.y + Detail::Spread(Detail::Mix64(h + 2ull), POI_BAND_MAX),
            centre.z + Detail::Spread(Detail::Mix64(h + 3ull), POI_BAND_MAX),
          };
          const int64_t bandSq = Detail::DistSq(cand, centre);
          if (bandSq < bandMinSq || bandSq > bandMaxSq)
            continue;

          bool clear = true;
          for (const PlacedPoi& p : placed)
            if (Detail::DistSq(cand, p.position) < sepSq) { clear = false; break; }
          if (clear)
            for (const Math::Vector3i64& a : _avoid)
              if (Detail::DistSq(cand, a) < sepSq) { clear = false; break; }
          if (!clear)
            continue;

          PlacedPoi p;
          p.kind = spec.kind;
          p.position = cand;
          p.radius = spec.radius;
          p.seed = static_cast<uint32_t>(Detail::Mix64(h + 7ull));
          p.commodity = spec.commodity;
          p.richness = spec.richness;
          p.encounterDef = spec.encounterDef;
          p.templateId = _tmpl.id;
          placed.push_back(p);
          ok = true;
          break;
        }
        (void)ok;   // a POI that never fits is intentionally dropped
      }
    }
    return placed;
  }
}
