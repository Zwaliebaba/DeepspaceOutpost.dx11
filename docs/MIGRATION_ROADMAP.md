# DeepspaceOutpost → MMO Migration Roadmap

Target: turn the single-player *Elite: The New Kind* port into a
**server-authoritative MMO** supporting **up to 100 concurrent players** as a
first milestone.

> **Sequencing rule (per project owner):** *modernize/decouple the client
> first*, **then** split logic between client and server. Phases A → B reflect
> that explicitly.

---

## Implementation status — last updated 2026-06-28

The client decouple **and** the server split are both done: the game now runs
**fully server-authoritative** over UDP. Single-player has been retired; the
client `Open()`s a `ReplicationClient` against a dedicated `Server/` host and
renders only replicated, interpolated state. All shared logic is headless and
unit-tested (163 tests, CI-green on the Windows runner); the client builds are
CI-verified and visually validated by the project owner.

| Phase | Status | Notes |
|---|---|---|
| **0** Baseline & CI | ✅ Done | Windows CI (`ci.yml`), headless test suite (`Tests/`, 163 tests), golden runs. |
| **A0** Legacy de-naming | ✅ Done | `universe`→`local_objects`, etc. |
| **A1** Render seam | ✅ Done | `RenderQueue` / `ActiveRenderQueue`; sim emits draw commands. |
| **A2** ECS de-globalization | ✅ Done | In-house `Neuron::ECS::Registry` (sparse-set, generational ids). |
| **A3** `int64³` + floating origin | ✅ Done | `Vector3i64`, `Spatial::Grid`, camera-relative render. |
| **A4** `GameLogic` (server-only) | ✅ Done | Flight, combat, economy, galaxy gen, spawning, stations — headless. |
| **B** Server host + BotClient | 🟡 Partial | `Server/` runs a ~30 Hz authoritative loop with sessions/AOI. **BotClient exe not built yet.** |
| **C** UDP + reliability + intent | ✅ Done | `NetLib` UDP, `ReliableChannel` (seq/ack/resend), `ClientInput` intent protocol. |
| **D** Replication & AOI | 🟡 Partial | Tactical AOI (`AreaOfInterest`), MTU-bounded reliability-free snapshots (`SnapshotPacketizer`). **No strategic tier, delta-compression, or quantization yet.** |
| **E** Client smoothing | 🟡 Partial | `SnapshotInterpolator` + floating origin done. **Dead-reckoning is minimal; local starfield drifts from the replicated world (no client-side prediction).** |
| **F** Persistence (SQL Server) | 🔴 Not started | **The main remaining blocker — a server restart wipes all player state.** |
| **G** Multiplayer gameplay | 🟡 Partial | Server-authoritative combat, trading, docking, multi-client sessions, police/wanted, **procedural galaxy + galactic/short-range charts + teleport**, **homing missiles** all done. **No player names / chat UI; minimal PvP rules. Several legacy mechanics still missing — see §7 for the full parity backlog (bounty, shields, loot/scooping, explosions, NPC AI, …).** |
| **H** Hardening & scale to 100 | 🔴 Not started | Needs BotClient load harness. |
| **I** Operations | 🔴 Minimal | Console logging only. |

### What works end-to-end today
- Connect → server spawns your ship, hands back your entity id (`AssignPlayer`)
  and the **galaxy manifest**; client renders the replicated world in the ship's
  camera frame (rolling/pitching rotate the view).
- Server-authoritative **flight** (intent → caps clamp → `int64` integrate),
  **combat** (forward fire, police response to crimes), **NPC spawning**.
- **Docking** (proximity + facing), **trading/equipping** against each station's
  own market, clean **undock** ejection.
- **Procedural 256-system galaxy**, each with a planet + orbiting station/market;
  **galactic chart** (F5) plots the server manifest; **teleport** (hyperspace key
  while docked) jumps to the system at the crosshair, server-validated.

### Top open items (see §5 for the full list)
1. **Persistence (Phase F)** — nothing survives a server restart. Highest-value
   next step for a real MMO.
2. **BotClient + 100-player load test** (Phase B/H).
3. **Strategic AOI tier + delta/quantization** (Phase D) — bandwidth scaling.
4. **Client prediction / smoothing** — close the starfield-vs-replicated rotation
   drift (Phase E).
5. **Player identity & chat UI** (Phase G) — names/factions across players; the
   `Chat` event type exists but has no UI.
6. **Delete remaining dead single-player code** now the replicated path is proven.
7. **Server gameplay parity backlog (§7)** — legacy mechanics with no server
   counterpart yet: bounty/credits, shields+regen, loot drops + scooping,
   explosions, real NPC AI, collisions, hyperspace fuel/witchspace, ECM, missions.

---

## 0. Locked design decisions

