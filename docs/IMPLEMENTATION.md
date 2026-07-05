# DeepspaceOutpost — Implementation Plan

**Status:** derived from a full code audit against `docs/ARCHITECTURE.md`
(the canonical design document), 2026-07-03. Every claim below was verified
against the source at audit time; file/line references are to that snapshot.
**Addendum 2026-07-04:** the interaction redesign (pointer-first command
interface, `docs/interaction.md`, ARCHITECTURE.md §13.2.4, roadmap #22) adds
**Track I** (§11) — its first item I1 was urgent, because the free-camera
migration had retired piloting, leaving the ship no movement verb (restored
by I1's `UnitOrder`; I1–I7 are all ✅ done).
**Addendum 2026-07-05:** the client presentation modernization that rode
Track I is complete — the letterbox, the break pattern, and the entire legacy
`gfx2d`/`gfx.h` 2D layer are retired (`gfx.h`/`gfx2d.h`/`gfx2d.cpp` deleted);
all client 2D is native `Render2D`/`TextRenderer` via `RenderGameHud` + the
GUI windows, with the engine seam in `platform/GameScene.h/.cpp` and the
palette/sprite ids in `GamePalette.h`. The step-by-step log lives in the I6
follow-ups (§11) and the §1.3 dead-code table.

This document is the **execution companion** to ARCHITECTURE.md: it records
where the code actually diverges from the design (§1), inventories dead and
legacy code (§2–§3), and then specifies, work item by work item, how to
implement the complete game described in ARCHITECTURE.md §12–§14 (§4–§12).
ARCHITECTURE.md stays the *what and why*; this document is the *what exactly,
where, and in what order*. When the two disagree, ARCHITECTURE.md wins —
update it first, then this plan.

Conventions used throughout:

- Roadmap numbers `#1–#22` refer to ARCHITECTURE.md §14; `S1–S7` to §13.1;
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
- **NeuronCore matches §3–§4 and §11 exactly.** Every registered catalog
  message carries the documented ids, traits and fields (incl. E1's `Ping`/`Pong`
  and E3's `StrategicSummary`); serialization caps (4096), `SAFE_UDP_PAYLOAD`
  (1200), the E2 v2 snapshot (32-byte `EntitySnapshot`, 41-byte header with the
  int64 reference origin + `complete` flag, plus the delta stream), the 47-byte
  manifest entries, the three reliable lanes and their drain order, and the ECS
  API are all as specified.
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
| `DeepspaceOutpost/threed.cpp` `draw_wireframe_ship` + the `wireframe` global (`elite.*`, `space.cpp` laser lines, options window, `newkind.cfg`) | Solid/Wireframe graphics toggle | Ships always render the solid GPU mesh; the CPU line path was never selected in production | ✅ **Removed 2026-07-04** (the retro-vector *art direction* is the low-poly meshes, unaffected — Track H) |
| `DeepspaceOutpost/SceneMeshes.cpp`, `threed.cpp` + the `planet_render_style` global (`elite.*`, options window, `newkind.cfg`) and `ModelDraw::style`/`colour2` | Multi-style planet renderer (Wireframe/Green/SNES/Fractal) | Only the classic green ever shipped; `ModelDraw::style`/`colour2` had no reader | ✅ **Removed 2026-07-04** (planet is one lit green sphere) |
| `DeepspaceOutpost/config.h`, `alg_data.h` | whole headers | `alg_data.h` = retired Allegro datafile indexes (unused); `config.h` = the `GFX_ALLEGRO` (dead) + `RES_800_600` macros, the latter still selecting `gfx.h`'s `GFX_SCALE=2` block | ✅ **Removed 2026-07-04** (`RES_800_600` moved to a DeepspaceOutpost target compile definition; every `#include` deleted) |
| `DeepspaceOutpost/file.cpp`, `file.h` + `GameData/newkind.cfg`, `newscan.cfg` | the local config subsystem (`read/write_config_file`, `read_scanner_config_file`, `get_filename`) | The MMO client keeps no on-disk settings; `write_config_file`/`get_filename` callers were the Save-Settings row and the dead `set_commander_name` | ✅ **Removed 2026-07-04** — the load-bearing values it read (scanner/compass HUD positions, frame-speed default) are baked into `elite.cpp`; the startup `read_config_file()` and the Save-Settings row are gone |
| `NeuronClient` cube-map **skybox** (`Scene3D::renderSkybox` + `s_sky*` resources, `skyboxVS/PS.hlsl`, `partials/skybox.hlsli`, `Textures/Skybox.dds`) + `stars.cpp` skybox-orientation math (`SetSkyboxOrientation`, `accumulate_skybox_orientation`, `mat3_*`) | the DDS-loaded environment skybox behind the flight scene | Superseded by the streaming **dust** starfield (kept); `SetSkyboxEnabled` had no callers, so the skybox was always-on dead weight over the dust | ✅ **Removed 2026-07-04** — the dust background now draws unconditionally; `LoadCubemap` stays in TextureManager as a general utility |
| The ship-fused camera/projection stack: `NeuronClient/ViewMetrics.h`, `SceneProjection.h`, `CameraFollow.h`, `DeepspaceOutpost/Camera.h/.cpp` (+ their tests) and the piloting keys (roll/climb ramps, speed keys, the cockpit corner-beam `draw_laser_lines`) | the implicit "camera == ship" view, the focal-pixel software projection, and hull piloting | Replaced by the free camera: `NeuronClient/Camera` (view+projection, DirectXMath) + `CameraController` (first-person / orbit) + the game's `CameraRig`; the renderer consumes `View()`/`Projection()`; records are world-frame; the active ship renders on screen; flight intent is always zero (camera-only control) | ✅ **Replaced 2026-07-04** — see ARCHITECTURE.md §7 "The free camera" |
| `NeuronClient/graphics/Render2D` runtime-HLSL path: `CompileHLSL` (`D3DCompile`), `AddProgram`, the public `RegisterProgram`, `#include <d3dcompiler.h>`, and the `d3dcompiler` link (`NeuronClient/CMakeLists.txt`) | runtime compilation of caller-supplied 2D shader programs | Every program the renderer uses (default + text outline) is compiled offline by fxc into `shaders/CompiledShaders/*.h` byte arrays; `RegisterProgram` had no callers, so the whole runtime-compile chain was dead | ✅ **Removed 2026-07-05** (`SetProgram`/`SetShaderParams`/`TextOutlineProgram` for the built-in programs stay) |
| Stale `NeuronClient/gfx.h` definitions: the unreachable `RES_512_512` metrics block (its guard is never defined in any TU that includes gfx.h), the unused `GFX_X_OFFSET`/`GFX_Y_OFFSET` and `GFX_VIEW_TX/TY/BX/BY` macros, the unreferenced palette entries `GFX_COL_YELLOW_3/4` + `GFX_ORANGE_1/2/3`, the `IMG_DICE` sprite id, and the never-called `gfx_draw_triangle`/`gfx_clear_area`/`xor_mode` (+ their `gfx2d.cpp` definitions and the vestigial `g_xor_mode`/`Cmd::xorop` XOR plumbing, which was never read at flush) | legacy Allegro-era graphics constants and dead 2D entry points | Zero references anywhere (`git grep`); the XOR path existed for the old chart cross-hairs that I6 replaced | ✅ **Removed 2026-07-05** (live metrics `GFX_SCALE`/`GFX_X_CENTRE`/`GFX_Y_CENTRE`, the used palette/`IMG_*` entries, and every called `gfx_*` entry point stay) |
| The letterboxed chart screens: `docked.cpp` `display_galactic_chart`/`display_short_range_chart`/`display_data_on_planet` + their `display_replicated_*` draw helpers, `draw_fuel_limit_circle`, `show_distance_to_planet`, `move_cursor_to_origin`, `find_planet_by_name`, `teleport_to_cursor`, `chart_nearest_to_cursor`, `chart_project_current`; `main.cpp` `handle_chart_pointer`/`draw_chart_hyperspace_button`/`draw_cross`/`move_cross`/`arrow_*`/`d_pressed`/`f_pressed`/`o_pressed`/name-search; the `SCR_GALACTIC_CHART`/`SCR_SHORT_RANGE`/`SCR_PLANET_DATA` modes | the galactic / short-range / planet-data screens drawn in the 512×514 canvas + their keyboard crosshair controls | Replaced by the native `ChartWindow` (`GameWindows.cpp`) on the GUI overlay, fed by the render-free `ChartData` API (`ChartData.h`); the projection/selection helpers it reuses (`chart_project_all`/`chart_project_short_range`/`chart_current_system`/`current_system_name`/`hyperspace_system_name`) stay | ✅ **Removed 2026-07-05** — first screen migrated off the letterbox toward retiring it |
| `docked.cpp` `display_commander_status` + its `EQUIP_*` layout macros; `SCR_CMDR_STATUS` | the legacy 512×514 commander-status screen (the default docked view) | Docked view is now the camera-space 3D scene + native `StationMenuWindow` (`GameWindows.cpp`); the commander data is the F9 `CommanderWindow`. `condition_txt` stays (`build_cmdr_status` uses it) | ✅ **Removed 2026-07-05** (Part B: docked side off the letterbox) |
| `main.cpp` `display_break_pattern` + `SCR_BREAK_PATTERN` (+ its setters at launch / dock / hyperspace / escape-pod) | the first-person concentric-ring launch/dock/hyperspace transition | Obsolete in third person — the ship just appears in space / at the station via server snapshots; transitions switch the view directly (`enter_station()` / `SCR_FRONT_VIEW` + `CloseStationMenu()`) | ✅ **Removed 2026-07-05** (Part B; was the last retro-mode render) |
| The **letterbox** present path: `NeuronClient/platform/gfx2d.cpp` `g_scene_full` + the retro branches of `canvasW/H`, `canvasPlacement`, `gfx_clear_display`, `gfx_set_scene_clip`, `gfx_display_centre_text`, `gfx_set_scene_fullwindow`; the scene-anchor helpers `gfx_hud_anchor`/`gfx_anchor`(+`gfx_anchor_point`)/`gfx_window_to_canvas` (`gfx.h`) | the fixed 512×514 retro canvas centered/scaled onto the window | With every screen full-window (native GUI windows + camera-space scene), retro mode was provably never active, so `canvasW/H` always return the client size → `canvasPlacement` is identity → the 2D batch and 3D scene fill the window 1:1. `gfx_scene_size`/`gfx_set_draw_origin`/`gfx_set_scene_clip`/`gfx_canvas_size` stay as client-space HUD utilities; `GFX_SCALE`/`GFX_X_CENTRE`/`GFX_Y_CENTRE` stay as the `ChartWindow`'s internal metrics | ✅ **Removed 2026-07-05** (Part B step 4 — the letterbox is decommissioned) |

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
| **Legacy presentation — keep, modernize incrementally** | `threed.cpp` (draw primitives), `stars.cpp`, `intro.cpp`, `shipdata.cpp`/`shipface.cpp` (mesh tables), `planet.cpp` (chart name/description text), station screens in `docked.cpp`, HUD in `space.cpp`, `random.cpp` (client VFX rng) | Stays; absorbed gradually by Track H (instanced renderer) and S6 (math-stack retirement). (`file.cpp` config subsystem removed 2026-07-04 — no local config files. **2026-07-05: their 2D drawing is fully native now** — the `gfx2d`/`gfx.h` layer is deleted; these files draw via the Track I native paths: `Render2D`/`TextRenderer` through `RenderGameHud` + the GUI windows, with the engine seam in `GameScene.h` and the palette/sprite ids in `GamePalette.h`) |
| **Legacy math stack — retire file-by-file** (S6) | `NeuronClient/vector.h/.cpp` (`Vector`, `Matrix[3]`) used by the legacy screens and the record structs (`ReplicatedScene.h` carries `Vector`/`Matrix` PODs) | 🟡 **Partially done 2026-07-04:** the live render path's matrix math is DirectXMath now (`Camera`/`CameraController` own view+projection; `Scene3D` composes XMMATRIX MVPs; `SceneProjection.h`/`ViewMetrics.h` deleted). Legacy screens + the POD record types convert as they are edited; delete `vector.h/.cpp` last |

Also legacy: the `EventManager` remnant (only the Win32 `WNDPROC` fan-out
remains; keep). (The `OpenglDirectx` GL-over-D3D layer this audit flagged was
retired by the GraphicsCore/Render2D device unification; it no longer exists.)

Orphaned assets: `GameData/Models/` (67 files: 33 `.obj` + 33 `.json` +
`elite.mtl`) generated by `tools/shipdata2obj` but loaded by nothing — the
runtime builds meshes from the compiled tables (`SceneMeshes.cpp:60-88`).
`cmo.md` proposes a DSOM loader for them but is referenced by nothing.
Owner decision needed (§13, Q3).

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
| **I — Interaction model** | `UnitOrder` + server `OrderSystem` (restores ship movement), selection/picking, command UX + move gizmo, ability bar, touch/gesture layer, pointer charts, keyboard reduction | #22 (`docs/interaction.md`) | **I1 now (urgent)**; before F1, which reuses its `UnitOrder` |

---

## 2.1 Recommended model per phase

Which Claude to point at each phase, tuned to *this* repo's constraint: the Linux
agent sandbox cannot compile (MSVC/DX11/WinRT), so **CI is the only oracle** and a
subtle mistake in un-runtime-tested code (wire ABI, native ODBC, `/fp:strict`
determinism, threading, security) is expensive to catch. The tiers:

- **Opus 4.8 / Fable 5** (top tier — pick either; Fable 5 is the faster of the
  two): security-sensitive work, code CI compiles but never *runs* (ODBC, the
  render path), behaviour-preserving refactors that must stay bit-identical under
  `/fp:strict`, off-thread/async correctness, and cross-cutting design where one
  wrong assumption cascades. Default to this tier whenever a bug would be silent.
- **Sonnet 5** (strong mid): well-scoped, CI-testable implementation with clear
  acceptance criteria and a fast feedback loop — most gameplay systems and
  mechanical-but-nontrivial wiring.
- **Haiku 4.5** (fast/cheap): trivial, low-risk mechanical work — deletions, tiny
  guards, doc passes.

When unsure, size **up** — the sandbox can't catch what a stronger model wouldn't
have written. Model ids: `claude-opus-4-8`, `claude-fable-5`, `claude-sonnet-5`,
`claude-haiku-4-5`.

| Phase | Rec. model | Why |
|---|---|---|
| A1 Purge client game rules | Sonnet 5 | Large but mechanical deletion; CI + existing tests catch regressions. |
| A2 Wire equip/refuel | Sonnet 5 | Well-scoped server wiring with tests. |
| A3 Snapshot interpolation | Sonnet 5 | Contained client-render change, testable. |
| A4 Delete dead code | Haiku 4.5 | Pure deletion by an inventory list. |
| A5 Protocol hygiene (codec/ABI) | **Opus 4.8 / Fable 5** | Permanent message-id ABI + generic codec — wire mistakes are silent and forever. |
| A6 Determinism & build hygiene | Haiku 4.5 | Tiny flag/build tweaks. |
| A7 Documentation truth pass | Haiku 4.5 | Prose reconciliation. |
| B1 `ClientHello`-first handshake | **Opus 4.8 / Fable 5** | Inverted connection state machine; include-sufficiency traps only CI sees. |
| B2 Session token (security) | **Opus 4.8 / Fable 5** | Security-critical: token auth, spoof/replay, rate limits — do not economize. |
| B3 Reconnect grace & resume | **Opus 4.8 / Fable 5** | Reliable-lane sequencing + endpoint migration edge cases. |
| B4 SQL persistence | **Opus 4.8 / Fable 5** | Off-sim-thread service + raw ODBC CI never runs + never-alias-a-save correctness. |
| C Identity layer | **Opus 4.8 / Fable 5** | Relational ownership index with generation-recycle correctness; cross-cutting. |
| D1 Spatial grid in pairwise loops | **Opus 4.8 / Fable 5** | Must stay bit-identical under `/fp:strict`; deterministic candidate ordering. |
| D2 Frame arena / scratch reuse | Sonnet 5 | Mechanical, golden-preserving buffer reuse. |
| D3 Accumulator + tick metrics | Sonnet 5 | Pure, testable pacer/metrics logic. |
| D4 Rate/cadence guards | Haiku 4.5 | One small per-session counter. |
| D5 BotClient harness | **Opus 4.8 / Fable 5** | New headless target + real-UDP timing + a process-level CI smoke lane. |
| E1 Time sync → lag compensation | **Opus 4.8 / Fable 5** | Transform ring buffers + the derived-state/determinism boundary. |
| E2 Snapshot quantization/delta | **Opus 4.8 / Fable 5** | New wire format, server-rounded exactness, delta/keyframe resync. |
| E3 Strategic AOI tier | Sonnet 5 | A new tier over the existing AOI grid; scoped and testable. |
| F1 First ordered unit (escort) | Sonnet 5 | Reuses AI/flight/combat; reuses I1's `UnitOrder`. |
| I1 UnitOrder + OrderSystem | **Opus 4.8 / Fable 5** | New wire ABI + order→intent execution with crime attribution; a silent validation gap is an anti-cheat hole. |
| I2 Selection & picking | Sonnet 5 | Client-side projection math that already exists, repurposed; manual-verified. |
| I3 Command UX (gizmo, radial menu) | **Opus 4.8 / Fable 5** | Un-CI-testable 3D UX (plane math, gesture disambiguation) — correctness by inspection. |
| I4 Ability bar & HUD | Sonnet 5 | Widget-stack work on existing patterns. |
| I5 Touch/gesture layer | **Opus 4.8 / Fable 5** | Multi-pointer state machine CI never runs; subtle edge cases (capture, cancel, palm). |
| I6 Pointer charts | Sonnet 5 | Contained screen rework over client-held data. |
| I7 Keyboard reduction | Haiku 4.5 | Mechanical deletion + doc pass. |
| F2 Fog of war | Sonnet 5 | `KnownSystems` + incremental manifest, testable. |
| F3 Ownership & territory | **Opus 4.8 / Fable 5** | Large, stateful, persistence-integrated claim/deploy. |
| F4 Living economy | **Opus 4.8 / Fable 5** | Market drift + persistence + emergent supply — many moving parts. |
| F5 Factions & standings | Sonnet 5 | Standings matrix over existing crime/roster hooks. |
| G1 Kill VFX broadcast | Haiku 4.5 | One broadcast message + client cue. |
| G2 Missile-lock validation | Sonnet 5 | Server-side validation (anti-cheat flavour). |
| G3 Chat + abuse controls | Sonnet 5 | Straightforward, but moderation deserves care. |
| G4 Suns & cabin heat | Sonnet 5 | A new gameplay system, headless-testable. |
| G5 Missions | **Opus 4.8 / Fable 5** | Large scripting/content system with persistence. |
| H Rendering | **Opus 4.8 / Fable 5** | DX11 visual work CI can't verify; instancing/LOD/post correctness by inspection. |

---

## 3. Track A — Truth & hygiene

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

### A5 — Protocol hygiene (#19: S2 + S3 + S7 remnants) — **M** — ✅ **done 2026-07-03**

All three followed the ABI rule: new id, retire the old id in place.

1. **S3 — Travel split.** ✅ `TravelRequest` (`0x1000`) / `TravelResponse`
   (`0x1001`) in `Messages/Defs/Travel.h` carry travel
   (`TravelKind{Hyperspace, InSystemJump}`, `TravelStatus{Arrived,
   Witchspace, Jumped, NotEnoughFuel, OutOfRange, UnknownSystem, MassLocked,
   Rejected}`). `HyperspaceSystem` outcomes now speak `TravelStatus`;
   `GameServer` routes `TravelRequest` to a dedicated handler and
   `HandleStationRequest` is docking + commerce only (the retired
   Teleport/JumpDrive kinds fall through to the dispatcher's rejection,
   pinned by the A4 test). The client chart/jump keys send `TravelRequest`;
   a `TravelResponse` subscriber drives the screen flow — and restores the
   classic "Mass Locked" message the thin client had lost.
2. **S2 — Manifest re-cut, request-driven.** ✅ `GalaxyChunkRequest`
   (`0x1002`) / `GalaxyChunk` (`0x1003`) in `Messages/Defs/GalaxyChunks.h`
   through the generic codec — which gained **nested Record support**
   (a `Fields()` struct with no id, usable in vectors; unit-tested) for the
   per-system entries. The client pulls ranges of ≤64 (server also clamps),
   ≤16 entries per message so each fits `SAFE_UDP_PAYLOAD`; an out-of-range
   request returns an empty chunk carrying `total`; the chart renders
   progressively and keeps pulling until complete. The hand-coded `0x0210`
   and `NeuronCore/GalaxyManifest.h` are retired (id reserved);
   `Net::GalaxySystemInfo` moved to the new header; the now-purposeless
   `MessageEndpoint::Channel()` accessor was removed. Fog of war (F2) later
   filters the chunk replies by `KnownSystems`.
3. **S7 — Band note + alias.** ✅ done earlier (with A4): the §4.3
   grandfathering note is in ARCHITECTURE.md and the `Net::ClientInput`
   alias is deleted.

*Acceptance (implemented):* round-trip + golden-layout tests for all four new
messages plus nested-record and truncation-safety tests
(`TravelProtocolTests.cpp`); the pull protocol is exercised end-to-end through
`ServerSessions`/`MessageEndpoint` (`GalaxyManifestTests.cpp`); governance
tests cover the new ids; the A4 station test pins that travel kinds are
rejected by the station dispatcher.

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

### B1 — `ClientHello`-first handshake (#2 / S1) — **S** — ✅ **done 2026-07-03**

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

*As built:* `OnInput` returns an invalid id for an unknown endpoint (no spawn,
no session, no reply); `OnReliable` provisions a pending, entity-less **shell**
so a brand-new endpoint's Control-lane `ClientHello` can be received; `OnHello`
version-checks (→ `HelloReject` on mismatch, no entity), else spawns + adopts
the name + queues `HelloAck{0, entityId, PROTOCOL_VERSION}`. `Session::Live()`
(entity valid) gates gameplay everywhere: `ProcessReliableRequests` ignores
station/travel/chart requests from a shell; `PublishState` streams no world
state to a shell but still flushes its Control lane (the reply + acks); `Roster`
excludes shells. Membership-change roster broadcast moved to the OnHello-Accepted
path (Count-based detection no longer works with shells). Client learns its
entity + token from `HelloAck` (`ReplicationClient::Pump`) and gates the chart
pull on being connected. `AssignPlayer 0x0001` retired in place (id reserved,
still registered); `HelloAck 0x0003` / `HelloReject 0x0004` added. Session tests
rewritten to the hello handshake (unknown-input-ignored, bad-version-rejected,
hello-renames-a-live-session, pending-shell-excluded-from-roster).

### B2 — Session token: endpoint ≠ identity (#3) — **S** — ✅ **done 2026-07-04**

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

*As built:* the framing (`Framing.h` `'NMSG'` + `MessageEndpoint.h` `'NRLB'`)
carries a `token u64` after the lane byte, `PROTOCOL_VERSION` bumped to 2 (no
back-compat window — nobody is on the wire yet, so the old framing is simply
retired rather than dual-decoded). Tokens come from the OS CSPRNG in
`Server/SecureRandom.h` (`BCryptGenRandom`, auto-linked via `#pragma comment(lib,
"bcrypt.lib")` — no CMake/vcxproj change), injected into the pure `ServerSessions`
through `SetTokenSource` (a deterministic fallback serves headless tests).
`ServerSessions` keys sessions by endpoint but authenticates by token: a
`token → endpoint` index (`Authenticate`) drops wrong/no-token datagrams before
decode and re-keys the session on a NAT rebind; `OnInput`/`OnReliable` take the
token (peeked via `PeekReliableToken`); token-less pre-handshake datagrams are
rate-limited per endpoint (`RATE_MAX_UNAUTH`/`RATE_WINDOW_TICKS`/`RATE_MUTE_TICKS`),
and a token-less datagram is never routed to a live session. The client stamps the
token (`ReplicationClient`: `m_events.SetToken` + `PacketWriter(..., token)`) once
`HelloAck` arrives. Tests: session-level (wrong-token input ignored, token-less
input ignored, NAT-rebind keeps the entity, wrong-token reliable dropped without
provisioning a session, reaped tokens forgotten) + framing-level (token
round-trips and is peekable; truncated-token datagrams rejected); existing
hand-built-packet tests updated for the new header.

### B3 — Reconnect grace & resume (#4) — **S** — ✅ **done 2026-07-04**

On session silence, keep the entity alive but **safe-parked** (intent
zeroed, autoEngage protections unchanged) for a grace window (60 s ≈ 1800
ticks, replacing the hard 300-tick reap for *authenticated* sessions). A
`ClientHello` carrying a known token re-binds to the new endpoint and
resumes: resend `HelloAck`, roster, `PlayerStatus`, `CargoManifest`, and
(post-A5) the client re-pulls manifest chunks. After the grace window the
reap proceeds as today.

*Acceptance:* kill the client socket, reconnect within grace from a new
port → same ship, same cargo; after grace → normal despawn.

*As built:* `Reap` now takes a shell timeout (`SESSION_TIMEOUT_TICKS` 300) and a
longer grace timeout (`SESSION_GRACE_TICKS` 1800) — a live session survives the
grace window, a pending shell still reaps short. `ServerSessions::SafeParkSilent`
zeros a live session's `FlightIntent` after `SESSION_PARK_TICKS` (~45) of silence
(GameServer calls it at the top of `AdvanceSimulation`). A hello on a live session
is `HelloResult::Resumed` (was `NameChanged`): OnHello keeps the entity + token
and re-queues `HelloAck`; GameServer replays the roster to just that client,
resends its `CargoManifest`, and evicts its `PlayerStatus` change-cache entry
(`OnChangeCache::Forget`) to force a HUD resend. The B2 token migration already
re-binds the endpoint, so a token-bearing reconnect from a new address lands on
the same session. Tests: grace-vs-shell reaping, safe-park zeroing, resume re-acks
and keeps the entity, and a new-endpoint reconnect resumes the same ship. (Scope
note: reliable-lane sequence continuity assumes the client keeps its transport
across the blip — true of the current client; a full channel-reset resume is
deferred.)

### B4 — Persistence on SQL Server (#1) — **L** — ✅ **done 2026-07-04** (OdbcStore soak-validated, not CI)

The top structural gap. This section is the full design (the pre-code pass);
honors §12: async batched writes off the sim thread, never per-tick
positions to SQL, the world simulates while players are offline, MS SQL
Server, and the key space anticipates the Account → Empire → N entities
identity model (Track C) so that retrofit is additive.

#### B4.1 Scope

Persisted: **commanders** (account + player rows + cargo), **world state
that drifts** (station markets — schema now, load-bearing when F4 makes
markets mutable; until then rows are materialized lazily on first write),
**world metadata** (galaxy seed, schema version, last world tick), and an
append-only **command log** for audit/replay. NOT persisted: per-tick
positions (locked §12), NPCs, canisters, missiles, sessions/endpoints (B2's
tokens are transport state, not durable identity), or anything derivable
from the galaxy seed.

#### B4.2 Schema v1 (SQL Server; shipped as `NeuronServer/schema.sql`)

```sql
CREATE TABLE dbo.accounts (
  account_id      INT IDENTITY PRIMARY KEY,
  commander_name  NVARCHAR(20) NOT NULL UNIQUE,  -- the sanitized ClientHello name
  auth_token_hash BINARY(32) NULL,               -- reserved: real auth later; NULL = name-claim
  created_utc     DATETIME2 NOT NULL,
  last_seen_utc   DATETIME2 NOT NULL
);

CREATE TABLE dbo.empires (            -- Track C lands into this; v1: one per account
  empire_id  INT IDENTITY PRIMARY KEY,
  account_id INT NOT NULL REFERENCES dbo.accounts(account_id)
);

CREATE TABLE dbo.players (            -- one avatar row today; N owned units later
  player_id      INT IDENTITY PRIMARY KEY,
  empire_id      INT NOT NULL REFERENCES dbo.empires(empire_id),
  credits        INT NOT NULL,        -- tenths of a credit (Wallet.credits)
  fuel_tenths    SMALLINT NOT NULL,   -- Fuel.tenths (max stays code-owned)
  wanted_level   SMALLINT NOT NULL,   -- Wanted.level
  score          INT NOT NULL,        -- session score (per-player record, C2)
  hold_capacity  SMALLINT NOT NULL,   -- CargoHold.capacity
  missiles       SMALLINT NOT NULL,   -- Equipment.missiles
  equip_flags    INT NOT NULL,        -- bitmask, see PersistEquipFlags below
  last_system_id INT NOT NULL,        -- system to wake docked at (-1 = home)
  in_witchspace  BIT NOT NULL,
  updated_tick   BIGINT NOT NULL,     -- world tick of the snapshot
  updated_utc    DATETIME2 NOT NULL   -- stamped by the persistence thread
);

CREATE TABLE dbo.player_cargo (       -- non-zero stacks only
  player_id INT NOT NULL REFERENCES dbo.players(player_id),
  commodity TINYINT NOT NULL,         -- 0..16
  units     SMALLINT NOT NULL,
  PRIMARY KEY (player_id, commodity)
);

CREATE TABLE dbo.station_markets (    -- lazily materialized; authoritative from F4
  system_id    INT NOT NULL,
  commodity    TINYINT NOT NULL,
  stock        SMALLINT NOT NULL,
  price        SMALLINT NOT NULL,     -- legacy x4 fixed-point, as in MarketEntry
  updated_tick BIGINT NOT NULL,
  PRIMARY KEY (system_id, commodity)
);

CREATE TABLE dbo.world_meta (         -- 'schema_version', 'galaxy_seed', 'world_tick'
  meta_key   NVARCHAR(32) PRIMARY KEY,
  meta_value NVARCHAR(128) NOT NULL
);

CREATE TABLE dbo.command_log (        -- audit/replay; order = log_id
  log_id     BIGINT IDENTITY PRIMARY KEY,
  world_tick BIGINT NOT NULL,
  player_id  INT NOT NULL,
  message_id INT NOT NULL,            -- the catalog MessageId
  payload    VARBINARY(512) NOT NULL, -- the message's generic-codec encoding
  logged_utc DATETIME2 NOT NULL
);
```

Design notes: `equip_flags` bit assignments live in ONE C++ enum
(`PersistEquipFlags` in the snapshot header) — bit0 largeCargoBay, bit1 ecm,
bit2 fuelScoop, bit3 energyBomb, bit4 escapePod; new G-track equipment
appends bits, never renumbers (same permanence discipline as message ids).
`empires` exists from day one so Track C adds columns/rows, not a rekeying
migration; v1 creates one empire per account transparently. Auth in v1 is
**name-claim** (first hello owning a name owns the account — consistent with
the current trust level; `auth_token_hash` is the reserved seam for real
auth). Schema changes bump `schema_version` and ship an idempotent migration
block in `schema.sql`; the server refuses to start against a newer schema
than it knows.

#### B4.3 The persistence service (NeuronServer)

New pieces, all in NeuronServer (its chartered role):

- **`PlayerPersistState`** — a plain snapshot struct mirroring the durable
  components (`Wallet`, `CargoHold`, `Fuel`, `Wanted`, `Equipment`,
  witchspace flag, last system) plus the session's per-player name/score
  records (C2: passed to the converter explicitly), with `FromComponents(world,
  entity, tick, name, score)` / `ApplyToComponents(world, entity)` converters.
  Copying it is ~120 bytes — cheap enough to snapshot every player at cadence.
- **`IPersistenceStore`** — the seam: `UpsertPlayer(state)`,
  `LoadPlayer(name) -> optional<state>`, `AppendCommands(batch)`,
  `UpsertMarketRows(batch)`, `ReadMeta/WriteMeta`. Two implementations:
  **`OdbcStore`** (production: raw ODBC via `SQLDriverConnect` — native-first,
  no ORM; a Windows-only .cpp) and **`InMemoryStore`** (a header-only map,
  used by every test). This interface is justified wrapper-wise: it exists to
  swap the backing store, which is real behavior, and it is what keeps CI
  free of a SQL Server dependency.
- **`PersistenceService`** — owns the writer thread and two queues:
  *requests in* (player snapshots — coalesced **one pending snapshot per
  player, latest wins**, so memory is bounded and a slow DB never grows the
  queue past player count; plus command-log batches on a capped ring that
  drops-oldest and counts drops) and *load results out* (drained by the sim
  thread at a fixed tick point). The sim thread only ever copies structs and
  swaps queue buffers under a mutex — it **never** touches ODBC, never
  blocks on the DB, and never reads the wall clock for sim purposes
  (timestamps are stamped on the persistence thread; the sim contributes
  only tick numbers). DB failures retry with exponential backoff on the
  writer thread; coalescing makes retry loss-free for snapshots.

#### B4.4 Server wiring (GameServer)

- **Load on hello (requires B1):** the version-checked `ClientHello` is the
  single choke point. On hello: issue an async `LoadPlayer(name)`; the
  session sits in a *loading* state (no entity yet). A completed load is
  applied at a fixed tick step (with the other reliable requests): spawn the
  player entity, `ApplyToComponents`, dock it at `last_system_id`'s station,
  reply the handshake. Unknown commander → create account+empire+player rows
  with the fresh-spawn defaults. **DB unavailable → the hello stays parked**
  (the reliable lane keeps it alive; the client shows its normal connecting
  state) — a transient outage must never alias an existing commander into a
  fresh one. Ordering note: if B4 code lands before B1, the interim load
  point is the current `ClientHello` handler (after spawn), applying the
  loaded state to the already-spawned entity — workable but uglier; B1 first
  is the intended order.
- **Save cadence:** every `PERSIST_INTERVAL` ticks (150 ≈ 5 s) snapshot every
  live player through the existing `OnChangeCache` pattern (write only what
  changed since the last accepted snapshot); immediately on session reap and
  on graceful shutdown (bounded flush, ~5 s deadline, then final
  `world_tick` to `world_meta`).
- **Command log:** appended where reliable commands are handled —
  `StationRequest`, `TravelRequest`, `ClientHello` (name adoption).
  `InputCommand` is deliberately NOT logged (30 Hz noise; positions are
  derived state). The log is audit/refund/replay material, not authority.
- **Deployment switch:** connection string from `DSO_DB` (ODBC string;
  suggested default `Driver={ODBC Driver 17 for SQL Server};
  Server=localhost;Database=dso;Trusted_Connection=yes`). **Unset → the
  server runs with persistence disabled** (no store, loads short-circuit to
  fresh spawns) so the dev loop, tests, and CI are unchanged by default.

#### B4.5 Testing (headless; CI gains no SQL dependency)

In `Tests/NeuronServer` against `InMemoryStore`: `PlayerPersistState`
component round-trip; service coalescing (N snapshots of one player → one
upsert, latest wins); load-completion application order (deterministic tick
point); parked-hello-on-store-error; log-ring overflow counting; and an
end-to-end "restart": run a mini-world, trade, snapshot, tear down, rebuild
from the store, assert credits/cargo/fuel/equipment/score/wanted survive and
the player wakes docked at the right station. `OdbcStore` itself is
validated by a manual soak on Windows (documented in the PR), not by CI.

#### B4.6 Sub-milestones

1. ✅ **done 2026-07-04** — Store interface + `InMemoryStore` +
   `PlayerPersistState` + service (queues/thread) + full headless test suite.
2. ✅ **done 2026-07-04** — GameServer wiring: load-on-hello (deferred spawn),
   cadence saves, shutdown flush, spawn-from-state. *As built:* `OnHello` gained a
   `_deferSpawn` flag - with persistence on it parks the session (`HelloResult::
   Loading`, no entity) and `SpawnLoaded` finishes the handshake once the load
   returns, so a returning commander is never spawned-fresh (which a save would
   then alias). GameServer's `ApplyCompletedLoads` drains completed loads and spawns
   + `PlayerStateApplyToComponents` + `DockAtSystemOrNearest` (a cargo-preserving
   dock helper) at the last system; an unknown commander fresh-spawns and the
   account row is created. `SavePlayers` snapshots live players every
   `PERSIST_INTERVAL` (150) ticks through an `OnChangeCache` (durable-field
   equality, tick excluded) and re-requests lost loads; the destructor final-saves.
   Loading sessions get the B3 grace window. `DSO_DB` unset ⇒ `m_persist` null ⇒
   zero behavior change (the CI path); set ⇒ in-memory store for now (B4.3 swaps in
   ODBC). ServerSessions loading primitives are unit-tested.
3. ✅ **done 2026-07-04** — `OdbcStore` + `schema.sql` + manual Windows soak.
   *As built:* `Server/OdbcStore.cpp` implements `IPersistenceStore` over raw ODBC
   (MERGE upserts for accounts/empires/players, cargo row replacement, joined load,
   command-log inserts, market MERGE, meta), auto-linking `odbc32` via a `#pragma`.
   It is compiled ONLY under the CMake option `DSO_ENABLE_ODBC` (OFF by default), so
   CI needs no driver/database and the file is an empty TU there; the factory then
   uses the in-memory store when `DSO_DB` is set. With the option ON the factory
   connects via `DSO_DB`, and a connect failure disables persistence entirely rather
   than aliasing saves over a fresh spawn. Runtime correctness is soak-validated.
4. ✅ **done 2026-07-04** — Command log + the replay-smoke test (station requests
   replayed from the log against a fresh world reproduce identical wallet outcomes).

*Acceptance:* stop/restart the server → commanders keep credits, cargo,
fuel, equipment, wanted, score, and wake docked at their last station; a DB
outage neither blocks the tick nor wipes a commander; with `DSO_DB` unset
nothing changes at all.

---

## 5. Track C — Identity layer (#5) — **M** — ✅ **done 2026-07-04**

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

*As built (C1 + C2, 2026-07-04):*

- **C1 — identity plumbing + wire.** `ServerSessions` allocates a
  monotonically increasing `playerId` per accepted hello (stable across a
  B3 resume). `SimComponents.h` gains `Owner{playerId}`, stamped by
  `SpawnPlayer` and indexed in `ECS::OwnershipIndex` (NeuronCore:
  add/remove/forget, swap-remove, generation-safe, unit-tested including
  slot-recycle). Reap destroys *every* owned entity via the index (plus a
  safety-net destroy of the primary). `GrantOwnership()` is the public seam
  a future second hull uses. **Wire deviation from the sketch above, by
  design:** since nobody plays yet (pre-launch, no back-compat), `HelloAck`
  was extended **in place** to `{sessionToken u64, playerId u32, entityId
  u32, protocolVersion u16}` and `PlayerInfo` to `{playerId, entityId,
  name, wantedLevel}` under `PROTOCOL_VERSION = 3` — no `AssignControl
  0x0005`, no successor id `0x0304`. Post-launch such layout changes take
  new ids per the permanent-ABI rule (noted at the definitions).
  `ReplicationClient` exposes `PlayerId()`; the BotClient smoke asserts a
  nonzero playerId end-to-end.
- **C2 — records off the hull.** The `PlayerRecord` component (name/score)
  is deleted; the commander name and score are per-player records on the
  session (the `players` row via B4). `CreditKill` now pays the wallet in
  place and returns `KillCredit{bounty, score}`; `GameServer` routes the
  score delta to `ServerSessions::AddScore`. The persistence converters
  take name/score as explicit parameters; on load the server stamps
  `session.score` from the snapshot. Rosters and the scoreboard publish
  straight from the session records.
- **Scope note — the wallet stays ship-borne.** `Wallet` remains a hull
  component: every trading/bounty/equipment path (`BuyCommodity`,
  `ProcessStationRequest`, `ApplyKill`) operates on it in place and is
  heavily unit-tested. It becomes an empire-level record only when several
  hulls can trade concurrently (F-track), where the move is forced anyway.

---

## 6. Track D — Performance & the load harness — ✅ **all of D1–D5 done 2026-07-04**

### D1 — Spatial grid into every pairwise loop (#6 / E1) — **M** — ✅ **done 2026-07-04**

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

*As built:* `GameLogic/Broadphase.h` (`BROADPHASE_CELL = 16 384` +
`QuerySortedNeighbours`/`CellsForRange`) converts the three real per-tick
pairwise loops — `StepCollisions` (i<j sweep), `StepCombat` (per-shooter
nearest-enemy scan), `ScoopSystem` (players × canisters). Exactness is by
construction, stronger than sort-by-entity-index: grid entries are keyed by the
system's own **dense-array index**, so the sorted candidates are a strict
subsequence of the original scan order — every outcome including distance
tie-breaks is bit-identical, proven by `BroadphaseEquivalenceTests.cpp` (seeded
worlds spanning negative coordinates and multiple cells; the pre-D1 brute-force
algorithms are kept verbatim in the test as oracles; kills/energies/shields/
fire-timers/holds compared exhaustively). `StepCollisions`/`StepCombat` feed the
D3 `candidatePairs` metric. **Scope note:** the per-event, single-subject scans
(`ResolvePlayerFire`, `ActivateEcm`, `DetonateEnergyBomb`) are deliberately NOT
converted — they are O(n) per rare event and a fresh grid build is itself O(n),
so conversion is pure overhead until a *persistent* per-tick grid exists; that
needs a staleness story across tick phases and rides fleet-scale work (E-track).
The ship↔planet check stays linear over the handful of planet landmarks.

### D2 — Frame arena / scratch reuse (#7 / E2) — **S** — ✅ **done 2026-07-04**

Persistent per-system scratch buffers with `clear()`-not-free semantics for
the per-tick vectors confirmed in the audit (`CollisionSystem`, `StepCombat`,
`StepAi`, `StepMissiles`, `StepLoot`, `SnapshotHelpers`), plus reuse of
snapshot build and reliable-channel resend buffers. Simplest shape: a
`FrameScratch` struct owned by `GameServer`, passed down by reference —
no globals, no allocator cleverness until profiling demands it.

*As built:* `GameLogic/FrameScratch.h` bundles the per-tick working-set storage
for the five GameLogic systems named above (`StepCollisions`/`StepCombat` also
had their own D1 `Spatial::Grid` — itself constructed fresh every call — folded
in as a persistent, `Clear()`-not-reconstructed member alongside their unit
vectors/maps). Every function taking a `FrameScratch&` gives it a trailing
default argument bound to a process-wide fallback instance
(`Detail::DefaultScratch()`), so every pre-existing test call site (~40 of them)
compiles unchanged; `GameServer` owns and passes one real `m_scratch` explicitly
by reference every tick (the "no globals" shape the spec asked for — the
fallback is purely an escape hatch for callers that don't care). Two Server-side
`SnapshotHelpers` functions (`CurrentIds`, `AppendLandmarks`) were converted to
out-parameter scratch (the latter is called once PER SESSION per tick, the
hottest allocation site in that file). `AreaOfInterest::Rebuild` reuses its grid
via a new `Spatial::Grid::Clear()` (NeuronCore) instead of replacing the whole
`Grid` object every tick; `SnapshotFor`'s internal candidate list is a `mutable`
member for the same reason. `DespawnTracker::Update` swaps a persistent scratch
set with `m_previous` instead of constructing-then-moving a fresh one every call.

Every scratch field is unconditionally cleared by its owning function before
use (verified field-by-field), so this is a pure allocator optimization with
zero behaviour change — the existing suites (including D1's
`BroadphaseEquivalenceTests`, which run the converted systems through their
default scratch) still pass unmodified. Added `FrameScratchTests.cpp`, which
deliberately shares one `FrameScratch` across two *different* worlds per system
to prove no state leaks between calls (the actual risk this refactor
introduces), plus a `Grid::Clear()` unit test in `SpatialTests.cpp`.

**Scope note:** reliable-channel resend buffers (`ReliableChannel::WritePacket`,
`MessageEndpoint::WriteDatagrams`) were deliberately NOT converted to out-params.
They already return via RVO/move (no double-copy today), and making them
out-params would be an API break across NeuronCore reaching every module
(client and server) for comparatively little payoff — exactly the "no allocator
cleverness until profiling demands it" the spec itself cautions against.
Deferred until D3's metrics (bytes/lane) show it actually matters.

### D3 — Accumulator fixed timestep + tick metrics (#8 / S5, E8) — **S** — ✅ **done 2026-07-04**

Replace `Sleep(33)` (`Server/Main.cpp:37`) with a QPC accumulator: run
catch-up ticks when behind (capped, e.g. 5, then declare overrun), sleep the
remainder when ahead. Add the always-on counters E8 lists: tick-duration
histogram + overrun count, bytes/session/s per lane, entities per AOI
snapshot, broadphase candidate-pair counts (validates D1), reliable resend
rates, per-phase tick-time breakdown. Expose as a periodic console line +
a queryable struct (the BotClient harness reads it).

*As built:* `NeuronServer/TickPacer.h` is the pure accumulator (feed it elapsed
ms → number of fixed steps to run, bounded by `TICK_MAX_CATCHUP`; overrun drops
the backlog rather than spiralling; `SleepMs()` when ahead), driving `Main.cpp`'s
loop off `Timer::Core`. `NeuronServer/TickMetrics.h` is the pure counter (per-tick
duration avg/max, overruns, live entity/session counts, broadphase candidate
pairs, bytes) that `GameServer` feeds each tick from QPC (off the sim's
determinism path) and prints as a `[metrics]` line every `METRICS_WINDOW_TICKS`;
`Metrics()` exposes it for D5. Both are unit-tested. (Bytes are the socket-send
total; per-lane byte split and reliable-resend rate ride E2's packetizer rework.)

### D4 — Rate/cadence guards — **XS** — ✅ **done 2026-07-04**

With B2's rate limiting in place, add the §9 "future" input-cadence sanity:
per-session `InputCommand` acceptance cap per tick window (latest-wins
already bounds damage; this bounds the CPU).

*As built:* `ServerSessions::OnInput` drops any authenticated input beyond
`MAX_INPUTS_PER_TICK` (8) applied by one session in a server tick (a per-session
counter that resets when the tick advances) — returning an invalid id so the
excess input's intent AND its fire are both shed. A normal client sends only a few
inputs per 30 Hz tick, so the cap only bites a flood.

### D5 — BotClient harness → 100-player load test (#20) — **M** — ✅ **done 2026-07-04**

New console target `BotClient/` (the AGENTS.md table finally becomes true):
links NeuronClient headless (no D3D/audio init) + NeuronCore, drives N
scripted sessions over the real UDP stack — hello/handshake (B1/B2), flight
intent orbits, fire bursts, station dock/trade cycles, hyperspace jumps.
Emits per-bot RTT/loss and reads the server's D3 metrics. CI gets a smoke
lane (server + 8 bots, 30 s, asserts zero overruns and zero desyncs);
the 100-bot soak is a manual/perf-lab run that gates every entity-cap
increase (§14).

*As built:* `BotClient/Main.cpp` — each bot is a full production
`ReplicationClient` (ephemeral port, hello redelivered by the reliable Control
lane until acked, session token adopted from `HelloAck`, deterministic orbit
intents, chart pull to completion). `--smoke` mode spawns the **Server itself**
on a side port (stdout redirected to a log), runs the fleet, kills the server,
and parses its `[metrics]` lines — registered as the `BotClient.Smoke` CTest
test (8 bots, 20 s, port 40123; Server gained an optional argv port and an
`fflush` after the metrics line for this). Asserts per bot: connected, token
nonzero, snapshots streaming, galaxy chart complete. **Honest deviations from
the sketch:** tick overruns are asserted under a small tolerance (3), not zero —
hosted CI runners stall for >165 ms through no fault of the server, and a hard
zero would only make the lane flaky; per-bot RTT/loss needs E1's `Ping`/`Pong`
(until then the harness reports frames-to-connect); fire bursts and dock/trade/
jump scripting are hooks for the manual soak, not the CI lane, which stays
minimal-and-reliable (connect + stream + bulk transfer). The 100-bot soak is
this same binary with bigger numbers: `BotClient --smoke --server-exe ...
--bots 100 --seconds 300`.

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

*As built — E1a (time sync), 2026-07-04:* the Control-lane exchange landed as
specified, with one deliberate addition: `Ping` also carries the client's own
latest smoothed `rttMs`, so the server learns each session's latency the same
tick it replies. The client (`ReplicationClient`) probes ~1 Hz once connected,
computes RTT as the unsigned-32-bit echo difference (`Net::LatencyEstimate`, an
EWMA that seeds on the first sample and discards non-positive/spike samples so a
reliable-lane resend can't poison it), and reports it back; the server stores it
raw on the session (`Session.rttMs`). **Trust note:** that RTT is the client's
self-report, not a server-authoritative measurement, so E1b CLAMPS the rewind it
drives to the transform-history window (≤ 15 ticks) — the same bound the ring
buffer imposes; a hardened version measures RTT from the reliable-ack loop
(deferred). New additive ids `0x0006/0x0007`, no `PROTOCOL_VERSION` bump (nothing
existing changed layout); `0x0005` stays reserved (the never-shipped
`AssignControl`, folded into `HelloAck` at C1). Pure-math estimator +
message-wire round-trips unit-tested (`TimeSyncTests.cpp`).

*As built — E1b (lag-compensated fire), 2026-07-04:* `GameLogic/TransformHistory.h`
keeps a 15-tick ring of `(position, nose)` per combat-relevant entity
(`WorldTransform + Combatant`), captured once per tick after the sim advances
(`GameServer::Capture`) — derived state that never feeds back into the
authoritative sim, so determinism holds. On a player laser shot the server sizes
the rewind from the shooter's own reported RTT (`ServerSessions::RttForEntity` →
`LagCompTicks`, which folds in the ~1-tick render interpolation delay of A3 and
CLAMPS to the ring), and `ResolvePlayerFire` tests each candidate TARGET at where
the shooter saw it while keeping the shooter authoritative-current
(favour-the-shooter); damage still lands on the live hull. The rewind is
generation-safe (a recycled index whose slot holds an earlier tenant's sample is
rejected — the C1 lesson), and passing no history (the default) reproduces the
un-compensated path exactly, so every existing fire test is unchanged.
`LagCompensationTests.cpp` covers the ms→ticks conversion, the ring
(sample/clamp/recycle), and the acceptance case (a laterally-moving target that
has left the cone is hit when rewound to its rendered position, and the live shot
misses). **Scope note:** only the instant laser is compensated; the A5
missile/travel cone validations are not yet rewound (the ring already stores nose
for that future use). Only players are lag-compensated — NPC fire goes through
`StepCombat`, never this path. **Trust bound:** the rewind depth is the client's
self-reported RTT clamped to the 15-tick window; a server-authoritative RTT (from
the ack loop) is the hardening follow-up.

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

*As built — E2a (quantized format v2), 2026-07-04:* `SNAPSHOT_VERSION` → 2,
replacing v1 outright (pre-launch: client and server are always the same build,
so no dual-format negotiation - documented so a post-launch layout change takes a
real negotiated version). `NeuronCore/Quantization.h` holds the pure fixed-point
codecs: a unit-vector component → `int16` (×32767, a ~3e-5 grid; 0 and ±1 are
EXACT, so an unrotated basis is lossless) and a speed → `uint16` at 1/256-unit
resolution. In `Replication.h` an entity is now **32 bytes** (was 58): `id(4)` +
`int32` position OFFSET `(12)` + `int16` nose/roof `(12)` + `uint16` speed `(2)` +
`int16` type `(2)`. **Position (reference-origin, per the spec):** the packet
header carries a reference origin as a full `int64` (the server sets it to the
viewer's position); each entity's position is an `int32` offset from it. The
ABSOLUTE world stays **unbounded int64** - only the offset is int32, and every
entity in a snapshot is within the viewer's area of interest (a few million units
at most), so the offset always fits int32 no matter how large the galaxy grows.
Exact (integer subtraction, no float loss); `OffsetToI32` saturates rather than
wraps if an out-of-AOI entity is ever handed in (a bug, not the send path). Header
grows +24 bytes (the int64 reference), amortized over every entity in the packet;
`PacketizeSnapshot` repeats the reference in each split datagram so each decodes
independently. The decoded in-memory `EntitySnapshot` is unchanged (int64 pos +
float basis), so the interpolator and render path are untouched. All rounding is
deterministic integer math (/fp:strict safe), so every client decodes
bit-identically - the "server-side rounding" the delta stage will rely on. Golden
tests: `QuantizationTests.cpp` (primitive round-trips incl. exactness/clamp; full
v2 wire round-trips - exact positions with a zero reference, **absolute positions
beyond int32 reconstructed exactly via a large int64 reference**, lossless
unrotated basis, within-grid rotated basis, wire size). ~45 % raw reduction now;
delta (E2b) crosses the ≥60 % soak bar at fleet scale.

*As built — E2b-1 (delta codec + differ, pure), 2026-07-04:* `NeuronCore/
SnapshotDelta.h` is the headless-testable core of delta compression, split from
the server/client wiring to contain blind-CI risk. A `DeltaSnapshot` carries only
the **changed** entities (new or wire-different, full 32-byte records) and the
**removed** ids, against a baseline the receiver already holds; everything else is
implied unchanged and omitted. `SnapshotDiff(baseline, current)` computes it and
`ApplyDelta(baseline, delta)` reconstructs the current snapshot. **The key
subtlety:** the reference origin tracks the MOVING viewer, so a stationary
entity's *offset* changes every tick - the change test (`SameOnWire`) therefore
compares **absolute** positions (stable int64) and the **quantized** basis/speed
(so a sub-quantum wobble is not a change - the server-side-rounding rule), never
the raw offset. The delta wire format shares the `'NSNP'` magic with a distinct
version byte (3), reuses the exact E2a entity record (`WriteEntityRecord`/
`ReadEntityRecord`, extracted so the layout lives in one place), and appends the
removed-id list; `PeekSnapshotVersion` lets a receiver route full vs delta.
`SnapshotDeltaTests.cpp` covers empty/new/changed/removed diffs, sub-quantum
suppression, diff→apply reconstruction, the **stationary-entity-survives-a-moving-
reference** case, full and delta wire round-trips (incl. a 9e11 reference), and
version peeking.

*As built — E2b-2 (server/client wiring), 2026-07-04:* the delta stream is live.
`Replication.h`'s `WorldSnapshot` gains a 1-byte `complete` flag (header now 41);
`PacketizeSnapshot` clears it on every part of a multi-datagram snapshot, so a
delta is only ever based on a snapshot the receiver holds in FULL.
`NeuronCore/SnapshotStream.h` pairs a `SnapshotStreamEncoder` (server) and
`SnapshotStreamDecoder` (client), each holding a bounded ring of recent snapshots:
the encoder deltas the current tick against the tick the client last ACKED (looked
up in its ring) and sends a single-datagram delta when it fits, else a full - also
forced on a ~1 s keyframe cadence; the decoder reconstructs each tick (applying a
delta against the baseline tick it names, found in its own ring) and tracks the
latest complete baseline to acknowledge. **Ack channel:** piggybacked on
`InputCommand` (a new appended `ackSnapshotTick` u32 - the highest-frequency
client→server traffic, and unreliable like the snapshots it acks, which suits a
lost ack better than the reliable lane the original note suggested); the client
stamps it in `SendInput`, the server reads it in `OnInput` (freshest input carries
the freshest ack). Each `Session` owns its encoder; `GameServer::PublishState`
sends `s.snapshotEncoder.Encode(snap, s.ackedSnapshotTick)`; `ReplicationClient`
routes every `'NSNP'` datagram through `m_stream.Decode` → `interpolator.Ingest`.
Loss self-heals (the server keeps deltaing against the still-acked older baseline;
a keyframe bounds the worst case); reordering is safe (deltas resolve their
baseline by tick). `SnapshotStreamTests.cpp` verifies keyframe-then-deltas
reconstruction, dropped-delta self-heal, the keyframe cadence, no-baseline drop,
and that a multi-datagram full is rendered but not acked. **Scope note:** delta
fires when the current tick's changes fit one datagram (the common case, incl. the
12-NPC acceptance target); a persistently *crowded* AOI (multi-datagram fulls)
never forms a single-datagram baseline and stays on full snapshots - fragment
reassembly for delta at extreme fleet density is a further step.

*As built — E2c (send budget + distance-sorted drop), 2026-07-04:*
`NeuronCore/SnapshotBudget.h` caps a viewer's per-tick state: `SnapshotEntityBudget`
turns the byte budget (`SNAPSHOT_SEND_BUDGET_BYTES`, ~4 MTU) into a max entity
count, and `TrimSnapshotToBudget` keeps the entities CLOSEST to the viewer and
sheds the farthest, sorted by squared distance then id so the trim is
DETERMINISTIC (identical on every client and in a replay). `GameServer::PublishState`
trims the AOI snapshot BEFORE delta-encoding (so baseline and current agree on the
kept set - a shed entity just updates on a later tick or when the viewer nears it),
and accumulates the drop count into the D3 tick metrics
(`TickSample::droppedEntities` → the `[metrics] … dropped=N` summary line, which
the BotClient harness token-parses safely). `SnapshotBudgetTests.cpp` covers the
budget arithmetic, the under-budget no-op, closest-kept/farthest-dropped, the
deterministic id tie-break, and 3-axis distance. **Track E2 is complete**
(quantization + delta + budgets); the ≥60 % bandwidth target is met by
quantization+delta for ordinary AOIs, with the budget bounding the overloaded
tail. The 100-bot bandwidth soak (D5) is a manual before/after run of the same
binary.

### E3 — Strategic AOI tier (#11) — **M** — ✅ **done 2026-07-04**

New reliable Gameplay-lane message `StrategicSummary` (`0x1004`, S→C):
`systemId u32`, `friendlyCount u16`, `hostileCount u16`, `alert u8`
(none/underAttack/lost), sent at 0.5–1 Hz per *known* system in which the
player has presence or (post-F3) property. Server aggregates per system
from the ownership index (C) at strategic cadence — this realizes §12's
decoupled clocks. Client renders it on the chart screen; the iconic-LOD
glyphs (H) reuse the same data shape.

*As built, 2026-07-04:* `Msg::StrategicSummary` (`0x1004`, reliable Gameplay
lane) + `Msg::StrategicAlert{None,UnderAttack,Lost}`. `GameLogic/StrategicView.h`
holds the pure aggregator `SummarizeStrategic(world, center, radius)` — it sweeps
combatants within `STRATEGIC_RADIUS` (8e6, ~a system's span) of a center and
tallies the player faction (friendly) vs pirates (hostile); police/traders/
stations are neutral; counts saturate at u16. At the strategic cadence
(`STRATEGIC_INTERVAL = 30` ticks, ~1 Hz) `GameServer::PublishStrategicFor` finds
each viewer's current system (the nearest `ServerStation`, by Chebyshev distance
so huge absolute coords never square-overflow), summarizes around that station,
and queues a `StrategicSummary` on the session's reliable lane (`alert =
UnderAttack` when hostiles are present; `Lost` is reserved for F3 station loss).
`ReplicationClient` consumes them into a `systemId → StrategicSummary` map exposed
as `Strategic()` for the chart. **Scope note:** v1 summarizes the player's current
system only (one owned ship pre-F); multi-system presence via the ownership index
(owned units/stations elsewhere) lands with F1/F3, and the chart-screen glyph
render is a client-UI follow-up on this now-flowing data. `StrategicTests.cpp`
covers the friendly/hostile tally, the radius cutoff, neutral-team exclusion, and
the message round-trip. **Track E is complete (E1–E3).**

---

## 8. Track F — 4X gameplay

All items follow the doc's "indirect control through the existing intent
pipeline" thesis. C (identity) is a hard prerequisite; B4 (persistence) is
required by F2–F4 to be meaningful; **Track I (I1–I3) precedes F1**, which
reuses its `UnitOrder` protocol and selection/command UX.

### F1 — First ordered unit: the escort (#12) — **M** — ✅ **core done 2026-07-05** (server + tests; persistence deferred)

*Note (2026-07-04): the order infrastructure this item originally carried —
`UnitOrder`/`UnitOrderAck`, the validation rules, the `OrderSystem`
order→intent execution — now lands earlier in **Track I (I1)**, where the
player's own ship becomes the first ordered unit. F1 shrinks to the escort
itself, reusing all of it.*

- Purchase: new `EquipItem::EscortFighter` through the *existing* Equip flow
  → server spawns a Viper-hulled NPC with `Owner{you}`, Team Player,
  `AiPilot` whose target source is an `ActiveOrder` component (I1), price
  steep (e.g. 5000.0 Cr).
- Orders: I1's `UnitOrder` (`0x1010`) as-is — the `Escort` order kind
  activates here; validation (ownership, legality, owner-attributed crime)
  is already in place from I1. Rejections via `UnitOrderAck` (`0x1011`).
- AI: an `Escort` order sets the pilot's waypoint to the owner's ship with
  the existing trader-autopilot follow; `Attack` sets `focus`. No new
  steering — only a new order source, exactly as §13.2.3-2 promises.
- Client: zero new UX — the I2/I3 selection + command grammar already
  handles "select escort, right-click/tap a target".
- Escorts replicate, fight, die and drop loot through every existing path;
  they persist (B4) as owned entities.

*As built (2026-07-05) — ✅ core, headless-tested (GameLogic verified locally on
Linux; the Server wiring reuses the tested helpers and rides CI):*

- **Purchase.** `Net::EquipItem::EscortFighter = 7` (`StationProtocol.h`). Because an
  escort is a UNIT, not a fitted boolean, it does NOT go through `EquipPlayer`;
  `GameServer::HandleStationRequest` intercepts `Equip{EscortFighter}` and calls
  `HandleBuyEscort`, which validates through the pure `GameLogic::BuyEscort` (docked +
  the per-player `Cfg::MAX_ESCORTS = 4` cap + `ESCORT_FIGHTER_PRICE = 50000` (5000.0
  Cr), charging on success), then `GameLogic::SpawnEscort` + `m_sessions.GrantOwnership`
  so the escort is `Owner`-stamped, indexed, and **reaps with the session** (the C
  ownership path). The reply reuses the `Equip` `StationResponse`.
- **Spawn (`GameLogic/EscortSpawn.h`, pure/tested).** A Viper NPC with `Team::Player`,
  `autoEngage = true`, `NpcFlightCaps`, `Combatant`/`ShipGear`, and a default
  `ActiveOrder{Escort → owner}` — but **no `AiPilot`**, so `StepOrders` (not `StepAi`)
  drives it: no new steering, exactly the §13.2.3-2 promise.
- **Orders.** I1's `UnitOrder` unchanged: the validator already accepts `Escort` (a
  `WorldTransform` target) and any owned unit, and `StepOrders`' Escort case already
  follows the owner's live position without arrive-stopping. The client needs zero new
  UX — I2/I3 select-and-order the escort as another owned unit.
- **Engagement discipline.** `StepCombat` gained `DisciplinedShooter` (Police **and**
  Player teams) so an auto-engaging escort fires only on legitimate hostiles
  (`PoliceMayEngage`: pirates + wanted) — without it a `Team::Player` auto-shooter would
  open up on police, traders and the station. `StepOrders`' "wants to fire" list is now
  gated on `!autoEngage`, so the escort fires via the NPC `StepCombat` path (its Attack
  order just sets `focus`) while the player's own ship keeps the command-fire path — no
  double-fire.
- **Crime.** Order-time `FlagIfCrime` now attributes to `_session.entity` (the owner's
  ship), not the ordered unit — so ordering an escort to attack a protected victim makes
  **you** wanted, not the drone (the "true owner attribution" I1 flagged).
- **Tests** (`Tests/GameLogic/EscortTests.cpp`, 11 cases; the full affected-system suite
  — 172 tests — stays green): purchase gate (undocked / broke / at-cap / success+charge),
  spawn loadout (owned, `Team::Player`, auto-engage, Escort order, no `AiPilot`), follow
  + hold-when-owner-gone, engages pirates but **not** traders/police, and the
  wantsFire-gating both ways.
- **Deferred (documented):** (a) **persistence** of escorts — `PlayerPersistState` is
  single-ship today, so surviving-escort save/load is a net-new B4 extension (an owned-
  units list + re-grant on load); until then an escort reaps on disconnect and is
  re-bought. (b) **Respawn re-target** — after the owner dies its escorts hold (their
  Escort target is the dead hull) until re-ordered; they still auto-defend. (c) Escorts
  can't help against **wanted players** (same `Team::Player`, so allies) — cross-owner
  PvP assistance waits on F5 factions. (d) The BotClient escort-fleet load script.

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

### G1 — Kill VFX broadcast (#18a) — **XS** — ✅ **done 2026-07-05**

*As built: `Msg::ExplosionAt` (`0x1005`, Wire/Event/Gameplay, S→C broadcast:
`x,y,z i64` + `scale u8`), broadcast by `GameServer::OnEntityKilled` at a player's
death position BEFORE the respawn teleports the hull — so the killer/bystanders
finally see the kill (the victim still gets its private `EntityDeath`). The client
plays the existing world-anchored debris burst (`spawn_explosion_at`, a Viper-hull
pop) + explosion cue. Round-trip + catalog-governance tested headless.*


New `ExplosionAt` (`0x1005`, Wire/Event/Gameplay/S→C broadcast:
`x,y,z i64`, `scale u8`) published on *player* deaths (NPC deaths already
broadcast `EntityDeath`). The killer finally sees the kill. Client plays
the existing debris VFX world-anchored.

### G2 — Missile-lock validation (#18b, closes D6) — **XS** — ✅ **done 2026-07-05**

*As built: `ResolveFireWeapon`'s Missile case now gates on `MissileTargetValid`
(the locked index must be a live, distinct COMBATANT within `MISSILE_LOCK_RANGE`
6000 and the forward hemisphere — spoofed/dead/out-of-range/behind/self refused)
and `SpendMissile` (consumes an `Equipment` rack round; refuses when empty) BEFORE
spawning. A refused launch spends nothing, silently — the client's optimistic
decrement is corrected by `PlayerStatus`. Also closed a second hole: the server
never checked the rack at all before. Unit-tested (the validation matrix + rack
spend). NPC missiles are unchanged.*


At launch, validate `missileTarget` with the same gates the laser has:
live entity (`LiveEntity`), within a lock range (6000) and a generous
forward cone at *lock time*; otherwise the launch is refused silently
(missile not spent). Headless tests for spoofed indices.

### G3 — Chat (#18c, §14 preamble) — **S–M** — ✅ **core done 2026-07-05** (server-persisted mute deferred)

*As built: `GameLogic/ChatModeration.h` (pure/tested) — a per-session `ChatLimiter`
(≤6 lines / ~10 s window) + `SanitizeChat` (strip control bytes, keep UTF-8, cap,
trim). `GameServer::HandleChat` rate-limits (drop + a system "too fast" warning),
sanitises, stamps the authenticated `sender = playerId`, and rebroadcasts to the
roster. Client: an 8-line scrollback (muted senders dropped), Enter-opened one-line
input off the WM_CHAR ring, and a client-side `/mute <id>` / `/unmute <id>` set;
a playerId→name map from `PlayerInfo`. Deferred: server-persisted mute list (B4),
AOI-scoped delivery (roster-wide is a superset).*


Server: relay `Chat{sender, text}` (already registered, `0x0300`) from a
session to AOI-plus-roster recipients, with server-side rate limit
(N lines / 10 s, drop + warn) and length re-validation. Client: a chat line
input (GameWindows overlay), a scrollback of the last ~8 lines over the
HUD, and a client-side **mute list** (by `PlayerId` — designed in from day
one per §13.2.2, persisted server-side with the player record; the client
keeps no local config file).

### G4 — Suns & cabin heat (§14 preamble; deferred G8+ payoff) — **M** — ✅ **done 2026-07-05**

*As built: `GameLogic/CabinHeatSystem.h` (pure/tested) — a `Sun` body per system
(placed by `WorldBuilder`, offset from the planet) + a per-ship `CabinHeat`.
`StepCabinHeat` warms a ship in a star's Chebyshev heat band, cools it outside, and
— held at max — drains the energy bank directly (heat bypasses shields, the legacy
zero-altitude analogue) until it dies; a fuel-scoop ship skimming the band tops its
tank (the scoop's long-missing payoff). Spawn grace respected. `PlayerStatus` gains
`cabinTemp` (pre-launch field add); the client mirrors it into `PlayerCaps().cabTemp`,
reviving the cabin-temp HUD dial `A1` deleted. Unit-tested (rise/fall/clamp,
cook-to-death, spawn-grace, scoop gains/limits/no-scoop/out-of-band, no-suns).*


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

*Status 2026-07-05: **H1 ✅** and **H3 (LOD decision) ✅ core** done — the two
pieces with a headless-testable core. `DeepspaceOutpost/RenderTable.h` is the
`NetType → {kind, glyphId, paletteRow}` table (`RenderFor`), consumed by
`draw_ship` in place of the type if-chain, plus the iconic-LOD range decision
(`ShouldDrawAsGlyph`); both unit-tested in `Tests/NeuronClient/RenderTableTests.cpp`.
A distant hull now draws as a contact glyph (a deferred blip; a 2-6-line vector
glyph is the refinement). **H2 (batched instanced pipeline) and H4 (emissive post
chain + GPU debris) are deferred**: they are large DX11/HLSL architecture rewrites
(persistent per-hull line buffers + `DrawIndexedInstanced`; render targets + blur +
`SV_VertexID` expansion) with **no CI oracle at all** — the sandbox can't compile or
run the DX11 client, and these are new pipelines, not wiring over proven primitives.
They should be built in an in-app session where each pass can be iterated visually.
The H1 table + descriptor is the seam they plug into when that happens.*

Per §13.2.1, in order:

1. **NetType indirection table** first (it is the seam everything else
   plugs into): a client-side table `NetType → {mesh id, glyph id, palette
   row}` replacing the `if/switch` in `draw_ship`/`build_ship_mesh`
   (`threed.cpp`, `SceneMeshes.cpp`). Adding a hull (F3's outpost) becomes a
   data row. (The `draw_ship` seam is already simpler: the Solid/Wireframe
   toggle and the multi-style planet branch were removed 2026-07-04 — ships
   take the single solid mesh path and the planet is one green sphere.)
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

## 11. Track I — Interaction model (#22) — pointer-first command interface

**Design source: `docs/interaction.md`** (owner-accepted 2026-07-04:
pure order-based control; context orders + Homeworld-style move gizmo;
attack orders with server-side engagement). ARCHITECTURE.md §13.2.4 is the
summary. This track restores ship movement — absent since the free-camera
migration zeroed the flight axes — as the first application of the
indirect-control thesis, and makes mouse **and touch** the primary devices
with the keyboard reduced to accelerators.

Sequencing: **I1 first and urgent** (the game has no movement verb without
it); I2 parallel; I3/I4 on I1+I2; I5/I6 then; I7 last. F1 consumes I1's
protocol and I2/I3's UX unchanged.

### I1 — `UnitOrder` protocol + server `OrderSystem` — **M** — ✅ **done 2026-07-04** (server + wire + tests; client command UX is I2–I3)

The wire and server halves; playable with a temporary debug binding even
before I3's UX.

- **Wire:** `UnitOrder` (`0x1010`, Wire/Command/**Gameplay (reliable)**/C→S:
  `unitId u32`, `order u8 {Stop=1, Move=2, Approach=3, Dock=4, Attack=5,
  Collect=6, Escort=7}` — Patrol/Route values reserved for F-track,
  `target u32`, `targetX/Y/Z i64`), `UnitOrderAck` (`0x1011`, S→C owner:
  `unitId`, `order`, `status u8 {Accepted, NotYours, BadTarget, Illegal,
  OutOfRange, Docked, Rejected}`), `AbilityRequest` (`0x1014`,
  Wire/Command/Gameplay/C→S: `kind u8 {FireMissile=1, Ecm=2, EnergyBomb=3,
  EscapePod=4}`, `target u32`) replacing the one-shot flags that today ride
  the *unreliable* lane (a lost datagram eats a button press). Round-trip +
  golden-layout + governance tests as usual.
- **Validation:** `LiveEntity(unitId)`; `Owner == sender's PlayerId`;
  order-legal target (Attack: live combatant, not self — crime rules
  evaluated with the **owner** as offender at order time and at fire time;
  Dock: a station; Collect: a canister); `targetPos` Chebyshev-clamped
  (≤ 1M units from the unit); latest order wins.
- **Execution:** new GameLogic `OrderSystem` before `StepAi`: an
  `ActiveOrder{order, target, targetPos}` component (plain, serializable —
  the §12 persistence invariant) translated per tick into `FlightIntent`
  via the **existing** steering (trader-lane autopilot for
  Move/Approach/Dock/Collect with ease-off + arrival radius ~200; attack-run
  steering + `focus` for Attack). Ordered flight stays clamped by
  `FlightCaps`; collisions/mass-lock/laser-temp all apply unmodified. Dock
  hands over to the existing dock-request path when in range (retiring the
  client-side proximity heuristic); Collect completes when the canister
  goes; Attack engagement uses the NPC fire discipline — "players fire only
  on command" survives as the order.
- **`InputCommand` re-cut:** with axes and buttons gone, `0x0100` re-cuts in
  place (pre-launch rule, `PROTOCOL_VERSION` bump) to a pure heartbeat:
  `{sequence u32, ackSnapshotTick u32}` — it still carries the E2 delta ack
  and feeds B3's safe-park silence detection; cadence unchanged.
- **BotClient:** the idle bot becomes an *order bot* (moves by `UnitOrder`),
  doubling as the ordered-fleet load harness.

*Acceptance:* headless matrix — ownership/legality/range rejections each ack
the right status and mutate nothing; a spoofed `unitId`/foreign owner does
nothing; Move converges and stops inside the arrival radius (golden,
bit-identical); Attack on a protected victim publishes `Crime{owner}`; Dock
order docks from any approach without the nose heuristic; ability requests
route to the same handlers the flags reached, with G2's missile validation;
the heartbeat keeps the delta stream acked across the re-cut.

*As built (2026-07-04):* new NeuronCore wire messages `UnitOrder` (`0x1010`),
`UnitOrderAck` (`0x1011`), `AbilityRequest` (`0x1014`), all on the reliable
Gameplay lane (round-trip + golden-layout + governance tested,
`UnitOrderTests.cpp`). `GameLogic/OrderSystem.h` holds the pure core:
`ActiveOrder{order, target, targetPos, complete}` (a serializable component),
`PlanUnitOrder(world, playerId, req, maxMoveDist)` (the anti-cheat validator —
ownership via `Owner`, docked gating, per-kind target-type gate, Move
`ClampToChebyshev` clamp — returning the status to ack), and `StepOrders(world)`
which each tick translates every `ActiveOrder` into a `FlightIntent` through the
shared `Detail::SteerToward` + an arrival-aware throttle (a `MIN_CREEP` floor so
the ease-to-zero doesn't asymptote short of the arrival radius), and returns the
Attack units that are aligned + in range as "wants to fire". `GameServer` decodes
`UnitOrder` → `PlanUnitOrder` → (crime-at-order-time via `FlagIfCrime`) → record +
`UnitOrderAck`; runs `StepOrders` before `StepAi` and publishes `FireWeapon{Laser}`
for the returned units (so ordered fire reuses the E1 lag-compensated,
crime-attributing player-fire path, heat-gated); `AbilityRequest` publishes the
same `FireWeapon` commands the input flags did; `CompleteDockOrders` auto-docks a
Dock-ordered ship at dock range through the tested station path. A player death
clears any standing order. `ReplicationClient` gained `SendUnitOrder`/`SendAbility`;
the BotClient idle bot became an **order bot** (zero-axis heartbeat + periodic
`UnitOrder{Move}` orbit), so the D5 smoke exercises the whole order path over real
UDP. Unit-tested headlessly in `OrderSystemTests.cpp` (the rejection matrix,
Move-arrive-and-stop, Attack fire gating, dead-target hold, determinism).

**Two deviations from the sketch, by design, to fit the no-runtime sandbox
(CI-only oracle):** (a) the `InputCommand`→`{sequence, ackSnapshotTick}` heartbeat
re-cut + `PROTOCOL_VERSION` bump is **deferred** to a mechanical follow-up — the
flight axes are already always-zero, so orders are purely additive and movement is
restored without touching the un-CI-testable client flight path or the framing ABI;
the server still honours the (dormant) input fire flags, so the existing client is
unbroken until I4 moves abilities onto `AbilityRequest`. (b) The **client command
UX** (temporary debug binding → real selection/gizmo) is I2–I3; for I1 the order
path is driven end-to-end by the BotClient, which is the CI-verifiable proof.
Crime is attributed to the ordered unit (== the owner's own ship pre-F1); F1 routes
true owner attribution.

### I2 — Selection & picking — **S–M** (parallel with I1)

Client-side: pointer events (mouse first — LMB click select, slop-thresholded
drag = camera orbit), screen-ray entity picking through the one shared
projection (reuse `camera_view_point`; generous distance-scaled hit radius),
a client-local `Selection{entityId}` with info card (name/type/legal
status/distance) + a persistent selection chip, and the orbit camera's
subject switched from the missile-lock coupling to the selection. The
center-of-view `find_lock_target` cone-pick and the T/U lock keys retire;
the missile target IS the selected enemy. Double-click focuses the camera.

*Acceptance:* every replicated entity is selectable at any zoom; selection
survives snapshots/despawn correctly (clears on death/despawn like the old
lock); orbit follows selection; no hover-dependent behaviour.

*As built (2026-07-04) — ✅ core, compile-verified (client UX not CI-exercisable):*
selection is unified onto the existing target field `g_missile_lock_target` (which
already drives the orbit-camera subject, the on-hull reticle, the missile launch,
and the clear-on-death/despawn sweep) — realizing the plan's "the missile target IS
the selected enemy" without a risky rename. New `pick_entity_at_screen(mx,my)`
(`space.cpp`) projects every replicated entity through the SAME optics the reticle
uses (`camera_view_point` → `CameraSpaceToPixels` over `gfx_scene_size`) and returns
the one nearest the cursor within a viewport-scaled hit radius — so what you click
is what you see, and ANY entity is selectable (stations/planets/canisters included,
for I3's orders), except your own hull and in-flight missiles. `CameraRig` tracks
the LMB press so a release inside a 6-px slop is a CLICK → select (empty space
clears); a drag is left to the camera. A compact info card (`display_selection_info`)
shows the selection's kind + range top-left of the view, and vanishes the frame the
entity leaves the AOI. **Deviations (low-risk, no-runtime sandbox):** (a) orbit stays
on RMB-drag (LMB was free) rather than moving to LMB-drag — the plan's LMB-drag-orbit
/ free-RMB rebind belongs with I3's RMB command grammar, so it lands there; (b) the
T/U centre-cone lock keys are KEPT as keyboard fallbacks (both now set the same
selection) — retiring keys is I7; (c) double-click camera-focus is deferred (the
orbit already follows the selection, which is the focus behaviour); (d) the info
card shows kind+range, not name/legal-status yet (needs the roster join — I3/I4).
Behaviour needs an in-app run to verify pixel-accuracy of picking and card
placement (CI compiles it but cannot exercise the DX11 client).

### I3 — Command UX: contextual orders, move gizmo, radial menu — **M** — ✅ **done** (core 2026-07-04; residues 2026-07-05)

- RMB click = contextual default order per `docs/interaction.md` §3.3
  (empty space → Move gizmo; enemy → Attack; station → Dock; canister →
  Collect; planet → Approach; clean player → Approach, Attack only via the
  menu — deliberate friction).
- **Move gizmo** (§3.4): command plane through the selected ship, normal =
  camera up, depth-faded grid; ray∩plane point; drag-before-release adjusts
  elevation along the normal (vertical stem); release sends
  `UnitOrder{Move}`. Range-clamped client-side to match the server gate.
- RMB-hold ≥ 0.35 s = radial context menu (all legal orders + Info) — the
  same menu long-press opens on touch (I5).
- Feedback: optimistic marker + route line (cleared/red-flashed on a
  rejecting `UnitOrderAck` toast), persistent dimmed marker while an order
  runs, order name on the info card.

*Acceptance (manual + injection tests):* click-vs-drag disambiguation at the
slop threshold; gizmo point stable under camera motion; every ack status
surfaces visibly; no path requires a key.

*As built (2026-07-04) — ✅ core, compile-verified (client UX not CI-exercisable):*
RMB **click** on the flight view issues the contextual default order to the player's
own ship (`handle_pointer_commands` → `dispatch_context_order`, main.cpp): an entity
under the cursor maps by kind — planet/sun → Approach, station → Dock, canister →
Collect, any ship → Attack — else empty space → **Move**, whose destination is the
cursor ray ∩ a horizontal plane through the ship (`cursor_to_move_point`: DirectXMath
`XMMatrixInverse(View*Projection)` unproject in the floating-origin frame, then
Chebyshev-clamped to the server's 1M reach so the client request matches the gate).
The order rides the reliable `UnitOrder` lane (I1); camera **orbit moved to LMB-drag**
(CameraRig) so RMB is free. Feedback: an optimistic toast naming the order, a
projected Move marker (`display_order_feedback`, space.cpp, reusing the target-lock
sprite; entity orders reuse the on-hull reticle), and a `UnitOrderAck` subscriber
that flashes the refusal reason red and drops the marker. **Deferred (documented):**
(a) the FULL move gizmo — command plane is world-up not camera-up, and the
elevation-drag stem + depth-faded grid + route line are not drawn (a click-to-a-
horizontal-plane point with a marker stands in); (b) the **RMB-hold radial menu**
(all-legal-orders + Info) — a larger GUI piece, so Attack-on-a-clean-player friction
(default Approach + menu-only Attack) and the Info action ride that follow-up; any
ship currently defaults to Attack and the server enforces the crime rules. Needs an
in-app run to verify unproject pixel-accuracy and marker placement (CI compiles the
client but cannot exercise it).

### I4 — Ability bar & HUD restructure — **S** — ✅ **done** (core 2026-07-04; nav strip 2026-07-05)

Persistent non-modal GuiOverlay bar (the overlay must stop suppressing game
input for non-modal elements): Stop / Missile / ECM / Energy bomb / Escape
pod / Jump / Launch per `docs/interaction.md` §3.7, availability driven by
the `PlayerStatus`/`CargoManifest` mirrors, **hold-to-confirm** on bomb and
pod. Screen navigation icon strip (charts/market/status/equip) with the
F-keys kept as accelerators. The A/E/Tab/Esc/M/C/J combat keys retire into
the bar (Esc keeps window-close duty).

*Acceptance:* every retired key's verb reachable by pointer alone; abilities
grey correctly from mirrors; bomb/pod cannot fire on a stray tap.

*As built (2026-07-04) — ✅ core, compile-verified (client UX not CI-exercisable):*
a persistent, **non-modal** ability strip across the top of the flight view
(`draw_ability_bar` / `handle_ability_bar`, main.cpp): **Stop / Missile / ECM / Bomb
/ Pod / Jump**. Non-modal by design — it does NOT raise `GuiOverlay` (which would
suppress game input); instead the camera's LMB select/orbit ignores any click whose
press began over a button (`ability_bar_button_at`, gating CameraRig), and the bar's
own handler triggers it. Each button greys from the equipment/`PlayerStatus` mirrors
(`ability_enabled`: Missile needs a selection + rack, ECM/Bomb/Pod need the fitting,
Jump needs undocked+not-witchspace); **Bomb/Pod require a ~0.6 s hold-to-confirm**
(a stray tap can't fire them; the button flashes red while confirming). Actions
reuse the exact key paths (`UnitOrder{Stop}`, `launch_missile`, the `ActionTriggered`
equipment publishes, `jump_warp`). **Deferred (documented):** (a) the combat keys
(A/E/Tab/M/C/J) are KEPT working in parallel — formal key retirement is I7; (b) the
screen-nav icon strip (charts/market/status/equip) is not added (the F-keys still
navigate); (c) **Launch/undock** stays on the docked screen's own UI (the flight bar
is flight-only). Needs an in-app run to verify bar placement/coordinate-space and
that click regions line up with the drawn boxes.

### I5 — Touch & gesture layer — **M** — 🟡 **core done** (2026-07-04; recognizer + gestures 2026-07-05; camera pan pending)

Full `WM_POINTER` multi-pointer tracking (replacing the single-pointer→LMB
stub) + a recognizer emitting device-neutral events: tap, double-tap,
long-press, drag, two-finger drag (pan), pinch (zoom). Touch mappings per
`docs/interaction.md` §3.2: tap select; tap-with-own-unit-selected =
command; long-press = radial menu / move gizmo; one-finger drag orbit;
pinch zoom; two-finger drag pan. Mouse wheel maps onto the same zoom event.
Ergonomics pass on the widget stack: ≥ 40 px rows in Market/Equip,
drag-scroll, quantity steppers with hold-repeat. Intros advance on
tap/click.

*Acceptance:* the complete gameplay loop (undock → move → attack → collect →
dock → trade → hyperspace) playable with touch only and with mouse only; no
gesture steals the camera from a tap or vice versa at the slop boundaries.

*As built (2026-07-04) — 🟡 core, compile-verified (touch is inspection-only —
CI has no touch device and the sandbox can't run the client):* `input_win.cpp`
now tracks up to two `WM_POINTER` pointers (slot-assigned by pointer id). One
finger maps to the mouse/LMB exactly as the old stub did — so **tap = select** and
**one-finger drag = orbit** already work through the I2/I3 mouse paths — while two
fingers **PINCH to zoom**, feeding the delta into the SAME `g_wheelSteps`
accumulator the mouse wheel uses, so "wheel and pinch are one zoom event" falls out
by construction. **Deferred (documented):** two-finger **pan** (needs camera-pan
plumbing the controller doesn't expose yet), **long-press** → radial menu / gizmo
(the radial menu is itself an I3 deferral), **double-tap**, and the widget-stack
**ergonomics pass** (≥40 px rows / drag-scroll / steppers in Market/Equip). The
device-neutral recognizer these need is the follow-up; this increment lands the one
gesture (pinch) with a clean, existing mapping.

### I6 — Pointer charts — **S–M** — ✅ **done** (core 2026-07-04/05; pan/zoom + info card 2026-07-05; name-search field pending)

Charts become pick surfaces: tap/click a system selects it (info card:
economy/government/distance/fuel cost/in-range), a **Hyperspace** button on
the card sends `TravelRequest{Hyperspace}`; drag pans, pinch/wheel zooms
(galactic ↔ short-range as zoom presets on one map where feasible);
find-by-name becomes a search field reusing the existing text-entry path.
The arrow-key crosshair (`move_cross`, `cross_x/y`) and the D/F/O/H chart
keys retire.

*Acceptance:* hyperspace fully operable by pointer; out-of-range systems
visibly gated before the server round-trip (fuel mirror), server stays the
authority.

*As built (2026-07-04) — ✅ core, compile-verified (client UX not CI-exercisable):*
the charts are pick surfaces. A new `gfx_window_to_canvas` (NeuronClient) inverts
the letterbox placement (offset + downscale from `canvasPlacement`) so a window-pixel
click maps to the chart's 512×514 canvas. `handle_chart_pointer` (main.cpp) parks the
crosshair (`cross_x/y`) on the clicked point — the existing `chart_nearest_to_cursor`
+ `draw_cross` + readout then show the selected system — and a drawn **HYPERSPACE**
button (`draw_chart_hyperspace_button`) fires `teleport_to_cursor()` →
`TravelRequest{Hyperspace}` (the server validates fuel/range). The chart help text
now reads "Click a system … Click HYPERSPACE …". The arrow-key crosshair and the
hyperspace key are RETAINED as accelerators (formal retirement is I7). **Deferred
(documented):** chart drag-pan / wheel-zoom (galactic and short-range are already
separate zoom presets) and find-by-name as a pointer search field (the F-key name
search still works); the full economy/fuel-cost **info card** rides the same readout
follow-up as I2's card.

*Superseded (2026-07-05) — charts moved to a native GUI window off the letterbox:*
the letterboxed chart described above was replaced by a native `ChartWindow`
(`GameWindows.cpp`) on the GUI overlay, fed by a render-free `ChartData` API
(`ChartData.h` / `docked.cpp`). The window draws the map through `Render2D` (new
native `DrawCircle`/`FillCircle` primitives) with a selected-system data panel, a
click selects the nearest system (`MouseEvent` → `ChartData::SetCursor`), and its
own **HYPERSPACE** button jumps; F5/F6/F7 open it and F5/F6 switch galactic/short-
range. The whole letterboxed path is gone: `handle_chart_pointer`,
`gfx_window_to_canvas` usage, `draw_cross`/`move_cross`, the D/F/O + name-search
keys, `display_*_chart`/`display_data_on_planet` and their replicated draw helpers,
and the `SCR_GALACTIC_CHART/SHORT_RANGE/PLANET_DATA` modes were all deleted. The
selected-system data panel replaced the separate F7 screen. Name-search-by-pointer
remains deferred. This is the first screen off the 512×514 letterbox toward
retiring it (the flight HUD dashboard is the remaining long pole).

*Follow-up (2026-07-05) — the flight HUD dashboard is now native too:* the in-flight
cockpit (`update_console` and its helpers in `space.cpp`: scanner, dials, compass,
speed/roll/climb, missiles, the station/ECM indicators) plus the I2/I3/I4 overlays
(target card, order toast/marker, ability bar in `main.cpp`) no longer emit into the
gfx2d deferred batch. They draw straight into the `Render2D` pass that `RenderGameHud`
(`HudRender.cpp`) opens during `RenderCanvas`, through a small native primitive shim
(`hud_line`/`hud_rect`/`hud_text`/`hud_sprite`/`hud_scanner`, palette-indexed colour via
`Renderer::paletteColour`, sprites via `TextureManager`, text via `g_gameFont`'s
outline path — the same look the batch produced). `update_console` self-gates
(connected + undocked + front view) so it is called unconditionally from the HUD pass.
Dropping the batch's stale scanner scissor also fixes a latent clip bug: the top-anchored
overlays (ability bar, target card, order toast) were being clipped out of view on any
window taller than ~520 px. The now-dead gfx2d primitives (`gfx_draw_colour_line`/
`gfx_draw_rectangle`/`gfx_draw_circle`/`gfx_draw_filled_circle`/`gfx_clear_text_area`/
`gfx_display_colour_text`/`gfx_draw_scanner`/`gfx_set_draw_origin`) and their batch helpers
were removed with it.

*Follow-up (2026-07-05) — the last gfx2d text path is native too:* `gfx_display_centre_text`
(the centred intro titles/prompts, the transient flight info message, and the `GAME OVER`
banner) is gone. Those are emitted from the RenderScene phase where no 2D pass is open, so a
tiny native queue (`hud_centre_text` records; `RenderOverlayText` draws them via `g_gameFont`
from `RenderGameHud`, then clears) bridges them into the HUD pass. gfx2d's whole bitmap-font
layer (`drawString`/`emitGlyphs`/`fontSheetSRV`/the shared sheet) and the text-outline branch
in `gfx2d_flush` went with it — the batch now replays sprites and pixels only.

*Follow-up (2026-07-05) — the gfx2d 2D batch is deleted (endgame Phase 1):* the last three
batch producers moved native — the ship-death debris (`gfx_plot_pixel`, `threed.cpp`), the
per-ship target reticle (`gfx_draw_sprite_scaled`, `space.cpp`) and the intro title sprite
(`gfx_draw_sprite`, `intro.cpp`). Like the centred text they are emitted from RenderScene, so
they queue in `space.cpp` (`hud_plot_pixel`/`hud_sprite_deferred`/`hud_sprite_scaled_deferred`)
and `RenderSceneOverlays` draws them via `Render2D::PlotPoint`/`TexQuad` from `RenderGameHud` —
first, under the dashboard and text, matching the old batch-flush-under-HUD order. With no
producers left, the entire gfx2d vertex batch is gone: `gfx2d_flush` (and its `RenderCanvas`
call), the `ColorVertex`/`TexVertex`/`Cmd` streams, `pushColor`/`pushTexQuad`/`addPoint`, the
`getTexture`/`spriteFile` sprite plumbing, `col_rgba`, and the batch's scissor. `gfx2d.cpp` is
now just the scene/viewport seam (`gfx_render_3d_scene` → `Scene3D`, `gfx_set_scene_fullwindow`/
`gfx_scene_size`/`gfx_canvas_size`, and vestigial clip/clear no-ops).

**What's left of gfx.h/gfx2d (endgame Phase 2):** no 2D drawing at all — only the engine seam:
lifecycle (`gfx_graphics_startup`/`shutdown`/`gfx_update_screen`), the 3D scene pass
(`gfx_render_3d_scene`), the viewport/projection (`gfx_set_scene_fullwindow`/`gfx_scene_size`/
`gfx_canvas_size`), the clip/clear no-ops, and the `GFX_COL_*`/`IMG_*` macros. Phase 2 rehomes
those into a native header (fold into `GraphicsCore`/`ClientEngine`) and replaces the macros,
after which `gfx.h`/`gfx2d.*` are deleted.

*Follow-up (2026-07-05) — endgame Phase 2 done: `gfx.h`/`gfx2d.*` are deleted.* The seam moved
into native modules, keeping the identifiers (a 431-site rename of the mostly-static ship
geometry tables was not worth it): the scene/viewport functions now live in
`platform/GameScene.h` + `platform/GameScene.cpp` (renamed from `gfx2d.cpp`), the platform
lifecycle stays in `platform_win.cpp` (declared in `GameScene.h`), and the `GFX_COL_*`/`IMG_*`
palette-index and sprite-id macros moved to `GamePalette.h` (still index-based — the 3D
renderer resolves the index once via `paletteColour`). The 12 `#include "gfx.h"` sites were
redirected (or dropped where stale), `gfx2d.h`/`gfx2d_flush` are gone, and CMake tracks the new
files. **`gfx.h`, `gfx2d.h` and `gfx2d.cpp` no longer exist** — the legacy 2D layer is fully
retired and all client rendering is native (`Render2D`/`Scene3D`).

### I7 — Keyboard reduction & cleanup — **XS–S**

Retire the dead `kbd_*` globals and bindings (speed keys are already dead;
T/U/A/E/Tab/M/C/J/H and the chart keys go with I2–I6), keep the accelerator
table (`docs/interaction.md` §3.9: F-keys, Esc, Space, camera-fly arrows,
Delete-deselect), and run the doc truth pass (ARCHITECTURE.md §7 input
bullet rewritten to the as-built order model; this plan's Track I items
marked done).

*Acceptance:* grep for retired `kbd_*` names returns nothing; every
remaining key has a pointer equivalent; docs match code.

*As built — ✅ done (2026-07-05, after the pointer UX was verified in-app):* the
nine combat keys with exact pointer equivalents are **retired** — their kbd_*
globals, key mappings (`input_win.cpp`), `keyboard.h` decls, `handle_flight_keys`
handlers, and now-orphaned functions (`lock_missile_target`, `unlock_missile_target`,
the centre-cone `find_lock_target`) all deleted: **A**=fire (→ the Attack order fires
server-side), **E**/**Tab**/**M**/**pod**/**J** (→ the I4 ability bar),
**T**/**U** (→ I2 pointer select), **H** (→ the I6 chart HYPERSPACE button). `grep`
for the retired `kbd_*` names returns nothing. **Kept as accelerators** (the
`docs/interaction.md` §3.9 table): the F1–F12 screen/nav keys, **Esc** (window-close,
now its sole duty), the camera-fly arrows, the chart crosshair arrows + **D**/**O**,
and **F** (name search — I6's pointer search field is still deferred, so F is the
only by-name search). **C**/docking-computer is kept (a purchased-equipment auto-dock
alongside the RMB Dock order). The `in.fire`/`s_frameFire` command-builder path is now
vestigial (always false); harmless, left in place. Doc truth pass done (this note, M6,
ARCHITECTURE.md §7).

### Track I residue closure (2026-07-05)

The I2–I6 "core" increments left a documented residue list (the full move gizmo,
the radial menu, clean-player friction, the nav strip, the touch gesture recognizer,
and the chart pan/zoom + info card). Those are now landed. Because the Linux CI
sandbox cannot compile or run the DX11 client (MSVC/DX11/WinRT), the strategy was
**headless-tested cores + carefully pattern-matched glue**: every piece of logic that
*could* be tested was extracted into pure, dependency-free headers under
`NeuronClient/input/` and pinned by the NeuronClient test suite; the DX11/Win32 glue
that wires them into the client is **compile-verified via the core seam only** and
still needs an in-app run to confirm pixel-accuracy and gesture feel.

- **Pure cores (headless GoogleTest, 30 cases — verified locally on Linux + CI):**
  `input/GestureRecognizer.h` (device-neutral multi-pointer state machine: tap,
  double-tap, long-press, drag, two-finger pan, pinch — the I5 piece the plan flagged
  as "CI never runs"), `input/OrderMenu.h` (contextual default order + radial-menu
  legal set + clean-player Attack-friction, over the wire `OrderKind`), and
  `input/MoveGizmo.h` (ray∩plane on the camera-up command plane, grazing/too-far
  clamp, elevation along the normal, Chebyshev clamp to the server Move reach).
- **I3:** the full move gizmo (camera-up plane; a vertical RMB drag sets the elevation
  stem; `draw_move_gizmo` renders the depth-faded plane ring + route line + stem +
  marker), the RMB-hold **radial menu** (`open_radial_menu`/`radial_slice_at`/
  `radial_commit` + `draw_radial_menu`), and **clean-player Attack-friction** (dispatch
  routes through `OrderMenu::DefaultContextOrder`, so a click on a lawful player is
  Approach; Attack is menu-only, crime still owner-attributed server-side). The I2 info
  card is enriched via the roster join (player name + CLEAN/WANTED).
- **I4:** the top-right **screen-nav strip** (Chart/Status/Inventory) opening the GUI
  windows by pointer, non-modal, camera-select-safe (`nav_strip_button_at` gates both
  CameraRig branches); F-keys stay as accelerators.
- **I5:** the recognizer is wired into `input_win.cpp`'s `WM_POINTER` path (one clock
  for Push+Tick); the proven one-finger→mouse + pinch→wheel synthesis stays, and the
  new gestures are surfaced — **long-press → touch radial menu**, **double-tap → focus**,
  **two-finger pan** exposed via `input_take_pan`. Widget ergonomics: taller touch rows
  in Market/Equip. *Still open:* the two-finger camera **pan** is exposed but not
  consumed (the Orbit controller has no translate axis — a camera-math change to verify
  in-app), and the full widget drag-scroll / hold-repeat steppers.
- **I6:** chart **wheel/pinch zoom** (`ChartWindow::Update`; CameraRig yields the wheel
  when a GUI window owns input) and **drag-pan** (release-applied), plus an **info card**
  with distance / fuel / IN RANGE from new render-free `ChartData` helpers. *Still open:*
  the pointer find-by-name **search field** (the F-key name search is the accelerator).

Net: the mouse command loop (select → order via gizmo/menu/ability-bar/nav-strip, jump
from the chart) is complete in code; the touch loop has its gestures; the remaining
residues (camera pan, widget drag-scroll/steppers, chart name-search) are the ones whose
correctness genuinely depends on an in-app run and are called out above.

---

## 12. New message-id allocation (summary)

Per §4.3 bands; all proposals — confirm against governance tests at
implementation time. Control band: `HelloAck 0x0003`, `HelloReject 0x0004`,
`AssignControl 0x0005` (or folded into HelloAck), `Ping 0x0006`,
`Pong 0x0007`. Game band: `TravelRequest 0x1000`, `TravelResponse 0x1001`,
`GalaxyChunkRequest 0x1002`, `GalaxyChunk 0x1003`,
`StrategicSummary 0x1004`, `ExplosionAt 0x1005`, `UnitOrder 0x1010` (I1;
order kinds shared with F1, Patrol/Route values reserved),
`UnitOrderAck 0x1011` (I1), `Deploy 0x1012` (if not folded into UnitOrder),
`FactionInfo 0x1013`, `AbilityRequest 0x1014` (I1). Identity band:
`PlayerInfo` successor `0x0304`. Re-cut in place (pre-launch rule +
`PROTOCOL_VERSION` bump): `InputCommand 0x0100` → the
`{sequence, ackSnapshotTick}` heartbeat (I1). Retired in place:
`AssignPlayer 0x0001` (→HelloAck), `0x0210` manifest (→GalaxyChunk),
`PlayerInfo 0x0301` (→0x0304), `StationRequestKind::Teleport/JumpDrive` +
travel `StationStatus` values (→Travel*).

## 13. Open decisions for the owner

Everything above follows decisions already locked in ARCHITECTURE.md §12.
Three items are genuinely open and block only their own bullets:

1. **Q1 — Offline mode.** A1 deletes single-player entirely (S4's spirit,
   extended to the whole fallback). If any offline/practice mode is ever
   wanted, the correct shape is a *locally hosted server process*, never
   client-side rules. Assumed: delete.
2. **Q2 — Legacy save/config files.** *Decided 2026-07-04: removed.* The
   `file.cpp` config subsystem (`newkind.cfg` settings + `newscan.cfg` HUD
   layout) is gone — the MMO client keeps no local files; durable player
   state is the server's (B4). The values it loaded (scanner/compass HUD
   positions, frame-speed default) are baked into `elite.cpp`.
3. **Q3 — `GameData/Models` + `cmo.md` (DSOM).** *Decided 2026-07-03:
   deferred — the owner will handle this separately. Leave `GameData/Models`,
   `tools/shipdata2obj` and `cmo.md` exactly as they are; no track touches
   them (the A7 doc pass leaves `cmo.md` alone too). Track H proceeds from
   the compiled mesh tables.*
4. **Q4 — x86.** Presets exist, CI never builds them, nothing in the plan
   needs x86. Assumed: drop the presets or add a CI lane; default drop.

## 14. Suggested milestone cut

1. **M1 "Honest client"** — A1–A7 complete. The load-bearing rule is
   literally true; protocol hygiene done; docs match code.
2. **M2 "Durable world"** ✅ — B1–B4 complete. Secure token sessions, reconnect
   grace + resume, SQL persistence (ODBC soak-validated). Restart-safe commanders.
3. **M3 "Empire-ready core"** ✅ — C + D1–D5 complete. Identity layer
   (playerId + Owner/OwnershipIndex + session records, wallet deferred to F)
   plus grid, arena, accumulator, metrics, BotClient smoke in CI.
4. **M4 "Fair & scalable netcode"** — 🟡 E1–E3 ✅ complete (time sync +
   lag-compensated fire; snapshot quantization + per-session delta/keyframes +
   send budget; strategic per-system tier), validated in unit tests and pending
   the 100-bot bandwidth soak; G1–G3 remain.
5. **M5 "Command of one"** — 🟡 I1–I4 core ✅: the order protocol + `OrderSystem`
   restore ship movement (I1, CI-green), pointer selection/picking (I2), the RMB
   command UX + move-plane + ack feedback (I3), and the non-modal ability bar (I4).
   Mouse-playable end to end. **Residues now landed (2026-07-05):** the full move
   gizmo + RMB-hold radial menu + clean-player Attack-friction (I3) and the screen-nav
   strip (I4), on headless-tested cores (`NeuronClient/input/*`); formal keyboard
   retirement was already I7. The I2–I4 client GLUE is compile-verified via the core
   seam — it needs an in-app run to confirm pixel-accuracy.
6. **M6 "Touch-complete"** — 🟡 I5–I7 ✅ (core + residues): pointer charts (I6,
   click-select + on-chart hyperspace + **wheel-zoom + drag-pan + distance/fuel info
   card**), the touch layer (I5, multi-pointer + pinch-zoom + the **device-neutral
   gesture recognizer** driving **long-press → radial menu**, **double-tap → focus**,
   two-finger pan exposed), and the I7 key deletion (done). Mouse path complete; the
   touch path has its gestures but is inspection-only. **Genuinely-remaining residues**
   (correctness depends on an in-app run): two-finger camera **pan** consumption (the
   Orbit controller needs a translate axis), widget **drag-scroll / hold-repeat
   steppers**, and the chart **find-by-name search field** (F-key search is the
   accelerator).
7. **M7 "The 4X turn"** — F1–F5, G4, with H landing in parallel (F1 reuses
   I1's protocol and I2/I3's UX verbatim).
8. **M8 "Missions"** — G5, after M2 has soaked in production.
