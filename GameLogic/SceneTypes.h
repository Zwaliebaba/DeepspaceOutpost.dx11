#pragma once

// SceneTypes - per-system scene primitives: POI kinds, the scene components, and
// the tunable constants that govern placement, mining and regeneration
// (GameLogic; design: scene.md).
//
// A "scene" is everything a solar system holds beyond the planet/station/sun
// triad: a set of points of interest (POIs) - mining belts, PvE encounter sites,
// nav beacons and (later) EVE-style structures. Each POI is one durable DB row
// (Server/SceneRows.h, dbo.system_pois) materialized at boot into an ANCHOR
// entity carrying ScenePoi plus whatever the kind scatters around it (belt rocks
// carry OreBody; a minable belt anchor carries PoiResources - the durable pool).
//
// Plain data aggregates only, in absolute int64 world space - the server is
// authoritative, the client renders replicated state. No behaviour here; the
// systems live in SceneSystem.h (materialize/regen) and MiningSystem.h (the mine
// order's beam cycle). Header-only and headless-testable.

#include <cstdint>

#include "Vector3i64.h"
#include "ECS.h"

namespace Neuron::GameLogic
{
  // What an area IS. Values are permanent (they key DB rows); the base game
  // materializes 1-3, the reserved kinds (4-7) get materializers as the EVE tier
  // lands (scene.md 3.8) and an unknown kind in the DB is skipped, not fatal.
  enum class PoiKind : uint8_t
  {
    NavBeacon     = 1,   // plain marked space: rally point, ambush spot
    AsteroidBelt  = 2,   // a minable field (rocks + a durable ore pool)
    EncounterSite = 3,   // an armed EncounterDef anchor (designai.md 3.7)
    // -- reserved, designed-for (scene.md 3.8): --
    MiningFactory = 4,   // ownable auto-extractor structure
    SentryGrid    = 5,   // automatic gun emplacement(s)
    JumpGate      = 6,   // linked-pair teleport chokepoint
    SalvageField  = 7,   // derelict wrecks: loot without mining gear
  };

  // The anchor component: identifies an area and carries the seed that drives all
  // of its in-area procedural detail (rock scatter), so the DB stores anchors +
  // parameters, never individual rocks (scene.md 2). One per POI.
  struct ScenePoi
  {
    uint32_t poiId = 0;           // stable DB key
    int32_t systemId = -1;        // owning galaxy system
    PoiKind kind = PoiKind::NavBeacon;
    uint32_t seed = 0;            // drives in-area detail (rock positions/units)
    int32_t radius = 0;           // area extent in world units (scatter / leash)
    uint16_t encounterDef = 0;    // EncounterSite: the EncounterDef id (else 0)
  };

  // A minable rock: which commodity it yields and how much is left in it. Rocks
  // carry NO Combatant (can't be shot, never collide, never mass-lock - the
  // legacy rock exemption already honoured by InSystemJump), so a belt of stone
  // never traps a jump drive; only ships and stations do.
  struct OreBody
  {
    uint8_t commodity = 0;        // CargoHold.units index a cycle deposits into
    int32_t units = 0;            // ore remaining in this rock
    uint32_t beltAnchor = ECS::INVALID_INDEX;  // the belt whose pool a cycle also drains
                                               // (INVALID for standalone ore: drain the rock only)
  };

  // The durable, authoritative resource pool of a minable POI (scene.md 3.7b),
  // loaded from dbo.poi_resources at boot and persisted on the slow cadence. The
  // pool is the single source of truth; the live rocks are its visible fraction,
  // so a half-mined belt renders sparser. Mining drains `units`; StepSceneRegen
  // drifts it back toward `baseline`, never past it - a stripped belt goes lean
  // (pushing miners onward) but never permanently dead.
  struct PoiResources
  {
    int32_t units = 0;            // current pool
    int32_t baseline = 0;         // regen ceiling (= belt richness at seed time)
    uint8_t commodity = 0;        // the ore this belt yields (mirrors its rocks)
  };

  // Transient per-unit mining progress (NOT persisted - a reboot mid-cycle loses
  // seconds of beam, nothing durable). Present only while a unit runs a Mine
  // order and is parked in range of a rock.
  struct MiningState
  {
    uint32_t rock = ECS::INVALID_INDEX;   // the rock currently being cut (entity index)
    uint16_t progress = 0;                // ticks accumulated toward the next cycle
  };

  // --- Placement (anti-crowding) ---------------------------------------------
  // POIs place in a wide band around the planet, beyond the station bustle and the
  // sun heat band (60k) yet inside comfortable jump reach, and no two closer than
  // POI_MIN_SEPARATION - so activity spreads and areas never stack or land on the
  // station approach. Enforced deterministically at seed time (Server/SceneRows.h).
  inline constexpr int64_t POI_BAND_MIN       = 120'000;
  inline constexpr int64_t POI_BAND_MAX       = 900'000;
  inline constexpr int64_t POI_MIN_SEPARATION = 80'000;
  inline constexpr int      POI_PLACE_ATTEMPTS = 64;      // rejection-sampling cap per POI

  // Distinct SplitMix64 salt so a system's scene seed never collides with its
  // attribute/market seeds (which derive from the same GalaxyConfig seed).
  inline constexpr uint64_t SCENE_SALT = 0x5CE4E5A17ULL;

  // --- Asteroid belts ---------------------------------------------------------
  inline constexpr int      BELT_MAX_ROCKS   = 24;        // live-rock cap per belt
  inline constexpr int32_t  BELT_ROCK_UNITS  = 128;       // nominal ore per fresh rock

  // --- Targeted in-system jump (scene.md 3.6) ---------------------------------
  // A POI is "in this system" when its anchor is within SCENE_JUMP_RANGE of the
  // system's planet (the police-dispatch precedent for "system-local"). Arrival
  // places the hull this far off the anchor, outside every contact range.
  inline constexpr int64_t SCENE_JUMP_RANGE   = 2'000'000;
  inline constexpr int64_t POI_ARRIVAL_OFFSET = 4'000;

  // --- Mining (scene.md 3.7) --------------------------------------------------
  inline constexpr int64_t MINING_RANGE       = 1'200;    // parked-in-range to cut
  inline constexpr int      MINING_CYCLE_TICKS = 120;      // ~4 s per extraction cycle
  inline constexpr int      MINING_YIELD       = 4;        // units per cycle (clamped)

  // --- Depletion & regeneration (scene.md 3.7b) -------------------------------
  inline constexpr int      SCENE_REGEN_INTERVAL = 300;    // ticks between regen steps
  inline constexpr int32_t  SCENE_REGEN_PER_STEP = 2;      // units added toward baseline per step
}