| Topic | Decision |
|---|---|
| Platform | **Windows** client **and** Windows server |
| Authority | **Server-authoritative** (clients send intent commands, render replicated state) |
| Game trajectory | Logic evolves over time from space-flight toward a **4X / RTS** style (less twitch). The architecture deliberately generalizes the single-avatar, single-tick, single-interest-radius assumptions now so the pivot is an extension, not a rewrite (see §2.4). |
| World | **Massive *seamless* open world** — no visible segments or loading screens. One continuous **absolute `int64³`** coordinate space, with an **invisible internal spatial partition** (cells) underneath for interest management and future multi-process sharding (seamless cross-boundary hand-off). |
| Coordinates | Server stores absolute `int64³`; client uses a **floating origin** (per active region) for its 32-bit render math. |
| Identity | **Account → Empire/Faction → owns N entities.** A player is *not* bound to one avatar/ship (4X-ready); the current single ship is just one owned entity. Camera & interest are **view-driven**, not avatar-driven. |
| Input | **Command/intent protocol** (validated orders with costs/preconditions), reusing the game's existing intent state (`flight_roll`/`flight_climb`/`flight_speed`/fire). Covers today's flight and tomorrow's unit orders; natural anti-cheat boundary. |
| Streaming | **Multi-resolution Area-of-Interest** — a high-detail *tactical* tier (entities near the view) plus a low-detail *strategic* tier (territory/fleet summaries across the known galaxy). |
| Transport | **Raw winsock UDP** + a custom reliability/ordering layer (built on NeuronCore's existing winsock) |
| Persistence | **Microsoft SQL Server** (accounts, ships, inventory, market, world state) |
| Wire format | **Hand-rolled binary** for the hot path (snapshots/input); JSON (NeuronCore::Json) for cold path (handshake/config) |
| Entity model | **ECS** — an **in-house** Entity Component System in NeuronCore; the de-globalized `Universe` *is* the ECS world (introduced ECS-first at A2, not bolted on later). Indexed **spatially *and* relationally** (ownership/faction/group/tag) for RTS-style queries. |
| Sim cadence | **Decoupled rates**: simulation tick, command intake, and replication are separate clocks (sim can run slow/RTS-friendly; tactical replicates faster than strategic). |
| Logic boundary | **`GameLogic` is the only game-logic library — server-only** (motion/physics + AI, economy, combat, spawning). **No shared game-logic library.** The client is a **thin presentation layer** (interpolation + dead-reckoning) and shares only **data/protocol schemas** (in `NeuronCore`), never behavior. |
| Test harness | A **headless `BotClient`** (no render/audio) drives scripted/AI bots over the real net stack for load/soak testing (esp. the 100-player test). |
| Code standards | Follow the repo's **`.github/coding-standards.md`** as the single source of truth (see §2.1). |
| Docs location | All project docs live in **`docs/`** (matches the standards' layout). |
| "Modernize client" scope | **Decouple simulation from rendering + de-globalize state into the ECS**; keep the faithful wireframe look |

---

## 1. Current-state analysis (grounded in the code)

### What exists
- **`DeepspaceOutpost/`** — the complete game. Ported C logic (`main`, `space`,
  `threed`, `swat`, `docked`, `shipdata`, `pilot`, `trade`, `missions`, …) plus
  a modern C++23 **`platform/`** layer (DX11, XAudio2, Win32 input/dialog/font/image).
- **`NeuronCore/`** — shared engine lib: tasks/threads (`TasksCore`), timers
  (`TimerCore`), JSON, filesystem, math. **Already includes `winsock2`/`ws2tcpip`.**
  Currently WinRT-bound, which is fine given Windows-only servers.
- **`NeuronClient/` / `NeuronServer/`** — placeholder libs (just include NeuronCore).
- **`Server/`** — placeholder console exe (a 10-second timer loop).
- Build: CMake + C++23 + **MSVC/Windows only**.

### The five migration blockers
1. **Global singleton state, sized for one player.** `elite.h` exposes
   `extern struct commander cmdr;`, `extern struct player_ship myship;`,
   `extern struct local_object local_objects[MAX_LOCAL_OBJECTS];` (renamed from
   the old `universe[]`) and many loose flight
   globals (`flight_speed`, `front_shield`, `energy`, `mcount`, …). There is no
   "player N" — the whole program *is* one player.
2. **`MAX_LOCAL_OBJECTS == 20`, local-system-only.** The `local_objects` array
   is a tiny fixed set of the objects around one player. An open `int64³` world needs a
   **dynamic, ECS-backed entity store** with spatial indexing (§2.3).
3. **Simulation and rendering are fused.** Measured `gfx_*` call counts:

   | File | Role | `gfx_` calls | Disposition |
   |---|---|---:|---|
   | `pilot.cpp` | NPC AI | **0** | → server sim core, ~as-is |
   | `trade.cpp` | economy/market | **0** | → server sim core, ~as-is |
   | `elite.cpp` | state | **0** | → split: data to core, screens to client |
   | `planet.cpp` | planet gen | **0** | → server (gen) + client (render) |
   | `shipface.cpp` | face/normal data | **0** | → shared data |
   | `swat.cpp` | combat/tactics | **6** | → split: tactics to server, draw to client |
   | `threed.cpp` | 3D projection + raster | **9** | → render-only (client) |
   | `stars.cpp` | starfield | **15** | → client (cosmetic) |
   | `space.cpp` | **universe sim + draw** | **29** | → **surgical split** (the hard one) |
   | `missions.cpp` | mission UI text | **36** | → client UI |
   | `docked.cpp` | station/trade UI | **98** | → client UI |

   `update_local_objects()` in `space.cpp` both **moves** objects and **draws**
   them in one pass — this fusion is the core thing to break.
4. **Local-file persistence.** `file.cpp` saves the 256-byte `commander` block
   with `fopen/fwrite`. Must become server-side authoritative storage in MS SQL.
5. **Deterministic, integer physics & procedural galaxy** (`random.cpp` seeds,
   integer/fixed-point math). This is the **good news** — it's reproducible and
   cheap to replicate, ideal for an authoritative server.

---

## 2. Target architecture

Modules and their dependency direction (lower layers never depend on higher
ones — per the standards' *Layers and Dependencies* rule):

**No shared game-logic library.** All game *behavior* lives in one place —
`GameLogic`, **server-only**. The client shares only **data** with the server
(ECS component layouts, the wire-protocol structs, static ship-data tables),
which live in `NeuronCore`. The client runs **no game rules** at all.

| Module | Kind | Role | Depends on |
|---|---|---|---|
| `NeuronCore` | lib | Engine foundation + **shared data only**: the **ECS** container, the component & wire-protocol **schemas** (`Transform`, `Motion`, `ShipDef`…), static ship-data tables, math/`Vector3i64`, tasks, timers, raw-UDP `NetLib`, `DataReader`/`DataWriter`. **No behavior.** | — |
| `GameLogic` | lib | **SERVER-ONLY — all game behavior**: the motion/physics integration *system*, AI/tactics, economy/market, combat resolution, missions, spawning/encounters. Headless, no `gfx`. | `NeuronCore` |
| `NeuronClient` | lib | Client engine: D3D11 graphics, audio, input, GUI **plus** client networking (reliability, **snapshot interpolation + dead-reckoning** — presentation only, no game rules). **Does not link `GameLogic`.** | `NeuronCore` |
| `NeuronServer` | lib | Server net + AOI/replication + persistence (MS SQL). | `GameLogic` |
| `DeepspaceOutpost` | **exe** | Game client: main loop, game-specific rendering (wireframe/HUD via render queue), input, UI. | `NeuronClient` |
| `BotClient` | **exe** | **Headless test client** — scripted/AI bots, **no render/audio**, for heavy load & soak testing. Same net stack as the real client. | `NeuronClient` |
| `Server` | **exe** | Dedicated server host: loop, sessions, fixed-tick scheduler. | `NeuronServer`, `GameLogic` |

```
  client input  ──UDP──▶   Server   ──UDP snapshots──▶  client / BotClient
   (DeepspaceOutpost or BotClient)    (the ONLY game logic: GameLogic over the ECS)
   client renders interpolated + dead-reckoned snapshots — it runs no game rules
```

**Thin presentation client (answers "no shared game-logic library, please"):**
the client is **fully server-authoritative and runs no game logic**. It forwards
input and renders **interpolated authoritative snapshots**, smoothed by
**dead-reckoning** (extrapolate last position along last velocity — pure
presentation, not a game rule). The *only* thing client and server share is
**data**: the component/protocol schemas needed to (de)serialize snapshots, in
`NeuronCore`. This is the EVE-Online-style model. The trade-off is ~RTT input
latency, softened by dead-reckoning, immediate *cosmetic* view response, and
**server-side lag compensation** for weapons (Phase E / G).

**Headless BotClient (answers "can I have a headless client for bot testing?"):**
yes — and it falls out almost for free once A1 makes the sim headless. `BotClient`
links the *same* `NeuronClient` net stack as the real game but swaps the
DX11/audio/UI front-end for a **null render sink** and a **bot-input driver**
(scripted flight paths or simple AI that reads the incoming snapshots). It
connects over the real UDP protocol, so N bot processes (or N bots per process)
drive genuine server load — the harness used for the 100-player test in Phase H.

### 2.1 House style (single, project-wide)

The **single source of truth** is the repo's **`.github/coding-standards.md`**.
It was updated so **functions/methods are `PascalCase`** (matching the NeuronCore
engine, which becomes the reference for the house style). Summary:

| Element | House style | Example |
|---|---|---|
| Namespaces | `PascalCase`, nested under `Neuron::` | `Neuron::GameLogic` |
| Types (class/struct/enum) | `PascalCase`; `enum class` w/ explicit base | `Universe`, `RenderQueue`, `EntityId`, `enum class ShipType : int8_t` |
| Methods & free functions | **`PascalCase`** | `Tick()`, `AddEntity()`, `Normalize()` |
| Parameters | `_camelCase` (Neuron convention) | `_value`, `_deltaTime` |
| Member variables | `m_` + `camelCase` | `m_running`, `m_entities` |
| Locals | `camelCase` | `entityCount` |
| Constants / macros / `constexpr` | `UPPER_SNAKE_CASE` | `ENGINE_VERSION`, `MAX_ENTITIES` |
| Integer world vector | `PascalCase`, matches `Math::Vector3` | **`Vector3i64`** (not `Vec3i64`) |
| New file names | `PascalCase.cpp/.h` for engine types/modules; keep `snake_case` when editing existing ported files | `Universe.h`, vs. `space.cpp` |

**Outliers to migrate** (folded into the phases, not a big-bang rewrite):
- **Platform layer** is the main non-conformant code now: `camelCase` methods
  (`Renderer::init` → `Init`, `clearCanvas` → `ClearCanvas`), trailing-underscore
  members (`palette_` → `m_palette`), and `kPascalCase` constants
  (`kCanvasWidth` → `CANVAS_WIDTH`). Converted file-by-file as the render seam
  (A1) touches it.
- **Ported Elite logic** stays `snake_case` only while still legacy; each file
  adopts the house style as it is modernized into the engine (A1–A4). New
  functions added to a still-legacy file temporarily match that file's local
  `snake_case` (e.g. `render_local_objects()` beside the renamed
  `update_local_objects()` in `space.cpp`) and are renamed when the file moves
  to the engine.
- **NeuronCore** is already conformant on method casing (`Startup`, `Normalize`).

> **Native-First (from the standards):** new subsystems must add genuine
> behavior, not thinly wrap a native API. The in-house ECS, the UDP
> reliability layer, and the binary serializer all qualify (real abstractions /
> invariants); a bare pass-through around `winsock` would not. Prefer std
> containers except where the ECS storage's measured layout needs justify a
> custom container.

### 2.2 Legacy de-naming — *done first, frees the `Universe` name*

So the **new** `Neuron::Universe` is clean and never confused with the old
20-object local array, the legacy "universe" identifiers were renamed up front
(complete, repo-wide, behaviour-preserving — pure identifier rename):

| Legacy (removed) | Renamed to | Notes |
|---|---|---|
| `struct univ_object` | `struct local_object` | the old per-object record |
| `universe[]` | `local_objects[]` | the fixed local-bubble array |
| `MAX_UNIV_OBJECTS` | `MAX_LOCAL_OBJECTS` | (was 20) |
| `update_universe()` | `update_local_objects()` | move+draw pass (split in A1) |
| `clear_universe()` | `clear_local_objects()` | |
| `move_univ_object()` | `move_local_object()` | |
| `univ` (local ptr) | `obj` | object pointer in `threed`/`swat` |

The name **`Universe`** is now reserved exclusively for the new clean,
house-style global world container introduced in A2/A3.

### 2.3 Entity model — in-house ECS (`Neuron::ECS`)

The de-globalized `Universe` **is** an Entity Component System, built in-house in
NeuronCore (no third-party dependency), introduced **ECS-first at A2**.

- **Why ECS:** the open `int64³` world holds thousands of heterogeneous entities
  (players, NPC ships, missiles, cargo, asteroids, stations, sun/planet). Data-
  oriented component storage gives cache-friendly iteration for the server tick,
  composition replaces the current `if (type == …)` branching in `swat`/`pilot`,
  and **components map 1:1 onto replication** — AOI picks entities, the snapshot
  serializes their *replicated* components as deltas.
- **Design:** sparse-set (or archetype) storage; stable `EntityId` (generational
  handle); **systems run only on the server** in a fixed order each tick.
  Components are plain `PascalCase` structs (`Transform`, `Motion`, `Combat`,
  `Ai`, `ShipDef`, `Renderable`, `NetReplicated`); systems are `PascalCase`
  (`MotionSystem`, `TacticsSystem`) and live in `GameLogic`. **The ECS container
  and the component/schema definitions live in `NeuronCore` (data); the client
  uses the ECS only as a read-only mirror of replicated snapshot state for
  rendering — it runs no systems.**
- **Migration is faithful, not a rewrite:** at A2 each `local_object` becomes one
  entity with *fat* components mirroring the existing fields; the existing loops
  become systems one at a time, each verified against the Phase 0 golden runs.
  Components are split/refined only after behavior is locked.
- **Replication hooks** are part of the ECS from the start: a component marked
  `NetReplicated` is automatically eligible for AOI snapshots (Phase D), so the
  netcode never reaches into game structs directly.

### 2.4 Designed for the 4X / RTS trajectory

The game logic will drift from single-ship space-flight toward a **4X / RTS**
style (many units per player, empire/economy/territory, less twitch). These
principles keep that pivot an *extension* rather than a rewrite — they cost
little now and a lot later:

- **Player ≠ avatar.** Identity is **Account → Empire/Faction → owns N entities**.
  The current ship is just one owned entity. Camera and interest are
  **view-driven** (where the camera looks + what you own), never "the player *is*
  this ship." Avoids the deepest retrofit.
- **Seamless world, partitioned underneath.** One continuous `int64³` space with
  **no visible boundaries**; an invisible cell partition drives interest and
  becomes the **shard boundary** for multi-process scaling, with seamless
  hand-off. Floating-origin is a per-region *render* detail, not world structure.
- **Command/intent input, not per-frame control.** Validated orders (with
  costs/preconditions) — today `flight_roll/climb/speed/fire`; later
  `move/build/attack`. Same server-side intent state either way.
- **Multi-resolution interest.** Tactical (detailed, local) + strategic
  (coarse, galaxy-wide territory/fleet summaries). A single AOI radius cannot
  express "show my whole empire at low detail."
- **Relational ECS indexing.** Index entities by **owner/faction/group/tag** as
  well as space, so "all my units", "enemies in sector 7", "this fleet" are cheap.
- **Decoupled clocks.** Separate sim-tick, command-intake, and replication rates;
  RTS sims can run slow while UI stays responsive via interpolation.
- **Persistent, always-on world.** The world keeps simulating (economy, AI
  empires, territory) while players are offline — persist **world/empire state +
  a command log**, not just per-player rows.
- **Bandwidth is the scaling wall** (units ≫ players): field **quantization +
  delta compression** and per-channel budgets are first-class, not an afterthought.
- **Replication, not lockstep.** Determinism is kept for replays/debugging, but
  100-player scale uses interest-managed **state replication** (classic RTS
  lockstep caps at ~8 players and shatters on one desync).

---

## 3. Phase plan

Each phase is independently shippable/testable. Phases A0–A4 are the
"modernize the client first" work and must land before B+.

### Phase 0 — Baseline & safety net
- Stand up a **build/test path**: CMake build on a Windows runner (this repo is
  MSVC/DX11; it cannot be compiled in the Linux dev container — wire CI early).
- Add a minimal **test harness** (e.g. a headless tick test) so refactors are
  verifiable. Snapshot a few deterministic sim runs as golden references.
- Tag the current single-player build as a known-good baseline.

### Phase A — Modernize & decouple the client *(do this first)*
**Goal: a headless, multi-instance simulation core extracted from rendering,
with no global singletons — without changing on-screen behavior.**

- **A0 — Legacy de-naming *(done).*** Repo-wide, behaviour-preserving rename of
  the old `universe`/`univ_object` identifiers to `local_objects`/`local_object`
  (see §2.2) so the new clean `Neuron::Universe` owns that name. This lands
  first, before any new `Universe` code is written.
- **A1 — Render seam.** Stop game logic from calling `gfx_*` directly. Introduce
  a `RenderQueue` (list of draw commands: line/polygon/sprite/text). The sim
  *emits* draw commands; the platform layer consumes them. Convert `space.cpp`,
  `threed.cpp`, `swat.cpp` so `update_local_objects()` does **move-only**, and a
  new `render_local_objects()` walks the entity list to emit draw commands.
- **A2 — De-globalize into `Neuron::Universe`, which *is* an ECS world.**
  Instead of a plain container that gets replaced later, the de-globalized
  `Universe` is the **in-house ECS registry from the start** (see §2.3). Each
  legacy `local_objects[i]` becomes **one entity**; the `local_object` fields
  migrate into a few *fat* components first (`Transform`, `Motion`, `Combat`,
  `Ai`, `ShipDef`, `Renderable`) so the data layout is faithful before it is
  refined. `cmdr`/`myship`/flight globals become components on a **player
  entity**. The loops (`update_local_objects`, `tactics`, AI from `pilot.cpp`)
  are converted into **systems**, one at a time, each guarded by the Phase 0
  golden-run tests. Mechanical and large; do it file-by-file behind the A1 seam.
- **A3 — `int64³` coordinates + floating origin.** Make `Transform.position`
  an absolute **`Vector3i64`** component; add a uniform-grid/loose-octree
  **spatial index** over the ECS world (reused later for AOI). Implement the
  **floating-origin** transform: render/physics math stays 32-bit relative to
  the local ship while the authoritative world position is `int64`. Keep the
  deterministic integer physics. (Smaller now that A2 already built the ECS.)
- **A4 — Extract `GameLogic` (server-only, *all* game behavior).** One headless
  library (no DX11/audio, fixed-timestep tick) holding **every game rule**: the
  motion/physics integration *system*, AI/tactics (`pilot`, `swat` resolution),
  economy (`trade`), spawning/encounters, missions, damage/legal status. Move the
  already-pure files (`pilot`, `trade`, `planet` gen, `elite` data) in first,
  then the systems split out of `space`/`swat`. The **ECS container + the
  component/protocol schemas + ship-data tables stay in `NeuronCore`** as shared
  *data*; **the client links none of `GameLogic`.** There is **no `GameShared`
  library** — no game behavior is shared.
- **Deliverable:** single-player game still runs identically, but now on top of a
  headless, de-globalized, `int64`-world ECS — with **all behavior isolated in
  the server-only `GameLogic`** and the client reduced to data + rendering.
  **This is the keystone.**

### Phase B — Server foundation + headless BotClient
- Flesh out **`Server/`**: a real host loop with a **fixed-tick scheduler**
  (e.g. 20–30 Hz sim), session manager, and a console/admin surface. Drop the
  placeholder 10-second timer.
- Link `NeuronServer` → `GameLogic`; run one authoritative `Universe`.
- Spawn NPCs/economy server-side using the existing deterministic generators.
- **Stand up the `BotClient` exe** (`NeuronClient`, **no render/audio**): a null
  render sink plus a bot-input driver (scripted flight first, simple AI reading
  the incoming snapshots later). Even before the network exists it runs against
  an in-process loopback server — giving an automated, headless integration test
  from day one, and the foundation for the Phase H load test.

### Phase C — Networking (raw winsock UDP + reliability)
- **UDP datagram endpoint** in NeuronCore (non-blocking, IPv4/IPv6). Calls
  `winsock` directly per *Native-First* — it earns its keep by owning the
  non-blocking setup and feeding the reliability layer, not by thinly renaming
  `sendto`/`recvfrom`.
- **Custom reliability layer**: sequence numbers, ack/ack-bitfield, retransmit,
  ordered + unordered channels, fragmentation/reassembly for large snapshots,
  connection handshake, heartbeat/timeout, basic congestion pacing. *RTS-leaning
  scope:* the workhorse is a **reliable-ordered command channel** + periodic
  state — the latency-tolerant traffic doesn't need full twitch-FPS unreliable-
  state machinery, which keeps this layer's scope contained.
- **Command/intent protocol** (not per-frame control): validated order messages
  (today `flight_roll/climb/speed/fire`; later `move/build/attack`) the server
  applies to the sender's intent components. **Versioned** message structs
  (login, command, snapshot, event, chat) so schemas can evolve as the game does.
  Hand-rolled binary hot path; JSON only for handshake/config.
- **Loopback harness** first (client+server in one process) before going to wire.

### Phase D — Replication & Area-of-Interest (for the seamless `int64` world)
- **Invisible cell partition** over the `int64³` world (uniform grid / hashed
  cells, or a loose octree) — the player never sees boundaries; cells drive
  neighbor queries (O(local)) and are the future **shard** unit with seamless
  cross-cell hand-off.
- **Multi-resolution AOI**: a **tactical** tier (full entity state near the view)
  *and* a **strategic** tier (coarse territory/fleet summaries across the known
  galaxy). Send **delta snapshots** per tier; strategic replicates slower.
- **Entity lifecycle events**: enter-AOI (full state) / leave-AOI (despawn).
- **Wire efficiency**: **quantize** fields (positions/angles) and **delta-compress**
  against the last acked baseline — bandwidth (units ≫ players) is the scaling wall.
- **Priority/bandwidth budgeting** per channel so 100 players stay within limits.

### Phase E — Client smoothing (interpolation + dead-reckoning, *no* prediction)
- The client runs **no game logic**. It renders **interpolated** authoritative
  snapshots (in the past by ~100 ms) for all entities, including the local ship.
- **Dead-reckoning** extrapolates an entity's rendered transform along its last
  known velocity between snapshots — pure presentation smoothing, not a game
  rule; it never simulates handling, thrust, or collision.
- **Immediate cosmetic input feedback** (e.g. nudging the view) to mask the
  ~RTT control latency without claiming authority.
- **Floating origin on the client** so the camera/render stay in 32-bit space
  while the world is `int64`.
- *(Weapons fairness — hit detection under latency — is handled by server-side
  lag compensation in Phase G, not by client prediction.)*

### Phase F — Persistence (Microsoft SQL Server) — *persistent, always-on world*
- **Data-access layer** in `NeuronServer` (ODBC / SQL Server Native Client),
  kept behind an interface so it's mockable and swappable.
- **Schema**: accounts/auth, **empire/faction** state, owned entities (migrate the
  256-byte `commander` block into normalized tables), ship/unit loadouts,
  inventory/cargo, **territory & economy/market** state, **command log** (for
  replay/audit), audit/anti-cheat log.
- **The world simulates while players are offline** (economy, AI empires,
  territory) — persistence captures **world/empire state**, not just per-player
  rows. The server is an always-on world sim, not a session host.
- **Async, batched writes** off the sim thread (snapshot the dirty set, flush on
  a worker) so DB latency never stalls the tick. **Never** write transient
  per-tick entity positions to SQL — hot state stays in memory + periodic snapshots.
- **Login flow**: authenticate → load empire → attach to owned entities; periodic
  + on-logout save.

### Phase G — Multiplayer gameplay
- **Shared combat** (server resolves hits/damage/kills authoritatively;
  `swat.cpp` tactics run server-side; lasers/missiles are server events).
- **Shared economy/trading** (server owns market prices and station stock; trade
  becomes a validated request/response).
- **Player visibility & identity** (names, factions, legal status across players).
- **Chat / social**, **docking with shared stations**, basic **PvP rules**.

### Phase H — Hardening & scale to 100
- **Load test** with 100 `BotClient` instances (the Phase B headless bot harness)
  driving input over the real UDP protocol.
- Profile tick time, bandwidth/player, DB throughput; tune AOI radius, snapshot
  rate, and grid cell size.
- **Anti-cheat**: input validation, rate limits, server-side bounds checks
  (authority already blocks most movement/teleport cheats).
- **Resilience**: reconnect/resume, graceful disconnect, server save-on-crash.

### Phase I — Operations
- Server config (ports, tick rate, DB connection), structured logging/metrics,
  versioned protocol with a handshake check, build/deploy scripts, backups.

---

## 4. Key technical design notes

- **Seamless `int64³` world + floating origin.** Absolute positions are `int64`
  to span a huge field without precision loss. The world is **seamless** — no
  visible boundaries — but an **invisible cell partition** underneath drives
  interest and future sharding. Each client (and the sim's render pass) rebases
  to a local origin near the view so existing 32-bit/fixed-point math is reused
  unchanged. Rebase when the view crosses a cell boundary.
- **Determinism.** Keep the integer/fixed-point physics; it keeps the
  authoritative server sim reproducible (replays, golden tests, bug reports) and
  AOI snapshots stable. Avoid float nondeterminism in the server sim paths.
- **From 20 objects to thousands.** `MAX_LOCAL_OBJECTS=20` is replaced by the
  ECS world (§2.3); AOI keeps the *per-client* working set small even though the
  world total is large.
- **Decoupled clocks.** Separate **sim-tick**, **command-intake**, and
  **replication** rates rather than one number. Flight starts ~20–30 Hz sim; an
  RTS-leaning sim can run slower while the UI stays responsive via interpolation.
  Tactical replication is faster than strategic. Clients **interpolate** between
  snapshots (and dead-reckon for smoothness) — they never simulate game rules.
- **Reuse what's already clean.** `pilot/trade/planet/elite` (0 `gfx_` calls)
  move to the sim core almost verbatim — start there to build momentum.

---

## 5. Risks & open items
- **MSVC/DX11-only build** can't be compiled or run in this Linux container —
  CI on a Windows runner (Phase 0) is required to verify every phase.
- **A1/A2 are the largest, riskiest refactor** (touching `space.cpp` +
  ~260 `gfx_` call sites). Do it incrementally behind the render seam with the
  golden-run tests guarding behavior.
- **Reliability layer over raw UDP is non-trivial** — budget real time for
  retransmit/ordering/fragmentation and fuzz/packet-loss testing.
- **4X-readiness is designed-in, not built-out.** §2.4 generalizes the
  single-avatar / single-tick / single-interest assumptions so the RTS pivot is
  an extension — but the *flight game ships first*. Don't gold-plate empire/RTS
  systems before the single-player decouple (Phase A) lands.
- **World partition is invisible but real.** The seamless world hides cell
  boundaries; the hand-off logic (and eventual cross-process meshing) is genuine
  engineering — budget for it when sharding becomes necessary, not on day one.
- **Open balance/design questions** to resolve before Phase G: PvP rules &
  safe zones, death/insurance, empire/faction ownership rules, and the starting
  cell size / interest radii / max entities per AOI tier.

---

## 6. Suggested immediate next step
Phases 0 and A are complete, and the server split (B–E) plus most of the
multiplayer gameplay (G) are working end-to-end (see the **Implementation
status** section at the top). The decoupling keystone is behind us.

The highest-value next step is **Phase F — Persistence (Microsoft SQL Server)**:
right now a server restart wipes every player's wallet, cargo, equipment,
position and wanted level, so nothing is durable. Stand up the data-access layer
in `NeuronServer`, normalize the `commander` block into account/empire/entity
tables, and wire **load-on-connect / async save** around the session lifecycle
(`ServerSessions`). Until state persists, no other gameplay is "real".

Strong runners-up: stand up the **`BotClient`** (Phase B) so the 100-player load
test (Phase H) becomes possible, and add the **strategic AOI tier + delta/
quantization** (Phase D) once persistence lands.

---

## 7. Server gameplay parity — feature gaps (legacy → server)

The migration moved the *architecture* over, but several **legacy gameplay
mechanics have no server-authoritative counterpart yet** (the thin client runs no
game logic, so anything not re-implemented in `GameLogic`/`Server` simply does not
happen). This is the running backlog of those gaps, from a legacy-vs-server audit.
Order is roughly by gameplay impact ÷ effort. **Nothing here is started unless its
row says so.**

> **Two are already written but un-wired:** `Combat.h` ports `ApplyKill` (bounty)
> and `ApplyDamageToShields` (directional shields) faithfully, but **neither is
> called by the live server** — items 1 and 2 are mostly "wire up existing code,"
> not greenfield. Everything else (loot, scooping, collisions, NPC steering,
> witchspace/fuel, missions) has **no server code at all**.

| # | Gap | Legacy (client) | Server state | Effort |
|---|---|---|---|---|
| 1 | **Kill rewards / bounty** — combat currently earns nothing | `space.cpp` `update_local_objects` (`cmdr.credits += bounty` on kill) | **Not wired.** `Combat.h ApplyKill` exists but is never called; the kill loop in `Server/Main.cpp` destroys the wreck without crediting the killer's `Wallet` | **Low** (wire `ApplyKill` + a per-`NetType` bounty table) |
| 2 | **Player shields + energy regen** — survivability is all-or-nothing | `space.cpp` `regenerate_shields` / `damage_ship` (front/aft shields + energy bank) | **Not wired.** Player has only a flat `Combatant.energy=255`; `Combat.h ApplyDamageToShields` exists but is unused; no `ShieldState` component, no regen tick | Medium |
| 3 | **Loot drops + scooping** — the "kill → loot → sell" loop is absent | `swat.cpp` `launch_loot`/`check_target` (drops canisters/alloys/rock); `trade.cpp` `scoop_item` (`distance < 170`, needs fuel scoop) | **None.** No loot/cargo entity is ever spawned; `Equipment.fuelScoop` is purchasable but inert; no proximity-collect logic | Medium |
| 4 | **Explosion / debris VFX** — killed ships just vanish | `swat.cpp` `explode_object` + `draw_ship` debris (`exp_seed`/`exp_delta`) | **None.** Server only `Destroy()`s + sends `EntityDeath`; no explosion entity | **Low** (client-side VFX on the existing `EntityDeath` event — no server entity needed) |
| 5 | **Real NPC AI movement** — NPCs are stationary turrets | `swat.cpp` `tactics` + `pilot.cpp` (pursue, flee at low energy, escape pods, NPC missiles, station-launched Vipers, traders/Thargoids) | **Minimal.** `StepCombat` only makes NPCs fire on the nearest enemy in range; `SpawnDirector` spawns generic pirates on a timer. No pursuit/flee/missiles. *(Also: spawned NPCs get no `NetType`, so they render as the default ship.)* | High |
| 6 | **Collisions** — planets/stations/ships are pass-through | `space.cpp` `check_docking` (crash), `trade.cpp` collision (`distance < 170` → damage), planet altitude | **None.** Combat tick only does ranged distance checks; no contact damage | Medium |
| 7 | **Hyperspace fuel / witchspace / in-system jump** — travel is free + instant | `space.cpp` `complete_hyperspace` (deduct `cmdr.fuel`), `enter_witchspace` (Thargoid ambush), `jump_warp` (mass-lock) | **Differs.** Docked-only instant `Teleport` (`StationServices.h`) with no fuel cost, witchspace, mass-lock, or in-flight jump; no fuel resource server-side | Medium |
| 8 | **ECM** — now meaningful since missiles fly | `swat.cpp` `activate_ecm`/`time_ecm` (destroys in-flight missiles) | **None.** `Equipment.ecm` purchasable but inert; could cancel locked missiles in range | Low |
| 9 | **Mission system** — progression/content | `missions.cpp` (Constrictor hunt, Thargoid invasion; dock-time briefings; `cmdr.mission`) | **None.** No mission state, briefings, or scripted targets server-side | High |
| 10 | **Inert purchased equipment & misc.** | energy bomb (`detonate_bomb`), escape pod (`abandon_ship`), cabin temp / sun damage (`update_cabin_temp`), laser heat (`fire_laser`), contraband legal-status accrual | **None / partial.** Several items are buyable in `StationServices` but have no runtime effect; `Wanted` is a coarse level bump on firing at police/station, with no contraband scanning or decay | Varies |

**Already done this migration (for contrast):** server-authoritative flight (with
correct ship-local pitch/roll), forward-laser combat + police/wanted response,
**homing missiles as real projectiles** (`MissileSystem`), docking (low-speed
gate), per-station markets/trading, procedural galaxy + galactic/short-range
charts + teleport.

**Suggested order when we pick this up:** 1 (bounty) and 4 (explosions) are the
cheapest wins; 2 (shields) and 3 (loot + scooping) restore the core combat
survivability and reward loop; 5–10 are larger and can follow persistence
(Phase F).
