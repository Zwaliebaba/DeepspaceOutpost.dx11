# DeepspaceOutpost — Per-System Scenes Design

**Status:** design document, written 2026-07-08 against the as-built code
(`GameLogic/GalaxyGen.h`, `Galaxy.h`, `HyperspaceSystem.h`,
`Server/WorldBuilder.cpp`, `NeuronServer/schema.sql`, `tools/dbseed` verified
in source). Companion to [`designai.md`](designai.md) (the Homeworld-style
fleet AI, accepted 2026-07-08): that document designs *who fights and how*;
this one designs *where* — every solar system gets a **scene**: a layout of
distinct areas (mining belts, PvE sites, nav beacons, and later EVE-style
structures) derived from the system's character, spread out against crowding,
and reachable by a targeted in-system jump.

**Owner decisions recorded here (2026-07-08):**

| Question | Decision |
|---|---|
| Where is scene content authored and stored? | **Templates + DB instances.** Scene *templates* (criteria → area recipe) are data definitions versioned with the code; `tools/dbseed` matches each system's attributes to a template and writes concrete **POI rows** into new tables in the master database. The server only loads rows at boot — the same v2 pattern as `dbo.systems`. An editor later edits rows directly; the DB structure may change to support this. |
| How do players reach an area inside a system? | **Targeted jump.** Extend the existing in-system jump drive: pick an area from the system chart and jump to its arrival beacon. Mass-lock rules still gate departure. (A jump-gate network is *not* planned; a gate could later exist as an authored POI kind if a template wants a chokepoint.) |
| Mining mechanic | **EVE-style beam cycle only.** A mining-laser-equipped ship runs a `Mine` order against an asteroid: lock, cycle, ore straight into the hold. No shoot-and-scoop phase. |
| Are resources finite? | **Deplete + regenerate.** Each mining area has a durable resource pool that extraction drains and that drifts back toward its template baseline — the same drift-back pattern as the living-economy market design (ARCHITECTURE.md §13.2-3). Persisted on a slow cadence. |

Nothing here touches the anti-cheat boundary: scenes are server-materialized
entities, mining is a validated order through the §designai pilot stack, and
the client renders replicated state. This is a **server + persistence + one
new order** design with a small set of reliable-lane wire additions (§3.9).

**Naming.** New identifiers follow
[`.github/coding-standards.md`](.github/coding-standards.md); existing
symbols are referenced by their real names.

---

## 0. The target model

What "every system has a scene, like Homeworld" means here — Homeworld
missions dress each map with distinct zones (the asteroid field, the wreck,
the enemy base) that give the space *structure*; EVE gives each solar system
belts, sites and structures you **warp** between. Combined and adapted:

1. **A scene per system, from criteria.** Each of the 256 systems gets a
   deterministic layout of **points of interest (POIs)** chosen by its
   already-generated character (`PlanetData`: economy, government, tech
   level): a poor agricultural anarchy gets rich belts and a pirate nest; a
   corporate high-tech system gets industrial sites and clean lanes.
2. **Areas are spread out.** POIs place in a wide band around the planet
   (~120k–900k units) with a minimum separation, so activities don't stack
   on the station doorstep and a busy system still has room — the
   anti-crowding requirement.
