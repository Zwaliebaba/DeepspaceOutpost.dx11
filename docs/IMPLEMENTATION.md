# DeepspaceOutpost — Implementation Plan

**Status:** derived from a full code audit against `docs/ARCHITECTURE.md`
(the canonical design document), 2026-07-03. Every claim below was verified
against the source at audit time; file/line references are to that snapshot.

This document is the **execution companion** to ARCHITECTURE.md: it records
where the code actually diverges from the design (§1), inventories dead and
legacy code (§2–§3), and then specifies, work item by work item, how to
implement the complete game described in ARCHITECTURE.md §12–§14 (§4–§11).
ARCHITECTURE.md stays the *what and why*; this document is the *what exactly,
where, and in what order*. When the two disagree, ARCHITECTURE.md wins —
update it first, then this plan.

Conventions used throughout:

- Roadmap numbers `#1–#21` refer to ARCHITECTURE.md §14; `S1–S7` to §13.1;
  `E1–E8` to §13.3.
- Effort: **XS** ≤ hours, **S** ≤ a day-ish, **M** = days, **L** = week(s).
- Every work item lands with headless GoogleTest coverage in the matching
  `Tests/<Library>/` suite and passes `ctest --output-on-failure` on
  x64 debug + release (what CI runs). New wire messages must pass the
  registry-governance tests (unique id, band/scope consistency, direction).
- Message ids are permanent ABI: new ids are *allocated*, old ids are
  *retired in place* (kept reserved, never reused).

---

## 1. Audit summary — code vs ARCHITECTURE.md

### 1.1 What is confirmed as-built (no action needed)

- **Server & GameLogic match §5–§6 in full.** All systems exist and behave as
  documented: intent-clamped flight shared by players and NPCs, teams/lasers/
  directional shields/missiles/spawn grace, crime→police warrants→decay→
  fugitive threshold, death/respawn flow, loot/scoop, per-station seeded
  markets, buy/sell/equip/refuel validation, the four NPC archetypes with the
  legacy tactics port, hyperspace/witchspace/in-system jump, collisions,
  256-system galaxy from seed `0xC0FFEE`, and the G8 equipment set
  (ECM/energy bomb/escape pod/laser temperature). The 13-step tick pipeline in
  `Server/GameServer.cpp` matches §5.1.
- **NeuronCore matches §3–§4 and §11 exactly.** All 14 registered catalog
  messages carry the documented ids, traits and fields; serialization caps
  (4096), `SAFE_UDP_PAYLOAD` (1200), the 58-byte `EntitySnapshot`, the 16-byte
  snapshot header, the 47-byte manifest entries, the three reliable lanes and
  their drain order, and the ECS API are all as specified.
- **The dependency rule holds.** The client executable links only
  `NeuronClient` + `NeuronCore` — it never links `GameLogic`
  (`DeepspaceOutpost/CMakeLists.txt`).
- **Testing claims hold.** 218 GameLogic tests (doc says "200+"), 117
  NeuronCore tests including the golden byte-layout, round-trip, governance
  and fuzz suites; CI builds and runs the full suite on Windows/MSVC x64
  debug + release.
- **The §13.3 performance critique is accurate.** The `i<j` O(n²) pair sweep
  in `CollisionSystem.h`, the same shape in combat scan / scoop / ECM /
  energy-bomb, per-tick scratch-vector churn, and `Spatial::Grid` being used
  only by `AreaOfInterest` were all confirmed in code.
- **None of roadmap #1–#17/#19 has any partial implementation.** No session
  token, no persistence, no `Owner`/`PlayerId`, no `UnitOrder`, no
  `KnownSystems`, no strategic tier, no delta/quantized snapshots, no time
  sync, no `FactionId`, no travel-protocol split. The codebase is exactly at
  the "Phase G complete" state the doc header claims.

### 1.2 Divergences — where the code contradicts the doc

These are ordered by severity. D1 is the headline finding of the audit.

| # | Divergence | Evidence | Fixed by |
|---|---|---|---|
| **D1** | **The client still runs game rules while connected.** §13.1-S4 claims shield regen is "the one game rule still living in the client". It is not. Running unguarded in connected play: `update_altitude()` can kill the player client-side via `do_game_over()` (`space.cpp:276-319`); `update_cabin_temp()` can kill the player *and* credits fuel locally for scooping (`space.cpp:322-370`); `random_encounter()` still rolls legacy spawns (`swat.cpp:1191-1224`); `cool_laser()`/`time_ecm()` run redundant local decay (`swat.cpp:947,403`). Worst: **`equip_do` performs equipment purchases entirely client-side** — sets `cmdr.ecm` etc. and deducts credits with no `StationRequest` (`docked.cpp:1663-1806`). | client audit §3 | Item A1 |
| **D2** | **`Equip` and `Refuel` request kinds are never sent by the client.** The server-authoritative equip/refuel paths (§6.6) exist and are tested server-side but are unreachable from the UI; the client buys equipment locally (D1) and has no refuel UI at all. | `docked.cpp` | Item A2 |
| **D3** | **Snapshot "interpolation" samples at alpha = 1.0.** §7 says the client renders "interpolated snapshots (`SnapshotInterpolator` + dead-reckoning)". The interpolator exists but `render_replicated_objects` passes a hardcoded `1.0` (`space.cpp:695,817`) — latest-tick + dead-reckoning only, no blend. | client audit §2 | Item A3 |
| **D4** | **The entire single-player Elite engine is still compiled and reachable** through the disconnected fallback (`main.cpp:1720-1744`): local AI/combat/spawning (`swat.cpp`), local hyperspace/witchspace (`space.cpp:1367-1443`), local escape pod (`main.cpp:578-643`), local sim loop `update_local_objects()` (`space.cpp:523-634`), guarded local shield regen (`main.cpp:1598-1599`), legacy missions (`missions.cpp`). §13.1-S4 / #21 understate this scope. | client audit §3 | Item A1 |
| **D5** | **Dead docked-Teleport path with contradictory rules.** `ProcessStationRequest`'s `Teleport` case (`StationServices.h:513-538`) implements a free, witchspace-less, arrive-docked teleport — unreachable in production (the server intercepts Teleport first, `GameServer.cpp:180-204`) and contradicting §6.8. Kept alive only by a test. | server audit §4 | Item A4 |
| **D6** | **`missileTarget` is not validated server-side** — a missile launches at any locked live index with no range/cone gate (`MissileSystem.h:50-93`). §13.2.2 acknowledges this; it is an anti-cheat hole today. | server audit §7 | Item G2 |
| **D7** | **`/fp:strict` is not set anywhere.** §13.3-E5 requires it on `GameLogic` and `Server` to protect golden-run determinism; all targets build with the MSVC default. | build audit §3 | Item A6 |
| **D8** | **Undocumented client-side state & heuristics:** the client keeps its own ECS mirror of ship state (`GameUniverse()`, `main.cpp:151-160`); auto-dock heuristics flip to docked client-side (`space.cpp:795-807,1546-1557`); optimistic local mutation of energy-bomb/missile counts before server confirmation (`main.cpp:981,711`). None described in §7. | client audit §8 | Items A1/A7 |
| **D9** | **Stale comment contradicting live behaviour:** `KillRewards.h:37` says witchspace bounty-withholding is "not modelled yet" — the code below it models it. | server audit §5 | Item A7 |
| **D10** | **Documentation drift:** ARCHITECTURE.md §2 omits the (real) `Tests/NeuronClient` and `Tests/NeuronServer` suites and §5.1 omits `StepEquipment` from the tick list; AGENTS.md claims the `Neuron*`/`Server` dirs are "empty placeholders", references a nonexistent `DemoShaders/` and a nonexistent `BotClient` target, and has a trailing XML artifact; `ci.yml` references the retired `MIGRATION_ROADMAP.md` and a stale feature branch; `cmo.md` falsely claims Linux CI. | all audits | Item A7 |

### 1.3 Dead code inventory

"Dead" = compiled but with no production call path (test-only callers noted).

| Location | Item | Why dead | Disposition |
|---|---|---|---|
| `GameLogic/StationServices.h:513-538` | `ProcessStationRequest` `Teleport` case | Intercepted upstream by `GameServer::HandleStationRequest`; contradicts §6.8 | **Delete** (+ its test); leave a `default:` note that travel never arrives here (until A5 removes travel from this message entirely) |
| `GameLogic/Combat.h:99-103` | `ResolveLaserHit` / `LaserHitResult` | Superseded by shield-aware `ApplyDamage`; test-only callers | **Delete** with its tests |
| `GameLogic/Combat.h:72,80-83` | `MILITARY_LASER_STRENGTH`, `LaserDamageTo` `Station`/`Armoured` branches | Only `TargetClass::Normal` is ever passed in production | **Delete** branches; keep enum only if a laser-upgrade feature is scheduled (it is not in §14 — delete) |
| `GameLogic/SnapshotBuilder.h:48-59` | `BuildWorldSnapshot` (full-world) | Server only ever sends AOI `SnapshotFor`; test-only | **Move into the test** that uses it, or delete and rewrite the test against `SnapshotFor` |
| `GameLogic/Galaxy.h` | `SystemSeed`, `NextGalaxy`, `RotateByteLeft` | Procedural galaxy uses SplitMix64, not the legacy 8-galaxy twist; test-only | **Delete** (`Waggle`/`NamePlanet` stay — they are live) |
| `NeuronCore/SnapshotReceiver.h` | whole header | Superseded by `SnapshotInterpolator` (strict superset); test-only | **Delete**; port its transport test to the interpolator |
| `NeuronCore/ClientInput.h` | `Net::ClientInput` alias | S7: one catalog name — `Msg::InputCommand` | **Delete alias**, mechanical rename at call sites (keep `NO_MISSILE_TARGET`, moved next to `InputCommand`) |
| `DeepspaceOutpost/alg_main.h`, `menu.h` | whole headers | Included nowhere | **Delete** |
| `AGENTS.md:247-248` | trailing `</content></invoke>` XML | Copy-paste artifact | **Delete** |

Not dead, do not remove: `Messages/Catalog.h`, `CatalogTools.h`,
`PacketInspect.h` (test/tooling infrastructure the governance and fuzz suites
depend on); `NetEntityId` + the `optional`/`double` leaf codecs (forward
scaffolding §4.2 documents — `NetEntityId` becomes load-bearing in Track E);
the `DebugOnly`/`Tooling` scopes and `0x0F00` band (reserved by design).

### 1.4 Legacy code inventory (client)

All of `DeepspaceOutpost/`'s legacy files are compiled; none are orphaned.
They fall into three classes with different fates:

| Class | Files (roles) | Fate |
|---|---|---|
| **Legacy game rules — remove** (Track A1) | `swat.cpp` (local AI/combat/spawn engine), local-sim parts of `space.cpp` (`update_local_objects`, local hyperspace/witchspace, `regenerate_shields`, altitude/cabin-temp kill rules), local escape pod in `main.cpp`, `missions.cpp` (single-player mission scripts; server has no mission system), local market/jump fallbacks in `trade.cpp`/`docked.cpp`, `pilot.cpp` local autopilot | Delete with the offline fallback; replace disconnected play with a connection-lost screen. Mission *content* may be mined later when #F-era missions land server-side |
| **Legacy presentation — keep, modernize incrementally** | `threed.cpp` (draw primitives), `stars.cpp`, `intro.cpp`, `shipdata.cpp`/`shipface.cpp` (mesh tables), `planet.cpp` (chart name/description text), station screens in `docked.cpp`, HUD in `space.cpp`, `file.cpp` (config), `random.cpp` (client VFX rng) | Stays; absorbed gradually by Track H (instanced renderer) and S6 (math-stack retirement) |
| **Legacy math stack — retire file-by-file** (S6) | `NeuronClient/vector.h/.cpp` (`Vector`, `Matrix[3]`) used by 13+ files **including the live thin-client render path** (`ReplicatedScene.h`, `SceneProjection.h`, `Scene3D.cpp`) | Convert the live render path to DirectXMath first (it is touched by Track H anyway); legacy screens convert as they are edited; delete `vector.h/.cpp` last |