3. **You jump there.** The system chart lists the scene's areas; a targeted
   in-system jump takes you to the area's arrival beacon. Distance becomes
   *time and choice* (mass-lock, what's camping the beacon) rather than
   minutes of cruising.
4. **Areas do things.** Launch content: **mining belts** (EVE-style beam
   mining into the hold, depleting/regenerating), **encounter sites** (the
   PvE scenes of `designai.md` §3.7, anchored here), and **nav beacons**
   (plain rally/ambush space). Everything else — mining factories, sentry
   guns, gates, salvage fields — is a *future POI kind* the same machinery
   materializes (§3.8).
5. **The master DB is the source of truth; templates are the pen.** dbseed
   writes the generated scene into the DB once; from then on rows are
   editable (hand, script, or a future editor) and the server just loads
   what the DB says. Procedural generation is the *author of first draft*,
   never a runtime dependency.

---

## 1. Current state evaluation

### 1.1 What a "solar system" is today

The world is one seamless `int64³` field (no loading, no instancing —
ARCHITECTURE.md §12). A *system* is a cluster of static entities
materialized by `MaterializeSystem` (`Server/WorldBuilder.cpp:21`):

| Entity | Placement | Source |
|---|---|---|
| Planet | absolute position from the DB row | `dbo.systems.planet_x/y/z` |
| Station | planet + `stationOrbit` (8000) | `dbo.systems.station_x/y/z` |
| Sun | planet + `SUN_SYSTEM_OFFSET` (300 000 on Y) | derived in code |
| Trader gate | 6000 above the planet | derived in code (lane endpoint) |

That is the entire scene: everything else is empty space plus ambient
spawns (`SpawnDirector`: pirates near players, police on crime, lane
traders).

### 1.2 Generation and persistence — the pattern to extend

- `GalaxyGen.h` scatters 256 systems from one `GalaxyConfig` seed
  (`GenerateSystem`, SplitMix64 hashing) and synthesizes each system's
  48-bit `GalaxySeed`, from which `GeneratePlanet` (`Galaxy.h:88`) derives
  **economy 0–7, government 0–7, tech level, population, productivity** —
  the criteria this design keys scenes off. All already in
  `dbo.systems` columns.
- **Schema v2** (`NeuronServer/schema.sql`): the universe's locations are
  *durable rows*, seeded once by `tools/dbseed` (idempotent MERGE), loaded
  at boot by `WorldBuilder`, hand-editable by design ("the rows can be
  authored/edited directly", schema.sql:99). Static layout in the DB
  honors §12's "never per-tick positions" rule. **This is exactly the
  authoring model the owner chose — scenes add tables to it, not a new
  mechanism.**
- Persistence writes off the sim thread on slow cadences
  (`PersistenceService`), with market drift as the precedent for "durable
  state that drifts and is persisted lazily".

### 1.3 In-system travel today

`InSystemJump` (`HyperspaceSystem.h:197`, the legacy `jump_warp`): a hop of
up to `IN_SYSTEM_JUMP_MAX` (200k) **toward the nearest planet only**,
blocked when mass-locked (any `Combatant` hull or planet within
`MASS_LOCK_RANGE`). Loot/missiles carry no `Combatant` and never mass-lock
(the legacy cargo/rock exemption — which, conveniently, will also exempt
asteroids, §3.6). There is no way to name a destination.

### 1.4 Mining and asteroids today

There is no mining. Minerals exist only as market commodities and as
loot-canister drops (`LootSystem.h`, Rock mesh for mineral canisters).
Asteroid *meshes* already exist client-side (`GameData/Models/asteroid.obj`,
`rock.obj`, `rock_hermit.obj` with legacy stats JSONs) — rendering a belt
needs `NetType` rows in `RenderTable.h`, not new art.

### 1.5 Adjacent designs this must fit

- **`designai.md` §3.7** defines open-world PvE encounters whose anchors
  are "deterministic world anchors (per-system, from the galaxy seed)" and
  are *supplied by the director*. This document is where those anchors come
  from: an **EncounterSite POI**. designai.md deliberately kept
  `EncounterDef` free of absolute coordinates for exactly this seam.
- **`designai.md` §3.5 Route / ARCHITECTURE.md §13.2-3 living economy**:
  hauler routes over station chains. Mining factories (§3.8) become route
  endpoints — ore supply feeding the market drift design.
- **AOI + strategic tier** (`AreaOfInterest.h`, `StrategicView.h`): POIs are
  ordinary static entities — high-detail inside AOI, and natural rows for
  the 1 Hz strategic rollup ("belt at 60 %, 2 hostiles").

### 1.6 Gaps

1. No concept of a POI/area: systems are planet + station + sun and nothing
   else; all play happens on the station doorstep (the crowding problem).
2. No targeted in-system travel; the jump drive knows only "toward the
   planet".
3. No minable entities, no mining verb, no ore source but the market.
4. No per-system content variation: a rich industrial system and a poor
   anarchy differ only in market numbers.
5. No DB home for scene/POI state (layout or resources).
6. The encounter system designed in `designai.md` has no anchor supply.

---

## 2. Design overview

```
                 TEMPLATES (data, versioned with code)
   SceneTemplate: criteria (economy/gov/tech ranges) → POI recipe
                 │  matched + placed deterministically
                 ▼  by tools/dbseed (once; re-runnable)
                 MASTER DATABASE (source of truth)
   dbo.system_pois      one row per placed POI (kind, position, params)
   dbo.poi_resources    durable resource pool per mining POI
                 │  loaded at boot (WorldBuilder), editable by
                 │  hand / script / future editor
                 ▼
                 SERVER RUNTIME
   Materializer: POI rows → entities (beacon + belt rocks / encounter
                 anchor / …) · SceneIndex (system → POIs)
   StepMining:   the Mine order's beam cycle (ore → hold, pool drain)
   StepSceneRegen: slow drift of pools back toward baseline (+ persistence)
   InSystemJump: targeted variant — jump to a POI's arrival beacon
                 │  replication (AOI) · scene chunk (chart data)
                 ▼
                 CLIENT
   System chart lists the scene's areas → jump → fly/mine/fight
```

The load-bearing choice: **the DB stores anchors and parameters, never
individual rocks.** A belt row says "Minerals belt, radius 12k, richness R,
seed S at position P"; the server materializes and respawns the actual
asteroid entities from that row deterministically. Rows stay few (3–7 per
system, ~1–2k total), the editor edits meaningful things, and §12's
"no per-tick positions in SQL" is never threatened.

---

## 3. The design

### 3.1 Vocabulary and components

- **Scene** — everything placed in one system beyond the planet/station/sun
  triad: the system's set of POIs. Not an entity; a query
  (`SceneIndex: systemId → [poiId]`).
- **POI (point of interest)** — one authored area: a kind, a world
  position, parameters, and (if minable) a resource pool. One DB row; at
  runtime an **anchor entity** plus whatever the kind materializes around
  it.

```cpp
enum class PoiKind : uint8_t
{
  NavBeacon    = 1,   // plain marked space: rally point, ambush spot
  AsteroidBelt = 2,   // minable field (§3.6)
  EncounterSite= 3,   // designai.md §3.7 anchor (armed EncounterDef)
  // -- reserved, designed-for (§3.8): --
  MiningFactory= 4,   // ownable auto-extractor structure
  SentryGrid   = 5,   // automatic gun emplacement(s)
  JumpGate     = 6,   // linked-pair teleport chokepoint
  SalvageField = 7,   // derelict wrecks, loot without mining gear
};

struct ScenePoi                 // component on the anchor entity
{
  uint32_t poiId = 0;           // DB key
  int32_t systemId = -1;
  PoiKind kind = PoiKind::NavBeacon;
  uint32_t seed = 0;            // drives all in-area procedural detail
  int32_t radius = 0;           // the area's extent (rock scatter, leash)
};
```

Anchor entities are static (`WorldTransform` + `ScenePoi` + `NetType`
beacon glyph), replicate through the normal snapshot path, and are listed
in the scene chunk (§3.9) so the chart can show them galaxy-wide without
AOI.

### 3.2 Scene templates — criteria → recipe

A **template** is a data definition, versioned with the code (initially
`constexpr` tables in `GameLogic/SceneTemplates.h` — the same
medium-vs-format trajectory as designai.md §3.7: the format is the
contract; a JSON/config loader or editor can replace the storage later
without touching the consumer, because **only dbseed reads templates**;
the server never does):

```cpp
struct PoiSpec
{
  PoiKind kind;
  uint8_t countMin, countMax;     // rolled from the scene seed
  int32_t radius;                 // area extent
  // kind params (belt: commodity + richness; site: encounterDef id; …)
  uint8_t commodity;  int32_t richness;  uint16_t encounterDef;
};

struct SceneTemplate
{
  uint16_t id;
  // criteria: a system matches when every attribute is in range
  uint8_t econMin, econMax;       // 0 rich industrial .. 7 poor agricultural
  uint8_t govMin,  govMax;        // 0 anarchy .. 7 corporate state
  uint8_t techMin, techMax;
  uint8_t priority;               // highest-priority match wins ties
  PoiSpec pois[6]; uint8_t poiCount;
};
```

**Seed templates** (first authored set — tuning values, not law):

| Template | Criteria | Recipe |
|---|---|---|
| Frontier belt | economy ≥ 5 (agri/poor) | 2–3 Minerals belts (rich), 1 NavBeacon, 1 EncounterSite (pirate nest) if government ≤ 2 |
| Industrial heart | economy ≤ 2, tech ≥ 9 | 1 Radioactives belt (near-sun band), 1 SalvageField*, 1 NavBeacon |
| Anarchy den | government ≤ 1 | 1–2 belts, 2 EncounterSites (nest + convoy raid), no police lane bias |
| Corporate lane | government ≥ 6 | 1 lean belt, 1 NavBeacon, 1 EncounterSite (courier/protect) |
| Default | always matches (priority 0) | 1 belt, 1 NavBeacon |

*Reserved kinds appear in templates only once their runtime lands; dbseed
refuses to write a kind the target schema version doesn't know.

Selection is `HighestPriorityMatch(PlanetData)` — total, deterministic,
unit-testable. The `Default` row guarantees every system has *a* scene.

### 3.3 Deterministic placement — the anti-crowding rules

All placement happens in dbseed (write-time), from
`sceneSeed = Mix64(cfg.seed ^ Mix64(systemId) ^ SCENE_SALT)` — the same
SplitMix64 discipline as `GenerateSystem` (`GalaxyGen.h:67`).

- **Distance band:** each POI places at `POI_BAND_MIN` (120 000) to
  `POI_BAND_MAX` (900 000) units from the planet — beyond the station
  bustle and sun heat band (60k), inside comfortable jump reach, and far
  enough apart that each area is its own AOI neighbourhood (tactical AOI
  is much smaller than the inter-POI spacing — areas never leak into each
  other's replication).
- **Minimum separation:** ≥ `POI_MIN_SEPARATION` (80 000) from every other
  POI in the system, the sun, the trader gate, and the planet/station.
- **Method:** deterministic rejection sampling — candidate positions are
  the sequence `Spread(Mix64(sceneSeed + attempt), …)` (the existing
  `Detail::Spread` mapping) filtered by the rules above, first-fit, with a
  fixed attempt cap (drop the POI if the cap is hit — a full system stays
  valid). No RNG state, reproducible forever, property-testable.
- **Near-sun placement:** a spec flagged near-sun (Radioactives belts)
  bands around the sun position instead — risk/reward against the cabin
  heat mechanic (§6.9), fuel-scoop synergy for haulers.

Anti-crowding is thus *structural*: activity concentrates where the
template put an area, and areas cannot generate on top of each other or on
the station approach.

### 3.4 Master database — schema v3 (append-only block)

Following the v2 conventions exactly (guarded `IF NOT EXISTS`, no edits to
prior blocks, version bump at the end):

```sql
-- v3: per-system scenes. One row per placed POI; anchors + parameters only —
-- the server materializes in-area detail (individual rocks) from poi seeds.
CREATE TABLE dbo.system_pois (
  poi_id        INT PRIMARY KEY,      -- stable id; assigned by the seeder (not IDENTITY)
  system_id     INT NOT NULL REFERENCES dbo.systems(system_id),
  kind          TINYINT NOT NULL,     -- PoiKind
  x             BIGINT NOT NULL,      -- absolute int64 anchor position
  y             BIGINT NOT NULL,
  z             BIGINT NOT NULL,
  radius        INT NOT NULL,         -- area extent (units)
  seed          INT NOT NULL,         -- in-area procedural detail
  commodity     TINYINT NULL,         -- AsteroidBelt: ore commodity index
  richness      INT NULL,             -- AsteroidBelt: baseline pool (units of ore)
  encounter_def SMALLINT NULL,        -- EncounterSite: designai.md EncounterDef id
  owner_empire  INT NULL REFERENCES dbo.empires(empire_id),  -- future: factories/guns
  link_poi      INT NULL,             -- future: JumpGate pairing
  enabled       BIT NOT NULL DEFAULT 1,
  template_id   SMALLINT NOT NULL,    -- provenance: which template authored it
  authored      BIT NOT NULL DEFAULT 0 -- 1 = hand-edited: dbseed MERGE must not touch
);

CREATE TABLE dbo.poi_resources (      -- durable pool per minable POI (drift-persisted)
  poi_id        INT PRIMARY KEY REFERENCES dbo.system_pois(poi_id),
  units         INT NOT NULL,         -- current pool
  baseline      INT NOT NULL,         -- regen target (= richness at seed time)
  updated_tick  BIGINT NOT NULL
);
```

Contract details that make the editor story work:

- **`authored` guards hand edits.** dbseed's MERGE re-establishes the
  generated baseline for rows it owns but **never overwrites a row marked
  `authored = 1`** (and never resurrects a row deleted by an editor — it
  inserts only missing poi_ids). Regenerating the galaxy and hand-dressing
  systems coexist.
- **`enabled` is the editor's kill switch** — turn an area off without
  losing the row.
- **Editing is the SQL surface.** Day one, "the editor" is any SQL tool or
  a script writing these rows; the schema *is* the editor API. A dedicated
  in-house editor later (plausibly reusing the client's `ChartWindow`
  rendering against a direct DB connection) is out of scope here but needs
  nothing this design doesn't already store. Scene changes take effect at
  server boot; a live `SceneReload` admin command is a future nicety, not a
  dependency.
- Extending POI kinds = new nullable columns or (if a kind ever needs many
  params) a `poi_params` key-value side table — both append-only v4+
  blocks. The owner has explicitly cleared DB structure changes.

### 3.5 Runtime materialization

`WorldBuilder` grows a second loop (after systems): for each enabled POI
row, create the **anchor** entity, register it in the `SceneIndex`, and run
the kind's materializer:

- **NavBeacon:** the anchor alone (a `NetType` beacon glyph — far contacts
  already draw as symbology via `ShouldDrawAsGlyph`).
- **AsteroidBelt:** the anchor plus up to `BELT_MAX_ROCKS` (24) asteroid
  entities scattered inside `radius` from the POI `seed`:
  `WorldTransform` + `NetType{Asteroid}` (mesh exists, §1.4) + slow random
  tumble/drift `Velocity` +

  ```cpp
  struct OreBody { uint8_t commodity; int32_t units; };
  ```

  Each rock's `units` is drawn from the belt's *pool* (§3.7); the live rock
  count scales with pool fraction — a half-mined belt is visibly sparser.
  Rocks carry **no `Combatant`**: they can't be targeted by weapons, pay no
  bounty, never collide (§6.9 exempts non-combatants) and — matching the
  legacy rock exemption already in `InSystemJump` — **never mass-lock**, so
  a belt full of stone doesn't trap your jump drive; only ships and
  stations do.
- **EncounterSite:** the anchor plus an armed `EncounterDirector` instance
  (designai.md §3.7) with `encounter_def` at this anchor — closing the
  "anchors are supplied by the director" seam: **the director's anchor
  source is the scene.** Trigger radius = POI `radius`.
- Reserved kinds (§3.8) get materializers when they land; an unknown kind
  in the DB is logged and skipped (forward compatibility).

Everything materialized is ordinary ECS + replication: AOI decides who
sees the rocks; the chart-level *existence* of areas travels in the scene
chunk (§3.9), mirroring how all 256 systems ship to the chart today.

### 3.6 Targeted in-system jump

Extend the travel path (`TravelRequest` → `HyperspaceSystem`), preserving
`InSystemJump`'s planet-hop as the no-target fallback:

- **Request:** `TravelRequest` gains a jump-to-POI variant carrying
  `poiId`.
- **Validation (server):** the POI exists, is enabled, and belongs to the
  player's **current system** — defined in the seamless world as the
  system whose planet is nearest the player, with the POI inside
  `SCENE_JUMP_RANGE` (2 000 000 — the police-dispatch precedent for
  "system-local") of that planet. Cross-system travel remains hyperspace's
  job. Rejections reuse `TravelStatus` (`MassLocked`, `Rejected`,
  `UnknownSystem` semantics extended with `UnknownPoi`).
- **Mass-lock gates departure** exactly as today: any `Combatant` hull in
  `MASS_LOCK_RANGE` pins you. Deliberate consequences kept: you cannot
  jump *out* of a fight (EVE's tackle, for free), a camped beacon is
  escapable by *flying* clear first, and rocks never pin (§3.5).
- **Arrival:** placed at the POI anchor + `POI_ARRIVAL_OFFSET` (4000)
  along a deterministic per-player-per-tick scatter direction (no RNG
  state; hash of playerId ^ tick), outside every contact range, **without**
  spawn grace — arriving at a hot beacon is exposed by design (the
  hyperspace-arrival precedent: "arriving in flight, fly in and dock").
  No fuel cost (the legacy in-system jump is free; distance is gated by
  mass-lock and the departure/arrival risk instead).
- **Fleet jumps:** a jump is per-hull. Ordered units (designai.md) jump
  with their owner when the owner's selection jumps — expressed as the
  client issuing the same validated request per selected unit; a future
  `FleetJump` convenience message is a wire nicety, not a mechanic.
- **Client:** the system chart (`ChartWindow`) and a "local" tab list the
  current system's POIs from the scene chunk (name, kind glyph, distance);
  selecting one and pressing the existing HYPERSPACE/JUMP affordance sends
  the request. POI names are generated (`"<System> Belt I"`,
  `"<System> Beacon"`) — no name column until the editor wants one.

### 3.7 Mining — the EVE-style beam cycle

**Equipment:** `MiningLaser` joins the station Equip list
(`StationRequestKind::Equip`, a `PersistEquipFlags` bit — persistence-ready
like every equipment flag). Price ~2 000 Cr (tuning). Any ship can fit it;
dedicated mining hulls are a later ship-data decision.

**The order:** `OrderKind::Mine = 10` (next free value after the reserved
`Route = 9`; designai.md's queueing and stances apply to it like any
order):

- **Validation** (`PlanUnitOrder` matrix row): target has `OreBody` *or* is
  a belt anchor (`ScenePoi{AsteroidBelt}`); the unit carries `MiningLaser`;
  the unit has hold space now (a full hold rejects with a new
  `OrderStatus::HoldFull`).
- **Execution** (`StepOrders` + a new `StepMining` in `GameLogic`):
  1. Fly to the rock (the §designai pilot stack: steering + avoidance;
     rocks aren't hulls, so the no-crash berth doesn't apply — parking
     close is the point) and hold inside `MINING_RANGE` (1 200).
  2. Cycle: every `MINING_CYCLE_TICKS` (120 ≈ 4 s) in range extracts
     `MINING_YIELD` (min(4, rock.units, hold space)) units of the rock's
     commodity into `CargoHold` — tonnage rules and `CargoManifest` resend
     exactly as scooping does today.
  3. The rock's `OreBody.units` and the belt's durable pool (§3.7b) both
     decrement. An emptied rock despawns (`EntityDeath` broadcast, like any
     entity removal); the order **auto-retargets the nearest live rock
     inside the belt radius** — order the belt, mine the belt (the
     EVE/Homeworld worker feel). Ordering a specific rock works too.
  4. Completion: hold full (`UnitOrderAck`-style toast via the existing
     order-complete path) or belt empty. A Route order (designai.md §3.5)
     can queue `Mine → Dock(sell) → Mine` — the first player-built ore
     loop, no new mechanism.
- **Per-unit state:** `struct MiningState { uint32_t rock; uint16_t progress; }`
  — transient, not persisted (a reboot mid-cycle loses seconds of beam,
  nothing durable).
- **Presentation:** a `MiningTick` event (AOI-scoped, §3.9) drives the
  beam VFX through the existing firing-beam path and a pickup sound — the
  beam is cosmetic; the extraction already happened server-side.
- **PvP reality:** mining is deliberately *exposed* — a mining ship is
  slow-parked in a known area with a full-ish hold, mass-lockable by any
  tackler. That is the EVE risk loop, and the crime system already prices
  the attack (clean miner = crime; the §designai Evasive stance is the
  miner's friend). No new protection mechanics.

**§3.7b Depletion & regeneration.** The belt's pool is authoritative and
durable:

- Live state: `PoiResources { int32 units, baseline }` on the anchor
  entity, loaded from `dbo.poi_resources` at boot (falling back to
  `richness` when the row is missing — pre-v3 saves stay valid).
- Extraction decrements the pool 1:1 with mined units. Rock respawn
  (holding the belt at its density curve) *allocates from the pool* — the
  pool is the single source of truth; rocks are its visible fraction.
- `StepSceneRegen`, at strategic cadence (every 300 ticks, staggered per
  POI): `units += REGEN_PER_STEP` toward `baseline`, never past it. Regen
  is slow by design (~5–10 % of baseline per hour, tuning) — a stripped
  belt stays lean long enough to push miners to the next system
  (the movement/scarcity goal), never permanently dead (`baseline` is the
  floor's guarantee).
- Persistence: the pool piggybacks the market-drift pattern — dirty pools
  written on the slow persistence cadence, `updated_tick` stamped; never
  per-tick, never per-rock.

### 3.8 The EVE tier — future POI kinds (designed-for, not built)

The flexibility requirement lands here: each future feature is **a new
`PoiKind` + a materializer + (usually) one component/system**, authored by
templates or the editor through the *same* rows. None requires touching
the scene machinery again:

- **MiningFactory (kind 4):** an ownable structure anchored in a belt:
  extracts from the *same pool* (§3.7b) at a slow automatic rate into an
  internal store; owned haulers on Route (designai.md §3.5 /
  ARCHITECTURE.md §13.2-3) collect and sell. Deployment is a validated
  station-bought deployable (the §13.2-2 outpost-kit pattern);
  `owner_empire` (already in the v3 schema) makes it durable property.
  Attackable → the territory game arrives with the faction/standings work
  (§13.2-4).
- **SentryGrid (kind 5):** 1–N automatic gun emplacements around the
  anchor: a `Combatant` with `autoEngage = true` and **no Flight** — it is
  a turret by construction; `StepCombat` already fires it at whatever its
  legality predicate allows. Legality = the owner's standings (the
  designai.md §3.8 principle: stances/automation choose *how hard*, the
  legality layer chooses *whom*). Guards a factory, a gate, or an authored
  PvE site.
- **JumpGate (kind 6):** paired anchors (`link_poi`); flying into the gate
  radius teleports to the twin (the hyperspace-relocate mechanism, minus
  fuel). Gives templates and the editor a chokepoint tool when a scene
  wants EVE-style topology; deliberately *not* part of the base travel
  model (owner decision §top).
- **SalvageField (kind 7):** scattered wreck entities with `LootItem`
  holds — the scoop/Collect path already consumes them; no mining gear
  needed, richer in industrial/war-torn templates.

The checklist for any new kind (this is the whole extension contract):
enum value → v(N) schema columns if new params → dbseed template use →
materializer → optional component/system → `RenderTable.h` row. Each step
is additive; old servers skip unknown kinds (§3.5).

### 3.9 Wire & protocol additions (reliable lanes; snapshot untouched)

| Message | Dir | Content | Notes |
|---|---|---|---|
| `SceneChunk` | S→C | per-system POI list: poiId, systemId, kind, position, radius | cold chart data, chunked like the existing galaxy manifest (`GalaxyChunks.h`); sent on join, re-sent on scene reload |
| `TravelRequest` v2 | C→S | + jump-to-POI variant `{poiId}` | same message, new kind discriminant; `TravelStatus` + `UnknownPoi` |
| `UnitOrder` (`Mine`) | C→S | existing message, new `OrderKind::Mine = 10` | validated per §3.7; `OrderStatus::HoldFull` added |
| `MiningTick` | S→C | miner entity, rock entity, commodity, units | AOI-scoped VFX/sound event; `CargoManifest` resend rides the existing path |

Replicated components: asteroids are plain entities (position/type — the
snapshot path as-is); `OreBody.units` does **not** replicate per-tick — the
client infers rock health from despawns, and the belt pool arrives as a
percentage in the strategic tier later (nice-to-have).

---

## 4. Flexibility: how the setup adapts

1. **Templates author the galaxy; the DB owns it.** Criteria→recipe rows
   regenerate a coherent first draft any time; `authored`/`enabled` bits
   let hand edits and regeneration coexist. The future editor needs zero
   new server features — it edits the same rows dbseed writes.
2. **POI kinds are the plugin surface.** Mining factories, sentry guns,
   gates, salvage — each is one enum value + one materializer on top of
   frozen machinery (§3.8's checklist). The v3 schema already carries the
   columns the named future kinds need (`owner_empire`, `link_poi`).
3. **Pools, not scripts, carry the economy.** The durable resource pool is
   the one number automation (factories), players (mining), and the living
   economy (ore supply) all meet at — adding any of them changes no
   schema.
4. **Scenes anchor the other designs.** Encounter sites give designai.md
   its anchors; belts give Route its endpoints; POIs give the strategic
   tier its rollup rows; templates give the 4X fog-of-war something worth
   scouting. One layout layer, consumed everywhere.
5. **Determinism end-to-end.** Template match, placement, rock scatter,
   arrival offsets: pure functions of stored seeds — replayable, golden-
   testable, and identical after any reboot.

---

## 5. Testing strategy

Headless GoogleTest, per the house method:

- **Template matrix:** every (economy, government, tech) triple matches
  exactly one template (the `Default` floor), ties broken by priority —
  exhaustive over the 8×8×16 attribute space.
- **Placement properties:** for a spread of seeds: every POI inside its
  band, pairwise separations ≥ `POI_MIN_SEPARATION`, clear of
  sun/gate/station, byte-identical across runs; attempt-cap exhaustion
  drops POIs without violating invariants.
- **Jump validation matrix:** unknown POI / disabled / wrong system /
  mass-locked / clean jump; arrival offset outside every contact range;
  determinism of the scatter direction.
- **Mining cycle:** yield clamps (rock, hold, tonnage rules), manifest
  resend, rock despawn + auto-retarget, order completion on hold-full and
  belt-empty, `HoldFull` rejection, no-laser rejection.
- **Pool conservation (the crown jewel):** mine a belt dry across rock
  respawns — total ore in holds equals pool drained, exactly; regen never
  overshoots baseline; reboot mid-way (persist + reload) conserves.
- **dbseed:** idempotent re-run; `authored = 1` rows untouched; deleted
  rows not resurrected; unknown-kind rows skipped by the loader with a log.
- **BotClient scenario:** a miner bot loop (jump → mine → dock → sell) for
  soak and tick-budget sanity at belt density.

---

## 6. Phasing

| # | Phase | Contents | Effort | Depends on |
|---|---|---|---|---|
| S1 | **Scene substrate** | `PoiKind`/`ScenePoi`, templates + deterministic placement, schema v3, dbseed extension, WorldBuilder materialization (beacons only), `SceneChunk` + chart "local areas" list | M | — |
| S2 | **Targeted jump** | `TravelRequest` v2, validation, arrival scatter | S–M | S1 |
| S3 | **Belts & mining** | Asteroid materializer + `OreBody`, `MiningLaser` equip, `OrderKind::Mine` + `StepMining`, `MiningTick` VFX | M–L | S1 (S2 in practice) |
| S4 | **Depletion & regen** | `poi_resources` + boot load, pool-backed rock respawn, `StepSceneRegen`, slow-cadence persistence | M | S3 |
| S5 | **Encounter sites** | EncounterSite materializer binding designai.md's `EncounterDirector` to POI anchors | S | S1 + designai A6 |
| S6 | **EVE tier** *(future, per-kind mini-designs)* | MiningFactory → SentryGrid → JumpGate/SalvageField as prioritized | L each | S4 (+§13.2-2/-4 for ownership/standings) |

Fits the roadmap: S1–S4 are self-contained and give the 4X tier's
fog-of-war (#1) something to explore and the living economy (#3) its ore
supply; S5 closes the anchor seam designai.md A6 opens; S6 items slot
behind ownership (#2) and factions (#4) exactly where ARCHITECTURE.md
already sequences them.

**Risks & mitigations:**

- *Empty-world feel* — 3–7 areas across 1.8M units can still feel sparse.
  Mitigate: the chart sells the scene (names, glyphs, distances) before
  the eye does; ambient `SpawnDirector` pressure biases toward POIs
  (pirates prefer belts — a one-line spawn-anchor change) so areas feel
  inhabited.
- *Belt tick cost* — rocks are static non-combatants: no AI, no collision
  pairs (no `Combatant`), negligible broadphase load; `BELT_MAX_ROCKS`
  caps entity count (~24 × ~500 belts ≈ 12k static entities worst case —
  measure with BotClient before raising caps, roadmap #15).
- *Editor drift* — hand-edited rows that violate placement invariants
  (overlaps, in-sun placement). Mitigate: the loader logs violations and
  materializes anyway (the editor is trusted); a dbseed `--validate` mode
  checks a live DB against the §3.3 rules.
- *Mining balance* — yield × cycle × price is a three-knob economy faucet.
  Mitigate: all three are single constants; the drifting-market design
  (#3) is the eventual sink; until then station markets' fixed prices cap
  the faucet naturally (stock 0–63 quantities bound what a station buys).

---

## 7. Locked-decision compliance (ARCHITECTURE.md §12)

| Decision | This design |
|---|---|
| Server-authoritative | Scenes materialize server-side; mining/jumps are validated requests; the client renders replicated state and chart chunks |
| Seamless world | POIs are absolute-int64 placements in the one field; "system" stays a proximity notion (nearest planet); no instancing |
| Unit orders / anti-cheat boundary | Mining is `OrderKind::Mine` through `PlanUnitOrder`; jump is a validated `TravelRequest`; the client cannot conjure ore or position |
| Persistence: durable state only, never per-tick positions | DB stores anchors, params, and slow-drift pools; rocks and positions are derived from seeds at runtime |
| Entity model: in-house ECS, spatially indexed | POIs/rocks are plain entities; `SceneIndex` is a maintained index like `OwnershipIndex` |
| Determinism for replays/tests | Placement, scatter, cycles: pure seeded functions; pool conservation golden test |
| Replication, not lockstep | Rocks ride the existing snapshot path; scene layout is a cold reliable chunk |
| GameLogic server-only; schemas shared | Materializers/`StepMining`/`StepSceneRegen` in GameLogic; `ScenePoi`/`OreBody`/message defs are NeuronCore schema data |
| 4X/RTS trajectory, evolve-don't-rewrite | Extends GalaxyGen/dbseed/WorldBuilder/travel/orders in place; feeds fog-of-war, territory, living economy, factions |
| Pointer-first interaction | Areas are chart selections + existing order/travel affordances; nothing keyboard-exclusive |
| Aesthetic | Belts use the existing low-poly asteroid meshes; beacons are glyph symbology — retro-vector amplified, not replaced |