Also legacy: the `OpenglDirectx` GL-over-D3D layer in `NeuronClient`
(explicitly frozen — do not extend; retired naturally by Track H), and the
`EventManager` remnant (only the Win32 `WNDPROC` fan-out remains; keep).

Orphaned assets: `GameData/Models/` (67 files: 33 `.obj` + 33 `.json` +
`elite.mtl`) generated by `tools/shipdata2obj` but loaded by nothing — the
runtime builds meshes from the compiled tables (`SceneMeshes.cpp:60-88`).
`cmo.md` proposes a DSOM loader for them but is referenced by nothing.
Owner decision needed (§12, Q3).

---

## 2. The plan at a glance

Work is organized into tracks. Tracks map onto the §14 roadmap and honor its
sequencing spine (**#1 → #2/3/4 → #5 → #6/7/8 → #9/10/11 → #12+**, render and
hygiene parallelizable, #20 gating entity-cap increases).

| Track | Contents | Roadmap items | Runs |
|---|---|---|---|
| **A — Truth & hygiene** | client game-rule purge, equip/refuel wiring, interpolation, dead-code deletion, protocol hygiene, `/fp:strict`, doc fixes | #19, #21 (+S1–S7 residue) | **first / parallel with anything** |
| **B — Connection & persistence** | hello-first handshake, session token, reconnect, SQL persistence | #2, #3, #4, #1 | after A5's handshake pieces |
| **C — Identity layer** | `PlayerId`, `Owner`, relational index, `AssignControl` | #5 | after B |
| **D — Performance & harness** | spatial grid everywhere, frame arena, accumulator timestep, metrics, BotClient | #6, #7, #8, #20, E8 | parallel with B/C |
| **E — Netcode depth** | time sync + lag comp, snapshot quantization/delta/budgets, strategic tier | #9, #10, #11 | after D8 metrics exist |
| **F — 4X gameplay** | ordered units, fog of war, ownership/outposts, living economy, factions | #12, #14, #15, #16, #17 | after C (and B for #14/#15/#16) |
| **G — MMO polish & deferred gameplay** | kill VFX, missile-lock validation, chat, suns & cabin heat, missions | #18 + §14 preamble | #18 early; suns/missions late |
| **H — Rendering** | instanced wireframe, iconic LOD, post chain, NetType table | #13 | any time |

---## 3. Track A — Truth & hygiene

### A1 — Purge game rules from the client (extends S4 / #21) — **M** — ✅ **done 2026-07-03**

*As implemented (details in the numbered spec below): `swat.cpp`, `pilot.cpp`
and `missions.cpp` are deleted along with the whole offline fallback; the
unguarded connected-mode rules (client-side altitude/cabin-temp deaths,
`random_encounter`, local shield regen, local equipment/market mutation) are
gone; a disconnected client shows a CONNECTION LOST screen with automatic
retry (`ensure_connection`); docking is request-based — the client flips
docked only on `StationResponse{Dock, Ok}`; the hyperspace key now sends
`Teleport` from either chart in flight or docked (the legacy local
countdown/witchspace jump died with the fallback); the witchspace flag and
the ECM indicator are mirrored from server responses/events. Salvaged as
presentation into `space.cpp`: the display-object pool (intro parade,
game-over debris), the laser-beam visual (no heat/energy mutation — the
server's `laserTemp` gates the trigger), the weapon-HUD state, and a
display-only altitude dial. Known residues, accepted and documented:
(a) the energy-bomb flag is still cleared optimistically on use — no wire
message mirrors equipment consumption yet (PlayerStatus carries no equipment
bits); a G-track item adds the authoritative equipment mirror;
(b) `generate_stock_market` remains as the market screen's display baseline
(the server does not replicate per-station price/stock rows yet — A5's
successor protocol or F4's market messages close that);
(c) `GameWindows.cpp`'s now-uncalled `OpenPlanetDataWindow` is left for a
NeuronClient UI sweep.*

The single most important correction: make the load-bearing rule literally
true. §13.1-S4 scoped this as deleting shield regen; the audit shows the real
scope is the whole offline fallback plus five unguarded connected-mode rules.

1. **Delete the unguarded connected-mode rules** (`main.cpp:1609-1619`):
   `update_altitude()` and `update_cabin_temp()` calls (client-side
   `do_game_over()` must never exist — death arrives only via `EntityDeath`),
   `random_encounter()`, `cool_laser()`, `time_ecm()`. Keep any pure
   *display* computation they fed (altitude/temp HUD bars) by recomputing
   from replicated positions only — display, never consequence. (Cabin-temp
   HUD returns for real with G4's server-side suns.)
2. **Delete the offline single-player fallback**: the `!IsOpen()` degraded
   path (`main.cpp:1720-1744`, `1558-1561`), `update_local_objects()` and the
   local sim in `space.cpp` (`:523-634`), local hyperspace/witchspace
   (`space.cpp:1367-1504` legacy branches), local escape sequence
   (`main.cpp:578-643`), `regenerate_shields()` and its guard, and all of
   `swat.cpp`'s tactics/spawn/fire logic. A failed connection now shows a
   **connection-lost / retry screen**, not simulated vitals.
3. **Delete `missions.cpp`** and its `check_mission_brief` hook
   (`main.cpp:1153`) — missions are deferred until after persistence (§12)
   and will be server-authoritative when they come.
4. **Remove optimistic local mutations** (D8): energy-bomb/missile local
   decrements — the HUD mirrors `PlayerStatus` only. Remove the client-side
   auto-dock state flip: the proximity heuristic may *send* a Dock
   `StationRequest`, but the docked flow starts only on
   `StationResponse{Dock, Ok}`.
5. **Prune what this orphans**: after 1–4, sweep `swat.cpp`, `pilot.cpp`,
   `trade.cpp`, `planet.cpp` for now-unreferenced functions; delete
   `swat.cpp`/`pilot.cpp` outright if (as expected) nothing survives except
   HUD/text helpers, which move next to their callers.

*Acceptance:* the client contains no code path that mutates
credits/fuel/cargo/energy/shields/wanted or ends the player's life; grep for
`do_game_over`, `regenerate_shields`, `tactics(` returns nothing;
disconnected client shows the connection-lost state. All remaining
`DeepspaceOutpost` code is input capture, request sending, replicated-state
rendering, HUD, audio, and screens.

### A2 — Wire Equip and Refuel through the server (closes D1/D2) — **S** — ✅ **done 2026-07-03**

As implemented, in thin-client mode `equip_do` (`docked.cpp`) sends a
station request instead of mutating `cmdr`: Fuel → `StationRequest{Refuel}`;
the six server-modeled items (Missile/LargeCargoBay/Ecm/FuelScoop/
EnergyBomb/EscapePod) → `StationRequest{Equip, commodity=EquipItem}` via a
new `server_equip_item` mapping helper. Items the server does not model
(lasers, extra energy unit, docking computer, galactic hyperdrive) are not
purchasable while connected (they belong to the retired single-player tier;
the offline branch still handles them until A1 removes it). Ownership is
applied only from the reply: the `StationResponse` handler (`main.cpp`) now
mirrors an `Equip` Ok result to the matching `cmdr` flag/capacity, and
**the cargo-slot write is guarded to Buy/Sell** (an Equip response reuses
`commodity` for the EquipItem id 1–6, which previously would have clobbered
a cargo hold of the same index — a latent bug this fixes). `PlayerStatus`
now also mirrors `missiles` (server-owned rack count).

*Acceptance:* equipment ownership, fuel and missiles change only after a
`StationResponse{Ok}` / `PlayerStatus` round-trip; a rejected purchase
changes nothing locally. (Client-side visual behaviour is not covered by
the headless suites — validated by the CI build + manual thin-client run.)

### A3 — Real snapshot interpolation (closes D3) — **S** — ✅ **done 2026-07-03**

As implemented: `ReplicationClient::Pump` timestamps each snapshot tick as it
first appears and measures the interval between the last two; a new pure,
unit-tested `Net::InterpolationAlpha(now, arrival, interval)` (in
`SnapshotInterpolator.h`) turns that into a render alpha that walks 0→1 over
one interval, so the client renders ~one snapshot interval in the past and
tweens prev→curr (the interpolator already blended given an alpha — the
client just always passed 1.0). `render_replicated_objects` samples both the
entities and the floating-origin local player at that single per-frame alpha,
keeping the frame coherent. Targeting/logic queries that legitimately want
the freshest tick (`find_lock_target`, `chart_current_system`, the
death-VFX position capture) stay at 1.0. The measured interval is the
`interpDelay` input Track E1's lag compensation will need server-side.

*Acceptance:* NeuronCore test asserts alpha walks 0 → 0.5 → 1 across one
interval and clamps outside it (`InterpolationTests.cpp`); the existing
prev→curr position-lerp tests already cover the blend. Smooth motion at
display rate is validated by the CI build + manual run.

### A4 — Delete dead code (per §1.3 table) — **S** — ✅ **done 2026-07-03**

Mechanical, per the §1.3 table. As implemented: the docked-Teleport case is
gone (its tests replaced by one pinning "travel kinds are rejected by the
station dispatcher"); `ResolveLaserHit`/`LaserHitResult`/`TargetClass`/
`MILITARY_LASER_STRENGTH` deleted with damage now taken directly from laser
strength; `BuildWorldSnapshot` moved into `ReplicationTests.cpp`;
`SystemSeed`/`NextGalaxy`/`RotateByteLeft` deleted; `SnapshotReceiver.h`
deleted with its transport tests ported to `SnapshotInterpolator`;
`ClientInput.h` deleted with all call sites renamed to `Msg::InputCommand` /
`Msg::NO_MISSILE_TARGET`; `alg_main.h`/`menu.h` deleted. (The legacy
`MILITARY_LASER` in `DeepspaceOutpost/` legacy screens remains — that tier is
A1's scope.)

### A5 — Protocol hygiene (#19: S2 + S3 + S7 remnants) — **M**

All three follow the ABI rule: new id, retire the old id in place.

1. **S3 — Travel split.** New catalog messages in the game-specific band:
   `TravelRequest` (`0x1000`, Wire/Command/Gameplay/C→S:
   `kind u8 {Hyperspace=1, InSystemJump=2}`, `systemId u32`) and
   `TravelResponse` (`0x1001`, Wire/Event/Gameplay/S→C:
   `kind u8`, `status u8 {Ok, NotEnoughFuel, OutOfRange, UnknownSystem,
   MassLocked, Arrived, Witchspace}`). Client chart/jump keys send these;
   `GameServer::HandleStationRequest` loses its Teleport/JumpDrive
   interception; `StationRequestKind::Teleport/JumpDrive` and the travel
   values of `StationStatus` are marked retired (kept in the enum,
   rejected if received). §4.4/§11 tables updated.
2. **S2 — Manifest re-cut, request-driven.** New catalog messages:
   `GalaxyChunkRequest` (`0x1002`, Wire/Command/Bulk/C→S:
   `baseIndex u32`, `count u16`) and `GalaxyChunk` (`0x1003`,
   Wire/Event/Bulk/S→C: `total u32`, `baseIndex u32`,
   `systems vector<SystemEntry>` where `SystemEntry.Fields()` carries
   id/x/y/z/name-string/government/economy/techLevel/population/
   productivity through the generic codec). Client pulls chunks after the
   handshake instead of receiving a connect-time fire-hose; server clamps
   `count` so replies fit `SAFE_UDP_PAYLOAD`. Retire hand-coded `0x0210`
   and `NeuronCore/GalaxyManifest.h` once the client no longer speaks it.
   This is also the prerequisite for fog of war (F2): the server will later
   filter chunk replies by `KnownSystems`.
3. **S7 — Band note + alias.** ✅ done 2026-07-03: the §4.3 note
   grandfathering `EcmPulse`/`EscapePodUsed` is in ARCHITECTURE.md (future
   gameplay events allocate from `0x1000+`), and the `Net::ClientInput`
   alias is deleted (with A4).

*Acceptance:* round-trip + golden-layout tests for the four new messages;
governance tests pass; a legacy `StationRequest{Teleport}` is answered with a
rejection, not a jump; the client completes its chart using pulled chunks.

### A6 — Determinism & build hygiene — **XS** — ✅ **done 2026-07-03**

`/fp:strict` is now a PUBLIC compile option on `GameLogic`, so `Server` and
`GameLogic.Tests` inherit it transitively (§13.3-E5); CI's golden-test run
validates no expectation depended on contraction. The `NEURONCORE_HEADERS`
manifest gained the missing `GalaxyManifest.h`,
`Messages/Defs/PlayerSession.h` and `Messages/Defs/EquipmentEvents.h`, and
dropped the deleted `ClientInput.h`/`SnapshotReceiver.h`.

### A7 — Documentation truth pass — **XS** — ✅ **done 2026-07-03**
*(as itemized below; the client-ECS §7 note waits on A1's outcome, and the
§13 status fold-in is this document)*

- ARCHITECTURE.md: add `StepEquipment` to the §5.1 tick list; list all four
  test suites in §2; document the client's ECS-as-HUD-mirror in §7 (or
  remove it in A1 if it reduces to plain structs); fold this plan's §1.2
  findings into §13 status.
- Fix the `KillRewards.h:37` comment (D9).
- AGENTS.md: delete the stale "empty placeholders" status note, the
  `DemoShaders/` references, the `BotClient` row (until D5 creates it), and
  the trailing XML artifact.
- `ci.yml`: drop the `MIGRATION_ROADMAP.md` comment and the stale branch
  trigger.
- `cmo.md`: left untouched per the owner's Q3 decision (§12) — the owner
  covers the DSOM/Models question separately.

---

## 4. Track B — Connection, identity, persistence

### B1 — `ClientHello`-first handshake (#2 / S1) — **S**

Invert the front door (`ServerSessions.h:66-83`): unknown endpoints sending
anything but a valid Control-lane `ClientHello{protocolVersion == PROTOCOL_VERSION}`
are **ignored** (no allocation, no reply). On a valid hello: provision the
session, spawn the player, reply `HelloAck` (new Control message `0x0003`:
`sessionToken u64`, `entityId u32`, `protocolVersion u16`) — which subsumes
and retires `AssignPlayer 0x0001`. The `Commander-<n>` placeholder-name path
dies (the hello always carries the name). Version-mismatch hellos get a
one-datagram `HelloReject` (`0x0004`: `reason u8`) and no state.

*Acceptance:* a raw `InputCommand` from an unknown endpoint provokes zero
reply traffic (amplification test asserts 0 bytes); the §4.6 connect sequence
in ARCHITECTURE.md is rewritten to hello-first; session tests updated.

### B2 — Session token: endpoint ≠ identity (#3) — **S**

- `HelloAck.sessionToken` is a random 64-bit token from an OS CSPRNG
  (`BCryptGenRandom` — *not* a gameplay LCG stream; this is security, not
  simulation, and determinism rules don't apply outside GameLogic).
- Every subsequent client datagram carries the token: `'NMSG'` and `'NRLB'`
  headers gain a `token u64` after the lane byte (framing version bump —
  PROTOCOL_VERSION 2 — inside the B1 handshake window, so no dual-decode).
  Mismatched or missing token ⇒ datagram dropped before any decode.
- Sessions become keyed by token; the endpoint is just the current return
  address, updated when a correctly-tokened datagram arrives from a new
  endpoint (NAT rebind heals silently).
- Per-endpoint rate limiting at the same choke point: token-less datagrams
  per endpoint per second capped (small constant); over-cap endpoints are
  muted for a cooldown. Counters exposed for D4's metrics.

*Acceptance:* spoofed-endpoint `StationRequest` with a wrong token does
nothing; endpoint change mid-session keeps the entity; fuzz tests cover
truncated/garbage token fields.

### B3 — Reconnect grace & resume (#4) — **S**

On session silence, keep the entity alive but **safe-parked** (intent
zeroed, autoEngage protections unchanged) for a grace window (60 s ≈ 1800
ticks, replacing the hard 300-tick reap for *authenticated* sessions). A
`ClientHello` carrying a known token re-binds to the new endpoint and
resumes: resend `HelloAck`, roster, `PlayerStatus`, `CargoManifest`, and
(post-A5) the client re-pulls manifest chunks. After the grace window the
reap proceeds as today.

*Acceptance:* kill the client socket, reconnect within grace from a new
port → same ship, same cargo; after grace → normal despawn.

### B4 — Persistence on SQL Server (#1) — **L**

The top structural gap. Design points, honoring §12 (async batched writes
off the sim thread, never per-tick positions, world simulates while players
are offline):

- **Schema v1:** `accounts` (account id, commander name, auth secret hash —
  even if auth is just token-continuity at first), `players` (score, credits,
  fuel, wanted, equipment flags, hold capacity, last station/system),
  `player_cargo` (player × commodity → units), `stations_market`
  (station × commodity → stock/price — becomes load-bearing with F4's
  drifting markets; until then rows are seeded lazily from `GenerateMarket`),
  `world_meta` (galaxy seed, schema version), `command_log`
  (append-only: tick, player, message id, payload blob) for audit/replay.
  Design the key space assuming **empires** arrive in Track C (player rows
  key by `PlayerId`, not entity index).
- **Write path:** GameLogic marks components dirty via the existing
  serializable-component invariant (`Wallet`, `CargoHold`, `Fuel`, `Wanted`,
  `PlayerRecord`, `Equipment`); a persistence layer in **NeuronServer**
  (this is exactly the library's chartered role — it currently contains only
  `DatagramPump`/`OnChangeCache`) snapshots dirty rows at a low cadence
  (every few seconds + on logout/despawn) into a queue drained by a writer
  thread using ODBC (`SQLDriverConnect` — native-first, no ORM). The sim
  thread never blocks on the DB.
- **Read path:** load-on-hello (B1 gives the single choke point): known
  commander name/account resumes its row set; unknown creates one. Spawn
  location = persisted last station.
- **Testing:** the persistence layer is written against an interface whose
  production implementation is ODBC and whose test implementation is
  in-memory, so round-trip/dirty-tracking/replay-from-command-log tests stay
  headless in `Tests/NeuronServer`. CI does not get a SQL Server dependency.

*Acceptance:* stop/restart the server → commanders keep credits, cargo,
fuel, equipment, wanted, score, and wake at their last station; command log
replays a session's station requests to identical wallet outcomes.

---

## 5. Track C — Identity layer (#5) — **M**

§12 locks "Account → Empire → owns N entities"; the as-built protocol
re-concretized single-avatar. Do this before any F-track feature.

- **`PlayerId`** (`u32`, allocated per account by the server, persisted by
  B4). Sessions own a `PlayerId`; name/score/wallet move off the hull:
  `PlayerRecord` splits into a per-player record map (keyed by `PlayerId`,
  in `ServerSessions` today, `players` table with B4) and the hull keeps
  only gameplay components.
- **`Owner{playerId}`** component on every player-owned entity (today:
  exactly the one ship), maintained through a **relational index** in the
  ECS: a hash multimap `PlayerId → dense entity list` updated on
  add/remove (E3's "all my units is O(mine)"). Implemented as a small
  `OwnershipIndex` beside the Registry (NeuronCore), unit-tested for
  add/remove/destroy/generation-recycle correctness.
- **Wire:** new Control message `AssignControl` (`0x0005`:
  `playerId u32`, `primaryEntityId u32`) folded into the B1 `HelloAck`
  reply sequence (HelloAck can simply gain the `playerId` field if C lands
  with B; otherwise allocate `0x0005` and retire nothing). `PlayerInfo`
  gains `playerId` (successor id `0x0304`, retiring `0x0301`) so rosters
  key by player, not hull — required the moment one player owns two hulls.
- **Behaviour freeze:** after C lands, gameplay is *identical* — one owned
  ship per player — but every query that used "the session's entity" goes
  through `PlayerId`/`Owner`.

*Acceptance:* kill/respawn keeps `PlayerId` stable across the entity swap;
an integration test gives one player a second owned entity and asserts
roster, status routing, and the ownership index all behave; no gameplay
regression in the existing 218 tests.

---

## 6. Track D — Performance & the load harness

### D1 — Spatial grid into every pairwise loop (#6 / E1) — **M**

Convert one system at a time, golden-testing before/after (same seed, same
world ⇒ identical outcomes — the conversions must be behaviour-preserving):

1. Maintain a per-tick broadphase `Spatial::Grid` (cell ≥ 16 384 to serve
   the largest ranges with ±1-cell queries; AOI keeps its own 100 000 grid).
2. `StepCollisions`: replace the `i<j` sweep with per-cell candidate pairs +
   exact Chebyshev (ship/ship 600, station 1000, planet 4000).
3. `StepCombat` target scans and `ResolvePlayerFire` (6000),
   `ScoopSystem` (600), `ActivateEcm` (12 000), `DetonateEnergyBomb`
   (16 384): all become grid queries.
4. Keep iteration order deterministic: gather candidates, sort by entity
   index, then apply — never let hash-map order leak into outcomes.

### D2 — Frame arena / scratch reuse (#7 / E2) — **S**

Persistent per-system scratch buffers with `clear()`-not-free semantics for
the per-tick vectors confirmed in the audit (`CollisionSystem`, `StepCombat`,
`StepAi`, `StepMissiles`, `StepLoot`, `SnapshotHelpers`), plus reuse of
snapshot build and reliable-channel resend buffers. Simplest shape: a
`FrameScratch` struct owned by `GameServer`, passed down by reference —
no globals, no allocator cleverness until profiling demands it.

### D3 — Accumulator fixed timestep + tick metrics (#8 / S5, E8) — **S**

Replace `Sleep(33)` (`Server/Main.cpp:37`) with a QPC accumulator: run
catch-up ticks when behind (capped, e.g. 5, then declare overrun), sleep the
remainder when ahead. Add the always-on counters E8 lists: tick-duration
histogram + overrun count, bytes/session/s per lane, entities per AOI
snapshot, broadphase candidate-pair counts (validates D1), reliable resend
rates, per-phase tick-time breakdown. Expose as a periodic console line +
a queryable struct (the BotClient harness reads it).

### D4 — Rate/cadence guards — **XS**

With B2's rate limiting in place, add the §9 "future" input-cadence sanity:
per-session `InputCommand` acceptance cap per tick window (latest-wins
already bounds damage; this bounds the CPU).

### D5 — BotClient harness → 100-player load test (#20) — **M**

New console target `BotClient/` (the AGENTS.md table finally becomes true):
links NeuronClient headless (no D3D/audio init) + NeuronCore, drives N
scripted sessions over the real UDP stack — hello/handshake (B1/B2), flight
intent orbits, fire bursts, station dock/trade cycles, hyperspace jumps.
Emits per-bot RTT/loss and reads the server's D3 metrics. CI gets a smoke
lane (server + 8 bots, 30 s, asserts zero overruns and zero desyncs);
the 100-bot soak is a manual/perf-lab run that gates every entity-cap
increase (§14).

---

## 7. Track E — Netcode depth

### E1 — Time sync → lag-compensated fire (#9) — **M**

- Control-lane `Ping` (`0x0006`, C→S: `clientTimeMs u32`) / `Pong`
  (`0x0007`, S→C: `clientTimeMs u32`, `serverTick u32`) exchanged ~1 Hz;
  client keeps an RTT/offset estimate (smoothed), server stores per-session
  RTT.
- Server keeps a ring buffer of the last 15 ticks of
  `WorldTransform` (position + nose) per combat-relevant entity;
  `ResolvePlayerFire` and the A5 travel/missile validations test cones and
  ranges against the world at `now − RTT/2 − interpDelay` (A3 defines
  interpDelay). Derived state only — determinism unaffected.

*Acceptance:* headless test: target moving laterally at 100 ms simulated
RTT is hittable when aimed at its rendered (delayed) position.

### E2 — Snapshot quantization + delta + budgets (#10 / E4) — **M–L**

Successor snapshot format (version 2 of the `'NSNP'` header, negotiated by
protocol version at hello):

- Packet header carries a reference cell; positions become 3×i32
  cell-relative offsets (exact, no float loss).
- Orientation: smallest-three quantized quaternion (or oct-encoded nose +
  roll byte) replacing the 24-byte dual basis — client derives the frame.
- `speed` as u16 fixed-point. Target ≤ 20 bytes/entity before delta.
- **Server-side rounding** so every client sees identical values.
- Per-session delta against a last-acked baseline with periodic keyframes;
  baseline acks piggyback on the existing reliable-lane ack traffic;
  snapshots stay unreliable.
- Per-lane byte budgets per session per tick; the packetizer drops
  lowest-priority entities first (distance-sorted) and reports drops to D3
  metrics. Snapshot send rate becomes an explicit constant.

*Acceptance:* golden tests for the quantizer (server-rounded exactness),
soak test via D5 comparing bandwidth before/after (expect ≥ 60 % reduction
at 12 NPCs, far more at fleet counts), loss-recovery test (dropped baseline
⇒ keyframe resync).

### E3 — Strategic AOI tier (#11) — **M**

New reliable Gameplay-lane message `StrategicSummary` (`0x1004`, S→C):
`systemId u32`, `friendlyCount u16`, `hostileCount u16`, `alert u8`
(none/underAttack/lost), sent at 0.5–1 Hz per *known* system in which the
player has presence or (post-F3) property. Server aggregates per system
from the ownership index (C) at strategic cadence — this realizes §12's
decoupled clocks. Client renders it on the chart screen; the iconic-LOD
glyphs (H) reuse the same data shape.

---

## 8. Track F — 4X gameplay

All items follow the doc's "indirect control through the existing intent
pipeline" thesis. C (identity) is a hard prerequisite; B4 (persistence) is
required by F2–F4 to be meaningful.

### F1 — First ordered unit: the escort (#12) — **M**

- Purchase: new `EquipItem::EscortFighter` through the *existing* Equip flow
  → server spawns a Viper-hulled NPC with `Owner{you}`, Team Player,
  `AiPilot` whose target source is an `EscortOrder` component, price steep
  (e.g. 5000.0 Cr).
- Orders: `UnitOrder` (`0x1010`, Wire/Command/Gameplay/C→S: `unitId u32`,
  `order u8 {Escort=1, Attack=2, Dock=3}`, `target u32`), validated:
  unit is live (`LiveEntity`), `Owner` == sender's `PlayerId`, target legal
  for the order (Attack respects the crime rules — an ordered attack on a
  protected victim is the *owner's* crime, publishing `Crime` against the
  owner). Rejections via `UnitOrderAck` (`0x1011`: `unitId`, `status u8`).
- AI: an `Escort` order sets the pilot's waypoint to the owner's ship with
  the existing trader-autopilot follow; `Attack` sets `focus`. No new
  steering — only a new order source, exactly as §13.2.3-2 promises.
- Escorts replicate, fight, die and drop loot through every existing path;
  they persist (B4) as owned entities.

### F2 — Fog of war (#14) — **M**

`KnownSystems` bitset component (256 bits) per `PlayerId`, persisted (B4).
Known by: starting system, physically visiting (hyperspace arrival), buying
charts at high-tech stations (new station transaction — an economy sink),
or an owned unit arriving (F1 scouts). A5's pull-based `GalaxyChunkRequest`
now filters: the server sends only known systems' entries (unknown ids get
a stub entry with position only, or are omitted — chart shows unexplored
space). The strategic tier (E3) filters by it. Witchspace drops you in deep
space you may not know — that is the feature working.

### F3 — Ownership & territory (#15) — **L**

Stations gain `Owner{playerId}` (nullable = NPC-owned). Claiming = a
validated station transaction (charter purchase at an unowned station:
large credit sink, `StationResponse{AlreadyOwned}` guarding). Privileges
v1: a fee share on every trade executed at the owned station (credits
trickle on B4 cadence) and a docking whitelist toggle. Deployable outpost:
`EquipItem::OutpostKit` occupies hold tonnage; a deploy command (reuse
`UnitOrder{Deploy}` or a `0x1012` message) validates clear space and spawns
a structure entity — new `NetType` (allocate the next legacy-free value),
replicated/persisted/rendered by existing paths, with a small market
generated on deploy. Influence radii stay *emergent* (strategic-tier
counts) — no map-painting system, per the doc.

### F4 — Living economy (#16) — **L**

- Market state (per station × commodity stock/price) becomes mutable rows
  (B4) initialized from `GenerateMarket` and **drifting back toward** that
  baseline at strategic cadence.
- Player trades already move stock; now ambient traders do too: a trader
  despawn-docking at its destination (`AiSystem.h:360-363`) delivers its
  manifest — stock up, price down; its origin decremented at launch. Piracy
  (killing the trader) now causes real scarcity.
- Owned haulers: `UnitOrder{Route}` with a two-station route reuses the
  trade-lane autopilot; the hauler executes buy-low/sell-high automatically
  within its owner's wallet.
- Per-empire flow pass (strategic cadence): diff supply/demand across the
  player's known/owned stations, emit greedy hauler orders — policy in,
  routing out.

### F5 — Factions & standings (#17) — **M**

Keep `Team` for NPC archetype discipline. Add server-issued `FactionId`
(u32) + a standings table (faction × faction → Peace/War). "Protected
victim" (§6.3) generalizes: firing on a clean player *not at war with you*
is a crime; declared wars (a faction-leader station transaction, mutual
consent or cooldown-gated) suspend wanted consequences between
belligerents only. Police discipline is untouched for everyone else.
Wire: `PlayerInfo` successor already carries `playerId` (C); add
`factionId` to it and a `FactionInfo` message (`0x1013`) for names/standings.

---

## 9. Track G — MMO polish & deferred gameplay

### G1 — Kill VFX broadcast (#18a) — **XS**

New `ExplosionAt` (`0x1005`, Wire/Event/Gameplay/S→C broadcast:
`x,y,z i64`, `scale u8`) published on *player* deaths (NPC deaths already
broadcast `EntityDeath`). The killer finally sees the kill. Client plays
the existing debris VFX world-anchored.

### G2 — Missile-lock validation (#18b, closes D6) — **XS**

At launch, validate `missileTarget` with the same gates the laser has:
live entity (`LiveEntity`), within a lock range (6000) and a generous
forward cone at *lock time*; otherwise the launch is refused silently
(missile not spent). Headless tests for spoofed indices.

### G3 — Chat (#18c, §14 preamble) — **S–M**

Server: relay `Chat{sender, text}` (already registered, `0x0300`) from a
session to AOI-plus-roster recipients, with server-side rate limit
(N lines / 10 s, drop + warn) and length re-validation. Client: a chat line
input (GameWindows overlay), a scrollback of the last ~8 lines over the
HUD, and a client-side **mute list** (by `PlayerId` — designed in from day
one per §13.2.2, persisted in the local config file).

### G4 — Suns & cabin heat (§14 preamble; deferred G8+ payoff) — **M**

Server-side: each system gains a sun entity (`NetType −2` already reserved,
rendered today by the billboard path) placed by `GalaxyGen`; a
`StepCabinHeat` system heats ships by proximity (Chebyshev band inside the
D1 grid), damaging then killing at sustained maximum (the legacy
zero-altitude analogue), and **fuel-scooping while hot**: a fuel-scoop-
equipped ship skimming the band gains fuel per tick (finally paying off the
525 Cr scoop beyond cargo). Client: cabin-temp HUD bar returns as a pure
mirror (`PlayerStatus` gains `cabinTemp` — successor id per ABI rules or a
reserved field if PlayerStatus is re-cut in the same release), scoop audio
cue. This restores the *display* that A1 deleted, now backed by authority.

### G5 — Missions (§12: after persistence) — **L, last**

Server-authoritative mission system: offer/accept/complete as station
transactions, state persisted per `PlayerId` (B4), content mined from the
legacy `missions.cpp` scripts (couriers, assassinations, rare-goods runs).
Out of scope for this plan beyond the placeholder — design doc first, per
the ARCHITECTURE.md change-control rule.

---

## 10. Track H — Rendering (#13) — **M**, parallel any time

Per §13.2.1, in order:

1. **NetType indirection table** first (it is the seam everything else
   plugs into): a client-side table `NetType → {mesh id, glyph id, palette
   row}` replacing the `if/switch` in `draw_ship`/`build_ship_mesh`
   (`threed.cpp:446-484`, `SceneMeshes.cpp:27-89`). Adding a hull (F3's
   outpost) becomes a data row.
2. **Batched instanced wireframe:** one persistent line-list vertex buffer
   per hull type, one per-frame instance buffer (transform + palette tint),
   one `DrawIndexedInstanced` per hull type. This work converts
   `ReplicatedScene`/`SceneProjection`/`Scene3D` to DirectXMath as it goes
   — the S6 math-stack retirement rides this track for the live path.
3. **Client-side culling + iconic LOD:** grid/range cull (client may reuse
   `Spatial::Grid`), and beyond a range threshold draw the 2–6-line glyph
   from the NetType table instead of the mesh — the tactical-digital look
   *and* the LOD strategy; also the render path for E3's strategic
   contacts.
4. **Post chain:** lines to an emissive target, blur, composite; screen-
   space quad expansion in the vertex shader (4 verts/segment via
   `SV_VertexID`) so line weight is a style parameter. GPU-side explosion
   debris seeded by `EntityDeath` (E6: client GPU only — the server stays
   headless).

*Acceptance:* draw calls O(hull types) at any entity count (D5's 100-bot
soak doubles as the render stress scene); legacy screens (charts, station
UI) unaffected; `vector.h`/`vector.cpp` deleted when the last legacy screen
converts (S6 complete).

---

## 11. New message-id allocation (summary)

Per §4.3 bands; all proposals — confirm against governance tests at
implementation time. Control band: `HelloAck 0x0003`, `HelloReject 0x0004`,
`AssignControl 0x0005` (or folded into HelloAck), `Ping 0x0006`,
`Pong 0x0007`. Game band: `TravelRequest 0x1000`, `TravelResponse 0x1001`,
`GalaxyChunkRequest 0x1002`, `GalaxyChunk 0x1003`,
`StrategicSummary 0x1004`, `ExplosionAt 0x1005`, `UnitOrder 0x1010`,
`UnitOrderAck 0x1011`, `Deploy 0x1012` (if not folded into UnitOrder),
`FactionInfo 0x1013`. Identity band: `PlayerInfo` successor `0x0304`.
Retired in place: `AssignPlayer 0x0001` (→HelloAck), `0x0210` manifest
(→GalaxyChunk), `PlayerInfo 0x0301` (→0x0304), `StationRequestKind::
Teleport/JumpDrive` + travel `StationStatus` values (→Travel*).

## 12. Open decisions for the owner

Everything above follows decisions already locked in ARCHITECTURE.md §12.
Three items are genuinely open and block only their own bullets:

1. **Q1 — Offline mode.** A1 deletes single-player entirely (S4's spirit,
   extended to the whole fallback). If any offline/practice mode is ever
   wanted, the correct shape is a *locally hosted server process*, never
   client-side rules. Assumed: delete.
2. **Q2 — Legacy save files.** `file.cpp` still reads/writes local
   commander saves; B4 makes the server authoritative. Assumed: local saves
   die with B4 (config/keybinds stay local).
3. **Q3 — `GameData/Models` + `cmo.md` (DSOM).** *Decided 2026-07-03:
   deferred — the owner will handle this separately. Leave `GameData/Models`,
   `tools/shipdata2obj` and `cmo.md` exactly as they are; no track touches
   them (the A7 doc pass leaves `cmo.md` alone too). Track H proceeds from
   the compiled mesh tables.*
4. **Q4 — x86.** Presets exist, CI never builds them, nothing in the plan
   needs x86. Assumed: drop the presets or add a CI lane; default drop.

## 13. Suggested milestone cut

1. **M1 "Honest client"** — A1–A7 complete. The load-bearing rule is
   literally true; protocol hygiene done; docs match code.
2. **M2 "Durable world"** — B1–B4. Secure sessions, reconnect, SQL
   persistence. Restart-safe commanders.
3. **M3 "Empire-ready core"** — C + D1–D5. Identity layer, grid, arena,
   accumulator, metrics, BotClient smoke in CI.
4. **M4 "Fair & scalable netcode"** — E1–E3 (validated by the 100-bot
   soak) + G1–G3.
5. **M5 "The 4X turn"** — F1–F5, G4, with H landing in parallel.
6. **M6 "Missions"** — G5, after M2 has soaked in production.
